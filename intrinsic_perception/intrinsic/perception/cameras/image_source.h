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

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/base/no_destructor.h"
#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
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

namespace intrinsic {
namespace perception {

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
      CaptureCallback callback) final;

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
  UndistortionBySensorId undistortion_;

  virtual absl::StatusOr<CaptureResult> CaptureImpl(absl::Duration timeout) {
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
      CaptureResult capture_result, const CaptureArgs& args);

  absl::Mutex callback_mutex_;
  absl::flat_hash_map<CallbackToken, CaptureCallback> callbacks_
      ABSL_GUARDED_BY(callback_mutex_);

  absl::Mutex streaming_mutex_;
  std::optional<intrinsic::Thread> streaming_thread_
      ABSL_GUARDED_BY(streaming_mutex_);
  // Used internally to prevent deadlocks when starting a stream but still have
  // all of the relevant settings update.
  bool is_stream_starting_ ABSL_GUARDED_BY(streaming_mutex_) = false;
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

}  // namespace perception
}  // namespace intrinsic

#endif  // INTRINSIC_PERCEPTION_CAMERAS_IMAGE_SOURCE_H_
