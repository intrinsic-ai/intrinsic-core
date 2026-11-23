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

#include "intrinsic/scene/sdf/entity_from_sdf.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/substitute.h"
#include "intrinsic/geometry/storage/geometry_serializer.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/proto_conversion.h"
#include "intrinsic/scene/proto/v1/entity.pb.h"
#include "intrinsic/scene/sdf/custom_tags.h"
#include "intrinsic/scene/sdf/sdf_path_resolver.h"
#include "intrinsic/scene/sdf/sdf_util.h"
#include "intrinsic/util/status/ret_check.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/component/physics_component.h"
#include "intrinsic/world/conversion/sdf/geometry_component_from_sdf.h"
#include "intrinsic/world/conversion/sdf/joint_conversion.h"
#include "intrinsic/world/conversion/sdf/physics_component_from_sdf.h"
#include "intrinsic/world/conversion/sdf/sensor_conversion.h"
#include "sdf/Link.hh"
#include "sdf/World.hh"

namespace intrinsic {
namespace scene_object {

namespace {

// Creates a scene object entity corresponding to the parsed joint.
absl::StatusOr<intrinsic_proto::scene_object::v1::Entity>
JointEntityFromParsedJoint(const sdf::ParseJointResult& parse_joint_result) {
  intrinsic_proto::scene_object::v1::Entity joint_entity;
  joint_entity.set_name(parse_joint_result.name);
  if (parse_joint_result.parent_name == "world") {
    return absl::InvalidArgumentError(
        absl::Substitute("Cannot convert SDF joint '$0' with parent name "
                         "'world' to a scene object Entity.",
                         parse_joint_result.name));
  }
  joint_entity.set_parent_name(parse_joint_result.parent_name);
  INTR_RET_CHECK(parse_joint_result.parent_t_child.has_value());
  *joint_entity.mutable_parent_t_this() =
      ToProto(parse_joint_result.parent_t_child.value());

  INTR_RET_CHECK_NE(parse_joint_result.kinematics_component, nullptr);
  INTR_ASSIGN_OR_RETURN(
      *joint_entity.mutable_joint()->mutable_kinematics_component(),
      parse_joint_result.kinematics_component->ToProto());
  return joint_entity;
}
}  // namespace

absl::StatusOr<std::optional<intrinsic_proto::scene_object::v1::Entity>>
EntityFromSdfFrame(const ::sdf::Frame& sdf_frame) {
  const bool create_frame =
      sdf::GetAttributeAsBool(sdf_frame.Element(),
                              std::string(sdf::kCreateEntityCustomAttribute))
          .value_or(false);
  const bool create_attachment_frame =
      sdf::GetAttributeAsBool(
          sdf_frame.Element(),
          std::string(sdf::kCreateAttachmentEntityCustomAttribute))
          .value_or(false);

  if (!create_frame && !create_attachment_frame) {
    return std::nullopt;
  }

  std::string body;
  INTR_RETURN_IF_ERROR(sdf::ToStatus(sdf_frame.ResolveAttachedToBody(body)));

  // `Frame::SemanticPose()` already substitutes `attached_to` for an empty
  // `relative_to`, so a frame whose pose is relative to `body` gets its raw
  // pose back exactly.
  INTR_ASSIGN_OR_RETURN(const Pose3d parent_t_this,
                        sdf::ParseSemanticPose(sdf_frame.SemanticPose(), body));

  intrinsic_proto::scene_object::v1::Entity entity;
  entity.set_name(sdf_frame.Name());
  entity.set_parent_name(body);
  *entity.mutable_parent_t_this() = ToProto(parent_t_this);
  entity.mutable_frame();
  if (create_attachment_frame) {
    entity.mutable_frame()->set_is_attachment_frame(true);
  }

  return entity;
}

absl::StatusOr<std::vector<intrinsic_proto::scene_object::v1::Entity>>
EntitiesFromSdfLink(const ::sdf::Link& sdf_link,
                    const sdf::UriResolver& uri_resolver,
                    GeometrySerializer& geometry_serializer) {
  intrinsic_proto::scene_object::v1::Entity link_entity;

  link_entity.set_name(sdf_link.Name());

  INTR_ASSIGN_OR_RETURN(const Pose3d parent_model_t_this,
                        sdf::ParseSemanticPose(sdf_link.SemanticPose()));
  *link_entity.mutable_parent_t_this() = ToProto(parent_model_t_this);

  INTR_ASSIGN_OR_RETURN(
      *link_entity.mutable_link()->mutable_geometry_component(),
      sdf::GeometryComponentFromSdfLink(sdf_link, uri_resolver,
                                        geometry_serializer));

  INTR_ASSIGN_OR_RETURN(std::unique_ptr<PhysicsComponent> physics_component,
                        sdf::PhysicsComponentFromSdfLink(sdf_link));
  INTR_ASSIGN_OR_RETURN(
      *link_entity.mutable_link()->mutable_physics_component(),
      physics_component->ToProto());

  std::vector<intrinsic_proto::scene_object::v1::Entity> entities;
  entities.emplace_back(std::move(link_entity));

  const auto sensor_count = sdf_link.SensorCount();
  for (auto i = 0; i < sensor_count; ++i) {
    const ::sdf::Sensor& sdf_sensor = *sdf_link.SensorByIndex(i);
    INTR_ASSIGN_OR_RETURN(auto sensor_entity, EntityFromSdfSensor(sdf_sensor));
    sensor_entity.set_parent_name(sdf_link.Name());
    entities.emplace_back(std::move(sensor_entity));
  }

  return entities;
}

absl::StatusOr<CreateJointEntitiesResult> EntitiesFromSdfJoint(
    const ::sdf::Joint& sdf_joint, const ::sdf::Link* child_link) {
  CreateJointEntitiesResult result;
  INTR_ASSIGN_OR_RETURN(sdf::ParseJointResult parse_joint_result,
                        sdf::ParseJoint(sdf_joint, child_link));
  INTR_ASSIGN_OR_RETURN(result.joint_entity,
                        JointEntityFromParsedJoint(parse_joint_result));
  result.joint_child_name = std::move(parse_joint_result).child_name;

  const auto sensor_count = sdf_joint.SensorCount();
  for (auto i = 0; i < sensor_count; ++i) {
    const ::sdf::Sensor& sdf_sensor = *sdf_joint.SensorByIndex(i);
    INTR_ASSIGN_OR_RETURN(auto sensor_entity, EntityFromSdfSensor(sdf_sensor));
    sensor_entity.set_parent_name(sdf_joint.Name());
    result.sensor_entities.emplace_back(std::move(sensor_entity));
  }

  return result;
}

absl::StatusOr<intrinsic_proto::scene_object::v1::Entity> EntityFromSdfSensor(
    const ::sdf::Sensor& sdf_sensor) {
  INTR_ASSIGN_OR_RETURN(sdf::ParseSensorResult parse_sensor_result,
                        sdf::ParseSensor(sdf_sensor));
  intrinsic_proto::scene_object::v1::Entity entity;
  entity.set_name(parse_sensor_result.name);
  *entity.mutable_parent_t_this() =
      ToProto(parse_sensor_result.parent_t_sensor);
  INTR_ASSIGN_OR_RETURN(*entity.mutable_sensor()->mutable_sensor_component(),
                        parse_sensor_result.sensor_component->ToProto());

  return entity;
}

}  // namespace scene_object
}  // namespace intrinsic
