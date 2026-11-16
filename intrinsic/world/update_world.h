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

#ifndef INTRINSIC_WORLD_UPDATE_WORLD_H_
#define INTRINSIC_WORLD_UPDATE_WORLD_H_

#include "absl/status/status.h"
#include "intrinsic/world/proto/world_updates.pb.h"
#include "intrinsic/world/service/world_service.pb.h"
#include "intrinsic/world/world.h"

namespace intrinsic {

// Applies the given updates to the world, and returns an error if there was a
// problem updating the world.
absl::Status UpdateWorld(
    const intrinsic_proto::world::WorldUpdates& world_updates, World* world);

// Applies the given update to the world, and returns an error if there was a
// problem updating the world.
absl::Status UpdateWorld(
    const intrinsic_proto::world::WorldUpdate& world_update, World* world);

// Applies the given updates to the world, and returns an error if there was a
// problem updating the world.
absl::Status UpdateWorld(
    const intrinsic_proto::world::internal::WorldServerUpdates& world_updates,
    World* world);

// Applies the given update to the world, and returns an error if there was a
// problem updating the world.
absl::Status UpdateWorld(
    const intrinsic_proto::world::internal::WorldServerUpdate& world_update,
    World* world);

// Applies the given update to the world, and returns an error if there was a
// problem updating the world.
absl::Status UpdateWorld(const intrinsic_proto::world::MoveGroup& move_group,
                         World* world);

// Applies the given update to the world, and returns an error if there was a
// problem updating the world.
absl::Status UpdateWorld(const intrinsic_proto::world::AddLabel& label_spec,
                         World* world);

// Applies the given update to the world, and returns an error if there was a
// problem updating the world.
absl::Status UpdateWorld(const intrinsic_proto::world::RemoveLabel& label_spec,
                         World* world);

// Applies the given update to the world, and returns an error if there was a
// problem updating the world.
absl::Status UpdateWorld(
    const intrinsic_proto::world::SetDofValues& dof_value_change, World* world);

// Applies the given update to the world, and returns an error if there was a
// problem updating the world.
absl::Status UpdateWorld(
    const intrinsic_proto::world::SetRobotDofValues& dof_value_change,
    World* world);

// Applies the given update to the world, and returns an error if there was a
// problem updating the world.
absl::Status UpdateWorld(
    const intrinsic_proto::world::SelfExclusions& self_exclusions,
    World* world);

// Applies the given update to the world, and returns an error if there was a
// problem updating the world.
absl::Status UpdateWorld(const intrinsic_proto::world::SetAlias& set_alias,
                         World* world);

// Applies the given update to the world, and returns an error if there was a
// problem updating the world.
absl::Status UpdateWorld(
    const intrinsic_proto::world::SetObjectNames& set_object_names,
    World* world);

// Applies the given update to the world, and returns an error if there was a
// problem updating the world.
absl::Status UpdateWorld(const intrinsic_proto::world::AddEntity& add_entity,
                         World* world);

// Applies the given update to the world, and returns an error if there was a
// problem updating the world.
absl::Status UpdateWorld(
    const intrinsic_proto::world::SetRobotIkSolverKey& set_robot_ik_solver_key,
    World* world);

// Applies the given update to the world, and returns an error if there was a
// problem updating the world.
absl::Status UpdateWorld(
    const intrinsic_proto::world::CollisionExclusion& collision_exclusions,
    World* world);

// Applies the given update to the world, and returns an error if there was a
// problem updating the world.
absl::Status UpdateWorld(
    const intrinsic_proto::world::SetRobotTip& set_robot_tip, World* world);

// Applies the given update to the world, and returns an error if there was a
// problem updating the world.
absl::Status UpdateWorld(
    const intrinsic_proto::world::ReparentEntities& reparent_entity,
    World* world);

// Applies the given update to the world, and returns an error if there was a
// problem updating the world.
absl::Status UpdateWorld(
    const intrinsic_proto::world::UpdateAttachmentPose& update_attachment,
    World* world);

// Applies the given update to the world, and returns an error if there was a
// problem updating the world.
absl::Status UpdateWorld(
    const intrinsic_proto::world::RenameEntity& rename_entity, World* world);

// Applies the given update to the world, and returns an error if there was a
// problem updating the world.
absl::Status UpdateWorld(
    const intrinsic_proto::world::SetNamedConfiguration& named_config,
    World* world);

// Applies the given update to the world, and returns an error if there was a
// problem updating the world
absl::Status UpdateWorld(
    const intrinsic_proto::world::RenameGroup& rename_group, World* world);

// Sanity check for the imported kinematics. Returns INTERNAL_ERROR if the
// kinematics are not consistent.
absl::Status CheckKinematics(const World& world);

// Returns true if the world updates contain only changes to the state of
// the world and not its structure. State in this case can mean things like
// parent_t_this and dof values.
bool IsOnlyStateChange(
    const intrinsic_proto::world::WorldUpdates& world_updates);

// Returns true if the world update contain only changes to the state of
// the world and not its structure. State in this case can mean things like
// parent_t_this and dof values.
bool IsOnlyStateChange(const intrinsic_proto::world::WorldUpdate& world_update);

// Returns true if the world updates contain only changes to the state of
// the world and not its structure. State in this case can mean things like
// parent_t_this and dof values.
bool IsOnlyStateChange(
    const intrinsic_proto::world::internal::WorldServerUpdates& world_updates);

// Returns true if the world update contain only changes to the state of
// the world and not its structure. State in this case can mean things like
// parent_t_this and dof values.
bool IsOnlyStateChange(
    const intrinsic_proto::world::internal::WorldServerUpdate& world_update);

// Returns true if the world updates contain only changes to dofs in the world
// and not its structure.
bool IsOnlyDofChange(
    const intrinsic_proto::world::internal::WorldServerUpdates& world_updates);

// Returns true if the world update contains only changes to dofs in the world
// and not its structure.
bool IsOnlyDofChange(
    const intrinsic_proto::world::internal::WorldServerUpdate& world_update);

}  // namespace intrinsic

#endif  // INTRINSIC_WORLD_UPDATE_WORLD_H_
