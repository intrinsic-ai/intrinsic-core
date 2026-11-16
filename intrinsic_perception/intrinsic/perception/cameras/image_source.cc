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

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/base/nullability.h"
#include "absl/cleanup/cleanup.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/functional/any_invocable.h"
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
#include "intrinsic/perception/core/post_processing.h"
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

absl::StatusOr<CaptureResult> ApplyCommonCaptureProcessing(
    CaptureResult capture_result, const CaptureArgs& args) {
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
  return capture_result;
}

bool HasSensorDistortionParams(const SensorImage& sensor_image) {
  return sensor_image.camera_params().has_value() &&
         sensor_image.camera_params()->distortion_params.has_value();
}

bool WantsSensor(int64_t sensor_id, absl::Span<const int64_t> sensor_ids) {
  return sensor_ids.empty() || absl::c_contains(sensor_ids, sensor_id);
}

bool WantsUndistortion(
    int64_t sensor_id,
    const PostProcessingBySensorId& post_processing_by_sensor_id) {
  const auto it = post_processing_by_sensor_id.find(sensor_id);
  return it == post_processing_by_sensor_id.end() ||
         !it->second.skip_undistortion;
}

const PostProcessing* absl_nullable FindCropOrResize(
    int64_t sensor_id,
    const PostProcessingBySensorId& post_processing_by_sensor_id) {
  const auto it = post_processing_by_sensor_id.find(sensor_id);
  if (it != post_processing_by_sensor_id.end() &&
      (it->second.crop_region.has_value() || it->second.cols.has_value() ||
       it->second.rows.has_value())) {
    return &it->second;
  }
  return nullptr;
}

const SensorImage* absl_nullable FindSensorImage(
    absl::Span<const SensorImage> sensor_images, size_t expected_index,
    int64_t sensor_id) {
  if (expected_index < sensor_images.size() &&
      sensor_images[expected_index].sensor_id() == sensor_id) {
    return &sensor_images[expected_index];
  }
  const auto it = absl::c_find_if(
      sensor_images, [sensor_id](const SensorImage& sensor_image) {
        return sensor_image.sensor_id() == sensor_id;
      });
  return it != sensor_images.end() ? &*it : nullptr;
}

void RunAndLogCaptureCallback(ImageSourceInterface::CaptureCallback& callback,
                              const CaptureResult& result) {
  if (absl::Status status = callback(result); !status.ok()) {
    LOG(ERROR) << "Callback returned error status: " << status;
  }
}

std::optional<CaptureResult> BuildPerCallbackCaptureResult(
    const PostProcessingBySensorId& post_processing_by_sensor_id,
    absl::Span<const int64_t> sensor_ids, const CaptureResult& base_result,
    const std::optional<absl::StatusOr<CaptureResult>>& undistorted_base_result,
    absl::Mutex& crop_and_resize_mutex)
    ABSL_LOCKS_EXCLUDED(crop_and_resize_mutex) {
  CaptureResult callback_result{
      .capture_at = base_result.capture_at,
      .sensor_images = {},
      .capture_duration = base_result.capture_duration,
  };
  callback_result.sensor_images.reserve(base_result.sensor_images.size());
  for (size_t i = 0; i < base_result.sensor_images.size(); ++i) {
    const int64_t sensor_id = base_result.sensor_images[i].sensor_id();
    if (!WantsSensor(sensor_id, sensor_ids)) {
      continue;
    }
    const bool use_undistorted =
        HasSensorDistortionParams(base_result.sensor_images[i]) &&
        WantsUndistortion(sensor_id, post_processing_by_sensor_id);
    const SensorImage* source_image = &base_result.sensor_images[i];
    if (use_undistorted) {
      source_image = FindSensorImage((*undistorted_base_result)->sensor_images,
                                     i, sensor_id);
      if (source_image == nullptr) {
        LOG(WARNING) << "Undistorted image for sensor " << sensor_id
                     << " not found.";
        return std::nullopt;
      }
    }
    if (const PostProcessing* post_processing =
            FindCropOrResize(sensor_id, post_processing_by_sensor_id);
        post_processing != nullptr) {
      absl::StatusOr<SensorImage> cropped_image;
      if (post_processing->cols.has_value() ||
          post_processing->rows.has_value()) {
        absl::MutexLock lock(crop_and_resize_mutex);
        cropped_image = CropAndResize(*source_image, *post_processing);
      } else {
        cropped_image = CropAndResize(*source_image, *post_processing);
      }
      if (!cropped_image.ok()) {
        LOG(WARNING) << "Failed to process capture image for callback: "
                     << cropped_image.status();
        return std::nullopt;
      }
      callback_result.sensor_images.push_back(*std::move(cropped_image));
    } else {
      callback_result.sensor_images.push_back(*source_image);
    }
  }
  return callback_result;
}

void InvokeSingleCaptureCallback(
    ImageSourceInterface::CaptureCallback& callback,
    const PostProcessingBySensorId& post_processing_by_sensor_id,
    absl::Span<const int64_t> sensor_ids, const CaptureResult& base_result,
    const std::optional<absl::StatusOr<CaptureResult>>& undistorted_base_result,
    absl::Mutex& crop_and_resize_mutex)
    ABSL_LOCKS_EXCLUDED(crop_and_resize_mutex) {
  bool uses_undistorted = false;
  bool uses_raw_distorted = false;
  bool needs_crop_or_resize = false;
  bool filters_sensors = false;
  for (const SensorImage& sensor_image : base_result.sensor_images) {
    const int64_t sensor_id = sensor_image.sensor_id();
    if (!WantsSensor(sensor_id, sensor_ids)) {
      filters_sensors = true;
      continue;
    }
    if (FindCropOrResize(sensor_id, post_processing_by_sensor_id) != nullptr) {
      needs_crop_or_resize = true;
    }
    if (HasSensorDistortionParams(sensor_image)) {
      if (WantsUndistortion(sensor_id, post_processing_by_sensor_id)) {
        uses_undistorted = true;
      } else {
        uses_raw_distorted = true;
      }
    }
  }

  if (uses_undistorted) {
    if (!undistorted_base_result.has_value()) {
      LOG(WARNING) << "Missing undistorted base result for callback.";
      return;
    }
    if (!undistorted_base_result->ok()) {
      LOG(WARNING) << "Failed to undistort capture image for callback: "
                   << undistorted_base_result->status();
      return;
    }
  }

  // Fast path: no per-sensor filtering, crop/resize, or mixed distortion
  // states, so we can pass the shared result directly by const reference
  // without copying images.
  if (!filters_sensors && !needs_crop_or_resize &&
      !(uses_undistorted && uses_raw_distorted)) {
    RunAndLogCaptureCallback(
        callback, uses_undistorted ? **undistorted_base_result : base_result);
    return;
  }

  if (std::optional<CaptureResult> callback_result =
          BuildPerCallbackCaptureResult(
              post_processing_by_sensor_id, sensor_ids, base_result,
              undistorted_base_result, crop_and_resize_mutex);
      callback_result.has_value()) {
    RunAndLogCaptureCallback(callback, *callback_result);
  }
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
  INTR_ASSIGN_OR_RETURN(capture_result, ApplyCommonCaptureProcessing(
                                            std::move(capture_result), args));

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
                                         const CaptureArgs& default_args) {
  std::vector<RegisteredCallback> callbacks_snapshot;
  intrinsic::ThreadPool* callback_thread_pool = nullptr;
  {
    absl::MutexLock lock(callback_mutex_);
    if (callbacks_.empty()) {
      return;
    }
    callbacks_snapshot.reserve(callbacks_.size());
    for (const auto& [unused_token, entry] : callbacks_) {
      std::optional<CaptureCallbackOptions> effective_options = entry.options;
      if (effective_options.has_value() &&
          effective_options->post_processing_by_sensor_id.empty() &&
          !default_args.post_processing_by_sensor_id.empty()) {
        effective_options->post_processing_by_sensor_id =
            default_args.post_processing_by_sensor_id;
      }
      callbacks_snapshot.push_back(RegisteredCallback{
          .callback = entry.callback,
          .options = std::move(effective_options),
      });
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

  absl::StatusOr<CaptureResult> base_result =
      ApplyCommonCaptureProcessing(std::move(raw_capture_result), default_args);
  if (!base_result.ok()) {
    LOG(WARNING) << "Failed to apply common capture processing for callbacks: "
                 << base_result.status();
    return;
  }

  PostProcessingBySensorId shared_undistort_config;
  bool any_sensor_needs_undistortion = false;
  for (const SensorImage& sensor_image : base_result->sensor_images) {
    if (!HasSensorDistortionParams(sensor_image)) continue;
    const int64_t sensor_id = sensor_image.sensor_id();
    if (absl::c_any_of(callbacks_snapshot, [&](const RegisteredCallback& cb) {
          return cb.options.has_value() &&
                 WantsSensor(sensor_id, cb.options->sensor_ids) &&
                 WantsUndistortion(sensor_id,
                                   cb.options->post_processing_by_sensor_id);
        })) {
      any_sensor_needs_undistortion = true;
    } else {
      shared_undistort_config.emplace(
          sensor_id, PostProcessing{.skip_undistortion = true});
    }
  }

  std::optional<absl::StatusOr<CaptureResult>> undistorted_base_result;
  if (any_sensor_needs_undistortion) {
    absl::MutexLock lock(undistortion_mutex_);
    undistorted_base_result =
        Undistort(*base_result, shared_undistort_config, undistortion_);
  }

  const auto run_callback = [&](const RegisteredCallback& entry) {
    const ImageSource* previous_callback_source = current_callback_source;
    current_callback_source = this;
    const absl::Cleanup reset_source = [previous_callback_source]() {
      current_callback_source = previous_callback_source;
    };
    if (!entry.options.has_value()) {
      RunAndLogCaptureCallback(*entry.callback, *base_result);
      return;
    }
    InvokeSingleCaptureCallback(
        *entry.callback, entry.options->post_processing_by_sensor_id,
        entry.options->sensor_ids, *base_result, undistorted_base_result,
        crop_and_resize_mutex_);
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
ImageSource::AddCaptureCallback(CaptureCallback callback,
                                std::optional<CaptureCallbackOptions> options) {
  absl::MutexLock lock(callback_mutex_);
  static std::atomic<int> global_id = 0;
  CallbackToken callback_token(absl::StrCat(absl::Now(), global_id++));
  INTR_RET_CHECK(!callbacks_.contains(callback_token))
      << "Callback with id " << callback_token << " already exists.";
  callbacks_[callback_token] = RegisteredCallback{
      .callback = std::make_shared<CaptureCallback>(std::move(callback)),
      .options = std::move(options),
  };
  return callback_token;
}

absl::Status ImageSource::UpdateCaptureCallbackOptions(
    const CallbackToken& token, CaptureCallbackOptions options) {
  absl::MutexLock lock(callback_mutex_);
  auto it = callbacks_.find(token);
  if (it == callbacks_.end()) {
    return intrinsic::NotFoundErrorBuilder()
           << "Callback with id " << token << " not found.";
  }
  if (!it->second.options.has_value()) {
    return absl::FailedPreconditionError(
        "Cannot update options on a passive observer callback registered with "
        "std::nullopt.");
  }
  it->second.options = std::move(options);
  return absl::OkStatus();
}

absl::flat_hash_set<int64_t> ImageSource::GetActiveStreamSensorIds() const {
  absl::flat_hash_set<int64_t> union_sensor_ids;
  bool has_active_consumer = false;
  {
    absl::MutexLock lock(callback_mutex_);
    for (const auto& [unused_token, entry] : callbacks_) {
      if (!entry.options.has_value()) {
        continue;
      }
      // An empty `sensor_ids` list in a consumer's options means that consumer
      // requests all available sensors, so the union across active consumers
      // must also request all sensors (`{}`).
      if (entry.options->sensor_ids.empty()) {
        return {};
      }
      has_active_consumer = true;
      union_sensor_ids.insert(entry.options->sensor_ids.begin(),
                              entry.options->sensor_ids.end());
    }
  }
  if (has_active_consumer) {
    return union_sensor_ids;
  }
  absl::MutexLock lock(streaming_mutex_);
  return initial_stream_sensor_ids_;
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
  initial_stream_sensor_ids_ = std::move(sensor_ids);
  streaming_thread_.emplace([this, params](intrinsic::StopToken stop_token) {
    LOG(INFO) << "ImageSource::Thread: Starting streaming thread.";
    while (!stop_token.stop_requested()) {
      absl::StatusOr<CaptureResult> capture_result =
          CaptureImpl(params.timeout, GetActiveStreamSensorIds());
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
  {
    absl::MutexLock lock(streaming_mutex_);
    initial_stream_sensor_ids_.clear();
  }
  return absl::OkStatus();
}

}  // namespace intrinsic::perception
