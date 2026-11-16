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

#include "intrinsic/perception/cameras/aravis/aravis_ptp_utils.h"

#include <arv.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

#include "absl/base/nullability.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "intrinsic/perception/cameras/aravis/aravis_software_trigger.h"
#include "intrinsic/perception/cameras/aravis/aravis_utils.h"
#include "intrinsic/perception/cameras/genicam/feature_names.h"
#include "intrinsic/util/status/status_builder.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic {
namespace perception {
namespace {

constexpr char kBasler[] = "Basler";
constexpr char kBaslerAcePrefix[] = "acA";
constexpr char kBaslerAce2Prefix[] = "a2A";

constexpr absl::Duration kPtpLockPollInterval = absl::Milliseconds(10);
constexpr double kMinPtpFrameRateHz = 0.001;

// Command that latches the current IEEE 1588 dataset on Basler ace (acA)
// cameras (custom non-SFNC counterpart to SFNC PtpDataSetLatch).
constexpr char kBaslerAceGevIEEE1588DataSetLatch[] = "GevIEEE1588DataSetLatch";
// Command and register that latch and read the current timestamp on Basler ace
// (acA) cameras.
constexpr char kBaslerAceGevTimestampControlLatch[] =
    "GevTimestampControlLatch";
constexpr char kBaslerAceGevTimestampValue[] = "GevTimestampValue";
// Low and high 32-bit registers for the synchronous free-run start time.
constexpr char kBaslerAceSyncFreeRunTimerStartTimeLow[] =
    "SyncFreeRunTimerStartTimeLow";
constexpr char kBaslerAceSyncFreeRunTimerStartTimeHigh[] =
    "SyncFreeRunTimerStartTimeHigh";
// Trigger rate (in Hz) for Basler ace synchronous free-run mode.
constexpr char kBaslerAceSyncFreeRunTimerTriggerRateAbs[] =
    "SyncFreeRunTimerTriggerRateAbs";
// Command setting that resets the PTP synchonization timers.
constexpr char kBaslerAceSyncFreeRunTimerUpdate[] = "SyncFreeRunTimerUpdate";
// Enables the PTP based camera synchronisation.
constexpr char kBaslerAceSyncFreeRunTimerEnable[] = "SyncFreeRunTimerEnable";

// Period (in microseconds) and delay for Basler ace 2 (a2A) Periodic Signal.
constexpr char kBaslerAce2PeriodicSignalPeriod[] = "BslPeriodicSignalPeriod";
constexpr char kBaslerAce2PeriodicSignalDelay[] = "BslPeriodicSignalDelay";

// Official SFNC states for PtpStatus and GevIEEE1588Status.
constexpr char kPtpStatusInitializing[] = "Initializing";
constexpr char kPtpStatusListening[] = "Listening";
constexpr char kPtpStatusPreMaster[] = "PreMaster";
constexpr char kPtpStatusMaster[] = "Master";
constexpr char kPtpStatusUncalibrated[] = "Uncalibrated";
constexpr char kPtpStatusSlave[] = "Slave";

bool IsTransientPtpStatus(std::string_view status) {
  return status == kPtpStatusInitializing || status == kPtpStatusListening ||
         status == kPtpStatusPreMaster || status == kPtpStatusUncalibrated;
}

absl::StatusOr<bool> WaitForPtpLock(ArvCamera* absl_nonnull camera,
                                    const char* absl_nonnull latch_feature,
                                    const char* absl_nonnull status_feature,
                                    absl::Duration timeout) {
  if (!arv_camera_is_feature_available(camera, status_feature, nullptr)) {
    LOG(WARNING) << "PTP status feature '" << status_feature
                 << "' is not available on this camera; falling back to "
                    "free-running trigger.";
    return false;
  }
  const absl::Time deadline = absl::Now() + timeout;
  GError* error = nullptr;
  while (true) {
    if (arv_camera_is_feature_available(camera, latch_feature, nullptr)) {
      arv_camera_execute_command(camera, latch_feature, &error);
      INTR_RETURN_IF_ERROR(ValidateAndNullArvError(error));
    }
    const char* status_cstr =
        arv_camera_get_string(camera, status_feature, &error);
    INTR_RETURN_IF_ERROR(ValidateAndNullArvError(error));
    const std::string last_status = status_cstr != nullptr ? status_cstr : "";
    if (last_status == kPtpStatusSlave) {
      return true;
    }
    if (last_status == kPtpStatusMaster) {
      LOG(WARNING) << "Camera PTP clock is in Master state (no external PTP "
                      "grandmaster detected); configuring synchronous trigger "
                      "using camera clock.";
      return true;
    }
    const absl::Time now = absl::Now();
    if (!IsTransientPtpStatus(last_status) || now >= deadline) {
      LOG(WARNING) << "Camera PTP clock did not settle to Slave/Master state "
                      "within "
                   << timeout << " (current " << status_feature << ": '"
                   << last_status
                   << "'); falling back to free-running trigger.";
      return false;
    }
    absl::SleepFor(std::min(kPtpLockPollInterval, deadline - now));
  }
}

absl::Status ConfigureFreeRunningStreamingTrigger(
    ArvCamera* absl_nonnull camera) {
  GError* error = nullptr;
  if (GetVendorName(camera) == kBasler &&
      GetModelName(camera).starts_with(kBaslerAcePrefix) &&
      arv_camera_is_feature_available(camera, kBaslerAceSyncFreeRunTimerEnable,
                                      nullptr)) {
    arv_camera_set_boolean(camera, kBaslerAceSyncFreeRunTimerEnable, 0, &error);
    INTR_RETURN_IF_ERROR(ValidateAndNullArvError(error));
    if (arv_camera_is_feature_available(
            camera, kBaslerAceSyncFreeRunTimerUpdate, nullptr)) {
      arv_camera_execute_command(camera, kBaslerAceSyncFreeRunTimerUpdate,
                                 &error);
      INTR_RETURN_IF_ERROR(ValidateAndNullArvError(error));
    }
  }
  INTR_RETURN_IF_ERROR(SetFreeRunningTrigger(camera));
  // arv_camera_set_frame_rate_enable checks feature availability across vendor
  // variants (e.g. AcquisitionFrameRateEnable vs. AcquisitionFrameRateEnabled)
  // and is a no-op if unsupported.
  arv_camera_set_frame_rate_enable(camera, true, &error);
  return ValidateAndNullArvError(error);
}

absl::Status ConfigureBaslerAcePtpSync(ArvCamera* absl_nonnull camera,
                                       absl::Duration ptp_lock_timeout) {
  INTR_ASSIGN_OR_RETURN(
      const bool ptp_locked,
      WaitForPtpLock(camera, kBaslerAceGevIEEE1588DataSetLatch,
                     genicam::kGevIEEE1588Status, ptp_lock_timeout));
  if (!ptp_locked) {
    return ConfigureFreeRunningStreamingTrigger(camera);
  }

  GError* error = nullptr;
  const double frame_rate = arv_camera_get_frame_rate(camera, &error);
  INTR_RETURN_IF_ERROR(ValidateAndNullArvError(error));
  if (!std::isfinite(frame_rate) || frame_rate < kMinPtpFrameRateHz) {
    return ::intrinsic::FailedPreconditionErrorBuilder()
           << "Acquisition frame rate must be finite and at least "
           << kMinPtpFrameRateHz << " Hz for PTP synchronous free run, got "
           << frame_rate;
  }

  arv_camera_set_frame_rate_enable(camera, false, &error);
  INTR_RETURN_IF_ERROR(ValidateAndNullArvError(error));
  // The free running trigger must be set before updating and enabling the
  // timer.
  INTR_RETURN_IF_ERROR(SetFreeRunningTrigger(camera));
  uint64_t start_time_ns = 0;
  if (arv_camera_is_feature_available(
          camera, kBaslerAceGevTimestampControlLatch, nullptr) &&
      arv_camera_is_feature_available(camera, kBaslerAceGevTimestampValue,
                                      nullptr)) {
    arv_camera_execute_command(camera, kBaslerAceGevTimestampControlLatch,
                               &error);
    INTR_RETURN_IF_ERROR(ValidateAndNullArvError(error));
    const gint64 current_ts =
        arv_camera_get_integer(camera, kBaslerAceGevTimestampValue, &error);
    INTR_RETURN_IF_ERROR(ValidateAndNullArvError(error));
    const uint64_t period_ns =
        static_cast<uint64_t>(std::llround(1e9 / frame_rate));
    if (current_ts > 0 && period_ns > 0) {
      // Schedule the start time at a period-aligned boundary in the future so
      // the camera's 64-bit comparator does not underflow across 2^32 when
      // catching up from a past start time (such as 0).
      start_time_ns =
          ((static_cast<uint64_t>(current_ts) / period_ns) + 2) * period_ns;
    }
  }
  arv_camera_set_integer(camera, kBaslerAceSyncFreeRunTimerStartTimeLow,
                         static_cast<gint64>(start_time_ns & 0xffffffffULL),
                         &error);
  INTR_RETURN_IF_ERROR(ValidateAndNullArvError(error));
  arv_camera_set_integer(camera, kBaslerAceSyncFreeRunTimerStartTimeHigh,
                         static_cast<gint64>(start_time_ns >> 32), &error);
  INTR_RETURN_IF_ERROR(ValidateAndNullArvError(error));
  arv_camera_set_float(camera, kBaslerAceSyncFreeRunTimerTriggerRateAbs,
                       frame_rate, &error);
  INTR_RETURN_IF_ERROR(ValidateAndNullArvError(error));
  arv_camera_execute_command(camera, kBaslerAceSyncFreeRunTimerUpdate, &error);
  INTR_RETURN_IF_ERROR(ValidateAndNullArvError(error));
  arv_camera_set_boolean(camera, kBaslerAceSyncFreeRunTimerEnable, 1, &error);
  INTR_RETURN_IF_ERROR(ValidateAndNullArvError(error));
  arv_camera_execute_command(camera, kBaslerAceSyncFreeRunTimerUpdate, &error);
  return ValidateAndNullArvError(error);
}

absl::Status ConfigureBaslerAce2PtpSync(ArvCamera* absl_nonnull camera,
                                        absl::Duration ptp_lock_timeout) {
  // Poll PtpStatus until it transitions out of transient states (including
  // Uncalibrated) to Slave or Master. We intentionally do not require
  // PtpServoStatus / BslPtpServoStatus == "Locked", as physical Basler ace 2
  // cameras can report "Unknown" servo status for minutes even after PtpStatus
  // reaches "Slave" with sub-microsecond offset from the master.
  INTR_ASSIGN_OR_RETURN(const bool ptp_locked,
                        WaitForPtpLock(camera, genicam::kPtpDataSetLatch,
                                       genicam::kPtpStatus, ptp_lock_timeout));
  if (!ptp_locked) {
    return ConfigureFreeRunningStreamingTrigger(camera);
  }

  GError* error = nullptr;
  const double frame_rate = arv_camera_get_frame_rate(camera, &error);
  INTR_RETURN_IF_ERROR(ValidateAndNullArvError(error));
  if (!std::isfinite(frame_rate) || frame_rate < kMinPtpFrameRateHz) {
    return ::intrinsic::FailedPreconditionErrorBuilder()
           << "Acquisition frame rate must be finite and at least "
           << kMinPtpFrameRateHz << " Hz for PTP periodic signal trigger, got "
           << frame_rate;
  }

  const double period_us = 1e6 / frame_rate;
  arv_camera_set_float(camera, kBaslerAce2PeriodicSignalPeriod, period_us,
                       &error);
  INTR_RETURN_IF_ERROR(ValidateAndNullArvError(error));
  arv_camera_set_float(camera, kBaslerAce2PeriodicSignalDelay, 0.0, &error);
  INTR_RETURN_IF_ERROR(ValidateAndNullArvError(error));
  INTR_RETURN_IF_ERROR(SetPeriodicTrigger(camera));
  arv_camera_set_frame_rate_enable(camera, false, &error);
  return ValidateAndNullArvError(error);
}

}  // namespace

absl::Status ConfigurePtpSyncAndStreamingTrigger(
    ArvCamera* absl_nonnull camera, absl::Duration ptp_lock_timeout) {
  if (camera == nullptr) {
    return ::intrinsic::InvalidArgumentErrorBuilder()
           << "ArvCamera must not be a nullptr.";
  }

  // Depending on the camera different genicam parameters signify the use of PTP
  // synchronisation.
  const bool ptp_sync =
      arv_camera_get_boolean(camera, genicam::kPtpEnable, nullptr) ||
      arv_camera_get_boolean(camera, genicam::kGevIEEE1588, nullptr);

  if (ptp_sync) {
    // Specific Vendor/Model combinations support PTP via different settings and
    // the trigger modes which must be set in different orders.
    if (GetVendorName(camera) == kBasler &&
        GetModelName(camera).starts_with(kBaslerAcePrefix)) {
      return ConfigureBaslerAcePtpSync(camera, ptp_lock_timeout);
    }
    if (GetVendorName(camera) == kBasler &&
        GetModelName(camera).starts_with(kBaslerAce2Prefix)) {
      return ConfigureBaslerAce2PtpSync(camera, ptp_lock_timeout);
    }
    LOG(WARNING) << "PTP camera stream synchronisation is enabled but is not "
                    "supported for this camera.";
  }

  return ConfigureFreeRunningStreamingTrigger(camera);
}

}  // namespace perception
}  // namespace intrinsic
