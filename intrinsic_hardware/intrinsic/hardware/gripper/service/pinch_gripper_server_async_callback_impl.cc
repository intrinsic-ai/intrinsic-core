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

#include "intrinsic/hardware/gripper/service/pinch_gripper_server_async_callback_impl.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "intrinsic/assets/services/proto/v1/service_state.pb.h"
#include "intrinsic/hardware/gripper/gripper.h"
// `robotiq_gripper` is needed for registration with PinchGripperFactory.
#include "intrinsic/hardware/gripper/robotiq/robotiq_gripper.h"  // IWYU pragma: keep
#include "intrinsic/hardware/gripper/service/proto/generic_pinch_gripper_configs.pb.h"
#include "intrinsic/hardware/gripper/service/proto/pinch_gripper_server.pb.h"
// `wsg32_gripper` is needed for registration with PinchGripperFactory.
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "grpcpp/server_context.h"
#include "grpcpp/support/server_callback.h"
#include "grpcpp/support/status.h"
#include "intrinsic/hardware/gripper/wsg32/wsg32_gripper.h"  // IWYU pragma: keep
#include "intrinsic/resources/health/operational_status.h"
#include "intrinsic/util/proto_time.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/status/status_macros_grpc.h"
#include "intrinsic/util/thread/thread_pool.h"

namespace intrinsic::gripper {
namespace {

using ::grpc::ServerUnaryReactor;
using ::intrinsic_proto::gripper::CommandPinchGripperRequest;
using ::intrinsic_proto::gripper::CommandPinchGripperResponse;
using ::intrinsic_proto::gripper::CreatePinchGripperRequest;
using ::intrinsic_proto::gripper::CreatePinchGripperResponse;
using ::intrinsic_proto::gripper::GetPinchGripperStatusRequest;
using ::intrinsic_proto::gripper::GetPinchGripperStatusResponse;
using ::intrinsic_proto::gripper::PinchGripperConfig;
using ::intrinsic_proto::gripper::ResetPinchGripperRequest;
using ::intrinsic_proto::gripper::ResetPinchGripperResponse;

// A service only controls a single gripper.
constexpr int kNumMaxGrippers = 1;

// The wait time between the sending time of the gripper command and the
// completion of the hardware execution.
constexpr absl::Duration kDefaultGripperHardwareExecutionWaitTime =
    absl::Seconds(5);

}  // namespace

PinchGripperServerAsyncCallbackImpl::PinchGripperServerAsyncCallbackImpl(
    const intrinsic_proto::gripper::PinchGripperConfig& config,
    const absl::string_view pinch_gripper_type,
    std::optional<absl::Duration> default_timeout)
    :  // Create as many threads as the maximum number of grippers.
      cyclic_commander_pool_(std::make_unique<ThreadPool>(kNumMaxGrippers)),
      pinch_gripper_type_(pinch_gripper_type),
      config_(config),
      default_timeout_(
          default_timeout.value_or(kDefaultGripperHardwareExecutionWaitTime)) {
  static_assert(kNumMaxGrippers == 1);
}

absl::StatusOr<PinchGripperInterface*>
PinchGripperServerAsyncCallbackImpl::GetPinchGripper() const {
  if (pinch_gripper_ == nullptr) {
    return absl::FailedPreconditionError("Gripper has not been initialized!");
  }
  return pinch_gripper_.get();
}

ServerUnaryReactor* PinchGripperServerAsyncCallbackImpl::CreatePinchGripper(
    grpc::CallbackServerContext* context, const CreatePinchGripperRequest*,
    CreatePinchGripperResponse* response) {
  // Creates the handle for the gripper but does not update the health state
  // machine. That happens in `ResetPinchGripper`.
  ServerUnaryReactor* reactor = context->DefaultReactor();
  auto status = CreatePinchGripperInternal(response);
  if (!status.ok()) {
    absl::MutexLock lock(mutex_);
    state_machine_.SetState(resources::OperationalState::kFaulted);
  }
  reactor->Finish(ToGrpcStatus(status));
  return reactor;
}

absl::Status PinchGripperServerAsyncCallbackImpl::CreatePinchGripperInternal(
    CreatePinchGripperResponse* response) {
  LOG(INFO) << "Using pinch gripper config provided to the constructor.";
  const PinchGripperConfig& config = config_;
  const std::string& pinch_gripper_type = pinch_gripper_type_;
  const std::string& gripper_handle = config.name();

  absl::MutexLock lock(mutex_);
  if (!GetPinchGripper().ok()) {
    if (pinch_gripper_type.empty()) {
      return absl::InvalidArgumentError(
          "Cannot create a gripper because no pinch gripper type is "
          "specified.");
    }
    LOG(INFO) << "Creating a " << pinch_gripper_type
              << " with gripper handle: " << gripper_handle;
    google::protobuf::Any any_config;
    if (config.has_generic_pinch_gripper_config()) {
      if (!any_config.PackFrom(config.generic_pinch_gripper_config())) {
        return absl::InternalError("Failed to pack pinch gripper config.");
      }
    } else {
      return absl::InvalidArgumentError(
          "Cannot create a gripper because no communication config "
          "is specified.");
    }
    INTR_ASSIGN_OR_RETURN(
        pinch_gripper_,
        PinchGripperFactory::Create(pinch_gripper_type, any_config),
        _.LogError());

    LOG(INFO) << "Created a Pinch Gripper with gripper handle: "
              << gripper_handle;

    // We are validating all SI unit configurations, for position, velocity,
    // and effort. Hence we set all of `validate_si_position_config`,
    // `validate_si_velocity_config`, and `validate_si_effort_config` to true.
    INTR_RETURN_IF_ERROR(pinch_gripper_->Configure(
        config.generic_pinch_gripper_config().physical_config(),
        /* validate_si_position_config = */ true,
        /* validate_si_velocity_config = */ true,
        /* validate_si_effort_config = */ true));

    response->set_created_before(false);

    // Run a CyclicRunner that keeps sending the last gripper command
    // periodically at a specified rate to the gripper.
    INTR_RETURN_IF_ERROR(
        cyclic_commander_pool_->Schedule([gripper = pinch_gripper_.get()] {
          if (auto cyclic_run_status = gripper->RunCyclicKeepAliveRoutine();
              !cyclic_run_status.ok()) {
            LOG(ERROR) << "RunCyclicGripperCommander() returns error: "
                       << cyclic_run_status.message();
          }
        }));
  } else {
    LOG(INFO) << "Pinch Gripper with gripper handle: " << gripper_handle
              << " already exists!";
    response->set_created_before(true);
  }

  response->set_gripper_handle(gripper_handle);
  return absl::OkStatus();
}

ServerUnaryReactor* PinchGripperServerAsyncCallbackImpl::ResetPinchGripper(
    grpc::CallbackServerContext* context, const ResetPinchGripperRequest*,
    ResetPinchGripperResponse* response) {
  ServerUnaryReactor* reactor = context->DefaultReactor();
  reactor->Finish(ToGrpcStatus(ResetPinchGripperInternal(response)));
  return reactor;
}

absl::Status PinchGripperServerAsyncCallbackImpl::ResetPinchGripperInternal(
    ResetPinchGripperResponse* response) {
  absl::MutexLock lock(mutex_);
  // TODO(b/277614894): Once the health service has been rolled out, `Create`
  // and `Reset` rpc calls should be deleted from the pinch gripper service.

  // For reset, we only try to reset the underlying gripper and not try to
  // enable or disable it. The high level state machine handles the operational
  // state transitions.
  auto clear_faults_action = [this]() -> absl::Status {
    // TODO(b/247998880): Using `std::invoke` as a workaround to silence false
    // positive errors with -Wthread-safety-analysis.
    return std::invoke(&PinchGripperServerAsyncCallbackImpl::ClearFaultsAction,
                       this);
  };
  auto disable_action = []() -> absl::Status { return absl::OkStatus(); };
  INTR_RETURN_IF_ERROR_GRPC(
      state_machine_.ClearFaultsAndDisable(clear_faults_action, disable_action))
      .LogError();

  return state_machine_.Enable();
}

ServerUnaryReactor* PinchGripperServerAsyncCallbackImpl::CommandPinchGripper(
    grpc::CallbackServerContext* context,
    const CommandPinchGripperRequest* request,
    CommandPinchGripperResponse* response) {
  ServerUnaryReactor* reactor = context->DefaultReactor();
  reactor->Finish(ToGrpcStatus(CommandPinchGripperInternal(request, response)));
  return reactor;
}

absl::Status PinchGripperServerAsyncCallbackImpl::CommandPinchGripperInternal(
    const CommandPinchGripperRequest* request,
    CommandPinchGripperResponse* response) {
  absl::MutexLock lock(mutex_);
  INTR_RETURN_IF_ERROR_GRPC(RequireIsEnabled());
  INTR_ASSIGN_OR_RETURN(auto gripper, GetPinchGripper(), _.LogError());
  absl::Duration execution_timeout;
  if (request->has_execution_timeout()) {
    INTR_ASSIGN_OR_RETURN(execution_timeout,
                          ToAbslDuration(request->execution_timeout()));
  } else {
    execution_timeout = default_timeout_;
  }
  const absl::Time deadline = absl::Now() + execution_timeout;
  INTR_ASSIGN_OR_RETURN(
      *response->mutable_status(),
      gripper->ExecuteCommand(request->command(), deadline, true));

  return absl::OkStatus();
}

ServerUnaryReactor* PinchGripperServerAsyncCallbackImpl::GetPinchGripperStatus(
    grpc::CallbackServerContext* context,
    const GetPinchGripperStatusRequest* request,
    GetPinchGripperStatusResponse* response) {
  ServerUnaryReactor* reactor = context->DefaultReactor();
  reactor->Finish(
      ToGrpcStatus(GetPinchGripperStatusInternal(request, response)));
  return reactor;
}

absl::Status PinchGripperServerAsyncCallbackImpl::GetPinchGripperStatusInternal(
    const GetPinchGripperStatusRequest* request,
    GetPinchGripperStatusResponse* response) {
  absl::MutexLock lock(mutex_);
  INTR_ASSIGN_OR_RETURN(auto gripper, GetPinchGripper(), _.LogError());
  INTR_ASSIGN_OR_RETURN(*response->mutable_status(), gripper->GetStatus());
  return absl::OkStatus();
}

grpc::Status PinchGripperServerAsyncCallbackImpl::InitializeAndEnable() {
  // Each of the following calls internally lock the mutex.
  {
    CreatePinchGripperResponse response;
    INTR_RETURN_IF_ERROR_GRPC(CreatePinchGripperInternal(&response)).LogError();
  }

  {
    ResetPinchGripperResponse response;
    INTR_RETURN_IF_ERROR_GRPC(ResetPinchGripperInternal(&response)).LogError();
  }
  return Enable();
}

intrinsic_proto::services::v1::SelfState
PinchGripperServerAsyncCallbackImpl::GetState() const {
  absl::MutexLock lock(mutex_);
  return state_machine_.GetState();
}

grpc::Status PinchGripperServerAsyncCallbackImpl::Enable() {
  absl::MutexLock lock(mutex_);
  // TODO(dhirajgoel): transition to faulted state if this fails?
  INTR_RETURN_IF_ERROR_GRPC(this->EnableGripperAction()).LogError();
  return ToGrpcStatus(state_machine_.Enable());
}

grpc::Status PinchGripperServerAsyncCallbackImpl::Disable() {
  absl::MutexLock lock(mutex_);
  auto disable_action = [this]() {
    // TODO(b/247998880): Using `std::invoke` as a workaround to silence false
    // positive errors with -Wthread-safety-analysis.
    return std::invoke(
        &PinchGripperServerAsyncCallbackImpl::DisableGripperAction, this);
  };
  return ToGrpcStatus(state_machine_.Disable(disable_action));
}

grpc::Status PinchGripperServerAsyncCallbackImpl::ClearFaults() {
  // Initializes the gripper if not already done.
  {
    CreatePinchGripperResponse response;
    INTR_RETURN_IF_ERROR_GRPC(CreatePinchGripperInternal(&response)).LogError();
  }

  absl::MutexLock lock(mutex_);
  auto clear_faults_action = [this]() {
    // TODO(b/247998880): Using `std::invoke` as a workaround to silence false
    // positive errors with -Wthread-safety-analysis.
    return std::invoke(&PinchGripperServerAsyncCallbackImpl::ClearFaultsAction,
                       this);
  };

  auto disable_action = [this]() {
    // TODO(b/247998880): Using `std::invoke` as a workaround to silence false
    // positive errors with -Wthread-safety-analysis.
    return std::invoke(
        &PinchGripperServerAsyncCallbackImpl::DisableGripperAction, this);
  };
  return ToGrpcStatus(state_machine_.ClearFaultsAndDisable(clear_faults_action,
                                                           disable_action));
}

absl::Status PinchGripperServerAsyncCallbackImpl::EnableGripperAction() {
  INTR_ASSIGN_OR_RETURN(auto gripper, GetPinchGripper(), _.LogError());
  return gripper->Enable();
}

absl::Status PinchGripperServerAsyncCallbackImpl::DisableGripperAction() {
  INTR_ASSIGN_OR_RETURN(auto gripper, GetPinchGripper(), _.LogError());
  return gripper->Disable();
}

absl::Status PinchGripperServerAsyncCallbackImpl::ClearFaultsAction() {
  INTR_ASSIGN_OR_RETURN(auto gripper, GetPinchGripper(), _.LogError());
  return gripper->Reset();
}

}  // namespace intrinsic::gripper
