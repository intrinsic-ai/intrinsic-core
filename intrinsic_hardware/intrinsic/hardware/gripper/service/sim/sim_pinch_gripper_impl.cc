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

#include "intrinsic/hardware/gripper/service/sim/sim_pinch_gripper_impl.h"

#include <string>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/synchronization/mutex.h"
#include "grpcpp/client_context.h"
#include "intrinsic/assets/services/proto/v1/service_state.pb.h"
#include "intrinsic/hardware/gripper/service/pinch_gripper_extended_status.h"
#include "intrinsic/hardware/gripper/service/proto/pinch_gripper_server.pb.h"
#include "intrinsic/resources/health/operational_status.h"
#include "intrinsic/util/status/status_conversion_grpc.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic::gripper::simulation {

namespace {
using ::intrinsic_proto::gripper::CommandPinchGripperRequest;
using ::intrinsic_proto::gripper::CommandPinchGripperResponse;
using ::intrinsic_proto::gripper::CreatePinchGripperRequest;
using ::intrinsic_proto::gripper::CreatePinchGripperResponse;
using ::intrinsic_proto::gripper::ResetPinchGripperRequest;
using ::intrinsic_proto::gripper::ResetPinchGripperResponse;

}  // namespace

absl::Status SimPinchGripperImpl::CreatePinchGripper(
    const CreatePinchGripperRequest& request,
    CreatePinchGripperResponse* response) {
  absl::MutexLock lock(mutex_);

  // Always use the config provided to the constructor.
  LOG(INFO) << "Using the config provided to the constructor " << config_;
  CreatePinchGripperRequest modified_request = request;
  *modified_request.mutable_config() = config_;

  // Forward the request to the simulated pinch gripper service.
  grpc::ClientContext client_context;
  grpc::Status status =
      stub_->CreatePinchGripper(&client_context, modified_request, response);
  if (!status.ok()) {
    LOG(ERROR) << "Failed to create the pinch gripper.";
    state_machine_.SetState(resources::OperationalState::kFaulted);
  }
  return ToAbslStatus(status);
}

absl::Status SimPinchGripperImpl::ResetPinchGripper(
    const ResetPinchGripperRequest& request,
    ResetPinchGripperResponse* response) {
  absl::MutexLock lock(mutex_);

  // The sim server may have been reset since the last call. That would
  // invalidate the current health state machine and make us falsely think that
  // the gripper is "healthy". As a result, we always forward the call to the
  // sim gripper service to update the current state.

  LOG(INFO) << "Executing simulated ResetPinchGripper request: " << request;

  auto reset_action = [this, &request, response]() -> absl::Status {
    // Forward the request to the simulated pinch gripper service.
    grpc::ClientContext client_context;
    return ToAbslStatus(
        this->stub_->ResetPinchGripper(&client_context, request, response));
  };

  if (const auto status = reset_action(); !status.ok()) {
    LOG(WARNING) << "Failed to reset the pinch gripper.";
    state_machine_.SetState(resources::OperationalState::kFaulted);
    return status;
  }

  LOG(INFO) << "Successfully reset the pinch gripper.";
  state_machine_.SetState(resources::OperationalState::kEnabled);
  return absl::OkStatus();
}

absl::Status SimPinchGripperImpl::CommandPinchGripper(
    const CommandPinchGripperRequest& request,
    CommandPinchGripperResponse* response) {
  absl::MutexLock lock(mutex_);

  INTR_RETURN_IF_ERROR(
      PinchGripperNotEnabledStatus(state_machine_.IsEnabled()));

  LOG(INFO) << "Executing CommandPinchGripper request: " << request;
  // Forward the request to the simulated pinch gripper service.
  grpc::ClientContext client_context;
  return ToAbslStatus(
      stub_->CommandPinchGripper(&client_context, request, response));
}

absl::Status SimPinchGripperImpl::GetPinchGripperStatus(
    const intrinsic_proto::gripper::GetPinchGripperStatusRequest& request,
    intrinsic_proto::gripper::GetPinchGripperStatusResponse* response) {
  absl::MutexLock lock(mutex_);

  // Forward the request to the simulated pinch gripper service.
  grpc::ClientContext client_context;
  return ToAbslStatus(
      stub_->GetPinchGripperStatus(&client_context, request, response));
}

absl::Status SimPinchGripperImpl::InitializeAndEnable() {
  CreatePinchGripperRequest request;
  CreatePinchGripperResponse response;
  INTR_RETURN_IF_ERROR(CreatePinchGripper(request, &response)).LogError();
  INTR_RETURN_IF_ERROR(ClearFaults()).LogError();
  return Enable();
}

intrinsic_proto::services::v1::SelfState SimPinchGripperImpl::GetState() const {
  absl::MutexLock lock(mutex_);
  return state_machine_.GetState();
}

absl::Status SimPinchGripperImpl::ClearFaults() {
  absl::MutexLock lock(mutex_);

  auto clear_faults_action = [this]() -> absl::Status {
    // For clearing faults, simply forward the request to reset the simulated
    // pinch gripper service.
    auto gripper_handle = this->config_.name();
    ResetPinchGripperRequest request;
    request.set_gripper_handle(gripper_handle);
    grpc::ClientContext client_context;
    ResetPinchGripperResponse response;
    return ToAbslStatus(
        this->stub_->ResetPinchGripper(&client_context, request, &response));
  };

  auto disable_action = []() { return absl::OkStatus(); };
  return state_machine_.ClearFaultsAndDisable(clear_faults_action,
                                              disable_action);
}

absl::Status SimPinchGripperImpl::Enable() {
  absl::MutexLock lock(mutex_);
  return state_machine_.Enable();
}

absl::Status SimPinchGripperImpl::Disable() {
  absl::MutexLock lock(mutex_);
  auto disable_action = []() { return absl::OkStatus(); };
  return state_machine_.Disable(disable_action);
}

}  // namespace intrinsic::gripper::simulation
