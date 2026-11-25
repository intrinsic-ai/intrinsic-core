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

#ifndef INTRINSIC_SIMULATION_GAZEBO_PLUGINS_OBJECT_INTERACTION_MODERATOR_H_
#define INTRINSIC_SIMULATION_GAZEBO_PLUGINS_OBJECT_INTERACTION_MODERATOR_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/flags/declare.h"
#include "absl/status/status.h"
#include "absl/time/time.h"
#include "gz/sim/Entity.hh"
#include "gz/sim/EntityComponentManager.hh"
#include "gz/sim/EventManager.hh"
#include "gz/sim/Link.hh"
#include "gz/sim/System.hh"
#include "gz/sim/Types.hh"
#include "gz/sim/components/AxisAlignedBox.hh"
#include "sdf/Element.hh"

ABSL_DECLARE_FLAG(double, object_interaction_moderator_deactivation_time_sec);
ABSL_DECLARE_FLAG(double,
                  object_interaction_moderator_contact_update_interval_sec);

namespace intrinsic {
namespace simulation {

// The ObjectInteractionModerator plugin is associated with a floating non-robot
// object (non-static and not connected to world via fixed joint either directly
// or transitively). The plugin moderates its interaction with the rest of the
// workcell.
// When the object is in proximity to a robot tip, identified by the
// Interaction component associated with the tip link, accurate physics is
// enabled for the object.
// When the object is not in proximity to a robot tip, but is touching a
// neutral surface, the object will be temporarily welded to the surface with a
// fixed joint, which reduces physics accuracy but improves simulation speed.
// Notes:
// - The plugin should be under model tag of a floating object. An optional
//   robot_tip_proximity_aabb_scale parameter can be set in the range [1, inf)
//   to control how far the robot tip will be when proximity is detected to the
//   object.
// e.g.
// <model>
//   <link>...</link>
//   <plugin
//   filename="static://intrinsic::simulation::ObjectInteractionModerator"
//           name="intrinsic::simulation::ObjectInteractionModerator">
//     <robot_tip_proximity_aabb_scale>1.1</robot_tip_proximity_aabb_scale>
// </model>
//
// - Proximity to robot tip link is identified by a bounding-box
//   intersection check. So accurate physics may be enabled for the object
//   even when it is at a distance from the robot tip if the tip's collision
//   geometry differs significantly from its bounding box.
// - It is expected that the object model will have a single link.
class ObjectInteractionModerator : public gz::sim::System,
                                   public gz::sim::ISystemConfigure,
                                   public gz::sim::ISystemPreUpdate,
                                   public gz::sim::ISystemPostUpdate {
 public:
  enum InteractionState {
    kFree,
    kProximityToRobotObject,
    kAttachedToRobotObject,
    kSettlingOnNonRobotObject,
    kTouchingNonRobotObject
  };

  ObjectInteractionModerator();

  ~ObjectInteractionModerator() override;

  void Configure(const gz::sim::Entity& entity,
                 const std::shared_ptr<const sdf::Element>& sdf,
                 gz::sim::EntityComponentManager& ecm,
                 gz::sim::EventManager& eventMgr) override;

  void PreUpdate(const gz::sim::UpdateInfo& info,
                 gz::sim::EntityComponentManager& ecm) override;

  void PostUpdate(const gz::sim::UpdateInfo& info,
                  const gz::sim::EntityComponentManager& ecm) override;

  InteractionState State() const { return interaction_state_; }

  gz::sim::Entity AttachedToLink() const { return attached_to_link_; }

  double RobotTipProximityAABBScale() const {
    return robot_tip_proximity_aabb_scale_;
  }

  double DeactivationTimeSec() const {
    return absl::ToDoubleSeconds(deactivation_time_);
  }

 private:
  absl::flat_hash_set<gz::sim::Entity> CheckContactToNonRobotObject(
      const gz::sim::EntityComponentManager& ecm) const;
  std::optional<gz::sim::Entity> CheckProximityToRobotTip(
      const gz::sim::EntityComponentManager& ecm) const;
  std::optional<gz::sim::Entity> CheckAttachedToRobot(
      const gz::sim::EntityComponentManager& ecm) const;
  bool CheckReadyToDeactivate(const gz::sim::EntityComponentManager& ecm,
                              absl::Duration dt);
  void UpdateState(const gz::sim::EntityComponentManager& ecm,
                   absl::Duration dt);
  void UpdateContactSensorDataComponents(gz::sim::EntityComponentManager& ecm,
                                         absl::Duration sim_time);
  absl::Status RemoveNonRobotDetachableJoint(
      gz::sim::EntityComponentManager& ecm);

  struct RobotTipInfo {
    gz::sim::Entity model = gz::sim::kNullEntity;
    std::string link_name;
    bool aabb_ensured = false;
  };

  std::string name_;
  gz::sim::Entity model_entity_ = gz::sim::kNullEntity;
  gz::sim::Link link_;
  std::vector<gz::sim::Entity> link_collision_entities_;
  absl::flat_hash_map<gz::sim::Entity, RobotTipInfo> robot_tips_;
  InteractionState interaction_state_ = InteractionState::kFree;
  gz::sim::Entity attached_to_link_ = gz::sim::kNullEntity;
  gz::sim::Entity non_robot_detachable_joint_ = gz::sim::kNullEntity;
  // Surface joints whose removal this moderator requested but which the ECM has
  // not yet removed (entity removal is processed after PostUpdate). Tracked so
  // CheckAttachedToRobot does not misidentify them as external attachments on
  // the same tick.
  absl::flat_hash_set<gz::sim::Entity> removed_non_robot_detachable_joints_;

  static constexpr double kDefaultRobotTipProximityAABBScale = 1.1;
  static constexpr char kRobotTipProximityAABBScaleTag[] =
      "robot_tip_proximity_aabb_scale";
  double robot_tip_proximity_aabb_scale_;

  // Threshold is the sum of linear and angular velocities squared.
  static constexpr double kDefaultDeactivationMotionThreshold = 0.05;

  // Duration that the object needs to be at rest and in contact with the
  // neutral surface before being deactivated (welded with a fixed joint).
  const absl::Duration deactivation_time_;

  // Time that the object's velocities are below the deactivation motion
  // threshold.
  absl::Duration deactivation_timer_;

  // Interval at which contact sensor data is updated in free state.
  const absl::Duration free_state_contact_update_interval_;

  // Sim time when contact sensor data was last updated.
  absl::Duration last_updated_contact_time_ = -absl::InfiniteDuration();
};

}  // namespace simulation
}  // namespace intrinsic

#endif  // INTRINSIC_SIMULATION_GAZEBO_PLUGINS_OBJECT_INTERACTION_MODERATOR_H_
