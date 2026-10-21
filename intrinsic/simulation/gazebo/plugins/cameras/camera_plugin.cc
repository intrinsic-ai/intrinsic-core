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

#include "intrinsic/simulation/gazebo/plugins/cameras/camera_plugin.h"

#include <cstdint>
#include <ctime>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "boost/bimap.hpp"
#include "google/protobuf/timestamp.pb.h"
#include "gz/msgs/MessageTypes.hh"
#include "gz/sim/Entity.hh"
#include "gz/sim/EntityComponentManager.hh"
#include "gz/sim/EventManager.hh"
#include "gz/sim/components/Camera.hh"
#include "gz/sim/components/Name.hh"
#include "gz/sim/components/RgbdCamera.hh"
#include "intrinsic/math/proto/header.pb.h"
#include "intrinsic/math/proto_conversion.h"
#include "intrinsic/perception/cameras/image_source_interface.h"
#include "intrinsic/perception/cameras/sensor_image.h"
#include "intrinsic/perception/cameras/sensor_information.h"
#include "intrinsic/perception/core/compute_normals_least_sqr.h"
#include "intrinsic/perception/core/dimensions.h"
#include "intrinsic/perception/core/eigen_types.h"
#include "intrinsic/perception/core/image_traits.h"
#include "intrinsic/perception/core/intrinsic_params.h"
#include "intrinsic/perception/core/pixel_type.h"
#include "intrinsic/perception/proto/v1/camera_config.pb.h"
#include "intrinsic/perception/proto/v1/camera_service.pb.h"
#include "intrinsic/perception/proto_conversion/v1/camera_identifier.h"
#include "intrinsic/perception/proto_conversion/v1/camera_params.h"
#include "intrinsic/perception/proto_conversion/v1/camera_service.h"
#include "intrinsic/platform/pubsub/publisher.h"
#include "intrinsic/platform/pubsub/pubsub.h"
#include "intrinsic/simulation/gazebo/plugins/cameras/camera_service.h"
#include "intrinsic/simulation/gazebo/plugins/cameras/camera_services.h"
#include "intrinsic/simulation/gazebo/plugins/cameras/point_cloud_from_depth_buffer.h"
#include "intrinsic/simulation/gazebo/plugins/cameras/simulation_camera_utils.h"
#include "intrinsic/simulation/gazebo/plugins/cameras/triggered_camera_client.h"
#include "intrinsic/util/macros.h"
#include "intrinsic/util/status/ret_check.h"
#include "intrinsic/util/status/return.h"
#include "intrinsic/util/status/status_builder.h"
#include "intrinsic/util/status/status_macros.h"
#include "sdf/Element.hh"
#include "sdf/Sensor.hh"
#include "third_party/ros2/ros_interfaces/jazzy/sensor_msgs/msg/image.pb.h"
#include "third_party/ros2/ros_interfaces/jazzy/std_msgs/msg/header.pb.h"

ABSL_FLAG(bool, stream_simulated_images_to_pubsub, false,
          "Stream simulated cameras from Gazebo to Pubsub");

namespace intrinsic::simulation {

using ::intrinsic::perception::Depth32f;
using ::intrinsic::perception::Dimensions;
using ::intrinsic::perception::GazeboCameraConnection;
using ::intrinsic::perception::Image;
using ::intrinsic::perception::Point32f;
using ::intrinsic::perception::Rgb8u;

namespace {

constexpr float kNormalThreshold = 0.5f;
constexpr int kNormalRadius = 5;
constexpr int kNormalStep = 1;

absl::StatusOr<boost::bimap<perception::PixelType, int64_t>>
LoadSensorIdsFromPluginSdf(const std::shared_ptr<const sdf::Element>& sdf) {
  boost::bimap<perception::PixelType, int64_t> result;
  for (sdf::ElementConstPtr elem = sdf->FindElement("image"); elem;
       elem = elem->GetNextElement("image")) {
    int64_t id = -1;
    {
      // `sdf::Element::Get` does not support `int64_t`, so `static_cast` it
      // from `uint64_t`.
      std::pair<uint64_t, bool> get_result = elem->Get<uint64_t>("id", id);
      if (!get_result.second) {
        return absl::InvalidArgumentError(
            "Sensor id was not specified or was not valid.");
      }
      id = static_cast<int64_t>(get_result.first);
    }
    std::string type = elem->Get<std::string>("type");
    INTR_ASSIGN_OR_RETURN(
        perception::PixelType pixel_type,
        perception::PixelTypeFromSdfPluginImageType(type),
        _.With(intrinsic::ExtraMessage() << " for sensor id " << id));
    if (!result.left.insert({pixel_type, id}).second) {
      return InvalidArgumentErrorBuilder()
             << "Duplicate sensor id: " << id << " or image type '" << type
             << "' specified in the plugin SDF.";
    }
  }
  return result;
}

absl::StatusOr<CameraPlugin::PixelTypeToIdMap> LoadSensorIds(
    const std::shared_ptr<const sdf::Element>& sdf,
    sdf::SensorType sensor_type) {
  INTR_ASSIGN_OR_RETURN(auto bimap_sensor_ids_by_pixel_type,
                        LoadSensorIdsFromPluginSdf(sdf));

  // Fill in defaults for unspecified sensor ids.
  using PixelTypeImageTypePair =
      std::pair<perception::PixelType, std::string_view>;
  auto fill_default_sensor_id_if_needed =
      [&](PixelTypeImageTypePair default_pixel_and_image_type,
          absl::Span<const PixelTypeImageTypePair>
              alternative_pixel_and_image_types,
          int64_t default_sensor_id) -> absl::Status {
    bool pixel_type_found = bimap_sensor_ids_by_pixel_type.left.count(
                                default_pixel_and_image_type.first) > 0;
    bool alternative_pixel_type_found = absl::c_any_of(
        alternative_pixel_and_image_types,
        [&](const PixelTypeImageTypePair& alternative) {
          return bimap_sensor_ids_by_pixel_type.left.count(alternative.first) >
                 0;
        });
    if (!pixel_type_found && !alternative_pixel_type_found) {
      std::stringstream info_ss;
      info_ss << "Sensor id for '" << default_pixel_and_image_type.second
              << "' image not specified. ";
      if (!alternative_pixel_and_image_types.empty()) {
        info_ss << "Alternatives ['"
                << absl::StrJoin(alternative_pixel_and_image_types, "', '",
                                 [](std::string* out,
                                    const PixelTypeImageTypePair& pair) {
                                   absl::StrAppend(out, pair.second);
                                 })
                << "'] were also not specified. ";
      }
      info_ss << "Using default sensor id for '"
              << default_pixel_and_image_type.second
              << "' image: " << default_sensor_id;

      bool success =
          bimap_sensor_ids_by_pixel_type.left
              .insert({default_pixel_and_image_type.first, default_sensor_id})
              .second;
      if (!success) {
        return InvalidArgumentErrorBuilder()
               << "Sensor id was not specified for '"
               << default_pixel_and_image_type.second
               << "' image, but default '" << default_sensor_id
               << "' is already taken.";
      }
      LOG(INFO) << info_ss.str();
    }
    return absl::OkStatus();
  };

  if (sensor_type == sdf::SensorType::CAMERA) {
    INTR_RETURN_IF_ERROR(fill_default_sensor_id_if_needed(
        /*default_pixel_and_image_type=*/{perception::PixelType::kIntensity,
                                          "intensity"},
        /*alternative_pixel_and_image_types=*/{},
        perception::ImageSourceInterface::kFallbackSensorId));
    if (bimap_sensor_ids_by_pixel_type.size() > 1) {
      return InvalidArgumentErrorBuilder() << "Only 'intensity' image type is "
                                              "supported for 'camera' sensors.";
    }
  } else if (sensor_type == sdf::SensorType::RGBD_CAMERA) {
    INTR_RETURN_IF_ERROR(fill_default_sensor_id_if_needed(
        /*default_pixel_and_image_type=*/{perception::PixelType::kIntensity,
                                          "intensity"},
        /*alternative_pixel_and_image_types=*/{},
        perception::kIntensitySensorId));

    // If either depth or point pixel types are specified, don't fill in the
    // default id for either. Otherwise, serve point images by default.
    INTR_RETURN_IF_ERROR(fill_default_sensor_id_if_needed(
        /*default_pixel_and_image_type=*/{perception::PixelType::kPoint,
                                          "point"},
        /*alternative_pixel_and_image_types=*/
        {{perception::PixelType::kDepth, "depth"}},
        perception::kRangeSensorId));
  }

  CameraPlugin::PixelTypeToIdMap result;
  for (const auto& [pixel_type, sensor_id] :
       bimap_sensor_ids_by_pixel_type.left) {
    result[pixel_type] = sensor_id;
  }
  return result;
}

}  // namespace

void CameraPlugin::Configure(
    const gz::sim::Entity& entity,
    const std::shared_ptr<const sdf::Element>& plugin_sdf,
    gz::sim::EntityComponentManager& ecm, gz::sim::EventManager& eventMgr) {
  parent_entity_ = entity;
  auto plugin_name = plugin_sdf->Get<std::string>("name");

  INTR_ASSIGN_OR_RETURN(camera_identifier_,
                        perception::GetCameraIdentifierFromSdf(plugin_sdf),
                        _.LogError().With(ReturnVoid()));

  // Get the model name from the parent model entity.
  const gz::sim::Entity sensor_parent_entity = ecm.ParentEntity(entity);
  const gz::sim::Entity parent_model_entity =
      ecm.ParentEntity(sensor_parent_entity);
  INTR_RET_CHECK(parent_model_entity != gz::sim::kNullEntity)
      .LogError()
      .With(ReturnVoid());
  std::optional<std::string> name_data =
      ecm.ComponentData<gz::sim::components::Name>(parent_model_entity);
  INTR_RET_CHECK(name_data.has_value() && !name_data->empty())
      .LogError()
      .With(ReturnVoid());
  model_name_ = std::move(*name_data);

  // Get the sensor sdf.
  sdf::Sensor sensor_sdf;
  if (auto* camera_comp = ecm.Component<gz::sim::components::Camera>(entity);
      camera_comp != nullptr) {
    sensor_sdf = camera_comp->Data();
  } else if (auto* camera_comp =
                 ecm.Component<gz::sim::components::RgbdCamera>(entity);
             camera_comp != nullptr) {
    sensor_sdf = camera_comp->Data();
  } else {
    LOG(ERROR) << "Camera component not found for plugin: " << plugin_name;
    return;
  }

  INTR_RETURN_IF_ERROR(LoadSensorSdf(sensor_sdf))
      .With(intrinsic::ExtraMessage() << "Plugin name: " << plugin_name)
      .LogError()
      .With(ReturnVoid());

  // `trigger_topic_` is initialized in LoadSensorSdf.
  if (!trigger_topic_.empty()) {
    triggered_camera_client_.emplace(model_name_, trigger_topic_, node_,
                                     eventMgr);
  }

  INTR_ASSIGN_OR_RETURN(
      sensor_ids_by_pixel_type_, LoadSensorIds(plugin_sdf, sensor_type_),
      _.With(intrinsic::ExtraMessage() << "Camera model name: " << model_name_)
          .LogError()
          .With(ReturnVoid()));

  InitCameraConnection();
}

absl::Status CameraPlugin::LoadSensorSdf(const sdf::Sensor& sensor_sdf) {
  sensor_type_ = sensor_sdf.Type();
  const ::sdf::Camera* camera_sdf = sensor_sdf.CameraSensor();
  if (camera_sdf == nullptr) {
    return ::intrinsic::InvalidArgumentErrorBuilder()
           << "sdf::CameraSensor is null for camera:" << model_name_;
  }
  camera_params_ = intrinsic::perception::GetCameraParamsFromSdf(*camera_sdf);
  camera_t_sensor_ =
      intrinsic::perception::GzToIntrinsicSensorPose(sensor_sdf.RawPose());

  if (sensor_sdf.Topic().empty()) {
    return ::intrinsic::InvalidArgumentErrorBuilder()
           << "Empty topic name for camera: " << model_name_;
  }

  switch (sensor_type_) {
    case sdf::SensorType::CAMERA:
      intensity_topic_ = sensor_sdf.Topic();
      break;
    case sdf::SensorType::RGBD_CAMERA:
      intensity_topic_ = absl::StrCat(sensor_sdf.Topic(), "/image");
      range_topic_ = absl::StrCat(sensor_sdf.Topic(), "/depth_image");
      break;
    default:
      return ::intrinsic::InvalidArgumentErrorBuilder()
             << "Unsupported sensor type " << static_cast<int>(sensor_type_)
             << " for camera: " << model_name_;
  }

  LOG(INFO) << "Intensity topic: " << intensity_topic_
            << " for camera: " << model_name_;
  LOG_IF(INFO, !range_topic_.empty())
      << "Range topic: " << range_topic_ << " for camera: " << model_name_;

  if (camera_sdf->Triggered()) {
    trigger_topic_ = camera_sdf->TriggerTopic();
  }

  if (absl::GetFlag(FLAGS_stream_simulated_images_to_pubsub)) {
    // Because this plugin is loaded in a running Gazebo server that already has
    // a PubSub instance, it will always use the Zenoh config of the parent
    // server; no need to worry about the Zenoh config here.
    pubsub_ = std::make_unique<PubSub>();
    const std::string pubsub_image_topic =
        absl::StrCat("cameras/", model_name_, "/image");
    INTR_ASSIGN_OR_RETURN(
        auto image_pub,
        pubsub_->CreatePublisher(pubsub_image_topic, TopicConfig()));
    image_pub_ = std::make_unique<Publisher>(std::move(image_pub));
  }

  return absl::OkStatus();
}

void CameraPlugin::InitCameraConnection() {
  if (!camera_params_.has_value()) {
    LOG(ERROR) << "Invalid camera params for camera: " << model_name_;
    return;
  }

  // Setup camera config from Gazebo parameters.
  intrinsic_proto::perception::v1::CameraConfig camera_config;

  *camera_config.mutable_identifier() =
      intrinsic_proto::perception::v1::ToProto(camera_identifier_);

  std::vector<perception::SensorInformation> sensors;
  switch (sensor_type_) {
    case sdf::SensorType::CAMERA:
      QCHECK(sensor_ids_by_pixel_type_.contains(
          perception::PixelType::kIntensity));
      sensors.push_back(perception::SensorInformation(
          sensor_ids_by_pixel_type_[perception::PixelType::kIntensity],
          /*display_name=*/"color", camera_params_, camera_t_sensor_,
          {perception::PixelType::kIntensity}, camera_params_->Dimensions(),
          /*disabled=*/false));
      break;
    case sdf::SensorType::RGBD_CAMERA:
      for (const auto& [pixel_type, sensor_id] : sensor_ids_by_pixel_type_) {
        sensors.push_back(perception::SensorInformation(
            sensor_id,
            /*display_name=*/
            perception::SensorDisplayNameByPixelType(pixel_type),
            camera_params_, camera_t_sensor_, {pixel_type},
            camera_params_->Dimensions(), /*disabled=*/false));
      }
      break;
    default:
      return;
  }
  for (const perception::SensorInformation& sensor : sensors) {
    intrinsic_proto::perception::v1::SensorConfig* sensor_config =
        camera_config.add_sensor_configs();
    sensor_config->set_id(sensor.id());
    *sensor_config->mutable_camera_params() =
        intrinsic_proto::perception::v1::ToProto(*camera_params_);
    *sensor_config->mutable_camera_t_sensor() =
        ::intrinsic::ToProto(camera_t_sensor_);
  }

  intrinsic_proto::perception::v1::DescribeCameraResponse camera_description;
  for (const auto& sensor : sensors) {
    *camera_description.add_sensors() =
        intrinsic_proto::perception::v1::ToProto(sensor);
  }

  // To activate the camera, we publish a message on its trigger topic.
  const auto set_active =
      [this](bool active,
             const absl::flat_hash_set<int64_t>& requested_sensor_ids) {
        absl::MutexLock lock(image_callback_mutex_);
        // Remove any sensor images that may have been left over from a previous
        // camera activation to avoid sending stale images to the camera server.
        sensor_images_.clear();
        pending_sensor_ids_.clear();

        if (!active) {
          return true;
        }

        if (triggered_camera_client_.has_value()) {
          if (auto status = triggered_camera_client_->Trigger(); !status.ok()) {
            LOG(ERROR) << "Failed to trigger camera: " << model_name_
                       << ". Error: " << status;
            return false;
          }
        }

        // Mark all sensors as pending if `requested_sensor_ids` is empty.
        for (const auto& [_, sensor_id] : sensor_ids_by_pixel_type_) {
          if (requested_sensor_ids.empty() ||
              requested_sensor_ids.contains(sensor_id)) {
            pending_sensor_ids_.insert(sensor_id);
          }
        }
        return true;
      };

  connection_ = std::make_unique<GazeboCameraConnection>(
      camera_description, camera_config, set_active);

  // Register camera to service. If necessary, starts the gRPC service.
  auto& services =
      perception::GazeboCameraGrpcServices::StartCameraServicesSingleton();
  perception::GazeboCameraGrpcService* service = services.service();
  if (auto status = service->RegisterCamera(connection_.get()); !status.ok()) {
    LOG(ERROR) << "Failed to register " << model_name_ << ": " << status;
    set_active(false, {});
  }

  // Subscribe to camera images.
  // Note that we always generate and return both rgb and depth images for rgbd
  // cameras.
  node_.Subscribe(intensity_topic_, &CameraPlugin::OnImage, this);
  if (!range_topic_.empty()) {
    node_.Subscribe(range_topic_, &CameraPlugin::OnImage, this);
  }
}

CameraPlugin::~CameraPlugin() {
  node_.Unsubscribe(intensity_topic_);
  if (!range_topic_.empty()) {
    node_.Unsubscribe(range_topic_);
  }
  // Acquire the image mutex to ensure that all active callbacks have been
  // completed.
  absl::MutexLock lock(image_callback_mutex_);
}

void CameraPlugin::OnImage(const gz::msgs::Image& image) {
  absl::MutexLock lock(image_callback_mutex_);
  if (!camera_params_.has_value()) {
    return;
  }

  uint width = image.width();
  uint height = image.height();

  if (width == 0 || height == 0) {
    LOG(ERROR) << "Received empty frame from simulated camera " << model_name_;
    return;
  }

  absl::Time acquisition_time = absl::Now();
  Dimensions dim(width, height);

  if (image_pub_ != nullptr &&
      image.pixel_format_type() == gz::msgs::PixelFormatType::RGB_INT8) {
    sensor_msgs::msg::pb::jazzy::Image sensor_msgs_image;
    const timespec ts_now = absl::ToTimespec(acquisition_time);
    sensor_msgs_image.mutable_header()->mutable_stamp()->set_sec(ts_now.tv_sec);
    sensor_msgs_image.mutable_header()->mutable_stamp()->set_nanosec(
        ts_now.tv_nsec);
    sensor_msgs_image.mutable_header()->set_frame_id(model_name_ + "/sensor");
    sensor_msgs_image.set_width(image.width());
    sensor_msgs_image.set_height(image.height());
    sensor_msgs_image.set_step(image.step());
    sensor_msgs_image.set_is_bigendian(false);
    sensor_msgs_image.set_encoding("rgb8");
    sensor_msgs_image.set_data(image.data().c_str(), image.data().size());
    if (!image_pub_->Publish(sensor_msgs_image).ok()) {
      LOG(ERROR) << "Error publishing image in CameraPlugin::OnImage()";
    }
  }

  // If there are no pending sensor images to be populated in the
  // `GazeboCameraConnection`, return early.
  if (pending_sensor_ids_.empty()) return;

  if (image.pixel_format_type() == gz::msgs::PixelFormatType::RGB_INT8) {
    auto img = Image<Rgb8u>(
        dim, reinterpret_cast<const Rgb8u::PixelType*>(image.data().c_str()));
    QCHECK(
        sensor_ids_by_pixel_type_.contains(perception::PixelType::kIntensity));
    const int64_t intensity_sensor_id =
        sensor_ids_by_pixel_type_[perception::PixelType::kIntensity];
    if (pending_sensor_ids_.contains(intensity_sensor_id)) {
      intrinsic::perception::SensorImage image(
          intensity_sensor_id, acquisition_time, camera_params_,
          camera_t_sensor_, std::move(img));
      sensor_images_.push_back(image);
      pending_sensor_ids_.erase(intensity_sensor_id);
    }
  } else if (image.pixel_format_type() ==
             gz::msgs::PixelFormatType::R_FLOAT32) {
    const Depth32f::PixelType* depth =
        reinterpret_cast<const Depth32f::PixelType*>(image.data().c_str());

    // Compute and serve points map image if needed.
    if (sensor_ids_by_pixel_type_.contains(perception::PixelType::kPoint)) {
      const int64_t point_sensor_id =
          sensor_ids_by_pixel_type_[perception::PixelType::kPoint];
      if (pending_sensor_ids_.contains(point_sensor_id)) {
        // Compute point cloud image from depth image.
        Image<Point32f> points_image = ComputePointCloudFromDepthBuffer(
            camera_params_->intrinsic_params, dim, depth);

        intrinsic::perception::SensorImage points_sensor_image(
            point_sensor_id, acquisition_time, camera_params_, camera_t_sensor_,
            std::move(points_image));
        sensor_images_.push_back(points_sensor_image);
        pending_sensor_ids_.erase(point_sensor_id);
      }
    }

    // Compute and serve depth map image if needed.
    if (sensor_ids_by_pixel_type_.contains(perception::PixelType::kDepth)) {
      const int64_t depth_sensor_id =
          sensor_ids_by_pixel_type_[perception::PixelType::kDepth];
      if (pending_sensor_ids_.contains(depth_sensor_id)) {
        Image<Depth32f> depth_image(dim, depth);
        intrinsic::perception::SensorImage depth_sensor_image(
            depth_sensor_id, acquisition_time, camera_params_, camera_t_sensor_,
            std::move(depth_image));
        sensor_images_.push_back(depth_sensor_image);
        pending_sensor_ids_.erase(depth_sensor_id);
      }
    }

    // Compute and serve normal map image if needed.
    if (sensor_ids_by_pixel_type_.contains(perception::PixelType::kNormal)) {
      const int64_t normal_sensor_id =
          sensor_ids_by_pixel_type_[perception::PixelType::kNormal];
      if (pending_sensor_ids_.contains(normal_sensor_id)) {
        // Compute normal image from depth image view.
        const Image<Depth32f> depth_image_view(
            dim,
            reinterpret_cast<Depth32f::PixelType*>(const_cast<float*>(depth)),
            [](Depth32f::PixelType*) {});
        intrinsic::perception::SensorImage normals_sensor_image(
            normal_sensor_id, acquisition_time, camera_params_,
            camera_t_sensor_,
            perception::ComputeNormalsLeastSqr(
                camera_params_->intrinsic_params, depth_image_view,
                kNormalThreshold, kNormalRadius, kNormalStep));
        sensor_images_.push_back(normals_sensor_image);
        pending_sensor_ids_.erase(normal_sensor_id);
      }
    }
  }

  if (pending_sensor_ids_.empty()) {
    SetSensorImages();
  } else {
    LOG_EVERY_N_SEC(INFO, 3) << "Still waiting for sensor ids: "
                             << absl::StrJoin(pending_sensor_ids_, ", ");
  }
}

void CameraPlugin::SetSensorImages() {
  connection_->SetCurrentSensorImages(std::move(sensor_images_));
  sensor_images_ = std::vector<intrinsic::perception::SensorImage>();

  LOG_EVERY_N_SEC(INFO, 3) << "Set frame from simulated camera " << model_name_;
}

}  // namespace intrinsic::simulation
