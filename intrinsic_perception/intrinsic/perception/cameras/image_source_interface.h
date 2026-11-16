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
#include <optional>
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
#include "intrinsic/perception/core/post_processing.h"
#include "intrinsic/util/string_type.h"

namespace intrinsic::perception {

// Per-consumer post-processing and sensor filtering options for a stream
// capture callback.
struct CaptureCallbackOptions {
  // Per-sensor post-processing (undistortion, crop, resize). By default,
  // undistortion is applied to all sensors with distortion parameters unless
  // `skip_undistortion = true` is set for that sensor.
  PostProcessingBySensorId post_processing_by_sensor_id;
  // Sensor IDs to capture and deliver to the callback. Empty means all sensors.
  std::vector<int64_t> sensor_ids;
};

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

  // Add a callback that will receive new capture results. Different
  // registered callbacks may be invoked concurrently on arbitrary threads,
  // while a given callback is never invoked concurrently with itself.
  // - When `options` is `std::nullopt`, the callback is registered as a
  //   passive raw-stream observer: it receives all captured sensors without
  //   per-callback post-processing and does not vote on active hardware sensor
  //   selection.
  // - When `options` is populated (including `CaptureCallbackOptions{}`), the
  //   callback is registered as an active consumer:
  //   - `options->post_processing_by_sensor_id`: applies default undistortion
  //     (unless `skip_undistortion` is set) and any per-sensor crop/resize. An
  //     empty map inherits `StartStream`'s `post_processing_by_sensor_id` map
  //     as a whole (all or nothing).
  //   - `options->sensor_ids`: filters sensors delivered to this callback and
  //     votes on active hardware sensor selection. An empty list requests all
  //     sensors regardless of `StartStream`'s `sensor_ids`.
  virtual absl::StatusOr<CallbackToken> AddCaptureCallback(
      CaptureCallback callback,
      std::optional<CaptureCallbackOptions> options) = 0;

  // Updates the post-processing and sensor filter options for a previously
  // registered active consumer callback (with the same `CaptureCallbackOptions`
  // fallback semantics as `AddCaptureCallback`), replacing its options
  // wholesale. Returns `NOT_FOUND` if `token` is not registered, or
  // `FAILED_PRECONDITION` if `token` was registered as a passive observer
  // (`std::nullopt`).
  virtual absl::Status UpdateCaptureCallbackOptions(
      const CallbackToken& token, CaptureCallbackOptions options) = 0;

  // Validates that `settings` are compatible with the active stream's effective
  // settings when streaming is active. Returns `absl::OkStatus()` if `settings`
  // are compatible or if no stream is active, `FAILED_PRECONDITION` if any
  // setting conflicts or cannot be verified against hardware, or
  // `UNIMPLEMENTED` if the driver does not support compatibility checks.
  virtual absl::Status CheckStreamSettingsCompatibility(
      absl::Span<const CameraSetting> settings) const = 0;

  // Remove a previously registered callback. Blocks until any in-flight
  // invocation of the callback returns, except when called reentrantly from
  // within any capture callback of the same `ImageSourceInterface`.
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
