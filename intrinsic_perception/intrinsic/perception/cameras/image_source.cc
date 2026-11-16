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

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/cleanup/cleanup.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/functional/any_invocable.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/blocking_counter.h"
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
#include "intrinsic/util/thread/thread.h"
#include "intrinsic/util/thread/thread_pool.h"

namespace intrinsic::perception {
namespace {

constexpr int kCallbackThreadPoolSize = 4;

// Tracks the `ImageSource` currently invoking a `CaptureCallback` on the
// calling thread so reentrant `RemoveCaptureCallback` calls from within a
// callback do not deadlock waiting for `InvokeCaptureCallbacks` to finish.
thread_local const ImageSource* current_callback_source = nullptr;

absl::StatusOr<absl::flat_hash_set<int64_t>> BuildUniqueSensorIdSet(
    absl::Span<const int64_t> sensor_ids) {
  absl::flat_hash_set<int64_t> unique_ids(sensor_ids.begin(), sensor_ids.end());
  if (unique_ids.size() != sensor_ids.size()) {
    return absl::InvalidArgumentError("Duplicate sensor_ids are not allowed.");
  }
  return unique_ids;
}

}  // namespace

absl::StatusOr<CaptureResult> ImageSource::Capture(const CaptureArgs& args) {
  {
    // We do not allow streaming and capturing at the same time. This can be
    // problematic if the camera parameters are not the same. See the TDD for
    // more details: go/intrinsic-camera-streaming-design
    absl::MutexLock lock(streaming_mutex_);
    if (streaming_thread_.has_value() || is_stream_stopping_) {
      return absl::FailedPreconditionError(
          "We do not allow Capture calls while there is a stream running.");
    }
  }

  INTR_ASSIGN_OR_RETURN(const absl::flat_hash_set<int64_t> sensor_ids,
                        BuildUniqueSensorIdSet(args.sensor_ids));

  {
    const stats::ScopedSpan span("ImageSource::Capture::UpdateSettings");
    INTR_RETURN_IF_ERROR(
        ApplyCameraSettings(args.camera_config.camera_settings));
  }

  INTR_ASSIGN_OR_RETURN(CaptureResult capture_result,
                        CaptureImpl(args.timeout, sensor_ids));

  {
    const stats::ScopedSpan span("ImageSource::Capture::PostProcess");
    INTR_ASSIGN_OR_RETURN(capture_result, PostCaptureImplProcessing(
                                              std::move(capture_result), args));
  }
  return capture_result;
}

absl::Status ImageSource::ApplyCameraSettings(
    absl::Span<const CameraSetting> camera_settings) {
  return ApplyCameraSettings(camera_settings,
                             [this](const CameraSetting& camera_setting) {
                               return UpdateCameraSetting(camera_setting);
                             });
}

absl::Status ImageSource::ApplyCameraSettings(
    absl::Span<const CameraSetting> camera_settings,
    absl::FunctionRef<absl::Status(const CameraSetting&)> update_setting) {
  for (const CameraSetting& camera_setting : camera_settings) {
    if (camera_setting.name.empty()) {
      return absl::InvalidArgumentError(
          "Camera setting 'name' cannot be empty.");
    }
  }
  for (const CameraSetting& camera_setting : camera_settings) {
    if (std::holds_alternative<CameraSetting::Command>(camera_setting.value)) {
      INTR_RETURN_IF_ERROR(update_setting(camera_setting));
    } else if (const absl::StatusOr<CameraSetting> current_setting =
                   ReadCameraSetting(camera_setting.name);
               !current_setting.ok() ||
               !current_setting->IsApprox(camera_setting)) {
      INTR_RETURN_IF_ERROR(update_setting(camera_setting));
    }
  }
  return absl::OkStatus();
}

absl::StatusOr<CaptureResult> ImageSource::PostCaptureImplProcessing(
    CaptureResult capture_result, const CaptureArgs& args) {
  if (!args.sensor_ids.empty()) {
    if (!IsUnique(args.sensor_ids)) {
      return absl::InvalidArgumentError(
          "Duplicate sensor_ids are not allowed.");
    }
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
  {
    absl::MutexLock lock(undistortion_mutex_);
    INTR_ASSIGN_OR_RETURN(
        capture_result,
        Undistort(std::move(capture_result), args.post_processing_by_sensor_id,
                  undistortion_));
  }
  bool needs_crop = false;
  bool needs_resize = false;
  for (const SensorImage& sensor_image : capture_result.sensor_images) {
    const auto it =
        args.post_processing_by_sensor_id.find(sensor_image.sensor_id());
    if (it == args.post_processing_by_sensor_id.end()) {
      continue;
    }
    if (it->second.crop_region.has_value()) {
      needs_crop = true;
    }
    if (it->second.cols.has_value() || it->second.rows.has_value()) {
      needs_resize = true;
    }
  }
  if (!needs_crop && !needs_resize) {
    return capture_result;
  }
  if (needs_resize) {
    absl::MutexLock lock(crop_and_resize_mutex_);
    return CropAndResize(std::move(capture_result),
                         args.post_processing_by_sensor_id);
  }
  return CropAndResize(std::move(capture_result),
                       args.post_processing_by_sensor_id);
}

void ImageSource::InvokeCaptureCallbacks(CaptureResult raw_capture_result,
                                         const CaptureArgs& args) {
  std::vector<RegisteredCallback> callbacks_snapshot;
  intrinsic::ThreadPool* callback_thread_pool = nullptr;
  {
    absl::MutexLock lock(callback_mutex_);
    if (callbacks_.empty()) {
      return;
    }
    callbacks_snapshot.reserve(callbacks_.size());
    for (const auto& [unused_token, entry] : callbacks_) {
      callbacks_snapshot.push_back(entry);
    }
    if (callbacks_snapshot.size() > 1) {
      if (!callback_thread_pool_.has_value()) {
        callback_thread_pool_.emplace(kCallbackThreadPoolSize);
      }
      callback_thread_pool = &*callback_thread_pool_;
    }
  }
  const absl::Cleanup release_snapshot = [this, &callbacks_snapshot]() {
    callbacks_snapshot.clear();
    absl::MutexLock lock(callback_mutex_);
    absl::erase_if(in_flight_removed_callbacks_,
                   [](const auto& item) { return item.second.expired(); });
  };

  absl::StatusOr<CaptureResult> processed_result =
      PostCaptureImplProcessing(std::move(raw_capture_result), args);
  if (!processed_result.ok()) {
    LOG(WARNING) << "Failed to process capture image: "
                 << processed_result.status();
    return;
  }

  const auto run_callback =
      [this, &processed_result](const RegisteredCallback& entry) {
        const ImageSource* previous_callback_source = current_callback_source;
        current_callback_source = this;
        const absl::Cleanup reset_source = [previous_callback_source]() {
          current_callback_source = previous_callback_source;
        };
        if (absl::Status status = (*entry.callback)(*processed_result);
            !status.ok()) {
          LOG(ERROR) << "Callback returned error status: " << status;
        }
      };

  if (callbacks_snapshot.size() == 1) {
    run_callback(callbacks_snapshot[0]);
    return;
  }

  absl::BlockingCounter counter(
      static_cast<int>(callbacks_snapshot.size() - 1));
  for (size_t i = 1; i < callbacks_snapshot.size(); ++i) {
    const RegisteredCallback& entry = callbacks_snapshot[i];
    if (absl::Status schedule_status =
            callback_thread_pool->Schedule([&run_callback, &entry, &counter]() {
              run_callback(entry);
              counter.DecrementCount();
            });
        !schedule_status.ok()) {
      run_callback(entry);
      counter.DecrementCount();
    }
  }
  run_callback(callbacks_snapshot[0]);
  counter.Wait();
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
    if ((streaming_thread_.has_value() || is_stream_stopping_) &&
        !is_stream_starting_) {
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
  callbacks_[callback_token] = RegisteredCallback{
      .callback = std::make_shared<CaptureCallback>(std::move(callback)),
  };
  return callback_token;
}

absl::Status ImageSource::RemoveCaptureCallback(CallbackToken token) {
  std::shared_ptr<CaptureCallback> removed_callback;
  {
    absl::MutexLock lock(callback_mutex_);
    if (auto it = callbacks_.find(token); it != callbacks_.end()) {
      removed_callback = std::move(it->second.callback);
      callbacks_.erase(it);
      in_flight_removed_callbacks_[token] = removed_callback;
    }
  }
  // Destroy `removed_callback` outside `callback_mutex_` so a callback's
  // destructor cannot self-deadlock on `callback_mutex_`. If the callback is
  // currently in flight inside `InvokeCaptureCallbacks`, `callbacks_snapshot`
  // retains a strong reference until the invocation completes.
  removed_callback.reset();

  if (current_callback_source != this) {
    absl::MutexLock lock(callback_mutex_);
    const auto callback_not_in_flight =
        [this, &token]() ABSL_SHARED_LOCKS_REQUIRED(callback_mutex_) {
          auto it = in_flight_removed_callbacks_.find(token);
          return it == in_flight_removed_callbacks_.end() ||
                 it->second.expired();
        };
    callback_mutex_.Await(absl::Condition(&callback_not_in_flight));
    in_flight_removed_callbacks_.erase(token);
  }
  return absl::OkStatus();
}

absl::Status ImageSource::StartStream(const CaptureArgs& params) {
  INTR_ASSIGN_OR_RETURN(absl::flat_hash_set<int64_t> sensor_ids,
                        BuildUniqueSensorIdSet(params.sensor_ids));
  {
    absl::MutexLock lock(streaming_mutex_);
    if (streaming_thread_.has_value() || is_stream_starting_ ||
        is_stream_stopping_) {
      return absl::FailedPreconditionError("Stream already started.");
    }
    is_stream_starting_ = true;
  }
  absl::Cleanup reset_stream_starting = [this] {
    absl::MutexLock lock(streaming_mutex_);
    is_stream_starting_ = false;
  };

  // Apply all camera settings once before we start the capture loop.
  INTR_RETURN_IF_ERROR(
      ApplyCameraSettings(params.camera_config.camera_settings));

  absl::MutexLock lock(streaming_mutex_);
  std::move(reset_stream_starting).Cancel();
  is_stream_starting_ = false;
  streaming_thread_.emplace([this, params, sensor_ids = std::move(sensor_ids)](
                                intrinsic::StopToken stop_token) {
    LOG(INFO) << "ImageSource::Thread: Starting streaming thread.";
    while (!stop_token.stop_requested()) {
      absl::StatusOr<CaptureResult> capture_result =
          CaptureImpl(params.timeout, sensor_ids);
      if (!capture_result.ok()) {
        LOG(WARNING) << "Failed to capture image: " << capture_result.status();
        continue;
      }

      InvokeCaptureCallbacks(*std::move(capture_result), params);
    }
  });

  return absl::OkStatus();
}

absl::Status ImageSource::StopStream() {
  std::optional<intrinsic::Thread> streaming_thread;
  {
    absl::MutexLock lock(streaming_mutex_);
    const auto not_stopping = [this]()
                                  ABSL_SHARED_LOCKS_REQUIRED(streaming_mutex_) {
                                    return !is_stream_stopping_;
                                  };
    streaming_mutex_.Await(absl::Condition(&not_stopping));
    if (!streaming_thread_.has_value()) {
      return absl::OkStatus();
    }
    is_stream_stopping_ = true;
    streaming_thread = std::move(streaming_thread_);
    streaming_thread_.reset();
  }
  const absl::Cleanup reset_stopping = [this]() {
    absl::MutexLock lock(streaming_mutex_);
    is_stream_stopping_ = false;
  };
  // Deallocating the streaming thread requests a stop on the stop token and
  // joins the thread before completing the destructor call. Perform this
  // outside `streaming_mutex_` so `InvokeCaptureCallbacks` on the streaming
  // thread cannot deadlock with `StopStream()`.
  streaming_thread.reset();
  return absl::OkStatus();
}

}  // namespace intrinsic::perception
