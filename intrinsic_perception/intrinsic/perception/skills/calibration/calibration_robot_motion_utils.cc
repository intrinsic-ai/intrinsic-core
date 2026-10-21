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

#include "intrinsic/perception/skills/calibration/calibration_robot_motion_utils.h"

#include <optional>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/cc_client/client.h"
#include "intrinsic/icon/proto/cart_space_conversion.h"
#include "intrinsic/icon/proto/part_status.pb.h"
#include "intrinsic/icon/utils/arm_utils.h"
#include "intrinsic/logging/proto/context.pb.h"
#include "intrinsic/math/proto_conversion.h"
#include "intrinsic/motion_planning/motion_planner_client.h"
#include "intrinsic/motion_planning/proto/v1/geometric_constraints.pb.h"
#include "intrinsic/motion_planning/skills/motion_planning_util.h"
#include "intrinsic/motion_planning/trajectory_execution_utils.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic {
namespace skills {

absl::Status UpdateRobotJointPositions(
    const CalibrationRobotMotionConfig& config,
    world::ObjectWorldClient& world) {
  icon::Client icon_client(config.icon_channel);
  INTR_ASSIGN_OR_RETURN(
      intrinsic_proto::icon::PartStatus part_status,
      icon_client.GetSinglePartStatus(config.arm_part_info.name));
  const eigenmath::VectorXd joint_values =
      icon::GetSensedJointPosition(part_status);
  return world.UpdateJointPositions(config.robot, joint_values);
}

absl::Status PlanAndExecuteMotion(
    const intrinsic_proto::motion_planning::v1::GeometricConstraint& waypoint,
    const intrinsic_proto::world::TransformNodeReference& default_tool,
    const intrinsic_proto::world::TransformNodeReference& default_frame,
    const CalibrationRobotMotionConfig& config, ExecuteContext& context) {
  LOG(INFO) << "Executing robot move.";

  // 1. Get the starting joint configuration to use. Prefer the last commanded
  // position from ICON if available, otherwise use the sensed position.
  icon::Client icon_client(config.icon_channel);
  INTR_ASSIGN_OR_RETURN(
      const intrinsic_proto::icon::PartStatus part_status,
      icon_client.GetSinglePartStatus(config.arm_part_info.name));
  const eigenmath::VectorXd starting_configuration =
      icon::GetCommandedOrSensedJointPosition(part_status);

  // 2. Update the world with the starting joint configuration.
  world::ObjectWorldClient& world = context.object_world();
  INTR_RETURN_IF_ERROR(
      world.UpdateJointPositions(config.robot, starting_configuration));

  // 3. Update the world with correct joint limits and cartesian limits for this
  // robot.
  INTR_ASSIGN_OR_RETURN(const auto robot_config, icon_client.GetConfig());
  INTR_ASSIGN_OR_RETURN(
      intrinsic_proto::icon::GenericPartConfig part_config,
      robot_config.GetGenericPartConfig(config.arm_part_info.name));
  if (!part_config.has_joint_limits_config()) {
    return absl::FailedPreconditionError(
        absl::StrCat("Part '", config.arm_part_info.name,
                     "' does not have JointLimits config."));
  }
  INTR_ASSIGN_OR_RETURN(
      intrinsic::JointLimits application_limits,
      FromProto(part_config.joint_limits_config().application_limits()));
  INTR_ASSIGN_OR_RETURN(
      intrinsic::JointLimits system_limits,
      FromProto(part_config.joint_limits_config().system_limits()));
  INTR_ASSIGN_OR_RETURN(
      const intrinsic::CartesianLimits default_cartesian_limits,
      intrinsic::icon::FromProto(
          part_config.cartesian_limits_config().default_cartesian_limits()));

  INTR_RETURN_IF_ERROR(world.UpdateJointLimits(
      config.robot, intrinsic::JointLimitsXd::Create(application_limits),
      intrinsic::JointLimitsXd::Create(system_limits)));
  INTR_RETURN_IF_ERROR(
      world.UpdateCartesianLimits(config.robot, default_cartesian_limits));

  // 4. Build the MotionSpecification directly.
  intrinsic_proto::motion_planning::v1::MotionSpecification motion_specs;
  intrinsic_proto::motion_planning::v1::MotionSegment* segment =
      motion_specs.add_motion_segments();

  segment->mutable_collision_settings()->set_disable_collision_checking(
      config.disable_collision_checking);

  if (!waypoint.has_joint_position() && !waypoint.has_cartesian_pose()) {
    return absl::InvalidArgumentError(
        "Unsupported geometric constraint in waypoint: expected joint_position "
        "or cartesian_pose.");
  }

  *segment->mutable_target() = waypoint;
  if (segment->target().has_cartesian_pose()) {
    auto& cartesian_pose = *segment->mutable_target()->mutable_cartesian_pose();
    if (!cartesian_pose.has_moving_frame()) {
      *cartesian_pose.mutable_moving_frame() = default_tool;
    }
    if (!cartesian_pose.has_target_frame()) {
      *cartesian_pose.mutable_target_frame() = default_frame;
    }
  }

  if (config.motion_type == intrinsic_proto::skills::MOTION_TYPE_JOINT) {
    segment->set_motion_type(
        intrinsic_proto::motion_planning::v1::MotionSegment::JOINT);
  } else if (config.motion_type ==
             intrinsic_proto::skills::MOTION_TYPE_LINEAR) {
    segment->set_motion_type(
        intrinsic_proto::motion_planning::v1::MotionSegment::LINEAR);
  }

  // 5. Plan the trajectory.
  intrinsic_proto::motion_planning::v1::RobotSpecification robot_specification =
      ::intrinsic::motion_planning::CreateRobotSpecification(
          config.robot, starting_configuration);

  motion_planning::MotionPlannerClient::MotionPlanningOptions planning_options;
  INTR_ASSIGN_OR_RETURN(const auto trajectory_result,
                        config.motion_planner.PlanTrajectory(
                            robot_specification, motion_specs, planning_options,
                            "skills.CalibrationRobotMotion",
                            context.logging_context().data_logger_context));

  if (trajectory_result.trajectory.state_size() == 0) {
    LOG(INFO) << "Trajectory is empty. We already reached desired position.";
    return absl::OkStatus();
  }

  // 6. Execute the trajectory.
  LOG(INFO) << "Starting execution on the robot.";
  constexpr double kDefaultSettlingTimeoutSeconds = 2.0;
  return motion_planning::ExecuteJointTrajectory(
      context.logging_context().data_logger_context, config.icon_channel,
      config.arm_part_info.name, trajectory_result.trajectory,
      kDefaultSettlingTimeoutSeconds,
      /*use_is_settled_as_condition=*/true, context.canceller(),
      /*move_until_signal_params=*/std::nullopt,
      /*stopped_on_signal=*/
      nullptr

      ,
      trajectory_result.motion_events

  );
}

absl::Status MoveToWaypoint(
    const intrinsic_proto::motion_planning::v1::GeometricConstraint& waypoint,
    const intrinsic_proto::world::TransformNodeReference& default_tool,
    const intrinsic_proto::world::TransformNodeReference& default_frame,
    const CalibrationRobotMotionConfig& config, ExecuteContext& context) {
  if (context.canceller().cancelled()) {
    return absl::CancelledError("Motion was cancelled.");
  }

  absl::Status status = PlanAndExecuteMotion(waypoint, default_tool,
                                             default_frame, config, context);

  if (!status.ok()) {
    LOG(INFO) << "Could not reach requested joint configuration. Skipping."
              << status.message();
  }
  // TODO(b/261987487)
  LOG(INFO) << "Waiting for one second to let robot motions settle down...";
  absl::SleepFor(absl::Seconds(1));

  // We need to sense the robot pose again, because the robot's pose might
  // be stale in the world: b/208233733.
  INTR_RETURN_IF_ERROR(
      UpdateRobotJointPositions(config, context.object_world()));

  return status;
}

}  // namespace skills
}  // namespace intrinsic
