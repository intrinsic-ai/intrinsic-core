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

#ifndef INTRINSIC_ICON_HAL_TEST_TEST_HARDWARE_MODULE_H_
#define INTRINSIC_ICON_HAL_TEST_TEST_HARDWARE_MODULE_H_

#include <stdbool.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "absl/synchronization/mutex.h"
#include "intrinsic/icon/control/safety/extern/safety_status.fbs.h"
#include "intrinsic/icon/control/safety/safety_messages.fbs.h"
#include "intrinsic/icon/hal/default_hardware_interfaces.h"  // IWYU pragma: keep
#include "intrinsic/icon/hal/hardware_interface_handle.h"
#include "intrinsic/icon/hal/hardware_module_init_context.h"
#include "intrinsic/icon/hal/hardware_module_interface.h"
#include "intrinsic/icon/hal/interfaces/electrical_motor.fbs.h"
#include "intrinsic/icon/hal/interfaces/encoder.fbs.h"
#include "intrinsic/icon/hal/interfaces/hardware_module_state.fbs.h"
#include "intrinsic/icon/hal/interfaces/io_controller.fbs.h"
#include "intrinsic/icon/hal/interfaces/joint_command.fbs.h"
#include "intrinsic/icon/hal/interfaces/joint_limits.fbs.h"
#include "intrinsic/icon/hal/interfaces/joint_state.fbs.h"
#include "intrinsic/icon/utils/async_buffer.h"
#include "intrinsic/icon/utils/clock.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/util/invalid_until_set.h"

namespace intrinsic::icon::test_hardware_module {

// Corresponding module config in test_hardware_module_config.h
class TestHardwareModule final : public HardwareModuleInterface {
 public:
  static constexpr char kPositionStateInterfaceNamePrefix[] = "_position_state";
  static constexpr char kVelocityStateInterfaceNamePrefix[] = "_velocity_state";
  static constexpr char kPositionCommandInterfaceNamePrefix[] =
      "_position_command";
  static constexpr char kJointLimitsInterfaceNamePrefix[] = "_limits";
  static constexpr char kDigitalInputStatusInterfaceNamePrefix[] =
      "_digital_input_status";
  static constexpr char kAnalogInputStatusInterfaceNamePrefix[] =
      "_analog_input_status";
  static constexpr char kSafetyStatusInterfaceName[] = "safety_status";

  enum class State : int8_t {
    kDeactivated = 0,
    kActivated = 1,
    kMotionEnabled = 2,
    kFaulted = 3,
  };

  absl::Status Init(
      intrinsic::icon::HardwareModuleInitContext& init_context) override;
  absl::Status Prepare() override;

  RealtimeStatus Activate() override;

  RealtimeStatus Deactivate() override;

  absl::Status EnableMotion() override;

  RealtimeStatus Enabled() override;

  RealtimeStatus Disabled() override;

  absl::Status DisableMotion() override;

  absl::Status ClearFaults() override;

  absl::Status TriggerFault();

  absl::Status Shutdown() override;

  RealtimeStatus ApplyCommand() override;

  RealtimeStatus ReadStatus() override;

  State GetCurrentState() const;

  // Pause transitions. If pause is true, calling transition methods
  // (EnableMotion, DisableMotion motions and ClearFaults) blocks until it is
  // set to false.
  void PauseTransitions(bool pause = true);

  // Set transitions to fail. If called with non-ok status, calling transition
  // methods (EnableMotion, DisableMotion motions and ClearFaults) return this
  // error code.
  void SetFailingTransitions(
      absl::StatusCode code = absl::StatusCode::kInternal);

  // Set a callback to be called when a transition between hardware module
  // states is executed, e.g. EnableMotion() is called. Will trigger even if
  // transitions are paused or failing. The parameter of the function callback
  // will contain the currently requested transition.
  void SetTransitionCallback(
      std::function<RealtimeStatus(intrinsic_fbs::TransitionRequest)> callback);

  // Set a callback that the HardwareModuleRuntime call as part of the lockstep
  // when the hardware module gets enabled or disabled. The parameter of the
  // function callback describes the new enabled state (true==enabled).
  void SetEnabledCallback(std::function<RealtimeStatus(bool)> callback);

  // Lets `Init` immediately return `status` if `!status.ok()`.
  // Will not influence `Init` if `status.ok()`.
  void SetInitStatusOverride(absl::Status status);

  // Set the expected safety behavior.
  // Allows to use the module in tests which need to react on changes of the
  // behavior in the safety status.
  void SetRequestedSafetyBehavior(intrinsic_fbs::RequestedBehavior behavior);

  // Allows modifying the safety status exposed by this module.
  intrinsic_fbs::SafetyStatusMessage& MutableSafetyStatus();

  // Allows modifying the digital input status exposed by this module.
  intrinsic_fbs::DIOStatus& MutableDigitalInputStatus();

  // Prefix for most interfaces exposed by this module.
  // Configured via the `interface_prefix` config option.
  // Prefix is not used for the SafetyStatusInterface and all interface names
  // explicitly defined in the config.
  std::string GetInterfaceNamePrefix();

  int GetReadStatusCount() const;
  int GetApplyCommandCount() const;

  // Set the fault reason that will be reported in following calls to
  // ReadStatus(). Delete by passing in a std::nullopt.
  void SetFaultReason(const std::optional<std::string>& fault_reason);

  // Call before initializing the module to signal to the HardwareModuleRuntime
  // during `Init()` whether the malloc guard should be enabled.
  void EnableMallocGuard(bool enable = true) { enable_malloc_guard_ = enable; }

 private:
  using ElectricalMotorCommand = ::intrinsic_fbs::ElectricalMotorCommand;
  using EncoderStatus = ::intrinsic_fbs::EncoderStatus;
  MutableHardwareInterfaceHandle<intrinsic_fbs::SafetyStatusMessage>
      safety_status_;
  MutableHardwareInterfaceHandle<::intrinsic_fbs::JointPositionState>
      joint_position_state_;
  MutableHardwareInterfaceHandle<::intrinsic_fbs::JointVelocityState>
      joint_velocity_state_;
  MutableHardwareInterfaceHandle<::intrinsic_fbs::JointPositionCommand>
      joint_position_command_;
  MutableHardwareInterfaceHandle<::intrinsic_fbs::JointLimits> joint_limits_;
  MutableHardwareInterfaceHandle<intrinsic_fbs::DIOStatus>
      digital_input_status_;
  MutableHardwareInterfaceHandle<intrinsic_fbs::AIOStatus> analog_input_status_;

  std::optional<StrictHardwareInterfaceHandle<intrinsic_fbs::DIOCommand>>
      digital_output_command_;

  std::atomic<State> current_state_;

  absl::Mutex fault_reason_lock_;
  AsyncBuffer<InvalidUntilSet<std::string>> fault_reason_
      ABSL_GUARDED_BY(fault_reason_lock_);

  absl::Mutex transition_lock_;
  bool pause_transitions_{false};
  // Blocks as long as 'pause_transitions_' is true.
  void MaybeBlockTransition();

  std::atomic<absl::StatusCode> fail_transitions_with_{OkStatus().code()};
  std::function<RealtimeStatus(intrinsic_fbs::TransitionRequest)>
      transitions_callback_ =
          [](intrinsic_fbs::TransitionRequest) { return OkStatus(); };
  std::function<RealtimeStatus(bool)> enabled_callback_ = [](bool enabled) {
    return OkStatus();
  };
  absl::Status init_status_override_{absl::OkStatus()};

  int read_status_count_ = 0;
  int apply_command_count_ = 0;
  bool enable_malloc_guard_ = true;  
  bool do_illegal_mallocs_in_read_status_ = false;
  bool do_illegal_mallocs_in_apply_command_ = false;

  intrinsic::Clock clock_;
  // Prefix for the interface names declared with `k...Prefix`
  std::string interface_prefix_ = "";
};

template <typename Sink>
void AbslStringify(Sink& sink, TestHardwareModule::State e) {
  switch (e) {
    case TestHardwareModule::State::kDeactivated:
      absl::Format(&sink, "%s", "Deactivated");
      return;
    case TestHardwareModule::State::kActivated:
      absl::Format(&sink, "%s", "Activated");
      return;
    case TestHardwareModule::State::kMotionEnabled:
      absl::Format(&sink, "%s", "MotionEnabled");
      return;
    case TestHardwareModule::State::kFaulted:
      absl::Format(&sink, "%s", "Faulted");
      return;
  }
}

}  // namespace intrinsic::icon::test_hardware_module

#endif  // INTRINSIC_ICON_HAL_TEST_TEST_HARDWARE_MODULE_H_
