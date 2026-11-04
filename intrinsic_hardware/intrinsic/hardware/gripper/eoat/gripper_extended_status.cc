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

#include "intrinsic/hardware/gripper/eoat/gripper_extended_status.h"

#include <string>

#include "absl/status/status.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "intrinsic/util/status/status_builder.h"

namespace intrinsic::gripper {
namespace {

// TODO(b/519710664): Remove this once the Gazebo GPIO service is removed.
// The Gazebo GPIO plugin
// (intrinsic/simulation/gazebo/plugins/gpio/gpio_service.cc) aborts the write
// stream with "Write signals failed with error NOT_FOUND: Signals [...] not
// found writable among N plugin(s)..." instead of returning NOT_FOUND, which
// the GPIO client then wraps into an INTERNAL status.
constexpr absl::string_view kGazeboSignalsNotFoundMessageFragment =
    "not found writable";
constexpr absl::string_view kGpioClaimWriteFailedPrefix =
    "Failed to claim exclusive write access to GPIO signals:";
constexpr absl::string_view kGpioSessionWriteFailedPrefix =
    "Failed to write request to set value to GPIO session:";

}  // namespace

absl::string_view ExtendedStatusTitle(ExtendedStatusCode code) {
  switch (code) {
    case kGripperNotEnabled:
      return "Gripper not enabled";
    case kGripperIoWriteFailed:
      return "Gripper I/O write failed";
    case kGripperIoSignalsNotFound:
      return "Gripper I/O signals not found";
    case kGripperIoServiceUnreachable:
      return "Gripper I/O service unreachable";
    case kGripperIoSignalClaimed:
      return "Gripper I/O signals claimed by another client";
    case kGripperIoSignalValueInvalid:
      return "Gripper I/O signal value invalid";
    case kGripperClearFaultsFailed:
      return "Gripper fault clearing failed";
    case kGripperIoReadFailed:
      return "Gripper I/O read failed";
  }
  return "Gripper error";
}

absl::Status WithExtendedStatus(const absl::Status& status,
                                ExtendedStatusCode code,
                                absl::string_view user_message) {
  if (status.ok()) return status;
  return StatusBuilder(status).AttachExtendedStatus(
      kExtendedStatusComponent, code,
      {.title = std::string(ExtendedStatusTitle(code)),
       .timestamp = absl::Now(),
       .user_message = std::string(user_message),
       .debug_message = status.ToString()});
}

absl::Status NotEnabledStatus(const absl::Status& status) {
  return WithExtendedStatus(status, kGripperNotEnabled, status.message());
}

absl::Status GpioWriteStatus(const absl::Status& status) {
  if (status.ok()) return status;
  ExtendedStatusCode code = kGripperIoWriteFailed;
  switch (status.code()) {
    case absl::StatusCode::kNotFound:
      code = kGripperIoSignalsNotFound;
      break;
    case absl::StatusCode::kUnavailable:
    case absl::StatusCode::kDeadlineExceeded:
      code = kGripperIoServiceUnreachable;
      break;
    case absl::StatusCode::kPermissionDenied:
      code = kGripperIoSignalClaimed;
      break;
    case absl::StatusCode::kInvalidArgument:
      code = kGripperIoSignalValueInvalid;
      break;
    case absl::StatusCode::kInternal:
      if (absl::StrContains(status.message(),
                            kGazeboSignalsNotFoundMessageFragment)) {
        code = kGripperIoSignalsNotFound;
      } else if (absl::StartsWith(status.message(),
                                  kGpioClaimWriteFailedPrefix) ||
                 absl::StartsWith(status.message(),
                                  kGpioSessionWriteFailedPrefix)) {
        code = kGripperIoServiceUnreachable;
      }
      break;
    default:
      break;
  }
  return WithExtendedStatus(
      status, code,
      absl::StrCat("Failed to write the gripper's I/O signals: ",
                   status.message()));
}

absl::Status GpioReadStatus(const absl::Status& status) {
  if (status.ok()) return status;
  ExtendedStatusCode code = kGripperIoReadFailed;
  switch (status.code()) {
    case absl::StatusCode::kNotFound:
      code = kGripperIoSignalsNotFound;
      break;
    case absl::StatusCode::kUnavailable:
    case absl::StatusCode::kDeadlineExceeded:
      code = kGripperIoServiceUnreachable;
      break;
    default:
      break;
  }
  return WithExtendedStatus(
      status, code,
      absl::StrCat("Failed to read the gripper's I/O signals: ",
                   status.message()));
}

absl::Status ClearFaultsStatus(const absl::Status& status,
                               const absl::Status& cause) {
  if (status.ok()) return status;
  return StatusBuilder(status).AttachExtendedStatus(
      kExtendedStatusComponent, kGripperClearFaultsFailed,
      {.title = std::string(ExtendedStatusTitle(kGripperClearFaultsFailed)),
       .timestamp = absl::Now(),
       .user_message = absl::StrCat("Clearing the gripper's faults failed: ",
                                    cause.message()),
       .debug_message = cause.ToString()});
}

}  // namespace intrinsic::gripper
