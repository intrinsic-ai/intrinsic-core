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

#include "intrinsic/scene/validate/scene_object_validation.h"

#include <cmath>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "google/protobuf/any.pb.h"
#include "intrinsic/kinematics/types/cartesian_limits.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/proto_conversion.h"
#include "intrinsic/scene/constants.h"
#include "intrinsic/scene/conversion/object_properties_conversion.h"
#include "intrinsic/scene/conversion/scene_object_model_utils.h"
#include "intrinsic/scene/proto/v1/collision_rules.pb.h"
#include "intrinsic/scene/proto/v1/entity.pb.h"
#include "intrinsic/scene/proto/v1/object_properties.pb.h"
#include "intrinsic/scene/proto/v1/scene_object.pb.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/proto/kinematics_component.pb.h"

namespace intrinsic {
namespace scene_object {

namespace {
using intrinsic_proto::scene_object::v1::CollisionEntityPair;
using intrinsic_proto::scene_object::v1::CollisionRules;
using intrinsic_proto::scene_object::v1::Entity;
using intrinsic_proto::scene_object::v1::Kinematics;
using intrinsic_proto::scene_object::v1::SceneObject;

// To detect cycles, we know that each entity only has one outgoing edge, so
// we can do this in O(N): Keep a hash set of all entities that are *not* in
// a cycle (e.g. they have an ancestry chain to root). If we traverse the
// parents of an entity to the point that either it is in the set or one of
// its ancestors is in the set, then we know it's not part of a cycle.
absl::Status CheckEntityCycles(
    const SceneObject& scene_object,
    const absl::flat_hash_map<absl::string_view, absl::string_view>&
        entity_names_to_parents,
    absl::string_view root_entity) {
  absl::flat_hash_set<absl::string_view> visited;
  absl::flat_hash_set<absl::string_view> children_of_root;
  children_of_root.insert(root_entity);

  // Returns true if we detect a cycle
  std::function<bool(const absl::string_view)> walk_ancestors_detecting_cycles =
      [&](const absl::string_view e) {
        if (children_of_root.contains(e)) {
          return false;
        }

        // If we've already been here, then it's a cycle.
        if (visited.contains(e)) {
          return true;
        }
        visited.insert(e);

        if (!walk_ancestors_detecting_cycles(entity_names_to_parents.at(e))) {
          children_of_root.insert(e);
          return false;
        }

        return true;
      };

  for (const Entity& e : scene_object.entities()) {
    if (walk_ancestors_detecting_cycles(e.name())) {
      // The entity `e` is not necessarily in the cycle itself; it may be an
      // ancestor or descendant that merely points into a cycle. To extract the
      // exact cycle without looping indefinitely, `seen_in_path` tracks all
      // visited entities along the ancestry path until a repeat node is
      // encountered.
      std::vector<absl::string_view> path;
      absl::flat_hash_set<absl::string_view> seen_in_path;
      absl::string_view curr = e.name();
      while (seen_in_path.insert(curr).second) {
        path.push_back(curr);
        curr = entity_names_to_parents.at(curr);
      }
      // `curr` is the first node visited a second time, marking the start
      // and end of the cycle within `path`.
      auto cycle_start_it = absl::c_find(path, curr);
      std::vector<absl::string_view> cycle_entities(cycle_start_it, path.end());
      cycle_entities.push_back(curr);
      return absl::InvalidArgumentError(
          absl::Substitute("Scene object has cycle in entity parenting: $0",
                           absl::StrJoin(cycle_entities, " -> ")));
    }
  }

  return absl::OkStatus();
}

absl::Status ValidateKinematics(
    const Kinematics& kinematics,
    const absl::flat_hash_set<absl::string_view>& non_fixed_joint_names) {
  // TODO(b/343309271): Support multiple ik_solvers.
  if (kinematics.ik_solvers().size() > 1) {
    std::vector<std::string> ik_solver_keys;
    for (const auto& ik_solver : kinematics.ik_solvers()) {
      ik_solver_keys.push_back(ik_solver.ik_solver());
    }
    return absl::InvalidArgumentError(absl::StrCat(
        "Multiple ik solvers found: {", absl::StrJoin(ik_solver_keys, ", "),
        "}. Only one should be provided."));
  }

  // Add validation for cartesian limits.
  if (kinematics.has_limits()) {
    INTR_ASSIGN_OR_RETURN(auto cartesian_limits,
                          FromProto(kinematics.limits()));
    if (!cartesian_limits.IsValid()) {
      // TODO(stoyang): Add a better error message.
      return absl::InvalidArgumentError("Invalid cartesian limits.");
    }
  }

  // Validates named configurations against the set of non-fixed joints.
  for (const auto& named_config : kinematics.named_configurations()) {
    if (named_config.name().empty()) {
      return absl::InvalidArgumentError("Named configuration has no name.");
    }

    if (named_config.joint_positions().size() != non_fixed_joint_names.size()) {
      return absl::InvalidArgumentError(
          absl::StrCat("Named configuration '", named_config.name(), "' has ",
                       named_config.joint_positions().size(),
                       " joint positions, but there are ",
                       non_fixed_joint_names.size(), " joints."));
    }

    for (const auto& [joint_name, joint_position] :
         named_config.joint_positions()) {
      if (joint_name.empty()) {
        return absl::InvalidArgumentError(
            absl::StrCat("Named configuration '", named_config.name(),
                         "' has a joint position with an empty joint name."));
      }

      if (!non_fixed_joint_names.contains(joint_name)) {
        return absl::InvalidArgumentError(
            absl::StrCat("Named configuration '", named_config.name(),
                         "' has a joint position for joint '", joint_name,
                         "' which does not exist."));
      }
    }
  }

  return absl::OkStatus();
}

std::string EntityTypeToString(Entity::EntityTypeCase entity_type) {
  const auto* field_descriptor =
      Entity::descriptor()->FindFieldByNumber(entity_type);
  return field_descriptor == nullptr ? "UNKNOWN"
                                     : std::string(field_descriptor->name());
}

std::string GenericWrongParentTypeError(const Entity& entity,
                                        const Entity& parent) {
  return absl::Substitute(
      "$0 entity '$1' cannot be a child of entity $2 entity '$3'",
      EntityTypeToString(entity.entity_type_case()), entity.name(),
      EntityTypeToString(parent.entity_type_case()), parent.name());
}

absl::Status ValidateParentType(const Entity& entity, const Entity& parent) {
  switch (entity.entity_type_case()) {
    case Entity::kFrame: {
      if (parent.entity_type_case() != Entity::kLink &&
          parent.entity_type_case() != Entity::kFrame) {
        return absl::InvalidArgumentError(
            absl::StrCat("Frame entity can only be a child of frame or link. ",
                         GenericWrongParentTypeError(entity, parent)));
      }
    } break;
    case Entity::kLink: {
      if (parent.entity_type_case() != Entity::kJoint &&
          parent.entity_type_case() != Entity::kLink) {
        return absl::InvalidArgumentError(
            absl::StrCat("Link entity can only be a child of joint or link. ",
                         GenericWrongParentTypeError(entity, parent)));
      }
    } break;
    case Entity::kJoint: {
      if (parent.entity_type_case() != Entity::kLink) {
        return absl::InvalidArgumentError(
            absl::StrCat("Joint entity can only be a child of link. ",
                         GenericWrongParentTypeError(entity, parent)));
      }
    } break;
    case Entity::kSensor: {
      if (parent.entity_type_case() != Entity::kLink &&
          parent.entity_type_case() != Entity::kJoint) {
        return absl::InvalidArgumentError(
            absl::StrCat("Sensor entity can only be a child of link or joint. ",
                         GenericWrongParentTypeError(entity, parent)));
      }
    } break;
    default:
      return absl::InvalidArgumentError(absl::Substitute(
          "Unsupported entity type $0 for parent type validation.",
          EntityTypeToString(entity.entity_type_case())));
  }
  return absl::OkStatus();
}

absl::Status ValidateJointValue(const Entity& entity,
                                const Pose3d& parent_t_this,
                                const Pose3d& computed_parent_t_this) {
  // This is the default when the pose is unset, so if it's roughly identity
  // then ignore it (as some tools may write it out when processing).
  if (parent_t_this.isApprox(Pose3d())) {
    return absl::OkStatus();
  }

  const auto& kinematics = entity.joint().kinematics_component();
  // If no joint value is set then it's valid.
  if (!kinematics.has_raw_value()) {
    return absl::OkStatus();
  }

  if (!parent_t_this.isApprox(computed_parent_t_this, 0.005)) {
    return absl::InvalidArgumentError(absl::Substitute(
        "Joint value '$0' produces invalid parent_t_this from "
        "inboard and outboard poses. Expected $1, got $2",
        kinematics.raw_value(), parent_t_this, computed_parent_t_this));
  }

  return absl::OkStatus();
}

template <typename EntityLookup>
absl::Status ValidateCollisionEntityPairImpl(
    const CollisionEntityPair& entity_pair, const EntityLookup& entities) {
  const absl::string_view left_entity_name =
      entity_pair.left_entity().entity_name();
  const absl::string_view right_entity_name =
      entity_pair.right_entity().entity_name();

  if (!left_entity_name.empty() && !entities.contains(left_entity_name)) {
    return absl::InvalidArgumentError(absl::Substitute(
        "Entity '$0' in collision rule does not exist in the scene object.",
        left_entity_name));
  }

  if (!right_entity_name.empty() && !entities.contains(right_entity_name)) {
    return absl::InvalidArgumentError(absl::Substitute(
        "Entity '$0' in collision rule does not exist in the scene object.",
        right_entity_name));
  }

  return absl::OkStatus();
}

template <typename EntityLookup>
absl::Status ValidateCollisionRulesImpl(const CollisionRules& collision_rules,
                                        const EntityLookup& entities) {
  for (const auto& rule : collision_rules.exclusion_rules()) {
    INTR_RETURN_IF_ERROR(
        ValidateCollisionEntityPairImpl(rule.entity_pair(), entities));
  }

  return absl::OkStatus();
}

}  // namespace

absl::Status ValidateSceneObject(const SceneObject& object) {
  const int num_entities = object.entities_size();
  absl::flat_hash_map<absl::string_view, int> entities_by_name;
  entities_by_name.reserve(num_entities);
  for (int i = 0; i < num_entities; ++i) {
    entities_by_name.try_emplace(object.entities(i).name(), i);
  }

  absl::string_view root_entity;
  absl::flat_hash_map<absl::string_view, absl::string_view>
      entity_names_to_parents;
  entity_names_to_parents.reserve(num_entities);
  absl::flat_hash_set<absl::string_view> non_fixed_joint_names;
  for (int i = 0; i < num_entities; ++i) {
    const Entity& e = object.entities(i);
    if (e.name().empty()) {
      return absl::InvalidArgumentError("Provided entity with no name!");
    }

    // `entities_by_name` holds the first occurrence of each name.
    if (entities_by_name.at(e.name()) != i) {
      return absl::InvalidArgumentError(
          absl::Substitute("Duplicate entity name: $0", e.name()));
    }
    entity_names_to_parents[e.name()] = e.parent_name();

    if (e.parent_name().empty() && root_entity.empty()) {
      root_entity = e.name();

      // Root entity should be a link.
      if (e.entity_type_case() != Entity::kLink) {
        return absl::InvalidArgumentError(
            absl::Substitute("Root entity $0 must be a link.", e.name()));
      }
    } else if (e.parent_name().empty()) {
      return absl::InvalidArgumentError(absl::Substitute(
          "Scene object may only have a single root entity. Found: $0, $1",
          root_entity, e.name()));
    }

    std::optional<Pose3d> parent_t_this;
    if (e.has_parent_t_this()) {
      if (e.parent_name().empty()) {
        LOG(WARNING) << "Root entity " << e.name()
                     << " has parent_t_this which will be ignored.";
      } else {
        // Non-root entities should have valid poses.
        INTR_ASSIGN_OR_RETURN(parent_t_this,
                              FromProtoNormalized(e.parent_t_this()),
                              _ << "Invalid pose on entity " << e.name());
      }
    }

    if (!e.parent_name().empty()) {
      const auto parent_it = entities_by_name.find(e.parent_name());
      if (parent_it == entities_by_name.end()) {
        return absl::InvalidArgumentError(
            absl::Substitute("Cannot find parent entity '$1' for entity '$0'",
                             e.name(), e.parent_name()));
      }
      INTR_RETURN_IF_ERROR(
          ValidateParentType(e, object.entities(parent_it->second)));
    }

    switch (e.entity_type_case()) {
      case Entity::kFrame: {
        // Nothing to do here
      } break;
      case Entity::kLink: {
        // Nothing to do here
      } break;
      case Entity::kJoint: {
        const intrinsic_proto::world::KinematicsComponent& kinematics =
            e.joint().kinematics_component();
        if (kinematics.motion_type() ==
            intrinsic_proto::world::KinematicsComponent::
                MOTION_TYPE_UNDEFINED) {
          return absl::InvalidArgumentError(absl::Substitute(
              "Joint entity $0 must have a motion type defined", e.name()));
        }
        INTR_RETURN_IF_ERROR(ValidateJointLimits(kinematics))
            << "Invalid joint limits on joint " << e.name();

        if (kinematics.motion_type() !=
            intrinsic_proto::world::KinematicsComponent::MOTION_TYPE_FIXED) {
          non_fixed_joint_names.insert(e.name());

          // Rejects joints whose pose cannot be computed from their
          // kinematics, e.g., a zero axis, even if `parent_t_this` or
          // `raw_value` is unset.
          INTR_ASSIGN_OR_RETURN(
              const Pose3d computed_parent_t_this, ResolveJointEntityPose(e),
              _ << "Invalid kinematics on joint " << e.name());

          if (parent_t_this.has_value()) {
            INTR_RETURN_IF_ERROR(
                ValidateJointValue(e, *parent_t_this, computed_parent_t_this))
                << "Invalid joint value on joint " << e.name();
          }
        } else if (kinematics.raw_value() != 0.0) {
          return absl::InvalidArgumentError(absl::Substitute(
              "Invalid non-zero joint value for fixed joint '$0'", e.name()));
        }
      } break;
      case Entity::kSensor: {
        // Nothing to do here
      } break;
      default:
        return absl::InvalidArgumentError(absl::Substitute(
            "Entity $0 is neither frame, link, joint, nor sensor.", e.name()));
    }
  }

  if (root_entity.empty()) {
    return absl::InvalidArgumentError("Scene object has no root entities.");
  }

  if (object.has_properties() && object.properties().has_kinematics()) {
    INTR_RETURN_IF_ERROR(ValidateKinematics(object.properties().kinematics(),
                                            non_fixed_joint_names));
  }

  if (object.has_collision_rules()) {
    INTR_RETURN_IF_ERROR(
        ValidateCollisionRulesImpl(object.collision_rules(), entities_by_name));
  }

  if (!object.user_data().empty()) {
    for (const absl::string_view reserved : kSceneObjectReservedUserDataKeys) {
      if (object.user_data().contains(reserved)) {
        return absl::InvalidArgumentError(
            absl::Substitute("Found reserved user_data key: $0", reserved));
      }
    }
  }

  return CheckEntityCycles(object, entity_names_to_parents, root_entity);
}

namespace {

// Validate the consistency of the limits proto.
// TODO: Should we expose this for testing directly?
absl::Status ValidateJointLimits(
    const intrinsic_proto::world::KinematicsComponent::Limits& limits) {
  switch (limits.raw_value_limits_type_case()) {
    case intrinsic_proto::world::KinematicsComponent::Limits::kFixedLimits: {
      if (std::isnan(limits.fixed_limits().lower()) ||
          std::isnan(limits.fixed_limits().upper())) {
        return absl::InvalidArgumentError(
            "Joint position limits must not be NaN.");
      }

      if (limits.fixed_limits().lower() > limits.fixed_limits().upper()) {
        return absl::InvalidArgumentError(
            absl::StrCat("Joint position lower limit must be <= upper limit: ",
                         limits.fixed_limits().lower(),
                         " <= ", limits.fixed_limits().upper()));
      }
    } break;
    case intrinsic_proto::world::KinematicsComponent::Limits::
        RAW_VALUE_LIMITS_TYPE_NOT_SET:
      break;
  }

  if (std::isnan(limits.velocity()) || limits.velocity() < 0.0) {
    return intrinsic::InvalidArgumentErrorBuilder()
           << "Joint velocity limit should be non-negative! (got: "
           << limits.velocity() << ")";
  }
  if (std::isnan(limits.acceleration()) || limits.acceleration() < 0.0) {
    return intrinsic::InvalidArgumentErrorBuilder()
           << "Joint acceleration limit should be non-negative! (got: "
           << limits.acceleration() << ")";
  }
  if (std::isnan(limits.jerk()) || limits.jerk() < 0.0) {
    return intrinsic::InvalidArgumentErrorBuilder()
           << "Joint jerk limit should be non-negative! (got: " << limits.jerk()
           << ")";
  }
  if (std::isnan(limits.effort()) || limits.effort() < 0.0) {
    return intrinsic::InvalidArgumentErrorBuilder()
           << "Joint effort limit should be non-negative! (got: "
           << limits.effort() << ")";
  }

  return absl::OkStatus();
}

}  // namespace

absl::Status ValidateJointLimits(
    const intrinsic_proto::world::KinematicsComponent& component) {
  if (component.has_system_limits()) {
    INTR_RETURN_IF_ERROR(ValidateJointLimits(component.system_limits()))
        << "While validating system limits";
  }
  if (component.has_application_limits()) {
    INTR_RETURN_IF_ERROR(ValidateJointLimits(component.application_limits()))
        << "While validating application limits";
  }

  if (component.has_system_limits() && component.has_application_limits()) {
    // If we have both system and application limits, we need to make sure that
    // the application limits are within the system limits.

    if (component.system_limits().has_fixed_limits() &&
        component.application_limits().has_fixed_limits()) {
      if (component.system_limits().fixed_limits().lower() >
          component.application_limits().fixed_limits().lower()) {
        return ::intrinsic::InvalidArgumentErrorBuilder()
               << "System position lower ("
               << component.system_limits().fixed_limits().lower()
               << ") limit is more than the application position lower ("
               << component.application_limits().fixed_limits().lower()
               << ") limit";
      }

      if (component.system_limits().fixed_limits().upper() <
          component.application_limits().fixed_limits().upper()) {
        return ::intrinsic::InvalidArgumentErrorBuilder()
               << "System position upper ("
               << component.system_limits().fixed_limits().upper()
               << ") limit is less than the application position upper ("
               << component.application_limits().fixed_limits().upper()
               << ") limit";
      }
    }

    if (component.system_limits().velocity() <
        component.application_limits().velocity()) {
      return ::intrinsic::InvalidArgumentErrorBuilder()
             << "System velocity (" << component.system_limits().velocity()
             << ") limit is less than the application velocity ("
             << component.application_limits().velocity() << ") limit";
    }
    if (component.system_limits().acceleration() <
        component.application_limits().acceleration()) {
      return ::intrinsic::InvalidArgumentErrorBuilder()
             << "System acceleration ("
             << component.system_limits().acceleration()
             << ") limit is less than the application acceleration ("
             << component.application_limits().acceleration() << ") limit";
    }
    if (component.system_limits().jerk() <
        component.application_limits().jerk()) {
      return ::intrinsic::InvalidArgumentErrorBuilder()
             << "System jerk (" << component.system_limits().jerk()
             << ") limit is less than the application jerk ("
             << component.application_limits().jerk() << ") limit";
    }
    if (component.system_limits().effort() <
        component.application_limits().effort()) {
      return ::intrinsic::InvalidArgumentErrorBuilder()
             << "System effort (" << component.system_limits().effort()
             << ") limit is less than the application effort ("
             << component.application_limits().effort() << ") limit";
    }
  }

  // If we have application limits, we need to make sure that the raw value is
  // within those limits.
  if (component.application_limits().has_fixed_limits() &&
      component.has_raw_value()) {
    // We do the comparison this way to also guard against NaNs.
    const double lower = component.application_limits().fixed_limits().lower();
    const double upper = component.application_limits().fixed_limits().upper();
    if (!(component.raw_value() <= upper && component.raw_value() >= lower)) {
      return ::intrinsic::InvalidArgumentErrorBuilder()
             << "Joint raw value (" << component.raw_value()
             << ") is not within the application limits (" << lower << ", "
             << upper << ")";
    }
  }

  return absl::OkStatus();
}

absl::Status ValidateCollisionEntityPair(
    const CollisionEntityPair& entity_pair,
    const absl::flat_hash_set<absl::string_view>& entity_names) {
  return ValidateCollisionEntityPairImpl(entity_pair, entity_names);
}

absl::Status ValidateCollisionRules(
    const CollisionRules& collision_rules,
    const absl::flat_hash_set<absl::string_view>& entity_names) {
  return ValidateCollisionRulesImpl(collision_rules, entity_names);
}

}  // namespace scene_object
}  // namespace intrinsic
