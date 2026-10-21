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

#include "intrinsic/skills/apps/attach_object_to_robot.h"

#include <memory>
#include <optional>

#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/proto_conversion.h"
#include "intrinsic/skills/apps/attach_object_to_robot.pb.h"
#include "intrinsic/skills/cc/skill_interface.h"
#include "intrinsic/skills/cc/skill_utils.h"
#include "intrinsic/skills/proto/footprint.pb.h"
#include "intrinsic/skills/proto/skill_service.pb.h"
#include "intrinsic/stats/scoped_span.h"
#include "intrinsic/util/proto_time.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/objects/kinematic_object.h"
#include "intrinsic/world/objects/object_entity_filter.h"
#include "intrinsic/world/objects/object_world_client.h"
#include "intrinsic/world/objects/world_object.h"
#include "intrinsic/world/proto/object_world_updates.pb.h"

namespace intrinsic::skills {
namespace {

// Returns the entity filter to use when attaching an object to `gripper`.
// If `gripper` is a robot arm (indicated by having an ISO flange frame),
// attach to its final entity (the flange) rather than the stationary base.
// Otherwise (e.g., for single- or multi-finger grippers whose base is already
// mounted to the robot flange), attach to its base entity.
world::ObjectEntityFilter GetAttachmentEntityFilter(
    const world::WorldObject& gripper) {
  std::optional<world::KinematicObject> kinematic_object =
      world::KinematicObject::FromTransformNode(gripper);
  if (kinematic_object.has_value() &&
      !kinematic_object->IsoFlangeFrameIds().empty()) {
    return world::ObjectEntityFilter().IncludeFinalEntity();
  }
  return world::ObjectEntityFilter().IncludeBaseEntity();
}

}  // namespace

std::unique_ptr<SkillInterface> AttachObjectToRobotSkill::CreateSkill() {
  return std::make_unique<AttachObjectToRobotSkill>();
}

absl::StatusOr<intrinsic_proto::skills::Footprint>
AttachObjectToRobotSkill::GetFootprint(const GetFootprintRequest& request,
                                       GetFootprintContext& context) const {
  const stats::ScopedSpan span("skills.AttachObjectToRobotSkill/GetFootprint");
  INTR_ASSIGN_OR_RETURN(
      auto params,
      request.params<intrinsic_proto::skills::AttachObjectToRobotParams>());

  const world::ObjectWorldClient& world = context.object_world();

  INTR_ASSIGN_OR_RETURN(world::WorldObject gripper,
                        world.GetObject(params.gripper_entity()));
  INTR_ASSIGN_OR_RETURN(world::WorldObject object,
                        world.GetObject(params.object_entity()));

  intrinsic_proto::skills::Footprint out;
  ::intrinsic::skills::AddObjectReservation(
      gripper.Name().value(),
      intrinsic_proto::skills::ObjectWorldReservation::WRITE, out);
  ::intrinsic::skills::AddObjectReservation(
      object.Name().value(),
      intrinsic_proto::skills::ObjectWorldReservation::WRITE, out);
  return out;
}

absl::StatusOr<std::unique_ptr<google::protobuf::Message>>
AttachObjectToRobotSkill::ExecuteImpl(
    const intrinsic_proto::skills::AttachObjectToRobotParams& params,
    world::ObjectWorldClient& world) {
  INTR_ASSIGN_OR_RETURN(world::WorldObject gripper,
                        world.GetObject(params.gripper_entity()));
  INTR_ASSIGN_OR_RETURN(world::WorldObject object,
                        world.GetObject(params.object_entity()));

  INTR_RETURN_IF_ERROR(world.ReparentObject(
      object, gripper, GetAttachmentEntityFilter(gripper)));
  INTR_RETURN_IF_ERROR(world.DisableCollisions(object, gripper));

  if (params.has_gripper_t_object()) {
    INTR_ASSIGN_OR_RETURN(Pose3d gripper_t_object,
                          FromProto(params.gripper_t_object()));
    INTR_RETURN_IF_ERROR(
        world.UpdateTransform(gripper, object, gripper_t_object));
  }

  return nullptr;
}

absl::StatusOr<std::unique_ptr<google::protobuf::Message>>
AttachObjectToRobotSkill::Execute(const ExecuteRequest& request,
                                  ExecuteContext& context) {
  const stats::ScopedSpan span("skills.AttachObjectToRobotSkill/Execute");
  INTR_ASSIGN_OR_RETURN(
      auto params,
      request.params<intrinsic_proto::skills::AttachObjectToRobotParams>());

  world::ObjectWorldClient& world = context.object_world();

  return ExecuteImpl(params, world);
}

absl::StatusOr<std::unique_ptr<::google::protobuf::Message>>
AttachObjectToRobotSkill::Preview(const PreviewRequest& request,
                                  PreviewContext& context) {
  const stats::ScopedSpan span("skills.AttachObjectToRobotSkill/Preview");
  INTR_ASSIGN_OR_RETURN(
      auto params,
      request.params<intrinsic_proto::skills::AttachObjectToRobotParams>());

  const world::ObjectWorldClient& world = context.object_world();

  INTR_ASSIGN_OR_RETURN(world::WorldObject gripper,
                        world.GetObject(params.gripper_entity()));
  INTR_ASSIGN_OR_RETURN(world::WorldObject object,
                        world.GetObject(params.object_entity()));

  intrinsic_proto::world::ObjectWorldUpdate reparent_update;
  intrinsic_proto::world::ReparentObjectRequest* reparent_object =
      reparent_update.mutable_reparent_object();
  *reparent_object->mutable_object() = object.ObjectReference();
  *reparent_object->mutable_new_parent() =
      gripper.ObjectReferenceWithEntityFilter(
          GetAttachmentEntityFilter(gripper));
  INTR_RETURN_IF_ERROR(context.RecordWorldUpdate(
      reparent_update, absl::ZeroDuration(), absl::ZeroDuration()));

  intrinsic_proto::world::ObjectWorldUpdate collision_update;
  intrinsic_proto::world::ToggleCollisionsRequest* toggle_collisions =
      collision_update.mutable_toggle_collisions();
  toggle_collisions->set_toggle_mode(
      intrinsic_proto::world::ToggleMode::TOGGLE_MODE_DISABLE);
  *toggle_collisions->mutable_object_a() =
      object.ObjectReferenceWithEntityFilter(
          world::ObjectEntityFilter().IncludeAllEntities());
  *toggle_collisions->mutable_object_b() =
      gripper.ObjectReferenceWithEntityFilter(
          world::ObjectEntityFilter().IncludeAllEntities());
  INTR_RETURN_IF_ERROR(context.RecordWorldUpdate(
      collision_update, absl::ZeroDuration(), absl::ZeroDuration()));

  if (params.has_gripper_t_object()) {
    intrinsic_proto::world::ObjectWorldUpdate transform_update;
    intrinsic_proto::world::UpdateTransformRequest* update_transform =
        transform_update.mutable_update_transform();
    *update_transform->mutable_node_a() = gripper.TransformNodeReference();
    *update_transform->mutable_node_b() = object.TransformNodeReference();
    *update_transform->mutable_a_t_b() = params.gripper_t_object();
    update_transform->set_view(intrinsic_proto::world::ObjectView::BASIC);
    INTR_RETURN_IF_ERROR(context.RecordWorldUpdate(
        transform_update, absl::ZeroDuration(), absl::ZeroDuration()));
  }

  return nullptr;
}

}  // namespace intrinsic::skills
