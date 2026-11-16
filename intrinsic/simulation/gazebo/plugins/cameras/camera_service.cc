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

#include "intrinsic/simulation/gazebo/plugins/cameras/camera_service.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "absl/cleanup/cleanup.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/flags/flag.h"
#include "absl/functional/bind_front.h"
#include "absl/functional/overload.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "grpcpp/server.h"
#include "grpcpp/support/status.h"
#include "grpcpp/support/sync_stream.h"
#include "intrinsic/perception/cameras/camera_identifier.h"
#include "intrinsic/perception/cameras/camera_setting.h"
#include "intrinsic/perception/cameras/camera_setting_properties.h"
#include "intrinsic/perception/cameras/capture_result.h"
#include "intrinsic/perception/cameras/capture_result_helper.h"
#include "intrinsic/perception/cameras/genicam/feature_names.h"
#include "intrinsic/perception/cameras/image_source_interface.h"
#include "intrinsic/perception/cameras/sensor_image.h"
#include "intrinsic/perception/cameras/services/v1/camera_service_utils.h"
#include "intrinsic/perception/core/camera_params.h"
#include "intrinsic/perception/core/intrinsic_params.h"
#include "intrinsic/perception/core/undistortion.h"
#include "intrinsic/perception/logging/sensor_image.h"
#include "intrinsic/perception/proto/v1/camera_config.pb.h"
#include "intrinsic/perception/proto/v1/camera_identifier.pb.h"
#include "intrinsic/perception/proto/v1/camera_service.grpc.pb.h"
#include "intrinsic/perception/proto/v1/camera_service.pb.h"
#include "intrinsic/perception/proto_conversion/v1/camera_config.h"
#include "intrinsic/perception/proto_conversion/v1/camera_identifier.h"
#include "intrinsic/perception/proto_conversion/v1/camera_settings.h"
#include "intrinsic/perception/proto_conversion/v1/capture_result.h"
#include "intrinsic/perception/proto_conversion/v1/post_processing.h"
#include "intrinsic/platform/pubsub/publisher.h"
#include "intrinsic/platform/pubsub/pubsub.h"
#include "intrinsic/util/proto/pb_hash.h"
#include "intrinsic/util/status/ret_check.h"
#include "intrinsic/util/status/ret_check_grpc.h"
#include "intrinsic/util/status/status_builder.h"
#include "intrinsic/util/status/status_builder_grpc.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/status/status_macros_grpc.h"
#include "intrinsic/util/thread/stop_token.h"
#include "third_party/ros2/ros_interfaces/jazzy/builtin_interfaces/msg/time.pb.h"
#include "third_party/ros2/ros_interfaces/jazzy/sensor_msgs/msg/image.pb.h"
#include "third_party/ros2/ros_interfaces/jazzy/std_msgs/msg/header.pb.h"

namespace intrinsic {
namespace perception {

namespace {

absl::Status VerifyIntrinsicParams(
    const intrinsic_proto::perception::v1::CameraConfig&
        requested_camera_config,
    const intrinsic_proto::perception::v1::CameraConfig&
        matched_camera_config) {
  INTR_ASSIGN_OR_RETURN(
      (const auto [requested_config, _1]),
      intrinsic_proto::perception::v1::FromProto(requested_camera_config));
  INTR_ASSIGN_OR_RETURN(
      (const auto [matched_config, _2]),
      intrinsic_proto::perception::v1::FromProto(matched_camera_config));
  for (const auto& [requested_sensor_id, requested_sensor_params] :
       requested_config.camera_params_by_sensor_id) {
    // If only intrinsic parameters for a single sensor with the fallback id
    // are provided, we assume the simulated camera to have the same
    // intrinsic parameters for all sensors.
    if (requested_sensor_id == ImageSourceInterface::kFallbackSensorId) {
      for (const auto& [matched_sensor_id, matched_sensor_params] :
           matched_config.camera_params_by_sensor_id) {
        INTR_RETURN_IF_ERROR(
            IntrinsicParamsNear(requested_sensor_params.intrinsic_params,
                                matched_sensor_params.intrinsic_params, 1e-5))
                .SetCode(absl::StatusCode::kInvalidArgument)
            << "Requested intrinsic parameters do not match the intrinsic "
               "parameters which are used in simulation."
            << "\nRequested (sensor id " << requested_sensor_id << "):\n"
            << requested_sensor_params.intrinsic_params
            << "\nSimulation (sensor id " << matched_sensor_id << "):\n"
            << matched_sensor_params.intrinsic_params;
      }
    } else {
      const auto matched_it =
          matched_config.camera_params_by_sensor_id.find(requested_sensor_id);
      if (matched_it == matched_config.camera_params_by_sensor_id.end()) {
        LOG(WARNING) << "Requested sensor id " << requested_sensor_id
                     << " is not present in the camera config which is used in "
                        "simulation.";
        continue;
      }
      INTR_RETURN_IF_ERROR(
          IntrinsicParamsNear(requested_sensor_params.intrinsic_params,
                              matched_it->second.intrinsic_params, 1e-5))
              .SetCode(absl::StatusCode::kInvalidArgument)
          << "Requested intrinsic parameters do not match the intrinsic "
             "parameters which are used in simulation."
          << "\nRequested (sensor id " << requested_sensor_id << "):\n"
          << requested_sensor_params.intrinsic_params
          << "\nSimulation (sensor id " << matched_it->first << "):\n"
          << matched_it->second.intrinsic_params;
    }
  }
  return absl::OkStatus();
}

bool IsDimensionSetting(absl::string_view name) {
  return name == genicam::kWidth || name == genicam::kHeight ||
         name == genicam::kSensorWidth || name == genicam::kSensorHeight;
}

absl::StatusOr<int64_t> GetDimensionSettingValue(
    absl::string_view name,
    const intrinsic_proto::perception::v1::CameraConfig& camera_config) {
  INTR_RET_CHECK(!camera_config.sensor_configs().empty());
  // TODO(feuer): Add support for selecting a sensor.
  const auto& dimensions = camera_config.sensor_configs(0)
                               .camera_params()
                               .intrinsic_params()
                               .dimensions();
  return (name == genicam::kWidth || name == genicam::kSensorWidth)
             ? dimensions.cols()
             : dimensions.rows();
}

}  // namespace

GazeboCameraConnection::GazeboCameraConnection(
    const intrinsic_proto::perception::v1::DescribeCameraResponse&
        camera_description,
    const intrinsic_proto::perception::v1::CameraConfig& camera_config,
    const SetActiveFunction& set_active_function)
    : camera_description_(camera_description),
      camera_config_(camera_config),
      set_active_function_(set_active_function) {
  set_active_function(false, {});

  camera_handle_ = CanonicalString(
      intrinsic_proto::perception::v1::FromProto(camera_config_.identifier()));

  for (const auto& proto_setting : camera_config_.camera_settings()) {
    if (!proto_setting.name().empty() &&
        !IsDimensionSetting(proto_setting.name())) {
      CameraSetting setting =
          intrinsic_proto::perception::v1::FromProto(proto_setting);
      if (!std::holds_alternative<std::monostate>(setting.value)) {
        settings_[proto_setting.name()] = std::move(setting);
      }
    }
  }
}

void GazeboCameraConnection::SetUnregisterFunction(
    const UnregisterFunction& unregister_function) {
  unregister_function_ = unregister_function;
}

GazeboCameraConnection::~GazeboCameraConnection() {
  if (unregister_function_) {
    if (auto status = unregister_function_(camera_handle()); !status.ok()) {
      LOG(ERROR) << absl::StrFormat(
          "Failed to remove camera with handle '%s' from service. Error: %s",
          camera_handle(), status.message());
    }
  }
  {
    // Lock data mutex and set stopped_ true to signal to threads waiting on
    // new data that the connection has been stopped.
    absl::MutexLock lock(mutex_);
    stopped_ = true;
  }

  // Ensure we stop the streaming thread if we had one.
  if (auto status = StopStream(); !status.ok()) {
    LOG(WARNING) << "Error while trying to stop streaming thread: " << status;
  }

  // Lock request mutex to ensure all pending requests are finished before
  // destroying the connection.
  absl::MutexLock request_lock(request_mutex_);
}

absl::StatusOr<std::vector<intrinsic::perception::SensorImage>>
GazeboCameraConnection::WaitForNewSensorImages(
    const absl::flat_hash_set<int64_t>& sensor_ids,
    std::optional<StopToken> stop_token, absl::Duration timeout) {
  // We allow only one thread at a time to query sensor images since we don't
  // expect many clients to pull images in parallel. If that assumption changes
  // we have to reiterate on this approach.
  absl::MutexLock lock(request_mutex_);

  // A real camera may support more sensor ids than a simulated one. Discard the
  // sensor ids that are not supported by the simulated camera.
  absl::flat_hash_set<int64_t> available_sensor_ids;
  available_sensor_ids.reserve(camera_description_.sensors_size());
  for (const auto& sensor_information : camera_description_.sensors()) {
    available_sensor_ids.insert(sensor_information.id());
  }
  absl::flat_hash_set<int64_t> filtered_sensor_ids;
  for (const int64_t sensor_id : sensor_ids) {
    if (available_sensor_ids.contains(sensor_id)) {
      filtered_sensor_ids.insert(sensor_id);
    } else {
      LOG(WARNING) << "Requested sensor id " << sensor_id
                   << " is not present in the simulated camera description.";
    }
  }

  // Activate the sensor and make sure we deactivate immediately after we
  // receive sensor images.
  const absl::Time start_time = absl::Now();
  if (!set_active_function_(true, filtered_sensor_ids)) {
    return absl::UnavailableError("Unable to activate sensors");
  }
  auto deactivate_deferrer =
      absl::MakeCleanup([this]() { set_active_function_(false, {}); });

  absl::MutexLock data_lock(mutex_);
  auto sensor_images_are_ready_or_stopped = [this, start_time,
                                             &filtered_sensor_ids] {
    mutex_.AssertReaderHeld();

    if (stopped_) {
      return true;
    }

    if (last_sensor_images_.empty()) {
      return false;
    }

    for (const auto& sensor_image : last_sensor_images_) {
      if ((filtered_sensor_ids.empty() ||
           filtered_sensor_ids.contains(sensor_image.sensor_id())) &&
          sensor_image.acquisition_time() < start_time) {
        return false;
      }
    }
    return true;
  };
  absl::Condition cond(&sensor_images_are_ready_or_stopped);
  absl::Time deadline = absl::Now() + timeout;
  while (absl::Now() < deadline) {
    if (stop_token.has_value() && stop_token->stop_requested()) {
      return absl::CancelledError(
          "Cancelled while waiting for new sensor images.");
    }
    if (mutex_.AwaitWithTimeout(cond, absl::Milliseconds(100))) {
      if (stopped_) {
        return absl::AbortedError(
            "Camera connection stopped, cannot get new sensor images.");
      }

      std::vector<intrinsic::perception::SensorImage> sensor_images;
      std::swap(sensor_images, last_sensor_images_);

      // If we have sensor ids list we should only return sensor images for
      // those ids.
      if (!filtered_sensor_ids.empty()) {
        std::erase_if(sensor_images, [&filtered_sensor_ids](
                                         const SensorImage& sensor_image) {
          return !filtered_sensor_ids.contains(sensor_image.sensor_id());
        });
      }

      return sensor_images;
    }
  }
  return absl::DeadlineExceededError("No new sensors images available");
}

void GazeboCameraConnection::SetCurrentSensorImages(
    std::vector<SensorImage> sensor_images) {
  absl::MutexLock lock(mutex_);
  std::swap(last_sensor_images_, sensor_images);
}

const intrinsic_proto::perception::v1::DescribeCameraResponse&
GazeboCameraConnection::camera_description() const {
  return camera_description_;
}

const intrinsic_proto::perception::v1::CameraConfig&
GazeboCameraConnection::camera_config() const {
  return camera_config_;
}

const std::string& GazeboCameraConnection::camera_handle() const {
  return camera_handle_;
}

absl::StatusOr<CameraSettingProperties>
GazeboCameraConnection::ReadCameraSettingProperties(
    absl::string_view name) const {
  if (IsDimensionSetting(name)) {
    INTR_ASSIGN_OR_RETURN(const int64_t value,
                          GetDimensionSettingValue(name, camera_config_));
    return CameraSettingProperties{
        .name = std::string(name),
        .properties =
            CameraSettingProperties::Integer{
                .range =
                    CameraSettingProperties::Integer::Range{
                        .minimum = value,
                        .maximum = value,
                    },
                .increment = 1,
            },
    };
  }
  absl::MutexLock lock(mutex_);
  const auto it = settings_.find(name);
  if (it == settings_.end()) {
    return absl::NotFoundError(absl::StrFormat(
        "The parameter '%s' is not supported by a camera in simulation.",
        name));
  }
  return CameraSettingProperties{
      .name = std::string(name),
      .properties = std::visit(
          absl::Overload{
              [](const CameraSetting::Integer&)
                  -> CameraSettingProperties::Properties {
                return CameraSettingProperties::Integer{.increment = 1};
              },
              [](const CameraSetting::Float&)
                  -> CameraSettingProperties::Properties {
                return CameraSettingProperties::Float{};
              },
              [](const CameraSetting::Enumeration& val)
                  -> CameraSettingProperties::Properties {
                return CameraSettingProperties::Enumeration{
                    .values = {val.value}};
              },
              [](const auto&) -> CameraSettingProperties::Properties {
                return std::monostate{};
              },
          },
          it->second.value),
  };
}

absl::StatusOr<CameraSetting> GazeboCameraConnection::ReadCameraSetting(
    absl::string_view name) const {
  if (IsDimensionSetting(name)) {
    INTR_ASSIGN_OR_RETURN(const int64_t value,
                          GetDimensionSettingValue(name, camera_config_));
    return CameraSetting{
        .name = std::string(name),
        .value = CameraSetting::Integer{.value = value},
    };
  }
  absl::MutexLock lock(mutex_);
  const auto it = settings_.find(name);
  if (it == settings_.end()) {
    return absl::NotFoundError(absl::StrFormat(
        "The parameter '%s' is not supported by a camera in simulation.",
        name));
  }
  return it->second;
}

absl::Status GazeboCameraConnection::UpdateCameraSetting(
    const CameraSetting& camera_setting) {
  if (camera_setting.name.empty()) {
    return absl::InvalidArgumentError("Camera setting 'name' cannot be empty.");
  }
  if (IsDimensionSetting(camera_setting.name)) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "Cannot write read-only parameter '%s'.", camera_setting.name));
  }
  if (std::holds_alternative<std::monostate>(camera_setting.value)) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "Camera setting '%s' must specify a value.", camera_setting.name));
  }
  absl::MutexLock lock(mutex_);
  settings_[camera_setting.name] = camera_setting;
  return absl::OkStatus();
}

absl::StatusOr<ImageSourceInterface::CallbackToken>
GazeboCameraConnection::AddStreamCallback(
    ImageSourceInterface::CaptureCallback callback) {
  absl::MutexLock lock(callback_mutex_);
  static std::atomic<int> global_id = 0;
  ImageSourceInterface::CallbackToken callback_token(
      absl::StrCat(absl::Now(), global_id++));
  DCHECK(!stream_callbacks_.contains(callback_token))
      << "Callback with id " << callback_token << " already exists.";
  stream_callbacks_[callback_token] = std::move(callback);
  return callback_token;
}

void GazeboCameraConnection::RemoveStreamCallback(
    ImageSourceInterface::CallbackToken token) {
  absl::MutexLock lock(callback_mutex_);
  stream_callbacks_.erase(token);
}

absl::Status GazeboCameraConnection::StartStream(
    const absl::flat_hash_set<int64_t>& sensor_ids) {
  absl::MutexLock lock(streaming_mutex_);
  if (streaming_thread_.has_value()) {
    return absl::FailedPreconditionError("Stream already started.");
  }

  streaming_thread_.emplace([this,
                             sensor_ids](intrinsic::StopToken stop_token) {
    LOG(INFO) << "GazeboCameraConnection::Thread: Starting streaming thread.";

    while (!stop_token.stop_requested()) {
      auto sensor_images = WaitForNewSensorImages(sensor_ids, stop_token);
      if (!sensor_images.ok()) {
        LOG(WARNING) << "Failed to capture image: " << sensor_images.status();
        absl::SleepFor(absl::Milliseconds(100));
        continue;
      }

      absl::Time capture_at;
      if (!sensor_images->empty()) {
        capture_at = sensor_images->at(0).acquisition_time();
      }

      CaptureResult capture_result = {
          .capture_at = capture_at,
          .sensor_images = std::move(sensor_images).value(),
      };

      absl::MutexLock lock(callback_mutex_);
      for (auto& [_, callback] : stream_callbacks_) {
        if (absl::Status status = callback(capture_result); !status.ok()) {
          LOG(ERROR) << "Callback returned error status: " << status;
        }
      }
    }
  });

  return absl::OkStatus();
}

absl::Status GazeboCameraConnection::StopStream() {
  absl::MutexLock lock(streaming_mutex_);
  if (streaming_thread_.has_value()) {
    // This will deallocate the streaming thread and that will request a stop on
    // the stop token, which will then exit, and join before completing the
    // destructor call.
    streaming_thread_.reset();
  }
  return absl::OkStatus();
}

grpc::Status GazeboCameraGrpcService::ListAvailableCameras(
    grpc::ServerContext* context,
    const intrinsic_proto::perception::v1::ListAvailableCamerasRequest* request,
    intrinsic_proto::perception::v1::ListAvailableCamerasResponse* response) {
  LOG(INFO) << "GazeboCameraGrpcService::ListAvailableCameras(...)";
  absl::MutexLock lock(mutex_);
  for (const auto& camera : cameras_) {
    *response->add_camera_identifiers() =
        camera.second->camera_config().identifier();
  }
  return grpc::Status::OK;
}

grpc::Status GazeboCameraGrpcService::Capture(
    grpc::ServerContext* context,
    const intrinsic_proto::perception::v1::CaptureRequest* request,
    intrinsic_proto::perception::v1::CaptureResponse* response) {
  if (!request->camera_config().has_identifier()) {
    return InvalidArgumentErrorBuilderGrpc()
           << "Capture() requires a config with a camera identifier.";
  }

  const std::string camera_handle =
      CanonicalString(intrinsic_proto::perception::v1::FromProto(
          request->camera_config().identifier()));

  INTR_ASSIGN_OR_RETURN_GRPC(GazeboCameraConnection * camera,
                             GetCameraConnection(camera_handle), _.LogError());

  // Check that the intrinsic parameters in the requested camera config
  // match the ones in the found config.
  INTR_RETURN_IF_ERROR_GRPC(
      VerifyIntrinsicParams(request->camera_config(), camera->camera_config()));

  const auto start_capture = absl::Now();
  const absl::flat_hash_set<int64_t> sensor_ids(request->sensor_ids().begin(),
                                                request->sensor_ids().end());

  INTR_ASSIGN_OR_RETURN_GRPC(auto sensor_images,
                             camera->WaitForNewSensorImages(sensor_ids),
                             _.LogError());
  LOG(INFO) << "GazeboCameraGrpcService::Capture(...) received sensor images: "
            << absl::Now() - start_capture;

  absl::Time capture_at;
  if (!sensor_images.empty()) {
    capture_at = sensor_images[0].acquisition_time();
  }

  CaptureResult capture_result = {.capture_at = capture_at,
                                  .sensor_images = std::move(sensor_images)};
  // Simulated images are already undistorted, so post-processing is not
  // modifying the undistortion map and hence it is cheap to re-create an
  // empty map. If we ever add support for distorted images, we need to make
  // the undistortion map a member.
  UndistortionBySensorId undistortion_by_sensor_id;
  INTR_ASSIGN_OR_RETURN_GRPC(
      capture_result,
      PostProcessCaptureResult(std::move(capture_result),
                               intrinsic_proto::perception::v1::FromProto(
                                   request->post_processing_by_sensor_id())
                                   .first,
                               undistortion_by_sensor_id));
  if (request->has_context()) {
    INTR_RETURN_IF_ERROR_GRPC(
        LogSensorImages(executor_, capture_result.sensor_images,
                        absl::StrCat(kCameraServiceLoggerPrefix, camera_handle),
                        request->context())
            .status());
  }
  INTR_RETURN_IF_ERROR_GRPC(EncodeAndStoreCaptureResult(
      std::move(capture_result), pubsub_, *request, *response));

  return grpc::Status::OK;
}

grpc::Status GazeboCameraGrpcService::ReadCameraSettingProperties(
    grpc::ServerContext* context,
    const intrinsic_proto::perception::v1::ReadCameraSettingPropertiesRequest*
        request,
    intrinsic_proto::perception::v1::ReadCameraSettingPropertiesResponse*
        response) {
  if (!request->has_camera_identifier()) {
    return InvalidArgumentErrorBuilderGrpc()
           << "ReadCameraSettingProperties() requires a camera identifier.";
  }
  const std::string camera_handle = CanonicalString(
      intrinsic_proto::perception::v1::FromProto(request->camera_identifier()));
  INTR_ASSIGN_OR_RETURN_GRPC(GazeboCameraConnection * camera,
                             GetCameraConnection(camera_handle), _.LogError());
  INTR_ASSIGN_OR_RETURN_GRPC(
      const CameraSettingProperties properties,
      camera->ReadCameraSettingProperties(request->name()), _.LogError());
  *response->mutable_properties() =
      intrinsic_proto::perception::v1::ToProto(properties);
  LOG(INFO) << "Read camera setting properties: " << response->properties();
  return grpc::Status::OK;
}

grpc::Status GazeboCameraGrpcService::ReadCameraSetting(
    grpc::ServerContext* context,
    const intrinsic_proto::perception::v1::ReadCameraSettingRequest* request,
    intrinsic_proto::perception::v1::ReadCameraSettingResponse* response) {
  if (!request->has_camera_identifier()) {
    return InvalidArgumentErrorBuilderGrpc()
           << "ReadCameraSetting() requires a camera identifier.";
  }
  const std::string camera_handle = CanonicalString(
      intrinsic_proto::perception::v1::FromProto(request->camera_identifier()));
  INTR_ASSIGN_OR_RETURN_GRPC(const GazeboCameraConnection* camera,
                             GetCameraConnection(camera_handle), _.LogError());
  INTR_ASSIGN_OR_RETURN_GRPC(const CameraSetting setting,
                             camera->ReadCameraSetting(request->name()),
                             _.LogError());
  *response->mutable_setting() =
      intrinsic_proto::perception::v1::ToProto(setting);
  LOG(INFO) << "Read camera settings: " << response->setting();
  return grpc::Status::OK;
}

grpc::Status GazeboCameraGrpcService::UpdateCameraSetting(
    grpc::ServerContext* context,
    const intrinsic_proto::perception::v1::UpdateCameraSettingRequest* request,
    intrinsic_proto::perception::v1::UpdateCameraSettingResponse* response) {
  if (!request->has_camera_identifier()) {
    return InvalidArgumentErrorBuilderGrpc()
           << "UpdateCameraSetting() requires a camera identifier.";
  }
  const std::string camera_handle = CanonicalString(
      intrinsic_proto::perception::v1::FromProto(request->camera_identifier()));
  INTR_ASSIGN_OR_RETURN_GRPC(GazeboCameraConnection * camera,
                             GetCameraConnection(camera_handle), _.LogError());
  INTR_RETURN_IF_ERROR_GRPC(
      camera->UpdateCameraSetting(
          intrinsic_proto::perception::v1::FromProto(request->setting())))
      .LogError();
  return grpc::Status::OK;
}

grpc::Status GazeboCameraGrpcService::DescribeCamera(
    grpc::ServerContext* context,
    const intrinsic_proto::perception::v1::DescribeCameraRequest* request,
    intrinsic_proto::perception::v1::DescribeCameraResponse* response) {
  if (!request->has_camera_identifier()) {
    return InvalidArgumentErrorBuilderGrpc()
           << "DescribeCamera() requires a camera identifier.";
  }
  const std::string camera_handle = CanonicalString(
      intrinsic_proto::perception::v1::FromProto(request->camera_identifier()));
  INTR_ASSIGN_OR_RETURN_GRPC(const GazeboCameraConnection* camera,
                             GetCameraConnection(camera_handle), _.LogError());
  *response = camera->camera_description();
  return grpc::Status::OK;
}

absl::Status GazeboCameraGrpcService::RegisterCamera(
    GazeboCameraConnection* camera) {
  absl::MutexLock lock(mutex_);
  LOG(INFO) << "Registering Gazebo camera with identifier:\n"
            << camera->camera_config().identifier();
  if (!camera->camera_config().has_identifier()) {
    return absl::InvalidArgumentError(
        "Cannot register camera on Gazebo server without a camera identifier.");
  }
  const auto& camera_handle = camera->camera_handle();
  if (cameras_.contains(camera_handle)) {
    return absl::InvalidArgumentError(
        "A camera with the provided identifier has already been registered. "
        "Verify that the same camera (with the same identifier) is not being "
        "added twice in the simulation.");
  }
  cameras_[camera_handle] = camera;
  camera->SetUnregisterFunction(
      absl::bind_front(&GazeboCameraGrpcService::UnRegisterCamera, this));
  return absl::OkStatus();
}

absl::Status GazeboCameraGrpcService::UnRegisterCamera(
    absl::string_view camera_handle) {
  absl::MutexLock lock(mutex_);
  if (cameras_.erase(camera_handle) == 0) {
    return absl::NotFoundError(
        "A camera with the provided identifier has not been found.");
  }
  return absl::OkStatus();
}

absl::StatusOr<GazeboCameraConnection*>
GazeboCameraGrpcService::GetCameraConnection(absl::string_view camera_handle) {
  absl::MutexLock lock(mutex_);
  const auto iter = cameras_.find(camera_handle);
  if (iter == cameras_.end()) {
    return intrinsic::NotFoundErrorBuilder()
           << "Couldn't find camera with handle: " << camera_handle;
  }
  return iter->second;
}

GazeboCameraGrpcService::~GazeboCameraGrpcService() {
  absl::MutexLock lock(mutex_);
  for (auto& [handle, camera] : cameras_) {
    camera->SetUnregisterFunction(GazeboCameraConnection::UnregisterFunction());
  }
}

}  // namespace perception
}  // namespace intrinsic
