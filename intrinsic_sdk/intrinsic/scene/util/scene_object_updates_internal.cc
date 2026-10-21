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

#include "intrinsic/scene/util/scene_object_updates_internal.h"

#include <set>
#include <string>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/base/attributes.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "google/protobuf/any.pb.h"
#include "google/protobuf/repeated_ptr_field.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/kinematics/types/joint_limits.pb.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/proto/pose.pb.h"
#include "intrinsic/math/proto_conversion.h"
#include "intrinsic/scene/conversion/scene_object_model_utils.h"
#include "intrinsic/scene/proto/v1/collision_rules.pb.h"
#include "intrinsic/scene/proto/v1/entity.pb.h"
#include "intrinsic/scene/proto/v1/object_properties.pb.h"
#include "intrinsic/scene/proto/v1/scene_object.pb.h"
#include "intrinsic/scene/proto/v1/scene_object_updates.pb.h"
#include "intrinsic/scene/util/geometry_update_util.h"
#include "intrinsic/scene/util/object_user_data.h"
#include "intrinsic/scene/util/scene_object_updates.h"
#include "intrinsic/scene/validate/scene_object_validation.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/geometry_types.h"
#include "intrinsic/world/proto/kinematics_component.pb.h"
#include "intrinsic/world/proto/physics_component.pb.h"

namespace intrinsic {
namespace scene_object {
namespace internal {

using ::intrinsic_proto::scene_object::v1::CartesianLimitsUpdate;
using ::intrinsic_proto::scene_object::v1::CollisionEntityPair;
using ::intrinsic_proto::scene_object::v1::CollisionExclusionRule;
using ::intrinsic_proto::scene_object::v1::CreateFrameUpdate;
using ::intrinsic_proto::scene_object::v1::CreateJointUpdate;
using ::intrinsic_proto::scene_object::v1::DeleteEntityUpdate;
using ::intrinsic_proto::scene_object::v1::Entity;
using ::intrinsic_proto::scene_object::v1::EntityPoseUpdate;
using ::intrinsic_proto::scene_object::v1::GeometryUpdate;
using ::intrinsic_proto::scene_object::v1::NamedConfiguration;
using ::intrinsic_proto::scene_object::v1::RenameEntityUpdate;
using ::intrinsic_proto::scene_object::v1::ReparentEntityUpdate;
using ::intrinsic_proto::scene_object::v1::SceneObject;
using ::intrinsic_proto::scene_object::v1::SceneObjectInstanceUpdate;
using ::intrinsic_proto::scene_object::v1::SceneObjectUpdate;
using ::intrinsic_proto::scene_object::v1::SetIKSolversUpdate;
using ::intrinsic_proto::scene_object::v1::SetNamedConfigurationsUpdate;
using ::intrinsic_proto::scene_object::v1::UpdateCollisionRules;
using ::intrinsic_proto::scene_object::v1::UpdateFrameProperties;
using ::intrinsic_proto::scene_object::v1::UpdateJointsRequest;
using ::intrinsic_proto::scene_object::v1::UpdatePhysicsProperties;
using ::intrinsic_proto::scene_object::v1::UpdateUserData;
using ::intrinsic_proto::world::KinematicsComponent;

absl::StatusOr<SceneObject> ProcessSceneObjectUpdates(
    SceneObject&& object, const SceneObjectUpdate& update,
    UpdateType update_type) {
  switch (update.update_case()) {
    case SceneObjectUpdate::kEntityPose:
      return ProcessSceneObjectUpdate(std::move(object), update.entity_pose(),
                                      update_type);
    case SceneObjectUpdate::kCreateFrame:
      return ProcessSceneObjectUpdate(std::move(object), update.create_frame(),
                                      update_type);
    case SceneObjectUpdate::kDeleteEntity:
      return ProcessSceneObjectUpdate(std::move(object), update.delete_entity(),
                                      update_type);
    case SceneObjectUpdate::kSetNamedConfigurations:
      return ProcessSceneObjectUpdate(
          std::move(object), update.set_named_configurations(), update_type);
    case SceneObjectUpdate::kUpdateJoints:
      return ProcessSceneObjectUpdate(std::move(object), update.update_joints(),
                                      update_type);
    case SceneObjectUpdate::kCartesianLimits:
      return ProcessSceneObjectUpdate(std::move(object),
                                      update.cartesian_limits(), update_type);
    case SceneObjectUpdate::kRenameEntity:
      return ProcessSceneObjectUpdate(std::move(object), update.rename_entity(),
                                      update_type);
    case SceneObjectUpdate::kReparentEntity:
      return ProcessSceneObjectUpdate(std::move(object),
                                      update.reparent_entity(), update_type);
    case SceneObjectUpdate::kUpdateFrameProperties:
      return ProcessSceneObjectUpdate(
          std::move(object), update.update_frame_properties(), update_type);
    case SceneObjectUpdate::kUpdateCollisionRules:
      return ProcessSceneObjectUpdate(std::move(object),
                                      update.update_collision_rules());
    case SceneObjectUpdate::kUpdateUserData:
      return ProcessSceneObjectUpdate(std::move(object),
                                      update.update_user_data());
    case SceneObjectUpdate::kSetIkSolvers:
      return ProcessSceneObjectUpdate(std::move(object),
                                      update.set_ik_solvers());
    case SceneObjectUpdate::kUpdateGeometry:
      return ProcessSceneObjectUpdate(std::move(object),
                                      update.update_geometry());
    case SceneObjectUpdate::kUpdatePhysicsProperties:
      return ProcessSceneObjectUpdate(std::move(object),
                                      update.update_physics_properties());
    case SceneObjectUpdate::kCreateJoint:
      return ProcessSceneObjectUpdate(std::move(object), update.create_joint(),
                                      update_type);
    case SceneObjectUpdate::UPDATE_NOT_SET:
      return absl::InvalidArgumentError(
          "Update not set within SceneObjectUpdate proto.");
  }

  return absl::InvalidArgumentError(absl::StrCat(
      "Unsupported SceneObjectUpdate update type: ", update.update_case()));
}

absl::StatusOr<SceneObject> ProcessSceneObjectUpdates(
    SceneObject&& object, const SceneObjectInstanceUpdate& update) {
  switch (update.update_case()) {
    case SceneObjectInstanceUpdate::kEntityPose:
      return ProcessSceneObjectUpdate(std::move(object), update.entity_pose(),
                                      UpdateType::kObjectInstance);
    case SceneObjectInstanceUpdate::kCreateFrame:
      return ProcessSceneObjectUpdate(std::move(object), update.create_frame(),
                                      UpdateType::kObjectInstance);
    case SceneObjectInstanceUpdate::kSetNamedConfigurations:
      return ProcessSceneObjectUpdate(std::move(object),
                                      update.set_named_configurations(),
                                      UpdateType::kObjectInstance);
    case SceneObjectInstanceUpdate::kUpdateJoints:
      return ProcessSceneObjectUpdate(std::move(object), update.update_joints(),
                                      UpdateType::kObjectInstance);
    case SceneObjectInstanceUpdate::kCartesianLimits:
      return ProcessSceneObjectUpdate(std::move(object),
                                      update.cartesian_limits(),
                                      UpdateType::kObjectInstance);
    case SceneObjectInstanceUpdate::kSetIkSolvers:
      return ProcessSceneObjectUpdate(std::move(object),
                                      update.set_ik_solvers());
    case SceneObjectInstanceUpdate::kUpdateGeometry:
      return ProcessSceneObjectUpdate(std::move(object),
                                      update.update_geometry());
    case SceneObjectInstanceUpdate::kUpdateSimulationProperties:
      return ProcessSceneObjectUpdate(std::move(object),
                                      update.update_simulation_properties());
    case SceneObjectInstanceUpdate::UPDATE_NOT_SET:
      return absl::InvalidArgumentError(
          "Update not set within SceneObjectInstanceUpdate proto.");
  }

  return absl::InvalidArgumentError(
      absl::StrCat("Unsupported SceneObjectInstanceUpdate update type: ",
                   update.update_case()));
}

absl::StatusOr<SceneObject> ProcessSceneObjectUpdate(
    SceneObject&& object, const EntityPoseUpdate& update,
    UpdateType update_type) {
  if (update.entity_name().empty()) {
    return absl::InvalidArgumentError(
        "Entity name not set within EntityPoseUpdate proto.");
  }

  if (update.has_parent_t_this()) {
    // Verify the pose parses before we copy it
    INTR_RETURN_IF_ERROR(FromProtoNormalized(update.parent_t_this()).status());
  } else {
    // No update provided? Just ignore.
    LOG(WARNING) << "EntityPoseUpdate provided for entity '"
                 << update.entity_name() << "' with no pose set!";
    return std::move(object);
  }

  auto entity_iter = absl::c_find_if(
      *object.mutable_entities(),
      [&update](const Entity& e) { return e.name() == update.entity_name(); });
  if (entity_iter == object.mutable_entities()->end()) {
    return absl::NotFoundError(
        absl::StrCat("Entity not found: ", update.entity_name()));
  }

  // Actually do the update now.
  *entity_iter->mutable_parent_t_this() = update.parent_t_this();
  return std::move(object);
}

absl::StatusOr<SceneObject> ProcessSceneObjectUpdate(
    SceneObject&& object, const CreateFrameUpdate& update,
    UpdateType update_type) {
  if (update.new_frame_name().empty()) {
    return absl::InvalidArgumentError("Missing new_frame_name");
  }
  if (update.parent_name().empty()) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Missing parent_name for new frame: ", update.new_frame_name()));
  }

  bool found_parent = false;
  bool found_same_name = false;
  for (const auto& entity : object.entities()) {
    if (entity.name() == update.parent_name()) {
      found_parent = true;
    }
    if (entity.name() == update.new_frame_name()) {
      found_same_name = true;
      break;
    }
  }

  if (found_same_name) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Found existing frame with name: ", update.new_frame_name()));
  }

  // Empty means root, so we only fail if it is not root and we didn't find it.
  if (!update.parent_name().empty() && !found_parent) {
    return absl::NotFoundError(absl::StrCat(
        "Parent entity could not be found: ", update.parent_name()));
  }
  std::string parent_name = update.parent_name();
  // If create frame under root and there are existing entities, we need to set
  // the parent_name to the existing root entity(whose parent_name is "")
  if (parent_name.empty() && !object.entities().empty()) {
    auto root_entity = absl::c_find_if(object.entities(), [](const auto& e) {
      return e.parent_name().empty();
    });
    if (root_entity == object.entities().end()) {
      return absl::InvalidArgumentError(
          "Cannot find root entity from SceneObject to create frame under.");
    }
    parent_name = root_entity->name();
  }

  // Verify the pose parses before we copy it
  Pose parent_t_new_frame;
  if (update.has_parent_t_new_frame()) {
    INTR_ASSIGN_OR_RETURN(parent_t_new_frame,
                          FromProtoNormalized(update.parent_t_new_frame()));
  }

  // Add the new entity
  auto* entity = object.add_entities();
  entity->set_name(update.new_frame_name());
  entity->set_parent_name(parent_name);
  *entity->mutable_parent_t_this() = ToProto(parent_t_new_frame);
  entity->mutable_frame()->set_is_attachment_frame(
      update.designate_as_attachment_frame());

  return std::move(object);
}

namespace {

// Creates a map of Entity name to Entity.
absl::flat_hash_map<std::string, Entity*> CreateEntityLookupMap(
    SceneObject& object) {
  absl::flat_hash_map<std::string, Entity*> entity_map;
  for (auto& entity : *object.mutable_entities()) {
    entity_map[entity.name()] = &entity;
  }
  return entity_map;
}

// Accumulates parent-to-child transforms up the tree to calculate the
// transform of the given entity relative to the root entity.
absl::StatusOr<Pose3d> GetRootTEntity(
    const absl::flat_hash_map<std::string, Entity*>& entity_map,
    absl::string_view entity_name) {
  Pose3d accumulated = Pose3d::Identity();
  std::string current_name = std::string(entity_name);

  while (!current_name.empty()) {
    auto iter = entity_map.find(current_name);
    if (iter == entity_map.end()) {
      return absl::NotFoundError(
          absl::StrCat("Entity not found: ", current_name));
    }
    const Entity* entity = iter->second;

    if (!entity->parent_name().empty()) {
      INTR_ASSIGN_OR_RETURN(const Pose3d parent_T_curr,
                            FromProtoNormalized(entity->parent_t_this()));
      accumulated = parent_T_curr * accumulated;
    }
    current_name = entity->parent_name();
  }
  return accumulated;
}

// Returns true if the entity with `entity_name` is a descendant of the entity
// with `potential_ancestor_name` by traversing up the parent tree.
// Note: An entity is considered a descendant of itself.
bool IsDescendant(const absl::flat_hash_map<std::string, Entity*>& entity_map,
                  absl::string_view entity_name,
                  absl::string_view potential_ancestor_name) {
  std::string current_name = std::string(entity_name);
  while (!current_name.empty()) {
    if (current_name == potential_ancestor_name) {
      return true;
    }
    auto iter = entity_map.find(current_name);
    if (iter == entity_map.end()) {
      return false;
    }
    current_name = iter->second->parent_name();
  }
  return false;
}

void PruneEntityReferences(SceneObject& object,
                           const std::set<std::string>& deleted_names) {
  if (deleted_names.empty()) return;

  // Prune collision rules.
  if (object.has_collision_rules()) {
    auto* collision_rules = object.mutable_collision_rules();

    // Prune exclusion rules.
    for (auto itr = collision_rules->exclusion_rules().begin();
         itr != collision_rules->exclusion_rules().end();) {
      if (deleted_names.contains(
              itr->entity_pair().left_entity().entity_name()) ||
          deleted_names.contains(
              itr->entity_pair().right_entity().entity_name())) {
        itr = collision_rules->mutable_exclusion_rules()->erase(itr);
      } else {
        ++itr;
      }
    }

    if (collision_rules->exclusion_rules().empty()) {
      object.clear_collision_rules();
    }
  }

  // Prune kinematics properties.
  if (object.has_properties() && object.properties().has_kinematics()) {
    auto* kinematics = object.mutable_properties()->mutable_kinematics();

    // Prune IK solvers.
    for (auto itr = kinematics->ik_solvers().begin();
         itr != kinematics->ik_solvers().end();) {
      if (deleted_names.contains(itr->tip_link_name())) {
        itr = kinematics->mutable_ik_solvers()->erase(itr);
      } else {
        ++itr;
      }
    }

    // Prune named configurations.
    for (auto& named_config : *kinematics->mutable_named_configurations()) {
      auto* joint_positions = named_config.mutable_joint_positions();
      for (const auto& name : deleted_names) {
        joint_positions->erase(name);
      }
    }
  }

  // Prune simulation spec.
  if (object.has_simulation_spec()) {
    auto* sim_spec = object.mutable_simulation_spec();
    if (sim_spec->has_robot()) {
      auto* robot = sim_spec->mutable_robot();
      for (auto itr = robot->device_specs().begin();
           itr != robot->device_specs().end();) {
        if (deleted_names.contains(itr->joint_entity())) {
          itr = robot->mutable_device_specs()->erase(itr);
        } else {
          ++itr;
        }
      }
    }
    if (sim_spec->has_multi_camera_plugin()) {
      auto* multi_camera = sim_spec->mutable_multi_camera_plugin();
      for (auto itr = multi_camera->sensors().begin();
           itr != multi_camera->sensors().end();) {
        if (deleted_names.contains(itr->name())) {
          itr = multi_camera->mutable_sensors()->erase(itr);
        } else {
          ++itr;
        }
      }
    }
  }
}

absl::Status DeleteChildrenRecursive(SceneObject& object,
                                     std::set<std::string> children) {
  std::set<std::string> all_deleted_names = children;
  while (!children.empty()) {
    std::set<std::string> more_child_names;
    for (auto itr = object.entities().begin();
         itr != object.entities().end();) {
      // If this entity is a child of a soon to be deleted object we want to
      // mark it for deletion on the next pass.
      if (children.contains(itr->parent_name())) {
        more_child_names.insert(itr->name());
        all_deleted_names.insert(itr->name());
      }

      // If this entity is a child marked for deletion, remove it!
      if (children.contains(itr->name())) {
        itr = object.mutable_entities()->erase(itr);
      } else {
        ++itr;
      }
    }

    children = std::move(more_child_names);
  }

  PruneEntityReferences(object, all_deleted_names);

  return absl::OkStatus();
}
}  // namespace

absl::StatusOr<SceneObject> ProcessSceneObjectUpdate(
    SceneObject&& object, const DeleteEntityUpdate& update,
    UpdateType update_type) {
  if (update.entity_name().empty()) {
    return absl::InvalidArgumentError("Missing entity_name");
  }

  Entity* entity_to_delete = nullptr;
  std::vector<Entity*> children;
  for (auto& entity : *object.mutable_entities()) {
    if (entity.name().empty()) {
      return absl::InternalError("Found an Entity without a name.");
    }

    if (entity.name() == update.entity_name()) {
      entity_to_delete = &entity;
    }
    if (entity.parent_name() == update.entity_name()) {
      if (entity.name().empty()) {
        return absl::InternalError("Found an Entity without a name.");
      }

      children.push_back(&entity);
    }
  }

  if (entity_to_delete == nullptr) {
    return absl::NotFoundError(absl::StrCat("Could not find entity with name: ",
                                            update.entity_name()));
  }

  if (entity_to_delete->parent_name().empty()) {
    return absl::InvalidArgumentError(
        absl::StrCat("Cannot delete the root entity: ", update.entity_name()));
  }

  if (update_type == UpdateType::kObjectInstance) {
    if (!entity_to_delete->has_frame()) {
      return absl::PermissionDeniedError(
          "DeleteEntityUpdate updates are only allowed for frames within "
          "instance configuration.");
    }
  }

  if (!children.empty()) {
    switch (update.child_policy()) {
      case DeleteEntityUpdate::CHILD_POLICY_UNSPECIFIED:
        ABSL_FALLTHROUGH_INTENDED;
      case DeleteEntityUpdate::CHILD_POLICY_FAIL_IF_PRESENT: {
        return absl::InvalidArgumentError(
            absl::StrCat("Entity '", update.entity_name(),
                         "' cannot be removed because it has children."));
      }
      case DeleteEntityUpdate::CHILD_POLICY_DELETE_RECURSIVELY: {
        // Collect the names of the child entities to remove.
        std::set<std::string> child_names;
        for (const auto& child : children) {
          child_names.insert(child->name());
        }

        INTR_RETURN_IF_ERROR(
            DeleteChildrenRecursive(object, std::move(child_names)));

        // We clear out the entity_to_delete because the entities field has been
        // updated and the pointer we have may no longer be valid, so change it
        // to nullptr.
        entity_to_delete = nullptr;
        break;
      }
      case DeleteEntityUpdate::CHILD_POLICY_REPARENT_CHILDREN: {
        INTR_ASSIGN_OR_RETURN(
            const auto parent_t_entity_to_delete,
            FromProtoNormalized(entity_to_delete->parent_t_this()));

        for (auto& child : children) {
          child->set_parent_name(entity_to_delete->parent_name());
          INTR_ASSIGN_OR_RETURN(const auto entity_to_delete_t_child,
                                FromProtoNormalized(child->parent_t_this()));
          const Pose3d parent_t_child =
              parent_t_entity_to_delete * entity_to_delete_t_child;
          *child->mutable_parent_t_this() = ToProto(parent_t_child);
        }
        break;
      }
      default:
        return intrinsic::InvalidArgumentErrorBuilder()
               << "Unsupported child_policy: " << update.child_policy();
    }
  }

  // Delete the actual entity from the list
  bool found_entity = false;
  for (auto itr = object.entities().begin(); itr != object.entities().end();
       ++itr) {
    if (itr->name() == update.entity_name()) {
      object.mutable_entities()->erase(itr);
      found_entity = true;
      break;
    }
  }

  if (!found_entity) {
    return intrinsic::InternalErrorBuilder()
           << "Something went wrong while deleting entity: "
           << update.entity_name();
  }

  PruneEntityReferences(object, {update.entity_name()});

  return std::move(object);
}

absl::StatusOr<SceneObject> ProcessSceneObjectUpdate(
    SceneObject&& object, const SetNamedConfigurationsUpdate& update,
    UpdateType update_type) {
  absl::flat_hash_set<std::string> joint_names;
  std::vector<std::string> joint_names_ordered;
  for (auto& entity : *object.mutable_entities()) {
    if (entity.has_joint() &&
        entity.joint().kinematics_component().motion_type() !=
            intrinsic_proto::world::KinematicsComponent::MOTION_TYPE_FIXED) {
      joint_names.insert(entity.name());
      joint_names_ordered.push_back(entity.name());
    }
  }

  auto* kinematics = object.mutable_properties()->mutable_kinematics();
  if (update.clear_all_named_configurations()) {
    // Clear the named configurations if requested.
    kinematics->clear_named_configurations();

    // For consistency in errors we need to also validate the names of the
    // configurations to remove even if we ignore this list and drop everything.
    for (const auto& config_name : update.named_configurations_to_remove()) {
      if (config_name.empty()) {
        return absl::InvalidArgumentError(
            "Must specify a non empty name for a configuration to remove.");
      }
    }
  } else {
    // Gather the named configurations to remove.
    std::set<std::string> configs_to_remove;
    for (const auto& config_name : update.named_configurations_to_remove()) {
      if (config_name.empty()) {
        return absl::InvalidArgumentError(
            "Must specify a non empty name for a configuration to remove.");
      }

      configs_to_remove.insert(config_name);
    }

    // We also 'remove' any configs we are about to override.
    for (const auto& config : update.named_configurations_to_set()) {
      if (config.name().empty()) {
        return absl::InvalidArgumentError("Named configuration has no name.");
      }

      configs_to_remove.insert(config.name());
    }

    // Go through and remove any matching configurations.
    for (auto itr = kinematics->named_configurations().begin();
         itr != kinematics->named_configurations().end();) {
      if (configs_to_remove.contains(itr->name())) {
        itr = kinematics->mutable_named_configurations()->erase(itr);
      } else {
        ++itr;
      }
    }
  }

  for (const auto& config : update.named_configurations_to_set()) {
    if (config.joint_positions_size() != joint_names.size()) {
      return absl::InvalidArgumentError(absl::Substitute(
          "Named configuration '$0' has the wrong number of joint positions. "
          "Expected '$1' but got '$2'",
          config.name(), joint_names.size(), config.joint_positions_size()));
    }

    for (const auto& [joint_name, joint_position] : config.joint_positions()) {
      if (joint_name.empty()) {
        return absl::InvalidArgumentError(
            absl::StrCat("Named configuration '", config.name(),
                         "' has a joint position with an empty joint name."));
      }

      if (!joint_names.contains(joint_name)) {
        return absl::NotFoundError(absl::Substitute(
            "Joint '$0' not found in SceneObject, object has joints: $1",
            joint_name, absl::StrJoin(joint_names_ordered, ",")));
      }
    }
  }

  // Finally add the new named configurations to the list.
  if (!update.named_configurations_to_set().empty()) {
    kinematics->mutable_named_configurations()->Add(
        update.named_configurations_to_set().begin(),
        update.named_configurations_to_set().end());
  }

  return std::move(object);
}

namespace {

// TODO(stoyang): Move this to a separate file and add individual tests.
absl::Status ApplyJointLimits(
    intrinsic_proto::world::KinematicsComponent::Limits& limits,
    const intrinsic_proto::JointLimitUpdate& update) {
  if (update.has_min_position()) {
    limits.mutable_fixed_limits()->set_lower(update.min_position());
  }
  if (update.has_max_position()) {
    limits.mutable_fixed_limits()->set_upper(update.max_position());
  }
  if (update.has_max_velocity()) {
    if (update.max_velocity() < 0.0) {
      return absl::InvalidArgumentError(
          absl::StrCat("Joint velocity limit must be non-negative: ",
                       update.max_velocity()));
    }
    limits.set_velocity(update.max_velocity());
  }
  if (update.has_max_acceleration()) {
    if (update.max_acceleration() < 0.0) {
      return absl::InvalidArgumentError(
          absl::StrCat("Joint acceleration limit must be non-negative: ",
                       update.max_acceleration()));
    }
    limits.set_acceleration(update.max_acceleration());
  }
  if (update.has_max_jerk()) {
    if (update.max_jerk() < 0.0) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Joint jerk limit must be non-negative: ", update.max_jerk()));
    }
    limits.set_jerk(update.max_jerk());
  }
  if (update.has_max_effort()) {
    if (update.max_effort() < 0.0) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Joint effort limit must be non-negative: ", update.max_effort()));
    }
    limits.set_effort(update.max_effort());
  }

  return absl::OkStatus();
}

}  // namespace

absl::StatusOr<SceneObject> ProcessSceneObjectUpdate(
    SceneObject&& object, const UpdateJointsRequest& update,
    UpdateType update_type) {
  if (update.joint_positions_size() == 0 &&
      update.joint_system_limits_size() == 0 &&
      update.joint_application_limits_size() == 0 &&
      update.parent_t_inboard_size() == 0) {
    return absl::InvalidArgumentError(
        "UpdateJointsRequest contained neither 'joint_positions', "
        "'joint_system_limits', 'joint_application_limits' nor "
        "'parent_t_inboard'.");
  }

  absl::flat_hash_map<std::string, Entity*> all_joints;
  for (auto& entity : *object.mutable_entities()) {
    if (entity.has_joint()) {
      all_joints[entity.name()] = &entity;
    }
  }

  absl::flat_hash_map<std::string, Entity*> movable_joints;
  for (auto& [name, entity] : all_joints) {
    if (entity->joint().kinematics_component().motion_type() !=
        intrinsic_proto::world::KinematicsComponent::MOTION_TYPE_FIXED) {
      movable_joints[name] = entity;
    }
  }

  const int movable_joint_count = movable_joints.size();
  if (movable_joint_count == 0 && update.joint_positions_size() > 0) {
    return absl::InvalidArgumentError(
        "SceneObject has no joints to update positions.");
  }

  if (update.parent_t_inboard_size() > 0) {
    for (const auto& [joint_name, parent_t_inboard_proto] :
         update.parent_t_inboard()) {
      if (!all_joints.contains(joint_name)) {
        return absl::NotFoundError(absl::Substitute(
            "Joint '$0' not found in SceneObject", joint_name));
      }
      INTR_ASSIGN_OR_RETURN(const Pose3d parent_t_inboard,
                            FromProtoNormalized(parent_t_inboard_proto));
      Entity* joint_entity = all_joints[joint_name];
      *joint_entity->mutable_joint()
           ->mutable_kinematics_component()
           ->mutable_parent_t_inboard() = ToProto(parent_t_inboard);
    }
  }

  if (update.joint_positions_size() > 0) {
    if (update.joint_positions_size() != movable_joint_count) {
      return absl::InvalidArgumentError(absl::Substitute(
          "UpdateJointsRequest contained 'joint_positions' with the "
          "wrong number of entries. Expected '$0' but got '$1'",
          movable_joint_count, update.joint_positions_size()));
    }

    for (const auto& [joint_name, joint_position] : update.joint_positions()) {
      if (!movable_joints.contains(joint_name)) {
        return absl::NotFoundError(absl::Substitute(
            "Movable joint '$0' not found in SceneObject", joint_name));
      }

      Entity* joint_entity = movable_joints[joint_name];
      intrinsic_proto::world::KinematicsComponent* kinematics =
          joint_entity->mutable_joint()->mutable_kinematics_component();
      kinematics->set_raw_value(joint_position);

      // If we are not going to set any updated limits we need to ensure that
      // the current raw value is within the existing application and system
      // limits.
      if (update.joint_system_limits().empty() &&
          update.joint_application_limits().empty()) {
        INTR_RETURN_IF_ERROR(ValidateJointLimits(*kinematics));
      }
    }
  }

  // If either the joint positions or the parent_t_inboard are set we need to
  // update the parent_t_this. We do this for ALL joints to ensure consistency.
  if (update.joint_positions_size() > 0 || update.parent_t_inboard_size() > 0) {
    for (const auto& [joint_name, joint_entity] : all_joints) {
      INTR_ASSIGN_OR_RETURN(const Pose3d new_parent_t_this,
                            ResolveJointEntityPose(*joint_entity));
      *joint_entity->mutable_parent_t_this() = ToProto(new_parent_t_this);
    }
  }

  if (!update.joint_application_limits().empty()) {
    if (update.joint_application_limits_size() != movable_joint_count) {
      return absl::InvalidArgumentError(absl::Substitute(
          "UpdateJointsRequest contained 'joint_application_limits' "
          "with the wrong number of entries. Expected '$0' but got '$1'",
          movable_joint_count, update.joint_application_limits_size()));
    }

    for (const auto& [joint_name, application_limit] :
         update.joint_application_limits()) {
      if (!movable_joints.contains(joint_name)) {
        return absl::NotFoundError(absl::Substitute(
            "Movable joint '$0' not found in SceneObject", joint_name));
      }

      Entity* joint_entity = movable_joints[joint_name];
      intrinsic_proto::world::KinematicsComponent* kinematics =
          joint_entity->mutable_joint()->mutable_kinematics_component();
      INTR_RETURN_IF_ERROR(ApplyJointLimits(
          *kinematics->mutable_application_limits(), application_limit));

      // We optionally skip enforcing limits for the application limits
      // because the current system limits might not allow this update,
      // however we enforce the limits when applying the system limits because
      // that will ensure that the two are consistent with each other.
      if (update.joint_system_limits().empty()) {
        INTR_RETURN_IF_ERROR(ValidateJointLimits(*kinematics));
      }
    }
  }

  if (!update.joint_system_limits().empty()) {
    if (update.joint_system_limits_size() != movable_joint_count) {
      return absl::InvalidArgumentError(absl::Substitute(
          "UpdateJointsRequest contained 'joint_system_limits' with the "
          "wrong number of entries. Expected '$0' but got '$1'",
          movable_joint_count, update.joint_system_limits_size()));
    }

    for (const auto& [joint_name, system_limit] :
         update.joint_system_limits()) {
      if (!movable_joints.contains(joint_name)) {
        return absl::NotFoundError(absl::Substitute(
            "Movable joint '$0' not found in SceneObject", joint_name));
      }

      Entity* joint_entity = movable_joints[joint_name];
      intrinsic_proto::world::KinematicsComponent* kinematics =
          joint_entity->mutable_joint()->mutable_kinematics_component();
      INTR_RETURN_IF_ERROR(
          ApplyJointLimits(*kinematics->mutable_system_limits(), system_limit));

      // Ensure we check the joint values and limits that were used as part of
      // the update.
      INTR_RETURN_IF_ERROR(ValidateJointLimits(*kinematics));
    }
  }

  // Finally, validate all joints now that all updates have been applied.
  for (const auto& [joint_name, joint_entity] : all_joints) {
    INTR_RETURN_IF_ERROR(
        ValidateJointLimits(joint_entity->joint().kinematics_component()));
  }

  return std::move(object);
}

absl::StatusOr<SceneObject> ProcessSceneObjectUpdate(
    SceneObject&& object, const CartesianLimitsUpdate& update,
    UpdateType update_type) {
  // We expect the limits to be in sets of 3 for x, y, and z.
  static constexpr int kExpectedSize = 3;

#define LIMIT_UPDATE_FOR_REPEATED_FIELD(field, check_positivity)              \
  do {                                                                        \
    const auto min_data_size = update.min_##field##_size();                   \
    if (min_data_size > 0) {                                                  \
      if (kExpectedSize != min_data_size) {                                   \
        return absl::InvalidArgumentError(                                    \
            absl::StrCat("Number of limit values for 'min_", #field,          \
                         "' does not match expected size; ", min_data_size,   \
                         " != ", kExpectedSize));                             \
      }                                                                       \
      *object.mutable_properties()                                            \
           ->mutable_kinematics()                                             \
           ->mutable_limits()                                                 \
           ->mutable_min_##field() = update.min_##field();                    \
    }                                                                         \
                                                                              \
    const auto max_data_size = update.max_##field##_size();                   \
    if (max_data_size > 0) {                                                  \
      if (kExpectedSize != max_data_size) {                                   \
        return absl::InvalidArgumentError(                                    \
            absl::StrCat("Number of limit values for 'max_", #field,          \
                         "' does not match expected size; ", max_data_size,   \
                         " != ", kExpectedSize));                             \
      }                                                                       \
      *object.mutable_properties()                                            \
           ->mutable_kinematics()                                             \
           ->mutable_limits()                                                 \
           ->mutable_max_##field() = update.max_##field();                    \
    }                                                                         \
                                                                              \
    /* If we had any data we need to ensure the limits are still valid. */    \
    if (max_data_size > 0 || min_data_size > 0) {                             \
      const auto& limit_proto = object.properties().kinematics().limits();    \
      for (int i = 0; i < kExpectedSize; ++i) {                               \
        if (check_positivity && limit_proto.min_##field(i) > 0.0) {           \
          return absl::InvalidArgumentError(absl::StrCat(                     \
              "Limit values for 'min_", #field, "[", i,                       \
              "]' must be non-positive: ", limit_proto.min_##field(i)));      \
        }                                                                     \
        if (check_positivity && limit_proto.max_##field(i) < 0.0) {           \
          return absl::InvalidArgumentError(absl::StrCat(                     \
              "Limit values for 'max_", #field, "[", i,                       \
              "]' must be non-negative: ", limit_proto.max_##field(i)));      \
        }                                                                     \
        if (limit_proto.min_##field(i) > limit_proto.max_##field(i)) {        \
          return absl::InvalidArgumentError(                                  \
              absl::StrCat("Limit values for '[min|max]_", #field,            \
                           "' must be ordered: ", limit_proto.min_##field(i), \
                           " <= ", limit_proto.max_##field(i)));              \
        }                                                                     \
      }                                                                       \
    }                                                                         \
  } while (false)

#define LIMIT_UPDATE_FOR_DOUBLE_FIELD(field)                                \
  do {                                                                      \
    if (update.has_##field()) {                                             \
      if (update.field() <= 0.0) {                                          \
        return absl::InvalidArgumentError(                                  \
            absl::StrCat("Limit values for '", #field,                      \
                         "' must be greater than zero: ", update.field())); \
      }                                                                     \
      object.mutable_properties()                                           \
          ->mutable_kinematics()                                            \
          ->mutable_limits()                                                \
          ->set_##field(update.field());                                    \
    }                                                                       \
  } while (false)

  LIMIT_UPDATE_FOR_REPEATED_FIELD(translational_position, false);
  LIMIT_UPDATE_FOR_REPEATED_FIELD(translational_velocity, true);
  LIMIT_UPDATE_FOR_REPEATED_FIELD(translational_acceleration, true);
  LIMIT_UPDATE_FOR_REPEATED_FIELD(translational_jerk, true);
  LIMIT_UPDATE_FOR_DOUBLE_FIELD(max_rotational_velocity);
  LIMIT_UPDATE_FOR_DOUBLE_FIELD(max_rotational_acceleration);
  LIMIT_UPDATE_FOR_DOUBLE_FIELD(max_rotational_jerk);

#undef LIMIT_UPDATE_FOR_REPEATED_FIELD
#undef LIMIT_UPDATE_FOR_DOUBLE_FIELD

  return std::move(object);
}

absl::StatusOr<SceneObject> ProcessSceneObjectUpdate(
    SceneObject&& object, const RenameEntityUpdate& update,
    UpdateType update_type) {
  if (update.entity_name().empty()) {
    return absl::InvalidArgumentError(
        "Entity name not set within RenameEntityUpdate proto.");
  }
  if (update.new_entity_name().empty()) {
    return absl::InvalidArgumentError(
        "New entity name not set within RenameEntityUpdate proto.");
  }
  if (update.entity_name() == update.new_entity_name()) {
    LOG(WARNING) << "Entity name and new entity name are the same: "
                 << update.entity_name();
    return std::move(object);
  }

  Entity* expected_entity = nullptr;
  for (auto& entity : *object.mutable_entities()) {
    if (entity.name() == update.entity_name()) {
      expected_entity = &entity;
    } else if (entity.name() == update.new_entity_name()) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Entity '", update.new_entity_name(),
          "' is already in use within the scene object. Each entity must have "
          "a unique name within a scene object."));
    }
  }

  if (expected_entity == nullptr) {
    return absl::NotFoundError(
        absl::StrCat("Entity not found: ", update.entity_name()));
  }

  // Actually do the update now.
  expected_entity->set_name(update.new_entity_name());

  // Update any other entities that have this as their parent.
  for (auto& entity : *object.mutable_entities()) {
    if (entity.parent_name() == update.entity_name()) {
      entity.set_parent_name(update.new_entity_name());
    }
  }

  // Update collision rules.
  if (object.has_collision_rules()) {
    auto* collision_rules = object.mutable_collision_rules();
    for (auto& rule : *collision_rules->mutable_exclusion_rules()) {
      auto* pair = rule.mutable_entity_pair();
      if (pair->left_entity().entity_name() == update.entity_name()) {
        pair->mutable_left_entity()->set_entity_name(update.new_entity_name());
      }
      if (pair->right_entity().entity_name() == update.entity_name()) {
        pair->mutable_right_entity()->set_entity_name(update.new_entity_name());
      }
    }
  }

  // Update kinematics properties.
  if (object.has_properties() && object.properties().has_kinematics()) {
    auto* kinematics = object.mutable_properties()->mutable_kinematics();
    for (auto& ik_solver : *kinematics->mutable_ik_solvers()) {
      if (ik_solver.tip_link_name() == update.entity_name()) {
        ik_solver.set_tip_link_name(update.new_entity_name());
      }
    }
    for (auto& named_config : *kinematics->mutable_named_configurations()) {
      auto* joint_positions = named_config.mutable_joint_positions();
      if (joint_positions->contains(update.entity_name())) {
        double value = (*joint_positions)[update.entity_name()];
        joint_positions->erase(update.entity_name());
        (*joint_positions)[update.new_entity_name()] = value;
      }
    }
  }

  // Update simulation spec.
  if (object.has_simulation_spec()) {
    auto* sim_spec = object.mutable_simulation_spec();
    if (sim_spec->has_robot()) {
      auto* robot = sim_spec->mutable_robot();
      for (auto& device_spec : *robot->mutable_device_specs()) {
        if (device_spec.joint_entity() == update.entity_name()) {
          device_spec.set_joint_entity(update.new_entity_name());
        }
      }
    }
    if (sim_spec->has_multi_camera_plugin()) {
      auto* multi_camera = sim_spec->mutable_multi_camera_plugin();
      for (auto& sensor : *multi_camera->mutable_sensors()) {
        if (sensor.name() == update.entity_name()) {
          sensor.set_name(update.new_entity_name());
        }
      }
    }
  }

  return std::move(object);
}

namespace {

bool IsCollisionEntityPairEqual(const CollisionEntityPair& pair1,
                                const CollisionEntityPair& pair2) {
  const auto& left_name1 = pair1.left_entity().entity_name();
  const auto& right_name1 = pair1.right_entity().entity_name();
  const auto& left_name2 = pair2.left_entity().entity_name();
  const auto& right_name2 = pair2.right_entity().entity_name();

  return (left_name1 == left_name2 && right_name1 == right_name2) ||
         (right_name1 == left_name2 && left_name1 == right_name2);
}

template <typename RuleType>
absl::Status ProcessUpdateCollisionRule(
    ::google::protobuf::RepeatedPtrField<RuleType>* mutable_rules,
    const RuleType& updated_rule,
    const UpdateCollisionRules::UpdateCollisionRulesPolicy& policy) {
  switch (policy) {
    case UpdateCollisionRules::POLICY_UNSPECIFIED:
      ABSL_FALLTHROUGH_INTENDED;
    case UpdateCollisionRules::POLICY_APPEND:
      *mutable_rules->Add() = updated_rule;
      break;
    case UpdateCollisionRules::POLICY_REPLACE_OR_APPEND: {
      bool replaced = false;
      for (auto iter = mutable_rules->begin(); iter != mutable_rules->end();
           ++iter) {
        if (IsCollisionEntityPairEqual(iter->entity_pair(),
                                       updated_rule.entity_pair())) {
          *iter = updated_rule;
          replaced = true;
        }
      }

      if (!replaced) {
        *mutable_rules->Add() = updated_rule;
      }
      break;
    }
    case UpdateCollisionRules::POLICY_REMOVE: {
      for (auto iter = mutable_rules->begin(); iter != mutable_rules->end();) {
        if (IsCollisionEntityPairEqual(iter->entity_pair(),
                                       updated_rule.entity_pair())) {
          iter = mutable_rules->erase(iter);
        } else {
          ++iter;
        }
      }

      break;
    }
    case UpdateCollisionRules::POLICY_CLEAR_AND_REPLACE:
      mutable_rules->Clear();
      *mutable_rules->Add() = updated_rule;
      break;
    case UpdateCollisionRules::POLICY_CLEAR_ALL: {
      return absl::InternalError("Clear all should have been handled already.");
    }
    default:
      return absl::InvalidArgumentError(
          absl::StrCat("Unknown UpdateCollisionRules policy: ", policy));
  }

  return absl::OkStatus();
}

}  // namespace

absl::StatusOr<SceneObject> ProcessSceneObjectUpdate(
    intrinsic_proto::scene_object::v1::SceneObject&& object,
    const UpdateCollisionRules& update) {
  if (update.policy() == UpdateCollisionRules::POLICY_CLEAR_ALL) {
    object.clear_collision_rules();
    return std::move(object);
  }

  absl::flat_hash_set<absl::string_view> entity_names;
  for (const auto& entity : object.entities()) {
    entity_names.insert(entity.name());
  }

  switch (update.rule_case()) {
    case UpdateCollisionRules::kExclusionRule: {
      INTR_RETURN_IF_ERROR(ValidateCollisionEntityPair(
          update.exclusion_rule().entity_pair(), entity_names));

      INTR_RETURN_IF_ERROR(ProcessUpdateCollisionRule(
          object.mutable_collision_rules()->mutable_exclusion_rules(),
          update.exclusion_rule(), update.policy()));
      return std::move(object);
    }
    case UpdateCollisionRules::RULE_NOT_SET:
      return absl::InvalidArgumentError("UpdateCollisionRules rule not set.");
    default:
      return absl::InvalidArgumentError(absl::StrCat(
          "Unknown UpdateCollisionRules rule case: ", update.rule_case()));
  }
}

absl::StatusOr<SceneObject> ProcessSceneObjectUpdate(
    SceneObject&& object, const UpdateUserData& update) {
  INTR_RETURN_IF_ERROR(
      MergeSceneObjectUserData(*object.mutable_user_data(), update));
  return std::move(object);
}

absl::StatusOr<SceneObject> ProcessSceneObjectUpdate(
    SceneObject&& object, const SetIKSolversUpdate& update) {
  bool has_non_fixed_joints = absl::c_any_of(object.entities(), [](const Entity&
                                                                       entity) {
    return entity.has_joint() &&
           entity.joint().kinematics_component().motion_type() !=
               intrinsic_proto::world::KinematicsComponent::MOTION_TYPE_FIXED;
  });
  if (!has_non_fixed_joints) {
    return absl::FailedPreconditionError(
        "Scene object does not support setting IK solvers because there are "
        "no non-fixed joints.");
  }
  auto* kinematics = object.mutable_properties()->mutable_kinematics();
  kinematics->clear_ik_solvers();
  for (const auto& ik_solver : update.ik_solvers()) {
    *kinematics->add_ik_solvers() = ik_solver;
  }
  return std::move(object);
}

absl::StatusOr<SceneObject> ProcessSceneObjectUpdate(
    SceneObject&& object, const GeometryUpdate& update) {
  if (update.entity_name().empty()) {
    return absl::InvalidArgumentError(
        "Entity name not set within GeometryUpdate proto.");
  }

  auto entity_iter = absl::c_find_if(
      *object.mutable_entities(),
      [&update](const Entity& e) { return e.name() == update.entity_name(); });
  if (entity_iter == object.mutable_entities()->end()) {
    return absl::NotFoundError(
        absl::StrCat("Entity not found: ", update.entity_name()));
  }

  if (!entity_iter->has_link()) {
    return absl::InvalidArgumentError(
        absl::StrCat("Entity '", update.entity_name(),
                     "' is not a link. GeometryUpdate only applies to links."));
  }

  INTR_RETURN_IF_ERROR(ApplyGeometryUpdate(
      update, *entity_iter->mutable_link()->mutable_geometry_component()));

  return std::move(object);
}

absl::StatusOr<SceneObject> ProcessSceneObjectUpdate(
    SceneObject&& object, const ReparentEntityUpdate& update,
    UpdateType update_type) {
  if (update_type == UpdateType::kObjectInstance) {
    return absl::PermissionDeniedError(
        "ReparentEntityUpdate updates are not allowed within instance "
        "configuration.");
  }
  if (update.entity_name().empty()) {
    return absl::InvalidArgumentError("Missing entity_name");
  }
  if (update.new_parent_name().empty()) {
    return absl::InvalidArgumentError("Missing new_parent_name");
  }
  if (update.entity_name() == update.new_parent_name()) {
    return absl::InvalidArgumentError("Cannot parent an entity to itself");
  }

  auto entity_map = CreateEntityLookupMap(object);

  auto entity_iter = entity_map.find(update.entity_name());
  if (entity_iter == entity_map.end()) {
    return absl::NotFoundError(
        absl::StrCat("Entity not found: ", update.entity_name()));
  }
  auto* entity = entity_iter->second;

  if (entity->parent_name().empty()) {
    return absl::InvalidArgumentError("Cannot reparent the root entity");
  }

  std::string new_parent_name = update.new_parent_name();
  auto parent_iter = entity_map.find(new_parent_name);
  if (parent_iter == entity_map.end()) {
    return absl::NotFoundError(
        absl::StrCat("New parent entity not found: ", new_parent_name));
  }
  const auto* parent = parent_iter->second;

  // Enforce the parenting rules between different Entity types.
  // Refer to the SceneObject Entity proto for more details.
  if (entity->has_frame()) {
    if (!parent->has_link() && !parent->has_frame()) {
      return absl::InvalidArgumentError(
          "You can only reparent a frame to a link or a frame");
    }
  } else if (entity->has_link()) {
    // For now, do not allow reparenting a link to an existing joint. An
    // existing joint may only have one child, and we would break this invariant
    // if we allowed adding another link under it.
    if (!parent->has_link()) {
      return absl::InvalidArgumentError(
          "You can only reparent a link to another link");
    }
  } else if (entity->has_joint()) {
    if (!parent->has_link()) {
      return absl::InvalidArgumentError(
          "You can only reparent a joint to a link");
    }
  } else if (entity->has_sensor()) {
    if (!parent->has_link() && !parent->has_joint()) {
      return absl::InvalidArgumentError(
          "You can only reparent a sensor to a link or a joint");
    }
  }

  if (IsDescendant(entity_map, new_parent_name, update.entity_name())) {
    return absl::InvalidArgumentError(absl::Substitute(
        "Cannot reparent '$0' to itself. '$1' is a child of '$0'.",
        update.entity_name(), new_parent_name));
  }

  Pose3d new_parent_T_entity = Pose3d::Identity();
  // If the new parent is an attachment frame, it automatically snaps to the
  // frame's origin, setting the relative pose to identity. Otherwise, we
  // calculate the relative pose that maintains the current world-space pose.
  if (parent->has_frame() && parent->frame().is_attachment_frame()) {
    new_parent_T_entity = Pose3d::Identity();
  } else {
    INTR_ASSIGN_OR_RETURN(const Pose3d root_T_entity,
                          GetRootTEntity(entity_map, update.entity_name()));
    INTR_ASSIGN_OR_RETURN(const Pose3d root_T_new_parent,
                          GetRootTEntity(entity_map, new_parent_name));
    new_parent_T_entity = root_T_new_parent.inverse() * root_T_entity;
  }

  entity->set_parent_name(new_parent_name);
  *entity->mutable_parent_t_this() = ToProto(new_parent_T_entity);

  return std::move(object);
}

absl::StatusOr<SceneObject> ProcessSceneObjectUpdate(
    SceneObject&& object, const UpdateFrameProperties& update,
    UpdateType update_type) {
  if (update_type == UpdateType::kObjectInstance) {
    return absl::PermissionDeniedError(
        "UpdateFrameProperties updates are not allowed within instance "
        "configuration.");
  }

  if (update.frame_name().empty()) {
    return absl::InvalidArgumentError("Missing frame_name");
  }

  auto entity_iter = absl::c_find_if(
      *object.mutable_entities(),
      [&update](const Entity& e) { return e.name() == update.frame_name(); });
  if (entity_iter == object.mutable_entities()->end()) {
    return absl::NotFoundError(
        absl::StrCat("Entity not found: ", update.frame_name()));
  }

  if (!entity_iter->has_frame()) {
    return absl::InvalidArgumentError(
        absl::StrCat("Entity is not a frame: ", update.frame_name()));
  }

  if (update.has_is_attachment_frame()) {
    entity_iter->mutable_frame()->set_is_attachment_frame(
        update.is_attachment_frame());
  }
  return std::move(object);
}

absl::StatusOr<SceneObject> ProcessSceneObjectUpdate(
    SceneObject&& object, const UpdatePhysicsProperties& update) {
  if (update.entity_name().empty()) {
    return absl::InvalidArgumentError(
        "Entity name not set within UpdatePhysicsProperties proto.");
  }

  auto entity_iter = absl::c_find_if(
      *object.mutable_entities(),
      [&update](const Entity& e) { return e.name() == update.entity_name(); });
  if (entity_iter == object.mutable_entities()->end()) {
    return absl::NotFoundError(
        absl::StrCat("Entity not found: ", update.entity_name()));
  }

  if (!entity_iter->has_link()) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Entity '", update.entity_name(),
        "' is not a link. UpdatePhysicsProperties only applies to links."));
  }

  auto* physics = entity_iter->mutable_link()->mutable_physics_component();

  if (update.has_mass_kg()) {
    physics->set_mass_kg(update.mass_kg());
  }
  if (update.has_this_t_center_of_mass()) {
    INTR_RETURN_IF_ERROR(
        FromProtoNormalized(update.this_t_center_of_mass()).status());
    *physics->mutable_this_t_center_of_mass() = update.this_t_center_of_mass();
  }
  if (update.has_inertia()) {
    *physics->mutable_inertia() = update.inertia();
  }
  if (update.has_friction()) {
    *physics->mutable_friction() = update.friction();
  }
  if (update.has_torsional()) {
    *physics->mutable_torsional() = update.torsional();
  }
  if (update.has_contact_kp()) {
    physics->set_contact_kp(update.contact_kp());
  }
  if (update.has_contact_kd()) {
    physics->set_contact_kd(update.contact_kd());
  }

  return std::move(object);
}

absl::StatusOr<SceneObject> ProcessSceneObjectUpdate(
    SceneObject&& object,
    const intrinsic_proto::scene_object::v1::UpdateSimulationProperties&
        update) {
  if (update.has_is_static()) {
    object.mutable_simulation_spec()->set_is_static(update.is_static());
  }
  if (update.has_is_disabled()) {
    object.mutable_simulation_spec()->set_disabled(update.is_disabled());
  }
  return std::move(object);
}

absl::StatusOr<SceneObject> ProcessSceneObjectUpdate(
    SceneObject&& object, const CreateJointUpdate& update,
    UpdateType update_type) {
  if (update_type == UpdateType::kObjectInstance) {
    return absl::PermissionDeniedError(
        "CreateJointUpdate updates are not allowed within instance "
        "configuration.");
  }

  if (update.new_joint_name().empty()) {
    return absl::InvalidArgumentError("Missing new_joint_name");
  }
  if (update.parent_link_name().empty()) {
    return absl::InvalidArgumentError("Missing parent_link_name");
  }
  if (update.child_link_name().empty()) {
    return absl::InvalidArgumentError("Missing child_link_name");
  }
  if (update.parent_link_name() == update.child_link_name()) {
    return absl::InvalidArgumentError(
        "Parent and child link cannot be the same link.");
  }

  if (!update.has_joint()) {
    return absl::InvalidArgumentError("Missing joint definition.");
  }

  const KinematicsComponent& kinematics = update.joint().kinematics_component();
  if (kinematics.motion_type() == KinematicsComponent::MOTION_TYPE_UNDEFINED) {
    return absl::InvalidArgumentError(
        absl::Substitute("Joint entity '$0' must have a motion type defined.",
                         update.new_joint_name()));
  }
  if (std::isnan(kinematics.raw_value()) ||
      std::isinf(kinematics.raw_value())) {
    return absl::InvalidArgumentError(
        absl::Substitute("Invalid non-finite raw_value for joint '$0'",
                         update.new_joint_name()));
  }
  INTR_RETURN_IF_ERROR(ValidateJointLimits(kinematics));

  const Entity* parent_entity = nullptr;
  Entity* child_entity = nullptr;

  for (Entity& entity : *object.mutable_entities()) {
    if (entity.name() == update.new_joint_name()) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Found existing entity with name: ", update.new_joint_name()));
    }
    if (entity.name() == update.parent_link_name()) {
      parent_entity = &entity;
    }
    if (entity.name() == update.child_link_name()) {
      child_entity = &entity;
    }
  }

  if (parent_entity == nullptr) {
    return absl::NotFoundError(absl::StrCat(
        "Parent link entity could not be found: ", update.parent_link_name()));
  }
  if (!parent_entity->has_link()) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Parent entity '", update.parent_link_name(), "' is not a link."));
  }

  if (child_entity == nullptr) {
    return absl::NotFoundError(absl::StrCat(
        "Child link entity could not be found: ", update.child_link_name()));
  }
  if (!child_entity->has_link()) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Child entity '", update.child_link_name(), "' is not a link."));
  }

  if (child_entity->parent_name() != update.parent_link_name()) {
    return absl::InvalidArgumentError(
        absl::Substitute("Child link '$0' is not parented to parent link '$1'.",
                         update.child_link_name(), update.parent_link_name()));
  }

  if (kinematics.motion_type() == KinematicsComponent::MOTION_TYPE_FIXED) {
    return std::move(object);
  }

  const bool preserve_parent_t_child =
      update.parent_t_child_policy() !=
      CreateJointUpdate::PARENT_T_CHILD_POLICY_DO_NOT_PRESERVE;

  if (preserve_parent_t_child && kinematics.has_parent_t_inboard() &&
      kinematics.has_outboard_t_child()) {
    return absl::InvalidArgumentError(absl::Substitute(
        "Cannot specify both parent_t_inboard and outboard_t_child when "
        "preserving parent_t_child for joint '$0'. To explicitly specify both "
        "transforms without preserving parent_t_child, set "
        "parent_t_child_policy to PARENT_T_CHILD_POLICY_DO_NOT_PRESERVE.",
        update.new_joint_name()));
  }

  Pose3d parent_t_inboard = Pose3d::Identity();
  Pose3d outboard_t_child = Pose3d::Identity();

  if (preserve_parent_t_child) {
    // When inserting a joint between parent_link and child_link, we want to
    // preserve the child link's existing resting pose relative to parent_link
    // at 0 actuation (i.e. T_{parent_inboard} * T_{outboard_child} =
    // T_{parent_child}). Furthermore, child_entity->parent_t_this must remain
    // identity. Read the existing relative transform between parent and child
    // so the unspecified pose can absorb the offset.
    Pose3d parent_t_child = Pose3d::Identity();
    if (child_entity->has_parent_t_this()) {
      INTR_ASSIGN_OR_RETURN(parent_t_child,
                            FromProtoNormalized(child_entity->parent_t_this()));
    }

    if (kinematics.has_parent_t_inboard()) {
      INTR_ASSIGN_OR_RETURN(parent_t_inboard,
                            FromProtoNormalized(kinematics.parent_t_inboard()));
      outboard_t_child = parent_t_inboard.inverse() * parent_t_child;
    } else if (kinematics.has_outboard_t_child()) {
      INTR_ASSIGN_OR_RETURN(outboard_t_child,
                            FromProtoNormalized(kinematics.outboard_t_child()));
      parent_t_inboard = parent_t_child * outboard_t_child.inverse();
    } else {
      parent_t_inboard = parent_t_child;
      outboard_t_child = Pose3d::Identity();
    }
  } else {
    if (kinematics.has_parent_t_inboard()) {
      INTR_ASSIGN_OR_RETURN(parent_t_inboard,
                            FromProtoNormalized(kinematics.parent_t_inboard()));
    }
    if (kinematics.has_outboard_t_child()) {
      INTR_ASSIGN_OR_RETURN(outboard_t_child,
                            FromProtoNormalized(kinematics.outboard_t_child()));
    }
  }

  eigenmath::Vector3d axis = eigenmath::Vector3d::UnitZ();
  if (kinematics.has_axis()) {
    axis = FromProto(kinematics.axis());
    if (!axis.allFinite() || axis.squaredNorm() < 1e-12) {
      return absl::InvalidArgumentError(absl::Substitute(
          "Joint axis must be finite and non-zero for joint '$0'",
          update.new_joint_name()));
    }
    axis.normalize();
  }

  if (kinematics.motion_type() != KinematicsComponent::MOTION_TYPE_REVOLUTE &&
      kinematics.motion_type() != KinematicsComponent::MOTION_TYPE_PRISMATIC) {
    return absl::InvalidArgumentError(absl::Substitute(
        "Unsupported motion type for joint '$0'", update.new_joint_name()));
  }

  KinematicsComponent resolved_kinematics = kinematics;
  *resolved_kinematics.mutable_parent_t_inboard() = ToProto(parent_t_inboard);
  *resolved_kinematics.mutable_outboard_t_child() = ToProto(outboard_t_child);
  if (kinematics.has_axis()) {
    *resolved_kinematics.mutable_axis() = ToVectorProto(axis);
  }

  // Update child link's parent and pose first.
  child_entity->set_parent_name(update.new_joint_name());
  *child_entity->mutable_parent_t_this() = ToProto(Pose3d::Identity());

  // Add the new joint entity.
  Entity* joint_entity = object.add_entities();
  joint_entity->set_name(update.new_joint_name());
  joint_entity->set_parent_name(update.parent_link_name());
  *joint_entity->mutable_joint() = update.joint();
  *joint_entity->mutable_joint()->mutable_kinematics_component() =
      std::move(resolved_kinematics);

  INTR_ASSIGN_OR_RETURN(const Pose3d new_parent_t_this,
                        ResolveJointEntityPose(*joint_entity));
  *joint_entity->mutable_parent_t_this() = ToProto(new_parent_t_this);

  // Keep existing named configurations consistent by inserting the new joint's
  // position.
  if (object.has_properties() && object.properties().has_kinematics()) {
    for (NamedConfiguration& config : *object.mutable_properties()
                                           ->mutable_kinematics()
                                           ->mutable_named_configurations()) {
      (*config.mutable_joint_positions())[update.new_joint_name()] =
          kinematics.raw_value();
    }
  }

  return std::move(object);
}

}  // namespace internal
}  // namespace scene_object
}  // namespace intrinsic
