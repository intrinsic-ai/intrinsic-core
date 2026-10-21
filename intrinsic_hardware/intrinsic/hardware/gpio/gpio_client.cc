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

#include "intrinsic/hardware/gpio/gpio_client.h"

#include <memory>
#include <string>
#include <utility>

#include "absl/container/flat_hash_set.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "grpcpp/client_context.h"
#include "grpcpp/support/time.h"
#include "intrinsic/connect/cc/grpc/channel.h"
#include "intrinsic/hardware/gpio/gpio_service_proto_utils.h"
#include "intrinsic/hardware/gpio/v1/gpio_service.grpc.pb.h"
#include "intrinsic/hardware/gpio/v1/gpio_service.pb.h"
#include "intrinsic/util/grpc/channel.h"
#include "intrinsic/util/grpc/channel_interface.h"
#include "intrinsic/util/grpc/connection_params.h"
#include "intrinsic/util/grpc/grpc.h"
#include "intrinsic/util/status/status_conversion_grpc.h"
#include "intrinsic/util/status/status_conversion_rpc.h"
#include "intrinsic/util/status/status_macros.h"
namespace intrinsic::gpio {

using ::intrinsic_proto::gpio::v1::ReadSignalsRequest;
using ::intrinsic_proto::gpio::v1::ReadSignalsResponse;
using ::intrinsic_proto::gpio::v1::SignalValueSet;

namespace {
constexpr const absl::Duration kGpioClientTimeout = absl::Seconds(5);
constexpr const absl::Duration kGpioInitialTimeout =
    intrinsic::connect::kGrpcClientConnectDefaultTimeout;
};  // namespace

GPIOClient::GPIOClient(
    std::unique_ptr<intrinsic_proto::gpio::v1::GPIOService::StubInterface> stub,
    absl::string_view gpio_service_name,
    const absl::flat_hash_set<std::string>& signals_to_claim)
    : stub_(std::move(stub)),
      connection_params_(
          ConnectionParams::ResourceInstance(gpio_service_name)) {
  for (const auto& name : signals_to_claim) {
    claim_signals_request_.mutable_initial_session_data()->add_signal_names(
        name);
  }

  client_context_factory_ = [connection_params = this->connection_params_]() {
    auto client_context = std::make_unique<::grpc::ClientContext>();
    intrinsic::ConfigureClientContext(client_context.get());

    for (const auto& metadata : connection_params.Metadata()) {
      client_context->AddMetadata(metadata.first, metadata.second);
    }

    return client_context;
  };
}

GPIOClient::GPIOClient(const ConnectionParams& connection_params,
                       const absl::flat_hash_set<std::string>& signals_to_claim)
    : connection_params_(connection_params) {
  for (const auto& name : signals_to_claim) {
    claim_signals_request_.mutable_initial_session_data()->add_signal_names(
        name);
  }

  LOG(INFO) << "Delay creating client channel on: "
            << connection_params_.address;
}

GPIOClient::~GPIOClient() {
  // Close the streaming session if valid
  if (signals_claimed_ && session_stream_) {
    if (!session_stream_->WritesDone()) {
      auto error =
          absl::InternalError("Failed to close the streaming GPIO session.");
      LOG(ERROR) << error;
    }

    auto session_result = session_stream_->Finish();
    if (!session_result.ok()) {
      LOG(ERROR) << session_result.error_message();
    }
  }
}

absl::Status GPIOClient::CreateClientChannel() {
  absl::MutexLock lock(&create_channel_mutex_);
  if (this->stub_) {
    return absl::OkStatus();
  }

  LOG(INFO) << "Create client channel on: " << connection_params_.address;

  INTR_ASSIGN_OR_RETURN(const auto channel,
                        intrinsic::Channel::MakeFromAddress(
                            connection_params_, kGpioInitialTimeout),
                        _ << "Failed to create grpc client channel to: "
                          << connection_params_.address);
  client_context_factory_ = channel->GetClientContextFactory();
  this->stub_ =
      intrinsic_proto::gpio::v1::GPIOService::NewStub(channel->GetChannel());

  return absl::OkStatus();
}

absl::StatusOr<intrinsic_proto::gpio::v1::ReadSignalsResponse> GPIOClient::Read(
    const intrinsic_proto::gpio::v1::ReadSignalsRequest& request) {
  INTR_RETURN_IF_ERROR(this->CreateClientChannel()).LogError();

  // NOTE: if response time of this rpc call is not acceptable, then a possible
  // improvement would be to subscribe to corresponding DDS topics and do
  // ReadAndMatch asynchronously.

  std::unique_ptr<grpc::ClientContext> context = client_context_factory_();
  context->set_deadline(absl::Now() + kGpioClientTimeout);

  ReadSignalsResponse response;
  INTR_RETURN_IF_ERROR(
      ToAbslStatus(stub_->ReadSignals(context.get(), request, &response)))
      .LogError();
  return response;
}

absl::StatusOr<intrinsic_proto::gpio::v1::WaitForValueResponse>
GPIOClient::WaitForValue(
    const intrinsic_proto::gpio::v1::WaitForValueRequest& request,
    const absl::Duration timeout) {
  INTR_RETURN_IF_ERROR(this->CreateClientChannel()).LogError();

  std::unique_ptr<grpc::ClientContext> context = client_context_factory_();
  context->set_deadline(absl::Now() + timeout);

  intrinsic_proto::gpio::v1::WaitForValueResponse response;
  INTR_RETURN_IF_ERROR(
      ToAbslStatus(stub_->WaitForValue(context.get(), request, &response)))
      .LogError();
  return response;
}

absl::StatusOr<bool> GPIOClient::ReadAndMatch(
    const SignalValueSet& match_values) {
  // Constructs a read request message containing only the signal names.
  ReadSignalsRequest request;
  for (const auto& [name, value] : match_values.values()) {
    request.add_signal_names(name);
  }

  INTR_ASSIGN_OR_RETURN(const ReadSignalsResponse response, Read(request));
  return match_values == response.signal_values();
}

absl::Status GPIOClient::CleanUpSessionAfterError() {
  LOG(INFO) << "Cleaning up the current write session.";

  ::intrinsic_proto::gpio::v1::OpenWriteSessionResponse response_message;

  // Explicitly call `WritesDone` to notify the server that no more writes will
  // happen. This is to guard against both client and server getting blocked on
  // `Read` (thus deadlocking).
  session_stream_->WritesDone();

  // Clear out any response messages from the read queue. Our API suggests
  // that there shouldn't be any, since we haven't sent a request, but to
  // protect against server-side bugs that would deadlock or crash the client
  // by erroneously sending responses, we should read until explicit failure
  // before Finish()ing the stream.
  while (session_stream_->Read(&response_message)) {
    LOG(ERROR) << "Received unexpected response from the server:"
               << response_message;
  }
  absl::Status result = ToAbslStatus(session_stream_->Finish());
  session_stream_.reset();
  signals_claimed_ = false;
  return result;
}

absl::Status GPIOClient::Write(const SignalValueSet& desired_values,
                               const bool retry_on_session_error) {
  // Only one write call is allowed to proceed
  absl::MutexLock lock(&write_mutex_);

  const bool session_valid_before_write = signals_claimed_;
  const absl::Status result = WriteInternal(desired_values);

  // It is possible for a previously valid write session to become invalid (e.g.
  // if the GPIO service was restarted). Attempt to do another write (if
  // allowed), which internally would open another write session.
  if (!result.ok() && retry_on_session_error) {
    const bool session_valid_after_write = signals_claimed_;
    if (session_valid_before_write && !session_valid_after_write) {
      LOG(WARNING) << "Retrying because a previously valid write session "
                      "became invalid.";
      return WriteInternal(desired_values);
    }
  }

  return result;
}

absl::Status GPIOClient::WriteInternal(const SignalValueSet& desired_values) {
  INTR_RETURN_IF_ERROR(this->CreateClientChannel()).LogError();

  using ::intrinsic_proto::gpio::v1::OpenWriteSessionRequest;
  using ::intrinsic_proto::gpio::v1::OpenWriteSessionResponse;

  // Open a stream session to claim exclusive write access to signals if not
  // already done so.
  // TODO(dhirajgoel): Use `session_stream_` directly and remove
  // `signals_claimed_`.
  if (!signals_claimed_) {
    OpenWriteSessionResponse resp;

    LOG(INFO) << "Attempting to open a write session.";

    // Creates a new context for opening the write stream. Contexts are not
    // allowed to be shared across rpc calls.
    stream_session_ctx_ = client_context_factory_();
    // Not setting any deadline since this session stays active for the lifetime
    // of this class object. The default is already the max value.
    stream_session_ctx_->set_deadline(absl::InfiniteFuture());

    session_stream_ = stub_->OpenWriteSession(stream_session_ctx_.get());
    if (!session_stream_->Write(claim_signals_request_)) {
      LOG(ERROR) << "Opening a write session failed.";
      absl::Status cleanup_result = CleanUpSessionAfterError();
      auto error = absl::InternalError(absl::StrCat(
          "Failed to claim exclusive write access to GPIO signals: ",
          cleanup_result.message()));
      LOG(ERROR) << error;
      return error;
    }

    if (!session_stream_->Read(&resp)) {
      LOG(ERROR) << "Reading response in opening a write session failed.";
      absl::Status cleanup_result = CleanUpSessionAfterError();
      auto error = absl::InternalError(absl::StrCat(
          "Failed to read response while claiming exclusive write access to "
          "GPIO signals: ",
          cleanup_result.message()));
      LOG(ERROR) << error;
      return error;
    }

    INTR_RETURN_IF_ERROR(intrinsic::MakeStatusFromRpcStatus(resp.status()))
        .LogError();

    // Update the flag to indicate that signals were successfully claimed
    signals_claimed_ = true;
    LOG(INFO) << "Successfully opened a write session.";
  }

  // Do the actual write for the desired signals
  OpenWriteSessionRequest req;
  *req.mutable_write_signals()->mutable_signal_values() = desired_values;
  if (!session_stream_->Write(req)) {
    LOG(ERROR) << "session_stream_->Write returned false";
    absl::Status cleanup_result = CleanUpSessionAfterError();
    auto error = absl::InternalError(
        absl::StrCat("Failed to write request to set value to GPIO session: ",
                     cleanup_result.message()));
    LOG(ERROR) << error;
    return error;
  }
  OpenWriteSessionResponse resp;
  if (!session_stream_->Read(&resp)) {
    LOG(ERROR) << "session_stream_->Read returned false";
    absl::Status cleanup_result = CleanUpSessionAfterError();
    auto error = absl::InternalError(absl::StrCat(
        "Failed to read response from setting value from GPIO session: ",
        cleanup_result.message()));
    LOG(ERROR) << error;
    return error;
  }

  const absl::Status session_status =
      intrinsic::MakeStatusFromRpcStatus(resp.status());
  if (session_status.code() == absl::StatusCode::kAborted) {
    std::string err_msg = "Server aborted the GPIO write session. ";
    LOG(ERROR) << err_msg;
    absl::Status cleanup_result = CleanUpSessionAfterError();
    if (cleanup_result.ok()) {
      absl::StrAppend(&err_msg, "Successfully cleaned up the write session.");
    } else {
      absl::StrAppend(&err_msg, "Cleaning up write session failed with: ",
                      cleanup_result.message());
    }
    auto error = absl::InternalError(err_msg);
    LOG(ERROR) << error;
    return error;
  } else if (!session_status.ok()) {
    LOG(ERROR) << "Server returned non-session ending error: "
               << session_status;
    return session_status;
  }

  return absl::OkStatus();
}

absl::StatusOr<intrinsic_proto::gpio::v1::GetSignalDescriptionsResponse>
GPIOClient::GetSignalDescriptions() {
  INTR_RETURN_IF_ERROR(this->CreateClientChannel()).LogError();

  std::unique_ptr<grpc::ClientContext> context = client_context_factory_();
  context->set_deadline(absl::Now() + kGpioClientTimeout);

  auto service_name = [this]() -> std::string {
    if (!this->connection_params_.instance_name.empty()) {
      return this->connection_params_.instance_name;
    }
    return this->connection_params_.address;
  };

  intrinsic_proto::gpio::v1::GetSignalDescriptionsRequest request;
  intrinsic_proto::gpio::v1::GetSignalDescriptionsResponse response;
  INTR_RETURN_IF_ERROR(ToAbslStatus(
      stub_->GetSignalDescriptions(context.get(), request, &response)))
      << absl::StrCat("Failed to get signal descriptions from GPIO service: ",
                      service_name());
  return response;
}

absl::StatusOr<intrinsic::Subscription> GPIOClient::SubscribeToSignal(
    const std::string& signal, SignalValueCallback value_callback) {
  INTR_ASSIGN_OR_RETURN(const auto response, GetSignalDescriptions());
  const intrinsic_proto::gpio::v1::SignalDescription* found_desc = nullptr;
  for (const auto& desc : response.signal_descriptions()) {
    if (desc.signal_name() == signal) {
      found_desc = &desc;
      break;
    }
    bool found_in_alt = false;
    for (const auto& alt_name : desc.alternate_signal_names()) {
      if (alt_name == signal) {
        found_desc = &desc;
        found_in_alt = true;
        break;
      }
    }
    if (found_in_alt) {
      break;
    }
  }

  if (found_desc == nullptr) {
    return absl::NotFoundError(absl::StrCat("Signal not found: ", signal));
  }

  if (found_desc->pubsub_topic_name().empty()) {
    return absl::NotFoundError(
        absl::StrCat("Signal '", signal, "' does not have a pubsub topic"));
  }

  return pubsub_.CreateSubscription<intrinsic_proto::gpio::v1::SignalValue>(
      found_desc->pubsub_topic_name(), TopicConfig(),
      std::move(value_callback));
}

};  // namespace intrinsic::gpio
