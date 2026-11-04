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

#ifndef INTRINSIC_HARDWARE_GRIPPER_EOAT_GRIPPER_EXTENDED_STATUS_H_
#define INTRINSIC_HARDWARE_GRIPPER_EOAT_GRIPPER_EXTENDED_STATUS_H_

#include <cstdint>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"

namespace intrinsic::gripper {

// Component of the ExtendedStatus errors returned by the EOAT gripper
// services. The same gripper server binary ships under several service assets,
// so the component is a fixed name rather than an asset id.
inline constexpr absl::string_view kExtendedStatusComponent =
    "ai.intrinsic.eoat_gripper";

// Status codes of the EOAT gripper services. Clients (e.g. the gripper skills)
// read `ExtendedStatus.status_code` instead of matching message texts.
enum ExtendedStatusCode : uint32_t {
  // A command was sent while the gripper is not enabled.
  kGripperNotEnabled = 10001,
  // Writing the gripper's I/O signals failed for a reason that none of the
  // more specific codes below describes.
  kGripperIoWriteFailed = 10002,
  // The I/O (GPIO) service does not know the gripper's signal names.
  kGripperIoSignalsNotFound = 10003,
  // The I/O (GPIO) service, or the real-time control service behind it, could
  // not be reached.
  kGripperIoServiceUnreachable = 10004,
  // Another client holds exclusive write access to the gripper's signals.
  kGripperIoSignalClaimed = 10005,
  // The I/O (GPIO) service rejected the value written to a signal, e.g.
  // because of its type.
  kGripperIoSignalValueInvalid = 10006,
  // Clearing the gripper's faults failed, i.e. the signal descriptions could
  // not be read from the I/O (GPIO) service.
  kGripperClearFaultsFailed = 10007,
  kGripperIoReadFailed = 10008,
};

// Human readable title of `code`, as shown in the ExtendedStatus.
absl::string_view ExtendedStatusTitle(ExtendedStatusCode code);

// Attaches an ExtendedStatus with `code` and `user_message` to the failed
// `status`. The canonical status code and message of `status` are kept, so
// clients that only look at those keep working; `status` itself goes into the
// debug report. Returns `status` unchanged if it is OK.
absl::Status WithExtendedStatus(const absl::Status& status,
                                ExtendedStatusCode code,
                                absl::string_view user_message);

// Wraps the failed result of `HealthStateMachine::IsEnabled()` into
// `kGripperNotEnabled`.
absl::Status NotEnabledStatus(const absl::Status& status);

// Classifies the failed result of `GPIOClient::Write()`. The GPIO client wraps
// stream transport failures and an ABORTED session status (e.g. the real-time
// control service is not enabled) into INTERNAL and forwards every other
// status of the GPIO service verbatim, so the classification is by canonical
// status code:
//   NOT_FOUND                     -> kGripperIoSignalsNotFound
//   UNAVAILABLE, DEADLINE_EXCEEDED -> kGripperIoServiceUnreachable
//   PERMISSION_DENIED             -> kGripperIoSignalClaimed
//   INVALID_ARGUMENT              -> kGripperIoSignalValueInvalid
//   everything else               -> kGripperIoWriteFailed
// The Gazebo GPIO plugin reports unknown signals by aborting the write stream
// instead of returning NOT_FOUND; that case is recognized by its message.
absl::Status GpioWriteStatus(const absl::Status& status);

absl::Status GpioReadStatus(const absl::Status& status);

// Wraps the failed result `status` of clearing faults into
// `kGripperClearFaultsFailed`. `cause` is the failure of the fault-clearing
// action, which the health state machine does not forward itself. Today that
// action only reads the signal descriptions from the I/O (GPIO) service, so
// the cause is a GPIO failure; the message does not assume that.
absl::Status ClearFaultsStatus(const absl::Status& status,
                               const absl::Status& cause);

}  // namespace intrinsic::gripper

#endif  // INTRINSIC_HARDWARE_GRIPPER_EOAT_GRIPPER_EXTENDED_STATUS_H_
