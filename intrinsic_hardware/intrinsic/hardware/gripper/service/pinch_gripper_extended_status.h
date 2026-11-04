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

#ifndef INTRINSIC_HARDWARE_GRIPPER_SERVICE_PINCH_GRIPPER_EXTENDED_STATUS_H_
#define INTRINSIC_HARDWARE_GRIPPER_SERVICE_PINCH_GRIPPER_EXTENDED_STATUS_H_

#include <cstdint>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"

namespace intrinsic::gripper {

// Component of the ExtendedStatus errors returned by the pinch gripper server
// (`PinchGripperServerAsyncCallbackImpl` and its simulation counterpart). The
// same server binary ships under several service assets, so the component is
// a fixed name rather than an asset id.
inline constexpr absl::string_view kPinchGripperServerComponent =
    "ai.intrinsic.pinch_gripper_server";

// Status codes of the pinch gripper server. Clients (e.g. the gripper skills)
// read `ExtendedStatus.status_code` instead of matching message texts.
enum PinchGripperServerStatusCode : uint32_t {
  // A command was sent while the gripper is not enabled.
  kPinchGripperNotEnabled = 10001,
  // The gripper driver has not been created yet (or its creation failed).
  kPinchGripperNotInitialized = 10002,
  // A commanded position, velocity or effort is outside the range the gripper
  // hardware accepts.
  kPinchGripperCommandOutOfRange = 10003,
  // The gripper did not finish executing the command before the execution
  // timeout.
  kPinchGripperCommandTimeout = 10004,
  // The gripper driver failed to execute the command for a reason that none
  // of the more specific codes describes.
  kPinchGripperCommandFailed = 10005,
  // The gripper service configuration is invalid.
  kPinchGripperConfigInvalid = 10006,
};

// Human readable title of `code`, as shown in the ExtendedStatus.
absl::string_view PinchGripperServerStatusTitle(
    PinchGripperServerStatusCode code);

// Attaches an ExtendedStatus with `code` and `user_message` to the failed
// `status`. The canonical status code and message of `status` are kept, so
// clients that only look at those keep working; `status` itself goes into the
// debug report. Returns `status` unchanged if it is OK.
absl::Status WithPinchGripperServerStatus(const absl::Status& status,
                                          PinchGripperServerStatusCode code,
                                          absl::string_view user_message);

// Wraps the failed result of `HealthStateMachine::IsEnabled()` into
// `kPinchGripperNotEnabled`.
absl::Status PinchGripperNotEnabledStatus(const absl::Status& status);

// INVALID_ARGUMENT status for a converted `hardware_value` that lies outside
// the gripper's configured hardware range, carrying
// `kPinchGripperCommandOutOfRange`.
absl::Status PinchGripperCommandOutOfRangeStatus(int64_t hardware_value);

// Classifies the failed result of `PinchGripperInterface::ExecuteCommand()`.
// Statuses that already carry an ExtendedStatus of this component (e.g.
// `kPinchGripperCommandOutOfRange` from the hardware value conversion) are
// returned unchanged; DEADLINE_EXCEEDED becomes `kPinchGripperCommandTimeout`
// and everything else `kPinchGripperCommandFailed`.
absl::Status PinchGripperCommandStatus(const absl::Status& status);

}  // namespace intrinsic::gripper

#endif  // INTRINSIC_HARDWARE_GRIPPER_SERVICE_PINCH_GRIPPER_EXTENDED_STATUS_H_
