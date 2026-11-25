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

#include "intrinsic/simulation/gazebo/plugins/object_interaction_moderator.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/container/flat_hash_set.h"
#include "absl/flags/flag.h"
#include "absl/functional/any_invocable.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/time/time.h"
#include "gz/common/Profiler.hh"
#include "gz/math/AxisAlignedBox.hh"
#include "gz/math/Vector3.hh"
#include "gz/msgs/MessageTypes.hh"
#include "gz/sim/Entity.hh"
#include "gz/sim/EntityComponentManager.hh"
#include "gz/sim/EventManager.hh"
#include "gz/sim/Link.hh"
#include "gz/sim/Model.hh"
#include "gz/sim/Types.hh"
#include "gz/sim/Util.hh"
#include "gz/sim/components/AxisAlignedBox.hh"
#include "gz/sim/components/ContactSensorData.hh"
#include "gz/sim/components/DetachableJoint.hh"
#include "gz/sim/components/Link.hh"
#include "gz/sim/components/PoseCmd.hh"
#include "intrinsic/simulation/gazebo/components/interaction_component.h"
#include "intrinsic/simulation/gazebo/plugins/link_device_util.h"
#include "intrinsic/util/status/status_macros.h"
#include "sdf/Element.hh"

ABSL_FLAG(double, object_interaction_moderator_deactivation_time_sec, 0.0,
          "Set the time that the object needs to be at rest before being "
          "attached to a neutral surface with a fixed joint. If a value of 0 "
          "is set, objects will be attached to the surface on contact, "
          "possibly before coming to rest.");

ABSL_FLAG(double, object_interaction_moderator_contact_update_interval_sec, 0.0,
          "Set the time interval at which contact sensor data will be checked "
          "for in kFree state.");

namespace intrinsic {
namespace simulation {

using ::gz::math::AxisAlignedBox;
using ::gz::sim::Entity;
using ::gz::sim::kNullEntity;

namespace {
std::string GetEntityName(const Entity& entity,
                          const gz::sim::EntityComponentManager& ecm) {
  return gz::sim::scopedName(entity, ecm);
}

AxisAlignedBox ScaledBoundingBox(const AxisAlignedBox& box, double scale) {
  gz::math::Vector3d center = box.Center();
  gz::math::Vector3d scaled_center_to_max = scale * (box.Max() - center);
  return AxisAlignedBox(center + scaled_center_to_max,
                        center - scaled_center_to_max);
}

template <typename T>
class LazyInitialized {
 public:
  typedef absl::AnyInvocable<T()> Initializer;

  explicit LazyInitialized(Initializer initializer)
      : initializer_(std::move(initializer)) {}

  T operator()() {
    if (!initialized_) {
      value_ = initializer_();
      initialized_ = true;
    }
    return value_;
  }

 private:
  T value_;
  bool initialized_ = false;
  Initializer initializer_;
};

}  // namespace

ObjectInteractionModerator::ObjectInteractionModerator()
    : deactivation_time_(absl::Seconds(absl::GetFlag(
          FLAGS_object_interaction_moderator_deactivation_time_sec))),
      free_state_contact_update_interval_(absl::Seconds(absl::GetFlag(
          FLAGS_object_interaction_moderator_contact_update_interval_sec))) {};

ObjectInteractionModerator::~ObjectInteractionModerator() = default;

void ObjectInteractionModerator::Configure(
    const Entity& entity, const std::shared_ptr<const sdf::Element>& sdf,
    gz::sim::EntityComponentManager& ecm, gz::sim::EventManager& eventMgr) {
  gz::sim::Model model(entity);
  if (!model.Valid(ecm)) {
    LOG(ERROR) << "ObjectInteractionModerator plugin should be a child of a "
               << "model, is child of " << GetEntityName(entity, ecm);
    return;
  }
  name_ = model.Name(ecm);

  // Get single link under the parent model entity.
  Entity link_entity = model.CanonicalLink(ecm);
  link_ = gz::sim::Link(link_entity);
  const std::optional<std::string> link_name_maybe = link_.Name(ecm);
  LOG_IF(WARNING, model.LinkCount(ecm) > 1)
      << "Model " << name_ << " has " << " " << model.LinkCount(ecm)
      << " links. Using link "
      << (link_name_maybe.has_value() ? link_name_maybe.value() : "");

  // Add Interaction component so that we know this is a managed entity.
  Interaction* interaction_component = ecm.CreateComponent(
      link_.Entity(), Interaction(InteractionType::kFloatingObject));
  if (interaction_component == nullptr) {
    LOG(ERROR) << "Couldn't create Interaction component for object " << name_;
  }

  // Cache link collision entities.
  link_collision_entities_ = link_.Collisions(ecm);
  if (link_collision_entities_.empty()) {
    LOG(INFO) << "Found 0 collision entities for object " << name_
              << ". ObjectInteractionModerator is a no-op for this object.";
    link_ = gz::sim::Link(kNullEntity);
    return;
  }

  // Ensure AxisAlignedBox component exists on parent model.
  model_entity_ = model.Entity();
  gz::sim::enableComponent<gz::sim::components::AxisAlignedBox>(
      ecm, model_entity_, /*_enable=*/true);

  bool success = false;
  std::tie(robot_tip_proximity_aabb_scale_, success) = sdf->Get(
      kRobotTipProximityAABBScaleTag, kDefaultRobotTipProximityAABBScale);
  LOG_IF(INFO, !success) << "Using default robot_tip_proximity_aabb_scale "
                         << robot_tip_proximity_aabb_scale_ << " for object "
                         << name_;
  if (robot_tip_proximity_aabb_scale_ < 1) {
    LOG(WARNING) << "robot_tip_proximity_aabb_scale was set below 1 at "
                 << robot_tip_proximity_aabb_scale_ << " for object " << name_
                 << " which is unsafe. Setting to default value of "
                 << kDefaultRobotTipProximityAABBScale << " instead.";
    robot_tip_proximity_aabb_scale_ = kDefaultRobotTipProximityAABBScale;
  }

  if (deactivation_time_ > absl::ZeroDuration()) {
    // Enable velocity checks so that the WorldLinearVelocity and
    // WorldAngularVelocity components are populated for this link.
    link_.EnableVelocityChecks(ecm);

    LOG(INFO) << "Using deactivation time " << deactivation_time_
              << " and motion threshold " << kDefaultDeactivationMotionThreshold
              << " for object " << name_;
  }
}

void ObjectInteractionModerator::PreUpdate(
    const gz::sim::UpdateInfo& info, gz::sim::EntityComponentManager& ecm) {
  if (link_.Entity() == kNullEntity) {
    return;
  }

  for (auto& [link_entity, tip] : robot_tips_) {
    if (!tip.aabb_ensured && ecm.HasEntity(tip.model)) {
      gz::sim::enableComponent<gz::sim::components::AxisAlignedBox>(
          ecm, tip.model, /*_enable=*/true);
      tip.aabb_ensured = true;
    }
  }

  if (non_robot_detachable_joint_ != kNullEntity &&
      !ecm.HasEntity(non_robot_detachable_joint_)) {
    non_robot_detachable_joint_ = kNullEntity;
    if (interaction_state_ == kTouchingNonRobotObject ||
        interaction_state_ == kSettlingOnNonRobotObject) {
      LOG(INFO) << "Non-robot detachable joint for object " << name_
                << " was removed externally. Resetting state to free.";
      attached_to_link_ = kNullEntity;
      interaction_state_ = kFree;
    } else {
      LOG(INFO) << "Non-robot detachable joint for object " << name_
                << " was removed externally.";
    }
  }
  // Forget surface joints whose removal we requested once the ECM has processed
  // their removal. In SimulationRunner this happens after PostUpdate; checking
  // HasEntity defensively keeps any still-present joint (e.g., in tests that
  // defer removal) from being mistaken for an external attachment.
  absl::erase_if(removed_non_robot_detachable_joints_,
                 [&ecm](Entity joint) { return !ecm.HasEntity(joint); });

  if (ecm.EntityHasComponentType(model_entity_,
                                 gz::sim::components::WorldPoseCmd::typeId)) {
    if (non_robot_detachable_joint_ != kNullEntity) {
      LOG(INFO) << "Removing non-robot detachable joint for object " << name_
                << " due to pending WorldPoseCmd.";
      INTR_RETURN_IF_ERROR(RemoveNonRobotDetachableJoint(ecm))
          .With(intrinsic::ExtraMessage()
                << "Couldn't remove non-robot detachable joint for object "
                << name_ << ": ")
          .LogError()
          .With([](const absl::Status& s) { return; });
    }
    if (interaction_state_ == kTouchingNonRobotObject ||
        interaction_state_ == kSettlingOnNonRobotObject) {
      attached_to_link_ = kNullEntity;
      interaction_state_ = kFree;
    }
  }

  // Update components as per new state.
  // If state is kFree, nothing to do
  // If state is kProximityToRobotObject or kAttachedToRobotObject, remove
  // non-robot DetachableJoint if present.
  // If state is kSettlingOnNonRobotObject, nothing to do
  // If state is kTouchingNonRobotObject, add DetachableComponent with
  // attached_to_link_ if not.
  switch (interaction_state_) {
    case kFree: {
      break;
    }
    case kProximityToRobotObject:
    case kAttachedToRobotObject: {
      if (non_robot_detachable_joint_ != kNullEntity) {
        LOG(INFO) << "Removing non-robot detachable joint for object " << name_;
        INTR_RETURN_IF_ERROR(RemoveNonRobotDetachableJoint(ecm))
            .With(intrinsic::ExtraMessage()
                  << "Couldn't remove non-robot detachable joint for object "
                  << name_ << ": ")
            .LogError()
            .With([](const absl::Status& s) { return; });
      }
      break;
    }
    case kSettlingOnNonRobotObject: {
      break;
    }
    case kTouchingNonRobotObject: {
      if (non_robot_detachable_joint_ == kNullEntity) {
        LOG(INFO) << "Adding non-robot detachable joint for object " << name_;
        INTR_ASSIGN_OR_RETURN(
            non_robot_detachable_joint_,
            AddDetachableJoint(attached_to_link_, link_.Entity(), ecm),
            _.With(intrinsic::ExtraMessage()
                   << "Couldn't add non-robot detachable joint for object "
                   << name_ << ": ")
                .LogError()
                .With([](const absl::Status& s) { return; }));
      }
      break;
    }
  }

  // Update contact sensor data components to signal to the Physics System
  // whether or not contact sensor data is required for this object. This is an
  // optimization to avoid unnecessary data copy from the physics engine.
  UpdateContactSensorDataComponents(ecm, absl::FromChrono(info.simTime));
}

void ObjectInteractionModerator::PostUpdate(
    const gz::sim::UpdateInfo& info,
    const gz::sim::EntityComponentManager& ecm) {
  GZ_PROFILE("ObjectInteractionModerator::PostUpdate");

  if (link_.Entity() == kNullEntity) {
    return;
  }

  absl::flat_hash_set<Entity> active_robot_tips;
  ecm.Each<gz::sim::components::Link, Interaction>(
      [this, &ecm, &active_robot_tips](
          const Entity& link_entity,
          const gz::sim::components::Link* link_component,
          const Interaction* interaction_component) -> bool {
        if (link_entity != link_.Entity() &&
            interaction_component->Data() == InteractionType::kRobotTip) {
          active_robot_tips.insert(link_entity);
          if (!robot_tips_.contains(link_entity)) {
            if (std::optional<gz::sim::Model> model =
                    gz::sim::Link(link_entity).ParentModel(ecm);
                model.has_value()) {
              robot_tips_[link_entity] = RobotTipInfo{
                  .model = model->Entity(),
                  .link_name = GetEntityName(link_entity, ecm),
              };
            }
          }
        }
        return true;
      });
  absl::erase_if(robot_tips_, [&active_robot_tips](const auto& entry) {
    return !active_robot_tips.contains(entry.first);
  });

  UpdateState(ecm, absl::FromChrono(info.dt));
}

void ObjectInteractionModerator::UpdateState(
    const gz::sim::EntityComponentManager& ecm, absl::Duration dt) {
  // Allowed state transitions:
  // kFree -> (has external DetachableJoint) -> kAttachedToRobotObject
  // kFree -> (is close to a robot tool link) -> kProximityToRobotObject
  // kFree -> (no pending WorldPoseCmd and is in contact with one or more
  //   non-robot object links) -> kSettlingOnNonRobotObject (or
  //   kTouchingNonRobotObject if deactivation time is zero).
  // Note: Contacts with other non-robot floating objects are ignored.

  // kSettlingOnNonRobotObject -> (has external DetachableJoint) ->
  //   kAttachedToRobotObject
  // kSettlingOnNonRobotObject -> (is close to a robot tool link) ->
  //   kProximityToRobotObject
  // kSettlingOnNonRobotObject -> (has pending WorldPoseCmd or lost contact with
  //   non-robot object) -> kFree
  // kSettlingOnNonRobotObject -> (is at rest and remains in contact with
  //   non-robot object links for a certain period of time) ->
  //   kTouchingNonRobotObject

  // kProximityToRobotObject -> (has external DetachableJoint) ->
  //   kAttachedToRobotObject
  // kProximityToRobotObject -> (not close to a robot tool link any more) ->
  //   kFree

  // kAttachedToRobotObject -> (doesn't have external DetachableJoint and is
  //   close to a robot tool link) -> kProximityToRobotObject
  // kAttachedToRobotObject -> (doesn't have external DetachableJoint and is not
  //   close to a robot tool link) -> kFree
  // Note: We don't allow directly transitioning from kAttachedToRobotObject to
  // kTouchingNonRobotObject to ensure that the DetachableJoint is first
  // removed and then added again.

  // kTouchingNonRobotObject -> (has external DetachableJoint) ->
  //   kAttachedToRobotObject
  // kTouchingNonRobotObject -> (is close to a robot tool link) ->
  //   kProximityToRobotObject
  // kTouchingNonRobotObject -> (has pending WorldPoseCmd and surface
  //   DetachableJoint is not present) -> kFree
  // Note: PreUpdate also resets kTouchingNonRobotObject /
  // kSettlingOnNonRobotObject to kFree when the surface DetachableJoint is
  // removed externally or when WorldPoseCmd is present.

  InteractionState new_state = interaction_state_;

  // Following variables are initialized lazily to avoid initializing them if
  // they are not needed.
  LazyInitialized<std::optional<Entity>> robot_tip_link_in_proximity(
      [this, &ecm]() { return CheckProximityToRobotTip(ecm); });

  LazyInitialized<absl::flat_hash_set<Entity>>
      non_robot_object_links_in_contact(
          [this, &ecm]() { return CheckContactToNonRobotObject(ecm); });

  LazyInitialized<std::optional<Entity>> attached_to_robot_link(
      [this, &ecm]() { return CheckAttachedToRobot(ecm); });

  const bool has_world_pose_cmd = ecm.EntityHasComponentType(
      model_entity_, gz::sim::components::WorldPoseCmd::typeId);

  // Check for transitions.
  switch (interaction_state_) {
    case kFree: {
      if (attached_to_robot_link().has_value()) {
        new_state = kAttachedToRobotObject;
      } else if (robot_tip_link_in_proximity().has_value()) {
        new_state = kProximityToRobotObject;
      } else if (!has_world_pose_cmd &&
                 !non_robot_object_links_in_contact().empty()) {
        if (deactivation_time_ > absl::ZeroDuration()) {
          new_state = kSettlingOnNonRobotObject;
        } else {
          new_state = kTouchingNonRobotObject;
        }
      }
      break;
    }
    case kProximityToRobotObject: {
      if (attached_to_robot_link().has_value()) {
        new_state = kAttachedToRobotObject;
      } else if (!robot_tip_link_in_proximity().has_value()) {
        new_state = kFree;
      }
      break;
    }
    case kAttachedToRobotObject: {
      if (!attached_to_robot_link().has_value()) {
        new_state = robot_tip_link_in_proximity().has_value()
                        ? kProximityToRobotObject
                        : kFree;
      }
      break;
    }
    case kSettlingOnNonRobotObject: {
      if (attached_to_robot_link().has_value()) {
        new_state = kAttachedToRobotObject;
      } else if (robot_tip_link_in_proximity().has_value()) {
        new_state = kProximityToRobotObject;
      } else if (has_world_pose_cmd ||
                 non_robot_object_links_in_contact().empty()) {
        new_state = kFree;
      } else if (CheckReadyToDeactivate(ecm, dt)) {
        new_state = kTouchingNonRobotObject;
      }
      break;
    }
    case kTouchingNonRobotObject: {
      if (attached_to_robot_link().has_value()) {
        new_state = kAttachedToRobotObject;
      } else if (robot_tip_link_in_proximity().has_value()) {
        new_state = kProximityToRobotObject;
      } else if (has_world_pose_cmd &&
                 non_robot_detachable_joint_ == kNullEntity) {
        new_state = kFree;
      }
      break;
    }
  }
  if (new_state == interaction_state_) {
    return;
  }

  // Update variables for new state.
  switch (new_state) {
    case kFree: {
      attached_to_link_ = kNullEntity;
      LOG(INFO) << "Object " << name_ << " in state " << interaction_state_
                << " is now free. Switching to state " << new_state;
      break;
    }
    case kProximityToRobotObject: {
      CHECK(robot_tip_link_in_proximity().has_value());
      CHECK(!attached_to_robot_link().has_value());
      attached_to_link_ = kNullEntity;
      LOG(INFO)
          << "Object " << name_ << " in state " << interaction_state_
          << " is close to robot link "
          << robot_tips_.at(robot_tip_link_in_proximity().value()).link_name
          << ". Switching to state " << new_state;
      break;
    }
    case kAttachedToRobotObject: {
      attached_to_link_ = attached_to_robot_link().value();
      LOG(INFO) << "Object " << name_ << " in state " << interaction_state_
                << " is attached to robot at link "
                << GetEntityName(attached_to_link_, ecm)
                << ". Switching to state " << new_state;
      break;
    }
    case kSettlingOnNonRobotObject: {
      deactivation_timer_ = absl::ZeroDuration();
      LOG(INFO) << "Object " << name_ << " in state " << interaction_state_
                << " is touching " << non_robot_object_links_in_contact().size()
                << " links. Switching to state " << new_state;
      break;
    }
    case kTouchingNonRobotObject: {
      // Attach to first entity.
      CHECK(!non_robot_object_links_in_contact().empty());
      attached_to_link_ = *non_robot_object_links_in_contact().begin();
      LOG(INFO) << "Object " << name_ << " in state " << interaction_state_
                << " is touching " << non_robot_object_links_in_contact().size()
                << " links. Attaching to "
                << GetEntityName(attached_to_link_, ecm)
                << ". Switching to state " << new_state;
      break;
    }
  }
  interaction_state_ = new_state;
}

std::optional<gz::sim::Entity>
ObjectInteractionModerator::CheckProximityToRobotTip(
    const gz::sim::EntityComponentManager& ecm) const {
  // We check for proximity by testing whether the AABB for this object's model
  // intersects the AABB of a robot tip model. To add a margin to the check,
  // both AABBs are first scaled up.
  std::optional<AxisAlignedBox> self_aabb =
      ecm.ComponentData<gz::sim::components::AxisAlignedBox>(model_entity_);
  if (!self_aabb.has_value() || self_aabb->Volume() == 0) {
    // The bounding box is uninitialized, so skip proximity check.
    return std::nullopt;
  }

  AxisAlignedBox self_aabb_scaled =
      ScaledBoundingBox(*self_aabb, robot_tip_proximity_aabb_scale_);

  for (const auto& [link_entity, tip] : robot_tips_) {
    std::optional<AxisAlignedBox> tip_aabb =
        ecm.ComponentData<gz::sim::components::AxisAlignedBox>(tip.model);
    if (!tip_aabb.has_value() || tip_aabb->Volume() == 0) {
      continue;
    }
    AxisAlignedBox aabb_scaled =
        ScaledBoundingBox(*tip_aabb, robot_tip_proximity_aabb_scale_);
    if (self_aabb_scaled.Intersects(aabb_scaled)) {
      return link_entity;
    }
  }
  return std::nullopt;
}

absl::flat_hash_set<Entity>
ObjectInteractionModerator::CheckContactToNonRobotObject(
    const gz::sim::EntityComponentManager& ecm) const {
  absl::flat_hash_set<Entity> objects_in_contact;
  // Loop through contact sensor data.
  for (Entity collision_entity : link_collision_entities_) {
    std::optional<gz::msgs::Contacts> contact_data_optional =
        ecm.ComponentData<gz::sim::components::ContactSensorData>(
            collision_entity);

    if (!contact_data_optional.has_value()) {
      continue;
    }

    const gz::msgs::Contacts& contact_data = contact_data_optional.value();
    for (int i = 0; i < contact_data.contact_size(); i++) {
      Entity parent_link_entity = kNullEntity;
      auto collision1 = contact_data.contact(i).collision1();
      if (collision1.id() != collision_entity) {
        parent_link_entity = ecm.ParentEntity(collision1.id());
      }
      auto collision2 = contact_data.contact(i).collision2();
      if (collision2.id() != collision_entity) {
        // Safe to overwrite since only one of the parent links can be
        // different from link_.
        parent_link_entity = ecm.ParentEntity(collision2.id());
      }

      if (objects_in_contact.contains(parent_link_entity)) {
        continue;
      }

      // Ignore contact with links which are either other floating objects or
      // robot tips.
      std::optional<int> interaction_type =
          ecm.ComponentData<Interaction>(parent_link_entity);
      if (interaction_type.has_value() &&
          (*interaction_type == InteractionType::kFloatingObject ||
           *interaction_type == InteractionType::kRobotTip)) {
        continue;
      }

      // Ignore contact with links which share the same parent model as a
      // robot tip.
      gz::sim::Link parent_link(parent_link_entity);
      std::optional<gz::sim::Model> model = parent_link.ParentModel(ecm);
      if (model.has_value() &&
          absl::c_none_of(robot_tips_, [&](const auto& entry) {
            return entry.second.model == model->Entity();
          })) {
        objects_in_contact.insert(parent_link_entity);
      }
    }
  }
  return objects_in_contact;
}

absl::Status ObjectInteractionModerator::RemoveNonRobotDetachableJoint(
    gz::sim::EntityComponentManager& ecm) {
  INTR_RETURN_IF_ERROR(RemoveDetachableJoint(non_robot_detachable_joint_, ecm));
  removed_non_robot_detachable_joints_.insert(non_robot_detachable_joint_);
  non_robot_detachable_joint_ = kNullEntity;
  return absl::OkStatus();
}

std::optional<gz::sim::Entity> ObjectInteractionModerator::CheckAttachedToRobot(
    const gz::sim::EntityComponentManager& ecm) const {
  std::optional<Entity> robot_link = std::nullopt;

  ecm.Each<gz::sim::components::DetachableJoint>(
      [this, &robot_link](
          const Entity& joint,
          const gz::sim::components::DetachableJoint* component) {
        if (component->Data().childLink == link_.Entity() &&
            joint != non_robot_detachable_joint_ &&
            !removed_non_robot_detachable_joints_.contains(joint)) {
          robot_link = component->Data().parentLink;
          return false;
        }
        return true;
      });

  return robot_link;
}

bool ObjectInteractionModerator::CheckReadyToDeactivate(
    const gz::sim::EntityComponentManager& ecm, absl::Duration dt) {
  if (deactivation_time_ <= absl::ZeroDuration()) return true;
  gz::math::Vector3d lin_vel = *link_.WorldLinearVelocity(ecm);
  gz::math::Vector3d ang_vel = *link_.WorldAngularVelocity(ecm);
  double motion = lin_vel.SquaredLength();
  motion += ang_vel.SquaredLength();
  if (motion < kDefaultDeactivationMotionThreshold) {
    deactivation_timer_ += dt;
  } else {
    deactivation_timer_ = absl::ZeroDuration();
  }
  return deactivation_timer_ > deactivation_time_;
}

void ObjectInteractionModerator::UpdateContactSensorDataComponents(
    gz::sim::EntityComponentManager& ecm, absl::Duration sim_time) {
  // When this object's link is promoted to kRobotTip (e.g. a detachable finger
  // attached to a FixedJointGripper), the gripper manages ContactSensorData on
  // its collision shapes; avoid overwriting it here.
  if (ecm.ComponentData<Interaction>(link_.Entity()) ==
      InteractionType::kRobotTip) {
    return;
  }

  bool need_contact_sensor_data;
  switch (interaction_state_) {
    case kFree: {
      need_contact_sensor_data =
          (last_updated_contact_time_ + free_state_contact_update_interval_) <=
          sim_time;
      break;
    }
    case kSettlingOnNonRobotObject: {
      need_contact_sensor_data = true;
      break;
    }
    case kProximityToRobotObject:
    case kAttachedToRobotObject:
    case kTouchingNonRobotObject:
      need_contact_sensor_data = false;
      break;
  };

  for (Entity collision_entity : link_collision_entities_) {
    gz::sim::enableComponent<gz::sim::components::ContactSensorData>(
        ecm, collision_entity, /*_enable=*/need_contact_sensor_data);
  }

  if (need_contact_sensor_data) {
    last_updated_contact_time_ = sim_time;
  }
}

}  // namespace simulation
}  // namespace intrinsic
