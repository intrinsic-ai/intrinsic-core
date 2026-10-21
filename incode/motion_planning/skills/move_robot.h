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

#ifndef MOTION_PLANNING_SKILLS_MOVE_ROBOT_H_
#define MOTION_PLANNING_SKILLS_MOVE_ROBOT_H_

#include <memory>
#include <optional>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/message.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/equipment/channel_factory.h"
#include "intrinsic/kinematics/types/cartesian_limits.h"
#include "intrinsic/kinematics/types/joint_limits_xd.h"
#include "intrinsic/motion_planning/motion_planner_client.h"
#include "intrinsic/motion_planning/proto/v1/motion_specification.pb.h"
#include "intrinsic/motion_planning/service/debug/timing_debug_logger.h"
#include "intrinsic/motion_planning/skills/move_robot.pb.h"
#include "intrinsic/skills/cc/skill_interface.h"
#include "intrinsic/skills/proto/skill_service.pb.h"
#include "intrinsic/world/objects/kinematic_object.h"

namespace intrinsic::skills {

class MoveRobot final : public SkillInterface {
 public:
  static constexpr char kEquipmentSlot[] = "robot";
  static std::unique_ptr<SkillInterface> CreateSkill();

  explicit MoveRobot(
      std::unique_ptr<icon::ChannelFactory> icon_channel_factory);

  MoveRobot(MoveRobot const&) = delete;
  MoveRobot& operator=(MoveRobot const&) = delete;

  absl::StatusOr<std::unique_ptr<google::protobuf::Message>> Execute(
      ExecuteRequest const& execute_request, ExecuteContext& context) override;

  absl::StatusOr<std::unique_ptr<::google::protobuf::Message>> Preview(
      PreviewRequest const& request, PreviewContext& context) override;

  absl::StatusOr<intrinsic_proto::skills::Footprint> GetFootprint(
      GetFootprintRequest const& request,
      GetFootprintContext& context) const override;

  absl::StatusOr<intrinsic_proto::skills::MoveRobotInternalData> ComputePlan(
      const intrinsic_proto::skills::MoveRobotParams& params,
      world::ObjectWorldClient& world,
      motion_planning::MotionPlannerClient& default_planner,
      std::optional<world::KinematicObject> robot_object) const;

  static absl::StatusOr<intrinsic_proto::skills::MoveRobotInternalData>
  InternalComputePlan(
      intrinsic_proto::skills::MoveRobotParams const& parameters,
      motion_planning::MotionPlannerClient& planner,
      world::KinematicObject const& robot_object,
      eigenmath::VectorXd const& start_configuration,
      JointLimitsXd const& application_limits, TimingDebugLogger& logger);

  // Computes a fresh motion plan and stores it in `SkillData` under
  // `(context_id, "plan")` so that a subsequent `Execute()` or `Preview()` for
  // the same action can run it without replanning.
  //
  // Never reuses a plan from a previous call. Re-projection under the same
  // `context_id` may happen in a changed world (e.g., conflicting actions
  // started or finished), so a previously computed plan must not be trusted.
  // Cheap reuse for unchanged worlds is provided by the MPS cache instead.
  //
  // Any previously stored entry for `context_id` is removed before planning,
  // so a failed computation never leaves a stale plan behind for `Execute()` or
  // `Preview()`.
  absl::StatusOr<intrinsic_proto::skills::MoveRobotInternalData>
  ComputeAndStorePlan(absl::string_view context_id,
                      const intrinsic_proto::skills::MoveRobotParams& params,
                      world::ObjectWorldClient& world,
                      motion_planning::MotionPlannerClient& planner,
                      std::optional<world::KinematicObject> robot_object) const;

  // Returns the plan stored in `SkillData` for `context_id` if present, or
  // computes and stores a new plan. Used by `Preview()` to reuse the plan
  // cached by `GetFootprint()`.
  absl::StatusOr<intrinsic_proto::skills::MoveRobotInternalData>
  GetOrComputePlan(absl::string_view context_id,
                   const intrinsic_proto::skills::MoveRobotParams& params,
                   world::ObjectWorldClient& world,
                   motion_planning::MotionPlannerClient& planner,
                   std::optional<world::KinematicObject> robot_object) const;

  // Passing std::nullopt for start_configuration makes planning start from the
  // the current world state.
  static absl::StatusOr<
      motion_planning::MotionPlannerClient::PlanTrajectoryResult>
  PlanTrajectory(
      world::KinematicObject const& robot_object,
      motion_planning::MotionPlannerClient& planner,
      intrinsic_proto::skills::MoveRobotParams const& skill_parameters,
      std::optional<eigenmath::VectorXd> const& start_configuration,
      std::string const& caller_id,
      intrinsic_proto::data_logger::Context const& context =
          intrinsic_proto::data_logger::Context());

 protected:
  absl::Status ValidateInputParams(
      intrinsic_proto::skills::MoveRobotParams const& params) const;

  std::optional<intrinsic_proto::icon::JointTrajectoryPVA>
  GetTrajectoryFromInternalData(
      intrinsic_proto::skills::MoveRobotExecutionPlan const& execution_plan)
      const;

  absl::StatusOr<std::optional<JointLimitsXd>>
  GetApplicationLimitsFromInternalData(
      intrinsic_proto::skills::MoveRobotExecutionPlan const& execution_plan)
      const;

  bool DynamicCartesianLimitsAreCompatible(
      CartesianLimits const& robot_limits,
      CartesianLimits const& world_limits) const;

  bool JointLimitsAreApproximate(JointLimitsXd const& robot_limits,
                                 JointLimitsXd const& world_limits) const;

  // Factory used to instantiate the connection to ICON. This is injected so
  // that tests can use FakeChannelFactory.
  std::unique_ptr<icon::ChannelFactory> icon_channel_factory_;

  // We pass the current_joint_position separately from the robot_object so that
  // we have the joint of using either the sensed value, or the last commanded
  // joint position.
  absl::StatusOr<std::optional<intrinsic_proto::icon::JointTrajectoryPVA>>
  ValidateSolution(
      eigenmath::VectorXd const& current_joint_position,
      world::KinematicObject const& robot_object,
      intrinsic_proto::skills::MoveRobotExecutionPlan const& execution_plan,
      JointLimitsXd const& icon_application_limits,
      CartesianLimits const& icon_cartesian_limits) const;
};

}  // namespace intrinsic::skills

#endif  // MOTION_PLANNING_SKILLS_MOVE_ROBOT_H_
