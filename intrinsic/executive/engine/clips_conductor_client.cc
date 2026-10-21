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

#include "intrinsic/executive/engine/clips_conductor_client.h"

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/flags/flag.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "google/longrunning/operations.pb.h"
#include "google/protobuf/empty.pb.h"
#include "grpcpp/client_context.h"
#include "grpcpp/support/time.h"
#include "intrinsic/conductor/proto/conductor.grpc.pb.h"
#include "intrinsic/conductor/proto/conductor.pb.h"
#include "intrinsic/executive/clips_cpp/assert_facade.h"
#include "intrinsic/executive/clips_cpp/function_facade.h"
#include "intrinsic/executive/clips_cpp/protobuf.h"
#include "intrinsic/executive/clips_cpp/value.h"
#include "intrinsic/util/grpc/grpc.h"
#include "intrinsic/util/status/extended_status.pb.h"
#include "intrinsic/util/status/return.h"
#include "intrinsic/util/status/status_builder.h"
#include "intrinsic/util/status/status_conversion_grpc.h"
#include "intrinsic/util/status/status_conversion_rpc.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/status/status_specs.h"

ABSL_FLAG(bool, enable_conductor_process_start, true,
          "If true, the conductor will be notified when a process is started "
          "and the process will not start executing until the conductor has "
          "completed preparations.");

namespace intrinsic {
namespace executive {
using ::intrinsic_proto::conductor::PrepareProcessStartRequest;
using ::intrinsic_proto::conductor::PrepareProcessStartResponse;
using ::intrinsic_proto::status::ExtendedStatus;

namespace {
constexpr char kConductorCheckPreparationIsDoneAsync[] =
    "conductor-check-preparation-is-done-async";
constexpr char kConductorCancelPreparationAsync[] =
    "conductor-cancel-preparation-async";
constexpr char kConductorPrepareProcessResume[] =
    "conductor-prepare-process-resume";
constexpr char kConductorNotifyAllProcessesStopped[] =
    "conductor-notify-all-processes-stopped";

constexpr absl::Duration kConductorLROStartRpcDeadline = absl::Seconds(10);

PrepareProcessStartRequest CreatePrepareProcessStartRequest(
    absl::string_view executive_operation_name, absl::string_view scene_id,
    const std::vector<ClipsConductorClient::ExecutiveOperationInfo>&
        all_executive_operations) {
  // TODO(b/311443570): Update once conductor accepts scene IDs.
  PrepareProcessStartRequest request;
  request.set_init_world_id(scene_id);
  for (const auto& operation_info : all_executive_operations) {
    if (operation_info.name != executive_operation_name) {
      PrepareProcessStartRequest::ProcessInfo* info =
          request.add_other_process_info();
      info->set_init_world_id(operation_info.scene_id);
      info->set_state(operation_info.state);
    }
  }
  return request;
}

struct ConductorOperationUpdateParams {
  bool is_done = false;
  bool has_error = false;
  absl::string_view world_id = "";
  std::optional<ExtendedStatus> extended_status = std::nullopt;
};
void AssertConductorOperationUpdateFact(
    clips::EnvironmentAssertFacade* assert_facade,
    clips::ProtobufManager* protobuf_manager,
    absl::string_view conductor_operation_name,
    ConductorOperationUpdateParams params)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(assert_facade->GetClipsMutex()) {
  clips::ProtoMessageId extended_status_proto_id =
      clips::ProtobufManager::kInvalidId;
  if (params.extended_status.has_value()) {
    extended_status_proto_id =
        protobuf_manager->AddGeneratedProto(*std::move(params.extended_status));
  }
  INTR_RETURN_IF_ERROR(
      assert_facade
          ->AssertFact(
              "conductor-update-client-operation",
              {
                  {"conductor-operation-name", conductor_operation_name},
                  {"is-done", params.is_done ? clips::Symbol::True()
                                             : clips::Symbol::False()},
                  {"has-error", params.has_error ? clips::Symbol::True()
                                                 : clips::Symbol::False()},
                  {"world-id", params.world_id},
                  {"extended-status-proto-id",
                   extended_status_proto_id.value()},
              })
          .status())
      .LogError()
      .With(
          intrinsic::ExtraMessage()
          << "Failed to update conductor-update-client-operation fact in CLIPS")
      .With([protobuf_manager, extended_status_proto_id](absl::Status unused) {
        // Remove the extended status proto if the
        // conductor-update-client-operation fact could not be asserted.
        if (extended_status_proto_id != clips::ProtobufManager::kInvalidId) {
          protobuf_manager->RemoveProto(extended_status_proto_id);
        }
      });
  assert_facade->NotifyRunner();
}

absl::StatusOr<PrepareProcessStartResponse> UnpackPrepareProcessStartResponse(
    const google::protobuf::Any& any_response) {
  PrepareProcessStartResponse response;
  if (!any_response.UnpackTo(&response)) {
    return absl::InternalError(
        "Conductor::PrepareProcessStart response cannot be unpacked.");
  }
  return response;
}

}  // namespace

ClipsConductorClient::ClipsConductorClient(
    clips::EnvironmentAssertFacade* assert_facade,
    clips::ProtobufManager* protobuf_manager,
    intrinsic_proto::conductor::ConductorService::StubInterface* conductor_stub)
    : assert_facade_(assert_facade),
      protobuf_manager_(protobuf_manager),
      conductor_stub_(conductor_stub) {}

absl::Status ClipsConductorClient::Init(
    clips::EnvironmentFunctionFacade* function_facade,
    absl::Duration client_operation_poll_interval)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(function_facade->clips_mutex()) {
  assert_facade_->GetClipsMutex()->AssertHeld();
  if (client_operation_poll_interval <= absl::ZeroDuration() &&
      absl::GetFlag(FLAGS_enable_conductor_process_start)) {
    return absl::InvalidArgumentError(
        "Client operation poll interval must be a positive duration.");
  }

  // Create a thread pool with 10 threads.
  bundle_.emplace(10);

  INTR_RETURN_IF_ERROR(
      assert_facade_
          ->AssertFact(
              "flag",
              {{"name",
                clips::Symbol("conductor-client-operation-poll-interval")},
               {"type", clips::Symbol("FLOAT")},
               {"value",
                absl::ToDoubleSeconds(client_operation_poll_interval)}})
          .status());

  INTR_RETURN_IF_ERROR(function_facade->AddFunction(
      kConductorCheckPreparationIsDoneAsync,
      std::function([this](const std::string& conductor_operation_name) {
        CheckConductorPrepareProcessStartDoneAsync(conductor_operation_name);
      })));

  INTR_RETURN_IF_ERROR(function_facade->AddFunction(
      kConductorCancelPreparationAsync,
      std::function([this](const std::string& conductor_operation_name) {
        CancelConductorPrepareProcessStartAsync(conductor_operation_name);
      })));

  INTR_RETURN_IF_ERROR(function_facade->AddFunction(
      kConductorPrepareProcessResume, std::function([this]() -> clips::Values {
        INTR_RETURN_IF_ERROR(PrepareConductorProcessResume())
            .LogWarning()
            .With([](const absl::Status& s) -> clips::Values {
              return {clips::Symbol::False(), clips::Value(s.message())};
            });
        return {clips::Symbol::True(), clips::Value("")};
      })));

  INTR_RETURN_IF_ERROR(function_facade->AddFunction(
      kConductorNotifyAllProcessesStopped,
      std::function([this]() -> clips::Values {
        INTR_RETURN_IF_ERROR(NotifyConductorAllProcessesStopped())
            .LogWarning()
            .With([](const absl::Status& s) -> clips::Values {
              return {clips::Symbol::False(), clips::Value(s.message())};
            });
        return {clips::Symbol::True(), clips::Value("")};
      })));
  return absl::OkStatus();
}

absl::Status ClipsConductorClient::TearDown() {
  // Invokes the dtor for all threads and calls cancel prior to joining.
  bundle_.reset();
  return absl::OkStatus();
}

absl::StatusOr<ClipsConductorClient::ConductorClientOperationInfo>
ClipsConductorClient::PrepareConductorProcessStart(
    absl::string_view executive_operation_name, absl::string_view scene_id,
    const std::vector<ExecutiveOperationInfo>& all_executive_operations) const {
  if (!absl::GetFlag(FLAGS_enable_conductor_process_start)) {
    const std::string dummy_conductor_operation_name =
        absl::StrFormat("feature-disabled-%s", executive_operation_name);
    return ConductorClientOperationInfo{.name = dummy_conductor_operation_name,
                                        .is_done = true};
  }

  if (conductor_stub_ == nullptr) {
    return absl::FailedPreconditionError("Conductor stub not initialized");
  }
  PrepareProcessStartRequest request = CreatePrepareProcessStartRequest(
      executive_operation_name, scene_id, all_executive_operations);
  google::longrunning::Operation prepare_operation;
  LOG(INFO) << "Calling Conductor::PrepareProcessStart RPC.";
  grpc::ClientContext context;
  // Set a short deadline since this is the start of an LRO and expected to
  // return quickly.
  context.set_deadline(absl::Now() + kConductorLROStartRpcDeadline);
  INTR_RETURN_IF_ERROR(ToAbslStatus(conductor_stub_->PrepareProcessStart(
      &context, request, &prepare_operation)));

  if (prepare_operation.name().empty()) {
    return absl::InternalError(
        "PrepareProcessStart returned an unnamed operation!");
  }

  return ConductorClientOperationInfo{.name = prepare_operation.name(),
                                      .is_done = false};
}

absl::Status ClipsConductorClient::AssertStopConductorPreparationFact(
    absl::string_view conductor_operation_name)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(assert_facade_->GetClipsMutex()) {
  if (!absl::GetFlag(FLAGS_enable_conductor_process_start)) {
    return absl::OkStatus();
  }
  return assert_facade_
      ->AssertFact("conductor-modify-client-operation",
                   {{"action", clips::Symbol("STOP-PREPARATION")},
                    {"conductor-operation-name", conductor_operation_name}})
      .status();
}

absl::StatusOr<std::optional<PrepareProcessStartResponse>>
ClipsConductorClient::GetConductorPrepareProcessStartResponse(
    absl::string_view conductor_operation_name) {
  if (conductor_stub_ == nullptr) {
    return absl::FailedPreconditionError("Conductor stub not initialized");
  }
  grpc::ClientContext context;
  intrinsic::ConfigureClientContext(&context);
  google::longrunning::GetOperationRequest request;
  request.set_name(conductor_operation_name);
  google::longrunning::Operation operation;

  const absl::Status get_op_status = ToAbslStatus(
      conductor_stub_->GetOperation(&context, request, &operation));

  // Ignore Unavailable and Unimplemented errors since they can be transient.
  if (absl::IsUnavailable(get_op_status) ||
      absl::IsUnimplemented(get_op_status)) {
    return std::nullopt;
  } else if (!get_op_status.ok()) {
    LOG(ERROR) << "Failed to get status for conductor operation '"
               << conductor_operation_name << "': " << get_op_status;
    return absl::InternalError(
        "Failed to get status. Please submit a support ticket.");
  }
  return ExtractPrepareProcessStartResponse(operation);
}

absl::StatusOr<
    std::optional<intrinsic_proto::conductor::PrepareProcessStartResponse>>
ClipsConductorClient::ExtractPrepareProcessStartResponse(
    const google::longrunning::Operation& operation) {
  if (!operation.done()) {
    return std::nullopt;
  }

  if (operation.has_error()) {
    const absl::Status absl_error = ToAbslStatus(operation.error());
    if (absl_error.ok()) {
      LOG(ERROR)
          << "Conductor returned an operation proto for client operation '"
          << operation.name()
          << "' with an error set but the error code is Ok.";
      return absl::InternalError(
          "Response is inconsistent. Please submit a support ticket.");
    }
    return ToAbslStatus(operation.error());
  }

  INTR_ASSIGN_OR_RETURN(
      PrepareProcessStartResponse response,
      UnpackPrepareProcessStartResponse(operation.response()),
      _.LogError()
          .With(intrinsic::ExtraMessage()
                << "Conductor operation '" << operation.name() << "'.")
          .With(Return(absl::InternalError(
              "Response is invalid. Please submit a support ticket."))));
  return response;
}

void ClipsConductorClient::CheckConductorPrepareProcessStartDoneAsync(
    absl::string_view conductor_operation_name) {
  absl::Status schedule_status = bundle_->Schedule(
      [this, conductor_operation_name = std::string(conductor_operation_name)] {
        INTR_ASSIGN_OR_RETURN(
            std::optional<PrepareProcessStartResponse> response,
            GetConductorPrepareProcessStartResponse(conductor_operation_name),
            _.With([this,
                    &conductor_operation_name](const absl::Status& status) {
              // Report user-facing error and mark the operation fact as done
              // with error. Note that the extended status user message is left
              // empty since it is populated in CLIPS.
              // TODO(b/375253985): Pass extended status from conductor once it
              // is supported.
              ExtendedStatus extended_status =
                  CreateExtendedStatus(21101, /*user_message=*/"",
                                       {.debug_message = status.ToString()});
              absl::MutexLock clips_lock(*assert_facade_->GetClipsMutex());
              AssertConductorOperationUpdateFact(
                  assert_facade_, protobuf_manager_, conductor_operation_name,
                  {.is_done = true,
                   .has_error = true,
                   .extended_status = extended_status});
            }));

        absl::MutexLock clips_lock(*assert_facade_->GetClipsMutex());
        if (response.has_value()) {
          AssertConductorOperationUpdateFact(
              assert_facade_, protobuf_manager_, conductor_operation_name,
              {.is_done = true,
               .has_error = false,
               .world_id = response->belief_world_id()});
        } else {
          AssertConductorOperationUpdateFact(assert_facade_, protobuf_manager_,
                                             conductor_operation_name,
                                             {.is_done = false});
        }
      });
  LOG_IF(ERROR, !schedule_status.ok())
      << "Failed to schedule status check for conductor client operation '"
      << conductor_operation_name << "': " << schedule_status;
}

void ClipsConductorClient::CancelConductorPrepareProcessStartAsync(
    absl::string_view conductor_operation_name) {
  if (conductor_stub_ == nullptr) {
    LOG(ERROR) << "Conductor stub not initialized";
  }
  absl::Status schedule_status = bundle_->Schedule(
      [this, conductor_operation_name = std::string(conductor_operation_name)] {
        LOG(INFO) << "Cancelling conductor client operation '"
                  << conductor_operation_name << "'.";
        grpc::ClientContext context;
        intrinsic::ConfigureClientContext(&context);
        google::longrunning::CancelOperationRequest request;
        request.set_name(conductor_operation_name);
        google::protobuf::Empty empty;
        INTR_RETURN_IF_ERROR(ToAbslStatus(conductor_stub_->CancelOperation(
                                 &context, request, &empty)))
            .LogWarning()
            .With(ReturnVoid());
      });
  LOG_IF(ERROR, !schedule_status.ok())
      << "Failed to schedule cancellation for conductor client operation '"
      << conductor_operation_name << "': " << schedule_status;
}

absl::Status ClipsConductorClient::PrepareConductorProcessResume() {
  if (conductor_stub_ == nullptr) {
    return absl::FailedPreconditionError("Conductor stub not initialized");
  }
  LOG(INFO) << "Calling Conductor::PrepareProcessResume RPC.";
  grpc::ClientContext context;
  intrinsic::ConfigureClientContext(&context);
  intrinsic_proto::conductor::PrepareProcessResumeRequest request;
  intrinsic_proto::conductor::PrepareProcessResumeResponse response;
  INTR_RETURN_IF_ERROR(ToAbslStatus(
      conductor_stub_->PrepareProcessResume(&context, request, &response)));
  LOG(INFO) << "Conductor::PrepareProcessResume RPC returned ok.";
  return absl::OkStatus();
}

absl::Status ClipsConductorClient::NotifyConductorAllProcessesStopped() {
  if (conductor_stub_ == nullptr) {
    return absl::FailedPreconditionError("Conductor stub not initialized");
  }
  LOG(INFO) << "Calling Conductor::NotifyAllProcessesStopped RPC.";
  grpc::ClientContext context;
  intrinsic::ConfigureClientContext(&context);
  intrinsic_proto::conductor::NotifyAllProcessesStoppedRequest request;
  intrinsic_proto::conductor::NotifyAllProcessesStoppedResponse response;
  INTR_RETURN_IF_ERROR(ToAbslStatus(conductor_stub_->NotifyAllProcessesStopped(
      &context, request, &response)));
  LOG(INFO) << "Conductor::NotifyAllProcessesStopped RPC returned ok.";
  return absl::OkStatus();
}

}  // namespace executive
}  // namespace intrinsic
