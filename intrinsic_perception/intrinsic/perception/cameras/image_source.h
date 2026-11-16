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

#ifndef INTRINSIC_PERCEPTION_CAMERAS_IMAGE_SOURCE_H_
#define INTRINSIC_PERCEPTION_CAMERAS_IMAGE_SOURCE_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/base/no_destructor.h"
#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/functional/function_ref.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "cppregpattern/registry.h"
#include "intrinsic/perception/cameras/camera_identifier.h"
#include "intrinsic/perception/cameras/camera_setting.h"
#include "intrinsic/perception/cameras/camera_setting_access.h"
#include "intrinsic/perception/cameras/camera_setting_properties.h"
#include "intrinsic/perception/cameras/capture_result.h"
#include "intrinsic/perception/cameras/image_source_interface.h"
#include "intrinsic/perception/cameras/sensor_information.h"
#include "intrinsic/perception/core/coordinate.h"
#include "intrinsic/perception/core/dimensions.h"
#include "intrinsic/perception/core/undistortion.h"
#include "intrinsic/util/status/status_builder.h"
#include "intrinsic/util/thread/thread.h"
#include "intrinsic/util/thread/thread_pool.h"

namespace intrinsic::perception {

// Represents an image source whose frames can be grabbed.
// The ImageSource is a building block of InternalCamera. ImageSource by itself
// only models an image acquisition device but does not specify camera
// intrinsics or distortion parameters (those only exist in InternalCamera).
class ImageSource : public ImageSourceInterface {
 public:
  ImageSource() = default;
  ~ImageSource() override = default;

  // ImageSources are not copyable nor movable, due to the absl::Mutex members
  ImageSource(ImageSource&& other) = delete;
  ImageSource& operator=(ImageSource&& other) = delete;
  ImageSource(const ImageSource&) = delete;
  ImageSource& operator=(const ImageSource&) = delete;

  absl::StatusOr<CaptureResult> Capture(const CaptureArgs& args) final;

  absl::StatusOr<CameraSettingAccess> ReadCameraSettingAccess(
      absl::string_view name) const final;

  absl::StatusOr<CameraSettingProperties> ReadCameraSettingProperties(
      absl::string_view name) const final;

  absl::StatusOr<CameraSetting> ReadCameraSetting(
      absl::string_view name) const final;

  absl::StatusOr<ImageSourceInterface::CallbackToken> AddCaptureCallback(
      CaptureCallback callback,
      std::optional<CaptureCallbackOptions> options) final;

  absl::Status UpdateCaptureCallbackOptions(
      const CallbackToken& token, CaptureCallbackOptions options) final;

  absl::Status RemoveCaptureCallback(CallbackToken token) final;

  absl::Status StartStream(const CaptureArgs& params) override;
  absl::Status StopStream() override;

  // Sets the user specified parameters.
  // This function performs a partial validation and checks if the specified
  // parameters are in conflict with the currently set camera parameters (if
  // any exist). In case of a conflict an invalid argument error is raised.
  // In all other cases, the function defers to the driver specific
  // implementation whose sole purpose is to actually set the parameters
  // without any additional error checking. Note: Even for drivers which to
  // not provide a concrete implementation of UpdateCameraSettingImpl() (see
  // below), this function may raise invalid argument errors.
  absl::Status UpdateCameraSetting(const CameraSetting& camera_setting) final;

  // Applies each setting in `camera_settings` if it is a command, cannot be
  // read, or differs from the currently read setting value.
  absl::Status ApplyCameraSettings(
      absl::Span<const CameraSetting> camera_settings) final;

  absl::StatusOr<std::vector<SensorInformation>> DescribeCameraSensors()
      const final;

 protected:
  // Captures raw sensor data from the driver, waiting up to `timeout`. When
  // `sensor_ids` is empty, captures from all available sensors; when
  // non-empty, drivers may restrict capture to those sensors (and
  // `PostCaptureImplProcessing` filters `sensor_images` afterward).
  virtual absl::StatusOr<CaptureResult> CaptureImpl(
      absl::Duration timeout, const absl::flat_hash_set<int64_t>& sensor_ids) {
    return intrinsic::UnimplementedErrorBuilder() << "CaptureImpl";
  }

  virtual absl::StatusOr<CameraSettingAccess> ReadCameraSettingAccessImpl(
      absl::string_view name) const {
    return intrinsic::UnimplementedErrorBuilder()
           << "ReadCameraSettingAccessImpl";
  }

  virtual absl::StatusOr<CameraSettingProperties>
  ReadCameraSettingPropertiesImpl(absl::string_view name) const {
    return intrinsic::UnimplementedErrorBuilder()
           << "ReadCameraSettingPropertiesImpl";
  }

  virtual absl::StatusOr<CameraSetting> ReadCameraSettingImpl(
      absl::string_view name) const {
    return intrinsic::UnimplementedErrorBuilder() << "ReadCameraSettingImpl";
  }

  // Derived classes should implement this function when offering functionality
  // to update / set camera settings.
  // The default function provided in the ImageSourceInterface has a concrete
  // implementation which is used for error checking.
  virtual absl::Status UpdateCameraSettingImpl(
      const CameraSetting& camera_setting) {
    return absl::UnimplementedError(
        "Updating camera settings is not implemented for the current driver.");
  }

  // Validates `camera_settings` and invokes `update_setting` for each setting
  // that is a command, cannot be read, or differs from the current readback.
  absl::Status ApplyCameraSettings(
      absl::Span<const CameraSetting> camera_settings,
      absl::FunctionRef<absl::Status(const CameraSetting&)> update_setting);

  // Derived classes should implement this function to offer sensor-specific
  // access, factory calibrations, and configuration options.
  virtual absl::StatusOr<std::vector<SensorInformation>>
  DescribeCameraSensorsImpl() const {
    return absl::UnimplementedError(
        "Camera driver doesn't provide sensor information.");
  }

  absl::StatusOr<Dimensions> ReadDimensions() const;

  absl::Status UpdateOffset(const Coordinate& offset);
  absl::Status UpdateDimensions(const Dimensions& dimensions);

  absl::StatusOr<CaptureResult> PostCaptureImplProcessing(
      CaptureResult capture_result, const CaptureArgs& args)
      ABSL_LOCKS_EXCLUDED(undistortion_mutex_, crop_and_resize_mutex_);

  // Applies `default_args.camera_config` to `raw_capture_result`, applies
  // per-callback post-processing for active consumers (falling back to
  // `default_args.post_processing_by_sensor_id` when empty, while skipping
  // undistortion and crop/resize for `std::nullopt` observers), and invokes
  // all registered callbacks. Callers must serialize calls to
  // `InvokeCaptureCallbacks` so a given callback is never invoked concurrently
  // with itself.
  void InvokeCaptureCallbacks(CaptureResult raw_capture_result,
                              const CaptureArgs& default_args)
      ABSL_LOCKS_EXCLUDED(callback_mutex_, undistortion_mutex_,
                          crop_and_resize_mutex_);

  // Returns the union of sensor IDs across all registered consumer callbacks
  // (`options.has_value()`), or an empty set if any consumer requests all
  // sensors (an empty `sensor_ids` list). Passive raw-stream observers
  // (`!options.has_value()`) do not affect the returned set. Falls back to the
  // `sensor_ids` passed to `StartStream()` when no consumer callback is
  // registered.
  absl::flat_hash_set<int64_t> GetActiveStreamSensorIds() const
      ABSL_LOCKS_EXCLUDED(callback_mutex_, streaming_mutex_);

 private:
  struct RegisteredCallback {
    std::shared_ptr<CaptureCallback> callback;
    std::optional<CaptureCallbackOptions> options;
  };

  // Guards `undistortion_` map lookup/initialization and serializes OpenCV
  // `Undistort` (`cv::remap`) calls on this `ImageSource` so concurrent
  // callers neither race on `undistortion_` entries nor collide on OpenCV's
  // global `cv::parallel_for_` `ThreadManager`.
  absl::Mutex undistortion_mutex_;
  UndistortionBySensorId undistortion_ ABSL_GUARDED_BY(undistortion_mutex_);
  // Serializes OpenCV `CropAndResize` (`cv::resize`) calls on this
  // `ImageSource` so concurrent post-processing callers (such as per-callback
  // post-processing or concurrent stream `Capture()` calls) do not collide on
  // OpenCV's global `cv::parallel_for_` `ThreadManager`, which otherwise forces
  // concurrent callers onto single-core fallback execution.
  absl::Mutex crop_and_resize_mutex_;

  mutable absl::Mutex callback_mutex_;
  absl::flat_hash_map<CallbackToken, RegisteredCallback> callbacks_
      ABSL_GUARDED_BY(callback_mutex_);
  absl::flat_hash_map<CallbackToken, std::weak_ptr<CaptureCallback>>
      in_flight_removed_callbacks_ ABSL_GUARDED_BY(callback_mutex_);
  std::optional<intrinsic::ThreadPool> callback_thread_pool_
      ABSL_GUARDED_BY(callback_mutex_);

  mutable absl::Mutex streaming_mutex_;
  // Used internally to prevent deadlocks when starting a stream but still have
  // all of the relevant settings update.
  bool is_stream_starting_ ABSL_GUARDED_BY(streaming_mutex_) = false;
  bool is_stream_stopping_ ABSL_GUARDED_BY(streaming_mutex_) = false;
  absl::flat_hash_set<int64_t> initial_stream_sensor_ids_
      ABSL_GUARDED_BY(streaming_mutex_);
  std::optional<intrinsic::Thread> streaming_thread_
      ABSL_GUARDED_BY(streaming_mutex_);
};

// Registration mechanism for factories that create the camera providers.
using ImageSourceRegistry =
    registry::Registry<std::string,
                       absl::StatusOr<std::unique_ptr<ImageSource>>(
                           const CameraIdentifier&),
                       registry::MissingKeyPolicy::default_construct>;

inline absl::flat_hash_map<std::string, std::string>&
GetImageSourceAliasRegistry() {
  static absl::NoDestructor<absl::flat_hash_map<std::string, std::string>>
      registry;
  return *registry;
}

// In some cases, we have implemented multiple concrete image sources through a
// single interface. For these cases, it is possible to register additional
// aliases for the same image source.
#define REGISTER_IMAGE_SOURCE(name, alias, FactoryName)          \
  [[maybe_unused]] const bool kUnused##name =                    \
      intrinsic::perception::ImageSourceRegistry::Register(      \
          #name, [](const CameraIdentifier& camera_identifier) { \
            return FactoryName(camera_identifier);               \
          });                                                    \
  [[maybe_unused]] const bool kUnusedAlias##name = []() {        \
    intrinsic::perception::GetImageSourceAliasRegistry().insert( \
        {alias, #name});                                         \
    return true;                                                 \
  }();

#define REGISTER_ADDITIONAL_IMAGE_SOURCE(name, alias)            \
  [[maybe_unused]] const bool kUnusedExtraAlias##name = []() {   \
    intrinsic::perception::GetImageSourceAliasRegistry().insert( \
        {alias, #name});                                         \
    return true;                                                 \
  }();

}  // namespace intrinsic::perception

#endif  // INTRINSIC_PERCEPTION_CAMERAS_IMAGE_SOURCE_H_
