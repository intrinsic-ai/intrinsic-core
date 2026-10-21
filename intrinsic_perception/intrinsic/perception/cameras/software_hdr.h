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

#ifndef INTRINSIC_PERCEPTION_CAMERAS_SOFTWARE_HDR_H_
#define INTRINSIC_PERCEPTION_CAMERAS_SOFTWARE_HDR_H_

#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "intrinsic/perception/cameras/camera_setting.h"
#include "intrinsic/perception/cameras/camera_setting_access.h"
#include "intrinsic/perception/cameras/camera_setting_properties.h"
#include "intrinsic/perception/core/hdr_utils.h"

namespace intrinsic::perception {

// Enumerates the supported HDR modes.
enum class HdrMode { kOff, kSoftwareMertens };

class SoftwareHdr {
 public:
  // Constructs a software HDR wrapper with the minimum, maximum, and increment
  // exposure times supported by the underlying camera.
  explicit SoftwareHdr(
      absl::Duration min_exposure_time, absl::Duration max_exposure_time,
      absl::Duration exposure_time_increment = absl::ZeroDuration());

  // Creates a software HDR wrapper with minimum, maximum, and increment
  // exposure times derived from the exposure time properties of the underlying
  // camera.
  explicit SoftwareHdr(
      const CameraSettingProperties::Float& exposure_time_properties);

  // Specifies, whether a specific camera setting can be handled by software
  // HDR.
  static bool CanHandleCameraSetting(std::string_view name);

  // Reads a software HDR camera setting access.
  absl::StatusOr<CameraSettingAccess> ReadCameraSettingAccess(
      std::string_view name) const;

  // Reads a software HDR camera setting.
  absl::StatusOr<CameraSetting> ReadCameraSetting(std::string_view name) const;

  // Reads the properties of a software HDR camera setting.
  absl::StatusOr<CameraSettingProperties> ReadCameraSettingProperties(
      std::string_view name) const;

  // Updates a software HDR camera setting.
  absl::Status UpdateCameraSetting(const CameraSetting& camera_setting);

  // Constructs the list of HDR exposure times, starting with
  // `init_exposure_time` followed by the sorted unique knee-point exposure
  // times clamped and rounded to the camera's exposure time range and
  // increment. Returns an empty vector if HDR is disabled or no additional
  // distinct exposure times exist.
  //
  // Placing `init_exposure_time` first allows single-frame `Capture` to
  // converge any active auto settings (`ExposureAuto`, `GainAuto`,
  // `BalanceWhiteAuto`) at the base exposure and reuse that initial frame as
  // the first HDR bracket, and records the base exposure at `front()` to
  // restore after capture or streaming. Sorting the remaining knee-point
  // exposures in ascending order ensures at most one downward exposure
  // transition (`init_exposure_time` -> shortest knee-point exposure) during
  // bracket acquisition, with all subsequent transitions strictly increasing.
  absl::StatusOr<std::vector<absl::Duration>> HdrExposureTimes(
      absl::Duration init_exposure_time) const;

  // Returns whether the HDR mode is enabled.
  bool IsEnabled() const;

  // Returns an HDR operator required to create an HDR image.
  absl::StatusOr<HdrOperator> HdrOperator() const;

 private:
  // The minimum exposure time supported by the underlying camera.
  absl::Duration min_exposure_time_ = absl::ZeroDuration();

  // The maximum exposure time supported by the underlying camera.
  absl::Duration max_exposure_time_ = absl::InfiniteDuration();

  // The exposure time increment supported by the underlying camera.
  absl::Duration exposure_time_increment_ = absl::ZeroDuration();

  // This variable is used to enable multi-slope mode.
  HdrMode multi_slope_mode_ = HdrMode::kOff;

  // This variable maps knee-point selector to percentages of exposure time.
  std::vector<double> knee_point_exposure_percentages_;

  // This variable sets the active knee-point. By Genicam standards, knee-point
  // indexing starts at 1 but internally, it starts at 0.
  int active_knee_point_ = 0;
};

}  // namespace intrinsic::perception

#endif  // INTRINSIC_PERCEPTION_CAMERAS_SOFTWARE_HDR_H_
