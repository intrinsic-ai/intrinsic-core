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

#include "intrinsic/world/update_world.h"

#include <cstddef>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/geometry/api/affine_transform_of_geometry.h"
#include "intrinsic/geometry/compatibility/io.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/proto_conversion.h"
#include "intrinsic/motion_planning/path_planning/kinematics_system_proxy_util.h"
#include "intrinsic/util/aggregate_type.h"
#include "intrinsic/util/eigen.h"
#include "intrinsic/util/status/status_builder.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/collision/coal_collision_checker.h"
#include "intrinsic/world/component/attachment_component.h"
#include "intrinsic/world/component/collections_component.h"
#include "intrinsic/world/component/geometry_component.h"
#include "intrinsic/world/component/robot_component.h"
#include "intrinsic/world/dof_kinematic_view.h"
#include "intrinsic/world/entity.h"
#include "intrinsic/world/entity_id.h"
#include "intrinsic/world/geometry_types.h"
#include "intrinsic/world/grouping.h"
#include "intrinsic/world/hashing/hashing.h"
#include "intrinsic/world/labels.h"
#include "intrinsic/world/proto/entity_search.pb.h"
#include "intrinsic/world/proto/world_updates.pb.h"
#include "intrinsic/world/service/world_service.pb.h"
#include "intrinsic/world/util/entity_search_util.h"
#include "intrinsic/world/world.h"

namespace intrinsic {

absl::Status UpdateWorld(
    const intrinsic_proto::world::WorldUpdates& world_updates, World* world) {
  VLOG(1) << "Running world updates with " << world_updates.updates_size()
          << " updates";

  for (const auto& update : world_updates.updates()) {
    INTR_RETURN_IF_ERROR(UpdateWorld(update, world));
  }
  return absl::OkStatus();
}

absl::Status UpdateWorld(
    const intrinsic_proto::world::internal::WorldServerUpdates& world_updates,
    World* world) {
  VLOG(1) << "Running world server updates with "
          << world_updates.updates_size() << " updates";

  for (const auto& update : world_updates.updates()) {
    INTR_RETURN_IF_ERROR(UpdateWorld(update, world));
  }
  return absl::OkStatus();
}

absl::Status UpdateWorld(
    const intrinsic_proto::world::internal::WorldServerUpdate& world_update,
    World* world) {
  switch (world_update.update_case()) {
    case intrinsic_proto::world::internal::WorldServerUpdate::kSetDofValues:
      return UpdateWorld(world_update.set_dof_values(), world);
    case intrinsic_proto::world::internal::WorldServerUpdate::
        kSetRobotDofValues:
      return UpdateWorld(world_update.set_robot_dof_values(), world);
    case intrinsic_proto::world::internal::WorldServerUpdate::kReparentEntity:
      return UpdateWorld(world_update.reparent_entity(), world);
    case intrinsic_proto::world::internal::WorldServerUpdate::kUpdateAttachment:
      return UpdateWorld(world_update.update_attachment(), world);
    case intrinsic_proto::world::internal::WorldServerUpdate::
        kSetNamedConfiguration:
      return UpdateWorld(world_update.set_named_configuration(), world);
    case intrinsic_proto::world::internal::WorldServerUpdate::kAddLabel:
      return UpdateWorld(world_update.add_label(), world);
    case intrinsic_proto::world::internal::WorldServerUpdate::kRemoveLabel:
      return UpdateWorld(world_update.remove_label(), world);
    case intrinsic_proto::world::internal::WorldServerUpdate::UPDATE_NOT_SET: {
      break;
    }
  }

  return absl::InvalidArgumentError(
      "No valid update set for WorldServerUpdate");
}

absl::Status UpdateWorld(
    const intrinsic_proto::world::WorldUpdate& world_update, World* world) {
  switch (world_update.update_case()) {
    case intrinsic_proto::world::WorldUpdate::kMoveGroup:
      return UpdateWorld(world_update.move_group(), world);
    case intrinsic_proto::world::WorldUpdate::kAddLabel:
      return UpdateWorld(world_update.add_label(), world);
    case intrinsic_proto::world::WorldUpdate::kSetDofValues:
      return UpdateWorld(world_update.set_dof_values(), world);
    case intrinsic_proto::world::WorldUpdate::kSetRobotDofValues:
      return UpdateWorld(world_update.set_robot_dof_values(), world);
    case intrinsic_proto::world::WorldUpdate::kSelfExclusions:
      return UpdateWorld(world_update.self_exclusions(), world);
    case intrinsic_proto::world::WorldUpdate::kSetAlias:
      return UpdateWorld(world_update.set_alias(), world);
    case intrinsic_proto::world::WorldUpdate::kSetObjectNames:
      return UpdateWorld(world_update.set_object_names(), world);
    case intrinsic_proto::world::WorldUpdate::kAddEntity:
      return UpdateWorld(world_update.add_entity(), world);
    case intrinsic_proto::world::WorldUpdate::kSetRobotIkSolverKey:
      return UpdateWorld(world_update.set_robot_ik_solver_key(), world);
    case intrinsic_proto::world::WorldUpdate::kCollisionExclusions:
      return UpdateWorld(world_update.collision_exclusions(), world);
    case intrinsic_proto::world::WorldUpdate::kSetRobotTip:
      return UpdateWorld(world_update.set_robot_tip(), world);
    case intrinsic_proto::world::WorldUpdate::kRenameEntity:
      return UpdateWorld(world_update.rename_entity(), world);
    case intrinsic_proto::world::WorldUpdate::kSetNamedConfiguration:
      return UpdateWorld(world_update.set_named_configuration(), world);
    case intrinsic_proto::world::WorldUpdate::kRenameGroup:
      return UpdateWorld(world_update.rename_group(), world);
    case intrinsic_proto::world::WorldUpdate::UPDATE_NOT_SET: {
      break;
    }
  }

  return absl::InvalidArgumentError("No valid update set for WorldUpdate");
}

absl::Status UpdateWorld(const intrinsic_proto::world::MoveGroup& move_group,
                         World* world) {
  auto& grouping = world->As<Grouping>();

  // Move objects from one group to the other.
  const GroupId old_group_id(move_group.old_group_id());
  const GroupId new_group_id(move_group.new_group_id());
  if (!grouping.CheckGroupExists(new_group_id)) {
    return ::intrinsic::InvalidArgumentErrorBuilder()
           << "Cannot move objects from " << old_group_id << " to "
           << new_group_id << " because '" << new_group_id
           << "' does not seem to exist in the world.";
  }
  if (!grouping.CheckGroupExists(old_group_id)) {
    return ::intrinsic::InvalidArgumentErrorBuilder()
           << "Cannot move objects from " << old_group_id << " to "
           << new_group_id << " because '" << old_group_id
           << "' does not seem to exist in the world.";
  }

  VLOG(1) << "Adding all objects from group " << old_group_id << " to group "
          << new_group_id;
  auto objects_in_group = grouping.GetObjectIds(old_group_id);

  // TODO(stoyang): Remove the special handling for tip/urdf_root
  for (auto group_object_id : objects_in_group) {
    INTR_ASSIGN_OR_RETURN(auto* entity, world->GetEntityById(group_object_id));
    if (entity->GetLabels().count(labels::UrdfRoot()) > 0) {
      INTR_RETURN_IF_ERROR(entity->RemoveLabels({labels::UrdfRoot()}));
    }
    if (entity->GetLabels().count(labels::Tip()) > 0) {
      VLOG(2) << "Found tip link in gripper group: " << group_object_id;
    }

    if (!world->GetGroupIds().contains(new_group_id)) {
      return intrinsic::NotFoundErrorBuilder()
             << "GroupId \"" << new_group_id.value() << "\" does not exist";
    }
    INTR_RETURN_IF_ERROR(entity->AddLabels({LabelId(new_group_id.value())}));
  }
  VLOG(1) << "Moved all parts from " << old_group_id << " to " << new_group_id;

  return absl::OkStatus();
}

absl::Status UpdateWorld(const intrinsic_proto::world::AddLabel& label_spec,
                         World* world) {
  INTR_ASSIGN_OR_RETURN(const auto entities,
                        GetEntities(*world, label_spec.entities(),
                                    label_spec.apply_to_multiple()));

  std::set<LabelId> label_ids_to_add;
  for (const std::string& l : label_spec.labels_to_add()) {
    label_ids_to_add.emplace(l);
  }

  for (const auto& entity_id : entities) {
    INTR_ASSIGN_OR_RETURN(auto* ent, world->GetEntityById(entity_id));
    INTR_RETURN_IF_ERROR(ent->AddLabels(label_ids_to_add));
  }

  return absl::OkStatus();
}

absl::Status UpdateWorld(const intrinsic_proto::world::RemoveLabel& label_spec,
                         World* world) {
  INTR_ASSIGN_OR_RETURN(const auto entities,
                        GetEntities(*world, label_spec.entities(),
                                    label_spec.apply_to_multiple()));

  std::set<LabelId> label_ids_to_remove;
  for (const std::string& label : label_spec.labels_to_remove()) {
    label_ids_to_remove.emplace(label);
  }

  for (const auto& entity_id : entities) {
    INTR_ASSIGN_OR_RETURN(auto* ent, world->GetEntityById(entity_id));
    INTR_RETURN_IF_ERROR(ent->RemoveLabels(label_ids_to_remove));
  }

  return absl::OkStatus();
}

absl::Status UpdateWorld(
    const intrinsic_proto::world::SetDofValues& dof_value_change,
    World* world) {
  GroupId group_id(dof_value_change.robot_id());
  INTR_ASSIGN_OR_RETURN(auto dof_view,
                        world->GetDofKinematicViewForRobotGroupId(group_id));
  if (!dof_view) {
    return absl::InvalidArgumentError(absl::Substitute(
        "Failed to set dof values on missing group '$0'.", group_id.value()));
  }

  size_t dof_count = dof_view->GetDofCount();
  if (dof_count != dof_value_change.dof_values_size()) {
    return absl::InvalidArgumentError(absl::Substitute(
        "Failed to set dof values on group '$0' with mismatched dof count; "
        "given '$1' vs expected '$2'.",
        group_id.value(), dof_value_change.dof_values_size(), dof_count));
  }

  eigenmath::VectorXd dof_values(dof_count);
  for (size_t i = 0; i < dof_values.size(); ++i) {
    dof_values(i) = dof_value_change.dof_values(i);
  }

  VLOG(1) << "Setting robot '" << group_id << "' dofs to {"
          << toString(dof_values) << "}";
  INTR_RETURN_IF_ERROR(dof_view->SetDofValues(
      dof_values, !dof_value_change.skip_limit_enforcement()));

  return absl::OkStatus();
}

absl::Status UpdateWorld(
    const intrinsic_proto::world::SetRobotDofValues& dof_value_change,
    World* world) {
  std::string robot_description;
  RobotCollectionsEntityId robot_id{kInvalidEntityId};

  switch (dof_value_change.robot_case()) {
    case intrinsic_proto::world::SetRobotDofValues::kResourceName: {
      absl::string_view robot_name = dof_value_change.resource_name();
      for (const auto id :
           world->GetTypedEntityIds<RobotCollectionsEntityId>()) {
        INTR_ASSIGN_OR_RETURN(const WorldEntity* entity,
                              world->GetEntityById(id));
        if (entity->GetLocalName() == robot_name ||
            entity->GetAlias() == robot_name) {
          robot_id = id;
          break;
        }
      }
      robot_description =
          absl::StrCat("Resource(", dof_value_change.resource_name(), ")");
      break;
    }
    case intrinsic_proto::world::SetRobotDofValues::kRobotEntityId: {
      INTR_ASSIGN_OR_RETURN(robot_id,
                            world->ValidateEntity<RobotCollectionsEntityId>(
                                EntityId(dof_value_change.robot_entity_id())));
      robot_description = absl::StrCat("RobotCollectionsEntityId(",
                                       dof_value_change.robot_entity_id(), ")");
      break;
    }
    case intrinsic_proto::world::SetRobotDofValues::ROBOT_NOT_SET: {
      return absl::InvalidArgumentError(
          "No valid robot set for SetRobotDofValues");
    }
  }

  if (robot_id == kInvalidEntityId) {
    return absl::InvalidArgumentError("Unable to find valid robot id");
  }

  INTR_ASSIGN_OR_RETURN(auto dof_view, world->GetDofKinematicView(robot_id));
  if (!dof_view) {
    return absl::InvalidArgumentError(absl::Substitute(
        "Failed to set dof values on missing $0.", robot_description));
  }

  eigenmath::VectorXd dof_values;
  switch (dof_value_change.dof_values_case()) {
    case intrinsic_proto::world::SetRobotDofValues::kNamedConfiguration: {
      if (dof_value_change.named_configuration().empty()) {
        return absl::InvalidArgumentError(absl::Substitute(
            "Failed to set dof values on $0 with empty named configuration.",
            robot_description));
      }

      INTR_ASSIGN_OR_RETURN(
          auto* robot, world->GetComponentByEntityId<RobotComponent>(robot_id));
      INTR_ASSIGN_OR_RETURN(
          dof_values,
          robot->GetNamedConfiguration(dof_value_change.named_configuration()));

      break;
    }
    case intrinsic_proto::world::SetRobotDofValues::kValues: {
      const auto& values = dof_value_change.values();
      dof_values = eigenmath::VectorXd(values.dof_values_size());
      for (size_t i = 0; i < dof_values.size(); ++i) {
        dof_values(i) = values.dof_values(i);
      }
      break;
    }
    case intrinsic_proto::world::SetRobotDofValues::DOF_VALUES_NOT_SET: {
      return absl::InvalidArgumentError(absl::Substitute(
          "Failed to set dof values on $0 with missing dof values.",
          robot_description));
    }
  }

  const size_t dof_count = dof_view->GetDofCount();
  if (dof_count != dof_values.size()) {
    return absl::InvalidArgumentError(absl::Substitute(
        "Failed to set dof values on $0 with mismatched dof count; "
        "given '$1' vs expected '$2'.",
        robot_description, dof_values.size(), dof_count));
  }

  VLOG(1) << "Setting robot " << robot_description << " dofs to {"
          << toString(dof_values) << "}";
  INTR_RETURN_IF_ERROR(dof_view->SetDofValues(
      dof_values, !dof_value_change.skip_limit_enforcement()));

  return absl::OkStatus();
}

namespace {

absl::Status AddSelfExclusionPairs(
    const WorldHashSet<PhysicalEntityId>& objects, World* world) {
  const auto collision_pairs =
      GetCollisionsBetweenSets(*world, objects, objects,
                               /*check_upper_triangle_only=*/true);
  for (const auto& [left, right] : collision_pairs) {
    INTR_RETURN_IF_ERROR(world->AddExclusionPair(left, right));
  }
  return absl::OkStatus();
}

}  // namespace

absl::Status UpdateWorld(
    const intrinsic_proto::world::SelfExclusions& self_exclusions,
    World* world) {
  auto& grouping = world->As<Grouping>();

  GroupId robot_id(self_exclusions.robot_id());
  VLOG(1) << "Checking the exclusion pairs for '" << robot_id << "'";
  auto robot_objects = grouping.GetObjectIds(robot_id);
  INTR_RETURN_IF_ERROR(AddSelfExclusionPairs(robot_objects, world));

  return absl::OkStatus();
}

absl::Status UpdateWorld(const intrinsic_proto::world::SetAlias& set_alias,
                         World* world) {
  INTR_ASSIGN_OR_RETURN(const EntityId entity,
                        GetSingleEntity(*world, set_alias.entities()));
  VLOG(1) << "Adding alias '" << set_alias.alias() << "'  to object named '";
  INTR_RETURN_IF_ERROR(world->SetAlias(entity, set_alias.alias()));
  return absl::OkStatus();
}

absl::Status UpdateWorld(
    const intrinsic_proto::world::SetObjectNames& set_object_names,
    World* world) {
  for (const auto& model_to_object : set_object_names.models_to_objects()) {
    intrinsic_proto::world::EntitySearchCriteria by_label;
    *by_label.mutable_by_labels()->add_label_ids() =
        model_to_object.sdf_model_name();
    intrinsic_proto::world::EntitySearchCriteria by_components;
    by_components.mutable_by_components()->set_collections_component(
        intrinsic_proto::world::EntitySearchCriteria::ByComponents::PRESENT);

    intrinsic_proto::world::SetAlias set_alias;
    *set_alias.mutable_entities()->mutable_by_and()->add_criteria() =
        std::move(by_label);
    *set_alias.mutable_entities()->mutable_by_and()->add_criteria() =
        std::move(by_components);
    set_alias.set_alias(model_to_object.object_name());

    INTR_RETURN_IF_ERROR(UpdateWorld(set_alias, world));
  }
  return absl::OkStatus();
}

absl::Status CheckKinematics(const World& world) {
  LOG(INFO) << "Checking the world for kinematic views";
  for (auto robot_id : world.GetTypedEntityIds<RobotCollectionsEntityId>()) {
    auto dof_view = world.GetDofKinematicView(robot_id);
    if (!dof_view.ok() || !dof_view.value()) {
      return intrinsic::InternalErrorBuilder()
             << "Robot " << world.GetLocalNameForEntityById(robot_id)
             << " has null DofKinematicView";
    }
  }

  LOG(INFO) << "Checking the world for cartesian views";

  for (auto robot_id : world.GetTypedEntityIds<RobotCollectionsEntityId>()) {
    LOG(INFO) << "Checking the world for "
              << world.GetLocalNameForEntityById(robot_id);
    auto cartesian_view = world.GetCartesianKinematicView(robot_id);
    if (!cartesian_view.ok() || cartesian_view.value() == nullptr) {
      LOG(WARNING) << "Robot " << world.GetLocalNameForEntityById(robot_id)
                   << " has no CartesianKinematicView. Skipping..";
      continue;
    }
    auto status_or_proxy = CreateKinematicsProxy(world, robot_id);
    if (!status_or_proxy.ok()) {
      return intrinsic::InternalErrorBuilder()
             << "CreateKinematicsProxy fails with robot "
             << world.GetLocalNameForEntityById(robot_id) << ": "
             << status_or_proxy.status();
    }
  }
  return absl::OkStatus();
}

absl::Status UpdateWorld(const intrinsic_proto::world::AddEntity& add_entity,
                         World* world) {
  INTR_ASSIGN_OR_RETURN(
      AttachmentEntityId parent_id,
      GetSingleTypedEntity<AttachmentEntityId>(*world, add_entity.parent()),
      _ << "parent entity is not uniquely identified by EntitySearchCriteria "
           "or does not have an AttachmentComponent");

  Pose3d parent_t_this;
  if (add_entity.has_parent_t_this()) {
    INTR_ASSIGN_OR_RETURN(
        parent_t_this, intrinsic_proto::FromProto(add_entity.parent_t_this()),
        _ << "Failed to parse parent_t_this from AddEntity");
  }

  EntityId entity_id = world->CreateEntity();
  INTR_ASSIGN_OR_RETURN(auto* entity, world->GetEntityById(entity_id));

  INTR_RETURN_IF_ERROR(
      world->CreateAttachmentComponent(parent_id, entity_id, parent_t_this)
          .status());

  if (!add_entity.alias().empty()) {
    INTR_RETURN_IF_ERROR(world->SetAlias(entity_id, add_entity.alias()));
  }

  if (!add_entity.local_name().empty()) {
    INTR_RETURN_IF_ERROR(entity->SetLocalName(add_entity.local_name()));
  }

  return absl::OkStatus();
}

absl::Status UpdateWorld(
    const intrinsic_proto::world::SetRobotIkSolverKey& set_robot_ik_solver_key,
    World* world) {
  // Use the search criteria to find the corresponding entity IDs.
  INTR_ASSIGN_OR_RETURN(
      const auto entity_ids,
      GetEntities(*world, set_robot_ik_solver_key.entities(),
                  set_robot_ik_solver_key.apply_to_multiple()));

  // Translate the entity IDs (which can point to robots or robot parts) into
  // robot IDs.
  std::set<RobotEntityId> robot_ids;
  for (EntityId id : entity_ids) {
    INTR_ASSIGN_OR_RETURN(const WorldEntity* ent, world->GetEntityById(id));
    RobotEntityId robot_id(kInvalidEntityId);
    if (ent->HasComponent<RobotComponent>()) {
      robot_id = RobotEntityId(id.value());
    } else {
      auto parent_robot_or =
          world->ValidateCollectionParentAmongTypes<RobotCollectionsEntityId>(
              id, CollectionsComponent::RobotPartTypes());
      if (parent_robot_or.ok()) {
        robot_id = parent_robot_or.value();
      }
    }
    if (robot_id == kInvalidEntityId) {
      return ::intrinsic::InvalidArgumentErrorBuilder()
             << "Unable to determine robot associated with Entity (ID "
             << id.value() << ")";
    }
    robot_ids.insert(robot_id);
  }

  // Locate the tip entity, if specified.
  AttachmentEntityId override_tip_id(kInvalidEntityId);
  if (set_robot_ik_solver_key.has_override_tip_entity()) {
    INTR_ASSIGN_OR_RETURN(
        const EntityId tip_id,
        GetSingleEntity(*world, set_robot_ik_solver_key.override_tip_entity()));
    INTR_ASSIGN_OR_RETURN(auto* tip_entity, world->GetEntityById(tip_id));
    INTR_RETURN_IF_ERROR(tip_entity->ValidateEntity<AttachmentEntityId>());
    INTR_RETURN_IF_ERROR(tip_entity->AddLabels({labels::Tip()}));
    override_tip_id = AttachmentEntityId(tip_id);
  }

  // This update operation is a stop-gap measure until we are able to select IK
  // solvers dynamically (i.e. based upon a robot's kinematic structure).
  //
  // As of 2020/08/28, all of our robots have exactly 1 solvable frame.
  // Consequently, the following code assumes we just need to remap the existing
  // solvable frame, rather than add any new solvable frames.
  for (RobotEntityId robot_id : robot_ids) {
    INTR_ASSIGN_OR_RETURN(WorldEntity * robot_ent,
                          world->GetEntityById(robot_id));
    INTR_ASSIGN_OR_RETURN(RobotComponent * robot_comp,
                          robot_ent->GetComponent<RobotComponent>());
    const auto solvable_frames = robot_comp->GetSolvableFrames();
    if (solvable_frames.empty()) {
      return intrinsic::InternalErrorBuilder()
             << "Robot (ID " << robot_id.value() << ") has no solvable frames";
    }
    for (auto [base, tip] : solvable_frames) {
      if (override_tip_id != kInvalidEntityId) {
        INTR_RETURN_IF_ERROR(robot_comp->RemoveSolvableFrames(base, tip));
        tip = override_tip_id;
      }
      INTR_RETURN_IF_ERROR(robot_comp->AddSolvableFrames(
          base, tip, set_robot_ik_solver_key.solver_key()));
    }
  }
  return absl::OkStatus();
}

absl::Status UpdateWorld(
    const intrinsic_proto::world::CollisionExclusion& collision_exclusions,
    World* world) {
  INTR_ASSIGN_OR_RETURN(
      const EntityId entity_a,
      GetSingleEntity(*world, collision_exclusions.entity_a()));
  INTR_ASSIGN_OR_RETURN(
      const EntityId entity_b,
      GetSingleEntity(*world, collision_exclusions.entity_b()));

  INTR_RETURN_IF_ERROR(world->AddExclusionPair(PhysicalEntityId(entity_a),
                                               PhysicalEntityId(entity_b)));
  return absl::OkStatus();
}

absl::Status UpdateWorld(
    const intrinsic_proto::world::SetRobotTip& set_robot_tip, World* world) {
  INTR_ASSIGN_OR_RETURN(const EntityId tip_id,
                        GetSingleEntity(*world, set_robot_tip.tip_entity()));
  // We already know we have exactly one entity and so we use it to set the tip
  // of the given robot with it.
  INTR_RETURN_IF_ERROR(world->SetTipIdForRobotGroupId(
      GroupId(set_robot_tip.robot_name()), PhysicalEntityId(tip_id)));

  return absl::OkStatus();
}

absl::Status UpdateWorld(
    const intrinsic_proto::world::ReparentEntities& reparent_entity,
    World* world) {
  INTR_ASSIGN_OR_RETURN(const auto entity,
                        GetEntities(*world, reparent_entity.entity()));
  INTR_ASSIGN_OR_RETURN(const auto new_parent,
                        GetEntities(*world, reparent_entity.parent_entity()));

  AttachmentEntityId new_parent_id(*new_parent.begin());
  AttachmentEntityId entity_id(*entity.begin());

  Pose3d parent_t_entity;
  if (reparent_entity.has_parent_t_entity()) {
    INTR_ASSIGN_OR_RETURN(parent_t_entity,
                          FromProto(reparent_entity.parent_t_entity()));
  } else {
    parent_t_entity = world->GetTransform(new_parent_id, entity_id);
  }

  INTR_RETURN_IF_ERROR(
      world->ReparentEntity(new_parent_id, entity_id, parent_t_entity));

  return absl::OkStatus();
}

absl::Status UpdateWorld(
    const intrinsic_proto::world::UpdateAttachmentPose& update_attachment,
    World* world) {
  INTR_ASSIGN_OR_RETURN(const auto entity,
                        GetEntities(*world, update_attachment.entity()));

  INTR_ASSIGN_OR_RETURN(Pose3d parent_t_entity,
                        FromProto(update_attachment.parent_t_entity()));

  AttachmentEntityId entity_id(*entity.begin());
  INTR_ASSIGN_OR_RETURN(
      auto attachment,
      world->GetComponentByEntityId<AttachmentComponent>(entity_id));

  world->UpdateAttachmentPose(attachment->GetParentId(), entity_id,
                              parent_t_entity);
  return absl::OkStatus();
}

absl::Status UpdateWorld(
    const intrinsic_proto::world::RenameEntity& rename_entity, World* world) {
  if (rename_entity.new_name().empty()) {
    return absl::InvalidArgumentError("RenameEntity.new_name cannot be empty");
  }
  INTR_ASSIGN_OR_RETURN(const auto id,
                        GetSingleEntity(*world, rename_entity.entity()));
  INTR_ASSIGN_OR_RETURN(auto* entity, world->GetEntityById(id));
  INTR_RETURN_IF_ERROR(entity->SetLocalName(rename_entity.new_name()));
  return absl::OkStatus();
}

absl::Status UpdateWorld(
    const intrinsic_proto::world::SetNamedConfiguration& named_config,
    World* world) {
  if (named_config.configuration_name().empty()) {
    return absl::InvalidArgumentError(
        "SetNamedConfiguration.configuration_name cannot be empty");
  }
  if (named_config.dof_values().empty()) {
    return absl::InvalidArgumentError(
        "SetNamedConfiguration.dof_values cannot be empty");
  }

  INTR_ASSIGN_OR_RETURN(const auto robot_id,
                        GetSingleEntity(*world, named_config.entity()));
  INTR_ASSIGN_OR_RETURN(
      auto* robot, world->GetComponentByEntityId<RobotComponent>(robot_id));
  robot->SetNamedConfiguration(
      named_config.configuration_name(),
      RepeatedDoubleToVectorXd(named_config.dof_values()));

  return absl::OkStatus();
}

absl::Status UpdateWorld(
    const intrinsic_proto::world::RenameGroup& rename_group, World* world) {
  auto& grouping = world->As<Grouping>();

  // Verify old group id exists and new group id does not exist
  const GroupId old_group_id(rename_group.old_group_id());
  const GroupId new_group_id(rename_group.new_group_id());
  if (grouping.CheckGroupExists(new_group_id)) {
    return ::intrinsic::InvalidArgumentErrorBuilder()
           << "Cannot rename group from " << old_group_id << " to "
           << new_group_id << " because '" << new_group_id
           << "' already exists in the world.";
  }
  if (!grouping.CheckGroupExists(old_group_id)) {
    return ::intrinsic::InvalidArgumentErrorBuilder()
           << "Cannot rename group from " << old_group_id << " to "
           << new_group_id << " because '" << old_group_id
           << "' does not seem to exist in the world.";
  }

  // Move objects to newly created group
  auto objects_in_group = grouping.GetObjectIds(old_group_id);
  INTR_RETURN_IF_ERROR(grouping.AddGroup(new_group_id, objects_in_group));

  // Move robot collections entity ids to new group
  if (auto robot_collections_entity_ids =
          world->GetRobotCollectionsEntityIdsForRobotGroupId(old_group_id);
      robot_collections_entity_ids.ok()) {
    world->SetRobotCollectionsEntityIdsForRobotGroupId(
        new_group_id, **robot_collections_entity_ids);
    world->SetRobotCollectionsEntityIdsForRobotGroupId(old_group_id, {});
  }
  grouping.RemoveGroup(old_group_id);
  return absl::OkStatus();
}

bool IsOnlyStateChange(
    const intrinsic_proto::world::WorldUpdates& world_updates) {
  for (const auto& world_update : world_updates.updates()) {
    if (!IsOnlyStateChange(world_update)) {
      return false;
    }
  }

  return true;
}

bool IsOnlyStateChange(
    const intrinsic_proto::world::WorldUpdate& world_update) {
  switch (world_update.update_case()) {
    case intrinsic_proto::world::WorldUpdate::kSetDofValues:
      ABSL_FALLTHROUGH_INTENDED;
    case intrinsic_proto::world::WorldUpdate::kSetRobotDofValues:
      ABSL_FALLTHROUGH_INTENDED;
    case intrinsic_proto::world::WorldUpdate::UPDATE_NOT_SET:
      return true;
    default: {
      return false;
    }
  }
}

bool IsOnlyStateChange(
    const intrinsic_proto::world::internal::WorldServerUpdates& world_updates) {
  for (const auto& world_update : world_updates.updates()) {
    if (!IsOnlyStateChange(world_update)) {
      return false;
    }
  }

  return true;
}

bool IsOnlyStateChange(
    const intrinsic_proto::world::internal::WorldServerUpdate& world_update) {
  switch (world_update.update_case()) {
    case intrinsic_proto::world::internal::WorldServerUpdate::kSetDofValues:
      ABSL_FALLTHROUGH_INTENDED;
    case intrinsic_proto::world::internal::WorldServerUpdate::
        kSetRobotDofValues:
      ABSL_FALLTHROUGH_INTENDED;
    case intrinsic_proto::world::internal::WorldServerUpdate::UPDATE_NOT_SET:
      ABSL_FALLTHROUGH_INTENDED;
    case intrinsic_proto::world::internal::WorldServerUpdate::kUpdateAttachment:
      return true;
    default:
      return false;
  }
}

bool IsOnlyDofChange(
    const intrinsic_proto::world::internal::WorldServerUpdates& world_updates) {
  for (const auto& world_update : world_updates.updates()) {
    if (!IsOnlyDofChange(world_update)) {
      return false;
    }
  }

  return true;
}

bool IsOnlyDofChange(
    const intrinsic_proto::world::internal::WorldServerUpdate& world_update) {
  switch (world_update.update_case()) {
    case intrinsic_proto::world::internal::WorldServerUpdate::kSetDofValues:
      ABSL_FALLTHROUGH_INTENDED;
    case intrinsic_proto::world::internal::WorldServerUpdate::
        kSetRobotDofValues:
      ABSL_FALLTHROUGH_INTENDED;
    case intrinsic_proto::world::internal::WorldServerUpdate::UPDATE_NOT_SET:
      return true;
    default:
      return false;
  }
}

}  // namespace intrinsic
