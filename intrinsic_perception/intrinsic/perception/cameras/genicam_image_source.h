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

#ifndef INTRINSIC_PERCEPTION_CAMERAS_GENICAM_IMAGE_SOURCE_H_
#define INTRINSIC_PERCEPTION_CAMERAS_GENICAM_IMAGE_SOURCE_H_

#include <arv.h>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/perception/cameras/aravis/aravis_glib_utils.h"
#include "intrinsic/perception/cameras/aravis/aravis_utils.h"
#include "intrinsic/perception/cameras/camera_identifier.h"
#include "intrinsic/perception/cameras/camera_setting.h"
#include "intrinsic/perception/cameras/camera_setting_access.h"
#include "intrinsic/perception/cameras/camera_setting_properties.h"
#include "intrinsic/perception/cameras/capture_result.h"
#include "intrinsic/perception/cameras/image_source.h"
#include "intrinsic/perception/cameras/sensor_information.h"
#include "intrinsic/perception/cameras/software_hdr.h"
#include "intrinsic/perception/core/camera_params.h"
#include "intrinsic/perception/core/dimensions.h"
#include "intrinsic/perception/core/hdr_utils.h"
#include "intrinsic/stats/scoped_span.h"

namespace intrinsic::perception {

class GenicamImageSource : public ImageSource {
 public:
  // Creates and returns a GenicamImageSource implemented with Aravis.
  //
  // Design note: The function returns a unique_ptr<> as that interface is
  // preferred over manually implementing a move ctor which is error prone.
  static absl::StatusOr<std::unique_ptr<ImageSource>> Create(
      const CameraIdentifier& camera_identifier);

  // Creates and returns a GenicamImageSource implemented with Aravis.
  //
  // Once this function is called, fake cameras are activated for the current
  // process. The camera ID "Fake_1" has a special meaning after invoking this
  // function as it will trigger the creation of a fake and not a regular
  // camera.
  static absl::StatusOr<std::unique_ptr<ImageSource>> CreateFake(
      const CameraIdentifier&);

  ~GenicamImageSource() override;

  absl::Status GetFaultsStatus() const final;

  absl::Status ClearFaults() final;

  absl::Status StartStream(const CaptureArgs& params) override;
  absl::Status StopStream() override;

 protected:
  explicit GenicamImageSource(std::string_view device_id);

  absl::Status Init(bool requires_device_reset);

 private:
  struct CaptureResultWithExposureTime {
    CaptureResult capture_result;
    std::optional<absl::Duration> exposure_time;
  };
  struct StreamingHdrState {
    std::vector<absl::Duration> exposure_times;
    std::vector<CameraSetting> auto_settings_to_restore;
    int num_tries = 0;
    HdrOperator hdr_operator = HdrOperator::kMertens;
    std::vector<CaptureResult> ldr_frames;
  };
  struct StreamingState {
    CaptureArgs params;
    // True once StartStream() has finished configuring the camera and while
    // streaming acquisition is active; false while StartStream() is setting up
    // or StopStream() is tearing down the stream.
    bool is_active = false;
    std::optional<StreamingHdrState> hdr;
  };

  absl::StatusOr<std::vector<CameraSetting>> GetActiveAutoSettings() const;
  // Switches any `Continuous` settings in `active_auto_settings` to `Once`
  // (appending the original `Continuous` settings to `auto_settings_to_restore`
  // so the caller can restore them later) and acquires frames until all
  // settings in `active_auto_settings` converge to `Off` (or forces them to
  // `Off` after `kMaxNumFramesToConverge` frames). Returns the last acquired
  // frame. Precondition: `!active_auto_settings.empty()`.
  absl::StatusOr<CaptureResult> ConvergeAutoSettings(
      absl::Span<const CameraSetting> active_auto_settings,
      absl::Duration timeout,
      std::vector<CameraSetting>& auto_settings_to_restore);
  absl::Status RestoreAutoSettings(
      absl::Span<const CameraSetting> auto_settings_to_restore);
  // Returns the base exposure time at `front()` followed by sorted unique
  // knee-point exposure times. Returns an empty vector if software HDR is
  // disabled or no distinct knee-point exposures exist.
  absl::StatusOr<std::vector<absl::Duration>> GetHdrExposureTimes() const;
  // Acquires a single LDR frame within `timeout`. When `expected_exposure_time`
  // is provided, avoids querying `ExposureTime` from the camera and, in
  // free-running mode, retries until the acquired frame's exposure time
  // matches `expected_exposure_time` (via `ChunkExposureTime` or the 2-frame
  // fallback).
  absl::StatusOr<CaptureResult> GetLdrCaptureResult(
      absl::Duration timeout,
      std::optional<absl::Duration> expected_exposure_time = std::nullopt);
  absl::StatusOr<std::vector<CaptureResult>> GetLdrCaptureResults(
      absl::Duration timeout);

  // Processes a newly captured frame during streaming, advancing the HDR
  // exposure bracket and updating the camera's ExposureTime when streaming HDR
  // is active. Returns `std::nullopt` if the stream has stopped, the frame's
  // exposure time does not match the current bracket step, or the frame was
  // buffered as an intermediate LDR bracket frame. Returns a populated
  // `CaptureResult` containing the fused HDR image once all bracket exposures
  // are collected, or the unmodified `capture_result` when streaming HDR is
  // inactive.
  absl::StatusOr<std::optional<CaptureResult>> ProcessStreamingHdrFrame(
      CaptureResult capture_result,
      std::optional<absl::Duration> received_exposure, int64_t frame_id)
      ABSL_LOCKS_EXCLUDED(mutex_, camera_mutex_);

  // Retries the passed function until write permission is granted or the device
  // is ready again (after a timeout or being busy). Waiting for permission is
  // in particular useful during camera discovery, which internally blocks all
  // available cameras. So a process executing a write command may not have
  // write permissions during discovery of another process.
  absl::Status RetryOnPermissionDeniedOrTimeoutOrBusy(
      const std::function<absl::Status()>& fn);

  static absl::Status ConfigureComponents(GenicamImageSource& aravis_camera);
  static absl::Status ConfigureCameraDefaults(
      GenicamImageSource& aravis_camera);

  // Returns the minimal timeout we need to wait in the innermost driver loop
  // to guarantee that we have a chance of successfully acquiring an image
  // frame.
  absl::Duration MimimalAcquisitionTimeout(absl::Duration exposure_time) const;

  absl::StatusOr<CaptureResult> CaptureImpl(absl::Duration timeout) override;

  // Reads the access to a camera setting.
  absl::StatusOr<CameraSettingAccess> ReadCameraSettingAccessImpl(
      absl::string_view name) const override
      ABSL_LOCKS_EXCLUDED(mutex_, camera_mutex_);

  // The function reads camera setting properties and returns them.
  // An error is only raised when settings for an unknown property are
  // requested. If some properties cannot be queried for a specific setting
  // these are not populated in the result.
  absl::StatusOr<CameraSettingProperties> ReadCameraSettingPropertiesImpl(
      absl::string_view name) const override
      ABSL_LOCKS_EXCLUDED(mutex_, camera_mutex_);

  // Reads and returns the currently configured camera settings. The provided
  // name must refer to a feature listed in the Standard Feature Naming
  // Convention (SFNC) from the GenICam standard.
  // The function raises an error when either the feature name is unknown or
  // when the current value of the feature cannot be requested.
  absl::StatusOr<CameraSetting> ReadCameraSettingImpl(absl::string_view name)
      const override ABSL_LOCKS_EXCLUDED(mutex_, camera_mutex_);

  // Retries to set the specified camera setting by `UpdateCameraSettingOnce`
  // until write permission is granted.
  absl::Status UpdateCameraSettingImpl(
      const CameraSetting& camera_setting) override;

  // Internal helper to update a camera setting during stream startup/teardown
  // while `streaming_state_` is populated.
  absl::Status UpdateCameraSettingInternal(const CameraSetting& camera_setting);

  // Sets a value of the specified camera setting.
  // The name in the camera setting specifies the exact feature which is updated
  // and it must refer to a feature listed in the Standard Feature Naming
  // Convention (SFNC) from the GenICam standard.
  absl::Status UpdateCameraSettingOnce(const CameraSetting& camera_setting);

  // Performs the actual setting update while the acquisition stream is paused.
  absl::Status UpdateCameraSettingOnceWithPausedStream(
      const CameraSetting& camera_setting);

  absl::StatusOr<std::vector<SensorInformation>> DescribeCameraSensorsImpl()
      const override;

  absl::StatusOr<CaptureResultWithExposureTime> GetCaptureResultAsync(
      absl::Time deadline) ABSL_LOCKS_EXCLUDED(mutex_);

  // Creates a stream w/ buffers and starts the camera acquisition.
  // Post-condition: stream_ != nullptr.
  absl::Status StartAsyncAcquisition();

  // Stops the camera acquisition while keeping the current stream and its
  // buffers allocated.
  absl::Status StopAsyncAcquisition();

  // Temporarily stops a stream to quickly change camera parameters.
  // virtual for unit testing.
  virtual absl::Status PauseStream();

  // Restarts a stream after it has been stopped with PauseStream.
  // virtual for unit testing.
  virtual absl::Status ResumeStream();

  // Called from the Aravis capture thread when a new buffer is available.
  static void OnNewBuffer(ArvStream* arv_stream, void* data);
  static void OnControlLost(ArvDevice* arv_device, void* data);

  GObjectPtr<ArvCamera> camera_;
  GObjectPtr<ArvStream> stream_;
  BufferHelper buffer_helper_;

  mutable absl::Mutex mutex_;
  mutable absl::Mutex camera_mutex_ ABSL_ACQUIRED_AFTER(mutex_);

  std::string device_id_;

  // This flag indicates whether software triggering is enabled.
  // Software triggering is enabled by default unless this is overridden by the
  // user in the camera config. Triggering in general (including software
  // triggering) can be disabled and switched to free running mode by setting
  // 'TriggerMode = Off'.
  bool use_software_trigger_ = true;

  std::optional<StreamingState> streaming_state_ ABSL_GUARDED_BY(mutex_);

  // Stores the id of the lost control callback handler.
  uint32_t lost_control_handler_id_ = 0;

  // Tracks the acquisition state.
  struct AcquisitionState {
    bool is_started = false;
    int payload_size = 0;
  };
  AcquisitionState acquisition_state_;

  absl::flat_hash_set<int64_t> all_sensor_ids_;

  absl::flat_hash_map<int64_t, std::string> component_selector_by_sensor_id_;
  absl::flat_hash_map<int64_t, CameraParams>
      factory_camera_params_by_sensor_id_;
  absl::flat_hash_map<int64_t, Pose> camera_t_sensor_by_sensor_id_;
  double distance_scale_ = 1.0;

  // Tracks the current state, which needs to be locked because it is used in
  // callbacks.
  struct LockableState {
    absl::flat_hash_map<int64_t, Dimensions> dimensions_by_sensor_id;
    absl::flat_hash_map<int64_t, ArvPixelFormat> pixel_format_by_sensor_id;
    absl::flat_hash_map<int64_t, bool> disabled_by_sensor_id;
    int packet_size = 0;
    bool lost_control = false;
  };
  LockableState state_ ABSL_GUARDED_BY(mutex_);

  std::optional<stats::ScopedSpan> acquisition_span_ ABSL_GUARDED_BY(mutex_);

  // The capture result will be populated by the receiving thread in OnNewBuffer
  // and it will then be moved to the client. The variable is used during
  // asynchronous acquisition to transport image data from the receiving thread
  // to the camera thread.
  CaptureResultWithExposureTime capture_result_ ABSL_GUARDED_BY(mutex_);

  // This boolean variable is used to signal the camera that a new buffer has
  // been handled. The buffer handling may still have gone wrong and the caller
  // must check the 'status_' member to validate success.
  bool new_buffer_handled_ ABSL_GUARDED_BY(mutex_) = false;

  // The status is used to carry over any error messages from the receiving
  // thread in OnNewBuffer to the camera.
  absl::Status status_ ABSL_GUARDED_BY(mutex_);

  // The point in time at which the Aravis driver invoked the OnNewBuffer()
  // callback. At this time, Aravis has received and assembled the frame. The
  // data needs to be still converted to an RGB image and the image still needs
  // to be undistorted.
  absl::Time buffer_callback_time_;

  // Exposure time properties.
  std::optional<CameraSettingProperties::Float> exposure_time_props_;

  // Gain properties.
  std::optional<CameraSettingProperties::Float> gain_props_;

  // All settings and functionality required to create HDR images.
  std::optional<SoftwareHdr> software_hdr_;

  struct AcquisitionMeasurement {
    absl::Duration acquisition_time;
    int num_retries = 0;
  };
  std::map<absl::Duration, AcquisitionMeasurement>
      acquisition_measurements_per_exposure_time_;
};

}  // namespace intrinsic::perception

#endif  // INTRINSIC_PERCEPTION_CAMERAS_GENICAM_IMAGE_SOURCE_H_
