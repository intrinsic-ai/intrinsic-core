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

#include "incode/motion_planning/skills/move_robot.h"

#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/no_destructor.h"
#include "absl/cleanup/cleanup.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "google/protobuf/duration.pb.h"
#include "google/protobuf/message.h"
#include "incode/motion_planning/skills/move_robot_util.h"
#include "intrinsic/assets/dependencies/utils.h"
#include "intrinsic/assets/proto/v1/resolved_dependency.pb.h"
#include "intrinsic/choreographer/footprints/footprint.h"
#include "intrinsic/choreographer/footprints/object_world_reservation.h"
#include "intrinsic/connect/cc/grpc/channel.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/cc_client/client.h"
#include "intrinsic/icon/equipment/channel_factory.h"
#include "intrinsic/icon/equipment/equipment_utils.h"
#include "intrinsic/icon/equipment/icon_equipment.pb.h"
#include "intrinsic/icon/proto/cart_space_conversion.h"
#include "intrinsic/icon/proto/generic_part_config.pb.h"
#include "intrinsic/icon/proto/joint_space.pb.h"
#include "intrinsic/icon/proto/joint_state_conversion.h"
#include "intrinsic/icon/proto/part_status.pb.h"
#include "intrinsic/icon/proto/v1/condition_types.pb.h"
#include "intrinsic/icon/skills/util/position_part_util.h"
#include "intrinsic/icon/utils/arm_utils.h"
#include "intrinsic/icon/utils/realtime_status_or.h"
#include "intrinsic/kinematics/types/cartesian_limits.h"
#include "intrinsic/kinematics/types/check_cartesian_limits.h"
#include "intrinsic/kinematics/types/check_joint_limits.h"
#include "intrinsic/kinematics/types/joint_limits.h"
#include "intrinsic/kinematics/types/joint_limits_xd.h"
#include "intrinsic/kinematics/types/joint_state.h"
#include "intrinsic/kinematics/types/to_string.h"
#include "intrinsic/logging/data_logger_client.h"
#include "intrinsic/logging/log_item_builder.h"
#include "intrinsic/math/proto/quaternion.pb.h"
#include "intrinsic/motion_planning/motion_planner_client.h"
#include "intrinsic/motion_planning/path_planning/path_planner_definitions.h"
#include "intrinsic/motion_planning/proto/v1/geometric_constraints.pb.h"
#include "intrinsic/motion_planning/proto/v1/motion_blending_parameter.pb.h"
#include "intrinsic/motion_planning/service/debug/timing_debug_logger.h"
#include "intrinsic/motion_planning/service/motion_planner_service_asset_utils.h"

#include "intrinsic/motion_planning/proto/v1/motion_events.pb.h"

#include "intrinsic/motion_planning/proto/v1/motion_planner_service.grpc.pb.h"
#include "intrinsic/motion_planning/proto/v1/motion_planner_service.pb.h"
#include "intrinsic/motion_planning/proto/v1/motion_planning_limits.pb.h"
#include "intrinsic/motion_planning/proto/v1/motion_specification.pb.h"
#include "intrinsic/motion_planning/skills/motion_planning_error_util.h"
#include "intrinsic/motion_planning/skills/motion_planning_util.h"
#include "intrinsic/motion_planning/skills/move_robot.pb.h"
#include "intrinsic/motion_planning/trajectory_execution_utils.h"
#include "intrinsic/skills/cc/equipment_pack.h"
#include "intrinsic/skills/cc/skill_data.h"
#include "intrinsic/skills/cc/skill_interface.h"
#include "intrinsic/skills/cc/skill_utils.h"
#include "intrinsic/skills/proto/equipment.pb.h"
#include "intrinsic/skills/proto/skill_service.pb.h"
#include "intrinsic/skills/proto/skills.pb.h"
#include "intrinsic/stats/scoped_span.h"
#include "intrinsic/util/eigen.h"
#include "intrinsic/util/grpc/channel_interface.h"
#include "intrinsic/util/proto_time.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/objects/kinematic_object.h"
#include "intrinsic/world/objects/object_world_client.h"
#include "intrinsic/world/proto/collision_settings.pb.h"
#include "intrinsic/world/proto/object_world_refs.pb.h"
#include "intrinsic/world/proto/object_world_updates.pb.h"

namespace intrinsic::skills {

using ::intrinsic::motion_planning::MotionPlannerClient;

constexpr char kMoveRobotSkillName[] = "move_robot";

namespace {

using ::intrinsic::connect::kGrpcClientConnectDefaultTimeout;

// Time to wait for the robot to settle after the planned move is`done`.
constexpr double kDefaultSettlingTimeoutSeconds = 2.0;
constexpr double kNearZeroThreshold = 1e-6;
constexpr double kJointLimitTolerance = 1e-4;
constexpr bool kUseRad = false;

struct RobotParameters {
  JointLimits system_limits;
  JointLimits application_limits;
  CartesianLimits default_cartesian_limits;
};

absl::StatusOr<RobotParameters> ReadICONArmParams(
    const std::string& arm_part_name, icon::Client& icon_client) {
  INTR_ASSIGN_OR_RETURN(auto robot_config, icon_client.GetConfig());
  INTR_ASSIGN_OR_RETURN(intrinsic_proto::icon::GenericPartConfig part_config,
                        robot_config.GetGenericPartConfig(arm_part_name));
  if (!part_config.has_joint_limits_config()) {
    return absl::FailedPreconditionError(absl::StrCat(
        "Part '", arm_part_name, "' does not have JointLimits config."));
  }

  INTR_ASSIGN_OR_RETURN(
      JointLimits application_limits,
      FromProto(part_config.joint_limits_config().application_limits()));
  INTR_ASSIGN_OR_RETURN(
      JointLimits system_limits,
      FromProto(part_config.joint_limits_config().system_limits()));
  INTR_ASSIGN_OR_RETURN(
      const intrinsic::CartesianLimits default_cartesian_limits,
      intrinsic::icon::FromProto(
          part_config.cartesian_limits_config().default_cartesian_limits()));

  return RobotParameters{.system_limits = system_limits,
                         .application_limits = application_limits,
                         .default_cartesian_limits = default_cartesian_limits};
}

absl::StatusOr<eigenmath::VectorXd>
GetPreviouslyCommandedJointPositionFromIconIfAvailable(
    const icon::Client& icon_client, absl::string_view arm_part_name) {
  INTR_ASSIGN_OR_RETURN(const intrinsic_proto::icon::PartStatus part_status,
                        icon_client.GetSinglePartStatus(arm_part_name));
  return icon::GetCommandedOrSensedJointPosition(part_status);
}

// Creates a string representation of the position joint limits.
std::string JointPositionLimitsToString(const JointLimits& joint_limits,
                                        bool use_rad = false) {
  std::string unit = use_rad ? "rad" : "deg";
  const eigenmath::VectorNd joint_min_positions =
      use_rad ? joint_limits.min_position
              : ConvertRadiansToDegrees(joint_limits.min_position);
  const eigenmath::VectorNd joint_max_positions =
      use_rad ? joint_limits.max_position
              : ConvertRadiansToDegrees(joint_limits.max_position);
  return absl::StrCat("min_position: [",
                      eigenmath::ToString(joint_min_positions),
                      "], max_position: [",
                      eigenmath::ToString(joint_max_positions), "] in ", unit);
}

std::string JointPositionXdToString(
    const eigenmath::VectorXd& joint_position_in_rad, bool use_rad = false) {
  return absl::StrCat("joint position: [",
                      eigenmath::ToString(joint_position_in_rad *
                                          (use_rad ? 1.0 : 180.0 / M_PI)),
                      "] in ", use_rad ? "rad" : "deg");
}

intrinsic_proto::world::ObjectWorldUpdates CreateUpdateForPosition(
    const world::KinematicObject& robot,
    const google::protobuf::RepeatedField<double>& position) {
  intrinsic_proto::world::ObjectWorldUpdates result;
  // TODO(kmuelling): Investigate passing ordered joint names to the
  // UpdateObjectJointsRequest when updating the world.
  intrinsic_proto::world::UpdateObjectJointsRequest* update =
      result.add_updates()->mutable_update_object_joints();
  *update->mutable_object() = robot.ObjectReference();
  *update->mutable_joint_positions() = position;
  return result;
}

bool IsCartLimitsValid(const CartesianLimits& limits) {
  if (!limits.IsValid()) {
    LOG(INFO)
        << "Cartesian limits are not valid. Check if the Cartesian linits set "
           "are all positive for velocity, acceleration and jerk and if "
           "min_position limits are smaller than max_position limits.";
    return false;
  }
  if (limits.max_translational_velocity.maxCoeff() <= kNearZeroThreshold) {
    LOG(INFO) << "Cartesian limits for robot are almost zero or below. Require "
                 "positive velocity to generate trajectory. Robot values: "
              << limits.max_translational_velocity.transpose();
    return false;
  }
  if (limits.max_translational_acceleration.maxCoeff() <= kNearZeroThreshold) {
    LOG(INFO)
        << "Cartesian acceleration limits for robot are almost zero or below. "
           "Require positive acceleration limits to generate trajectory. "
           "Provided values "
        << limits.max_translational_acceleration.transpose();
    return false;
  }
  if (limits.max_rotational_velocity <= kNearZeroThreshold ||
      limits.max_rotational_acceleration <= kNearZeroThreshold) {
    LOG(INFO)
        << "Rotational velocity or acceleration limits provided are almost "
           "zero or below. Require positive velocity and acceleration limits. "
           "Provided velocity limits "
        << limits.max_rotational_velocity << ", provided acceleration limits "
        << limits.max_rotational_acceleration;
    return false;
  }
  return true;
}

absl::StatusOr<JointLimitsXd> ConvertMaxLimitsToInfinite(
    const JointLimitsXd& joint_limits) {
  JointLimitsXd joint_limits_inf = joint_limits;
  if (joint_limits_inf.size() != joint_limits.min_position.size() ||
      joint_limits_inf.max_position.size() !=
          joint_limits_inf.max_position.size()) {
    return absl::InvalidArgumentError("Joint limit has inconsistent size.");
  }
  for (int i = 0; i < joint_limits_inf.size(); ++i) {
    if (joint_limits_inf.min_position[i] ==
        -std::numeric_limits<double>::max()) {
      joint_limits_inf.min_position[i] =
          -std::numeric_limits<double>::infinity();
    }
    if (joint_limits_inf.max_position[i] ==
        std::numeric_limits<double>::max()) {
      joint_limits_inf.max_position[i] =
          std::numeric_limits<double>::infinity();
    }
  }
  return joint_limits_inf;
}

absl::StatusOr<JointLimits> ToJointLimitsNd(const JointLimitsXd& xd) {
  icon::RealtimeStatusOr<JointLimits> maybe_nd =
      JointLimits::Unlimited(xd.size());
  if (!maybe_nd.ok()) {
    return absl::InvalidArgumentError(maybe_nd.status().message());
  }
  auto nd = *maybe_nd;
  nd.min_position = xd.min_position;
  nd.max_position = xd.max_position;
  nd.max_velocity = xd.max_velocity;
  nd.max_acceleration = xd.max_acceleration;
  nd.max_jerk = xd.max_jerk;
  nd.max_torque = xd.max_torque;

  if (!nd.IsValid()) {
    return absl::InvalidArgumentError(
        "Converted JointLimitsXd resulted in invalid JointLimitsNd.");
  }
  return nd;
}

}  // namespace

bool MoveRobot::JointLimitsAreApproximate(
    const JointLimitsXd& robot_limits,
    const JointLimitsXd& world_limits) const {
  // Check if limits are equal by checking IsWithinLimits in both
  // directions.
  constexpr double kLimitTolerance = 1e-4;
  // TODO(b/290691412): Convert std::max to inf
  auto robot_limits_inf_request = ConvertMaxLimitsToInfinite(robot_limits);
  auto world_limits_inf_request = ConvertMaxLimitsToInfinite(world_limits);
  if (!robot_limits_inf_request.ok() || !world_limits_inf_request.ok()) {
    LOG(INFO) << "Could not convert to inf limits "
              << robot_limits_inf_request.status().message();
    return false;
  }

  const JointLimitsXd& robot_limits_inf = robot_limits_inf_request.value();
  const JointLimitsXd& world_limits_inf = world_limits_inf_request.value();

  icon::RealtimeStatusOr<LimitCheckResult> limit_check_robot_in_world =
      IsWithinLimits(robot_limits_inf, world_limits_inf, kLimitTolerance);
  if (!limit_check_robot_in_world.ok()) {
    LOG(INFO) << "Internal error: robot and world joint limits are not "
                 "compatible. "
              << limit_check_robot_in_world.status().message();
    return false;
  }
  if (!limit_check_robot_in_world.value().p_ok ||
      !limit_check_robot_in_world.value().v_ok ||
      !limit_check_robot_in_world.value().a_ok ||
      !limit_check_robot_in_world.value().j_ok) {
    LOG(INFO) << absl::StrFormat(
        "Limits from the robot hardware differ from those in the "
        "world used for planning. Robot limits %s, world limits %s. Robot "
        "exceeds world limits.",
        ToString(robot_limits), ToString(world_limits));
    return false;
  }
  icon::RealtimeStatusOr<LimitCheckResult> limit_check_world_in_robot =
      IsWithinLimits(world_limits_inf, robot_limits_inf, kLimitTolerance);
  if (!limit_check_world_in_robot.ok()) {
    LOG(INFO) << "Internal error: robot and world joint limits are not "
                 "compatible. "
              << limit_check_robot_in_world.status().message();
    return false;
  }
  if (!limit_check_world_in_robot.value().p_ok ||
      !limit_check_world_in_robot.value().v_ok ||
      !limit_check_world_in_robot.value().a_ok ||
      !limit_check_world_in_robot.value().j_ok) {
    LOG(INFO) << absl::StrFormat(
        "Limits from the robot hardware differ from those in the "
        "world used for planning. Robot limits %s, world limits %s. World "
        "exceeds robot limits.",
        ToString(robot_limits), ToString(world_limits));
    return false;
  }
  return true;
}

bool MoveRobot::DynamicCartesianLimitsAreCompatible(
    const CartesianLimits& robot_limits,
    const CartesianLimits& world_limits) const {
  // At this point we neglect the position limits and only check if the
  // planning limits (i.e., world limits) are within the allowed robot
  // limits. But do not require them to be the same.
  CartesianLimits world_with_same_position_limits = world_limits;
  world_with_same_position_limits.min_translational_position =
      robot_limits.min_translational_position;
  world_with_same_position_limits.max_translational_position =
      robot_limits.max_translational_position;
  if (!IsWithinLimits(world_with_same_position_limits, robot_limits)) {
    LOG(INFO) << "Cartesian limits of robot in world exceed hardware limits.";
    return false;
  }
  return true;
}

std::optional<intrinsic_proto::icon::JointTrajectoryPVA>
MoveRobot::GetTrajectoryFromInternalData(
    const intrinsic_proto::skills::MoveRobotExecutionPlan& execution_plan)
    const {
  if (!execution_plan.has_planned_trajectory()) {
    LOG(WARNING) << "MoveRobotExecutionPlan does not contain solution.";
    return std::nullopt;
  }

  return execution_plan.planned_trajectory();
}

absl::StatusOr<std::optional<JointLimitsXd>>
MoveRobot::GetApplicationLimitsFromInternalData(
    const intrinsic_proto::skills::MoveRobotExecutionPlan& execution_plan)
    const {
  if (!execution_plan.has_robot_specifications() ||
      !execution_plan.robot_specifications().has_application_limits()) {
    LOG(WARNING) << "MoveRobotExecutionPlan does not specify the "
                    "application limits used to plan the solution.";
    return std::nullopt;
  }
  INTR_ASSIGN_OR_RETURN(
      JointLimits joint_limits,
      ::intrinsic::FromProto(
          execution_plan.robot_specifications().application_limits()));
  return JointLimitsXd::Create(joint_limits);
}

absl::Status MoveRobot::ValidateInputParams(
    const intrinsic_proto::skills::MoveRobotParams& params) const {
  if (params.motion_segments_size() == 0) {
    return absl::InvalidArgumentError(
        "MoveRobot needs to specify at least one motion segment.");
  }
  return absl::OkStatus();
}

absl::StatusOr<std::optional<intrinsic_proto::icon::JointTrajectoryPVA>>
MoveRobot::ValidateSolution(
    const eigenmath::VectorXd& current_joint_position,
    const world::KinematicObject& robot_object,
    const intrinsic_proto::skills::MoveRobotExecutionPlan& execution_plan,
    const JointLimitsXd& icon_application_limits,
    const CartesianLimits& icon_cartesian_limits) const {
  std::optional<intrinsic_proto::icon::JointTrajectoryPVA> preplanned_solution;
  preplanned_solution = GetTrajectoryFromInternalData(execution_plan);
  if (!preplanned_solution.has_value()) {
    LOG(INFO) << "Received execution plan does not contain solution.";
    return preplanned_solution;
  }
  if (preplanned_solution->state_size() < 1) {
    // We are at the correct spot. No motion necessary.
    LOG(INFO) << "Received execution plan contains empty trajectory.";
    return preplanned_solution;
  }

  LOG(INFO) << "Analyzing validity of precomputed solution.";

  // Perform validity checks of the solution. Currently we check two
  // conditions:
  // (1) Deviations from the start configuration. Sometimes the believe world
  // is not aligned with the robot state. Often this happens in the very first
  // call of a skill.
  // (2) Robot specifications do not match. That is either joint application
  // limits, joint system limits, or cartesian limits. In
  // this case we will throw an error.

  // Case (1): Current robot state is not as expected.
  INTR_ASSIGN_OR_RETURN(
      const JointStatePVA initial_state,
      ::intrinsic::FromProto(preplanned_solution.value().state(0)));
  if (!initial_state.position.isApprox(current_joint_position)) {
    const auto norm = (initial_state.position - current_joint_position).norm();
    if (norm > kSquaredConnectingDistance) {
      LOG(WARNING) << "The start of the solution from internal_data does "
                   << "not match the current state. Recomputing the "
                      "trajectory required. Solution start '"
                   << toString(initial_state.position)
                   << "' and current joints '"
                   << toString(current_joint_position) << "' diff is " << norm
                   << " which is more than the max diff of "
                   << kSquaredConnectingDistance;
      preplanned_solution = std::nullopt;
    }
  }

  // Case (2): Robot specifications do not match.
  INTR_ASSIGN_OR_RETURN(const CartesianLimits world_cart_limits,
                        robot_object.GetCartesianLimits());
  INTR_ASSIGN_OR_RETURN(
      const std::optional<JointLimitsXd> world_application_limits,
      GetApplicationLimitsFromInternalData(execution_plan));

  if (!world_application_limits.has_value()) {
    return absl::InternalError(
        "Execution plan does not specify application limits, but does provide "
        "a solution.");
  }

  const bool joint_limits_are_approximate = JointLimitsAreApproximate(
      icon_application_limits, world_application_limits.value());
  const bool cartesian_limits_are_compatible =
      DynamicCartesianLimitsAreCompatible(icon_cartesian_limits,
                                          world_cart_limits);
  if (joint_limits_are_approximate && cartesian_limits_are_compatible) {
    return preplanned_solution;
  }

  if (!joint_limits_are_approximate) {
    // TODO(kmuelling): Return error here instead of warning.
    LOG(WARNING) << "Need to replan trajectory. World and hardware have "
                    "conflicting joint limits.";
  }

  if (!cartesian_limits_are_compatible) {
    // TODO(kmuelling): Return error here instead of warning.
    LOG(WARNING) << "Need to replan trajectory. World and hardware have "
                    "conflicting cartesian limits.";
  }

  // No solution, will enforce replanning
  return std::nullopt;
}

std::unique_ptr<SkillInterface> MoveRobot::CreateSkill() {
  return std::make_unique<MoveRobot>(
      std::make_unique<icon::DefaultChannelFactory>());
}

MoveRobot::MoveRobot(std::unique_ptr<icon::ChannelFactory> icon_channel_factory)
    : icon_channel_factory_(std::move(icon_channel_factory)) {}

absl::StatusOr<intrinsic_proto::skills::Footprint> MoveRobot::GetFootprint(
    const GetFootprintRequest& request, GetFootprintContext& context) const {
  // MoveRobot at least needs to modify the robot.
  Footprint footprint;

  INTR_ASSIGN_OR_RETURN(
      const intrinsic_proto::skills::MoveRobotParams params,
      request.params<intrinsic_proto::skills::MoveRobotParams>());
  // If there is an arm selected, we will use this one. Otherwise, we will
  // use the robot from the equipment slot. In execute, it is checked that there
  // is only one robot in the world in this case.
  std::optional<world::KinematicObject> robot_object;
  if (params.has_arm_part()) {
    footprint.AddObjectReservation(
        ObjectWorldReservation(ObjectWorldReservation::kSharingTypeWrite,
                               params.arm_part().by_name()));
    INTR_ASSIGN_OR_RETURN(
        robot_object,
        context.object_world().GetKinematicObject(params.arm_part()));
  } else {
    INTR_ASSIGN_OR_RETURN(
        const world::KinematicObject robot,
        context.GetKinematicObjectForEquipment(MoveRobot::kEquipmentSlot));
    footprint.AddObjectReservation(ObjectWorldReservation(
        ObjectWorldReservation::kSharingTypeWrite, robot.Name()));
    robot_object = robot;
  }

  // Compute and store the motion plan.
  INTR_RETURN_IF_ERROR(
      ComputeAndStorePlan(context.context_id(), params, context.object_world(),
                          context.motion_planner(), robot_object)
          .status());

  intrinsic_proto::skills::Footprint out;
  INTR_ASSIGN_OR_RETURN(
      out, footprint.ToProto(&(context.geometry_library().Serializer())));
  return out;
}

absl::StatusOr<MotionPlannerClient::PlanTrajectoryResult>
MoveRobot::PlanTrajectory(
    const world::KinematicObject& robot_object, MotionPlannerClient& planner,
    const intrinsic_proto::skills::MoveRobotParams& skill_parameters,
    const std::optional<eigenmath::VectorXd>& start_configuration,
    const std::string& caller_id,
    const intrinsic_proto::data_logger::Context& context) {
  intrinsic_proto::motion_planning::v1::RobotSpecification robot_specification =
      ::intrinsic::motion_planning::CreateRobotSpecification(
          robot_object, start_configuration);
  INTR_ASSIGN_OR_RETURN(
      const auto motion_specs,
      CreateMotionSpecificationFromMoveRobotSkillParams(skill_parameters));
  MotionPlannerClient::MotionPlanningOptions planning_options;
  if (skill_parameters.has_planning_parameters()) {
    if (skill_parameters.planning_parameters().has_max_planning_time_in_sec()) {
      planning_options.path_planning_time_out =
          skill_parameters.planning_parameters().max_planning_time_in_sec();
    }
    if (skill_parameters.planning_parameters().has_path_planner_step_size()) {
      planning_options.path_planning_step_size =
          skill_parameters.planning_parameters().path_planner_step_size();
    }
    if (skill_parameters.planning_parameters()
            .has_lock_motion_configuration()) {
      planning_options.lock_motion_configuration =
          skill_parameters.planning_parameters().lock_motion_configuration();
    }
    if (skill_parameters.planning_parameters().has_skip_fuzzy_cache_check()) {
      planning_options.skip_fuzzy_cache_check =
          skill_parameters.planning_parameters().skip_fuzzy_cache_check();
    }
    if (skill_parameters.planning_parameters()
            .has_shortcutting_combine_collinear_segments()) {
      planning_options.shortcutting_combine_collinear_segments =
          skill_parameters.planning_parameters()
              .shortcutting_combine_collinear_segments();
    }
    planning_options.collision_checker_config =
        skill_parameters.planning_parameters().collision_checker_config();
    if (skill_parameters.planning_parameters()
            .has_collision_check_spacing_override()) {
      planning_options.collision_check_spacing_override =
          skill_parameters.planning_parameters()
              .collision_check_spacing_override();
    }
    if (skill_parameters.planning_parameters()
            .has_enable_strict_trajectory_fallback()) {
      planning_options.enable_strict_trajectory_fallback =
          skill_parameters.planning_parameters()
              .enable_strict_trajectory_fallback();
    }
  }
  absl::StatusOr<MotionPlannerClient::PlanTrajectoryResult>
      status_or_trajectory =
          planner.PlanTrajectory(robot_specification, motion_specs,
                                 planning_options, caller_id, context);
  if (!status_or_trajectory.status().ok()) {
    auto status_w_user_friendly_error =
        GetMotionPlanningExtendedStatusErrorMessage(
            status_or_trajectory.status(), kUseRad);
    return status_w_user_friendly_error;
  }
  return status_or_trajectory.value();
}

absl::StatusOr<intrinsic_proto::skills::MoveRobotInternalData>
MoveRobot::InternalComputePlan(
    const intrinsic_proto::skills::MoveRobotParams& parameters,
    MotionPlannerClient& planner, const world::KinematicObject& robot_object,
    const eigenmath::VectorXd& start_configuration,
    const JointLimitsXd& application_limits, TimingDebugLogger& logger) {
  const stats::ScopedSpan span("skills.MoveRobot/InternalComputePlan");
  const absl::Time planning_start_time = absl::Now();
  // Plan trajectory
  INTR_ASSIGN_OR_RETURN(
      const MotionPlannerClient::PlanTrajectoryResult trajectory_result,
      PlanTrajectory(robot_object, planner, parameters, start_configuration,
                     "skills.MoveRobot/InternalComputePlan"));
  const double path_planning_duration_seconds =
      absl::ToDoubleSeconds(absl::Now() - planning_start_time);
  LOG(INFO) << "Motion planning took " << path_planning_duration_seconds
            << " s.";
  LOG(INFO) << "Planned trajectory of size "
            << trajectory_result.trajectory.state_size();
  intrinsic_proto::skills::MoveRobotExecutionPlan::RobotSpecifications
      robot_specification_proto;
  *robot_specification_proto.mutable_application_limits() =
      ToProto(application_limits);

  intrinsic_proto::skills::MoveRobotExecutionPlan execution_plan_result;
  *execution_plan_result.mutable_planned_trajectory() =
      trajectory_result.trajectory;
  *execution_plan_result.mutable_robot_specifications() =
      robot_specification_proto;

  *execution_plan_result.mutable_motion_events() = {
      trajectory_result.motion_events.begin(),
      trajectory_result.motion_events.end()};

  intrinsic_proto::skills::MoveRobotInternalData internal_data;
  *internal_data.mutable_execution_plan() = execution_plan_result;
  if (trajectory_result.lock_motion_id.has_value()) {
    internal_data.set_lock_motion_id(trajectory_result.lock_motion_id.value());
  }
  return internal_data;
}

absl::StatusOr<intrinsic_proto::skills::MoveRobotInternalData>
MoveRobot::ComputePlan(
    const intrinsic_proto::skills::MoveRobotParams& params,
    world::ObjectWorldClient& world,
    motion_planning::MotionPlannerClient& default_planner,
    std::optional<world::KinematicObject> robot_object) const {
  const stats::ScopedSpan span("skills.MoveRobot/ComputePlan");
  TimingDebugLogger logger("MoveRobot::ComputePlan");
  INTR_RETURN_IF_ERROR(ValidateInputParams(params));

  // If we are planning using the last commanded position, we bypass
  // preplanning because we cannot get the last commanded position from ICON in
  // ComputePlan.
  //
  // This is ok because we can do planning on the fly in Execute as well.
  if (params.plan_using_last_commanded_position()) {
    return intrinsic_proto::skills::MoveRobotInternalData();
  }

  if (!robot_object) {
    return absl::InternalError(
        "ComputePlan could not select robot object. This is an "
        "internal failure.");
  }

  // Get relevant limits from the world for planning
  INTR_ASSIGN_OR_RETURN(CartesianLimits default_cart_limits,
                        robot_object->GetCartesianLimits());

  if (!IsCartLimitsValid(default_cart_limits)) {
    return absl::FailedPreconditionError(
        "No valid Cartesian limits defined for robot. They either equal or "
        "less than zero. To fix this error, "
        "please set the correct Cartesian limits to the robot defined within "
        "the world service.");
  }

  // TODO(kmuelling): Ideally we should plan using the last commanded position
  // here. However, this is not possible when called through GetFootprint
  // because we cannot get the last commanded position from ICON there.
  //
  // As a consequence, we access the sensed position stored in the
  // `robot_object`. If the last commanded position is close to the application
  // limits of the robot, the sensed position might violate these limits.
  // As a workaround, we plan with the sensed position projected onto
  // the application limits in case of small limits violations.
  //
  // The tolerated limit violation should depend on the robot. As a first
  // approximation, we use the same tolerance for all robots.
  eigenmath::VectorXd start_configuration = robot_object->JointPositions();
  INTR_ASSIGN_OR_RETURN(const LimitCheckResult hard_limit_check_result,
                        IsWithinLimits(start_configuration,
                                       robot_object->JointApplicationLimits()));
  if (!hard_limit_check_result.p_ok) {
    LOG(WARNING) << "Start_configuration is outside of application limits: "
                 << start_configuration.transpose();

    INTR_ASSIGN_OR_RETURN(const LimitCheckResult soft_limit_check_result,
                          IsWithinLimits(start_configuration,
                                         robot_object->JointApplicationLimits(),
                                         kJointLimitTolerance));
    if (soft_limit_check_result.p_ok) {
      start_configuration =
          start_configuration
              .cwiseMin(robot_object->JointApplicationLimits().max_position)
              .cwiseMax(robot_object->JointApplicationLimits().min_position);
      LOG(INFO) << "Start_configuration projected onto application limits: "
                << start_configuration.transpose();
    } else {
      INTR_ASSIGN_OR_RETURN(
          const JointLimits application_limits_nd,
          ToJointLimitsNd(robot_object->JointApplicationLimits()));
      const std::string user_message = absl::StrFormat(
          "Start configuration (%s) is outside of application limits (%s). Try "
          "to either increase the application limits or jog the robot to a "
          "valid configuration.",
          JointPositionXdToString(start_configuration, kUseRad),
          JointPositionLimitsToString(application_limits_nd, kUseRad));
      return GetMotionPlanningExtendedStatusErrorMessage(
          absl::FailedPreconditionError(user_message), kJointLimitErrorCode,
          user_message);
    }
  }

  INTR_ASSIGN_OR_RETURN(
      std::unique_ptr<MotionPlannerClient> motion_planner_service_asset_client,
      GetMotionPlannerServiceAssetClient(world.GetWorldID(),
                                         params.motion_planner_service()));
  // TODO(b/524634328): Remove the Fallback Logic from the Skills.
  MotionPlannerClient& planner =
      (motion_planner_service_asset_client != nullptr)
          ? *motion_planner_service_asset_client
          : default_planner;

  // Plan trajectory and generate internal data
  logger.Start("planning");
  return InternalComputePlan(params, planner, *robot_object,
                             start_configuration,
                             robot_object->JointApplicationLimits(), logger);
}

absl::StatusOr<intrinsic_proto::skills::MoveRobotInternalData>
MoveRobot::ComputeAndStorePlan(
    absl::string_view context_id,
    const intrinsic_proto::skills::MoveRobotParams& params,
    world::ObjectWorldClient& world,
    motion_planning::MotionPlannerClient& planner,
    std::optional<world::KinematicObject> robot_object) const {
  // Drop any plan from a previous projection of this action before planning
  // (b/569565855). Re-projection under the same `context_id` means the world
  // may have changed, so the old plan must neither be returned here nor survive
  // a failed recomputation and be picked up by `Execute()` or `Preview()`.
  // This removes all keys of the context; MoveRobot only stores "plan".
  // Note: `Delete()` and `GetOrCompute()` are not atomic, but the Executive
  // projects an action one call at a time sequentially.
  if (!context_id.empty()) {
    GetSkillData().Delete(context_id);
  }
  return GetOrComputePlan(context_id, params, world, planner,
                          std::move(robot_object));
}

absl::StatusOr<intrinsic_proto::skills::MoveRobotInternalData>
MoveRobot::GetOrComputePlan(
    absl::string_view context_id,
    const intrinsic_proto::skills::MoveRobotParams& params,
    world::ObjectWorldClient& world,
    motion_planning::MotionPlannerClient& planner,
    std::optional<world::KinematicObject> robot_object) const {
  const auto compute_fn =
      [&]() -> absl::StatusOr<intrinsic_proto::skills::MoveRobotInternalData> {
    return ComputePlan(params, world, planner, robot_object);
  };
  return GetSkillData()
      .GetOrCompute<intrinsic_proto::skills::MoveRobotInternalData>(
          context_id, "plan", compute_fn);
}

absl::StatusOr<std::unique_ptr<google::protobuf::Message>> MoveRobot::Execute(
    const ExecuteRequest& request, ExecuteContext& context) {
  const stats::ScopedSpan span("skills.MoveRobot/Execute");
  TimingDebugLogger logger("MoveRobot::Execute");

  const absl::Time move_robot_execute_start_time = absl::Now();

  auto return_value =
      std::make_unique<intrinsic_proto::skills::MoveRobotReturnValue>();

  world::ObjectWorldClient& world = context.object_world();

  // Unpack all the necessary configuration.
  INTR_ASSIGN_OR_RETURN(
      const auto parameters,
      request.params<intrinsic_proto::skills::MoveRobotParams>());
  INTR_RETURN_IF_ERROR(ValidateInputParams(parameters));

  INTR_ASSIGN_OR_RETURN(auto handle,
                        context.equipment().GetHandle(kEquipmentSlot));
  INTR_ASSIGN_OR_RETURN(const auto connection_config,
                        skills::GetConnectionParamsFromHandle(handle));

  logger.Start("connect to icon and read params");
  const absl::Time icon_channel_creation_start_time = absl::Now();
  INTR_ASSIGN_OR_RETURN(
      std::shared_ptr<ChannelInterface> icon_channel,
      icon_channel_factory_->MakeChannel(connection_config,
                                         kGrpcClientConnectDefaultTimeout));
  if (icon_channel == nullptr) {
    return absl::InternalError("Failed to create ICON channel.");
  }
  const double icon_channel_creation_duration_seconds =
      absl::ToDoubleSeconds(absl::Now() - icon_channel_creation_start_time);
  LOG(INFO) << "ICON Channel creation took "
            << icon_channel_creation_duration_seconds << " s.";
  icon::Client icon_client(icon_channel);

  // We look into the resource data to find the parts to control. If multiple
  // parts are specified, we error unless the user has provided us with a part
  // name to disambiguate.
  //
  // Long term we would replace this with talking to icon directly to reduce the
  // amount of skill specific information carried by the assets framework.
  INTR_ASSIGN_OR_RETURN(
      auto position_part,
      context.equipment().Unpack<intrinsic_proto::icon::Icon2PositionPart>(
          MoveRobot::kEquipmentSlot, icon::kIcon2PositionPartKey),
      _ << "Failed to find Icon2PositionPart in equipment data.");
  INTR_ASSIGN_OR_RETURN(
      const skills::ArmPartInformation arm_part_info,
      skills::GetArmPartInformation(
          position_part, world,
          parameters.has_arm_part() ? std::make_optional(parameters.arm_part())
                                    : std::nullopt));

  std::optional<intrinsic_proto::skills::MoveRobotInternalData>
      internal_data_proto;
  INTR_ASSIGN_OR_RETURN(
      internal_data_proto,
      GetSkillData().Get<intrinsic_proto::skills::MoveRobotInternalData>(
          context.context_id(), "plan"));

  // Find the right object in the world.
  INTR_ASSIGN_OR_RETURN(world::KinematicObject kinematic_object,
                        world.GetKinematicObject(arm_part_info.object));

  // This Cleanup will ensure the joint position of the robot is updated in the
  // world no matter how we exit.
  absl::Cleanup update_robot_on_exit = [&arm_part_info, &icon_client,
                                        &kinematic_object, &world]() {
    auto part_status_or = icon_client.GetSinglePartStatus(arm_part_info.name);
    if (!part_status_or.ok()) {
      LOG(WARNING) << "Failed to get part status upon finishing Execute.";
      return;
    }
    eigenmath::VectorXd dof_values =
        icon::GetSensedJointPosition(*part_status_or);
    auto status = world.UpdateJointPositions(kinematic_object, dof_values);
    if (!status.ok()) {
      LOG(WARNING) << "Failed to update world with robot position upon "
                      "finishing Execute.";
    }
  };

  // Get the starting joint configuration to use. Prefer the last commanded
  // position from ICON if available, otherwise use the sensed position.
  INTR_ASSIGN_OR_RETURN(const eigenmath::VectorXd starting_configuration,
                        GetPreviouslyCommandedJointPositionFromIconIfAvailable(
                            icon_client, arm_part_info.name));
  INTR_RETURN_IF_ERROR(
      world.UpdateJointPositions(kinematic_object, starting_configuration));

  logger.Start("get preplanned solution");
  std::optional<intrinsic_proto::icon::JointTrajectoryPVA> preplanned_solution;

  std::vector<intrinsic_proto::motion_planning::v1::MotionEvent>
      preplanned_motion_events;

  INTR_ASSIGN_OR_RETURN(RobotParameters icon_arm_params,
                        ReadICONArmParams(arm_part_info.name, icon_client));
  if (internal_data_proto.has_value()) {
    INTR_ASSIGN_OR_RETURN(
        preplanned_solution,
        ValidateSolution(
            starting_configuration, kinematic_object,
            internal_data_proto.value().execution_plan(),
            JointLimitsXd::Create(icon_arm_params.application_limits),
            icon_arm_params.default_cartesian_limits));


    preplanned_motion_events = {
        internal_data_proto.value().execution_plan().motion_events().begin(),
        internal_data_proto.value().execution_plan().motion_events().end()};


    if (internal_data_proto.value().has_lock_motion_id()) {
      return_value->set_lock_motion_id(
          internal_data_proto.value().lock_motion_id());
    }
  }

  if (!preplanned_solution.has_value()) {
    logger.Start("planning");
    LOG(INFO) << "No preplanned solution available for execution. Replanning "
                 "during Execute.";

    LOG(INFO) << "Updating robot parameters in world. So replanning will not "
                 "happen again due to wrong parameter settings in the world. "
                 "Please make sure world has the correct joint limit, and "
                 "cartesian limits for this robot.";
    INTR_RETURN_IF_ERROR(world.UpdateJointLimits(
        kinematic_object,
        JointLimitsXd::Create(icon_arm_params.application_limits),
        JointLimitsXd::Create(icon_arm_params.system_limits)));
    INTR_RETURN_IF_ERROR(world.UpdateCartesianLimits(
        kinematic_object, icon_arm_params.default_cartesian_limits));
    // Setting up the robot specifications for the planning problem
    INTR_ASSIGN_OR_RETURN(
        std::unique_ptr<MotionPlannerClient>
            motion_planner_service_asset_client,
        GetMotionPlannerServiceAssetClient(
            world.GetWorldID(), parameters.motion_planner_service()));
    // Plan trajectory and generate prediction result
    // TODO(b/524634328): Remove the Fallback Logic from the Skills.
    MotionPlannerClient& planner =
        (motion_planner_service_asset_client != nullptr)
            ? *motion_planner_service_asset_client
            : context.motion_planner();
    const absl::Time planning_start_time = absl::Now();
    INTR_ASSIGN_OR_RETURN(
        const MotionPlannerClient::PlanTrajectoryResult trajectory_result,
        PlanTrajectory(kinematic_object, planner, parameters,
                       starting_configuration, "skills.MoveRobot/Execute",
                       context.logging_context().data_logger_context));
    const double path_planning_duration_seconds =
        absl::ToDoubleSeconds(absl::Now() - planning_start_time);
    LOG(INFO) << "Motion planning took " << path_planning_duration_seconds
              << " s.";

    // Populate `return_value` with metadata from the newly planned trajectory.
    // Explicitly clear `lock_motion_id` if the replanned trajectory does not
    // have one to avoid retaining stale IDs from an invalidated preplan.
    if (trajectory_result.lock_motion_id.has_value()) {
      return_value->set_lock_motion_id(
          trajectory_result.lock_motion_id.value());
    } else {
      return_value->clear_lock_motion_id();
    }
    preplanned_solution.emplace(trajectory_result.trajectory);

    preplanned_motion_events = std::move(trajectory_result.motion_events);

  }

  if (preplanned_solution.value().state_size() == 0) {
    LOG(INFO) << "Trajectory is empty. We already reached desired position.";
    return nullptr;
  }

  std::optional<intrinsic_proto::icon::v1::Condition>
      move_until_signal_condition = std::nullopt;
  if (parameters.execution_parameters().has_move_until_signal_parameters()) {
    INTR_ASSIGN_OR_RETURN(
        move_until_signal_condition,
        TranslateMoveUntilSignalParametersToCondition(
            parameters.execution_parameters().move_until_signal_parameters(),
            context.equipment()));
  }

  // If the move until signal feature is on and if the robot motion is
  // terminated due to the signal, this will be true.
  bool stopped_on_signal = false;

  data_logger::LogAsync(
          // clang-format off
          data_logger::Builder::PackAnyFrom(preplanned_solution.value())
          // clang-format on
          .WithContext(context.logging_context().data_logger_context)
          .WithEventSource(absl::StrCat("skills.", kMoveRobotSkillName,
                                        ".planned_trajectory"))
          .Item());
  LOG(INFO) << "Starting execution on the robot.";
  logger.Start("executing trajectory");
  const absl::Time execute_joint_trajectory_start_time = absl::Now();
  INTR_RETURN_IF_ERROR(
      motion_planning::ExecuteJointTrajectory(
          context.logging_context().data_logger_context, icon_channel,
          arm_part_info.name, preplanned_solution.value(),
          kDefaultSettlingTimeoutSeconds,
          !parameters.execution_parameters().ignore_is_settled_condition(),
          context.canceller(), move_until_signal_condition,
          &stopped_on_signal

          ,
          preplanned_motion_events

          ))
      .LogError();
  const double execute_joint_trajectory_duration_seconds =
      absl::ToDoubleSeconds(absl::Now() - execute_joint_trajectory_start_time);
  LOG(INFO) << "ExecuteJointTrajectory() took "
            << execute_joint_trajectory_duration_seconds << " s.";
  return_value->set_stopped_on_signal(stopped_on_signal);
  LOG(INFO) << "Stopped on signal return value: " << stopped_on_signal;

  const double move_robot_execute_duration_seconds =
      absl::ToDoubleSeconds(absl::Now() - move_robot_execute_start_time);
  LOG(INFO) << "MoveRobot::Execute() took "
            << move_robot_execute_duration_seconds << " s.";
  return return_value;
}

absl::StatusOr<std::unique_ptr<::google::protobuf::Message>> MoveRobot::Preview(
    const PreviewRequest& request, PreviewContext& context) {
  INTR_ASSIGN_OR_RETURN(
      const intrinsic_proto::skills::MoveRobotParams params,
      request.params<intrinsic_proto::skills::MoveRobotParams>());

  std::optional<world::KinematicObject> robot_object;
  if (params.has_arm_part()) {
    INTR_ASSIGN_OR_RETURN(
        robot_object,
        context.object_world().GetKinematicObject(params.arm_part()));
  } else {
    INTR_ASSIGN_OR_RETURN(
        robot_object, context.GetKinematicObjectForEquipment(kEquipmentSlot));
  }

  INTR_ASSIGN_OR_RETURN(
      const intrinsic_proto::skills::MoveRobotInternalData internal_data,
      GetOrComputePlan(context.context_id(), params, context.object_world(),
                       context.motion_planner(), robot_object),
      _ << "Failed to obtain or compute motion plan for Preview.");

  const auto& trajectory = internal_data.execution_plan().planned_trajectory();
  absl::Duration previous_time = absl::ZeroDuration();
  for (int i = 0; i < trajectory.state_size(); ++i) {
    INTR_ASSIGN_OR_RETURN(absl::Duration current_time,
                          ToAbslDuration(trajectory.time_since_start(i)));
    absl::Duration duration = current_time - previous_time;
    intrinsic_proto::world::ObjectWorldUpdates updates =
        CreateUpdateForPosition(*robot_object, trajectory.state(i).position());
    for (const auto& update : updates.updates()) {
      INTR_RETURN_IF_ERROR(
          context.RecordWorldUpdate(update, /*elapsed=*/duration, duration));
    }
    previous_time = current_time;
  }

  auto return_value =
      std::make_unique<intrinsic_proto::skills::MoveRobotReturnValue>();
  if (internal_data.has_lock_motion_id()) {
    return_value->set_lock_motion_id(internal_data.lock_motion_id());
  }
  return return_value;
}

}  // namespace intrinsic::skills
