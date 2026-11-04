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

#include "intrinsic/hardware/gripper/service/pinch_gripper_extended_status.h"

#include <cstdint>
#include <optional>
#include <string>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "intrinsic/util/status/extended_status.pb.h"
#include "intrinsic/util/status/get_extended_status.h"
#include "intrinsic/util/status/status_builder.h"

namespace intrinsic::gripper {

absl::string_view PinchGripperServerStatusTitle(
    PinchGripperServerStatusCode code) {
  switch (code) {
    case kPinchGripperNotEnabled:
      return "Gripper not enabled";
    case kPinchGripperNotInitialized:
      return "Gripper not initialized";
    case kPinchGripperCommandOutOfRange:
      return "Gripper command value out of range";
    case kPinchGripperCommandTimeout:
      return "Gripper command not completed in time";
    case kPinchGripperCommandFailed:
      return "Gripper command failed";
    case kPinchGripperConfigInvalid:
      return "Gripper service configuration invalid";
  }
  return "Gripper error";
}

absl::Status WithPinchGripperServerStatus(const absl::Status& status,
                                          PinchGripperServerStatusCode code,
                                          absl::string_view user_message) {
  if (status.ok()) return status;
  return StatusBuilder(status).AttachExtendedStatus(
      kPinchGripperServerComponent, code,
      {.title = std::string(PinchGripperServerStatusTitle(code)),
       .timestamp = absl::Now(),
       .user_message = std::string(user_message),
       .debug_message = status.ToString()});
}

absl::Status PinchGripperNotEnabledStatus(const absl::Status& status) {
  return WithPinchGripperServerStatus(status, kPinchGripperNotEnabled,
                                      status.message());
}

absl::Status PinchGripperCommandOutOfRangeStatus(int64_t hardware_value) {
  const std::string message =
      absl::StrFormat("Invalid Hardware Value: %d", hardware_value);
  return WithPinchGripperServerStatus(absl::InvalidArgumentError(message),
                                      kPinchGripperCommandOutOfRange, message);
}

absl::Status PinchGripperCommandStatus(const absl::Status& status) {
  if (status.ok()) return status;
  if (std::optional<intrinsic_proto::status::ExtendedStatus> es =
          GetExtendedStatus(status);
      es.has_value() &&
      es->status_code().component() == kPinchGripperServerComponent) {
    return status;
  }
  const PinchGripperServerStatusCode code =
      status.code() == absl::StatusCode::kDeadlineExceeded
          ? kPinchGripperCommandTimeout
          : kPinchGripperCommandFailed;
  return WithPinchGripperServerStatus(
      status, code,
      absl::StrCat("Failed to execute the gripper command: ",
                   status.message()));
}

}  // namespace intrinsic::gripper
