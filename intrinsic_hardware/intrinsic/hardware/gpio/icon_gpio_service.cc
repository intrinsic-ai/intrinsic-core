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

#include "intrinsic/hardware/gpio/icon_gpio_service.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "absl/base/nullability.h"
#include "absl/base/thread_annotations.h"
#include "absl/container/btree_map.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/container/node_hash_map.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "google/protobuf/repeated_ptr_field.h"
#include "grpcpp/server_context.h"
#include "grpcpp/support/status.h"
#include "grpcpp/support/sync_stream.h"
#include "grpcpp/support/time.h"
#include "intrinsic/hardware/gpio/gpio_service_proto_utils.h"  // IWYU pragma: keep
#include "intrinsic/hardware/gpio/v1/gpio_service.grpc.pb.h"
#include "intrinsic/hardware/gpio/v1/gpio_service.pb.h"
#include "intrinsic/hardware/gpio/v1/signal.pb.h"
#include "intrinsic/icon/actions/adio_info.h"
#include "intrinsic/icon/cc_client/client.h"
#include "intrinsic/icon/cc_client/condition.h"
#include "intrinsic/icon/cc_client/operational_status.h"
#include "intrinsic/icon/cc_client/session.h"
#include "intrinsic/icon/cc_client/state_variable_path.h"
#include "intrinsic/icon/common/id_types.h"
#include "intrinsic/icon/proto/io_block.pb.h"
#include "intrinsic/icon/proto/part_status.pb.h"
#include "intrinsic/logging/proto/log_item.pb.h"
#include "intrinsic/platform/pubsub/publisher.h"
#include "intrinsic/platform/pubsub/pubsub.h"
#include "intrinsic/platform/pubsub/subscription.h"
#include "intrinsic/util/grpc/channel_interface.h"
#include "intrinsic/util/status/status_conversion_grpc.h"
#include "intrinsic/util/status/status_conversion_rpc.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/status/status_macros_grpc.h"
#include "intrinsic/util/thread/thread.h"
#include "intrinsic/util/thread/util.h"

namespace intrinsic {
namespace {

using ::intrinsic_proto::gpio::IconGpioServiceConfig;
using ::intrinsic_proto::gpio::v1::SignalValue;

static constexpr const double kAnalogValueTolerance = 0.000001;
static constexpr absl::Duration kAlwaysPublishAfter = absl::Seconds(1);

class EmptyIconGPIOService
    : public intrinsic_proto::gpio::v1::GPIOService::Service {
 public:
  ::grpc::Status GetSignalDescriptions(
      ::grpc::ServerContext* context,
      const intrinsic_proto::gpio::v1::GetSignalDescriptionsRequest* request,
      intrinsic_proto::gpio::v1::GetSignalDescriptionsResponse* response)
      override {
    return ToGrpcStatus(absl::OkStatus());
  }

  ::grpc::Status ReadSignals(
      ::grpc::ServerContext* context,
      const intrinsic_proto::gpio::v1::ReadSignalsRequest* request,
      intrinsic_proto::gpio::v1::ReadSignalsResponse* response) override {
    if (request->signal_names().empty()) {
      return ToGrpcStatus(absl::OkStatus());
    }
    return ToGrpcStatus(absl::FailedPreconditionError(
        "No ADIO part has been configured. Check the application "
        "configuration."));
  }

  ::grpc::Status WaitForValue(
      ::grpc::ServerContext* context,
      const intrinsic_proto::gpio::v1::WaitForValueRequest* request,
      intrinsic_proto::gpio::v1::WaitForValueResponse* response) override {
    if (request->any_of().values().empty() &&
        request->all_of().values().empty()) {
      return ToGrpcStatus(absl::OkStatus());
    }
    return ToGrpcStatus(absl::FailedPreconditionError(
        "No ADIO part has been configured. Check the application "
        "configuration."));
  }

  ::grpc::Status OpenWriteSession(
      ::grpc::ServerContext* context,
      ::grpc::ServerReaderWriter<
          intrinsic_proto::gpio::v1::OpenWriteSessionResponse,
          intrinsic_proto::gpio::v1::OpenWriteSessionRequest>* stream)
      override {
    return ToGrpcStatus(absl::FailedPreconditionError(
        "No ADIO part has been configured. Check the application "
        "configuration."));
  }
};

class IconGPIOService : public intrinsic_proto::gpio::v1::GPIOService::Service {
  struct Signal;

 public:
  ::grpc::Status GetSignalDescriptions(
      ::grpc::ServerContext* context,
      const intrinsic_proto::gpio::v1::GetSignalDescriptionsRequest* request,
      intrinsic_proto::gpio::v1::GetSignalDescriptionsResponse* response)
      override {
    LOG(INFO) << "Gpio GetSignalDescriptions";
    absl::ReaderMutexLock l(&signal_mu_);
    for (const auto& [signal_name, signal] : signals_) {
      auto signal_description = response->add_signal_descriptions();
      signal_description->set_signal_name(signal_name);
      signal_description->set_can_read(signal->can_read);
      signal_description->set_can_write(signal->can_write);
      signal_description->set_description(signal->description);
      signal_description->set_type(signal->type);

      // Force is unimplemented for ICON.
      signal_description->set_can_force(false);
      signal_description->set_pubsub_topic_name(signal->pubsub_topic_name);
    }

    return ToGrpcStatus(absl::OkStatus());
  }

  ::grpc::Status ReadSignals(
      ::grpc::ServerContext* context,
      const intrinsic_proto::gpio::v1::ReadSignalsRequest* request,
      intrinsic_proto::gpio::v1::ReadSignalsResponse* response) override {
    LOG(INFO) << "Gpio ReadSignals";

    absl::flat_hash_set<std::string> signal_names_without_value;
    {
      absl::ReaderMutexLock l(&signal_mu_);
      for (const auto& signal_name : request->signal_names()) {
        auto signal_it = signals_.find(signal_name);
        if (signal_it == signals_.end()) {
          return ToGrpcStatus(absl::NotFoundError(
              absl::StrCat("Signal `", signal_name, "` was not found.")));
        }

        // There is also a `written_value` variable that we could check. If we
        // attempt to use the value within `written_value` it is not guaranteed
        // that we have not seen an update come in from someone else that has
        // also changed the value. So it would not be clear if this value is the
        // latest and as such we rely on the pubsub status to tell us when the
        // value was written to and changed no matter the source.
        absl::ReaderMutexLock l(&signal_it->second->last_value_mu);
        if (signal_it->second->last_value.has_value()) {
          response->mutable_signal_values()->mutable_values()->insert(
              {signal_name, signal_it->second->last_value.value()});
        } else {
          signal_names_without_value.insert(signal_name);
        }
      }
    }

    // If we have any signals without a value we need to explicitly fetch them.
    if (!signal_names_without_value.empty()) {
      INTR_ASSIGN_OR_RETURN_GRPC(auto icon_status, icon_client_->GetStatus());
      INTR_RETURN_IF_ERROR_GRPC(ProcessStatusMap(
          icon_status.part_status(),
          [&response, &signal_names_without_value](
              const std::string& signal_name, Signal& signal,
              const SignalValue& value) {
            absl::MutexLock l(&signal.last_value_mu);
            // Only update the last value if it was never set, otherwise trust
            // the values coming from the pubsub subscription to be up to date.
            if (!signal.last_value.has_value()) {
              signal.last_value = value;
              signal.last_publish_time = absl::Now();
            }

            // Since the part status has every value, we only need to update the
            // signals that didn't have a value yet. We also remove them from
            // the list so we can ensure that we did get all of the missing
            // values after this processing completes.
            if (signal_names_without_value.contains(signal_name)) {
              signal_names_without_value.erase(signal_name);
              response->mutable_signal_values()->mutable_values()->insert(
                  {signal_name, signal.last_value.value()});
            }
          }));
    }

    // If we still have any signals without a value we need to return an error.
    if (!signal_names_without_value.empty()) {
      return ToGrpcStatus(absl::NotFoundError(
          absl::StrCat("Signal values for `",
                       absl::StrJoin(signal_names_without_value, ", "),
                       "` were not found.")));
    }

    return ::grpc::Status::OK;
  }

  absl::Status ProcessStatusMap(
      const ::google::protobuf::Map<
          std::string, ::intrinsic_proto::icon::PartStatus>& part_status_map,
      std::function<void(const std::string& topic, Signal& signal,
                         const SignalValue& value)>
          signal_value_callback) {
    // Collect the signals into a map form that can be used more efficiently
    // for the search we need to do below.
    absl::flat_hash_map<
        std::string, absl::flat_hash_map<
                         int, std::pair<std::string, std::shared_ptr<Signal>>>>
        signals_by_block_name_and_index;
    {
      absl::ReaderMutexLock l(&signal_mu_);
      for (const auto& [signal_name, signal] : signals_) {
        signals_by_block_name_and_index[signal->icon_block_name]
                                       [signal->signal_block_index] =
                                           std::make_pair(signal_name, signal);
      }
    }

    SignalValue shared_signal_value;

    auto process_signals = [&signals_by_block_name_and_index,
                            &shared_signal_value,
                            &signal_value_callback](auto transform_to_signal) {
      return [&signals_by_block_name_and_index, &shared_signal_value,
              &signal_value_callback,
              transform_to_signal = std::move(transform_to_signal)](
                 const auto& input_signal) -> absl::Status {
        auto signal_map_it =
            signals_by_block_name_and_index.find(input_signal.first);
        if (signal_map_it == signals_by_block_name_and_index.end()) {
          return absl::NotFoundError(absl::StrCat("Signal with block name '",
                                                  input_signal.first,
                                                  "' was not found."));
        }

        for (const auto& [input_signal_block_index, input_signal_data] :
             input_signal.second.signals()) {
          auto signal_it = signal_map_it->second.find(input_signal_block_index);
          if (signal_it == signal_map_it->second.end()) {
            return absl::NotFoundError(absl::StrCat(
                "Signal with block name '", input_signal.first, "' with index ",
                input_signal_block_index, " was not found."));
          }

          auto& [signal_name, signal] = signal_it->second;

          if (input_signal_block_index != signal->signal_block_index) {
            return absl::InternalError(absl::StrCat(
                "Inconsistency for signal with block name '",
                input_signal.first, "' with index ", input_signal_block_index,
                " and the one found in the map with index ",
                signal->signal_block_index, "."));
          }

          const auto& input_value =
              input_signal.second.signals().at(signal->signal_block_index);

          // Convert the input signal into the shared_signal_value.
          shared_signal_value.Clear();
          transform_to_signal(input_value, &shared_signal_value);

          // Process the signal value.
          signal_value_callback(signal_name, *signal, shared_signal_value);
        }

        return absl::OkStatus();
      };
    };

    const auto digital_callback = process_signals(
        [](const intrinsic_proto::icon::DigitalSignal& input,
           SignalValue* value) { value->set_bool_value(input.value()); });
    const auto analog_callback = process_signals(
        [](const intrinsic_proto::icon::AnalogSignal& input,
           SignalValue* value) { value->set_double_value(input.value()); });

    // Search for the signal among the adio state and copy its value if found.
    for (const auto& part_name : service_config_.parts()) {
      auto part_status_it = part_status_map.find(part_name);
      if (part_status_it == part_status_map.end()) {
        return absl::NotFoundError(
            absl::StrCat("No PartStatus for part '", part_name, "'."));
      }
      if (!part_status_it->second.has_adio_state()) {
        return absl::NotFoundError(absl::StrCat(
            "PartStatus for part '", part_name, "' is missing ADIO state."));
      }

      const auto& adio_state = part_status_it->second.adio_state();

      // Search the digital inputs.
      for (const auto& block_pair : adio_state.digital_inputs()) {
        INTR_RETURN_IF_ERROR(digital_callback(block_pair));
      }

      // Search the analog inputs.
      for (const auto& block_pair : adio_state.analog_inputs()) {
        INTR_RETURN_IF_ERROR(analog_callback(block_pair));
      }

      // Search the digital outputs.
      for (const auto& block_pair : adio_state.digital_outputs()) {
        INTR_RETURN_IF_ERROR(digital_callback(block_pair));
      }

      // Search the analog outputs.
      for (const auto& block_pair : adio_state.analog_outputs()) {
        INTR_RETURN_IF_ERROR(analog_callback(block_pair));
      }
    }

    return absl::OkStatus();
  }

  // Generates a topic name in the format of
  //   /equipment/gpio/<instance_name>/<signal_name>
  //
  // Ideally we would switch this to use the asset name and produce a topic in
  // the new format:
  //   /assets/<instance_name>/gpio/<signal_name>
  std::string GetTopicNameForSignalName(absl::string_view signal_name) const {
    return absl::StrCat("/equipment/gpio/",
                        service_config_.equipment_instance_name(), "/",
                        absl::StrReplaceAll(signal_name, {{".", "_"}}));
  }

  absl::StatusOr<Publisher* absl_nonnull> GetOrCreatePublisher(
      const std::string& topic) {
    absl::MutexLock lock(&publishers_mutex_);
    auto it = publishers_.find(topic);
    if (it != publishers_.end()) {
      return &it->second;
    }

    INTR_ASSIGN_OR_RETURN(auto publisher,
                          pubsub_.CreatePublisher(topic, TopicConfig()),
                          absl::InternalError("Failed to create publisher"));

    std::tie(it, std::ignore) =
        publishers_.emplace(topic, std::move(publisher));
    return &it->second;
  }

  void ProcessLogItem(const intrinsic_proto::data_logger::LogItem& log_item) {
    if (!log_item.has_payload()) return;
    if (!log_item.payload().has_icon_robot_status()) return;

    auto callback = [this](const std::string& signal_name, Signal& signal,
                           const SignalValue& value) {
      // Don't publish if the value hasn't changed and not enough time has
      // passed since the last publish.
      absl::MutexLock l(&signal.last_value_mu);
      if (signal.last_publish_time + kAlwaysPublishAfter > absl::Now() &&
          signal.last_value == value) {
        VLOG_EVERY_N_SEC(1, 10)
            << "Not publishing signal " << signal_name << " because the "
            << "value has not changed and not enough time has passed since "
            << "the last publish.";
        return;
      }

      auto publisher = GetOrCreatePublisher(signal.pubsub_topic_name);
      if (!publisher.ok()) {
        LOG_EVERY_N_SEC(ERROR, 10) << "Failed to create publisher for signal "
                                   << signal_name << ": " << publisher.status();
        return;
      }

      if (auto status = publisher.value()->Publish(value); status.ok()) {
        signal.last_value = value;
        signal.last_publish_time = absl::Now();
      } else {
        LOG_EVERY_N_SEC(ERROR, 10)
            << "Failed to publish signal " << signal_name << ": " << status;
      }
    };

    const auto& status_map =
        log_item.payload().icon_robot_status().status_map();
    if (auto status = ProcessStatusMap(status_map, callback); !status.ok()) {
      LOG_EVERY_N_SEC(ERROR, 10)
          << "Failed to process part status map: " << status;
    }
  }

  ::grpc::Status WaitForValue(
      ::grpc::ServerContext* context,
      const intrinsic_proto::gpio::v1::WaitForValueRequest* request,
      intrinsic_proto::gpio::v1::WaitForValueResponse* response) override {
    LOG(INFO) << "Gpio WaitForValue";
    // No need to claim any parts here since we don't run any actions and only
    // use FreeStandingReactions.
    INTR_ASSIGN_OR_RETURN_GRPC(
        auto session,
        icon::Session::Start(
            icon_channel_, /*parts=*/{}, /*context=*/{},
            /*deadline=*/absl::FromChrono(context->deadline())),
        _.LogError());

    // Convert the request data into a ICON conditions using the ADIO part
    // status fields.
    constexpr icon::ReactionHandle kInputObservedSetHandle(0);
    std::vector<icon::Condition> literals;

    const ::google::protobuf::Map<std::string, SignalValue>* expectations =
        nullptr;
    if (request->has_all_of()) {
      expectations = &request->all_of().values();
    } else if (request->has_any_of()) {
      expectations = &request->any_of().values();
    } else {
      return ToGrpcStatus(absl::InvalidArgumentError(
          "At least one AnyOf or AllOf condition must be specified."));
    }
    {
      absl::ReaderMutexLock l(&signal_mu_);
      for (const auto& signal_expectation : *expectations) {
        INTR_ASSIGN_OR_RETURN_GRPC(std::shared_ptr<Signal> signal,
                                   GetSignal(signal_expectation.first));
        absl::string_view block_name = signal->icon_block_name;
        absl::string_view part_name = signal->icon_part_name;
        uint32_t block_index = signal->signal_block_index;

        std::string path;
        switch (signal_expectation.second.value_case()) {
          case SignalValue::kBoolValue:
            path = icon::ADIODigitalInputStateVariablePath(
                part_name, block_name, block_index);

            literals.push_back(icon::Condition(
                signal_expectation.second.bool_value() ? icon::IsTrue(path)
                                                       : icon::IsFalse(path)));
            break;
          case SignalValue::kDoubleValue:
            path = icon::ADIOAnalogInputStateVariablePath(part_name, block_name,
                                                          block_index);
            literals.push_back(icon::Condition(
                icon::IsApprox(path, signal_expectation.second.double_value(),
                               kAnalogValueTolerance)));
            break;
          default:
            return ToGrpcStatus(absl::InvalidArgumentError(absl::StrCat(
                signal_expectation.first, " expects a bool or double value.")));
        }
      }
    }
    std::optional<icon::Condition> condition;
    if (request->has_all_of()) {
      condition = icon::AllOf(literals);
    } else if (request->has_any_of()) {
      condition = icon::AnyOf(literals);
    }

    if (!condition.has_value()) {
      return ToGrpcStatus(
          absl::InternalError("Variable 'condition' was not set."));
    }
    // TODO(b/329688113): These reactions are not cleaned up and may need to
    // breaking ICON's realtime loop deadline if they accumulate.
    //
    // The API for removing reactions is not available in the c++ session
    // client.
    INTR_RETURN_IF_ERROR_GRPC(session->AddFreestandingReactions(
        {icon::ReactionDescriptor(*condition)
             .WithHandle(kInputObservedSetHandle)}));

    // Start a thread to end the session if the RPC is cancelled.
    bool cancelled = false;
    absl::Notification finished_waiting;
    Thread cancel_thread([&cancelled, &context, &finished_waiting, &session]() {
      // If the following returns false, it means the wait was interrupted
      // (because the RPC was cancelled) before `finished_waiting` was
      // notified.
      if (!WaitForNotificationWithInterrupt(
              finished_waiting,
              [&context]() -> bool { return context->IsCancelled(); })) {
        cancelled = session->End().ok();
      }
    });

    // Run the watcher loop until the RPC's deadline. If the deadline is
    // reached before kInputObservedSetHandle is reached, then we propagate
    // ICON's timeout to the caller.
    auto status = session->RunWatcherLoopUntilReaction(
        kInputObservedSetHandle,
        grpc::TimeFromGprTimespec(context->raw_deadline()));

    // Stop the cancellation monitoring thread we started above.
    finished_waiting.Notify();
    cancel_thread.join();

    INTR_RETURN_IF_ERROR_GRPC(status).LogError();
    if (cancelled) {
      return ToGrpcStatus(absl::CancelledError(
          "Cancelled while waiting for condition to be met."));
    }

    return ToGrpcStatus(absl::OkStatus());
  }

  ::grpc::Status OpenWriteSession(
      ::grpc::ServerContext* context,
      ::grpc::ServerReaderWriter<
          intrinsic_proto::gpio::v1::OpenWriteSessionResponse,
          intrinsic_proto::gpio::v1::OpenWriteSessionRequest>* stream)
      override {
    LOG(INFO) << "Gpio OpenWriteSession";
    // Try to open an ICON session.
    intrinsic_proto::gpio::v1::OpenWriteSessionRequest request;
    if (!stream->Read(&request)) {
      return ToGrpcStatus(
          absl::AbortedError("Failed to read initial request."));
    }
    if (!request.has_initial_session_data()) {
      return ToGrpcStatus(absl::InvalidArgumentError(
          "Initial OpenWriteSessionRequest is missing initial_session_data."));
    }

    LOG(INFO) << "OpenWriteSession for signals "
              << absl::StrJoin(request.initial_session_data().signal_names(),
                               ", ");

    std::unique_ptr<WriteSessionHandler> session;
    {
      // Hold the signal mutex while we establish the session. Subsequent
      // operations on the session are lock-free.
      absl::MutexLock mu(&signal_mu_);
      auto session_or = CreateSession(request.initial_session_data());

      // The order of operations here is a bit convoluted. We return an error
      // here, since clients are generally expected to read the status
      // associated with each request. *Then* we tear down the RPC and return an
      // error with the overall disposition of the session.
      intrinsic_proto::gpio::v1::OpenWriteSessionResponse initial_resp;
      (*initial_resp.mutable_status()) =
          SaveStatusAsRpcStatus(session_or.status());
      if (!stream->Write(initial_resp)) {
        return ToGrpcStatus(absl::AbortedError(
            "Failed to write initial response to the client."));
      }
      if (!session_or.ok()) {
        return ToGrpcStatus(
            absl::AbortedError("Failed to start write session."));
      }
      session = std::move(*session_or);
    }
    LOG(INFO) << "Opened write session.";

    if (session == nullptr) {
      return ToGrpcStatus(absl::InternalError(
          "Failed to initialize session handler. GPIO server may be in a bad "
          "state."));
    }

    while (stream->Read(&request)) {
      if (request.has_initial_session_data()) {
        return ToGrpcStatus(absl::FailedPreconditionError(
            "Received initial session data when Session is already "
            "initialized."));
      }

      absl::Status status_result;
      switch (request.action_request_case()) {
        case intrinsic_proto::gpio::v1::OpenWriteSessionRequest::
            ActionRequestCase::kWriteSignals:
          status_result =
              session->HandleWriteOperation(request.write_signals());
          break;
        case intrinsic_proto::gpio::v1::OpenWriteSessionRequest::
            ActionRequestCase::ACTION_REQUEST_NOT_SET:
          status_result =
              absl::InvalidArgumentError("No request was provided.");
          break;
        default:
          status_result = absl::UnimplementedError("Request type not handled.");
      }

      if (!status_result.ok()) {
        LOG(ERROR) << "Session error: " << status_result;
      }

      intrinsic_proto::gpio::v1::OpenWriteSessionResponse resp;
      (*resp.mutable_status()) = SaveStatusAsRpcStatus(status_result);
      if (!stream->Write(resp)) {
        return ToGrpcStatus(absl::AbortedError(
            "Failed to write streaming response to the client. Assuming the "
            "client is dead and ending session."));
      }

      // Closes the write session if underlying icon session aborted.
      // `OpenWriteSession` API requires that an aborted error should end the
      // write session.
      // intrinsic/hardware/gpio/v1/gpio_service.proto;l=71-72;rcl=559549436
      if (resp.status().code() == grpc::StatusCode::ABORTED) {
        LOG(INFO) << "Closing the write stream as icon session was aborted.";
        return ToGrpcStatus(absl::OkStatus());
      }
    }
    return ToGrpcStatus(absl::OkStatus());
  }

  explicit IconGPIOService(const IconGpioServiceConfig& service_config,
                           std::shared_ptr<ChannelInterface> icon_channel)
      : service_config_(service_config), icon_channel_(icon_channel) {}

  absl::Status InitializeSignals(const IconGpioServiceConfig& service_config) {
    INTR_ASSIGN_OR_RETURN(auto config, icon_client_->GetConfig());

    absl::MutexLock signal_lock(&signal_mu_);
    if (!signals_.empty()) {
      return absl::InternalError("Signals already initialized.");
    }

    for (const auto& part_name : service_config.parts()) {
      INTR_ASSIGN_OR_RETURN(auto part_config,
                            config.GetGenericPartConfig(part_name));

      const auto& adio_config = part_config.adio_config();
      for (const auto& digital_input_block :
           adio_config.digital_input_blocks()) {
        auto block_name = digital_input_block.first;
        uint32_t idx = 0;
        for (const auto& signal_description :
             digital_input_block.second.signal_names()) {
          std::string signal_name = absl::StrCat(block_name, ".", idx);

          if (signals_.contains(signal_name)) {
            return absl::AlreadyExistsError(absl::StrCat(
                "There is already an input or output with signal name '",
                signal_name,
                "'. Cannot add a digital input with the same name."));
          }
          signals_.emplace(std::make_pair(
              // WrapUnique + new here because std::atomic isn't copy or
              // move-constructible.
              signal_name,
              absl::WrapUnique(new Signal{
                  .can_read = true,
                  .can_write = false,
                  .is_claimed_by_session = std::atomic(false),
                  .pubsub_topic_name = GetTopicNameForSignalName(
                      absl::StrCat("input_", block_name, "_", idx)),
                  .icon_part_name = part_name,
                  .icon_block_name = block_name,
                  .description = signal_description,
                  .signal_block_index = idx++,
                  .type = intrinsic_proto::gpio::v1::SIGNAL_TYPE_BOOL,
              })));
        }
      }

      for (const auto& digital_output_block :
           adio_config.digital_output_blocks()) {
        auto block_name = digital_output_block.first;
        uint32_t idx = 0;
        for (const auto& signal_description :
             digital_output_block.second.signal_names()) {
          std::string signal_name = absl::StrCat(block_name, ".", idx);

          if (signals_.contains(signal_name)) {
            return absl::AlreadyExistsError(absl::StrCat(
                "There is already an input or output with signal name '",
                signal_name,
                "'. Cannot add a digital output with the same name."));
          }
          signals_.emplace(std::make_pair(
              // WrapUnique + new here because std::atomic isn't copy or
              // move-constructible.
              signal_name,
              absl::WrapUnique(new Signal{
                  .can_read = false,
                  .can_write = true,
                  .is_claimed_by_session = std::atomic(false),
                  .pubsub_topic_name = GetTopicNameForSignalName(
                      absl::StrCat("output_", block_name, "_", idx)),
                  .icon_part_name = part_name,
                  .icon_block_name = block_name,
                  .description = signal_description,
                  .signal_block_index = idx++,
                  .type = intrinsic_proto::gpio::v1::SIGNAL_TYPE_BOOL,
              })));
        }
      }

      for (const auto& analog_input_block : adio_config.analog_input_blocks()) {
        auto block_name = analog_input_block.first;
        uint32_t idx = 0;
        for (const auto& signal_description :
             analog_input_block.second.signal_names()) {
          std::string signal_name = absl::StrCat(block_name, ".", idx);

          if (signals_.contains(signal_name)) {
            return absl::AlreadyExistsError(absl::StrCat(
                "There is already an input or output with signal name '",
                signal_name,
                "'. Cannot add an analog input with the same name."));
          }
          signals_.emplace(std::make_pair(
              // WrapUnique + new here because std::atomic isn't copy or
              // move-constructible.
              signal_name,
              absl::WrapUnique(new Signal{
                  .can_read = true,
                  .can_write = false,
                  .is_claimed_by_session = std::atomic(false),
                  .pubsub_topic_name = GetTopicNameForSignalName(
                      absl::StrCat("input_", block_name, "_", idx)),
                  .icon_part_name = part_name,
                  .icon_block_name = block_name,
                  .description = signal_description,
                  .signal_block_index = idx++,
                  .type = intrinsic_proto::gpio::v1::SIGNAL_TYPE_DOUBLE,
              })));
        }
      }

      for (const auto& analog_output_block :
           adio_config.analog_output_blocks()) {
        auto block_name = analog_output_block.first;
        uint32_t idx = 0;
        for (const auto& signal_description :
             analog_output_block.second.signal_names()) {
          std::string signal_name = absl::StrCat(block_name, ".", idx);

          if (signals_.contains(signal_name)) {
            return absl::AlreadyExistsError(absl::StrCat(
                "There is already an input or output with signal name '",
                signal_name,
                "'. Cannot add an analog output with the same name."));
          }
          signals_.emplace(std::make_pair(
              // WrapUnique + new here because std::atomic isn't copy or
              // move-constructible.
              signal_name,
              absl::WrapUnique(new Signal{
                  .can_read = false,
                  .can_write = true,
                  .is_claimed_by_session = std::atomic(false),
                  .pubsub_topic_name = GetTopicNameForSignalName(
                      absl::StrCat("output_", block_name, "_", idx)),
                  .icon_part_name = part_name,
                  .icon_block_name = block_name,
                  .description = signal_description,
                  .signal_block_index = idx++,
                  .type = intrinsic_proto::gpio::v1::SIGNAL_TYPE_DOUBLE,
              })));
        }
      }
    }

    return absl::OkStatus();
  }

  static absl::StatusOr<std::unique_ptr<IconGPIOService>> Make(
      const IconGpioServiceConfig& service_config,
      std::shared_ptr<intrinsic::ChannelInterface> icon_channel) {
    auto instance =
        std::make_unique<IconGPIOService>(service_config, icon_channel);

    LOG(INFO) << "IconGPIOService connecting to in-process ICON client.";
    instance->icon_client_ =
        std::make_unique<icon::Client>(instance->icon_channel_);

    INTR_RETURN_IF_ERROR(instance->InitializeSignals(service_config));
    LOG(INFO) << "IconGPIOService connected to in-process ICON client.";

    // Subscribe to the icon status updates. We use the throttle topic to
    // avoid getting too many updates. (1kHz is too much)
    const std::string icon_status_topic =
        absl::StrCat("/icon/", service_config.equipment_instance_name(),
                     "/robot_status_throttle");
    INTR_ASSIGN_OR_RETURN(
        instance->subscription_,
        instance->pubsub_
            .CreateSubscription<intrinsic_proto::data_logger::LogItem>(
                icon_status_topic, TopicConfig(),
                [instance = instance.get()](
                    const intrinsic_proto::data_logger::LogItem& log_item) {
                  instance->ProcessLogItem(log_item);
                }));

    LOG(INFO) << "IconGPIOService done with initialization.";
    return instance;
  }

 private:
  const IconGpioServiceConfig service_config_;
  std::unique_ptr<icon::Client> icon_client_;
  std::shared_ptr<ChannelInterface> icon_channel_;
  intrinsic::PubSub pubsub_;
  intrinsic::Subscription subscription_;

  absl::Mutex publishers_mutex_;
  absl::node_hash_map<std::string, intrinsic::Publisher> publishers_
      ABSL_GUARDED_BY(publishers_mutex_);

  size_t action_instance_id_ = 0;
  size_t reaction_id_ = 0;

  // Represents the mapping between a GPIO signal and the underlying ICON bit.
  struct Signal {
    bool can_read;
    bool can_write;

    // Set to true if a session claims this signal.
    std::atomic<bool> is_claimed_by_session;

    std::string pubsub_topic_name;

    // TODO(b/228630961): Reconcile block naming conventions between ICON and
    // the GPIO API.
    std::string icon_part_name;
    std::string icon_block_name;
    std::string description;
    // The index of the signal within the block.
    uint32_t signal_block_index;
    intrinsic_proto::gpio::v1::SignalType type =
        intrinsic_proto::gpio::v1::SIGNAL_TYPE_UNKNOWN;

    // If a session has written a value to this signal, then this value is
    // populated.
    std::optional<SignalValue> written_value;

    absl::Mutex last_value_mu;
    // The last value that was received from ICON using pubsub
    std::optional<SignalValue> last_value ABSL_GUARDED_BY(last_value_mu);
    // The last time that the signal was published.
    absl::Time last_publish_time ABSL_GUARDED_BY(last_value_mu) =
        absl::InfinitePast();
  };

  absl::Mutex signal_mu_;
  absl::node_hash_map<std::string, std::shared_ptr<Signal>> signals_
      ABSL_GUARDED_BY(signal_mu_);

  absl::StatusOr<absl::flat_hash_map<std::string, std::shared_ptr<Signal>>>
  GetSignals(
      const google::protobuf::RepeatedPtrField<std::string>& signal_names)
      ABSL_SHARED_LOCKS_REQUIRED(signal_mu_) {
    absl::flat_hash_map<std::string, std::shared_ptr<Signal>> signals;
    signals.reserve(signal_names.size());
    for (const auto& signal_name : signal_names) {
      auto signal_it = signals_.find(signal_name);
      if (signal_it == signals_.end()) {
        return absl::NotFoundError(
            absl::StrCat("Signal `", signal_name, "` was not found."));
      }

      signals.insert({signal_name, signal_it->second});
    }

    return signals;
  }

  absl::StatusOr<std::shared_ptr<Signal>> GetSignal(
      absl::string_view signal_name) ABSL_SHARED_LOCKS_REQUIRED(signal_mu_) {
    auto signal_it = signals_.find(signal_name);
    if (signal_it == signals_.end()) {
      return absl::NotFoundError(
          absl::StrCat("Signal `", signal_name, "` was not found."));
    }
    return signal_it->second;
  }

  absl::Status CommitSignalWrites(
      const std::set<std::string>& claimed_signals) {
    // Early exit if ICON is not enabled, even if we have no signals to write.
    INTR_ASSIGN_OR_RETURN(auto icon_status,
                          icon_client_->GetOperationalStatus());
    if (!IsEnabled(icon_status)) {
      return absl::AbortedError(
          absl::StrCat("Cannot commit writes because ICON is not enabled. ICON "
                       "is in state: ",
                       ToString(icon_status)));
    }
    // For each part which must be written to, we will create a separate action
    // which must have corresponding ADIOActionInfo::FixedParams. Below we
    // iterate through all the signals which must be written to and add the
    // block info to the corresponding part ADIOActionInfo::FixedParams.
    // Using b_tree_map for deterministic iteration order.
    absl::btree_map<std::string, icon::ADIOActionInfo::FixedParams>
        claimed_signal_part_names_and_action_params;

    // TODO(b/228630961): Reconcile block naming conventions between ICON and
    // the GPIO API.
    {
      absl::ReaderMutexLock l(&signal_mu_);
      for (const auto& [signal_name, signal] : signals_) {
        // Only write claimed signals (this check skips signals not claimed by
        // *ANY* session).
        if (!signal->is_claimed_by_session) {
          continue;
        }

        // Only write signals with written values.
        if (!signal->written_value.has_value()) {
          continue;
        }

        // Only write signals that are claimed by this specific write session.
        if (!claimed_signals.contains(signal_name)) {
          continue;
        }

        absl::string_view block_name = signal->icon_block_name;
        uint32_t block_index = signal->signal_block_index;

        auto* outputs =
            claimed_signal_part_names_and_action_params[signal->icon_part_name]
                .mutable_outputs();

        VLOG(0) << "Write " << *signal->written_value << " to " << block_name
                << "[" << block_index << "]";

        switch (signal->written_value->value_case()) {
          case SignalValue::kBoolValue:
            if (!outputs->digital_outputs().contains(block_name)) {
              (*outputs->mutable_digital_outputs())[std::string(block_name)] =
                  intrinsic_proto::icon::actions::proto::DigitalBlock();
            }
            outputs->mutable_digital_outputs()
                ->at(block_name)
                .mutable_values_by_index()
                ->insert({block_index, signal->written_value->bool_value()});
            break;
          case SignalValue::kDoubleValue:
            if (!outputs->analog_outputs().contains(block_name)) {
              (*outputs->mutable_analog_outputs())[std::string(block_name)] =
                  intrinsic_proto::icon::actions::proto::AnalogOutputBlock();
            }
            outputs->mutable_analog_outputs()
                ->at(block_name)
                .mutable_values_by_index()
                ->insert({block_index, signal->written_value->double_value()});
            break;
          default:
            return absl::InvalidArgumentError(
                absl::StrCat(signal_name, " must be a bool or double value."));
        }
      }
    }

    // Apply a "generous" timeout for connecting to ICON.
    constexpr absl::Duration kTimeout = absl::Seconds(2);

    std::vector<std::string> controlled_parts;
    for (const auto& [part_name, _] :
         claimed_signal_part_names_and_action_params) {
      controlled_parts.push_back(part_name);
    }

    // If there are no parts to write to, we can just return.
    if (controlled_parts.empty()) {
      return absl::OkStatus();
    }

    INTR_ASSIGN_OR_RETURN_GRPC(
        auto icon_session,
        icon::Session::Start(icon_channel_,
                             /*parts=*/
                             controlled_parts,
                             /*context=*/{},
                             /*deadline=*/absl::Time(absl::Now() + kTimeout)),
        _.LogError());
    if (icon_session == nullptr) {
      return absl::InternalError(
          "Failed to acquire ICON session. "
          "The GPIO server may be in a bad state.");
    }

    icon::ReactionHandle outputs_set_handle(++reaction_id_);
    std::vector<icon::ActionDescriptor> adio_action_descriptors;
    std::vector<icon::ActionInstanceId> action_instance_ids;
    int i = 0;
    for (const auto& [part_name, action_params] :
         claimed_signal_part_names_and_action_params) {
      auto action_instance_id = icon::ActionInstanceId(++action_instance_id_);
      icon::ActionDescriptor adio_action =
          icon::ActionDescriptor(icon::ADIOActionInfo::kActionTypeName,
                                 action_instance_id, part_name)
              .WithFixedParams(action_params);
      if (i != claimed_signal_part_names_and_action_params.size() - 1) {
        // This action will be followed by another action.
        adio_action.WithReaction(
            icon::ReactionDescriptor(
                icon::IsTrue(icon::ADIOActionInfo::kOutputsSet))
                .WithRealtimeActionOnCondition(
                    icon::ActionInstanceId(action_instance_id.value() + 1)));
      } else {
        // This is the terminal action.
        adio_action.WithReaction(
            icon::ReactionDescriptor(
                icon::IsTrue(icon::ADIOActionInfo::kOutputsSet))
                .WithHandle(outputs_set_handle));
      }
      adio_action_descriptors.push_back(adio_action);
      action_instance_ids.push_back(action_instance_id);
      LOG(INFO) << "Added Action: " << action_instance_id;
      i++;
    }

    INTR_ASSIGN_OR_RETURN(std::vector<icon::Action> actions,
                          icon_session->AddActions(adio_action_descriptors));
    INTR_RETURN_IF_ERROR(icon_session->StartActions({*actions.begin()}));
    INTR_RETURN_IF_ERROR(icon_session->RunWatcherLoopUntilReaction(
        outputs_set_handle, absl::Now() + absl::Seconds(5)));

    // TODO(keegang): ICON may try to remove this from the API, so work with
    // them to come up with a workaround (probably one session per action).
    return icon_session->RemoveActions(action_instance_ids);
  }

  class WriteSessionHandler {
   public:
    explicit WriteSessionHandler(
        absl::flat_hash_map<std::string, std::shared_ptr<Signal>>
            claimed_signals,
        std::function<absl::Status()> commit_writes_func)
        : claimed_signals_(std::move(claimed_signals)),
          commit_writes_func_(commit_writes_func) {
      // Mark all signals as claimed here, since the dtor will take
      // responsibility for releasing them.
      for (const auto& signal : claimed_signals_) {
        signal.second->is_claimed_by_session = true;
      }
    }

    ~WriteSessionHandler() {
      for (auto& signal : claimed_signals_) {
        signal.second->is_claimed_by_session = false;
        signal.second->written_value.reset();
      }

      auto status = commit_writes_func_();
      if (!status.ok()) {
        LOG(WARNING) << "Failed to commit writes on session handler "
                        "destruction. Error: "
                     << status;
      }
    }

    absl::Status HandleWriteOperation(
        const intrinsic_proto::gpio::v1::WriteSignalsRequest& request) {
      for (const auto& signal_request : request.signal_values().values()) {
        if (!claimed_signals_.contains(signal_request.first)) {
          return absl::PermissionDeniedError(
              absl::StrCat("Signal `", signal_request.first,
                           "` was not claimed by this session."));
        }
        auto& claimed_signal = claimed_signals_.at(signal_request.first);
        claimed_signal->written_value = signal_request.second;
      }

      return commit_writes_func_();
    }

   private:
    absl::flat_hash_map<std::string, std::shared_ptr<Signal>> claimed_signals_;
    std::function<absl::Status()> commit_writes_func_;
  };

  absl::StatusOr<std::unique_ptr<WriteSessionHandler>> CreateSession(
      const intrinsic_proto::gpio::v1::OpenWriteSessionRequest::
          InitialSessionData& initial_session_data)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(signal_mu_) {
    INTR_ASSIGN_OR_RETURN(auto signals,
                          GetSignals(initial_session_data.signal_names()));

    std::set<std::string> claimed_signal_names;
    for (const auto& [signal_name, signal] : signals) {
      if (signal->is_claimed_by_session) {
        return absl::PermissionDeniedError(
            absl::StrCat("Signal `", signal_name,
                         "` is already claimed by another session."));
      }
      claimed_signal_names.insert(signal_name);
    }
    LOG(INFO) << "Gpio service claimed " << signals.size() << " signals.";

    return std::make_unique<WriteSessionHandler>(
        signals,
        [this, claimed_signal_names = std::move(claimed_signal_names)] {
          return this->CommitSignalWrites(claimed_signal_names);
        });
  }
};

}  // namespace

absl::StatusOr<std::unique_ptr<intrinsic_proto::gpio::v1::GPIOService::Service>>
MakeIconGPIOService(const IconGpioServiceConfig& config,
                    std::shared_ptr<intrinsic::ChannelInterface> icon_channel) {
  if (config.parts().empty()) {
    LOG(INFO) << "No ADIO parts are configured. An empty GPIO server will be "
                 "created.";
    return std::make_unique<EmptyIconGPIOService>();
  }
  return IconGPIOService::Make(config, icon_channel);
}
}  // namespace intrinsic
