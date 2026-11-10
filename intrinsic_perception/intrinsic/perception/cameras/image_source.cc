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

#include "intrinsic/perception/cameras/image_source.h"

#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/container/flat_hash_map.h"
#include "absl/functional/any_invocable.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/types/span.h"
#include "intrinsic/perception/cameras/camera_setting.h"
#include "intrinsic/perception/cameras/camera_setting_access.h"
#include "intrinsic/perception/cameras/camera_setting_properties.h"
#include "intrinsic/perception/cameras/capture_result.h"
#include "intrinsic/perception/cameras/capture_result_helper.h"
#include "intrinsic/perception/cameras/genicam/feature_names.h"
#include "intrinsic/perception/cameras/image_source_interface.h"
#include "intrinsic/perception/cameras/sensor_image.h"
#include "intrinsic/perception/cameras/sensor_information.h"
#include "intrinsic/perception/core/camera_params.h"
#include "intrinsic/perception/core/coordinate.h"
#include "intrinsic/perception/core/dimensions.h"
#include "intrinsic/perception/core/range_tools.h"
#include "intrinsic/stats/scoped_span.h"
#include "intrinsic/util/status/ret_check.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/thread/stop_token.h"

namespace intrinsic {
namespace perception {

absl::StatusOr<CaptureResult> ImageSource::Capture(const CaptureArgs& args) {
  {
    // We do not allow streaming and capturing at the same time. This can be
    // problematic if the camera parameters are not the same. See the TDD for
    // more details: go/intrinsic-camera-streaming-design
    absl::MutexLock lock(streaming_mutex_);
    if (streaming_thread_.has_value()) {
      return absl::FailedPreconditionError(
          "We do not allow Capture calls while there is a stream running.");
    }
  }

  {
    const stats::ScopedSpan span("ImageSource::Capture::UpdateSettings");
    INTR_RETURN_IF_ERROR(
        ApplyCameraSettings(args.camera_config.camera_settings));
  }

  INTR_ASSIGN_OR_RETURN(CaptureResult capture_result,
                        CaptureImpl(args.timeout));

  {
    const stats::ScopedSpan span("ImageSource::Capture::PostProcess");
    INTR_ASSIGN_OR_RETURN(capture_result, PostCaptureImplProcessing(
                                              std::move(capture_result), args));
  }
  return capture_result;
}

absl::Status ImageSource::ApplyCameraSettings(
    absl::Span<const CameraSetting> camera_settings) {
  for (const CameraSetting& camera_setting : camera_settings) {
    if (camera_setting.name.empty()) {
      return absl::InvalidArgumentError(
          "Camera setting 'name' cannot be empty.");
    }
  }
  for (const CameraSetting& camera_setting : camera_settings) {
    if (std::holds_alternative<CameraSetting::Command>(camera_setting.value)) {
      INTR_RETURN_IF_ERROR(UpdateCameraSetting(camera_setting));
    } else if (const absl::StatusOr<CameraSetting> current_setting =
                   ReadCameraSetting(camera_setting.name);
               !current_setting.ok() ||
               !current_setting->IsApprox(camera_setting)) {
      INTR_RETURN_IF_ERROR(UpdateCameraSetting(camera_setting));
    }
  }
  return absl::OkStatus();
}

absl::StatusOr<CaptureResult> ImageSource::PostCaptureImplProcessing(
    CaptureResult capture_result, const CaptureArgs& args) {
  if (!args.sensor_ids.empty()) {
    INTR_RET_CHECK(IsUnique(args.sensor_ids));
    std::erase_if(capture_result.sensor_images, [&args](const SensorImage& i) {
      return !absl::c_contains(args.sensor_ids, i.sensor_id());
    });
  }

  if (!args.camera_config.camera_params_by_sensor_id.empty()) {
    INTR_ASSIGN_OR_RETURN(
        capture_result,
        ApplyCameraParams(std::move(capture_result),
                          args.camera_config.camera_params_by_sensor_id));
  }

  if (!args.camera_config.camera_t_sensor_by_sensor_id.empty()) {
    INTR_ASSIGN_OR_RETURN(
        capture_result,
        ApplyCameraTsSensor(std::move(capture_result),
                            args.camera_config.camera_t_sensor_by_sensor_id));
  }

  // Even for an empty post processing map we apply post processing, as by
  // default undistortion needs to be applied to all images.
  INTR_ASSIGN_OR_RETURN(capture_result,
                        PostProcessCaptureResult(
                            std::move(capture_result),
                            args.post_processing_by_sensor_id, undistortion_));
  return capture_result;
}

absl::StatusOr<CameraSettingProperties>
ImageSource::ReadCameraSettingProperties(absl::string_view name) const {
  return ReadCameraSettingPropertiesImpl(name);
}

absl::StatusOr<CameraSettingAccess> ImageSource::ReadCameraSettingAccess(
    absl::string_view name) const {
  return ReadCameraSettingAccessImpl(name);
}

absl::StatusOr<CameraSetting> ImageSource::ReadCameraSetting(
    absl::string_view name) const {
  return ReadCameraSettingImpl(name);
}

absl::Status ImageSource::UpdateCameraSetting(
    const CameraSetting& camera_setting) {
  if (camera_setting.name.empty()) {
    return absl::InvalidArgumentError("Camera setting 'name' cannot be empty.");
  }

  {
    absl::MutexLock lock(streaming_mutex_);
    if (streaming_thread_.has_value() && !is_stream_starting_) {
      return absl::FailedPreconditionError(
          "We do not allow UpdateCameraSetting calls while there is a stream "
          "running.");
    }
  }

  return UpdateCameraSettingImpl(camera_setting);
}

absl::StatusOr<std::vector<SensorInformation>>
ImageSource::DescribeCameraSensors() const {
  return DescribeCameraSensorsImpl();
}

absl::StatusOr<Dimensions> ImageSource::ReadDimensions() const {
  INTR_ASSIGN_OR_RETURN(const CameraSetting width,
                        ReadCameraSetting(genicam::kWidth));
  INTR_ASSIGN_OR_RETURN(const CameraSetting height,
                        ReadCameraSetting(genicam::kHeight));
  INTR_RET_CHECK(std::holds_alternative<CameraSetting::Integer>(width.value));
  INTR_RET_CHECK(std::holds_alternative<CameraSetting::Integer>(height.value));
  return Dimensions(std::get<CameraSetting::Integer>(width.value).value,
                    std::get<CameraSetting::Integer>(height.value).value);
}

absl::Status ImageSource::UpdateOffset(const Coordinate& offset) {
  absl::Status status = absl::OkStatus();
  status.Update(UpdateCameraSetting(
      {.name = genicam::kOffsetX,
       .value = CameraSetting::Integer{.value = offset.col}}));
  status.Update(UpdateCameraSetting(
      {.name = genicam::kOffsetY,
       .value = CameraSetting::Integer{.value = offset.row}}));
  return status;
}

absl::Status ImageSource::UpdateDimensions(const Dimensions& dimensions) {
  absl::Status status = absl::OkStatus();
  status.Update(UpdateCameraSetting(
      {.name = genicam::kWidth,
       .value = CameraSetting::Integer{.value = dimensions.cols}}));
  status.Update(UpdateCameraSetting(
      {.name = genicam::kHeight,
       .value = CameraSetting::Integer{.value = dimensions.rows}}));
  return status;
}

absl::StatusOr<ImageSourceInterface::CallbackToken>
ImageSource::AddCaptureCallback(CaptureCallback callback) {
  absl::MutexLock lock(callback_mutex_);
  static int global_id = 0;
  CallbackToken callback_token(absl::StrCat(absl::Now(), global_id++));
  INTR_RET_CHECK(!callbacks_.contains(callback_token))
      << "Callback with id " << callback_token << " already exists.";
  callbacks_[callback_token] = std::move(callback);
  return callback_token;
}

absl::Status ImageSource::RemoveCaptureCallback(CallbackToken token) {
  absl::MutexLock lock(callback_mutex_);
  callbacks_.erase(token);
  return absl::OkStatus();
}

absl::Status ImageSource::StartStream(const CaptureArgs& params) {
  {
    absl::MutexLock lock(streaming_mutex_);
    if (streaming_thread_.has_value() || is_stream_starting_) {
      return absl::FailedPreconditionError("Stream already started.");
    }
    is_stream_starting_ = true;
  }

  // Apply all camera settings once before we start the capture loop.
  INTR_RETURN_IF_ERROR(
      ApplyCameraSettings(params.camera_config.camera_settings));

  absl::MutexLock lock(streaming_mutex_);
  is_stream_starting_ = false;
  streaming_thread_.emplace([this, params](intrinsic::StopToken stop_token) {
    LOG(INFO) << "ImageSource::Thread: Starting streaming thread.";
    while (!stop_token.stop_requested()) {
      absl::StatusOr<CaptureResult> capture_result =
          CaptureImpl(params.timeout);
      if (!capture_result.ok()) {
        LOG(WARNING) << "Failed to capture image: " << capture_result.status();
        continue;
      }

      capture_result =
          PostCaptureImplProcessing(std::move(capture_result).value(), params);
      if (!capture_result.ok()) {
        LOG(WARNING) << "Failed to process capture image: "
                     << capture_result.status();
        continue;
      }

      absl::MutexLock lock(callback_mutex_);
      for (auto& [_, callback] : callbacks_) {
        if (absl::Status status = callback(*capture_result); !status.ok()) {
          LOG(ERROR) << "Callback returned error status: " << status;
        }
      }
    }
  });

  return absl::OkStatus();
}

absl::Status ImageSource::StopStream() {
  absl::MutexLock lock(streaming_mutex_);
  if (streaming_thread_.has_value()) {
    // This will deallocate the streaming thread and that will request a stop on
    // the stop token, which will then exit, and join before completing the
    // destructor call.
    streaming_thread_.reset();
  }
  return absl::OkStatus();
}

}  // namespace perception
}  // namespace intrinsic
