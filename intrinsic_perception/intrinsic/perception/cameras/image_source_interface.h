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

#ifndef INTRINSIC_PERCEPTION_CAMERAS_IMAGE_SOURCE_INTERFACE_H_
#define INTRINSIC_PERCEPTION_CAMERAS_IMAGE_SOURCE_INTERFACE_H_

#include <cstdint>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "intrinsic/perception/cameras/camera_setting.h"
#include "intrinsic/perception/cameras/camera_setting_access.h"
#include "intrinsic/perception/cameras/camera_setting_properties.h"
#include "intrinsic/perception/cameras/capture_result.h"
#include "intrinsic/perception/cameras/sensor_information.h"
#include "intrinsic/util/string_type.h"

namespace intrinsic::perception {

class ImageSourceInterface {
 public:
  // LINT.IfChange(fallback_sensor_id)
  static constexpr int64_t kFallbackSensorId = 0;
  // LINT.ThenChange(//intrinsic/frontend/shared/perception/sensor_id.ts:fallback_sensor_id)

  ImageSourceInterface() = default;
  virtual ~ImageSourceInterface() = default;

  // ImageSourceInterface is not copyable but move only.
  ImageSourceInterface(ImageSourceInterface&& other) = default;
  ImageSourceInterface& operator=(ImageSourceInterface&& other) = default;
  ImageSourceInterface(const ImageSourceInterface&) = delete;
  ImageSourceInterface& operator=(const ImageSourceInterface&) = delete;

  virtual absl::StatusOr<std::vector<SensorInformation>> DescribeCameraSensors()
      const = 0;

  // Captures from the image source and returns a capture result containing the
  // sensor images.
  // Note: Making Capture() const is complicated since many underlying libraries
  //    mutate internal state when acquiring frames.
  virtual absl::StatusOr<CaptureResult> Capture(const CaptureArgs& args) = 0;

  virtual absl::StatusOr<CameraSettingAccess> ReadCameraSettingAccess(
      absl::string_view name) const = 0;

  virtual absl::StatusOr<CameraSettingProperties> ReadCameraSettingProperties(
      absl::string_view name) const = 0;

  virtual absl::StatusOr<CameraSetting> ReadCameraSetting(
      absl::string_view name) const = 0;

  virtual absl::Status UpdateCameraSetting(
      const CameraSetting& camera_setting) = 0;

  // Applies each setting in `camera_settings` if it is a command, cannot be
  // read, or differs from the currently read setting value.
  virtual absl::Status ApplyCameraSettings(
      absl::Span<const CameraSetting> camera_settings) {
    return absl::UnimplementedError(
        "Applying camera settings is not implemented for the current driver.");
  }

  // A token that represents a registered callback.
  INTRINSIC_DEFINE_STRING_TYPE(CallbackToken);

  using CaptureCallback =
      absl::AnyInvocable<absl::Status(const CaptureResult&)>;

  // Add a callback that will receive new capture results
  virtual absl::StatusOr<CallbackToken> AddCaptureCallback(
      CaptureCallback callback) = 0;

  // Remove a previously registered callback.
  virtual absl::Status RemoveCaptureCallback(CallbackToken token) = 0;

  // Starts a stream with the given CaptureArgs and returns the images back into
  // any registered callbacks. Multiple streams may be supported by the
  // underlying driver, but that is not guaranteed, and may result in an error
  // for subsequent calls.
  virtual absl::Status StartStream(const CaptureArgs& params) = 0;

  // Stops any currently running streams, no-op if there is no stream running.
  virtual absl::Status StopStream() = 0;

  virtual absl::Status GetFaultsStatus() const {
    return absl::UnimplementedError(
        "Faults status is not implemented for the current driver.");
  }

  virtual absl::Status ClearFaults() {
    return absl::UnimplementedError(
        "Clear faults is not implemented for the current driver.");
  }
};

}  // namespace intrinsic::perception

#endif  // INTRINSIC_PERCEPTION_CAMERAS_IMAGE_SOURCE_INTERFACE_H_
