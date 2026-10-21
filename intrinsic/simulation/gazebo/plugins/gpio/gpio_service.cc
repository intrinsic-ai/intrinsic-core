// Copyright 2026 Intrinsic Innovation LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "intrinsic/simulation/gazebo/plugins/gpio/gpio_service.h"

#include <cstdlib>
#include <functional>
#include <string>
#include <utility>

#include "absl/algorithm/container.h"
#include "absl/cleanup/cleanup.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gloop/util/gtl/iterator_adaptors.h"
#include "grpc/grpc.h"
#include "grpcpp/security/server_credentials.h"
#include "grpcpp/server_builder.h"
#include "grpcpp/server_context.h"
#include "grpcpp/support/status.h"
#include "grpcpp/support/sync_stream.h"
#include "grpcpp/support/time.h"
#include "intrinsic/hardware/gpio/gpio_service_proto_utils.h"
#include "intrinsic/hardware/gpio/v1/gpio_service.pb.h"
#include "intrinsic/hardware/gpio/v1/signal.pb.h"
#include "intrinsic/simulation/gazebo/plugins/gpio/gpio_connection_interface.h"
#include "intrinsic/util/proto_time.h"
#include "intrinsic/util/status/status_conversion_grpc.h"
#include "intrinsic/util/status/status_conversion_rpc.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic {
namespace simulation {
namespace {
constexpr absl::Duration kWaitForValueInterval = absl::Milliseconds(200);
constexpr int kMaxRetriesForNotFoundSignals = 30;
}  // namespace

::grpc::Status GPIOService::GetSignalDescriptions(
    ::grpc::ServerContext* context,
    const intrinsic_proto::gpio::v1::GetSignalDescriptionsRequest* request,
    intrinsic_proto::gpio::v1::GetSignalDescriptionsResponse* response) {
  absl::MutexLock lock(connections_mutex_);
  for (const auto& connection : gtl::value_view(plugin_connections_)) {
    const auto descriptions = connection->GetSignalDescriptions();
    for (const auto& description : gtl::value_view(descriptions)) {
      *response->add_signal_descriptions() = description;
    }
  }
  return ::grpc::Status::OK;
}
::grpc::Status GPIOService::ReadSignals(
    ::grpc::ServerContext* context,
    const intrinsic_proto::gpio::v1::ReadSignalsRequest* request,
    intrinsic_proto::gpio::v1::ReadSignalsResponse* response) {
  absl::MutexLock lock(connections_mutex_);
  absl::flat_hash_set<std::string> request_names{
      request->signal_names().begin(), request->signal_names().end()};

  intrinsic_proto::gpio::v1::SignalValueSet result_signal_values;
  for (const auto& connection : gtl::value_view(plugin_connections_)) {
    const ::intrinsic_proto::gpio::v1::SignalValueSet signal_values =
        connection->GetSignalValues();
    for (const auto& [name, value] : signal_values.values()) {
      if (request_names.contains(name)) {
        result_signal_values.mutable_values()->insert({name, value});
      }
    }
  }

  // Check if there are unread request names
  for (const auto& name : gtl::key_view(result_signal_values.values())) {
    request_names.erase(name);
  }
  if (!request_names.empty()) {
    return ToGrpcStatus(absl::NotFoundError(
        absl::Substitute("Signal with names [$0] doesn't have readable "
                         "corresponding readable values",
                         absl::StrJoin(request_names, ", "))));
  } else {
    response->mutable_signal_values()->Swap(&result_signal_values);
  }

  return ::grpc::Status::OK;
}
::grpc::Status GPIOService::WaitForValue(
    ::grpc::ServerContext* context,
    const intrinsic_proto::gpio::v1::WaitForValueRequest* request,
    intrinsic_proto::gpio::v1::WaitForValueResponse* response) {
  const absl::Time deadline =
      grpc::TimeFromGprTimespec(context->raw_deadline());
  while (absl::Now() < deadline) {
    if (context->IsCancelled()) {
      return ::grpc::Status(
          grpc::StatusCode::CANCELLED,
          absl::Substitute("Cancelled while waiting for value $0", *request));
    }

    auto poll_result = PollMatchingValues(*request);
    if (poll_result.ok()) {
      response->Swap(&poll_result.value());
      return ::grpc::Status::OK;
    } else if (poll_result.status().code() != absl::StatusCode::kNotFound) {
      // TODO(b/244454115): use the same error code instead of internal.
      return ::grpc::Status(grpc::StatusCode::INTERNAL,
                            poll_result.status().ToString());
    }
    absl::SleepFor(kWaitForValueInterval);
  }

  return ::grpc::Status(
      grpc::StatusCode::DEADLINE_EXCEEDED,
      absl::Substitute("Deadline exceeded while waiting for value $0",
                       *request));
}
::grpc::Status GPIOService::OpenWriteSession(
    ::grpc::ServerContext* context,
    ::grpc::ServerReaderWriter<
        intrinsic_proto::gpio::v1::OpenWriteSessionResponse,
        intrinsic_proto::gpio::v1::OpenWriteSessionRequest>* stream) {
  ::intrinsic_proto::gpio::v1::OpenWriteSessionRequest request;
  if (!stream->Read(&request)) {
    return ToGrpcStatus(absl::AbortedError("Failed to read initial request."));
  }
  if (!request.has_initial_session_data()) {
    return ToGrpcStatus(absl::InvalidArgumentError(
        "Initial OpenWriteSessionRequest is missing initial_session_data."));
  }

  const absl::flat_hash_set<std::string> signal_names(
      {request.initial_session_data().signal_names().begin(),
       request.initial_session_data().signal_names().end()});

  LOG(INFO) << "OpenWriteSession for signals "
            << absl::StrJoin(signal_names, ", ");

  intrinsic_proto::gpio::v1::OpenWriteSessionResponse initial_response;
  initial_response.mutable_status()->set_code(grpc::StatusCode::OK);
  if (absl::Status claim_status = ClaimSignals(signal_names);
      !claim_status.ok()) {
    initial_response.mutable_status()->set_code(
        grpc::StatusCode::PERMISSION_DENIED);
    initial_response.mutable_status()->set_message(claim_status.message());
  }
  absl::Cleanup release_signals = [&signal_names, this] {
    ReleaseSignals(signal_names);
  };

  if (!stream->Write(initial_response)) {
    return ToGrpcStatus(
        absl::AbortedError("Failed to write initial response to the client."));
  }

  if (initial_response.status().code() != grpc::StatusCode::OK) {
    return ToGrpcStatus(
        absl::AbortedError("Initial status is not valid for writing"));
  }

  while (stream->Read(&request)) {
    if (request.has_initial_session_data()) {
      return ToGrpcStatus(absl::FailedPreconditionError(
          "Received initial session data when Session is already "
          "initialized."));
    }

    absl::Status status_result;
    switch (request.action_request_case()) {
      case ::intrinsic_proto::gpio::v1::OpenWriteSessionRequest::
          ActionRequestCase::kWriteSignals: {
        absl::Status write_status = WriteSignals(request.write_signals());
        if (!write_status.ok()) {
          // Sometimes right after simulation reset, when we receive the write
          // status, the gripper is not instantiated, wait for the gripper to
          // get ready.
          for (int retry_count = 0;
               retry_count < kMaxRetriesForNotFoundSignals &&
               write_status.code() == absl::StatusCode::kNotFound;
               ++retry_count) {
            // TODO(b/244454115): Figure if there is a better way to communicate
            // with the simulator instead of sleeping.
            absl::SleepFor(absl::Seconds(1));
            write_status = WriteSignals(request.write_signals());
          }
          if (!write_status.ok()) {
            auto result = absl::AbortedError(absl::Substitute(
                "Write signals failed with error $0", write_status.ToString()));
            LOG(ERROR) << result.message();
            return ToGrpcStatus(result);
          }
        }
        break;
      }
      case ::intrinsic_proto::gpio::v1::OpenWriteSessionRequest::
          ActionRequestCase::ACTION_REQUEST_NOT_SET: {
        status_result = absl::InvalidArgumentError("No request was provided.");
      } break;
      default: {
        status_result = absl::UnimplementedError("Request type not handled.");
      }
    }
    if (!status_result.ok()) {
      LOG(ERROR) << "Session error: " << status_result.message();
    }

    intrinsic_proto::gpio::v1::OpenWriteSessionResponse response;
    (*response.mutable_status()) = SaveStatusAsRpcStatus(status_result);

    if (!stream->Write(response)) {
      return ToGrpcStatus(absl::AbortedError(
          "Failed to write streaming response to the client."));
    }
  }

  return ::grpc::Status::OK;
}

absl::Status GPIOService::RegisterPluginConnection(
    GPIOConnectionInterface* plugin_connection) {
  if (plugin_connection == nullptr) {
    return absl::InvalidArgumentError("plugin_connection cannot be null");
  }
  absl::MutexLock lock(connections_mutex_);
  if (plugin_connections_.contains(plugin_connection->plugin_handle())) {
    return absl::AlreadyExistsError(
        absl::Substitute("Plugin connection with handle '$0' already exists",
                         plugin_connection->plugin_handle()));
  }
  plugin_connections_.insert(
      {plugin_connection->plugin_handle(), plugin_connection});
  return absl::OkStatus();
}

absl::Status GPIOService::UnregisterPluginConnection(
    absl::string_view plugin_handle) {
  absl::MutexLock lock(connections_mutex_);
  if (!plugin_connections_.contains(plugin_handle)) {
    return absl::NotFoundError(absl::Substitute(
        "GPIO plugin connection $0 requested to be unregistered "
        "but it does not exist",
        plugin_handle));
  }
  plugin_connections_.erase(plugin_handle);
  return absl::OkStatus();
}

void GPIOService::StartServer() {
  const char* server_address = getenv("GPIO_SERVICE_ADDRESS");
  if (server_address == nullptr) {
    server_address = "0.0.0.0:12394";
  }

  grpc::ServerBuilder builder;
  builder.AddChannelArgument(GRPC_ARG_ALLOW_REUSEPORT, 0);
  builder.AddListeningPort(
      server_address,
      grpc::InsecureServerCredentials());  // NOLINT (insecure)
  builder.RegisterService(this);
  server_ = builder.BuildAndStart();
  CHECK(server_) << "Can't set up SimulatedGPIOService";
  LOG(INFO) << "Started simulated gpio service at: " << server_address;
}

GPIOService& GPIOService::StartSingleton() {
  static GPIOService* kGPIOService = []() {
    auto* gpio_service = new GPIOService;
    gpio_service->StartServer();
    return gpio_service;
  }();
  return *kGPIOService;
}

absl::StatusOr<intrinsic_proto::gpio::v1::WaitForValueResponse>
GPIOService::PollMatchingValues(
    const intrinsic_proto::gpio::v1::WaitForValueRequest& request) {
  intrinsic_proto::gpio::v1::SignalValueSet observed_values;
  {
    absl::MutexLock lock(connections_mutex_);
    for (const auto& connection : gtl::value_view(plugin_connections_)) {
      observed_values.MergeFrom(connection->GetSignalValues());
    }
  }
  auto has_value =
      [&observed_values](
          const std::pair<std::string, intrinsic_proto::gpio::v1::SignalValue>&
              name_and_value) {
        const auto& [name, value] = name_and_value;
        if (!observed_values.values().contains(name)) return false;
        return intrinsic::gpio::SignalValuesAreApproxEqual(
            observed_values.values().at(name), value);
      };
  if (request.has_all_of()) {
    if (absl::c_all_of(request.all_of().values(), has_value)) {
      intrinsic_proto::gpio::v1::WaitForValueResponse response;
      *response.mutable_values() = request.all_of();
      INTR_ASSIGN_OR_RETURN(*response.mutable_event_time(),
                            FromAbslTime(absl::Now()));
      return response;
    }
  } else if (request.has_any_of()) {
    auto matching_value = absl::c_find_if(request.any_of().values(), has_value);
    if (matching_value != request.any_of().values().end()) {
      intrinsic_proto::gpio::v1::WaitForValueResponse response;
      (*response.mutable_values()->mutable_values())[matching_value->first] =
          matching_value->second;
      INTR_ASSIGN_OR_RETURN(*response.mutable_event_time(),
                            FromAbslTime(absl::Now()));
      return response;
    }
  } else {
    return absl::UnimplementedError(
        "Simulated GPIO server currently only supports all_of or any_of "
        "WaitForSignalValue conditions.");
  }
  return absl::NotFoundError("No matching values found");
}

absl::Status GPIOService::ClaimSignals(
    const absl::flat_hash_set<std::string>& signals) {
  absl::MutexLock lock(claimed_signals_mutex_);
  for (const std::string& signal_name : signals) {
    if (claimed_signals_.contains(signal_name)) {
      return absl::PermissionDeniedError(
          absl::Substitute("Signal $0 is already claimed", signal_name));
    }
  }

  for (const std::string& signal_name : signals) {
    claimed_signals_.insert(signal_name);
  }
  return absl::OkStatus();
}
void GPIOService::ReleaseSignals(
    const absl::flat_hash_set<std::string>& signals) {
  absl::MutexLock lock(claimed_signals_mutex_);
  for (const std::string& signal_name : signals) {
    claimed_signals_.erase(signal_name);
  }
}

absl::Status GPIOService::WriteSignals(
    const intrinsic_proto::gpio::v1::WriteSignalsRequest& request) {
  absl::MutexLock lock(connections_mutex_);
  absl::flat_hash_set<std::string> signal_names;
  for (const auto& name : gtl::key_view(request.signal_values().values())) {
    signal_names.insert(name);
  }
  bool signals_writable = false;
  for (const auto& connection : gtl::value_view(plugin_connections_)) {
    if (connection->CheckSignalsWritable(signal_names).ok()) {
      signals_writable = true;
      INTR_RETURN_IF_ERROR(
          connection->SetCommandValues(request.signal_values()));
    }
  }
  if (!signals_writable) {
    return absl::NotFoundError(absl::Substitute(
        "Signals [$0] not found writable among $1 plugin(s). Maybe the "
        "plugin is not ready yet?",
        absl::StrJoin(signal_names, ","), plugin_connections_.size()));
  }
  return absl::OkStatus();
}

}  // namespace simulation
}  // namespace intrinsic
