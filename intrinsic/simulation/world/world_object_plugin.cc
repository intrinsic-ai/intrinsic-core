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

#include "intrinsic/simulation/world/world_object_plugin.h"

#include <cmath>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/log/log.h"
#include "absl/strings/str_join.h"
#include "absl/strings/substitute.h"
#include "google/protobuf/text_format.h"
#include "google/protobuf/wrappers.pb.h"
#include "intrinsic/hardware/gripper/service_asset/gripper_service_utils.h"
#include "intrinsic/math/proto_conversion.h"
#include "intrinsic/scene/user_data_keys.h"
#include "intrinsic/simulation/world/inlined_plugins_util.h"
#include "intrinsic/simulation/world/multi_camera_plugin_spec.h"
#include "intrinsic/util/status/ret_check.h"
#include "intrinsic/util/status/status_builder.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/component/attachment_component.h"
#include "intrinsic/world/component/collections_component.h"
#include "intrinsic/world/component/geometry_component.h"
#include "intrinsic/world/component/kinematics_component.h"
#include "intrinsic/world/component/robot_component.h"
#include "intrinsic/world/component/sensor_component.h"
#include "intrinsic/world/component/user_data_component.h"
#include "intrinsic/world/entity_id.h"
#include "intrinsic/world/geometry_types.h"
#include "intrinsic/world/proto/robot_component.pb.h"
#include "intrinsic/world/util/walk_attachment_tree.h"

namespace intrinsic {
namespace simulation {

namespace {
// Helper to find collections entity ID by alias or local name in world
absl::StatusOr<CollectionsEntityId> GetCollectionsEntityId(
    const World& world, std::string_view object_name) {
  if (auto entity_id = world.FindByAlias(object_name); entity_id.ok()) {
    return world.ValidateEntity<CollectionsEntityId>(*entity_id);
  }
  WorldHashSet<AttachmentEntityId> entity_ids =
      world.FindByLocalNames(kRootEntityId, {std::string(object_name)});
  if (entity_ids.empty()) {
    return absl::NotFoundError(absl::Substitute(
        "Cannot find entity with alias or local name '$0' in the World",
        object_name));
  }
  auto entity_id = *entity_ids.begin();
  return world.ValidateEntity<CollectionsEntityId>(entity_id);
}

absl::StatusOr<ActuatedGripperPluginSpec> CreateActuatedGripperPluginSpec(
    const World& world, std::string_view object_name,
    CollectionsEntityId collections_entity_id) {
  ActuatedGripperPluginSpec actuated_gripper;
  // Traverse object to determine prismatic joints and sticky link with
  // collision.
  INTR_ASSIGN_OR_RETURN(
      std::vector<JointEntityId> all_joints,
      world.ValidateCollectionMembers<JointEntityId>(
          collections_entity_id, CollectionsComponent::kJoints));
  std::vector<JointEntityId> candidate_sticky_link_joint_ids;
  std::stringstream joints_debug_str;
  for (auto joint_id : all_joints) {
    INTR_ASSIGN_OR_RETURN(
        const auto* kinematics_component,
        world.GetComponentByEntityId<KinematicsComponent>(joint_id));

    if (kinematics_component->GetMotionType() ==
            intrinsic_proto::world::KinematicsComponent::MOTION_TYPE_FIXED ||
        kinematics_component->GetMotionType() ==
            intrinsic_proto::world::KinematicsComponent::
                MOTION_TYPE_UNDEFINED) {
      continue;
    }

    const auto [lower, upper] =
        kinematics_component->GetSystemRawValueFixedLimits();
    if (std::isinf(lower)) {
      joints_debug_str << "Skipped joint ["
                       << world.GetLocalNameForEntityById(joint_id)
                       << "] because it does not have a finite lower limit. ";
      continue;
    }
    if (std::isinf(upper)) {
      joints_debug_str << "Skipped joint ["
                       << world.GetLocalNameForEntityById(joint_id)
                       << "] because it does not have a finite upper limit. ";
      continue;
    }
    if (std::abs(upper - lower) < 1e-4) {
      joints_debug_str << "Skipped joint ["
                       << world.GetLocalNameForEntityById(joint_id)
                       << "] because its range of motion is less than 0.1mm. ";
      continue;
    }

    if (kinematics_component->GetMotionType() ==
            intrinsic_proto::world::KinematicsComponent::
                MOTION_TYPE_PRISMATIC ||
        kinematics_component->GetMotionType() ==
            intrinsic_proto::world::KinematicsComponent::MOTION_TYPE_REVOLUTE) {
      actuated_gripper.joint_names.push_back(
          world.GetLocalNameForEntityById(joint_id));
      candidate_sticky_link_joint_ids.push_back(joint_id);
    } else {
      joints_debug_str << "Skipped joint ["
                       << world.GetLocalNameForEntityById(joint_id)
                       << "] which is not a prismatic or revolute joint. ";
      continue;
    }
  }
  if (candidate_sticky_link_joint_ids.empty()) {
    return absl::NotFoundError(absl::Substitute(
        "Cannot find any valid joints in world object $0 when trying to "
        "configure it as a pinch gripper resource instance. $1",
        object_name, joints_debug_str.str()));
  }
  const auto populate_child_links_with_collision =
      [&world](JointEntityId joint_id,
               std::vector<AttachmentEntityId>& links_with_collision)
      -> absl::Status {
    INTR_ASSIGN_OR_RETURN(WorldHashSet<AttachmentEntityId> attached_links,
                          GetRigidlyAttachedChildrenEntities(world, joint_id));
    for (auto child_id : attached_links) {
      if (auto geom_comp =
              world.GetComponentByEntityId<GeometryComponent>(child_id);
          geom_comp.ok()) {
        if ((*geom_comp)->HasGeometry(kKindCollisionGeometry)) {
          links_with_collision.push_back(child_id);
        }
      }
    }
    return absl::OkStatus();
  };

  // Use joint's child link as the sticky link for the gripper.
  std::vector<AttachmentEntityId> candidate_sticky_link_ids;
  for (auto joint_id : candidate_sticky_link_joint_ids) {
    INTR_RETURN_IF_ERROR(populate_child_links_with_collision(
        joint_id, candidate_sticky_link_ids));
  }

  if (candidate_sticky_link_ids.empty()) {
    return absl::NotFoundError(absl::Substitute(
        "Cannot find any gripper finger link with collision geometry for "
        "grasping when configuring world object $0 as a pinch gripper. Please "
        "revisit the gripper definition and add a collision geometry on at "
        "least one of the gripper finger links.",
        object_name));
  }

  // TODO(qingyou): Choosing arbitrarily from all candidates based on the
  // assumption that gripper fingers are symmetrical around the grasp
  // location. After a better heuristic is applied, make this use the most
  // likely link.
  AttachmentEntityId sticky_link_id = candidate_sticky_link_ids[0];
  actuated_gripper.sticky_link_name =
      world.GetLocalNameForEntityById(sticky_link_id);
  actuated_gripper.status_topic = absl::StrCat(object_name, "/status");
  return actuated_gripper;
}

absl::StatusOr<FixedJointGripperPluginSpec> ConfigureEoatSuctionGripper(
    const World& world,
    const ::intrinsic_proto::eoat::SuctionGripperConfig& config,
    std::string_view object_name, CollectionsEntityId collections_entity_id) {
  // Try to find a geometry with collision for simulation
  std::vector<AttachmentEntityId> candidate_gripper_links;

  INTR_ASSIGN_OR_RETURN(
      std::vector<LinkEntityId> link_ids,
      world.ValidateCollectionMembers<LinkEntityId>(
          collections_entity_id,
          intrinsic_proto::world::CollectionsComponent::COLLECTION_TYPE_LINKS));
  for (const auto& link_id : link_ids) {
    INTR_ASSIGN_OR_RETURN(const WorldEntity* link_ent,
                          world.GetEntityById(link_id));
    if (auto geometry_component = link_ent->GetComponent<GeometryComponent>();
        geometry_component.ok()) {
      if ((*geometry_component)->HasGeometry(kKindCollisionGeometry)) {
        candidate_gripper_links.emplace_back(link_id);
      }
    }
  }

  if (candidate_gripper_links.empty()) {
    LOG(WARNING) << absl::Substitute(
        "Cannot find any link with collision geometry for gripper object $0 to "
        "configure attachment link for EOAT suction gripper. Simulation will "
        "not work without a valid collision geometry.",
        object_name);
  }
  auto find_suction_gripper_link =
      [&candidate_gripper_links,
       &world]() -> absl::StatusOr<AttachmentEntityId> {
    const std::string kGripperCollisionName("gripper_collision");
    for (const auto id : candidate_gripper_links) {
      absl::StatusOr<const WorldEntity*> ent = world.GetEntityById(id);
      INTR_RET_CHECK_OK(ent);
      if ((*ent)->GetLocalName() == kGripperCollisionName) {
        return id;
      }
    }

    // Find a leaf collision link within the model(collection)
    for (AttachmentEntityId id1 : candidate_gripper_links) {
      bool id1_is_leaf = true;
      for (AttachmentEntityId id2 : candidate_gripper_links) {
        if (id1 == id2) continue;
        absl::StatusOr<AttachmentEntityId> common_ancestor =
            world.FindCommonAncestor(id1, id2);
        INTR_RET_CHECK_OK(common_ancestor);
        if (*common_ancestor == id1) {
          id1_is_leaf = false;
          break;
        }
      }
      if (id1_is_leaf) return id1;
    }
    return absl::NotFoundError(absl::Substitute(
        "Found neither leaf collision link nor collision link named '$0'",
        kGripperCollisionName));
  };

  INTR_ASSIGN_OR_RETURN(AttachmentEntityId attachment_id,
                        find_suction_gripper_link());

  return FixedJointGripperPluginSpec{
      .gripper_link_name = world.GetLocalNameForEntityById(attachment_id),
      .gripper_command_topic =
          absl::StrCat("/", object_name, "/gripper_command"),
      .gripper_status_topic = absl::StrCat("/", object_name, "/gripper_status"),
      .suction_gripper_config = config,
  };
}

absl::StatusOr<ActuatedGripperPluginSpec> ConfigureEoatPinchGripper(
    const World& world,
    const ::intrinsic_proto::eoat::PinchGripperConfig& pinch_config,
    std::string_view object_name, CollectionsEntityId collections_entity_id) {
  INTR_ASSIGN_OR_RETURN(ActuatedGripperPluginSpec actuated_gripper,
                        CreateActuatedGripperPluginSpec(world, object_name,
                                                        collections_entity_id));
  actuated_gripper.pinch_gripper_config = pinch_config;
  actuated_gripper.is_default_closed = pinch_config.is_default_closed();

  return actuated_gripper;
}

// Returns the Gazebo joint plugins (JointStatePublisher and
// JointPositionController) for the given collections entity.
//
// The returned GzPluginsSpec will be empty in the following cases:
// 1. If the collections entity represents an object that has a gripper spec
//    override configured (i.e. it is a gripper).
// 2. If the collections entity represents a hardware module.
// 3. If there are no joints associated with this collections entity.
// 4. If none of the joints are prismatic or revolute.
// 5. If the user data component of the collections entity already contains
//    gazebo plugins related to joint control or state.
absl::StatusOr<WorldObjectPlugin::GzPluginsSpec> GetGzPluginsForCollection(
    const World& world, CollectionsEntityId collections_ent_id,
    const WorldEntity& collections_ent, std::string_view object_name,
    const absl::flat_hash_map<std::string, WorldObjectPlugin::GripperSpec>&
        gripper_spec_overrides,
    const absl::flat_hash_set<std::string>& hardware_module_objects) {
  if (gripper_spec_overrides.contains(object_name) ||
      hardware_module_objects.contains(object_name)) {
    return WorldObjectPlugin::GzPluginsSpec();
  }

  absl::StatusOr<std::vector<JointEntityId>> all_joints =
      world.ValidateCollectionMembers<JointEntityId>(
          collections_ent_id, CollectionsComponent::kJoints);
  if (!all_joints.ok()) {
    if (absl::IsNotFound(all_joints.status())) {
      return WorldObjectPlugin::GzPluginsSpec();
    }
    return all_joints.status();
  }

  std::vector<JointEntityId> joint_ids;
  for (auto joint_id : *all_joints) {
    INTR_ASSIGN_OR_RETURN(
        const auto* kinematics_component,
        world.GetComponentByEntityId<KinematicsComponent>(joint_id));

    if (kinematics_component->GetMotionType() ==
            intrinsic_proto::world::KinematicsComponent::
                MOTION_TYPE_PRISMATIC ||
        kinematics_component->GetMotionType() ==
            intrinsic_proto::world::KinematicsComponent::MOTION_TYPE_REVOLUTE) {
      joint_ids.push_back(joint_id);
    }
  }
  if (joint_ids.empty()) {
    return WorldObjectPlugin::GzPluginsSpec();
  }

  std::string user_data_plugin_str;
  if (absl::StatusOr<const UserDataComponent*> user_data_component =
          collections_ent.GetComponent<UserDataComponent>();
      user_data_component.ok()) {
    const auto& user_data_protos = (*user_data_component)->UserDataProtos();
    if (auto proto_itr =
            user_data_protos.find(::intrinsic::sdf::kGazeboPlugins);
        proto_itr != user_data_protos.end()) {
      google::protobuf::StringValue str_val;
      if (proto_itr->second.UnpackTo(&str_val)) {
        user_data_plugin_str = str_val.value();
      }
    }
  }

  auto has_gz_joint_plugins = [](const std::string& plugin_str) {
    static constexpr std::array<std::string_view, 4> kGzJointPlugins = {
        "JointStatePublisher", "JointController", "JointPositionController",
        "JointTrajectoryController"};
    return std::any_of(kGzJointPlugins.begin(), kGzJointPlugins.end(),
                       [&plugin_str](std::string_view gz_joint_plugin) {
                         return plugin_str.find(gz_joint_plugin) !=
                                std::string::npos;
                       });
  };

  if (has_gz_joint_plugins(user_data_plugin_str)) {
    return WorldObjectPlugin::GzPluginsSpec();
  }

  WorldObjectPlugin::GzPluginsSpec object_gz_plugins;

  for (auto joint_id : joint_ids) {
    std::string joint_name = world.GetLocalNameForEntityById(joint_id);

    constexpr uint32_t axis_idx = 0u;
    std::string topic_prefix = absl::StrCat("/model/", object_name, "/joint/",
                                            joint_name, "/", axis_idx);

    WorldObjectPlugin::GzJointControl joint_control;
    joint_control.joint_name = joint_name;
    joint_control.axis_index = axis_idx;
    joint_control.topic = absl::StrCat(topic_prefix, "/cmd_pos");

    object_gz_plugins.push_back(std::move(joint_control));

    WorldObjectPlugin::GzJointState joint_state;
    joint_state.joint_name = std::move(joint_name);
    joint_state.topic = absl::StrCat(topic_prefix, "/state");

    object_gz_plugins.push_back(std::move(joint_state));
  }

  return object_gz_plugins;
}

absl::StatusOr<WorldObjectPlugin::CameraSpec::IntrinsicParams>
ExtractCameraIntrinsicsOverride(
    const intrinsic_proto::perception::v1::SensorConfig& sensor_config) {
  if (!sensor_config.has_camera_params()) {
    return absl::InvalidArgumentError(
        "Sensor config does not have camera params");
  }
  const auto& params = sensor_config.camera_params();
  if (params.intrinsic_params().dimensions().cols() <= 0) {
    return absl::InvalidArgumentError(
        "Camera intrinsic params must have a positive image width.");
  }
  if (params.intrinsic_params().dimensions().rows() <= 0) {
    return absl::InvalidArgumentError(
        "Camera intrinsic params must have a positive image height.");
  }
  intrinsic_proto::world::SensorComponent::Intrinsics intrinsics;
  intrinsics.set_fx(params.intrinsic_params().focal_length_x());
  intrinsics.set_fy(params.intrinsic_params().focal_length_y());
  intrinsics.set_cx(params.intrinsic_params().principal_point_x());
  intrinsics.set_cy(params.intrinsic_params().principal_point_y());

  int32_t image_width = params.intrinsic_params().dimensions().cols();
  int32_t image_height = params.intrinsic_params().dimensions().rows();
  double horizontal_fov = 0.0;
  if (params.intrinsic_params().focal_length_x() > 0) {
    horizontal_fov =
        2.0 * std::atan2(static_cast<double>(image_width),
                         2.0 * params.intrinsic_params().focal_length_x());
  }

  return WorldObjectPlugin::CameraSpec::IntrinsicParams{
      .intrinsics = std::move(intrinsics),
      .image_width = image_width,
      .image_height = image_height,
      .horizontal_fov = horizontal_fov,
  };
}

absl::StatusOr<std::optional<WorldObjectPlugin::CameraSpec::SensorProperties>>
ExtractSensorPropertiesOverrides(
    const intrinsic_proto::perception::v1::SensorConfig* sensor_config,
    std::string_view object_name) {
  if (sensor_config == nullptr) {
    return std::nullopt;
  }

  std::optional<WorldObjectPlugin::CameraSpec::IntrinsicParams> intrinsics;
  if (sensor_config->has_camera_params()) {
    INTR_ASSIGN_OR_RETURN(intrinsics,
                          ExtractCameraIntrinsicsOverride(*sensor_config));
  }

  std::optional<Pose3d> parent_t_sensor;
  if (sensor_config->has_camera_t_sensor()) {
    if (absl::StatusOr<Pose3d> pose = intrinsic_proto::FromProtoNormalized(
            sensor_config->camera_t_sensor());
        pose.ok()) {
      parent_t_sensor = *pose;
    } else {
      LOG(WARNING) << "Camera [" << object_name
                   << "] has invalid camera_t_sensor orientation for sensor id "
                   << sensor_config->id() << ". Ignoring this pose.";
    }
  }

  if (intrinsics.has_value() || parent_t_sensor.has_value()) {
    return WorldObjectPlugin::CameraSpec::SensorProperties{
        .intrinsics = std::move(intrinsics),
        .parent_t_sensor = std::move(parent_t_sensor),
    };
  }

  return std::nullopt;
}

absl::StatusOr<WorldObjectPlugin::CameraSpec> ConfigureMultiCamera(
    const World& world,
    const intrinsic_proto::perception::v1::CameraConfig& camera_config,
    const std::vector<SensorEntityId>& sensor_entity_ids,
    CollectionsEntityId collections_entity_id, std::string_view object_name) {
  INTR_ASSIGN_OR_RETURN(const WorldEntity* collections_entity,
                        world.GetEntityById(collections_entity_id));
  INTR_RET_CHECK(collections_entity != nullptr);

  INTR_ASSIGN_OR_RETURN(InlinedPluginsInfo inlined_plugins_info,
                        ParseInlinedPlugins(collections_entity));
  if (!inlined_plugins_info.multi_camera_plugin_spec.has_value()) {
    return InvalidArgumentErrorBuilder()
           << "Passed camera object " << object_name << " has "
           << sensor_entity_ids.size()
           << " child sensor entities, but does not have an associated "
              "MultiCameraPluginSpec";
  }
  const auto& multi_camera_spec =
      *inlined_plugins_info.multi_camera_plugin_spec;
  if (multi_camera_spec.sensors.empty()) {
    return InvalidArgumentErrorBuilder()
           << "MultiCameraPluginSpec for " << object_name
           << " does not contain any sensor elements.";
  }

  absl::flat_hash_set<std::string> world_sensor_names;
  world_sensor_names.reserve(sensor_entity_ids.size());
  for (SensorEntityId sensor_id : sensor_entity_ids) {
    INTR_ASSIGN_OR_RETURN(const WorldEntity* sensor_entity,
                          world.GetEntityById(sensor_id));
    world_sensor_names.insert(sensor_entity->GetLocalName());
  }

  for (const auto& sensor : multi_camera_spec.sensors) {
    if (!world_sensor_names.contains(sensor.name)) {
      return InvalidArgumentErrorBuilder()
             << "MultiCameraPluginSpec for " << object_name
             << " contains sensor with name '" << sensor.name
             << "' which does not exist in the world object.";
    }
  }

  std::string camera_identifier_proto;
  if (camera_config.has_identifier()) {
    if (!google::protobuf::TextFormat::PrintToString(
            camera_config.identifier(), &camera_identifier_proto)) {
      return absl::InternalError("Failed to format camera identifier proto");
    }
  }

  WorldObjectPlugin::CameraSpec spec;
  spec.camera_identifier_proto = std::move(camera_identifier_proto);

  // Sensor configs are stored by id in `camera_config` but by name and EntityId
  // in the world. The mapping from sensor id to name is expected to be set in
  // `multi_camera_spec`. We use this mapping to add an override spec for the
  // sensor in the return map based on the sensor config.
  absl::flat_hash_map<int64_t,
                      const intrinsic_proto::perception::v1::SensorConfig*>
      sensor_config_by_id;
  for (const auto& sensor_config : camera_config.sensor_configs()) {
    sensor_config_by_id[sensor_config.id()] = &sensor_config;
  }

  for (const auto& sensor : multi_camera_spec.sensors) {
    auto it = sensor_config_by_id.find(sensor.id);
    if (it != sensor_config_by_id.end()) {
      INTR_ASSIGN_OR_RETURN(
          std::optional<WorldObjectPlugin::CameraSpec::SensorProperties>
              properties_overrides,
          ExtractSensorPropertiesOverrides(it->second, object_name));
      if (properties_overrides.has_value()) {
        spec.sensor_properties_overrides[sensor.name] =
            std::move(*properties_overrides);
      }
    }
  }

  return spec;
}

absl::StatusOr<WorldObjectPlugin::CameraSpec> ConfigureSingleCamera(
    const World& world,
    const intrinsic_proto::perception::v1::CameraConfig& camera_config,
    SensorEntityId sensor_entity_id, std::string_view object_name) {
  INTR_ASSIGN_OR_RETURN(const WorldEntity* sensor_entity,
                        world.GetEntityById(sensor_entity_id));
  std::string sensor_name = sensor_entity->GetLocalName();

  // Select the camera params with the smallest sensor id, which refers to
  // the intensity sensor.
  const intrinsic_proto::perception::v1::SensorConfig* intensity_sensor_config =
      nullptr;
  for (const auto& sensor_config : camera_config.sensor_configs()) {
    if (sensor_config.has_camera_params() ||
        sensor_config.has_camera_t_sensor()) {
      if (intensity_sensor_config == nullptr ||
          sensor_config.id() < intensity_sensor_config->id()) {
        intensity_sensor_config = &sensor_config;
      }
    }
  }

  INTR_ASSIGN_OR_RETURN(
      std::optional<WorldObjectPlugin::CameraSpec::SensorProperties>
          properties_overrides,
      ExtractSensorPropertiesOverrides(intensity_sensor_config, object_name));

  std::string camera_identifier_proto;
  if (camera_config.has_identifier()) {
    if (!google::protobuf::TextFormat::PrintToString(
            camera_config.identifier(), &camera_identifier_proto)) {
      return absl::InternalError("Failed to format camera identifier proto");
    }
  }

  WorldObjectPlugin::CameraSpec spec;
  spec.camera_identifier_proto = std::move(camera_identifier_proto);

  if (properties_overrides.has_value()) {
    spec.sensor_properties_overrides[sensor_name] =
        std::move(*properties_overrides);
  }

  return spec;
}

absl::StatusOr<WorldObjectPlugin::CameraSpec> ConfigureCameraImpl(
    const World& world,
    const intrinsic_proto::perception::v1::CameraConfig& camera_config,
    std::string_view object_name) {
  INTR_ASSIGN_OR_RETURN(CollectionsEntityId collections_entity_id,
                        GetCollectionsEntityId(world, object_name));

  absl::StatusOr<std::vector<SensorEntityId>> sensor_entity_ids =
      world.ValidateCollectionMembers<SensorEntityId>(
          collections_entity_id, CollectionsComponent::kSensors);
  if (!sensor_entity_ids.ok()) {
    if (absl::IsNotFound(sensor_entity_ids.status())) {
      return absl::FailedPreconditionError(
          absl::StrCat("Object has no sensor entity: ", object_name));
    }
    return sensor_entity_ids.status();
  }
  if (sensor_entity_ids->empty()) {
    return absl::FailedPreconditionError(
        absl::StrCat("Object has no sensor entity: ", object_name));
  }

  if (sensor_entity_ids->size() > 1) {
    return ConfigureMultiCamera(world, camera_config, *sensor_entity_ids,
                                collections_entity_id, object_name);
  }
  return ConfigureSingleCamera(world, camera_config, (*sensor_entity_ids)[0],
                               object_name);
}

}  // namespace

WorldObjectPlugin::WorldObjectPlugin(const World& world) : world_(world) {}

absl::Status WorldObjectPlugin::ConfigureHardwareModule(
    const intrinsic_proto::icon::HardwareModuleConfig& /*config*/,
    std::string_view object_name) {
  INTR_RETURN_IF_ERROR(GetCollectionsEntityId(world_, object_name).status());
  hardware_module_objects_.emplace(object_name);
  return absl::OkStatus();
}

absl::Status WorldObjectPlugin::ConfigureEoatGripper(
    const intrinsic_proto::eoat::GripperConfig& config,
    std::string_view object_name) {
  INTR_ASSIGN_OR_RETURN(CollectionsEntityId collections_entity_id,
                        GetCollectionsEntityId(world_, object_name));
  switch (config.gripper_config_case()) {
    case intrinsic_proto::eoat::GripperConfig::kPinch: {
      INTR_ASSIGN_OR_RETURN(
          ActuatedGripperPluginSpec spec,
          ConfigureEoatPinchGripper(world_, config.pinch(), object_name,
                                    collections_entity_id));
      gripper_specs_[object_name] = spec;
      return absl::OkStatus();
    }
    case intrinsic_proto::eoat::GripperConfig::kSuction: {
      INTR_ASSIGN_OR_RETURN(
          FixedJointGripperPluginSpec spec,
          ConfigureEoatSuctionGripper(world_, config.suction(), object_name,
                                      collections_entity_id));
      gripper_specs_[object_name] = spec;
      return absl::OkStatus();
    }
    default:
      return absl::UnimplementedError(
          "Unsupported intrinsic_proto::eoat::GripperConfig type");
  }
}

absl::Status WorldObjectPlugin::ConfigurePinchGripper(
    const intrinsic_proto::gripper::PinchGripperPart& config,
    std::string_view object_name) {
  INTR_ASSIGN_OR_RETURN(CollectionsEntityId collections_entity_id,
                        GetCollectionsEntityId(world_, object_name));

  INTR_ASSIGN_OR_RETURN(ActuatedGripperPluginSpec plugin_spec,
                        CreateActuatedGripperPluginSpec(world_, object_name,
                                                        collections_entity_id));

  if (config.config().name().empty()) {
    plugin_spec.pinch_gripper_handle = object_name;
  }
  plugin_spec.service_pinch_gripper_config = config.config();
  if (config.config().has_generic_pinch_gripper_config() &&
      config.config().generic_pinch_gripper_config().has_additional_config()) {
    const auto& add_cfg =
        config.config().generic_pinch_gripper_config().additional_config();
    if (!add_cfg.status_topic().empty()) {
      plugin_spec.status_topic = add_cfg.status_topic();
    }
  }

  gripper_specs_[object_name] = plugin_spec;
  return absl::OkStatus();
}

absl::Status WorldObjectPlugin::ConfigureSuctionGripperRealtimeControl(
    const intrinsic_proto::gripper_service::
        SuctionGripperRealtimeControlServiceConfig& config,
    std::string_view object_name) {
  INTR_ASSIGN_OR_RETURN(
      intrinsic_proto::eoat::GripperConfig gripper_config,
      intrinsic::gripper::MakeSuctionGripperRealtimeControlConfig(config));
  return ConfigureEoatGripper(gripper_config, object_name);
}

absl::Status WorldObjectPlugin::ConfigurePinchGripperRealtimeControl(
    const intrinsic_proto::gripper_service::
        PinchGripperRealtimeControlServiceConfig& config,
    std::string_view object_name) {
  INTR_ASSIGN_OR_RETURN(
      intrinsic_proto::eoat::GripperConfig gripper_config,
      intrinsic::gripper::MakePinchGripperRealtimeControlConfig(config));
  return ConfigureEoatGripper(gripper_config, object_name);
}

absl::Status WorldObjectPlugin::ConfigureSuctionGripperOpcua(
    const intrinsic_proto::gripper_service::SuctionGripperOpcuaServiceConfig&
        config,
    std::string_view object_name) {
  INTR_ASSIGN_OR_RETURN(
      intrinsic_proto::eoat::GripperConfig gripper_config,
      intrinsic::gripper::MakeSuctionGripperOpcuaConfig(config));
  return ConfigureEoatGripper(gripper_config, object_name);
}

absl::Status WorldObjectPlugin::ConfigurePinchGripperOpcua(
    const intrinsic_proto::gripper_service::PinchGripperOpcuaServiceConfig&
        config,
    std::string_view object_name) {
  INTR_ASSIGN_OR_RETURN(
      intrinsic_proto::eoat::GripperConfig gripper_config,
      intrinsic::gripper::MakePinchGripperOpcuaConfig(config));
  return ConfigureEoatGripper(gripper_config, object_name);
}

absl::Status WorldObjectPlugin::ConfigureCamera(
    const intrinsic_proto::perception::v1::CameraConfig& camera_config,
    std::string_view object_name) {
  INTR_ASSIGN_OR_RETURN(
      CameraSpec spec, ConfigureCameraImpl(world_, camera_config, object_name));

  camera_specs_[object_name] = std::move(spec);
  return absl::OkStatus();
}

absl::StatusOr<WorldObjectPlugin::AllPluginSpecs>
WorldObjectPlugin::GetAllPluginSpecs(bool add_gz_plugins) const {
  AllPluginSpecs specs;
  specs.gripper_specs = gripper_specs_;
  specs.hardware_module_objects = hardware_module_objects_;
  specs.camera_specs = camera_specs_;

  if (!add_gz_plugins) {
    return specs;
  }

  std::vector<CollectionsEntityId> all_collections =
      world_.GetTypedEntityIds<CollectionsEntityId>();

  std::stringstream errors;
  for (auto collections_ent_id : all_collections) {
    absl::StatusOr<const WorldEntity*> collections_ent =
        world_.GetEntityById(collections_ent_id);
    INTR_RET_CHECK_OK(collections_ent);

    std::string object_name = (*collections_ent)->GetAlias();
    if (object_name.empty()) {
      LOG(WARNING)
          << "Collections entity " << collections_ent_id.value()
          << " with local name " << (*collections_ent)->GetLocalName()
          << " does not have a global alias. Skipping Gazebo joint plugins "
             "config.";
      continue;
    }

    absl::StatusOr<GzPluginsSpec> object_gz_plugins = GetGzPluginsForCollection(
        world_, collections_ent_id, **collections_ent, object_name,
        gripper_specs_, hardware_module_objects_);
    if (!object_gz_plugins.ok()) {
      errors << object_gz_plugins.status().ToString() << "\n";
      continue;
    }

    if (!object_gz_plugins->empty()) {
      specs.gz_plugins_specs[object_name] = std::move(*object_gz_plugins);
    }
  }

  if (!errors.str().empty()) {
    return absl::AbortedError(absl::StrCat(
        "Errors configuring Gazebo joint plugins:\n", errors.str()));
  }

  return specs;
}

}  // namespace simulation
}  // namespace intrinsic
