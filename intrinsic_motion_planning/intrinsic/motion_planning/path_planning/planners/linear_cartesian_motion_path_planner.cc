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

#include "intrinsic/motion_planning/path_planning/planners/linear_cartesian_motion_path_planner.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "Eigen/Core"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "google/protobuf/any.pb.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/control/parts/feature_interfaces.h"
#include "intrinsic/icon/proto/joint_space.pb.h"
#include "intrinsic/icon/utils/realtime_status_macro.h"
#include "intrinsic/kinematics/types/dynamic_limits_check_mode.h"
#include "intrinsic/kinematics/types/joint_state.h"
#include "intrinsic/kinematics/types/joint_trajectories.h"
#include "intrinsic/math/ipow.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/proto_conversion.h"
#include "intrinsic/motion_planning/motion_planner/motion_planning_error_utils.h"
#include "intrinsic/motion_planning/path_planning/data_structures/path_planner_graph.h"
#include "intrinsic/motion_planning/path_planning/kinematics_system_proxy.h"
#include "intrinsic/motion_planning/path_planning/parametric_curves/cartesian_parametric_curve.h"
#include "intrinsic/motion_planning/path_planning/parametric_curves/cartesian_polyline_bsplines_curve.h"
#include "intrinsic/motion_planning/path_planning/parametric_curves/parametric_curve_utils.h"
#include "intrinsic/motion_planning/path_planning/path_ik/path_ik.h"
#include "intrinsic/motion_planning/path_planning/path_planner.h"
#include "intrinsic/motion_planning/path_planning/path_planner_definitions.h"
#include "intrinsic/motion_planning/path_planning/planners/linear_cartesian_motion_path_planner_configs.pb.h"
#include "intrinsic/motion_planning/path_planning/planners/validation.h"
#include "intrinsic/motion_planning/proto/v1/motion_planning_error.pb.h"
#include "intrinsic/motion_planning/trajectory_planning/validation.h"
#include "intrinsic/stats/scoped_span.h"
#include "intrinsic/util/eigen.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic {

namespace {

using LinearCartesianMotionPathPlannerConfig =
    intrinsic_proto::motion_planning::LinearCartesianMotionPathPlannerConfig;

// Default values for proto config.
static constexpr double kDefaultTranslationalRoundingM = 0.05;
static constexpr double kDefaultRotationalRoundingRad = 0.1;
static constexpr double kDefaultCartesianSamplingDistance = 0.005;
static constexpr double kDefaultCollisionCheckingDistance = 0.01;

// Set default values for all non-set values in the config proto.
LinearCartesianMotionPathPlannerConfig SetDefaults(
    const LinearCartesianMotionPathPlannerConfig& config) {
  LinearCartesianMotionPathPlannerConfig out_config;

  out_config.set_translational_rounding_m(kDefaultTranslationalRoundingM);
  out_config.set_rotational_rounding_rad(kDefaultRotationalRoundingRad);
  out_config.set_translational_path_sampling_distance_m(
      kDefaultCartesianSamplingDistance);
  out_config.set_rotational_path_sampling_distance_rad(
      kDefaultCartesianSamplingDistance);
  out_config.set_collision_checking_spacing(kDefaultCollisionCheckingDistance);
  *out_config.mutable_tip_t_target() = ToProto(Pose3d::Identity());

  if (config.has_translational_rounding_m()) {
    out_config.set_translational_rounding_m(config.translational_rounding_m());
  }
  if (config.has_rotational_rounding_rad()) {
    out_config.set_rotational_rounding_rad(config.rotational_rounding_rad());
  }
  if (config.has_translational_path_sampling_distance_m()) {
    out_config.set_translational_path_sampling_distance_m(
        config.translational_path_sampling_distance_m());
  }
  if (config.has_rotational_path_sampling_distance_rad()) {
    out_config.set_rotational_path_sampling_distance_rad(
        config.rotational_path_sampling_distance_rad());
  }
  if (config.has_tip_t_target()) {
    *out_config.mutable_tip_t_target() = config.tip_t_target();
  }
  if (config.has_collision_checking_spacing()) {
    out_config.set_collision_checking_spacing(
        config.collision_checking_spacing());
  }
  if (config.has_joint_sampling_distance_rad()) {
    out_config.set_joint_sampling_distance_rad(
        config.joint_sampling_distance_rad());
  }

  return out_config;
}

absl::StatusOr<std::vector<PointPath>> PlanImpl(
    const LinearCartesianMotionPathPlannerConfig& config, const PointPath& path,
    const icon::ManipulatorKinematics* manipulator_kinematics,
    PathPlannerGraph* graph) {
  const stats::ScopedSpan span("LinearCartesianMotionPathPlanner/PlanImpl");
  absl::string_view kMotionPlanningErrorSuffix = "PlanImpl";
  if (manipulator_kinematics == nullptr) {
    return absl::InvalidArgumentError(
        "The manipulator kinematics cannot be a nullptr.");
  }
  // Quick return for zero motion.
  if (path.size() == 2 &&
      (path.back() - path.front()).norm() < kJointDeviationToleranceRad) {
    return std::vector<PointPath>{path};
  }

  // Create Cartesian parametric curve of linear motions with blending.
  INTR_ASSIGN_OR_RETURN(const Pose3d tip_t_target,
                        FromProto(config.tip_t_target()));
  std::vector<Pose3d> curve_waypoints(path.size());
  for (int i = 0; i < path.size(); ++i) {
    INTRINSIC_RT_ASSIGN_OR_RETURN(
        const Pose3d waypoint_base_t_tip,
        manipulator_kinematics->ComputeChainFK(path[i]));
    curve_waypoints[i] = waypoint_base_t_tip * tip_t_target;
  }

  INTR_ASSIGN_OR_RETURN(
      std::unique_ptr<CartesianParametricCurve> cartesian_curve,
      CartesianPolylineBSplinesCurve::Create(
          /*rotational_blending_radius=*/config.rotational_rounding_rad(),
          /*translational_blending_radius=*/config.translational_rounding_m(),
          /*pose_waypoints=*/absl::MakeConstSpan(curve_waypoints)));

  JointSpaceUniformArcLengthPreservingSamplingOptions options;
  if (config.has_joint_sampling_distance_rad()) {
    options.sampling_distance_rad = static_cast<std::optional<double>>(
        config.joint_sampling_distance_rad());
  }
  INTR_ASSIGN_OR_RETURN(
      std::vector<eigenmath::VectorNd> path_ik_solutions,
      PathIkOnCartesianCurveAndResampleForUniformJointSampling(
          *cartesian_curve, config.translational_path_sampling_distance_m(),
          config.rotational_path_sampling_distance_rad(),
          manipulator_kinematics, path.front(), tip_t_target,
          /*resampling_steps_function=*/
          CreateUniformResamplingStepsForJointSpacePathFunction(options),
          /*joint_limits=*/std::nullopt,
          /*force_odd_number_of_samples=*/false,
          /*validate_first_ik_solution_against_hint_joint_configuration=*/
          true));

  // Last configuration obtained from path IK should coincide with the given
  // path end configuration.
  if ((path_ik_solutions.back() - path.back()).squaredNorm() >
      ::intrinsic::IPow(kJointDeviationToleranceRad, 2)) {
    const std::string error_message = absl::StrCat(
        "Could not generate Cartesian linear path to the desired "
        "motion target joint configuration [",
        toString(path.back()),
        "] (in rad). Planner reached final joint configuration [",
        toString(path_ik_solutions.back()),
        "] (in rad). You can try to either use the reached final joint "
        "configuration as motion target or restrict the joint position limits "
        "of the motion target to favor IK solutions closer to the reached "
        "configuration.");
    return CreateStatusWithLinCartPathPlanningError(
        error_message, path.back(), path_ik_solutions.back(),
        kMotionPlanningErrorSuffix, absl::StatusCode::kNotFound,
        intrinsic_proto::motion_planning::v1::ErrorContext::
            LINEAR_CARTESIAN_PATH_PLANNER_ERROR);
  }
  std::vector<intrinsic::eigenmath::VectorXd> path_ik_solutions_xd;
  path_ik_solutions_xd.reserve(path_ik_solutions.size());
  path_ik_solutions_xd.insert(path_ik_solutions_xd.end(),
                              path_ik_solutions.begin(),
                              path_ik_solutions.end());
  return std::vector<PointPath>{path_ik_solutions_xd};
}

// Creates trajectory from path. Velocity and acceleration will be set to zero.
absl::StatusOr<JointTrajectoryPVA> EncapsulatePathInTrajectory(
    const PointPath& path) {
  std::vector<JointStatePVA> path_states;
  for (const auto& position : path) {
    JointStatePVA state = JointStatePVA::Zero(position.size());
    state.position = position;
    path_states.push_back(state);
  }
  return JointTrajectoryPVA::Create(
      std::move(path_states), absl::Milliseconds(2),
      DynamicLimitsCheckMode::kCheckNone,
      JointTrajectoryInterpolationType::kUnspecified);
}

}  // namespace

LinearCartesianMotionPathPlanner::LinearCartesianMotionPathPlanner(
    const intrinsic_proto::motion_planning::
        LinearCartesianMotionPathPlannerConfig& config)
    : config_(SetDefaults(config)) {
  CHECK_GT(config_.translational_rounding_m(), 0.0)
      << "No negative translational rounding allowed in config file of "
         "LinearCartesianMotionPathPathPlanner.";
  CHECK_GT(config_.rotational_rounding_rad(), 0.0)
      << "No negative rotational rounding allowed in config file of "
         "LinearCartesianMotionPathPathPlanner.";
  CHECK_GT(config_.translational_path_sampling_distance_m(), 0.0)
      << "No negative path translational sampling distance allowed in config "
         "file of LinearCartesianMotionPathPathPlanner.";
  CHECK_GT(config_.rotational_path_sampling_distance_rad(), 0.0)
      << "No negative path rotational sampling distance allowed in config "
         "file of LinearCartesianMotionPathPathPlanner.";
  CHECK_GT(config_.collision_checking_spacing(), 0.0)
      << "No negative collision checking space distance allowed for "
         "LinearCartesianMotionPathPlanner configuration.";
  if (config_.has_joint_sampling_distance_rad()) {
    CHECK_GT(config_.joint_sampling_distance_rad(), 0.0)
        << "No negative joint sampling distance allowed for "
           "LinearCartesianMotionPathPlanner configuration.";
  }
}

absl::StatusOr<std::vector<PointPath>> LinearCartesianMotionPathPlanner::Plan(
    const PointPath& path,
    const icon::ManipulatorKinematics* manipulator_kinematics,
    PathPlannerGraph* graph) const {
  if (path.size() < 2) {
    return absl::InvalidArgumentError(
        "The input path must contain at least two joint configurations.");
  }
  return PlanImpl(config_, path, manipulator_kinematics, graph);
}

absl::StatusOr<std::vector<PointPath>> LinearCartesianMotionPathPlanner::Plan(
    const PointPath& path, const KinematicsSystemProxy& proxy,
    PathPlannerGraph* graph) const {
  const stats::ScopedSpan span("LinearCartesianMotionPathPlanner/Plan");
  if (path.size() < 2) {
    return absl::InvalidArgumentError(
        "The input path must contain at least two joint configurations.");
  }

  INTR_RETURN_IF_ERROR(ValidateWithinLimitsAndValidStartAndEnd(path, proxy))
          .SetPrepend()
      << "Cannot generate a Cartesian linear path. Reason: ";

  // TODO(b/279222142) perform FK/IK calls directly via the proxy. For now we
  // extract a ManipulatorKinematics and work with it.
  INTR_ASSIGN_OR_RETURN(
      std::unique_ptr<icon::ManipulatorKinematics> manipulator_kinematics,
      proxy.GetManipulatorKinematics());

  INTR_ASSIGN_OR_RETURN(
      std::vector<PointPath> solutions,
      PlanImpl(config_, path, manipulator_kinematics.get(), graph));
  INTR_ASSIGN_OR_RETURN(JointTrajectoryPVA trajectory,
                        EncapsulatePathInTrajectory(solutions.front()));
  // TODO(b/311220297): Transition to point path interface for collision
  // checking.
  INTR_RETURN_IF_ERROR(
      CheckCollisions(trajectory, proxy, config_.collision_checking_spacing()))
          .SetPrepend()
      << "Cannot generate a Cartesian linear path. ";
  return solutions;
}

absl::StatusOr<std::unique_ptr<PathPlanner>>
LinearCartesianMotionPathPlanner::Create(
    const std::optional<google::protobuf::Any>& any_config) {
  LinearCartesianMotionPathPlannerConfig config;
  if (any_config.has_value() && !any_config->UnpackTo(&config)) {
    return absl::InvalidArgumentError("Failed to unpack config.");
  }
  auto planner = std::make_unique<LinearCartesianMotionPathPlanner>(config);
  return std::unique_ptr<PathPlanner>(std::move(planner));
}

REGISTER_PATH_PLANNER(LinearCartesianMotionPathPlanner,
                      "LinearCartesianMotionPathPlanner",
                      LinearCartesianMotionPathPlanner::Create);

}  // namespace intrinsic
