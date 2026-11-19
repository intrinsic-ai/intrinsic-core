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

#include "intrinsic/world/service/objects/object_world_updates_utils.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/base/call_once.h"
#include "absl/container/btree_map.h"
#include "absl/flags/flag.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "google/protobuf/message.h"
#include "google/protobuf/repeated_ptr_field.h"
#include "google/protobuf/struct.pb.h"
#include "google/protobuf/wrappers.pb.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/geometry/proto/geometry_service.pb.h"
#include "intrinsic/geometry/storage/geometry_library.h"
#include "intrinsic/icon/proto/cart_space_conversion.h"
#include "intrinsic/kinematics/types/cartesian_limits.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/proto_conversion.h"
#include "intrinsic/scene/proto/v1/scene_object.pb.h"
#include "intrinsic/stats/metrics_utils.h"
#include "intrinsic/util/eigen.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/collision/collision_context.pb.h"
#include "intrinsic/world/collision/util/collision_action_util.h"
#include "intrinsic/world/collision/util/make_rule_set.h"
#include "intrinsic/world/component/geometry_component.h"
#include "intrinsic/world/component/ppr_component.h"
#include "intrinsic/world/entity.h"
#include "intrinsic/world/entity_id.h"
#include "intrinsic/world/geometry_types.h"
#include "intrinsic/world/objects/frame_internal.h"
#include "intrinsic/world/objects/kinematic_object_internal.h"
#include "intrinsic/world/objects/object_entity_filter.h"
#include "intrinsic/world/objects/object_world.h"
#include "intrinsic/world/objects/object_world_ids.h"
#include "intrinsic/world/objects/object_world_proto_utils.h"
#include "intrinsic/world/objects/transform_node_internal.h"
#include "intrinsic/world/objects/world_object_internal.h"
#include "intrinsic/world/proto/collision_action.pb.h"
#include "intrinsic/world/proto/geometry_component.pb.h"
#include "intrinsic/world/proto/object_world_updates.pb.h"
#include "intrinsic/world/proto/world_fragment.pb.h"
#include "intrinsic/world/robot_payload/robot_payload.h"
#include "intrinsic/world/service/objects/object_world_converter.h"
#include "intrinsic/world/world.h"
#include "opentelemetry/metrics/sync_instruments.h"

ABSL_DECLARE_FLAG(bool, always_deserialize_geometries);

namespace intrinsic {
namespace object_world {

using ::intrinsic_proto::world::CreateFrameRequest;
using ::intrinsic_proto::world::CreateObjectRequest;
using ::intrinsic_proto::world::DeleteFrameRequest;
using ::intrinsic_proto::world::DeleteObjectRequest;
using ::intrinsic_proto::world::ObjectSpecFromSceneObject;
using ::intrinsic_proto::world::ObjectWorldUpdate;
using ::intrinsic_proto::world::ObjectWorldUpdates;
using ::intrinsic_proto::world::ReparentFrameRequest;
using ::intrinsic_proto::world::ReparentObjectRequest;
using ::intrinsic_proto::world::ToggleCollisionsRequest;
using ::intrinsic_proto::world::UpdateFrameNameRequest;
using ::intrinsic_proto::world::UpdateFromSceneObject;
using ::intrinsic_proto::world::UpdateKinematicObjectPropertiesRequest;
using ::intrinsic_proto::world::UpdateObjectJointRequest;
using ::intrinsic_proto::world::UpdateObjectJointsRequest;
using ::intrinsic_proto::world::UpdateObjectNameRequest;
using ::intrinsic_proto::world::UpdateObjectRequest;
using ::intrinsic_proto::world::UpdateTransformRequest;

namespace {

// Instruments in this file are intentionally released so they remain alive
// for the program's lifetime, avoiding global destructor ordering errors on
// service destruction. Each is a one-time allocation, so this doesn't leak.
opentelemetry::metrics::Counter<uint64_t>& NameIsGlobalAliasFalseCounter() {
  static opentelemetry::metrics::Counter<uint64_t>* counter =
      stats::GetMeter()
          ->CreateUInt64Counter(
              kNameIsGlobalAliasFalseMeasureName,
              "Counts occurrences where name_is_global_alias is false during "
              "object creation or rename.",
              "1")
          .release();
  return *counter;
}

// TODO(b/475826463): We should make this shared with world_object.cc
// Returns true if the geometry component has any geometries with only v0 refs.
bool NeedsDeserialization(
    const intrinsic_proto::world::GeometryComponent& geo_component) {
  for (const auto& [name, geo_set] : geo_component.named_geometries()) {
    if (geo_set.named_geometries().empty() && !geo_set.geometries().empty()) {
      return true;
    }
  }

  return false;
}

template <typename Container>
absl::Status ReorderDofs(
    const KinematicObject& object, Container& dofs,
    const google::protobuf::RepeatedPtrField<std::string>& joint_names) {
  if (joint_names.empty()) {
    return absl::OkStatus();
  }

  if (dofs.size() != joint_names.size()) {
    return absl::InvalidArgumentError(
        absl::StrCat("Joint values and joint_names have different sizes; ",
                     dofs.size(), " != ", joint_names.size()));
  }

  const auto& world = object.GetEntityWorld();
  INTR_ASSIGN_OR_RETURN(const auto joint_ids, object.GetJointEntityIds());

  if (joint_ids.size() != joint_names.size()) {
    return absl::InvalidArgumentError(
        absl::StrCat("Joint entities and joint_names have different sizes; ",
                     joint_ids.size(), " != ", joint_names.size()));
  }

  absl::btree_map<std::string, size_t> mapping;
  for (size_t i = 0; i < dofs.size(); ++i) {
    mapping[world.GetLocalNameForEntityById(joint_ids[i])] = i;
  }

  Container dof_copy = dofs;
  for (size_t i = 0; i < joint_names.size(); ++i) {
    if (mapping.find(joint_names[i]) == mapping.end()) {
      return absl::InvalidArgumentError(
          absl::StrCat("Cannot find joint with given name: ", joint_names[i]));
    }

    dofs[i] = dof_copy[mapping[joint_names[i]]];
  }

  return absl::OkStatus();
}

absl::Status ReorderJointLimits(
    const KinematicObject& object, intrinsic_proto::JointLimitsUpdate& limits,
    const google::protobuf::RepeatedPtrField<std::string>& joint_names) {
  if (joint_names.empty()) {
    return absl::OkStatus();
  }

  if (limits.has_min_position()) {
    INTR_RETURN_IF_ERROR(ReorderDofs(
        object, *limits.mutable_min_position()->mutable_values(), joint_names))
        << " for min_position limit updates";
  }

  if (limits.has_max_position()) {
    INTR_RETURN_IF_ERROR(ReorderDofs(
        object, *limits.mutable_max_position()->mutable_values(), joint_names))
        << " for max_position limit updates";
  }

  if (limits.has_max_velocity()) {
    INTR_RETURN_IF_ERROR(ReorderDofs(
        object, *limits.mutable_max_velocity()->mutable_values(), joint_names))
        << " for max_velocity limit updates";
  }

  if (limits.has_max_acceleration()) {
    INTR_RETURN_IF_ERROR(ReorderDofs(
        object, *limits.mutable_max_acceleration()->mutable_values(),
        joint_names))
        << " for max_acceleration limit updates";
  }

  if (limits.has_max_jerk()) {
    INTR_RETURN_IF_ERROR(ReorderDofs(
        object, *limits.mutable_max_jerk()->mutable_values(), joint_names))
        << " for max_jerk limit updates";
  }

  if (limits.has_max_effort()) {
    INTR_RETURN_IF_ERROR(ReorderDofs(
        object, *limits.mutable_max_effort()->mutable_values(), joint_names))
        << " for max_effort limit updates";
  }

  return absl::OkStatus();
}

absl::StatusOr<WorldObject*> HandleCreateObjectRequestForSingleEntitySpec(
    const intrinsic_proto::world::ObjectSpecForSingleEntityObject& spec,
    const WorldObjectName& name, const WorldObjectNameType name_type,
    const Pose3d& parent_object_t_created_object, GeometryLibrary* geolib,
    WorldObject& parent_object, ObjectWorld& world) {
  std::unique_ptr<GeometryComponent> geometry_component =
      GeometryComponent::Create();
  if (spec.has_geometry_component()) {
    bool deserialize_geometries =
        absl::GetFlag(FLAGS_always_deserialize_geometries) ||
        NeedsDeserialization(spec.geometry_component());

    if (deserialize_geometries) {
      INTR_ASSIGN_OR_RETURN(geometry_component, GeometryComponent::FromProto(
                                                    spec.geometry_component(),
                                                    geolib->Deserializer()));
      // Reserialize the geometry protos so that we have the protos in place
      // when we might need to write them out. This happens automatically with
      // v1 protos, but any v0 protos will not be present.
      INTR_ASSIGN_OR_RETURN(auto geometry_component_proto,
                            geometry_component->ToProto(&geolib->Serializer()));
      INTR_ASSIGN_OR_RETURN(geometry_component, GeometryComponent::FromProto(
                                                    geometry_component_proto));
    } else {
      INTR_ASSIGN_OR_RETURN(geometry_component, GeometryComponent::FromProto(
                                                    spec.geometry_component()));
    }
  }

  std::optional<std::string> entity_local_name;
  if (!spec.entity_local_name().empty()) {
    entity_local_name = spec.entity_local_name();
  }

  return parent_object.CreateChildObject(
      name, name_type, parent_object_t_created_object, entity_local_name,
      std::move(geometry_component));
}

absl::StatusOr<WorldObject*> HandleCreateObjectRequestFromSceneObject(
    const ObjectSpecFromSceneObject& so_spec, const WorldObjectName& name,
    WorldObjectNameType name_type, const Pose3d& parent_object_t_created_object,
    GeometryLibrary* geolib, WorldObject& parent_object, ObjectWorld& world) {
  if (!so_spec.has_scene_object()) {
    return absl::InvalidArgumentError(
        "ObjectSpecFromSceneObject: No scene object provided");
  }
  INTR_ASSIGN_OR_RETURN(WorldObject * created_object,
                        parent_object.CreateChildObject(
                            name, name_type, parent_object_t_created_object,
                            geolib, so_spec.scene_object()));

  for (const auto& [key, value] : so_spec.user_data()) {
    INTR_ASSIGN_OR_RETURN(auto user_data_map,
                          created_object->GetMutableUserDataMap());
    user_data_map->insert_or_assign(key, value);
  }
  return created_object;
}

absl::StatusOr<WorldObject*> HandleCreateObjectRequestFromExistingObject(
    const intrinsic_proto::world::ObjectSpecFromExistingObject& clone_spec,
    const WorldObjectName& name, const WorldObjectNameType name_type,
    std::optional<Pose3d> parent_object_t_created_object,
    WorldObject& parent_object, ObjectWorld& world) {
  INTR_ASSIGN_OR_RETURN(
      const WorldObject* existing_object,
      GetObjectByReference(world, clone_spec.existing_object()));
  return parent_object.CloneExistingObjectAndAttach(
      *existing_object, name, name_type, clone_spec.clone_frames(),
      parent_object_t_created_object);
}

absl::StatusOr<CreateObjectRequest> BuildCreateFromUpdateObjectRequest(
    ObjectWorld& world, const absl::StatusOr<WorldObject*>& object,
    const UpdateObjectRequest& request) {
  CreateObjectRequest create_request;
  WorldObject* parent_object = nullptr;
  if (object.ok()) {
    create_request.set_name(object.value()->GetName().value());
    parent_object = object.value()->GetParent();
  } else {
    // If the object does not exist, then we can only create if:
    // - `allow_missing` is set to true, and
    // - object name is specified.
    if (!request.update_from_scene_object().allow_missing()) {
      return absl::NotFoundError(
          absl::StrCat("Object '", request.object().by_name().object_name(),
                       "' not found and `allow_missing` is not set to true."));
    }

    absl::string_view name = request.object().by_name().object_name();
    if (name.empty()) {
      return absl::InvalidArgumentError(
          "UpdateObjectRequest must reference object by name as "
          "`allow_missing` is set to true.");
    }
    create_request.set_name(name);

    // Parent to root since the object does not exist.
    INTR_ASSIGN_OR_RETURN(parent_object, world.GetObject(RootObjectName()));
  }
  create_request.set_name_is_global_alias(true);
  *create_request.mutable_create_from_scene_object() =
      request.update_from_scene_object().object_spec_from_scene_object();
  create_request.mutable_parent_object()
      ->mutable_reference()
      ->mutable_by_name()
      ->set_object_name(parent_object->GetName().value());
  create_request.set_world_id(request.world_id());
  return create_request;
}

}  // namespace

bool AnyWorldIdsSet(const ObjectWorldUpdates& world_updates) {
  return absl::c_any_of(world_updates.updates(), [](const auto& update) {
    switch (update.update_case()) {
      case ObjectWorldUpdate::kCreateObject:
        return !update.create_object().world_id().empty();
      case ObjectWorldUpdate::kDeleteObject:
        return !update.delete_object().world_id().empty();
      case ObjectWorldUpdate::kUpdateObjectName:
        return !update.update_object_name().world_id().empty();
      case ObjectWorldUpdate::kUpdateObjectJoints:
        return !update.update_object_joints().world_id().empty();
      case ObjectWorldUpdate::kUpdateObjectJoint:
        return !update.update_object_joint().world_id().empty();
      case ObjectWorldUpdate::kUpdateKinematicObjectProperties:
        return !update.update_kinematic_object_properties().world_id().empty();
      case ObjectWorldUpdate::kUpdateObjectProperties:
        return !update.update_object_properties().world_id().empty();
      case ObjectWorldUpdate::kUpdateEntityProperties:
        return !update.update_entity_properties().world_id().empty();
      case ObjectWorldUpdate::kCreateFrame:
        return !update.create_frame().world_id().empty();
      case ObjectWorldUpdate::kDeleteFrame:
        return !update.delete_frame().world_id().empty();
      case ObjectWorldUpdate::kUpdateFrameName:
        return !update.update_frame_name().world_id().empty();
      case ObjectWorldUpdate::kReparentFrame:
        return !update.reparent_frame().world_id().empty();
      case ObjectWorldUpdate::kUpdateFrameProperties:
        return !update.update_frame_properties().world_id().empty();
      case ObjectWorldUpdate::kUpdateTransform:
        return !update.update_transform().world_id().empty();
      case ObjectWorldUpdate::kReparentObject:
        return !update.reparent_object().world_id().empty();
      case ObjectWorldUpdate::kToggleCollisions:
        return !update.toggle_collisions().world_id().empty();
      case ObjectWorldUpdate::kUpdateCollisionSettings:
        return !update.update_collision_settings().world_id().empty();
      case ObjectWorldUpdate::UPDATE_NOT_SET:
        return false;
    }
  });
}

absl::StatusOr<const WorldObject*> HandleCreateObjectRequest(
    ObjectWorld& world, const CreateObjectRequest& request,
    GeometryLibrary* geolib) {
  INTR_ASSIGN_OR_RETURN(
      WorldObject * parent_object,
      GetObjectByReference(world, request.parent_object().reference()));

  std::optional<Pose3d> parent_object_t_created_object;
  if (request.has_parent_object_t_created_object()) {
    INTR_ASSIGN_OR_RETURN(
        parent_object_t_created_object,
        FromProtoNormalized(request.parent_object_t_created_object()));
  }

  const WorldObjectNameType name_type =
      request.name_is_global_alias()
          ? WorldObjectNameType::kNameIsGlobalAlias
          : WorldObjectNameType::kNameIsNotGlobalAlias;

  WorldObject* new_object = nullptr;
  bool override_exiting_collision_rules = true;
  switch (request.object_spec_case()) {
    case CreateObjectRequest::kCreateSingleEntityObject: {
      INTR_ASSIGN_OR_RETURN(
          new_object, HandleCreateObjectRequestForSingleEntitySpec(
                          request.create_single_entity_object(),
                          WorldObjectName(request.name()), name_type,
                          parent_object_t_created_object.value_or(Pose3d()),
                          geolib, *parent_object, world));
      override_exiting_collision_rules = false;  // No existing rules.
      break;
    }
    case CreateObjectRequest::kCloneFromExistingObject: {
      INTR_ASSIGN_OR_RETURN(new_object,
                            HandleCreateObjectRequestFromExistingObject(
                                request.clone_from_existing_object(),
                                WorldObjectName(request.name()), name_type,
                                std::move(parent_object_t_created_object),
                                *parent_object, world));
      break;
    }
    case CreateObjectRequest::kCreateFromSceneObject: {
      INTR_ASSIGN_OR_RETURN(
          new_object, HandleCreateObjectRequestFromSceneObject(
                          request.create_from_scene_object(),
                          WorldObjectName(request.name()), name_type,
                          parent_object_t_created_object.value_or(Pose3d()),
                          geolib, *parent_object, world));
      break;
    }
    case CreateObjectRequest::OBJECT_SPEC_NOT_SET:
      return absl::InvalidArgumentError(
          "CreateObjectRequest.object_spec must be set.");
  }

  if (new_object == nullptr) {
    return absl::InternalError(
        "CreateObjectRequest did not generate a new object");
  }

  absl::Status reparented =
      request.parent_object().has_entity_filter()
          ? new_object->ReparentTo(
                *new_object->GetParent(),
                FromProto(request.parent_object().entity_filter()))
          : new_object->ReparentTo(*new_object->GetParent());

  if (!reparented.ok()) {
    // Delete the newly created object and return the status.
    INTR_RETURN_IF_ERROR(new_object->DeleteIfNoChildObjects())
        << "when reparenting created object failed with: " << reparented;
    return reparented;
  }

  // If we have a default action set, then apply it to the new entities
  if (request.has_action_for_object_entities()) {
    INTR_ASSIGN_OR_RETURN(auto rule_set, world.GetDefaultCollisionSettings());

    if (!request.action_for_object_entities().has_is_excluded() &&
        !request.action_for_object_entities().has_margin()) {
      return absl::InvalidArgumentError(
          "CollisionAction must have either 'is_excluded' or 'margin' set.");
    }

    // Add a new rule to the ruleset for each entity in the new object
    for (const auto& entity_id : new_object->GetEntityIds()) {
      if (override_exiting_collision_rules) {
        rule_set = SetCollisionActionForEntity(
            rule_set, entity_id, request.action_for_object_entities());
      } else {
        auto* new_rule = rule_set.add_rules();
        new_rule->add_id_1(entity_id.value());
        *new_rule->mutable_action() = request.action_for_object_entities();
      }
    }

    // Set the updated rules back to the entity world.
    INTR_RETURN_IF_ERROR(world.SetDefaultCollisionSettings(rule_set));
  }

  if (!request.user_data().empty()) {
    INTR_ASSIGN_OR_RETURN(auto* user_data_protos,
                          new_object->GetMutableUserDataProtos());
    for (const auto& [key, value] : request.user_data()) {
      user_data_protos->insert({key, value});
    }
  }

  if (!request.name_is_global_alias()) {
    NameIsGlobalAliasFalseCounter().Add(1, {{kUpdateTypeKey, "create_object"}});
  }

  return new_object;
}

absl::Status HandleDeleteObjectRequest(ObjectWorld& world,
                                       const DeleteObjectRequest& request) {
  if (request.reparent_child_objects() && request.force()) {
    return absl::InvalidArgumentError(
        "Only one of 'force' and 'reparent_child_objects' should be set in "
        "DeleteObjectRequest.");
  }

  INTR_ASSIGN_OR_RETURN(WorldObject * object,
                        GetObjectByReference(world, request.object()));

  if (request.reparent_child_objects()) {
    return object->ReparentChildObjectsAndDelete();
  }

  if (request.force()) {
    return object->DeleteIncludingChildObjects();
  } else {
    return object->DeleteIfNoChildObjects();
  }
}

absl::StatusOr<const WorldObject*> HandleUpdateObjectRequest(
    ObjectWorld& world, const UpdateObjectRequest& request,
    GeometryLibrary* geolib) {
  switch (request.update_type_case()) {
    case UpdateObjectRequest::kUpdateFromSceneObject: {
      absl::StatusOr<WorldObject*> object =
          GetObjectByReference(world, request.object());
      absl::StatusOr<CreateObjectRequest> create_object_request =
          BuildCreateFromUpdateObjectRequest(world, object, request);

      // Checks if the policy can be respected.
      const auto policy = request.update_from_scene_object().update_policy();
      switch (policy) {
        case UpdateFromSceneObject::UNSPECIFIED:
        // fallthrough
        case UpdateFromSceneObject::UPDATE_IF_LEAF_OBJECT:
          if (object.ok() && !object.value()->GetChildren().empty()) {
            return absl::InvalidArgumentError(
                absl::StrCat("Object '", object.value()->GetName().value(),
                             "' has child objects and cannot be updated using "
                             "SceneObject spec."));
          }
          break;
        default:
          return absl::InvalidArgumentError(
              absl::StrCat("Unsupported update policy: ", policy));
      }

      if (!create_object_request.ok()) {
        return create_object_request.status();
      }

      // Finally, deletes the old object and creates a new one.
      if (object.ok()) {
        INTR_RETURN_IF_ERROR(object.value()->DeleteIfNoChildObjects())
            .LogError();
      }
      INTR_ASSIGN_OR_RETURN(const WorldObject* new_object,
                            HandleCreateObjectRequest(
                                world, create_object_request.value(), geolib),
                            _.LogError());
      return new_object;
    }
    case UpdateObjectRequest::UPDATE_TYPE_NOT_SET:
      return absl::InvalidArgumentError(
          "UpdateObjectRequest.update_type must be set.");
  }

  return absl::InvalidArgumentError("UpdateObjectRequest.update_type not set.");
}

absl::StatusOr<const WorldObject*> HandleUpdateObjectNameRequest(
    ObjectWorld& world, const UpdateObjectNameRequest& request) {
  INTR_ASSIGN_OR_RETURN(WorldObject * object,
                        GetObjectByReference(world, request.object()));

  INTR_RETURN_IF_ERROR(object->SetName(WorldObjectName(request.name()),
                                       request.name_is_global_alias()));

  if (!request.name_is_global_alias()) {
    NameIsGlobalAliasFalseCounter().Add(
        1, {{kUpdateTypeKey, "update_object_name"}});
  }

  return object;
}

absl::StatusOr<const KinematicObject*> HandleUpdateObjectJointsRequest(
    ObjectWorld& world, const UpdateObjectJointsRequest& request) {
  INTR_ASSIGN_OR_RETURN(KinematicObject * object,
                        GetKinematicObjectByReference(world, request.object()));
  absl::string_view object_name = object->GetName().value();

  if (request.joint_positions_size() == 0 &&
      !request.has_joint_system_limits() &&
      !request.has_joint_application_limits()) {
    return absl::InvalidArgumentError(absl::Substitute(
        "UpdateObjectJointsRequest for object '$0' contained neither "
        "'joint_positions', 'joint_system_limits' nor "
        "'joint_application_limits'.",
        object_name));
  }

  bool enforce_limits_for_set_joints = true;
  if (request.has_joint_system_limits() ||
      request.has_joint_application_limits()) {
    enforce_limits_for_set_joints = false;
  }

  if (request.joint_positions_size() > 0) {
    eigenmath::VectorXd joint_positions =
        RepeatedDoubleToVectorXd(request.joint_positions());
    INTR_RETURN_IF_ERROR(
        ReorderDofs(*object, joint_positions, request.joint_names()));

    INTR_RETURN_IF_ERROR(object->SetJointPositions(
                             joint_positions, enforce_limits_for_set_joints))
            .SetPrepend()
        << "Invalid 'joint_positions' in UpdateObjectJointsRequest for object "
        << object_name << ". ";
  }

  if (request.has_joint_application_limits()) {
    intrinsic_proto::JointLimitsUpdate application_limits =
        request.joint_application_limits();
    INTR_RETURN_IF_ERROR(
        ReorderJointLimits(*object, application_limits, request.joint_names()));

    // We optionally skip enforcing limits for the application limits because
    // the current system limits might not allow this update, however we enforce
    // the limits when applying the system limits because that will ensure that
    // the two are consistent with each other.
    INTR_RETURN_IF_ERROR(
        object->UpdateJointApplicationLimits(
            application_limits, !request.has_joint_system_limits()))
            .SetPrepend()
        << "Invalid 'joint_application_limits' in UpdateObjectJointsRequest "
           "for object "
        << object_name << ". ";
  }
  if (request.has_joint_system_limits()) {
    intrinsic_proto::JointLimitsUpdate system_limits =
        request.joint_system_limits();
    INTR_RETURN_IF_ERROR(
        ReorderJointLimits(*object, system_limits, request.joint_names()));

    INTR_RETURN_IF_ERROR(
        object->UpdateJointSystemLimits(system_limits, /*enforce_limits=*/true))
            .SetPrepend()
        << "Invalid 'joint_system_limits' in UpdateObjectJointsRequest for "
           "object "
        << object_name << ". ";
  }

  return object;
}

absl::StatusOr<const KinematicObject*> HandleUpdateObjectJointRequest(
    ObjectWorld& world, const UpdateObjectJointRequest& request) {
  INTR_ASSIGN_OR_RETURN(KinematicObject * object,
                        GetKinematicObjectByReference(world, request.object()));

  if (!request.has_joint_position()) {
    return object;
  }

  INTR_ASSIGN_OR_RETURN(eigenmath::VectorXd joint_positions,
                        object->GetJointPositions());

  const auto& entity_world = object->GetEntityWorld();
  INTR_ASSIGN_OR_RETURN(const auto joint_ids, object->GetJointEntityIds());
  std::optional<size_t> target_joint_idx;
  for (size_t i = 0; i < joint_positions.size(); ++i) {
    std::string joint_local_name =
        entity_world.GetLocalNameForEntityById(joint_ids[i]);
    if (joint_local_name == request.joint_name()) {
      target_joint_idx = i;
    }
  }

  if (!target_joint_idx.has_value()) {
    return absl::NotFoundError(absl::StrCat(
        "Joint with name `", request.joint_name(), "` was not found."));
  }

  joint_positions[target_joint_idx.value()] = request.joint_position();
  INTR_RETURN_IF_ERROR(object->SetJointPositions(joint_positions));

  return object;
}

absl::StatusOr<const KinematicObject*>
HandleUpdateKinematicObjectPropertiesRequest(
    ObjectWorld& world, const UpdateKinematicObjectPropertiesRequest& request) {
  INTR_ASSIGN_OR_RETURN(KinematicObject * object,
                        GetKinematicObjectByReference(world, request.object()));

  for (const std::string& config_name :
       request.named_joint_configurations_to_remove()) {
    INTR_RETURN_IF_ERROR(object->RemoveNamedJointConfiguration(config_name));
  }

  for (const intrinsic_proto::world::NamedJointConfiguration& config :
       request.named_joint_configurations_to_set()) {
    INTR_RETURN_IF_ERROR(object->SetNamedJointConfiguration(
        config.name(), RepeatedDoubleToVectorXd(config.joint_positions())));
  }

  if (request.has_cartesian_limits()) {
    INTR_ASSIGN_OR_RETURN(
        CartesianLimits cartesian_limits,
        intrinsic::icon::FromProto(request.cartesian_limits()));
    INTR_RETURN_IF_ERROR(object->SetCartesianLimits(cartesian_limits));
  }

  if (request.has_mounted_payload()) {
    INTR_ASSIGN_OR_RETURN(RobotPayload mounted_payload,
                          intrinsic::FromProto(request.mounted_payload()));
    INTR_RETURN_IF_ERROR(object->SetMountedPayload(mounted_payload));
  }

  if (request.has_robot_kinematics_update()) {
    if (!request.robot_kinematics_update().ik_solver_key().empty()) {
      INTR_RETURN_IF_ERROR(object->SetRobotIKSolverKey(
          request.robot_kinematics_update().ik_solver_key()));
    }
    if (request.robot_kinematics_update()
            .robot_entity_updates()
            .entities_size() > 0) {
      std::vector<std::pair<world::ObjectEntityFilter, Pose3d>>
          robot_entity_updates;
      for (const auto& entity_update : request.robot_kinematics_update()
                                           .robot_entity_updates()
                                           .entities()) {
        INTR_ASSIGN_OR_RETURN(
            Pose3d pose, FromProtoNormalized(entity_update.parent_t_entity()));
        robot_entity_updates.push_back(
            {world::ObjectEntityFilter::FromEntityNames(
                 {entity_update.entity_name()}),
             pose});
      }
      INTR_RETURN_IF_ERROR(object->UpdateRobotKinematics(robot_entity_updates));
    }
  }
  return object;
}

absl::StatusOr<const WorldObject*> HandleUpdateObjectPropertiesRequest(
    ObjectWorld& world,
    const intrinsic_proto::world::UpdateObjectPropertiesRequest& request,
    GeometryLibrary* geolib) {
  INTR_ASSIGN_OR_RETURN(WorldObject * object,
                        GetObjectByReference(world, request.object()));

  if (request.has_simulation_component()) {
    INTR_RETURN_IF_ERROR(
        object->SetSimulationComponent(request.simulation_component()));
  }

  // Apply the geometry option overrides to all entities within the object.
  if (request.has_geometry_option_overrides()) {
    if (geolib == nullptr) {
      return absl::InvalidArgumentError(
          "Updating geometry options requires a deserializer");
    }
    INTR_RETURN_IF_ERROR(object->ApplyGeometryOptionOverrides(
        request.geometry_option_overrides(), *geolib));
  }

  if (request.has_user_data()) {
    INTR_RETURN_IF_ERROR(object->ApplyUserData(request.user_data()));
  }

  return object;
}

absl::StatusOr<AttachmentEntityId> HandleUpdateEntityPropertiesRequest(
    ObjectWorld& world,
    const intrinsic_proto::world::UpdateEntityPropertiesRequest& request,
    GeometryLibrary* geolib) {
  INTR_ASSIGN_OR_RETURN(
      const WorldObject* object,
      GetObjectByReference(world, request.entity().reference()));
  INTR_ASSIGN_OR_RETURN(AttachmentEntityId entity_id,
                        object->GetTransformEntityId(
                            FromProto(request.entity().entity_filter())));

  // Updates of individual components: Note that we use the component protos
  // coming from the client as they are. We don't have any special treatment
  // (yet) for cases where the client uses the internal/unstripped proto
  // definition and sets fields that are stripped out in the public version of
  // the proto.

  if (request.has_collision_action()) {
    INTR_ASSIGN_OR_RETURN(auto rule_set, world.GetDefaultCollisionSettings());

    // Update the rule set with the new action.
    const auto updated_rule_set = SetCollisionActionForEntity(
        rule_set, entity_id, request.collision_action());

    // Set the updated rules back to the entity world.
    INTR_RETURN_IF_ERROR(world.SetDefaultCollisionSettings(updated_rule_set));
  }

  if (request.has_physics_component()) {
    LOG(WARNING) << "Ignoring deprecated physics component update.";
  }

  if (request.has_geometry_component()) {
    if (NeedsDeserialization(request.geometry_component())) {
      INTR_ASSIGN_OR_RETURN(
          auto geometry_component,
          GeometryComponent::FromProto(request.geometry_component(),
                                       geolib->Deserializer()));
      INTR_ASSIGN_OR_RETURN(auto geometry_component_proto,
                            geometry_component->ToProto(&geolib->Serializer()));
      INTR_RETURN_IF_ERROR(world.UpdateEntityComponentFromProto(
          entity_id, geometry_component_proto));
    } else {
      INTR_RETURN_IF_ERROR(world.UpdateEntityComponentFromProto(
          entity_id, request.geometry_component()));
    }
  }

  // Apply the geometry option overrides to the given entity.
  if (request.has_geometry_option_overrides()) {
    if (geolib == nullptr) {
      return absl::InvalidArgumentError(
          "Updating geometry options requires a deserializer");
    }
    INTR_RETURN_IF_ERROR(world.ApplyGeometryOptionOverrides(
        entity_id, request.geometry_option_overrides(), *geolib));
  }

  return entity_id;
}

absl::StatusOr<const Frame*> HandleCreateFrameRequest(
    ObjectWorld& world, const CreateFrameRequest& request) {
  INTR_ASSIGN_OR_RETURN(Pose3d parent_t_new_frame,
                        FromProtoNormalized(request.parent_t_new_frame()));
  FrameName new_frame_name(request.new_frame_name());

  Frame* frame = nullptr;
  switch (request.attach_to_case()) {
    case CreateFrameRequest::AttachToCase::kParentObject: {
      INTR_ASSIGN_OR_RETURN(
          WorldObject * parent_object,
          GetObjectByReference(world, request.parent_object()));
      INTR_ASSIGN_OR_RETURN(frame, parent_object->CreateChildFrame(
                                       new_frame_name, parent_t_new_frame));
    } break;
    case CreateFrameRequest::AttachToCase::kParentObjectWithFilter: {
      INTR_ASSIGN_OR_RETURN(
          WorldObject * parent_object,
          GetObjectByReference(
              world, request.parent_object_with_filter().reference()));
      if (!request.parent_object_with_filter().has_entity_filter()) {
        INTR_ASSIGN_OR_RETURN(frame, parent_object->CreateChildFrame(
                                         new_frame_name, parent_t_new_frame));
        break;
      }

      const world::ObjectEntityFilter entity_filter =
          world::ObjectEntityFilter::FromProto(
              request.parent_object_with_filter().entity_filter());
      INTR_ASSIGN_OR_RETURN(
          frame, parent_object->CreateChildFrame(new_frame_name, entity_filter,
                                                 Pose3d()));
      if (const Frame* parent_frame = frame->GetParentFrame();
          parent_frame != nullptr) {
        INTR_RETURN_IF_ERROR(
            frame->SetTransform(parent_frame, frame, parent_t_new_frame));
      } else {
        INTR_RETURN_IF_ERROR(
            frame->SetTransform(parent_object, frame, parent_t_new_frame));
      }
    } break;
    case CreateFrameRequest::AttachToCase::kParentFrame: {
      INTR_ASSIGN_OR_RETURN(Frame * parent_frame,
                            GetFrameByReference(world, request.parent_frame()));
      INTR_ASSIGN_OR_RETURN(frame, parent_frame->CreateChildFrame(
                                       new_frame_name, parent_t_new_frame));
    } break;
    default:
    case CreateFrameRequest::AttachToCase::ATTACH_TO_NOT_SET:
      return absl::InvalidArgumentError(
          "CreateFrameRequest must have the field 'attach_to' set.");
  }

  if (frame == nullptr) {
    return absl::InternalError("frame not specified");
  }

  if (request.designate_as_attachment_frame()) {
    INTR_RETURN_IF_ERROR(frame->SetIsAttachmentFrame(true));
  }

  if (request.match_object_asset_name()) {
    INTR_ASSIGN_OR_RETURN(std::optional<std::string> asset_name,
                          frame->GetParent()->GetResourceName());
    if (asset_name.has_value()) {
      AttachmentEntityId frame_entity_id = frame->GetEntityId();
      INTR_ASSIGN_OR_RETURN(
          const WorldEntity* frame_entity,
          world.GetEntityWorld().GetEntityById(frame_entity_id));
      INTR_ASSIGN_OR_RETURN(auto* ppr,
                            const_cast<WorldEntity*>(frame_entity)
                                ->GetOrCreateComponent<PPRComponent>());
      ppr->SetResourceName(*asset_name);
    }
  }

  return frame;
}

namespace {

absl::StatusOr<std::string> GetFrameAssetName(const ObjectWorld& world,
                                              const Frame* frame) {
  INTR_ASSIGN_OR_RETURN(AttachmentEntityId entity_id,
                        frame->GetTransformOriginEntityId());
  INTR_ASSIGN_OR_RETURN(const WorldEntity* entity,
                        world.GetEntityWorld().GetEntityById(entity_id));
  absl::StatusOr<const PPRComponent*> ppr =
      entity->GetComponent<PPRComponent>();
  if (ppr.ok()) {
    std::optional<absl::string_view> resource_name = (*ppr)->ResourceName();
    if (resource_name.has_value() && !resource_name->empty()) {
      return std::string(*resource_name);
    }
  }

  return "";
}

}  // namespace

absl::Status HandleDeleteFrameRequest(ObjectWorld& world,
                                      const DeleteFrameRequest& request,
                                      bool disable_asset_frame_edits) {
  INTR_ASSIGN_OR_RETURN(Frame * frame,
                        GetFrameByReference(world, request.frame()));
  if (disable_asset_frame_edits) {
    INTR_ASSIGN_OR_RETURN(std::string frame_resource_name,
                          GetFrameAssetName(world, frame));
    if (!frame_resource_name.empty()) {
      return absl::InvalidArgumentError(absl::Substitute(
          "Cannot delete frame '$0': frame is defined in asset instance '$1'",
          frame->GetName(), frame_resource_name));
    }
  }
  if (request.force()) {
    return frame->DeleteIncludingChildFrames();
  }
  return frame->DeleteIfNoChildFrames();
}

absl::StatusOr<const Frame*> HandleUpdateFrameNameRequest(
    ObjectWorld& world, const UpdateFrameNameRequest& request,
    bool disable_asset_frame_edits) {
  INTR_ASSIGN_OR_RETURN(Frame * frame,
                        GetFrameByReference(world, request.frame()));

  if (disable_asset_frame_edits) {
    INTR_ASSIGN_OR_RETURN(std::string frame_resource_name,
                          GetFrameAssetName(world, frame));
    if (!frame_resource_name.empty()) {
      return absl::InvalidArgumentError(absl::Substitute(
          "Cannot rename frame '$0': frame is defined in asset instance '$1'",
          frame->GetName(), frame_resource_name));
    }
  }

  INTR_RETURN_IF_ERROR(frame->SetName(FrameName(request.name())));

  return frame;
}

absl::StatusOr<const Frame*> HandleReparentFrameRequest(
    ObjectWorld& world, const ReparentFrameRequest& request,
    bool disable_asset_frame_edits) {
  INTR_ASSIGN_OR_RETURN(Frame * frame,
                        GetFrameByReference(world, request.frame()));

  if (disable_asset_frame_edits) {
    INTR_ASSIGN_OR_RETURN(std::string frame_resource_name,
                          GetFrameAssetName(world, frame));
    if (!frame_resource_name.empty()) {
      return absl::InvalidArgumentError(absl::Substitute(
          "Cannot reparent frame '$0': frame parent is defined in asset '$1'",
          frame->GetName(), frame_resource_name));
    }
  }

  switch (request.attach_to_case()) {
    case ReparentFrameRequest::AttachToCase::kParentObject: {
      INTR_ASSIGN_OR_RETURN(
          WorldObject * object,
          GetObjectByReference(world, request.parent_object().reference()));
      auto filter = world::ObjectEntityFilter::BaseEntity();
      if (request.parent_object().has_entity_filter()) {
        filter = FromProto(request.parent_object().entity_filter());
      }
      INTR_RETURN_IF_ERROR(frame->ReparentTo(*object, filter));
      break;
    }
    case ReparentFrameRequest::AttachToCase::kParentFrame: {
      INTR_ASSIGN_OR_RETURN(Frame * parent_frame,
                            GetFrameByReference(world, request.parent_frame()));
      INTR_RETURN_IF_ERROR(frame->ReparentTo(*parent_frame));
      break;
    }
    case ReparentFrameRequest::AttachToCase::ATTACH_TO_NOT_SET:
      return absl::InvalidArgumentError(
          "ReparentFrameRequest.attach_to not set.");
  }

  return frame;
}

absl::StatusOr<const Frame*> HandleUpdateFramePropertiesRequest(
    ObjectWorld& world,
    const intrinsic_proto::world::UpdateFramePropertiesRequest& request) {
  INTR_ASSIGN_OR_RETURN(Frame * frame,
                        GetFrameByReference(world, request.frame()));

  if (request.has_is_attachment_frame()) {
    INTR_RETURN_IF_ERROR(
        frame->SetIsAttachmentFrame(request.is_attachment_frame().value()));
  }

  return frame;
}

absl::StatusOr<const TransformNode*> HandleUpdateTransformRequest(
    ObjectWorld& world, const UpdateTransformRequest& request) {
  INTR_ASSIGN_OR_RETURN(TransformNode * node_a,
                        GetTransformNodeByReference(world, request.node_a()));
  INTR_ASSIGN_OR_RETURN(TransformNode * node_b,
                        GetTransformNodeByReference(world, request.node_b()));

  std::optional<world::ObjectEntityFilter> node_a_filter;
  if (request.has_node_a_filter()) {
    node_a_filter = FromProto(request.node_a_filter());
  }

  std::optional<world::ObjectEntityFilter> node_b_filter;
  if (request.has_node_b_filter()) {
    node_b_filter = FromProto(request.node_b_filter());
  }

  std::optional<world::ObjectEntityFilter> node_to_update_filter;
  TransformNode* node_to_update;
  if (request.has_node_to_update()) {
    INTR_ASSIGN_OR_RETURN(node_to_update, GetTransformNodeByReference(
                                              world, request.node_to_update()));
  } else if (node_a->GetParent() == node_b) {
    node_to_update = node_a;
    node_to_update_filter = node_a_filter;
  } else if (node_b->GetParent() == node_a) {
    node_to_update = node_b;
    node_to_update_filter = node_b_filter;
  } else {
    return absl::InvalidArgumentError(
        "Cannot decide which node to update. 'node_to_update' was omitted in "
        "the request but the given 'node_a' and 'node_b' are not neighbors "
        "in the transform tree.");
  }
  INTR_ASSIGN_OR_RETURN(Pose3d a_t_b, FromProtoNormalized(request.a_t_b()));

  INTR_RETURN_IF_ERROR(
      node_to_update->SetTransform(node_to_update_filter, node_a, node_a_filter,
                                   node_b, node_b_filter, a_t_b, absl::Now()));

  return node_to_update;
}

absl::StatusOr<const WorldObject*> HandleReparentObjectRequest(
    ObjectWorld& world, const ReparentObjectRequest& original_request) {
  INTR_ASSIGN_OR_RETURN(WorldObject * object,
                        GetObjectByReference(world, original_request.object()));

  ReparentObjectRequest request = original_request;
  if (original_request.has_new_parent() &&
      !original_request.has_parent_object()) {
    *request.mutable_parent_object() = original_request.new_parent();
  }

  world::ObjectEntityFilter filter = world::ObjectEntityFilter::BaseEntity();
  WorldObject* new_parent = nullptr;
  switch (request.attach_to_case()) {
    case intrinsic_proto::world::ReparentObjectRequest::kParentObject: {
      INTR_ASSIGN_OR_RETURN(
          new_parent,
          GetObjectByReference(world, request.parent_object().reference()));
      if (request.parent_object().has_entity_filter()) {
        filter = FromProto(request.parent_object().entity_filter());
      }
      break;
    }
    case intrinsic_proto::world::ReparentObjectRequest::kParentFrame: {
      INTR_ASSIGN_OR_RETURN(Frame * parent_frame,
                            GetFrameByReference(world, request.parent_frame()));
      new_parent = parent_frame->GetParent();
      filter = world::ObjectEntityFilter::FromEntityNames(
          {parent_frame->GetName().value()});
      break;
    }
    default: {
      return absl::InvalidArgumentError("No parent specified");
    }
  }

  INTR_RETURN_IF_ERROR(object->ReparentTo(*new_parent, filter));

  return object;
}

absl::StatusOr<std::pair<const WorldObject*, const WorldObject*>>
HandleToggleCollisionsRequest(ObjectWorld& world,
                              const ToggleCollisionsRequest& request) {
  INTR_ASSIGN_OR_RETURN(
      WorldObject * object_a,
      GetObjectByReference(world, request.object_a().reference()));
  INTR_ASSIGN_OR_RETURN(
      WorldObject * object_b,
      GetObjectByReference(world, request.object_b().reference()));

  if (request.toggle_mode() ==
      intrinsic_proto::world::ToggleMode::TOGGLE_MODE_ENABLE) {
    INTR_RETURN_IF_ERROR(object_a->EnableCollisionsWith(
        *object_b, FromProto(request.object_a().entity_filter()),
        FromProto(request.object_b().entity_filter())));
  } else if (request.toggle_mode() ==
             intrinsic_proto::world::ToggleMode::TOGGLE_MODE_DISABLE) {
    INTR_RETURN_IF_ERROR(object_a->DisableCollisionsWith(
        *object_b, FromProto(request.object_a().entity_filter()),
        FromProto(request.object_b().entity_filter())));
  } else {
    return absl::InvalidArgumentError("No toggle_mode specified.");
  }

  return std::make_pair(object_a, object_b);
}

absl::Status HandleUpdateCollisionSettingsRequest(
    ObjectWorld& world,
    const intrinsic_proto::world::UpdateCollisionSettingsRequest& request) {
  switch (request.type_case()) {
    case intrinsic_proto::world::UpdateCollisionSettingsRequest::
        kCollisionSettings: {
      INTR_ASSIGN_OR_RETURN(auto rule_set,
                            MakeRuleSet(request.collision_settings(), world));
      return world.SetDefaultCollisionSettings(rule_set);
    } break;
    case intrinsic_proto::world::UpdateCollisionSettingsRequest::TYPE_NOT_SET: {
      return absl::InvalidArgumentError(
          "No update set for UpdateCollisionSettingsRequest");
    }
  }

  return absl::InvalidArgumentError(
      "Unknown update set for UpdateCollisionSettingsRequest");
}

absl::Status HandleObjectWorldUpdate(ObjectWorld& world,
                                     const ObjectWorldUpdate& update,
                                     GeometryLibrary* geolib,
                                     bool disable_asset_frame_edits) {
  switch (update.update_case()) {
    case ObjectWorldUpdate::kCreateObject:
      return HandleCreateObjectRequest(world, update.create_object(), geolib)
          .status();
    case ObjectWorldUpdate::kDeleteObject:
      return HandleDeleteObjectRequest(world, update.delete_object());
    case ObjectWorldUpdate::kUpdateObjectName:
      return HandleUpdateObjectNameRequest(world, update.update_object_name())
          .status();
    case ObjectWorldUpdate::kUpdateObjectJoints:
      return HandleUpdateObjectJointsRequest(world,
                                             update.update_object_joints())
          .status();
    case ObjectWorldUpdate::kUpdateObjectJoint:
      return HandleUpdateObjectJointRequest(world, update.update_object_joint())
          .status();
    case ObjectWorldUpdate::kUpdateKinematicObjectProperties:
      return HandleUpdateKinematicObjectPropertiesRequest(
                 world, update.update_kinematic_object_properties())
          .status();
    case ObjectWorldUpdate::kUpdateObjectProperties:
      return HandleUpdateObjectPropertiesRequest(
                 world, update.update_object_properties(), geolib)
          .status();
    case ObjectWorldUpdate::kUpdateEntityProperties:
      return HandleUpdateEntityPropertiesRequest(
                 world, update.update_entity_properties(), geolib)
          .status();
    case ObjectWorldUpdate::kCreateFrame:
      return HandleCreateFrameRequest(world, update.create_frame()).status();
    case ObjectWorldUpdate::kDeleteFrame:
      return HandleDeleteFrameRequest(world, update.delete_frame(),
                                      disable_asset_frame_edits);
    case ObjectWorldUpdate::kUpdateFrameName:
      return HandleUpdateFrameNameRequest(world, update.update_frame_name(),
                                          disable_asset_frame_edits)
          .status();
    case ObjectWorldUpdate::kReparentFrame:
      return HandleReparentFrameRequest(world, update.reparent_frame(),
                                        disable_asset_frame_edits)
          .status();
    case ObjectWorldUpdate::kUpdateFrameProperties:
      return HandleUpdateFramePropertiesRequest(
                 world, update.update_frame_properties())
          .status();
    case ObjectWorldUpdate::kUpdateTransform:
      return HandleUpdateTransformRequest(world, update.update_transform())
          .status();
    case ObjectWorldUpdate::kReparentObject:
      return HandleReparentObjectRequest(world, update.reparent_object())
          .status();
    case ObjectWorldUpdate::kToggleCollisions:
      return HandleToggleCollisionsRequest(world, update.toggle_collisions())
          .status();
    case ObjectWorldUpdate::kUpdateCollisionSettings:
      return HandleUpdateCollisionSettingsRequest(
          world, update.update_collision_settings());
    case ObjectWorldUpdate::UPDATE_NOT_SET:
      return absl::InvalidArgumentError(
          "UpdateWorldResourcesRequest may not contain an empty/unset update "
          "request.");
  }
}

absl::Status HandleObjectWorldUpdates(ObjectWorld& world,
                                      const ObjectWorldUpdates& updates,
                                      GeometryLibrary* geolib,
                                      bool disable_asset_frame_edits) {
  if (AnyWorldIdsSet(updates)) {
    return absl::InvalidArgumentError(
        "Nested ObjectWorldUpdate's in UpdateWorldResourcesRequest may not "
        "have a world id set. Use the top-level field 'world_id' in "
        "UpdateWorldResourcesRequest instead.");
  }

  for (const ObjectWorldUpdate& update_request : updates.updates()) {
    INTR_RETURN_IF_ERROR(HandleObjectWorldUpdate(world, update_request, geolib,
                                                 disable_asset_frame_edits));
  }

  return absl::OkStatus();
}

bool IsOnlyStateChange(
    const intrinsic_proto::world::ObjectWorldUpdates& world_updates) {
  for (const auto& world_update : world_updates.updates()) {
    if (!IsOnlyStateChange(world_update)) {
      return false;
    }
  }

  return true;
}

bool IsOnlyStateChange(
    const intrinsic_proto::world::ObjectWorldUpdate& world_update) {
  switch (world_update.update_case()) {
    case ObjectWorldUpdate::kUpdateObjectJoints:
      return !world_update.update_object_joints().joint_positions().empty() &&
             !world_update.update_object_joints()
                  .has_joint_application_limits() &&
             !world_update.update_object_joints().has_joint_system_limits();
    case ObjectWorldUpdate::kUpdateTransform:
      return true;
    default:
      return false;
  }

  return false;
}

void RegisterObjectWorldUpdatesMetrics() {
  static absl::once_flag called;
  absl::call_once(called, []() {
    // Eagerly initialize the instruments to register them.
    NameIsGlobalAliasFalseCounter();
  });
}

}  // namespace object_world
}  // namespace intrinsic
