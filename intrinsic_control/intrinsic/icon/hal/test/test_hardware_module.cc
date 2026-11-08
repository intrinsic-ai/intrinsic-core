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

#include "intrinsic/icon/hal/test/test_hardware_module.h"

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "intrinsic/icon/control/safety/extern/safety_status.fbs.h"
#include "intrinsic/icon/control/safety/safety_messages.fbs.h"
#include "intrinsic/icon/hal/hardware_interface_registry.h"
#include "intrinsic/icon/hal/hardware_module_init_context.h"
#include "intrinsic/icon/hal/hardware_module_registry.h"
#include "intrinsic/icon/hal/interfaces/hardware_module_state.fbs.h"
#include "intrinsic/icon/hal/interfaces/io_controller.fbs.h"
#include "intrinsic/icon/hal/interfaces/joint_command.fbs.h"
#include "intrinsic/icon/hal/interfaces/joint_limits.fbs.h"
#include "intrinsic/icon/hal/interfaces/joint_state.fbs.h"
#include "intrinsic/icon/hal/module_config.h"
#include "intrinsic/icon/hal/test/test_hardware_module.pb.h"
#include "intrinsic/icon/utils/realtime_guard.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/icon/utils/realtime_status_macro.h"
#include "intrinsic/util/invalid_until_set.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic::icon::test_hardware_module {

absl::Status TestHardwareModule::Init(
    intrinsic::icon::HardwareModuleInitContext& init_context) {
  INTRINSIC_ASSERT_NON_REALTIME();
  intrinsic::icon::HardwareInterfaceRegistry& interface_registry =
      init_context.GetInterfaceRegistry();
  const intrinsic::icon::ModuleConfig& module_config =
      init_context.GetModuleConfig();
  if (!enable_malloc_guard_) {
    init_context.DisableMallocGuard();
  }
  if (!init_status_override_.ok()) {
    return init_status_override_;
  }

  INTR_ASSIGN_OR_RETURN(
      auto config,
      module_config.GetConfig<intrinsic_proto::testing::TestHardwareModule>());

  interface_prefix_ = config.interface_prefix();

  INTR_ASSIGN_OR_RETURN(
      safety_status_,
      interface_registry.AdvertiseMutableInterface<
          intrinsic_fbs::SafetyStatusMessage>(
          kSafetyStatusInterfaceName,
          /*mode_of_safe_operation=*/
          intrinsic_fbs::ModeOfSafeOperation::UNKNOWN,
          /*estop_button_status=*/intrinsic_fbs::ButtonStatus::UNKNOWN,
          /*enable_button_status=*/intrinsic_fbs::ButtonStatus::UNKNOWN,
          /*requested_behavior=*/intrinsic_fbs::RequestedBehavior::UNKNOWN));
  INTR_ASSIGN_OR_RETURN(
      joint_position_state_,
      interface_registry
          .AdvertiseMutableInterface<::intrinsic_fbs::JointPositionState>(
              absl::StrCat(interface_prefix_,
                           kPositionStateInterfaceNamePrefix),
              1));
  INTR_ASSIGN_OR_RETURN(
      joint_velocity_state_,
      interface_registry
          .AdvertiseMutableInterface<::intrinsic_fbs::JointVelocityState>(
              absl::StrCat(interface_prefix_,
                           kVelocityStateInterfaceNamePrefix),
              1));
  INTR_ASSIGN_OR_RETURN(
      joint_position_command_,
      interface_registry
          .AdvertiseMutableInterface<::intrinsic_fbs::JointPositionCommand>(
              absl::StrCat(interface_prefix_,
                           kPositionCommandInterfaceNamePrefix),
              1));

  INTR_ASSIGN_OR_RETURN(
      joint_limits_,
      interface_registry
          .AdvertiseMutableInterface<::intrinsic_fbs::JointLimits>(
              absl::StrCat(interface_prefix_, kJointLimitsInterfaceNamePrefix),
              /*dof=*/1));

  joint_limits_->mutable_max_position()->Mutate(0, 1);
  joint_limits_->mutable_min_position()->Mutate(0, -1);
  joint_limits_->mutable_max_velocity()->Mutate(0, 1);
  joint_limits_->mutable_max_acceleration()->Mutate(0, 1);
  joint_limits_->mutable_max_jerk()->Mutate(0, 1);
  joint_limits_->mutable_max_effort()->Mutate(0, 1);

  INTR_ASSIGN_OR_RETURN(
      digital_input_status_,
      interface_registry.AdvertiseMutableInterface<intrinsic_fbs::DIOStatus>(
          absl::StrCat(interface_prefix_,
                       kDigitalInputStatusInterfaceNamePrefix),
          std::vector<std::string>{"bit_0", "bit_1"}));

  INTR_ASSIGN_OR_RETURN(
      analog_input_status_,
      interface_registry.AdvertiseMutableInterface<intrinsic_fbs::AIOStatus>(
          absl::StrCat(interface_prefix_,
                       kAnalogInputStatusInterfaceNamePrefix),
          std::vector<std::string>{"bit_0", "bit_1"}));

  if (config.has_strict_dio_command_interface_name()) {
    INTR_ASSIGN_OR_RETURN(
        digital_output_command_,
        interface_registry.AdvertiseStrictInterface<intrinsic_fbs::DIOCommand>(
            config.strict_dio_command_interface_name(),
            std::vector<std::string>{"bit_0", "bit_1"}));
  }

  current_state_ = State::kDeactivated;
  do_illegal_mallocs_in_read_status_ = config.do_illegal_mallocs();
  do_illegal_mallocs_in_apply_command_ = config.do_illegal_mallocs();
  return absl::OkStatus();
}

absl::Status TestHardwareModule::Prepare() {
  INTRINSIC_RT_RETURN_IF_ERROR(
      transitions_callback_(intrinsic_fbs::TransitionRequest::kPrepare));
  current_state_ = State::kDeactivated;
  return OkStatus();
}

RealtimeStatus TestHardwareModule::Activate() {
  INTRINSIC_RT_RETURN_IF_ERROR(
      transitions_callback_(intrinsic_fbs::TransitionRequest::kActivate));
  current_state_ = State::kActivated;
  return OkStatus();
}

RealtimeStatus TestHardwareModule::Deactivate() {
  INTRINSIC_RT_RETURN_IF_ERROR(
      transitions_callback_(intrinsic_fbs::TransitionRequest::kDeactivate));
  current_state_ = State::kDeactivated;
  return OkStatus();
}

absl::Status TestHardwareModule::EnableMotion() {
  INTRINSIC_RT_RETURN_IF_ERROR(
      transitions_callback_(intrinsic_fbs::TransitionRequest::kEnableMotion));
  if (fail_transitions_with_ != absl::StatusCode::kOk) {
    return absl::Status(fail_transitions_with_, "Failed to enable");
  }
  MaybeBlockTransition();

  return absl::OkStatus();
}

RealtimeStatus TestHardwareModule::Enabled() {
  INTRINSIC_RT_RETURN_IF_ERROR(enabled_callback_(true));
  current_state_ = State::kMotionEnabled;
  return OkStatus();
}

RealtimeStatus TestHardwareModule::Disabled() {
  INTRINSIC_RT_RETURN_IF_ERROR(enabled_callback_(false));
  current_state_ = State::kActivated;
  return OkStatus();
}

absl::Status TestHardwareModule::DisableMotion() {
  INTRINSIC_RT_RETURN_IF_ERROR(
      transitions_callback_(intrinsic_fbs::TransitionRequest::kDisableMotion));
  if (fail_transitions_with_ != absl::StatusCode::kOk) {
    return absl::Status(fail_transitions_with_, "Failed to disable");
  }
  MaybeBlockTransition();
  current_state_ = State::kActivated;
  return absl::OkStatus();
}

absl::Status TestHardwareModule::ClearFaults() {
  INTRINSIC_RT_RETURN_IF_ERROR(
      transitions_callback_(intrinsic_fbs::TransitionRequest::kClearFaults));
  if (fail_transitions_with_ != absl::StatusCode::kOk) {
    return absl::Status(fail_transitions_with_, "Failed to clear faults");
  }
  MaybeBlockTransition();
  if (current_state_ == State::kFaulted) {
    current_state_ = State::kActivated;
  }
  {
    absl::MutexLock lock(fault_reason_lock_);
    *fault_reason_.GetFreeBuffer() = std::nullopt;
    fault_reason_.CommitFreeBuffer();
  }
  LOG(INFO) << "Cleared Faults";
  return absl::OkStatus();
}

absl::Status TestHardwareModule::TriggerFault() {
  current_state_ = State::kFaulted;
  absl::MutexLock lock(fault_reason_lock_);
  *fault_reason_.GetFreeBuffer() = "Injected fault";
  fault_reason_.CommitFreeBuffer();
  return absl::OkStatus();
}

absl::Status TestHardwareModule::Shutdown() { return absl::OkStatus(); }

RealtimeStatus TestHardwareModule::ApplyCommand() {
  if (current_state_ != State::kMotionEnabled) {
    return FailedPreconditionError(
        "Can't call ApplyCommand if motion is not enabled.");
  }
  {
    InvalidUntilSet<std::string>* fault_reason;
    ABSL_TS_UNCHECKED_READ(fault_reason_).GetActiveBuffer(&fault_reason);
    if (fault_reason->has_value()) {
      current_state_ = State::kFaulted;
      return FailedPreconditionError(absl::string_view(**fault_reason));
    }
  }

  joint_position_state_->mutable_position()->Mutate(
      0, joint_position_command_->position()->Get(0));
  joint_position_state_.UpdatedAt(clock_.Now());
  apply_command_count_++;
  if (do_illegal_mallocs_in_apply_command_) {
    LOG(INFO) << "illegal malloc in ApplyCommand";
    do_illegal_mallocs_in_apply_command_ = false;
  }

  if (digital_output_command_.has_value()) {
    // Returns an error if the interface exists, but was not updated
    // correctly by ICON / the test.
    INTRINSIC_RT_RETURN_IF_ERROR(digital_output_command_->Value().status());
  }

  return OkStatus();
}

RealtimeStatus TestHardwareModule::ReadStatus() {
  // Although this is not updates we set the update time to verify when
  // ReadStatus is called in tests.
  joint_velocity_state_.UpdatedAt(clock_.Now());
  read_status_count_++;
  if (do_illegal_mallocs_in_read_status_) {
    LOG(INFO) << "illegal malloc in ReadStatus";
    do_illegal_mallocs_in_read_status_ = false;
  }

  InvalidUntilSet<std::string>* fault_reason;
  ABSL_TS_UNCHECKED_READ(fault_reason_).GetActiveBuffer(&fault_reason);
  if (fault_reason->has_value()) {
    current_state_ = State::kFaulted;
    return FailedPreconditionError(absl::string_view(**fault_reason));
  }
  if (current_state_ == State::kFaulted) {
    return FailedPreconditionError(
        absl::string_view(fault_reason->value_or("Unknown fault reason")));
  }

  return OkStatus();
}

TestHardwareModule::State TestHardwareModule::GetCurrentState() const {
  return current_state_;
}

void TestHardwareModule::PauseTransitions(bool pause) {
  absl::MutexLock lock(transition_lock_);
  pause_transitions_ = pause;
}

void TestHardwareModule::SetFailingTransitions(absl::StatusCode code) {
  fail_transitions_with_ = code;
}

void TestHardwareModule::SetInitStatusOverride(absl::Status status) {
  init_status_override_ = status;
}

void TestHardwareModule::SetRequestedSafetyBehavior(
    intrinsic_fbs::RequestedBehavior behavior) {
  safety_status_->mutate_requested_behavior(behavior);
}

intrinsic_fbs::SafetyStatusMessage& TestHardwareModule::MutableSafetyStatus() {
  return **safety_status_;
}

intrinsic_fbs::DIOStatus& TestHardwareModule::MutableDigitalInputStatus() {
  return **digital_input_status_;
}

void TestHardwareModule::MaybeBlockTransition() {
  absl::MutexLock lock(transition_lock_);
  auto unblocked = [this]() { return !pause_transitions_; };
  auto condition = absl::Condition(&unblocked);
  transition_lock_.Await(condition);
}

int TestHardwareModule::GetReadStatusCount() const {
  return read_status_count_;
}
int TestHardwareModule::GetApplyCommandCount() const {
  return apply_command_count_;
}

void TestHardwareModule::SetTransitionCallback(
    std::function<RealtimeStatus(intrinsic_fbs::TransitionRequest)> callback) {
  transitions_callback_ = std::move(callback);
}

void TestHardwareModule::SetEnabledCallback(
    std::function<RealtimeStatus(bool)> callback) {
  enabled_callback_ = std::move(callback);
}

void TestHardwareModule::SetFaultReason(
    const std::optional<std::string>& fault_reason) {
  absl::MutexLock lock(fault_reason_lock_);
  *fault_reason_.GetFreeBuffer() = fault_reason;
  fault_reason_.CommitFreeBuffer();
}

std::string TestHardwareModule::GetInterfaceNamePrefix() {
  return interface_prefix_;
}

}  // namespace intrinsic::icon::test_hardware_module

REGISTER_HARDWARE_MODULE(test_hardware_module::TestHardwareModule)
