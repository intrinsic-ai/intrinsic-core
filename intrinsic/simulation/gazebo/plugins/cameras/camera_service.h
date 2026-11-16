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

#ifndef INTRINSIC_SIMULATION_GAZEBO_PLUGINS_CAMERAS_CAMERA_SERVICE_H_
#define INTRINSIC_SIMULATION_GAZEBO_PLUGINS_CAMERAS_CAMERA_SERVICE_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "grpcpp/server_context.h"
#include "grpcpp/support/status.h"
#include "grpcpp/support/sync_stream.h"
#include "intrinsic/perception/cameras/camera_setting.h"
#include "intrinsic/perception/cameras/camera_setting_properties.h"
#include "intrinsic/perception/cameras/image_source_interface.h"
#include "intrinsic/perception/cameras/sensor_image.h"
#include "intrinsic/perception/core/pixel_type.h"
#include "intrinsic/perception/core/single_thread_executor.h"
#include "intrinsic/perception/proto/v1/camera_config.pb.h"
#include "intrinsic/perception/proto/v1/camera_service.grpc.pb.h"
#include "intrinsic/perception/proto/v1/camera_service.pb.h"
#include "intrinsic/platform/pubsub/publisher.h"
#include "intrinsic/platform/pubsub/pubsub.h"
#include "intrinsic/util/thread/stop_token.h"
#include "intrinsic/util/thread/thread.h"

namespace intrinsic {
namespace perception {

// The camera connection below will activate the camera in Gazebo by switching
// to a high frame rate.
inline constexpr double kGazeboCameraActiveUpdateRate = 1000.0;

// Instead of fully deactivating the camera the update rate is reduced to an
// amount that doesn't eat up too much computation time.
inline constexpr double kGazeboCameraDeactiveUpdateRate = 0.1;

// See https://www.emva.org/wp-content/uploads/GenICam_SFNC_v2_7.pdf#page=129
// for the ids.
inline constexpr int64_t kIntensitySensorId = 1;
inline constexpr int64_t kRangeSensorId = 4;
inline constexpr std::string_view kIntensitySensorName = "Intensity";
inline constexpr std::string_view kRangeSensorName = "Range";
inline constexpr std::string_view kNormalSensorName = "Normal";
inline constexpr std::string_view SensorDisplayNameByPixelType(
    perception::PixelType pixel_type) {
  switch (pixel_type) {
    case perception::PixelType::kIntensity:
      return kIntensitySensorName;
    case perception::PixelType::kDepth:
      [[fallthrough]];
    case perception::PixelType::kPoint:
      return kRangeSensorName;
    case perception::PixelType::kNormal:
      return kNormalSensorName;
    default:
      return "Undefined";
  }
}

// Defines the connection between the GazeboCameraGrpcService and Gazebo
// plugins. It synchronizes the frame transfer, manual triggering and holds
// configuration information about the camera.
class GazeboCameraConnection {
 public:
  // Function to activate / deactivate sensor[s]. The first argument
  // sets the active state. The 2nd argument is a list of sensors to
  // [de]activate - an empty set means activate all sensors. The function
  // returns true if the operation is successful.
  using SetActiveFunction =
      std::function<bool(bool, const absl::flat_hash_set<int64_t>&)>;
  GazeboCameraConnection(
      const intrinsic_proto::perception::v1::DescribeCameraResponse&
          camera_description,
      const intrinsic_proto::perception::v1::CameraConfig& camera_config,
      const SetActiveFunction& set_active_function);

  ~GazeboCameraConnection();

  // The service will call this method after successful registration to
  // allow the connection to unregister itself on destruction.
  using UnregisterFunction =
      std::function<absl::Status(absl::string_view handle)>;
  void SetUnregisterFunction(const UnregisterFunction& unregister_function);

  // The service will call this method to get new sensor images. Blocking.
  // Note that images can sometimes take extremely long to render in Gazebo. So
  // the default timeout is set quite high.
  absl::StatusOr<std::vector<intrinsic::perception::SensorImage>>
  WaitForNewSensorImages(const absl::flat_hash_set<int64_t>& sensor_ids,
                         std::optional<StopToken> stop_token = std::nullopt,
                         absl::Duration timeout = absl::Seconds(120))
      ABSL_LOCKS_EXCLUDED(mutex_, request_mutex_);

  // The plugin will call this method to update the current sensors images which
  // are then picked up by the WaitForNewSensorImages() call.
  void SetCurrentSensorImages(std::vector<SensorImage> sensor_images)
      ABSL_LOCKS_EXCLUDED(mutex_);

  const intrinsic_proto::perception::v1::DescribeCameraResponse&
  camera_description() const;

  const intrinsic_proto::perception::v1::CameraConfig& camera_config() const;

  // Returns a hardware unique identifier for a specific camera.
  const std::string& camera_handle() const;

  absl::StatusOr<CameraSettingProperties> ReadCameraSettingProperties(
      absl::string_view name) const ABSL_LOCKS_EXCLUDED(mutex_);

  absl::StatusOr<CameraSetting> ReadCameraSetting(absl::string_view name) const
      ABSL_LOCKS_EXCLUDED(mutex_);

  absl::Status UpdateCameraSetting(const CameraSetting& camera_setting)
      ABSL_LOCKS_EXCLUDED(mutex_);

  // Add a callback that will receive new capture results while a stream is
  // running.
  absl::StatusOr<ImageSourceInterface::CallbackToken> AddStreamCallback(
      ImageSourceInterface::CaptureCallback callback);

  // Remove a previously registered callback.
  void RemoveStreamCallback(ImageSourceInterface::CallbackToken token);

  // Starts a stream for the given sensor ids and returns the images back into
  // any registered callbacks. Only one stream is supported at a time.
  absl::Status StartStream(const absl::flat_hash_set<int64_t>& sensor_ids);

  // Stops any currently running streams, no-op if there is no stream running.
  absl::Status StopStream();

 private:
  UnregisterFunction unregister_function_;

  absl::Mutex request_mutex_;

  mutable absl::Mutex mutex_;
  std::vector<intrinsic::perception::SensorImage> last_sensor_images_
      ABSL_GUARDED_BY(mutex_);
  absl::flat_hash_map<std::string, CameraSetting> settings_
      ABSL_GUARDED_BY(mutex_);
  bool stopped_ ABSL_GUARDED_BY(mutex_) = false;

  intrinsic_proto::perception::v1::DescribeCameraResponse camera_description_;
  intrinsic_proto::perception::v1::CameraConfig camera_config_;
  SetActiveFunction set_active_function_;

  std::string camera_handle_;

  absl::Mutex callback_mutex_;
  absl::flat_hash_map<ImageSourceInterface::CallbackToken,
                      ImageSourceInterface::CaptureCallback>
      stream_callbacks_ ABSL_GUARDED_BY(callback_mutex_);

  absl::Mutex streaming_mutex_;
  std::optional<intrinsic::Thread> streaming_thread_
      ABSL_GUARDED_BY(streaming_mutex_);
};

// gRpc service emulating a CameraService to send out the image data.
class GazeboCameraGrpcService final
    : public intrinsic_proto::perception::v1::CameraService::Service {
 public:
  grpc::Status ListAvailableCameras(
      grpc::ServerContext* context,
      const intrinsic_proto::perception::v1::ListAvailableCamerasRequest*
          request,
      intrinsic_proto::perception::v1::ListAvailableCamerasResponse* response)
      override;

  grpc::Status Capture(
      grpc::ServerContext* context,
      const intrinsic_proto::perception::v1::CaptureRequest* request,
      intrinsic_proto::perception::v1::CaptureResponse* response) override;

  grpc::Status ReadCameraSettingProperties(
      grpc::ServerContext* context,
      const intrinsic_proto::perception::v1::ReadCameraSettingPropertiesRequest*
          request,
      intrinsic_proto::perception::v1::ReadCameraSettingPropertiesResponse*
          response) override ABSL_LOCKS_EXCLUDED(mutex_);

  grpc::Status ReadCameraSetting(
      grpc::ServerContext* context,
      const intrinsic_proto::perception::v1::ReadCameraSettingRequest* request,
      intrinsic_proto::perception::v1::ReadCameraSettingResponse* response)
      override ABSL_LOCKS_EXCLUDED(mutex_);

  grpc::Status UpdateCameraSetting(
      grpc::ServerContext* context,
      const intrinsic_proto::perception::v1::UpdateCameraSettingRequest*
          request,
      intrinsic_proto::perception::v1::UpdateCameraSettingResponse* response)
      override ABSL_LOCKS_EXCLUDED(mutex_);

  grpc::Status DescribeCamera(
      grpc::ServerContext* context,
      const intrinsic_proto::perception::v1::DescribeCameraRequest* request,
      intrinsic_proto::perception::v1::DescribeCameraResponse* response)
      override ABSL_LOCKS_EXCLUDED(mutex_);

  // Register a new camera. Plugins will register their connections with this
  // method.
  absl::Status RegisterCamera(GazeboCameraConnection* camera)
      ABSL_LOCKS_EXCLUDED(mutex_);

  // Unregisters a camera. This function will be automatically called during
  // the destruction of a GazeboCameraConnection.
  absl::Status UnRegisterCamera(absl::string_view camera_handle)
      ABSL_LOCKS_EXCLUDED(mutex_);

  ~GazeboCameraGrpcService() override;

 private:
  absl::StatusOr<GazeboCameraConnection*> GetCameraConnection(
      absl::string_view camera_handle) ABSL_LOCKS_EXCLUDED(mutex_);

  // Available cameras by handle.
  absl::flat_hash_map<std::string, GazeboCameraConnection*> cameras_
      ABSL_GUARDED_BY(mutex_);

  absl::Mutex mutex_;
  PubSub pubsub_;
  SingleThreadExecutor executor_;

};

}  // namespace perception
}  // namespace intrinsic

#endif  // INTRINSIC_SIMULATION_GAZEBO_PLUGINS_CAMERAS_CAMERA_SERVICE_H_
