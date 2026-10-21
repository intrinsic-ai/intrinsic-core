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

#include "intrinsic/motion_planning/motion_planner/motion_planning_utils.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/time/time.h"
#include "google/protobuf/any.pb.h"
#include "google/protobuf/duration.pb.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/control/parts/feature_interfaces.h"
#include "intrinsic/icon/utils/realtime_status_macro.h"
#include "intrinsic/kinematics/chain.h"
#include "intrinsic/kinematics/elements.h"
#include "intrinsic/kinematics/ik_solvers/kinematic_chain/kinematic_chain_newton_raphson_ik_solver.h"
#include "intrinsic/kinematics/state.h"
#include "intrinsic/kinematics/types/cartesian_limits.h"
#include "intrinsic/kinematics/types/joint_limits.h"
#include "intrinsic/kinematics/types/joint_limits_xd.h"
#include "intrinsic/kinematics/utils/compute_ik_util.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/proto_conversion.h"
#include "intrinsic/motion_planning/motion_planner/collision_settings_utils.h"
#include "intrinsic/motion_planning/motion_planner/robot_specification.h"
#include "intrinsic/motion_planning/motion_planner/trajectory_segment.h"
#include "intrinsic/motion_planning/path_planning/kinematics_system_proxy.h"
#include "intrinsic/motion_planning/path_planning/kinematics_system_proxy_util.h"
#include "intrinsic/motion_planning/path_planning/path_planner_definitions.h"
#include "intrinsic/motion_planning/path_planning/planners/joint_interpolation_configs.pb.h"
#include "intrinsic/motion_planning/path_planning/planners/linear_cartesian_motion_path_planner_configs.pb.h"
#include "intrinsic/motion_planning/path_planning/planners/pipeline_configs.pb.h"
#include "intrinsic/motion_planning/path_planning/planners/rrt_configs.pb.h"
#include "intrinsic/motion_planning/path_planning/planners/shortcutter_configs.pb.h"
#include "intrinsic/motion_planning/path_planning/validators.h"
#include "intrinsic/motion_planning/proto/motion_specification_proto_utils.h"
#include "intrinsic/motion_planning/proto/v1/motion_blending_parameter.pb.h"

#include "intrinsic/motion_planning/proto/v1/motion_events.pb.h"

#include "intrinsic/motion_planning/proto/v1/motion_planner_config.pb.h"
#include "intrinsic/motion_planning/proto/v1/motion_planning_limits.pb.h"
#include "intrinsic/motion_planning/proto/v1/motion_specification.pb.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/collision/collision_context.pb.h"
#include "intrinsic/world/collision/util/make_rule_set.h"
#include "intrinsic/world/objects/kinematic_object_internal.h"
#include "intrinsic/world/objects/object_world.h"
#include "intrinsic/world/proto/collision_settings.pb.h"

namespace intrinsic {

namespace {

using intrinsic_proto::motion_planning::LinearCartesianMotionPathPlannerConfig;
using intrinsic_proto::motion_planning::v1::MotionPlannerConfiguration;
using intrinsic_proto::motion_planning::v1::MotionSegment;

// Returns true if the motion segment specifies a path constraints of type
// linear. False otherwise.
inline bool IsLinearMotion(const MotionSegment& segment) {
  return segment.motion_type() == MotionSegment::LINEAR;
}

// Returns true if the motion segment specificies either (i) a path constraint
// of type joint interpolation or (ii) does not specify any path constraints
// and disables collision checking.
inline bool IsJointInterpolationMotion(const MotionSegment& segment) {
  bool specifies_no_path_constraints = !(segment.has_path_constraints());
  bool disable_collision =
      segment.has_collision_settings() &&
      segment.collision_settings().disable_collision_checking();
  return ((segment.motion_type() == MotionSegment::JOINT) ||
          (specifies_no_path_constraints && disable_collision));
}

inline bool CollisionCheckingIsDisabled(const MotionSegment& segment) {
  return segment.has_collision_settings() &&
         segment.collision_settings().disable_collision_checking();
}

// Returns nullopt if collision checking is disabled and if specified
// the rule set contained in the `MotionSegment`.
absl::StatusOr<std::optional<intrinsic_proto::RuleSet>>
GetRuleSetFromMotionSegment(const object_world::ObjectWorld& object_world,
                            const MotionSegment& motion_segment) {
  std::optional<intrinsic_proto::RuleSet> collision_rule_set = std::nullopt;
  if (motion_segment.has_collision_settings() &&
      !(motion_segment.collision_settings().disable_collision_checking())) {
    INTR_ASSIGN_OR_RETURN(
        collision_rule_set,
        MakeRuleSet(motion_segment.collision_settings(), object_world));
  }
  return collision_rule_set;
}

LinearCartesianMotionPathPlannerConfig GetLinearCartesianMotionConfig(
    const PlannerConfigurationOptions& configuration_options) {
  LinearCartesianMotionPathPlannerConfig linear_move_config;
  if (configuration_options.cart_translation_rounding_m.has_value()) {
    linear_move_config.set_translational_rounding_m(
        configuration_options.cart_translation_rounding_m.value());
  }
  if (configuration_options.cart_rotation_rounding_rad.has_value()) {
    linear_move_config.set_rotational_rounding_rad(
        configuration_options.cart_rotation_rounding_rad.value());
  }
  if (configuration_options.cart_translation_sampling_distance_m.has_value()) {
    linear_move_config.set_translational_path_sampling_distance_m(
        configuration_options.cart_translation_sampling_distance_m.value());
  }
  if (configuration_options.cart_rotation_sampling_distance_rad.has_value()) {
    linear_move_config.set_rotational_path_sampling_distance_rad(
        configuration_options.cart_rotation_sampling_distance_rad.value());
  }
  if (configuration_options.joint_sampling_distance_rad.has_value()) {
    linear_move_config.set_joint_sampling_distance_rad(
        configuration_options.joint_sampling_distance_rad.value());
  }
  linear_move_config.set_collision_checking_spacing(
      configuration_options.collision_check_spacing_rad);
  *linear_move_config.mutable_tip_t_target() =
      ToProto(configuration_options.tip_t_tool);
  return linear_move_config;
}

ForwardKinematicsFunction CreateForwardKinematicsFunction(
    const icon::ManipulatorKinematics& manipulator_kinematics,
    const Pose3d& tip_t_tool) {
  return [=, &manipulator_kinematics](
             const eigenmath::VectorXd& joint_configuration)
             -> absl::StatusOr<Pose3d> {
    INTRINSIC_RT_ASSIGN_OR_RETURN(
        const Pose3d base_t_tip,
        manipulator_kinematics.ComputeChainFK(joint_configuration));
    return base_t_tip * tip_t_tool;
  };
}

// Creates an `InverseKinematicsFunction` that internally calls the
// `ComputeSingularityRobustIK` function when given the `manipulator_kinematics`
// `singularity_robust_ik_solver`, `joint_limits` and `tip_t_tool`.
// The `ComputeSingularityRobustIK` computes singularity-robust IK solutions for
// the given `base_t_tip` computed from `base_t_tool` and `tip_t_tool`. The
// initial solution is computed using the IK solver of the
// `manipulator_kinematics`, given the `hint_joint_configuration` and
// `joint_limits`. If the initial solution is close to singularity,
// `singularity_robust_ik_solver` will be used to compute a more
// singularity-robust IK solution. The returned solutions are ranked by their
// distance to the `hint_joint_configuration`.
// This function does not check collisions for any of the solutions.
InverseKinematicsFunction CreateInverseKinematicsFunction(
    const icon::ManipulatorKinematics& manipulator_kinematics,
    kinematics::KinematicChainRandomSeedIKSolver& singularity_robust_ik_solver,
    const JointLimits& joint_limits, const Pose3d& tip_t_tool) {
  return [&manipulator_kinematics, &singularity_robust_ik_solver, &joint_limits,
          tip_t_tool](const Pose3d& base_t_tool,
                      const eigenmath::VectorXd& hint_joint_configuration)
             -> absl::StatusOr<eigenmath::VectorXd> {
    const Pose3d base_t_tip = base_t_tool * tip_t_tool.inverse();
    INTR_ASSIGN_OR_RETURN(
        std::vector<eigenmath::VectorNd> ik_solution_joint_configurations,
        ComputeSingularityRobustIK(
            manipulator_kinematics, hint_joint_configuration,
            /*base_t_target=*/base_t_tool, /*tip_t_target=*/tip_t_tool,
            joint_limits, /*closest_hint_joint_configuration=*/std::nullopt,
            &singularity_robust_ik_solver,
            /*ensure_same_branch=*/false));
    return ik_solution_joint_configurations[0];
  };
}

GeometricJacobianFunction CreateGeometricJacobianFunction(
    const icon::ManipulatorKinematics& manipulator_kinematics,
    const Pose3d& tip_t_tool) {
  return [&manipulator_kinematics,
          tip_t_tool](const eigenmath::VectorXd& joint_configuration)
             -> absl::StatusOr<eigenmath::Matrix6Nd> {
    INTRINSIC_RT_ASSIGN_OR_RETURN(kinematics::ElementId tip_id,
                                  manipulator_kinematics.GetKinematicsModel()
                                      .FindNonBranchingKinematicChainTip());
    INTRINSIC_RT_ASSIGN_OR_RETURN(
        const kinematics::Chain* chain,
        manipulator_kinematics.GetKinematicsChain(tip_id));
    kinematics::State state(chain);
    INTR_RETURN_IF_ERROR(
        state.SetDofPositions(joint_configuration, /*check_limits=*/false));
    INTR_ASSIGN_OR_RETURN(
        const eigenmath::MatrixNMd jacobian,
        state.ComputeJacobian(tip_id, tip_t_tool.translation()));
    return jacobian;
  };
}

}  // namespace

absl::StatusOr<intrinsic::proto::PipelinePathPlannerConfig>
GetPathPlanningPipelineForMotionSegment(
    const MotionPlannerConfiguration& motion_config,
    const MotionSegment& segment,
    const PlannerConfigurationOptions& configuration_options) {
  const intrinsic::proto::PointValidatorSpecification point_spec =
      GetDefaultPointValidatorSpecification();
  INTR_ASSIGN_OR_RETURN(
      const intrinsic::proto::EdgeValidatorSpecification edge_spec,
      GetEdgeValidatorSpecification(
          configuration_options.collision_check_spacing_rad));

  // Linear Cartesian Motion
  intrinsic::proto::PipelinePathPlannerConfig pipeline_config;
  if (IsLinearMotion(segment)) {
    auto linear_move_planner = pipeline_config.add_specs();
    linear_move_planner->set_name("LinearCartesianMotionPathPlanner");
    if (!linear_move_planner->mutable_config()->PackFrom(
            GetLinearCartesianMotionConfig(configuration_options))) {
      return absl::InternalError(
          "Failed to pack LinearCartesianMotionPathPlanner config");
    }
    return pipeline_config;
  }

  // Joint Interpolation Motion
  if (IsJointInterpolationMotion(segment)) {
    auto joint_move_planner = pipeline_config.add_specs();
    joint_move_planner->set_name("JointInterpolationPathPlanner");
    intrinsic::proto::JointInterpolationPathPlannerConfig joint_planner_config;
    if (motion_config.has_timeout_sec()) {
      double path_planning_time_out =
          motion_config.timeout_sec().seconds() +
          motion_config.timeout_sec().nanos() / (1e9);
      joint_planner_config.set_timeout_seconds(path_planning_time_out);
    }
    *joint_planner_config.mutable_point_validator_spec() = point_spec;
    *joint_planner_config.mutable_edge_validator_spec() = edge_spec;
    if (!joint_move_planner->mutable_config()->PackFrom(joint_planner_config)) {
      return absl::InternalError(
          "Failed to pack JointInterpolationPathPlanner config");
    }
    return pipeline_config;
  }

  // Default planner: Set up a pipeline consisting of rrt and joint shortcutter.
  // 1. Rrt with timeout to plan a collision free path.
  auto rrt_planner = pipeline_config.add_specs();
  rrt_planner->set_name("RrtConnectPathPlanner");
  intrinsic::proto::RrtPathPlannerConfig rrt_path_planner_config;
  if (motion_config.has_timeout_sec()) {
    double path_planning_time_out = motion_config.timeout_sec().seconds() +
                                    motion_config.timeout_sec().nanos() / (1e9);
    rrt_path_planner_config.mutable_rrt_connect_config()->set_timeout_seconds(
        path_planning_time_out);
  }
  if (motion_config.has_path_planning_step_size()) {
    if (motion_config.path_planning_step_size() <= 0) {
      return absl::InvalidArgumentError(
          "Planning option 'path planning step size' is zero or negative. "
          "Please set a positive valued step size.");
    }
    rrt_path_planner_config.mutable_rrt_connect_config()->set_step_size(
        motion_config.path_planning_step_size());
  }
  *rrt_path_planner_config.mutable_point_validator_spec() = point_spec;
  *rrt_path_planner_config.mutable_edge_validator_spec() = edge_spec;
  if (!rrt_planner->mutable_config()->PackFrom(rrt_path_planner_config)) {
    return absl::InternalError("Failed to pack RrtConnectPathPlanner config");
  }

  // 2. JointShortcutter for smoothing the results of the planned path.
  auto joint_shortcutter = pipeline_config.add_specs();
  joint_shortcutter->set_name("JointShortcutter");
  intrinsic::proto::JointShortcutterConfig joint_shortcutter_config;
  joint_shortcutter_config.set_use_binary_search(true);
  joint_shortcutter_config.set_combine_collinear_segments(
      motion_config.shortcutting_combine_collinear_segments());
  *joint_shortcutter_config.mutable_point_validator_spec() = point_spec;
  *joint_shortcutter_config.mutable_edge_validator_spec() = edge_spec;
  if (!joint_shortcutter->mutable_config()->PackFrom(
          joint_shortcutter_config)) {
    return absl::InternalError("Failed to pack JointShortcutter config");
  }
  return pipeline_config;
}

absl::StatusOr<ProxyCreateInfo> GetKinematicSystemsProxyCreateInfoForSegment(
    const object_world::ObjectWorld& object_world,
    const intrinsic_proto::motion_planning::v1::MotionSegment& segment) {
  // Get the rule set for the collision setting if not disabled
  intrinsic_proto::RuleSet rule_set;
  if (!CollisionCheckingIsDisabled(segment)) {
    INTR_ASSIGN_OR_RETURN(std::optional<intrinsic_proto::RuleSet> rule_set_opt,
                          GetRuleSetFromMotionSegment(object_world, segment));
    rule_set = rule_set_opt.value_or(intrinsic_proto::RuleSet());
  }
  return ProxyCreateInfo{
      .constraints_proto = segment.path_constraints(),
      .rule_set = rule_set,
      .disable_collision_checking = CollisionCheckingIsDisabled(segment)};
}

absl::StatusOr<std::unique_ptr<KinematicsSystemProxy>>
GetKinematicSystemsProxyForSegment(
    const object_world::ObjectWorld& object_world,
    const object_world::KinematicObject& robot,
    const intrinsic_proto::world::CollisionCheckerConfig&
        collision_checker_config,
    const MotionSegment& segment,
    std::optional<int> maybe_concurrent_thread_count) {
  INTR_ASSIGN_OR_RETURN(
      ProxyCreateInfo create_info,
      GetKinematicSystemsProxyCreateInfoForSegment(object_world, segment));
  return CreateKinematicsProxy(
      object_world, robot,
      KinematicsProxyOptions{
          .collision_checker_config = collision_checker_config,
          .constraints_proto = std::move(create_info.constraints_proto),
          .rule_set = std::move(create_info.rule_set),
          .disable_collision_checking = create_info.disable_collision_checking,
          .maybe_concurrent_thread_count = maybe_concurrent_thread_count,
      });
}
absl::StatusOr<CartesianKinematicsComponents>
CreateCartesianKinematicsComponents(const KinematicsSystemProxy& proxy,
                                    const Pose3d& tip_t_tool) {
  CartesianKinematicsComponents cartesian_kinematics_components;
  INTR_ASSIGN_OR_RETURN(cartesian_kinematics_components.manipulator_kinematics,
                        proxy.GetManipulatorKinematics());

  INTR_ASSIGN_OR_RETURN(JointLimits joint_limits,
                        ToJointLimits(proxy.GetJointLimits()));
  cartesian_kinematics_components.joint_limits =
      std::make_unique<JointLimits>(joint_limits);

  // Create a singularity robust IK to be used in the proximity of singular
  // configurations.
  INTR_ASSIGN_OR_RETURN(kinematics::ElementId tip,
                        cartesian_kinematics_components.manipulator_kinematics
                            ->GetKinematicsModel()
                            .FindNonBranchingKinematicChainTip());
  INTRINSIC_RT_ASSIGN_OR_RETURN(
      const kinematics::Chain* chain,
      cartesian_kinematics_components.manipulator_kinematics
          ->GetKinematicsChain(tip));
  cartesian_kinematics_components.singularity_robust_ik_solver =
      std::make_unique<kinematics::KinematicChainRandomSeedIKSolver>(chain);
  cartesian_kinematics_components.singularity_robust_ik_solver->setTimeout(
      kTimeoutSingularityRobustIkSolver);
  cartesian_kinematics_components.forward_kinematics_function =
      CreateForwardKinematicsFunction(
          /*manipulator_kinematics=*/*(
              cartesian_kinematics_components.manipulator_kinematics),
          /*tip_t_tool=*/tip_t_tool);
  cartesian_kinematics_components.inverse_kinematics_function =
      CreateInverseKinematicsFunction(
          /*manipulator_kinematics=*/*(
              cartesian_kinematics_components.manipulator_kinematics),
          /*singularity_robust_ik_solver=*/
          *(cartesian_kinematics_components.singularity_robust_ik_solver),
          /*joint_limits=*/
          *(cartesian_kinematics_components.joint_limits),
          /*tip_t_tool=*/tip_t_tool);
  cartesian_kinematics_components.geometric_jacobian_function =
      CreateGeometricJacobianFunction(
          /*manipulator_kinematics=*/*(
              cartesian_kinematics_components.manipulator_kinematics),
          /*tip_t_tool=*/tip_t_tool);
  return cartesian_kinematics_components;
}

double GetCartesianTranslationalRoundingFromProto(
    std::optional<intrinsic_proto::motion_planning::v1::BlendingParameters>
        blending_parameters,
    double default_translational_rounding,
    double minimum_translational_rounding) {
  double translational_rounding = default_translational_rounding;
  if (blending_parameters.has_value() &&
      blending_parameters->has_cartesian_blending()) {
    if (blending_parameters->cartesian_blending()
            .has_translation_corner_rounding()) {
      translational_rounding = blending_parameters->cartesian_blending()
                                   .translation_corner_rounding();
    }
  }
  return std::max(translational_rounding, minimum_translational_rounding);
}

double GetCartesianRotationalRoundingFromProto(
    std::optional<intrinsic_proto::motion_planning::v1::BlendingParameters>
        blending_parameters,
    double default_rotational_rounding, double minimum_rotational_rounding) {
  double rotational_rounding = default_rotational_rounding;
  if (blending_parameters.has_value() &&
      blending_parameters->has_cartesian_blending()) {
    if (blending_parameters->cartesian_blending()
            .has_rotational_corner_rounding()) {
      rotational_rounding = blending_parameters->cartesian_blending()
                                .rotational_corner_rounding();
    }
  }
  return std::max(rotational_rounding, minimum_rotational_rounding);
}

absl::StatusOr<bool> CheckIfDynamicJointLimitsAreCompatible(
    const intrinsic_proto::motion_planning::v1::MotionSegment& motion_segment,
    const JointLimits& joint_limits, double max_allowed_error) {
  if (!joint_limits.IsValid() || joint_limits.size() < 1) {
    return absl::InvalidArgumentError("Provided joint limits are not valid.");
  }
  // First check if we have dynamic joint limits, i.e., velocity, acceleration,
  // jerk
  if (!motion_segment.has_joint_limits()) {
    return true;
  }
  const auto& limit = motion_segment.joint_limits();
  INTR_ASSIGN_OR_RETURN(
      const JointLimits segment_limits,
      ParseJointLimitsFromProto(motion_segment, joint_limits));

  if (limit.has_max_velocity() &&
      (segment_limits.max_velocity - joint_limits.max_velocity).norm() >
          max_allowed_error) {
    return false;
  }
  if (limit.has_max_acceleration() &&
      (segment_limits.max_acceleration - joint_limits.max_acceleration).norm() >
          max_allowed_error) {
    return false;
  }

  if (limit.has_max_jerk() &&
      (segment_limits.max_jerk - joint_limits.max_jerk).norm() >
          max_allowed_error) {
    return false;
  }
  return true;
}

absl::StatusOr<bool> CheckIfPositionJointLimitsAreCompatible(
    const MotionSegment& motion_segment, const JointLimits& joint_limits,
    double max_allowed_error) {
  if (!motion_segment.has_joint_limits() ||
      (!motion_segment.joint_limits().has_max_position() &&
       !motion_segment.joint_limits().has_min_position())) {
    return true;
  }

  INTR_ASSIGN_OR_RETURN(
      const JointLimits new_limits,
      ParseJointLimitsFromProto(motion_segment, joint_limits));

  if ((new_limits.max_position - joint_limits.max_position).norm() >
      max_allowed_error) {
    return false;
  }

  if ((new_limits.min_position - joint_limits.min_position).norm() >
      max_allowed_error) {
    return false;
  }
  return true;
}

absl::StatusOr<bool> CheckIfCartesianLimitsAreCompatible(
    const MotionSegment& motion_segment, const CartesianLimits& cart_limits,
    double max_allowed_vel_error, double max_allowed_accel_error) {
  if (!motion_segment.has_cartesian_limits()) {
    return true;
  }

  const auto& limits = motion_segment.cartesian_limits();
  if (limits.has_max_translational_velocity() &&
      fabs(cart_limits.max_translational_velocity.maxCoeff() -
           limits.max_translational_velocity()) > max_allowed_vel_error) {
    return false;
  }
  if (limits.has_max_rotational_velocity() &&
      fabs(cart_limits.max_rotational_velocity -
           limits.max_rotational_velocity()) > max_allowed_vel_error) {
    return false;
  }
  if (limits.has_max_translational_acceleration() &&
      fabs(cart_limits.max_translational_acceleration.maxCoeff() -
           limits.max_translational_acceleration()) > max_allowed_accel_error) {
    return false;
  }
  if (limits.has_max_rotational_acceleration() &&
      fabs(cart_limits.max_rotational_acceleration -
           limits.max_rotational_acceleration()) > max_allowed_accel_error) {
    return false;
  }
  return true;
}

absl::StatusOr<std::vector<TrajectorySegment>>
ParseTrajectorySegmentsFromMotionSpecification(
    const object_world::ObjectWorld& object_world,
    const RobotSpecification& robot_specification,
    const intrinsic_proto::motion_planning::v1::MotionSpecification&
        motion_specification) {
  constexpr double kDynamicCartesianError = 1e-6;
  constexpr double kPositionJointError = 1e-6;
  constexpr double kDynamicJointError = 1e-6;
  // TODO(kmuelling): We currently only support trajectory generation with
  // identical trajectory type (cartesian vs joint) and identical limits.
  std::vector<TrajectorySegment> trajectory_segments;
  const int motion_segments_size = motion_specification.motion_segments_size();
  for (int motion_segment_number = 0;
       motion_segment_number < motion_segments_size; ++motion_segment_number) {
    const auto& motion_segment =
        motion_specification.motion_segments(motion_segment_number);


    for (const auto& event : motion_segment.motion_segment_events()) {
      // Return a series of informational errors if the events are not
      // correctly specified.
      //
      // If we have multiple segments then the first segment should only have
      // events which are defined relative to the segment start. Similarly
      // below, if we have multiple segments then the last segment should only
      // have events which are defined relative to the segment target.
      if (motion_segment_number == 0 && motion_segments_size != 1 &&
          event.event_location().relative_to() !=
              intrinsic_proto::motion_planning::v1::MotionEventSegmentLocation::
                  SEGMENT_START) {
        return absl::InvalidArgumentError(
            "Motion events in the first segment must be relative to the "
            "segment start.");
      }
      if (motion_segment_number == motion_segments_size - 1 &&
          motion_segments_size != 1 &&
          event.event_location().relative_to() !=
              intrinsic_proto::motion_planning::v1::MotionEventSegmentLocation::
                  SEGMENT_TARGET) {
        return absl::InvalidArgumentError(
            "Motion events in the last segment must be relative to the "
            "segment target.");
      }
      if (motion_segment_number > 0 &&
          motion_segment_number < motion_segments_size - 1) {
        return absl::InvalidArgumentError(
            "Motion events are currently only supported in the first and last "
            "segment.");
      }
    }


    INTR_ASSIGN_OR_RETURN(
        const TrajectorySegment::Type traj_type,
        TrajectorySegment::ValidatePathConstraintsAndReturnTrajectoryType(
            motion_segment));
    // If created already a linear move trajectory segment, we check for
    // compatibility and only add the motion segment to the existing trajectory
    // segment. Reason: We do not support Cartesian blending between trajectory
    // segments yet. This currently happens within the path planner
    // (b/309819021).
    if (!trajectory_segments.empty() &&
        trajectory_segments.back().trajectory_type ==
            TrajectorySegment::Type::kBlendedCartesian &&
        traj_type == TrajectorySegment::Type::kBlendedCartesian) {
      INTR_ASSIGN_OR_RETURN(
          const bool cart_limits_compatible,
          CheckIfCartesianLimitsAreCompatible(
              motion_segment, trajectory_segments.back().cart_limits,
              /*max_allowed_vel_error=*/kDynamicCartesianError,
              /*max_allowed_accel_error=*/kDynamicCartesianError));
      if (!cart_limits_compatible) {
        return absl::InvalidArgumentError(absl::StrFormat(
            "Motion segment %d contains different Cartesian limits "
            "than the previous segment. We currently do not "
            "support neighboring segments of type 'LINEAR' "
            "that have different Cartesian limits.",
            motion_segment_number));
      }

      INTR_ASSIGN_OR_RETURN(
          const bool dynamic_joint_limits_compatible,
          CheckIfDynamicJointLimitsAreCompatible(
              motion_segment, trajectory_segments.back().joint_limits,
              /*max_allowed_error=*/kDynamicJointError));
      if (!dynamic_joint_limits_compatible) {
        return absl::InvalidArgumentError(
            absl::StrFormat("Motion segment %d contains different joint limits "
                            "than the previous segment. We currently do not "
                            "support neighboring segments of type 'LINEAR' "
                            "that have different joint limits.",
                            motion_segment_number));
      }

      INTR_ASSIGN_OR_RETURN(
          const bool position_joint_limits_compatible,
          CheckIfPositionJointLimitsAreCompatible(
              motion_segment, trajectory_segments.back().joint_limits,
              /*max_allowed_error=*/kPositionJointError));
      if (!position_joint_limits_compatible) {
        return absl::InvalidArgumentError(absl::StrFormat(
            "Motion segment %d contains joint position limits that are not "
            "identical to the limits in the segment before. We currently do "
            "not support neighboring segments of type 'LINEAR' "
            "that have different joint position limits.",
            motion_segment_number));
      }

      INTR_ASSIGN_OR_RETURN(
          const bool collision_settings_match,
          CollisionSettingsMatch(object_world,
                                 motion_segment.collision_settings(),
                                 trajectory_segments.back()
                                     .motion_segments.back()
                                     .collision_settings()));
      if (!collision_settings_match) {
        return absl::InvalidArgumentError(absl::StrFormat(
            "Motion segment %d defines different collision settings than "
            "the previous segment. We currently do not support segments with "
            "different collision settings for blended Cartesian motions.",
            motion_segment_number));
      }

      trajectory_segments.back().motion_segments.push_back(motion_segment);
    } else {
      INTR_ASSIGN_OR_RETURN(
          TrajectorySegment new_segment,
          TrajectorySegment::Create(
              object_world, robot_specification, motion_segment,
              motion_specification.has_curve_parameters()
                  ? std::make_optional(motion_specification.curve_parameters())
                  : std::nullopt));
      trajectory_segments.push_back(std::move(new_segment));
    }
  }
  return trajectory_segments;
}
absl::StatusOr<double> GetCollisionCheckSpacing(
    const MotionPlannerFlags& flags,
    const intrinsic_proto::motion_planning::v1::MotionPlannerConfiguration&
        motion_config) {
  if (motion_config.has_collision_check_spacing_override()) {
    if (motion_config.collision_check_spacing_override() >
        flags.default_collision_check_spacing) {
      return absl::InvalidArgumentError(
          absl::StrFormat("MotionPlannerConfiguration specified a "
                          "collision_check_spacing_override of %f but "
                          "the maximum collision check spacing is %f.",
                          motion_config.collision_check_spacing_override(),
                          flags.default_collision_check_spacing));
    }
    return motion_config.collision_check_spacing_override();
  }
  return flags.default_collision_check_spacing;
}

std::optional<int> GetConcurrentThreadCount(
    const MotionPlannerFlags& flags,
    const DistanceCheckStatistics* distance_check_statistics) {
  if (!flags.enable_concurrent_collision_checking) {
    LOG(INFO) << "Creating single-threaded proxy for validity checking.";
    return std::nullopt;
  }
  if (distance_check_statistics) {
    LOG(INFO) << "Disabling concurrent collision checking because we are "
                 "collecting collision statistics.";
    return std::nullopt;
  }
  if (flags.concurrent_collision_checking_thread_count < 1) {
    LOG(INFO) << "Invalid thread count ("
              << flags.concurrent_collision_checking_thread_count
              << "). Disabling concurrent collision checking.";
    return std::nullopt;
  }
  LOG(INFO) << "Creating multithreaded proxy for validity checking.";
  return flags.concurrent_collision_checking_thread_count;
}

}  // namespace intrinsic
