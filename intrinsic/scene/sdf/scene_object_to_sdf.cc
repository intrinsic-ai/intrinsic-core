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

#include "intrinsic/scene/sdf/scene_object_to_sdf.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/substitute.h"
#include "intrinsic/geometry/storage/geometry_deserializer.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/proto_conversion.h"
#include "intrinsic/scene/conversion/user_data.h"
#include "intrinsic/scene/proto/v1/entity.pb.h"
#include "intrinsic/scene/proto/v1/scene_object.pb.h"
#include "intrinsic/scene/sdf/convert_to_sdf.h"
#include "intrinsic/scene/sdf/custom_tags.h"
#include "intrinsic/scene/sdf/sdf_sensor_pose.h"
#include "intrinsic/scene/sdf/sdf_util.h"
#include "intrinsic/scene/sdf/sim_spec_to_sdf.h"
#include "intrinsic/scene/sdf/xml_utils.h"
#include "intrinsic/scene/validate/scene_object_validation.h"
#include "intrinsic/util/full_precision.h"
#include "intrinsic/util/status/status_macros.h"
#include "tinyxml2.h"

namespace intrinsic {
namespace sdf {

namespace {

using ::intrinsic::FullPrecision;
using ::intrinsic_proto::scene_object::v1::Entity;

// Returns translation and normalized quaternion. An unset orientation maps to
// identity. Returns InvalidArgumentError if the orientation quaternion is set
// but cannot be normalized (e.g., all zeros or non-finite components).
absl::StatusOr<Pose3d> FromPoseProto(const intrinsic_proto::Pose& pose_proto) {
  Pose3d pose;
  if (pose_proto.has_position()) {
    pose.setTranslation(intrinsic_proto::FromProto(pose_proto.position()));
  }
  if (pose_proto.has_orientation()) {
    const auto q = intrinsic_proto::FromProto(pose_proto.orientation());
    const double squared_norm = q.squaredNorm();
    if (!std::isfinite(squared_norm) || squared_norm <= 1e-12) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Invalid orientation quaternion that cannot be normalized: {x: ",
          FullPrecision(q.x()), ", y: ", FullPrecision(q.y()),
          ", z: ", FullPrecision(q.z()), ", w: ", FullPrecision(q.w()), "}"));
    }
    pose.setQuaternion(q.normalized());
  }
  return pose;
}

absl::StatusOr<std::string> FrameToSdf(const Entity& entity) {
  std::string result =
      absl::Substitute("<frame name=\"$0\" ", EscapeXml(entity.name()));
  if (entity.frame().is_attachment_frame()) {
    absl::SubstituteAndAppend(&result, "$0=\"true\" ",
                              kCreateAttachmentEntityCustomAttribute);
  } else {
    absl::SubstituteAndAppend(&result, "$0=\"true\" ",
                              kCreateEntityCustomAttribute);
  }
  if (!entity.parent_name().empty()) {
    absl::StrAppend(&result, "attached_to=\"", EscapeXml(entity.parent_name()),
                    "\" ");
  }

  INTR_ASSIGN_OR_RETURN(auto pose, FromPoseProto(entity.parent_t_this()));

  absl::SubstituteAndAppend(&result, ">$0</frame>", Pose3ToPoseString(pose));
  return result;
}

absl::StatusOr<std::string> SensorToSdf(const Entity& entity) {
  const auto& sensor = entity.sensor().sensor_component();
  ::sdf::SensorType type;
  std::string inner_sdf;
  switch (sensor.type_oneof_case()) {
    case intrinsic_proto::world::SensorComponent::kCamera:
      [[fallthrough]];
    case intrinsic_proto::world::SensorComponent::kDepthCamera: {
      const bool is_camera = sensor.has_camera();
      const auto& props = is_camera ? sensor.camera().properties()
                                    : sensor.depth_camera().properties();
      type = is_camera ? ::sdf::SensorType::CAMERA
                       : ::sdf::SensorType::DEPTH_CAMERA;
      INTR_ASSIGN_OR_RETURN(inner_sdf, CommonCameraPropertiesToString(
                                           props, entity.name(), std::nullopt));
      break;
    }
    case intrinsic_proto::world::SensorComponent::kForceTorque: {
      type = ::sdf::SensorType::FORCE_TORQUE;
      INTR_ASSIGN_OR_RETURN(
          inner_sdf,
          ForceTorqueSpecToString(sensor.force_torque(), entity.name()));
      break;
    }
    case intrinsic_proto::world::SensorComponent::kLidar: {
      type = ::sdf::SensorType::LIDAR;
      INTR_ASSIGN_OR_RETURN(inner_sdf, LidarSpecToString(sensor.lidar()));
      break;
    }
    default:
      return absl::InvalidArgumentError("Unsupported sensor type");
  }

  std::string result =
      absl::Substitute("<sensor name=\"$0\" type=\"$1\">",
                       EscapeXml(entity.name()), GetSensorTypeString(type));

  INTR_ASSIGN_OR_RETURN(auto pose, FromPoseProto(entity.parent_t_this()));
  absl::StrAppend(&result, Pose3ToPoseString(SensorPoseToSdf(pose, type)));

  if (sensor.update_rate() > 0) {
    absl::SubstituteAndAppend(&result, "<update_rate>$0</update_rate>",
                              FullPrecision(sensor.update_rate()));
  }
  if (!sensor.topic().empty()) {
    absl::SubstituteAndAppend(&result, "<topic>$0</topic>",
                              EscapeXml(sensor.topic()));
  }

  absl::StrAppend(&result, inner_sdf, "</sensor>");
  return result;
}

absl::StatusOr<std::string> LinkToSdf(
    const Entity& entity, const std::vector<const Entity*>& sensors,
    const absl::flat_hash_map<std::string, const Entity*>& entity_map,
    const SceneObjectToSdfOptions& options) {
  LinkProperties properties;
  properties.name = entity.name();
  properties.parent_name = entity.parent_name();
  INTR_ASSIGN_OR_RETURN(properties.parent_t_link,
                        FromPoseProto(entity.parent_t_this()));

  std::string extra_joint_sdf;
  if (!entity.parent_name().empty()) {
    auto it = entity_map.find(entity.parent_name());
    if (it == entity_map.end()) {
      // Should never happen as SceneObject was validated.
      return absl::InternalError(
          absl::StrCat("Unable to find parent entity `", entity.parent_name(),
                       "` for link: `", entity.name(), "`"));
    }

    // Inserts a fixed joint if the parent is a link.
    if (it->second->has_link()) {
      intrinsic_proto::world::KinematicsComponent kinematics;
      kinematics.set_motion_type(
          intrinsic_proto::world::KinematicsComponent::MOTION_TYPE_FIXED);
      INTR_ASSIGN_OR_RETURN(
          extra_joint_sdf,
          JointSpecToString(
              kinematics,
              JointProperties{.joint_name = absl::StrCat(entity.parent_name(),
                                                         "_to_", entity.name()),
                              .parent_name = entity.parent_name(),
                              .child_name = entity.name()}));

      // We don't modify `properties.parent_name` as the link's pose is still
      // relative to the parent link. We can change the parent to be this fixed
      // joint but then we have to make sure that the joint's pose is not
      // relative to the child link (default in SDF) to avoid pose cycle.
    }

    // Uses outboard_t_child if the parent is a joint.
    if (it->second->has_joint()) {
      const auto& kinematics = it->second->joint().kinematics_component();
      if (kinematics.has_outboard_t_child() &&
          properties.parent_t_link.isApprox(Pose3d())) {
        INTR_ASSIGN_OR_RETURN(properties.parent_t_link,
                              FromPoseProto(kinematics.outboard_t_child()));
      }
    }
  }

  for (const auto* sensor : sensors) {
    if (sensor->has_sensor()) {
      INTR_ASSIGN_OR_RETURN(std::string sensor_sdf, SensorToSdf(*sensor));
      absl::StrAppend(&properties.sensors_sdf, sensor_sdf);
    }
  }

  LinkOptions link_options;
  link_options.inertial_options.enable_auto_inertial = false;
  link_options.save_geopath = options.save_geopath;
  if (!options.save_geopath.empty()) {
    link_options.geometry_deserializer = options.geometry_deserializer;
  }

  INTR_ASSIGN_OR_RETURN(std::string link_sdf,
                        LinkSpecToString(entity.link().geometry_component(),
                                         entity.link().physics_component(),
                                         properties, link_options));
  return extra_joint_sdf + link_sdf;
}

absl::StatusOr<std::string> JointToSdf(
    const Entity& entity,
    const absl::flat_hash_map<std::string, std::string>& joint_to_child,
    const std::vector<const Entity*>& sensors) {
  const auto& kinematics = entity.joint().kinematics_component();

  JointProperties properties;
  properties.joint_name = entity.name();
  properties.parent_name = entity.parent_name();
  auto child_it = joint_to_child.find(entity.name());
  if (child_it != joint_to_child.end()) {
    properties.child_name = child_it->second;
  }

  INTR_ASSIGN_OR_RETURN(properties.child_t_joint,
                        FromPoseProto(kinematics.parent_t_inboard()));
  properties.pose_relative_to = entity.parent_name();

  for (const auto* sensor : sensors) {
    if (sensor->has_sensor()) {
      INTR_ASSIGN_OR_RETURN(std::string sensor_sdf, SensorToSdf(*sensor));
      absl::StrAppend(&properties.sensors_sdf, sensor_sdf);
    }
  }

  return JointSpecToString(kinematics, properties);
}

void SceneObjectKinematicsToSdf(
    const intrinsic_proto::scene_object::v1::Kinematics& kinematics,
    std::string& sdf) {
  if (kinematics.has_limits()) {
    const auto& limits = kinematics.limits();
    bool has_any_limit = false;
    auto check_vec3 = [](const google::protobuf::RepeatedField<double>& v) {
      return v.size() == 3 && (std::isfinite(v[0]) || std::isfinite(v[1]) ||
                               std::isfinite(v[2]));
    };
    auto check_double = [](double v) { return v > 0 && std::isfinite(v); };

    if (check_vec3(limits.min_translational_position()) ||
        check_vec3(limits.max_translational_position()) ||
        check_vec3(limits.min_translational_velocity()) ||
        check_vec3(limits.max_translational_velocity()) ||
        check_vec3(limits.min_translational_acceleration()) ||
        check_vec3(limits.max_translational_acceleration()) ||
        check_vec3(limits.min_translational_jerk()) ||
        check_vec3(limits.max_translational_jerk()) ||
        check_double(limits.max_rotational_velocity()) ||
        check_double(limits.max_rotational_acceleration()) ||
        check_double(limits.max_rotational_jerk())) {
      has_any_limit = true;
    }

    if (has_any_limit) {
      absl::SubstituteAndAppend(&sdf, "<$0>", kCartesianLimitsCustomElement);
      auto append_vec3 = [&](const google::protobuf::RepeatedField<double>& v,
                             absl::string_view tag) {
        if (check_vec3(v)) {
          absl::SubstituteAndAppend(&sdf, "<$0>$1 $2 $3</$0>", tag,
                                    FullPrecision(v[0]), FullPrecision(v[1]),
                                    FullPrecision(v[2]));
        }
      };
      auto append_double = [&](double v, absl::string_view tag) {
        if (check_double(v)) {
          absl::SubstituteAndAppend(&sdf, "<$0>$1</$0>", tag, FullPrecision(v));
        }
      };

      append_vec3(limits.min_translational_position(),
                  kMinTranslationalPositionCustomElement);
      append_vec3(limits.max_translational_position(),
                  kMaxTranslationalPositionCustomElement);
      append_vec3(limits.min_translational_velocity(),
                  kMinTranslationalVelocityCustomElement);
      append_vec3(limits.max_translational_velocity(),
                  kMaxTranslationalVelocityCustomElement);
      append_vec3(limits.min_translational_acceleration(),
                  kMinTranslationalAccelerationCustomElement);
      append_vec3(limits.max_translational_acceleration(),
                  kMaxTranslationalAccelerationCustomElement);
      append_vec3(limits.min_translational_jerk(),
                  kMinTranslationalJerkCustomElement);
      append_vec3(limits.max_translational_jerk(),
                  kMaxTranslationalJerkCustomElement);
      append_double(limits.max_rotational_velocity(),
                    kMaxRotationalVelocityCustomElement);
      append_double(limits.max_rotational_acceleration(),
                    kMaxRotationalAccelerationCustomElement);
      append_double(limits.max_rotational_jerk(),
                    kMaxRotationalJerkCustomElement);

      absl::SubstituteAndAppend(&sdf, "</$0>", kCartesianLimitsCustomElement);
    }
  }
  for (const auto& ik_solver : kinematics.ik_solvers()) {
    absl::SubstituteAndAppend(&sdf, "<$0", kIkSolverCustomElement);
    if (!ik_solver.tip_link_name().empty()) {
      absl::SubstituteAndAppend(&sdf, " $0=\"$1\"", kIkSolverLinkNameAttribute,
                                EscapeXml(ik_solver.tip_link_name()));
    }
    absl::SubstituteAndAppend(&sdf, ">$0</$1>",
                              EscapeXml(ik_solver.ik_solver()),
                              kIkSolverCustomElement);
  }
}

}  // namespace

absl::StatusOr<std::string> SceneObjectToSdf(
    const intrinsic_proto::scene_object::v1::SceneObject& scene_object,
    const SceneObjectToSdfOptions& options) {
  if (!options.save_geopath.empty() &&
      !options.geometry_deserializer.has_value()) {
    return absl::InvalidArgumentError(
        "geometry_deserializer is required when save_geopath is set.");
  }
  INTR_RETURN_IF_ERROR(scene_object::ValidateSceneObject(scene_object));

  absl::flat_hash_map<std::string, const Entity*> entity_map;
  for (const auto& entity : scene_object.entities()) {
    entity_map[entity.name()] = &entity;
  }

  // Keeps track of parent entity to sensor mapping to emit `sensor` xml tag
  // when writing out the parent entity xml tag.
  absl::flat_hash_map<std::string, std::vector<const Entity*>>
      parent_to_sensors;

  // Keeps track of child entities of joints to emit `child` xml tag when
  // writing out `joint` xml tag.
  // A joint should only have one child entity.
  absl::flat_hash_map<std::string, std::string> joint_to_child;

  for (const auto& entity : scene_object.entities()) {
    switch (entity.entity_type_case()) {
      case Entity::kSensor: {
        const auto& parent_name = entity.parent_name();
        if (!parent_name.empty()) {
          auto it = entity_map.find(parent_name);
          if (it == entity_map.end()) {
            // Should never happen given that we have already validated the
            // SceneObject.
            return absl::InternalError(
                absl::StrCat("Failed to find entity with name: ", parent_name));
          }
          if (!it->second->has_joint() && !it->second->has_link()) {
            return absl::InternalError(
                "Sensor should only be parented to a link or a joint");
          }
          parent_to_sensors[parent_name].push_back(&entity);
        }
        break;
      }
      case Entity::kLink: {
        const auto& parent_name = entity.parent_name();
        if (!parent_name.empty()) {
          auto it = entity_map.find(parent_name);
          if (it == entity_map.end()) {
            // Should never happen given that we have already validated the
            // SceneObject.
            return absl::InternalError(
                absl::StrCat("Failed to find entity with name: ", parent_name));
          } else if (it->second->has_joint()) {
            joint_to_child[parent_name] = entity.name();
          }
        }
        break;
      }
      default:
        break;
    }
  }

  std::string sdf = "<?xml version='1.0'?>";
  absl::StrAppend(&sdf, "<sdf version='1.7' xmlns:intrinsic='intrinsic'>");
  absl::SubstituteAndAppend(&sdf, "<model name=\"$0\">",
                            EscapeXml(scene_object.name()));

  // Handles kinematic properties.
  if (scene_object.has_properties() &&
      scene_object.properties().has_kinematics()) {
    SceneObjectKinematicsToSdf(scene_object.properties().kinematics(), sdf);
  }

  // NOTE: we don't set `is_static` in SDF yet. This should happen when we
  // extend the functionality to export from "SceneObject proto + default
  // config" to SDF, and vice versa.

  if (scene_object.has_simulation_spec()) {
    INTR_ASSIGN_OR_RETURN(std::string sim_spec_sdf,
                          SimSpecToSdf(scene_object.simulation_spec()));
    absl::StrAppend(&sdf, sim_spec_sdf);
  }

  if (options.serialize_user_data && !scene_object.user_data().empty()) {
    INTR_ASSIGN_OR_RETURN(std::string user_data_str,
                          scene_object::UserDataToString(
                              scene_object.user_data(), options.user_data_fds));
    // Escapes raw text data in XML.
    // https://en.wikipedia.org/wiki/CDATA
    absl::SubstituteAndAppend(&sdf, "<$0><![CDATA[$1]]></$0>",
                              kUserDataCustomElement, user_data_str);
  }

  for (const auto& entity : scene_object.entities()) {
    switch (entity.entity_type_case()) {
      case Entity::kLink: {
        INTR_ASSIGN_OR_RETURN(
            std::string link_sdf,
            LinkToSdf(entity, parent_to_sensors[entity.name()], entity_map,
                      options));
        absl::StrAppend(&sdf, link_sdf);
        break;
      }
      case Entity::kJoint: {
        INTR_ASSIGN_OR_RETURN(std::string joint_sdf,
                              JointToSdf(entity, joint_to_child,
                                         parent_to_sensors[entity.name()]));
        absl::StrAppend(&sdf, joint_sdf);
        break;
      }
      case Entity::kFrame: {
        INTR_ASSIGN_OR_RETURN(std::string frame_sdf, FrameToSdf(entity));
        absl::StrAppend(&sdf, frame_sdf);
        break;
      }
      case Entity::kSensor:
        // Sensors are handled as part of its parent entity (link or joint).
        break;
      default:
        return absl::InvalidArgumentError(
            absl::StrCat("SceneObject contains invalid entity: ",
                         entity.entity_type_case()));
    }
  }

  absl::StrAppend(&sdf, "</model></sdf>");

  tinyxml2::XMLDocument doc;
  if (doc.Parse(sdf.c_str()) != tinyxml2::XML_SUCCESS) {
    return absl::InternalError(
        absl::StrCat("Failed to parse generated SDF: ", doc.ErrorStr()));
  }
  tinyxml2::XMLPrinter printer(nullptr, /*compact=*/false);
  doc.Print(&printer);
  return std::string(printer.CStr());
}

}  // namespace sdf
}  // namespace intrinsic
