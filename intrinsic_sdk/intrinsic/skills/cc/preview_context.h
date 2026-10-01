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

#ifndef INTRINSIC_SKILLS_CC_PREVIEW_CONTEXT_H_
#define INTRINSIC_SKILLS_CC_PREVIEW_CONTEXT_H_

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "intrinsic/geometry/storage/geometry_library.h"  
#include "intrinsic/logging/proto/context.pb.h"
#include "intrinsic/motion_planning/motion_planner_client.h"
#include "intrinsic/skills/cc/equipment_pack.h"
#include "intrinsic/skills/cc/skill_canceller.h"
#include "intrinsic/skills/cc/skill_logging_context.h"
#include "intrinsic/world/objects/frame.h"
#include "intrinsic/world/objects/kinematic_object.h"
#include "intrinsic/world/objects/object_world_client.h"
#include "intrinsic/world/objects/world_object.h"
#include "intrinsic/world/proto/object_world_updates.pb.h"

namespace intrinsic {
namespace skills {

// Provides extra metadata and functionality for a Skill::Preview call.
//
// It is provided by the skill service to a skill and allows access to the world
// and other services that a skill may use.
class PreviewContext {
 public:
  virtual ~PreviewContext() = default;

  // A unique identifier shared across all interactions with the Skill for a
  // single activation of a Skill node in a Process (including any preparation,
  // planning, or execution calls).
  virtual absl::string_view context_id() const = 0;

  // Supports cooperative cancellation of the skill.
  virtual SkillCanceller& canceller() const = 0;

  // The logging context of the execution.
  virtual const SkillLoggingContext& logging_context() const = 0;

  // A client for the motion planning service.
  virtual motion_planning::MotionPlannerClient& motion_planner() = 0;

  // A client for interacting with the object world.
  //
  // NOTE: This client should be treated as read-only. Any effect the skill is
  // expected to have on the physical world should be recorded using
  // RecordWorldUpdate(). (See further explanation in Skill::Preview().)
  virtual world::ObjectWorldClient& object_world() = 0;


  // The GeometryLibrary associated with this context.
  virtual GeometryLibrary& geometry_library() const = 0;


  // Returns the frame by name for an object corresponding to some equipment.
  //
  // The frame is sourced from the same world that's available via
  // object_world().
  virtual absl::StatusOr<world::Frame> GetFrameForEquipment(
      absl::string_view equipment_name, absl::string_view frame_name) = 0;

  // Returns the kinematic object that corresponds to this equipment.
  //
  // The kinematic object is sourced from the same world that's available via
  // object_world().
  virtual absl::StatusOr<world::KinematicObject> GetKinematicObjectForEquipment(
      absl::string_view equipment_name) = 0;

  // Returns the world object that corresponds to this equipment.
  //
  // The world object is sourced from the same world that's available via
  // object_world().
  virtual absl::StatusOr<world::WorldObject> GetObjectForEquipment(
      absl::string_view equipment_name) = 0;

  // Records a world update that the skill is expected to make.
  //
  // `elapsed` is the expected amount of (non-negative) elapsed time since the
  // start of the previous update (NOT since the start of skill execution).
  //
  // `duration` is the expected duration of the update.
  virtual absl::Status RecordWorldUpdate(
      const intrinsic_proto::world::ObjectWorldUpdate& update,
      absl::Duration elapsed, absl::Duration duration) = 0;

 private:
  virtual EquipmentPack& equipment() = 0;
};

}  // namespace skills
}  // namespace intrinsic

#endif  // INTRINSIC_SKILLS_CC_PREVIEW_CONTEXT_H_
