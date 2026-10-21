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

#include "intrinsic/simulation/gazebo/plugins/cameras/multi_camera_plugin.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gz/msgs/MessageTypes.hh"
#include "gz/msgs/convert/PixelFormatType.hh"
#include "gz/sim/Entity.hh"
#include "gz/sim/EntityComponentManager.hh"
#include "gz/sim/EventManager.hh"
#include "gz/sim/Types.hh"
#include "gz/sim/components/Camera.hh"
#include "gz/sim/components/Model.hh"
#include "gz/sim/components/Name.hh"
#include "gz/sim/components/RgbdCamera.hh"
#include "gz/sim/components/Sensor.hh"
#include "gz/transport/MessageInfo.hh"
#include "gz/transport/Node.hh"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/proto_conversion.h"
#include "intrinsic/perception/cameras/sensor_image.h"
#include "intrinsic/perception/cameras/sensor_information.h"
#include "intrinsic/perception/core/camera_params.h"
#include "intrinsic/perception/core/compute_normals_least_sqr.h"
#include "intrinsic/perception/core/dimensions.h"
#include "intrinsic/perception/core/image_traits.h"
#include "intrinsic/perception/core/pixel_type.h"
#include "intrinsic/perception/proto/v1/camera_config.pb.h"
#include "intrinsic/perception/proto/v1/camera_service.pb.h"
#include "intrinsic/perception/proto_conversion/v1/camera_identifier.h"
#include "intrinsic/perception/proto_conversion/v1/camera_params.h"
#include "intrinsic/perception/proto_conversion/v1/camera_service.h"
#include "intrinsic/simulation/gazebo/plugins/cameras/camera_service.h"
#include "intrinsic/simulation/gazebo/plugins/cameras/camera_services.h"
#include "intrinsic/simulation/gazebo/plugins/cameras/point_cloud_from_depth_buffer.h"
#include "intrinsic/simulation/gazebo/plugins/cameras/simulation_camera_utils.h"
#include "intrinsic/simulation/gazebo/plugins/cameras/triggered_camera_client.h"
#include "intrinsic/util/status/return.h"
#include "intrinsic/util/status/status_builder.h"
#include "intrinsic/util/status/status_macros.h"
#include "sdf/Camera.hh"
#include "sdf/Element.hh"
#include "sdf/Sensor.hh"

namespace intrinsic::simulation {

using ::intrinsic::InvalidArgumentErrorBuilder;
using ::intrinsic::NotFoundErrorBuilder;
using ::intrinsic::perception::Depth32f;
using ::intrinsic::perception::GazeboCameraConnection;
using ::intrinsic::perception::Gray8u;
using ::intrinsic::perception::Image;
using ::intrinsic::perception::Point32f;
using ::intrinsic::perception::Rgb8u;

namespace {

std::string FormatTopic(const std::string& topic) {
  if (topic.empty()) return topic;
  if (topic[0] != '/') {
    return absl::StrCat("/", topic);
  }
  return topic;
}

struct PluginSensorSpec {
  int64_t id;
  std::string name;
  std::optional<perception::PixelType> image_type;
};

absl::StatusOr<absl::flat_hash_map<int64_t, PluginSensorSpec>>
LoadSensorIdsFromPluginSdf(const std::shared_ptr<const sdf::Element>& sdf) {
  std::vector<std::string> errors;
  absl::flat_hash_map<int64_t, PluginSensorSpec> sensors_to_find;

  if (!sdf->HasElement("sensor")) {
    return InvalidArgumentErrorBuilder()
           << "One or more <sensor> elements must be specified.";
  }

  for (sdf::ElementConstPtr sensorElem = sdf->FindElement("sensor");
       sensorElem != nullptr;
       sensorElem = sensorElem->GetNextElement("sensor")) {
    if (!sensorElem->HasElement("id")) {
      errors.push_back("An <id> element must be specified in <sensor>.");
      continue;
    }
    if (!sensorElem->HasElement("name")) {
      errors.push_back("A <name> element must be specified in <sensor>.");
      continue;
    }

    // Get does not support int64_t / long so static_cast it.
    int64_t id = static_cast<int64_t>(sensorElem->Get<uint64_t>("id"));
    auto name = sensorElem->Get<std::string>("name");

    std::optional<perception::PixelType> image_type;
    std::string type_str;
    if (sensorElem->HasElement("image_type")) {
      type_str = sensorElem->Get<std::string>("image_type");
    }
    if (!type_str.empty()) {
      auto image_type_or =
          perception::PixelTypeFromSdfPluginImageType(type_str);
      if (!image_type_or.ok()) {
        errors.push_back(absl::StrCat(image_type_or.status().message(),
                                      " for sensor id ", id));
        continue;
      }
      image_type = *image_type_or;
    }

    if (!sensors_to_find.emplace(id, PluginSensorSpec{id, name, image_type})
             .second) {
      errors.push_back(absl::StrCat("Duplicate <sensor><id> detected: ", id));
      continue;
    }
  }

  if (!errors.empty()) {
    return InvalidArgumentErrorBuilder() << absl::StrJoin(errors, "\n");
  }
  if (sensors_to_find.empty()) {
    return InvalidArgumentErrorBuilder()
           << "Unable to find any sensors specified in the <sensor> elements.";
  }

  return sensors_to_find;
}

absl::StatusOr<perception::PixelType> ValidateImageTypeForCameraType(
    sdf::SensorType sensor_type, const PluginSensorSpec& sensor_item) {
  if (sensor_type == sdf::SensorType::CAMERA) {
    if (sensor_item.image_type.has_value() &&
        *sensor_item.image_type != perception::PixelType::kIntensity) {
      return InvalidArgumentErrorBuilder()
             << "Only 'intensity' image type is supported for 'camera' "
                "sensors. Sensor id: "
             << sensor_item.id;
    }
    return perception::PixelType::kIntensity;
  } else if (sensor_type == sdf::SensorType::RGBD_CAMERA) {
    if (!sensor_item.image_type.has_value()) {
      return NotFoundErrorBuilder()
             << "image_type must be specified for RGBD camera sensor id "
             << sensor_item.id << ", sensor name " << sensor_item.name;
    }
    return *sensor_item.image_type;
  }
  return InvalidArgumentErrorBuilder()
         << "Unsupported sensor type for sensor name: " << sensor_item.name;
}

absl::StatusOr<gz::sim::Entity> FindDescendentSensorInECM(
    const std::string& sensor_name, gz::sim::Entity model_entity,
    const gz::sim::EntityComponentManager& ecm) {
  std::vector<gz::sim::Entity> entities = ecm.EntitiesByComponents(
      gz::sim::components::Sensor(), gz::sim::components::Name(sensor_name));
  if (entities.empty()) {
    return NotFoundErrorBuilder() << "Failed to find camera " << sensor_name;
  }

  if (entities.size() == 1) {
    return entities[0];
  }

  // More than one camera entities with the same name exists
  // Find the one that belongs to this model
  for (const auto ent : entities) {
    auto parent_entity = ecm.ParentEntity(ent);
    while (parent_entity != gz::sim::kNullEntity) {
      if (parent_entity == model_entity) {
        return ent;
      }
      parent_entity = ecm.ParentEntity(parent_entity);
    }
  }

  return NotFoundErrorBuilder()
         << "Failed to find descendent camera entity " << sensor_name
         << " for model entity " << model_entity;
}

absl::StatusOr<absl::flat_hash_map<int64_t, CameraSensorProperty>>
SensorPropertiesFromECM(
    const absl::flat_hash_map<int64_t, PluginSensorSpec>& sensors_to_find,
    gz::sim::Entity model_entity, const gz::sim::EntityComponentManager& ecm) {
  absl::flat_hash_map<int64_t, CameraSensorProperty> sensor_properties;
  // Find Gazebo sensor entities based on names specified in plugin
  for (const auto& [id, sensor_item] : sensors_to_find) {
    INTR_ASSIGN_OR_RETURN(
        gz::sim::Entity camera_entity,
        FindDescendentSensorInECM(sensor_item.name, model_entity, ecm));
    // Copy the camera sdf
    sdf::Sensor sensor_sdf;
    if (auto* camera_comp =
            ecm.Component<gz::sim::components::Camera>(camera_entity);
        camera_comp != nullptr) {
      sensor_sdf = camera_comp->Data();
    } else if (auto* camera_comp =
                   ecm.Component<gz::sim::components::RgbdCamera>(
                       camera_entity);
               camera_comp != nullptr) {
      sensor_sdf = camera_comp->Data();
    } else {
      return NotFoundErrorBuilder()
             << "Camera component not found for entity " << camera_entity
             << " with name " << sensor_item.name;
    }

    INTR_ASSIGN_OR_RETURN(
        perception::PixelType image_type,
        ValidateImageTypeForCameraType(sensor_sdf.Type(), sensor_item));

    sensor_properties[id] = {.id = id,
                             .entity = camera_entity,
                             .name = sensor_item.name,
                             .sdf = std::move(sensor_sdf),
                             .image_type = image_type};
  }
  return sensor_properties;
}
}  // namespace

void MultiCameraPlugin::Configure(
    const gz::sim::Entity& entity,
    const std::shared_ptr<const sdf::Element>& sdf,
    gz::sim::EntityComponentManager& ecm, gz::sim::EventManager& eventMgr) {
  auto plugin_name = sdf->Get<std::string>("name");

  if (!ecm.EntityHasComponentType(entity, gz::sim::components::Model::typeId)) {
    connection_status_ =
        InvalidArgumentErrorBuilder().LogError()
        << "The Multi Camera Plugin System can only be attached to a model. "
        << "Plugin name: " << plugin_name;
    return;
  }

  INTR_ASSIGN_OR_RETURN(camera_identifier_,
                        perception::GetCameraIdentifierFromSdf(sdf),
                        (_.LogError() << "Plugin name: " << plugin_name)
                            .With([this](const absl::Status& status) {
                              connection_status_ = status;
                            }));

  model_name_ = ecm.Component<gz::sim::components::Name>(entity)->Data();
  LOG(INFO) << "Model name: " << model_name_
            << ", camera identifier: " << camera_identifier_;

  // Get values specified in the <sensor> elements, which are then used to find
  // the corresponding sensor entities from the Gazebo ECM.
  INTR_ASSIGN_OR_RETURN(auto sensors_to_find, LoadSensorIdsFromPluginSdf(sdf),
                        (_.LogError() << "Plugin name: " << plugin_name)
                            .With([this](const absl::Status& status) {
                              connection_status_ = status;
                            }));

  INTR_ASSIGN_OR_RETURN(sensor_properties_,
                        SensorPropertiesFromECM(sensors_to_find, entity, ecm),
                        (_.LogError() << "Plugin name: " << plugin_name)
                            .With([this](const absl::Status& status) {
                              connection_status_ = status;
                            }));

  // Initialize trigger publishers for triggered cameras.
  for (const auto& [id, sensor_prop] : sensor_properties_) {
    const sdf::Camera* camera_sdf = sensor_prop.sdf.CameraSensor();
    if (camera_sdf->Triggered()) {
      const std::string trigger_topic = camera_sdf->TriggerTopic();
      if (!triggered_camera_clients_.contains(trigger_topic)) {
        LOG(INFO) << "Trigger topic: " << trigger_topic
                  << " for camera: " << model_name_
                  << ", sensor name: " << sensor_prop.name;
        const std::string camera_and_sensor_name =
            absl::StrCat(model_name_, "::", sensor_prop.name);
        triggered_camera_clients_.try_emplace(trigger_topic,
                                              camera_and_sensor_name,
                                              trigger_topic, node_, eventMgr);
      }
    }
  }

  InitCameraConnection();
  connection_status_ = absl::OkStatus();
}

void MultiCameraPlugin::PostUpdate(const gz::sim::UpdateInfo& info,
                                   const gz::sim::EntityComponentManager& ecm) {
  if (sensor_topics_set_) {
    return;
  }

  bool sensor_topics_set = true;
  for (auto& [id, sensor_prop] : sensor_properties_) {
    auto sensor_topic_comp =
        ecm.Component<gz::sim::components::SensorTopic>(sensor_prop.entity);
    if (sensor_topic_comp && !sensor_topic_comp->Data().empty()) {
      // Make sure the topic name has leading '/'
      // to match the one stored in message info
      std::string base_topic = FormatTopic(sensor_topic_comp->Data());
      std::string topic;
      if (sensor_prop.sdf.Type() == sdf::SensorType::RGBD_CAMERA) {
        if (sensor_prop.image_type == perception::PixelType::kIntensity) {
          topic = absl::StrCat(base_topic, "/image");
        } else {
          topic = absl::StrCat(base_topic, "/depth_image");
        }
      } else {
        topic = base_topic;
      }
      sensor_prop.sdf.SetTopic(topic);
      if (!sensor_topics_.contains(topic)) {
        node_.Subscribe(topic, &MultiCameraPlugin::OnImage, this);
      }
      sensor_topics_[topic].insert(id);
    } else {
      sensor_topics_set = false;
    }
  }
  sensor_topics_set_ = sensor_topics_set;
}

void MultiCameraPlugin::InitCameraConnection() {
  // Setup camera description from Gazebo parameters.
  intrinsic_proto::perception::v1::DescribeCameraResponse camera_description;
  intrinsic_proto::perception::v1::CameraConfig camera_config;
  *camera_config.mutable_identifier() =
      intrinsic_proto::perception::v1::ToProto(camera_identifier_);

  CHECK(!sensor_properties_.empty());
  for (const auto& [id, sensor_prop] : sensor_properties_) {
    const Pose3d camera_t_sensor =
        perception::GzToIntrinsicSensorPose(sensor_prop.sdf.RawPose());

    intrinsic::perception::CameraParams sensor_params =
        intrinsic::perception::GetCameraParamsFromSdf(
            *sensor_prop.sdf.CameraSensor());

    perception::SensorInformation sensor_info(
        id, sensor_prop.name, sensor_params, camera_t_sensor,
        {sensor_prop.image_type}, sensor_params.Dimensions(),
        /*disabled=*/false);

    *camera_description.add_sensors() =
        intrinsic_proto::perception::v1::ToProto(sensor_info);

    // Set the intrinsic and distortion parameters for the sensor as the
    // respective parameters in the camera config.
    intrinsic_proto::perception::v1::SensorConfig* sensor_config =
        camera_config.add_sensor_configs();
    sensor_config->set_id(id);
    *sensor_config->mutable_camera_params() =
        intrinsic_proto::perception::v1::ToProto(sensor_params);
    *sensor_config->mutable_camera_t_sensor() =
        ::intrinsic::ToProto(camera_t_sensor);
  }

  // To activate a sensor, we publish a message on its trigger topic.
  const auto set_active = [this](
                              bool active,
                              const absl::flat_hash_set<int64_t>& sensor_ids) {
    if (active && !sensor_topics_set_) {
      LOG(WARNING) << "Tried activating sensor, but sensor topics have not yet "
                      "been set for device: "
                   << model_name_;
      return false;
    }

    absl::MutexLock lock(image_mutex_);
    image_request_active_ = active;
    if (active) {
      for (int64_t sensor_id : sensor_ids) {
        if (!sensor_properties_.contains(sensor_id)) {
          LOG(ERROR) << "Unknown sensor ID " << sensor_id
                     << " requested for camera device: " << model_name_;
          return false;
        }
      }
      active_sensor_ids_ = sensor_ids;

      INTR_RETURN_IF_ERROR(TriggerSensors(sensor_ids))
          .LogError()
          .With(Return(false));
    } else {
      // Clean up tracking variables in case all requested images have not been
      // received yet.
      received_sensor_ids_.clear();
      sensor_images_.clear();
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
    LOG(ERROR) << "Failed to register multi camera " << model_name_ << ": "
               << status;
    set_active(false, {});
  }
}

// Triggers sensors associated with the given `sensor_ids`. If `sensor_ids`
// is empty, all sensors for the camera device are triggered.
absl::Status MultiCameraPlugin::TriggerSensors(
    const absl::flat_hash_set<int64_t>& sensor_ids) {
  if (sensor_ids.empty()) {
    for (auto& [trigger_topic, triggered_camera_client] :
         triggered_camera_clients_) {
      INTR_RETURN_IF_ERROR(triggered_camera_client.Trigger())
          << "Failed to trigger sensor for camera: " << model_name_
          << ", trigger topic: " << trigger_topic;
    }
    return absl::OkStatus();
  }

  absl::flat_hash_set<std::string> triggered_topics;
  for (int64_t sensor_id : sensor_ids) {
    const CameraSensorProperty& sensor_prop = sensor_properties_.at(sensor_id);
    CHECK(sensor_prop.sdf.CameraSensor() != nullptr)
        << "Camera sensor SDF not found for sensor ID: " << sensor_id;
    const std::string& trigger_topic =
        sensor_prop.sdf.CameraSensor()->TriggerTopic();
    if (trigger_topic.empty() ||
        !triggered_topics.insert(trigger_topic).second) {
      continue;
    }
    auto it = triggered_camera_clients_.find(trigger_topic);
    CHECK(it != triggered_camera_clients_.end())
        << "Trigger topic not found in triggered_camera_clients_: "
        << trigger_topic;
    INTR_RETURN_IF_ERROR(it->second.Trigger())
        << "Failed to trigger sensor id " << sensor_id
        << " for camera: " << model_name_
        << ", trigger topic: " << trigger_topic;
  }
  return absl::OkStatus();
}

MultiCameraPlugin::~MultiCameraPlugin() {
  for (const auto& [topic, _] : sensor_topics_) {
    node_.Unsubscribe(topic);
  }
  // Acquire the image mutex to ensure that all active callbacks have been
  // completed.
  absl::MutexLock lock(image_mutex_);
}

void MultiCameraPlugin::OnImage(const gz::msgs::Image& msg,
                                const gz::transport::MessageInfo& info) {
  absl::MutexLock lock(image_mutex_);

  // Only publish the sensor images if the camera is currently active and
  // there is an active image request.
  if (connection_ == nullptr || !image_request_active_) {
    return;
  }

  uint width = msg.width();
  uint height = msg.height();

  if (width == 0 || height == 0) {
    LOG(ERROR) << "Received empty frame from simulated multi camera. "
               << "Model name: " << model_name_;
    return;
  }

  std::vector<int64_t> sensor_ids_to_process;
  {
    auto result = sensor_topics_.find(info.Topic());
    if (result == sensor_topics_.end()) {
      LOG(ERROR) << "Sensor topic not found in sensor_topics_: "
                 << info.Topic();
      return;
    }
    for (int64_t sensor_id : result->second) {
      if (!active_sensor_ids_.empty() &&
          !active_sensor_ids_.contains(sensor_id)) {
        continue;
      }
      if (received_sensor_ids_.contains(sensor_id)) {
        continue;
      }
      sensor_ids_to_process.push_back(sensor_id);
    }
  }

  absl::Time acquisition_time = absl::Now();
  intrinsic::perception::Dimensions dim(width, height);

  constexpr float kNormalThreshold = 0.5f;
  constexpr int kNormalRadius = 5;
  constexpr int kNormalStep = 1;

  for (int64_t sensor_id : sensor_ids_to_process) {
    const CameraSensorProperty& sensor_prop = sensor_properties_[sensor_id];
    const Pose3d camera_t_sensor =
        perception::GzToIntrinsicSensorPose(sensor_prop.sdf.RawPose());
    intrinsic::perception::CameraParams camera_params =
        intrinsic::perception::GetCameraParamsFromSdf(
            *sensor_prop.sdf.CameraSensor());

    auto convert_and_add_sensor_image =
        [this, sensor_id, acquisition_time, &camera_params,
         &camera_t_sensor]<typename ImgT>(ImgT&& img) {
          image_mutex_.AssertHeld();
          intrinsic::perception::SensorImage image(
              sensor_id, acquisition_time, camera_params, camera_t_sensor,
              std::forward<ImgT>(img));
          sensor_images_.push_back(image);
          received_sensor_ids_.insert(sensor_id);
        };

    if (msg.pixel_format_type() == gz::msgs::PixelFormatType::RGB_INT8) {
      if (sensor_prop.image_type == perception::PixelType::kIntensity) {
        convert_and_add_sensor_image(Image<Rgb8u>(
            dim,
            reinterpret_cast<const Rgb8u::PixelType*>(msg.data().c_str())));
      }
    } else if (msg.pixel_format_type() == gz::msgs::PixelFormatType::L_INT8) {
      if (sensor_prop.image_type == perception::PixelType::kIntensity) {
        convert_and_add_sensor_image(Image<Gray8u>(
            dim,
            reinterpret_cast<const Gray8u::PixelType*>(msg.data().c_str())));
      }
    } else if (msg.pixel_format_type() ==
               gz::msgs::PixelFormatType::R_FLOAT32) {
      const Depth32f::PixelType* depth =
          reinterpret_cast<const Depth32f::PixelType*>(msg.data().c_str());

      if (sensor_prop.image_type == perception::PixelType::kPoint) {
        convert_and_add_sensor_image(ComputePointCloudFromDepthBuffer(
            camera_params.intrinsic_params, dim, depth));
      } else if (sensor_prop.image_type == perception::PixelType::kDepth) {
        convert_and_add_sensor_image(Image<Depth32f>(dim, depth));
      } else if (sensor_prop.image_type == perception::PixelType::kNormal) {
        const Image<Depth32f> depth_image_view(
            dim,
            reinterpret_cast<Depth32f::PixelType*>(const_cast<float*>(depth)),
            [](Depth32f::PixelType*) {});
        convert_and_add_sensor_image(perception::ComputeNormalsLeastSqr(
            camera_params.intrinsic_params, depth_image_view, kNormalThreshold,
            kNormalRadius, kNormalStep));
      }
    } else {
      LOG(ERROR) << "The Multi-Camera Plugin System currently does not support "
                 << "Image format: "
                 << gz::msgs::ConvertPixelFormatType(msg.pixel_format_type());
    }
  }

  SetSensorImages();
}

void MultiCameraPlugin::SetSensorImages() {
  int expected_sensor_count = active_sensor_ids_.empty()
                                  ? sensor_properties_.size()
                                  : active_sensor_ids_.size();
  if (received_sensor_ids_.size() < expected_sensor_count) {
    return;
  }

  connection_->SetCurrentSensorImages(std::move(sensor_images_));

  LOG_EVERY_N_SEC(INFO, 3) << "Set sensor images from simulated camera "
                           << model_name_;

  received_sensor_ids_.clear();
  active_sensor_ids_.clear();
  sensor_images_ = std::vector<intrinsic::perception::SensorImage>();
}

absl::StatusOr<GazeboCameraConnection*> MultiCameraPlugin::GetCameraConnection()
    const {
  if (!connection_status_.ok()) {
    return connection_status_;
  }
  CHECK(connection_ != nullptr);
  return connection_.get();
}

}  // namespace intrinsic::simulation
