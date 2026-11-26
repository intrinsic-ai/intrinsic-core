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

#include "intrinsic/skills/apps/detach_object.h"

#include <memory>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/proto_conversion.h"
#include "intrinsic/skills/apps/detach_object.pb.h"
#include "intrinsic/skills/cc/skill_interface.h"
#include "intrinsic/skills/cc/skill_utils.h"
#include "intrinsic/skills/proto/footprint.pb.h"
#include "intrinsic/skills/proto/skill_service.pb.h"
#include "intrinsic/stats/scoped_span.h"
#include "intrinsic/util/proto_time.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/objects/object_entity_filter.h"
#include "intrinsic/world/objects/object_world_client.h"
#include "intrinsic/world/objects/object_world_ids.h"
#include "intrinsic/world/objects/world_object.h"
#include "intrinsic/world/proto/object_world_updates.pb.h"

namespace intrinsic::skills {
namespace {

absl::Status ValidateParams(
    const intrinsic_proto::skills::DetachObjectParams& params) {
  if (!params.has_object_entity()) {
    return absl::InvalidArgumentError(
        "Parameter 'object_entity' has to be set.");
  }
  if (!params.has_gripper_entity()) {
    return absl::InvalidArgumentError(
        "Parameter 'gripper_entity' has to be set.");
  }
  return absl::OkStatus();
}

}  // namespace

absl::StatusOr<intrinsic_proto::skills::Footprint>
DetachObjectSkill::GetFootprint(const GetFootprintRequest& request,
                                GetFootprintContext& context) const {
  const stats::ScopedSpan span("skills.DetachObjectSkill/GetFootprint");
  INTR_ASSIGN_OR_RETURN(
      auto params,
      request.params<intrinsic_proto::skills::DetachObjectParams>());
  INTR_RETURN_IF_ERROR(ValidateParams(params));

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

// LINT.IfChange(detach_world_changes)
absl::StatusOr<std::unique_ptr<google::protobuf::Message>>
DetachObjectSkill::ExecuteImpl(
    const intrinsic_proto::skills::DetachObjectParams& params,
    world::ObjectWorldClient& world) {
  INTR_ASSIGN_OR_RETURN(world::WorldObject gripper,
                        world.GetObject(params.gripper_entity()));
  INTR_ASSIGN_OR_RETURN(world::WorldObject object,
                        world.GetObject(params.object_entity()));
  if (object.ParentId() != gripper.Id()) {
    return absl::InvalidArgumentError(
        absl::StrCat("The object specified as 'object_entity' is not a child "
                     "of the 'gripper_entity'.",
                     "\n\tobject name: ", object.Name().value(),
                     "\n\tgripper name: ", gripper.Name().value()));
  }

  INTR_ASSIGN_OR_RETURN(Pose3d gripper_t_object,
                        world.GetTransform(gripper, object));
  gripper_t_object.translation()[2] += 0.001;
  INTR_RETURN_IF_ERROR(
      world.UpdateTransform(gripper, object, gripper_t_object));

  INTR_ASSIGN_OR_RETURN(world::WorldObject root_object,
                        world.GetObject(RootObjectName()));
  INTR_RETURN_IF_ERROR(world.ReparentObject(object, root_object));
  if (!params.skip_collision_enable()) {
    INTR_RETURN_IF_ERROR(world.EnableCollisions(gripper, object));
  }

  return nullptr;
}
// LINT.ThenChange(
//     //intrinsic/manipulation/skills/plan_grasp_world_changes.py:detach_object
// )

absl::StatusOr<std::unique_ptr<google::protobuf::Message>>
DetachObjectSkill::Execute(const ExecuteRequest& request,
                           ExecuteContext& context) {
  INTR_ASSIGN_OR_RETURN(
      auto params,
      request.params<intrinsic_proto::skills::DetachObjectParams>());
  INTR_RETURN_IF_ERROR(ValidateParams(params));

  return ExecuteImpl(params, context.object_world());
}

absl::StatusOr<std::unique_ptr<::google::protobuf::Message>>
DetachObjectSkill::Preview(const PreviewRequest& request,
                           PreviewContext& context) {
  const stats::ScopedSpan span("skills.DetachObjectSkill/Preview");
  INTR_ASSIGN_OR_RETURN(
      auto params,
      request.params<intrinsic_proto::skills::DetachObjectParams>());
  INTR_RETURN_IF_ERROR(ValidateParams(params));

  const world::ObjectWorldClient& world = context.object_world();

  INTR_ASSIGN_OR_RETURN(world::WorldObject gripper,
                        world.GetObject(params.gripper_entity()));
  INTR_ASSIGN_OR_RETURN(world::WorldObject object,
                        world.GetObject(params.object_entity()));

  if (object.ParentId() != gripper.Id()) {
    return absl::InvalidArgumentError(
        absl::StrCat("The object specified as 'object_entity' is not a child "
                     "of the 'gripper_entity'.",
                     "\n\tobject name: ", object.Name().value(),
                     "\n\tgripper name: ", gripper.Name().value()));
  }

  INTR_ASSIGN_OR_RETURN(Pose3d gripper_t_object,
                        world.GetTransform(gripper, object));
  gripper_t_object.translation()[2] += 0.001;

  intrinsic_proto::world::ObjectWorldUpdate transform_update;
  intrinsic_proto::world::UpdateTransformRequest* update_transform =
      transform_update.mutable_update_transform();
  *update_transform->mutable_node_a() = gripper.TransformNodeReference();
  *update_transform->mutable_node_b() = object.TransformNodeReference();
  *update_transform->mutable_a_t_b() = ToProto(gripper_t_object);
  update_transform->set_view(intrinsic_proto::world::ObjectView::BASIC);
  INTR_RETURN_IF_ERROR(context.RecordWorldUpdate(
      transform_update, absl::ZeroDuration(), absl::ZeroDuration()));

  intrinsic_proto::world::ObjectWorldUpdate reparent_update;
  intrinsic_proto::world::ReparentObjectRequest* reparent_object =
      reparent_update.mutable_reparent_object();
  *reparent_object->mutable_object() = object.ObjectReference();
  INTR_ASSIGN_OR_RETURN(world::WorldObject root_object,
                        world.GetObject(RootObjectName()));
  *reparent_object->mutable_new_parent() =
      root_object.ObjectReferenceWithEntityFilter(
          world::ObjectEntityFilter().IncludeBaseEntity());
  INTR_RETURN_IF_ERROR(context.RecordWorldUpdate(
      reparent_update, absl::ZeroDuration(), absl::ZeroDuration()));

  if (!params.skip_collision_enable()) {
    intrinsic_proto::world::ObjectWorldUpdate collision_update;
    intrinsic_proto::world::ToggleCollisionsRequest* toggle_collisions =
        collision_update.mutable_toggle_collisions();
    toggle_collisions->set_toggle_mode(
        intrinsic_proto::world::ToggleMode::TOGGLE_MODE_ENABLE);
    *toggle_collisions->mutable_object_a() =
        object.ObjectReferenceWithEntityFilter(
            world::ObjectEntityFilter().IncludeAllEntities());
    *toggle_collisions->mutable_object_b() =
        gripper.ObjectReferenceWithEntityFilter(
            world::ObjectEntityFilter().IncludeAllEntities());
    INTR_RETURN_IF_ERROR(context.RecordWorldUpdate(
        collision_update, absl::ZeroDuration(), absl::ZeroDuration()));
  }

  return nullptr;
}

std::unique_ptr<SkillInterface> DetachObjectSkill::CreateSkill() {
  return std::make_unique<DetachObjectSkill>();
}

}  // namespace intrinsic::skills
