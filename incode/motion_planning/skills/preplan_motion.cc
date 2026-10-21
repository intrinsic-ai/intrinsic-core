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

#include "incode/motion_planning/skills/preplan_motion.h"

#include <memory>
#include <optional>
#include <utility>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "google/protobuf/message.h"
#include "incode/motion_planning/skills/move_robot.h"
#include "incode/motion_planning/skills/preplan_motion.pb.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/motion_planning/motion_planner_client.h"
#include "intrinsic/motion_planning/service/debug/timing_debug_logger.h"
#include "intrinsic/motion_planning/service/motion_planner_service_asset_utils.h"
#include "intrinsic/skills/apps/attach_object_to_robot.h"
#include "intrinsic/skills/apps/attach_object_to_robot.pb.h"
#include "intrinsic/skills/apps/detach_object.h"
#include "intrinsic/skills/apps/detach_object.pb.h"
#include "intrinsic/skills/apps/update_world.pb.h"
#include "intrinsic/skills/cc/skill_interface.h"
#include "intrinsic/skills/internal/kinematic_object_for_position_part.h"
#include "intrinsic/skills/proto/footprint.pb.h"
#include "intrinsic/skills/proto/skill_service.pb.h"
#include "intrinsic/stats/scoped_span.h"
#include "intrinsic/util/eigen.h"
#include "intrinsic/util/proto_time.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/objects/kinematic_object.h"
#include "intrinsic/world/objects/object_world_client.h"
#include "intrinsic/world/proto/object_world_updates.pb.h"

namespace intrinsic::skills {
namespace {

// Returns the robot KinematicObject for planning.
//
// If any sub-skill explicitly specifies an arm_part, uses that object from the
// world. Otherwise, falls back to the robot object for the skill's equipment
// slot.
//
// (Note: Using fallback_robot_object is technically incorrect, since we need to
// use the same equipment specified for the Skill call. There are various
// scenarios here, but as long as there is only one ICON instances, and the user
// specifies the arm part, we should be ok.
absl::StatusOr<world::KinematicObject> GetRobotObject(
    const intrinsic_proto::skills::PreplanMotionParams& params,
    const world::ObjectWorldClient& world,
    const world::KinematicObject& fallback_robot_object) {
  for (const auto& skill : params.skills()) {
    if (skill.has_move_robot() && skill.move_robot().has_arm_part()) {
      return world.GetKinematicObject(skill.move_robot().arm_part());
    }
  }
  return fallback_robot_object;
}

absl::Status Preplan(const intrinsic_proto::skills::PreplanMotionParams& params,
                     world::ObjectWorldClient& world,
                     motion_planning::MotionPlannerClient& motion_planner,
                     const world::KinematicObject& robot_object) {
  const std::string& span_name = "skills.PreplanMotion/Preplan";
  TimingDebugLogger logger(span_name);

  // Run through the skills in order manually propagating the state as
  // necessary.
  auto true_start = absl::Now();
  auto start = true_start;
  int i = 0;
  for (auto const& skill : params.skills()) {
    logger.Start(absl::StrCat("preplan ", i++));
    if (skill.has_move_robot()) {
      logger.Attach("skill", "move_robot");
      // This will hit the motion planner service and cause the motion to be
      // cached.
      INTR_ASSIGN_OR_RETURN(
          auto result, MoveRobot::PlanTrajectory(robot_object, motion_planner,
                                                 skill.move_robot(),
                                                 std::nullopt, span_name));
      if (result.trajectory.state().empty()) {
        return absl::InternalError("Expected trajectory to be filled.");
      }
      auto end_state =
          result.trajectory.state().at(result.trajectory.state().size() - 1);
      INTR_ASSIGN_OR_RETURN(
          auto end_time, ToAbslDuration(result.trajectory.time_since_start(
                             result.trajectory.time_since_start_size() - 1)));
      LOG(INFO) << "Planned trajectory with duration: "
                << absl::ToDoubleSeconds(end_time)
                << "s and size: " << result.trajectory.state_size();
      eigenmath::VectorXd end_state_vec =
          RepeatedDoubleToVectorXd(end_state.position());
      INTR_RETURN_IF_ERROR(
          world.UpdateJointPositions(robot_object, end_state_vec));
    } else if (skill.has_attach_object_to_robot()) {
      logger.Attach("skill", "attach_object_to_robot");
      INTR_RETURN_IF_ERROR(AttachObjectToRobotSkill::ExecuteImpl(
                               skill.attach_object_to_robot(), world)
                               .status());
    } else if (skill.has_detach_object()) {
      logger.Attach("skill", "detach_object");
      INTR_RETURN_IF_ERROR(
          DetachObjectSkill::ExecuteImpl(skill.detach_object(), world)
              .status());
    } else if (skill.has_update_world()) {
      logger.Attach("skill", "update_world");
      if (skill.update_world().has_update()) {
        // Copy proto for modification.
        intrinsic_proto::world::ObjectWorldUpdates updates;
        *updates.add_updates() = skill.update_world().update();
        INTR_RETURN_IF_ERROR(world.BatchUpdate(updates));
      } else if (skill.update_world().has_updates()) {
        INTR_RETURN_IF_ERROR(world.BatchUpdate(skill.update_world().updates()));
      }
    }

    logger.Start("finalize");
    auto end = absl::Now();
    LOG(INFO) << "Step took " << absl::ToDoubleSeconds(end - start)
              << "s, total time: " << absl::ToDoubleSeconds(end - true_start)
              << "s.";
    start = end;
  }
  LOG(INFO) << "Done preplanning!";
  return absl::OkStatus();
}

}  // namespace

std::unique_ptr<SkillInterface> PreplanMotionSkill::CreateSkill() {
  return std::make_unique<PreplanMotionSkill>();
}

absl::StatusOr<intrinsic_proto::skills::Footprint>
PreplanMotionSkill::GetFootprint(GetFootprintRequest const& request,
                                 GetFootprintContext& context) const {
  intrinsic_proto::skills::Footprint result;
  result.set_lock_the_universe(false);
  return std::move(result);
}

absl::StatusOr<std::unique_ptr<google::protobuf::Message>>
PreplanMotionSkill::Execute(ExecuteRequest const& request,
                            ExecuteContext& context) {
  const stats::ScopedSpan span("skills.PreplanMotionSkill/Execute");

  INTR_ASSIGN_OR_RETURN(world::ObjectWorldClient world,
                        context.object_world().Clone());
  INTR_ASSIGN_OR_RETURN(
      auto params,
      request.params<intrinsic_proto::skills::PreplanMotionParams>());
  INTR_ASSIGN_OR_RETURN(
      std::unique_ptr<motion_planning::MotionPlannerClient>
          motion_planner_service_asset_client,
      GetMotionPlannerServiceAssetClient(world.GetWorldID(),
                                         params.motion_planner_service()));
  // TODO(b/524634328): Remove the Fallback Logic from the Skills.
  motion_planning::MotionPlannerClient motion_planner =
      (motion_planner_service_asset_client != nullptr)
          ? std::move(*motion_planner_service_asset_client)
          : context.motion_planner().ForWorld(world.GetWorldID());

  INTR_ASSIGN_OR_RETURN(const world::KinematicObject fallback_robot_object,
                        KinematicObjectForPositionPart(
                            kEquipmentSlot, context.equipment(), world));
  INTR_ASSIGN_OR_RETURN(const world::KinematicObject robot_object,
                        GetRobotObject(params, world, fallback_robot_object));

  INTR_RETURN_IF_ERROR(Preplan(params, world, motion_planner, robot_object));

  return nullptr;
}

absl::StatusOr<std::unique_ptr<::google::protobuf::Message>>
PreplanMotionSkill::Preview(PreviewRequest const& request,
                            PreviewContext& context) {
  const stats::ScopedSpan span("skills.PreplanMotionSkill/Preview");

  INTR_ASSIGN_OR_RETURN(world::ObjectWorldClient world,
                        context.object_world().Clone());
  INTR_ASSIGN_OR_RETURN(
      auto params,
      request.params<intrinsic_proto::skills::PreplanMotionParams>());
  INTR_ASSIGN_OR_RETURN(
      std::unique_ptr<motion_planning::MotionPlannerClient>
          motion_planner_service_asset_client,
      GetMotionPlannerServiceAssetClient(world.GetWorldID(),
                                         params.motion_planner_service()));
  // TODO(b/524634328): Remove the Fallback Logic from the Skills.
  motion_planning::MotionPlannerClient motion_planner =
      (motion_planner_service_asset_client != nullptr)
          ? std::move(*motion_planner_service_asset_client)
          : context.motion_planner().ForWorld(world.GetWorldID());

  INTR_ASSIGN_OR_RETURN(const world::KinematicObject fallback_robot_object,
                        context.GetKinematicObjectForEquipment(kEquipmentSlot));
  INTR_ASSIGN_OR_RETURN(const world::KinematicObject robot_object,
                        GetRobotObject(params, world, fallback_robot_object));

  INTR_RETURN_IF_ERROR(Preplan(params, world, motion_planner, robot_object));

  return nullptr;
}

}  // namespace intrinsic::skills
