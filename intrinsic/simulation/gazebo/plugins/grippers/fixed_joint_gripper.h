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

#ifndef INTRINSIC_SIMULATION_GAZEBO_PLUGINS_GRIPPERS_FIXED_JOINT_GRIPPER_H_
#define INTRINSIC_SIMULATION_GAZEBO_PLUGINS_GRIPPERS_FIXED_JOINT_GRIPPER_H_

#include <memory>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "gz/sim/Entity.hh"
#include "gz/sim/EntityComponentManager.hh"
#include "gz/sim/Link.hh"
#include "intrinsic/simulation/gazebo/components/articulation_component.h"
#include "intrinsic/simulation/gazebo/plugins/grippers/gripper.pb.h"

namespace intrinsic {
namespace simulation {

// An implementation of a Gripper in Gazebo that "welds" the grasped object to
// the gripper with a fixed joint when commanded to grip.
//
// Contact surface: an object is considered graspable when it is in contact
// with the gripper link *or with any link that is rigidly attached to the
// gripper link* (e.g. custom fingers that are nested in the gripper model with
// a fixed joint, or that were attached at run time with a DetachableJoint).
// This "contact surface" is recomputed whenever the rigid-attachment topology
// of the ECM changes, so fingers can be attached, removed and re-attached at
// run time. The gripper's own payload joint and anything behind another
// gripper's tip link are never part of the contact surface. The grasped object
// is always welded to the gripper link itself.
class FixedJointGripper {
 public:
  FixedJointGripper(const std::string& plugin_name,
                    const std::string& gripper_link_name,
                    double grasp_debounce_time_seconds,
                    ::gz::sim::Link gripper_link);

  static absl::StatusOr<std::unique_ptr<FixedJointGripper>> Create(
      const ::gz::sim::Entity& plugin_parent, absl::string_view plugin_name,
      absl::string_view gripper_link_name, double grasp_debounce_time_seconds,
      ::gz::sim::EntityComponentManager& ecm);

  void OnCommand(
      const intrinsic_proto::simulation::gazebo::GripperCommand& command);
  const intrinsic_proto::simulation::gazebo::GripperCommand& GetCommand()
      const {
    return command_;
  }
  const intrinsic_proto::simulation::gazebo::GripperStatus& GetStatus() const {
    return status_;
  }

  bool IsGrasping() const;
  // returns kNullEntity if no entity is currently grasped
  ::gz::sim::Entity GraspedLink() const;

  // Links (other than the gripper link) that are currently part of the contact
  // surface, e.g. attached custom fingers.
  const absl::flat_hash_set<::gz::sim::Entity>& ToolLinks() const {
    return tool_link_entities_;
  }
  // Collision entities that are currently monitored for contacts.
  const std::vector<::gz::sim::Entity>& CollisionTestEntities() const {
    return collision_test_entities_;
  }

  // Processes command_ and update the joints and links related to the grasping
  // status. Intended to be called in PreUpdate. Depending on the last
  // received grip/release command and the current grasp
  // attached/detached state, we decide whether or not to join the gripper link
  // to the object last detected in the collision entity's contacts list.
  // Also refreshes the contact surface if the rigid-attachment topology
  // changed since the last call.
  void ProcessCommand(::gz::sim::EntityComponentManager& ecm);

  // Updates grasped_object_link_entity_ status related states. Intended to be
  // called in PostUpdate. We check for the contacts enumerated by the
  // Physics engine for the collision entity. If there is a valid contact, we
  // store the link entity for the object in contact.
  void UpdateGraspedObject(const ::gz::sim::EntityComponentManager& ecm);

 protected:
  std::string plugin_name_;

  ::gz::sim::Link gripper_link_;
  // Collisions of gripper_link_ and of all tool_link_entities_.
  std::vector<::gz::sim::Entity> collision_test_entities_;
  ::gz::sim::Entity detachable_joint_entity_ = ::gz::sim::kNullEntity;
  ::gz::sim::Entity grasped_object_link_entity_ = ::gz::sim::kNullEntity;

  std::string gripper_link_name_;

 private:
  void UpdateContactSensorDataComponent(
      ::gz::sim::EntityComponentManager& ecm) const;

  // Returns the set of payload joints owned by this gripper (active or pending
  // removal) that must be excluded when computing the contact surface.
  absl::flat_hash_set<::gz::sim::Entity> ExcludedPayloadJoints() const;

  // Recomputes tool_link_entities_ and collision_test_entities_ from the
  // current rigid-attachment topology of the ECM, and updates the
  // ContactSensorData and Interaction components accordingly.
  void RefreshContactSurface(::gz::sim::EntityComponentManager& ecm);
  // Calls RefreshContactSurface() if the rigid-attachment topology changed
  // since the last refresh, or if the payload joint was removed externally.
  void MaybeRefreshContactSurface(::gz::sim::EntityComponentManager& ecm);

  // Links rigidly attached as descendants of gripper_link_ (excluding
  // gripper_link_ itself).
  absl::flat_hash_set<::gz::sim::Entity> tool_link_entities_;
  // Signature of the rigid-attachment topology at the last refresh.
  std::vector<::gz::sim::Entity> topology_signature_;
  // Payload joints whose removal this gripper requested but which the ECM has
  // not yet removed. They must not be traversed when computing the contact
  // surface.
  absl::flat_hash_set<::gz::sim::Entity> released_payload_joints_;
  // Tool links on which this gripper created the Interaction(kRobotTip)
  // component (so that it can be removed again when the link is detached).
  absl::flat_hash_set<::gz::sim::Entity> tagged_tool_links_;

  intrinsic_proto::simulation::gazebo::GripperCommand command_;
  intrinsic_proto::simulation::gazebo::GripperStatus status_;
  double grasp_debounce_time_seconds_;
  // Time the last attach or detach occurred.
  absl::Time time_last_action_change_ = absl::InfinitePast();

  // Cached values of ArticulationType for links that the gripper has been in
  // contact with. The cache is periodically cleared by updating the
  // Articulation component for the link in the ECM. This cache is needed since
  // the `ArcitulationType` is computed in `UpdateGraspedObject()` above which
  // takes a const reference to the ECM.
  absl::flat_hash_map<::gz::sim::Entity, ArticulationType>
      link_to_articulation_type_cache_;
  void UpdateArticulationComponent(::gz::sim::EntityComponentManager& ecm);
};

}  // namespace simulation
}  // namespace intrinsic

#endif  // INTRINSIC_SIMULATION_GAZEBO_PLUGINS_GRIPPERS_FIXED_JOINT_GRIPPER_H_
