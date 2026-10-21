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

#include "intrinsic/motion_planning/motion_planner/motion_planner.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/geometry/api/distance_stats.h"
#include "intrinsic/kinematics/types/cartesian_limits.h"
#include "intrinsic/kinematics/types/joint_limits.h"
#include "intrinsic/kinematics/types/joint_limits_xd.h"
#include "intrinsic/kinematics/utils/compute_ik_util.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/motion_planning/motion_planner/collision_settings_utils.h"
#include "intrinsic/motion_planning/motion_planner/motion_planner_flags.h"
#include "intrinsic/motion_planning/motion_planner/motion_planner_interface.h"
#include "intrinsic/motion_planning/motion_planner/motion_planning_error_utils.h"
#include "intrinsic/motion_planning/motion_planner/motion_planning_limits_utils.h"
#include "intrinsic/motion_planning/motion_planner/motion_planning_path_segment_type_utils.h"
#include "intrinsic/motion_planning/motion_planner/motion_planning_utils.h"
#include "intrinsic/motion_planning/motion_planner/robot_specification.h"
#include "intrinsic/motion_planning/motion_planner/trajectory_generation_with_fallback.h"
#include "intrinsic/motion_planning/motion_planner/trajectory_parameterizer.h"
#include "intrinsic/motion_planning/motion_planner/trajectory_segment.h"
#include "intrinsic/motion_planning/path_planning/kinematics_system_proxy.h"
#include "intrinsic/motion_planning/path_planning/kinematics_system_proxy_util.h"
#include "intrinsic/motion_planning/path_planning/path_planner_definitions.h"
#include "intrinsic/motion_planning/path_planning/path_segment.h"
#include "intrinsic/motion_planning/path_planning/planners/pipeline_configs.pb.h"
#include "intrinsic/motion_planning/path_planning/planners/pipeline_path_planner.h"
#include "intrinsic/motion_planning/path_planning/planners/validation.h"
#include "intrinsic/motion_planning/proto/motion_specification_proto_utils.h"
#include "intrinsic/motion_planning/proto/v1/geometric_constraints.pb.h"
#include "intrinsic/motion_planning/proto/v1/motion_blending_parameter.pb.h"
#include "intrinsic/motion_planning/proto/v1/motion_planning_error.pb.h"
#include "intrinsic/motion_planning/proto/v1/motion_planning_limits.pb.h"
#include "intrinsic/motion_planning/proto/v1/motion_specification.pb.h"
#include "intrinsic/stats/scoped_span.h"
#include "intrinsic/util/status/status_builder.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/collision/util/collision_world_util.h"
#include "intrinsic/world/collision/util/make_rule_set.h"
#include "intrinsic/world/objects/kinematic_object_internal.h"
#include "intrinsic/world/objects/object_world.h"
#include "intrinsic/world/world.h"

namespace intrinsic {
namespace {

// Returns the collision checker configuration to use for a given motion
// request. If the `MotionPlannerConfiguration` explicitly specifies a collision
// checker config, it takes precedence. Otherwise, falls back to the default
// collision checker configuration.
intrinsic_proto::world::CollisionCheckerConfig GetCollisionCheckerConfig(
    const intrinsic_proto::motion_planning::v1::MotionPlannerConfiguration&
        motion_config) {
  if (motion_config.collision_checker_config().config_case() !=
      intrinsic_proto::world::CollisionCheckerConfig::CONFIG_NOT_SET) {
    return motion_config.collision_checker_config();
  }
  intrinsic_proto::world::CollisionCheckerConfig config;
  *config.mutable_coal() = intrinsic_proto::world::CoalCollisionCheckerConfig();
  return config;
}

using intrinsic_proto::motion_planning::v1::GeometricConstraint;
using intrinsic_proto::motion_planning::v1::MotionSegment;
using intrinsic_proto::motion_planning::v1::UniformGeometricConstraint;

constexpr double kLowLevelJointBlendingDefaultDesiredTightness = 0.01;
constexpr double kMinimumLowLevelJointBlendingDeviationRad = 1e-5;
constexpr double kMinBlendTightness = 1e-4;
constexpr double kDefaultTranslationalSamplingStepM = 0.002;
constexpr double kDefaultRotationalSamplingStepRad = 0.005;
constexpr double kCartMotionLowLevelJointBlendingDeviationRad = 0.2;
constexpr double kMinimumTranslationalRoundingM = 0.001;
constexpr double kMinimumRotationalRoundingRad = 0.01;
// This is the joint sampling distance for performing an approximately-uniform
// joint arc-length sampling of the path. The default joint sampling distance
// was chosen empirically. Please check with @bponton or @mdfiore before
// changing it.
constexpr double kDefaultJointSpaceUniformSamplingDistanceRad = 0.05;
absl::StatusOr<std::unique_ptr<const PointPath>> PlanSegmentPath(
    const PointPath& path,
    const intrinsic_proto::motion_planning::v1::MotionPlannerConfiguration&
        motion_config,
    const intrinsic_proto::motion_planning::v1::MotionSegment& motion_segment,
    const KinematicsSystemProxy& proxy,
    const PlannerConfigurationOptions& planner_options) {
  const stats::ScopedSpan span("MotionPlanner/PlanSegmentPath");
  // Get the path planning pipeline for this segment
  INTR_ASSIGN_OR_RETURN(
      const intrinsic::proto::PipelinePathPlannerConfig planner_pipeline,
      GetPathPlanningPipelineForMotionSegment(motion_config, motion_segment,
                                              planner_options));
  INTR_ASSIGN_OR_RETURN(std::unique_ptr<PipelinePathPlanner> planner,
                        PipelinePathPlanner::Create(planner_pipeline));
  INTR_ASSIGN_OR_RETURN(const std::vector<PointPath> planning_result,
                        planner->Plan(path, proxy, /*graph=*/nullptr));

  if (planning_result.empty() || planning_result.front().empty()) {
    return intrinsic::NotFoundErrorBuilder().LogError()
           << "Path planner returned no solution.";
  }
  return std::make_unique<PointPath>(planning_result.front());
}

// Composes a path segment out of the `sampled_waypoints`. It is expected that
// the first sampled waypoint corresponds to the start configuration and the
// last to the final target configuration. Each data point has the same
// `low_level_joint_blending_desired_tightness` in radians except for the first
// and last data point. A minimum blending deviation in radians is enforced to
// guarantee differential consistency of the path samples.
PathSegment CreatePathSegment(
    const PointPath& sampled_waypoints, const JointLimits& joint_limits,
    const CartesianLimits& cart_limits,
    const std::optional<std::multiset<intrinsic_proto::Rule>>&
        collision_rule_set,
    const double low_level_joint_blending_desired_tightness,
    const Pose3d& tip_t_tool = Pose3d::Identity()

    ,
    const absl::Span<const MotionEventAndJointConfigurationIndex>
        motion_events_and_joint_configuration_indices = {}

) {
  PathSegment segment;
  segment.joint_configurations.reserve(sampled_waypoints.size());
  segment.joint_blending_parameter_rad.reserve(sampled_waypoints.size());

  for (const auto& waypoint : sampled_waypoints) {
    segment.joint_configurations.push_back(waypoint);
    segment.joint_blending_parameter_rad.push_back(
        std::max(low_level_joint_blending_desired_tightness,
                 kMinimumLowLevelJointBlendingDeviationRad));
  }

  segment.joint_limits = joint_limits;
  segment.cartesian_limits = cart_limits;
  segment.collision_rule_set = collision_rule_set;
  segment.tip_t_target = tip_t_tool;

  segment.indexed_motion_events =
      std::vector<MotionEventAndJointConfigurationIndex>(
          motion_events_and_joint_configuration_indices.begin(),
          motion_events_and_joint_configuration_indices.end());

  segment.type = PathSegment::Type::kUndefined;

  return segment;
}
// Checks if global collision settings are specified. If not, searches the local
// path constraints settings if they define the same collision settings.
absl::StatusOr<intrinsic_proto::world::CollisionSettings>
GetGlobalCollisionSettings(
    const object_world::ObjectWorld& object_world,
    const object_world::KinematicObject& robot,
    const intrinsic_proto::motion_planning::v1::MotionSpecification&
        motion_specification) {
  // No global settings available. Check if all the segments use the same
  // settings. Return empty settings otherwise.
  std::optional<intrinsic_proto::world::CollisionSettings>
      global_collision_settings;
  for (const auto& segment : motion_specification.motion_segments()) {
    if (!segment.has_collision_settings()) {
      // Return empty settings.
      return intrinsic_proto::world::CollisionSettings();
    }
    if (!global_collision_settings.has_value()) {
      global_collision_settings = segment.collision_settings();
      continue;
    }
    INTR_ASSIGN_OR_RETURN(
        bool collision_matches,
        CollisionSettingsMatch(object_world, global_collision_settings.value(),
                               segment.collision_settings()));
    if (!collision_matches) {
      // Return empty settings.
      return intrinsic_proto::world::CollisionSettings();
    }
  }

  if (!global_collision_settings.has_value()) {
    return intrinsic_proto::world::CollisionSettings();
  }

  return *global_collision_settings;
}

// Checks if a path constraint contains a local collision checking constraint.
bool MotionSegmentContainsCollisionCheckingPathConstraint(
    const MotionSegment& motion_segment) {
  return motion_segment.has_collision_settings();
}

// Checks if a path constraint contains a uniform geometric constraint.
bool MotionSegmentContainsUniformGeometricPathConstraint(
    const MotionSegment& motion_segment) {
  return motion_segment.has_path_constraints();
}

bool MotionSegmentHasCollisionCheckingDisabled(
    const MotionSegment& motion_segment) {
  return motion_segment.has_collision_settings() &&
         motion_segment.collision_settings().disable_collision_checking();
}

// Extracts the intersection joint limits from the motion segment at
// motion_segment_index and the neighboring motion segment
// `motion_segment_index` + 1 (if it exist). If the position limit is not
// defined it will use the default limits for the trajectory segment.
absl::StatusOr<JointLimits> GetJointPositionLimitIntersection(
    const object_world::ObjectWorld& object_world,
    const object_world::KinematicObject& robot,
    const TrajectorySegment& trajectory_segment,
    const MotionSegment& motion_segment,
    std::optional<MotionSegment> next_motion_segment) {
  INTR_ASSIGN_OR_RETURN(const JointLimitsXd application_limits_xd,
                        robot.GetJointApplicationLimits());
  INTR_ASSIGN_OR_RETURN(const JointLimits application_limits,
                        ToJointLimits(application_limits_xd));
  JointLimits updated_robot_limits = trajectory_segment.joint_limits;

  // Set position limits from the current motion segment. If not set in the
  // segment, it remains the default segment joint limit.
  INTR_ASSIGN_OR_RETURN(
      updated_robot_limits,
      ParseJointLimitsFromProto(motion_segment, updated_robot_limits));
  // If there is a neighboring segment that contains limits, create intersection
  // between those and already extracted limits.
  if (next_motion_segment.has_value()) {
    INTR_ASSIGN_OR_RETURN(const JointLimits next_segment_limits,
                          ParseJointLimitsFromProto(next_motion_segment.value(),
                                                    updated_robot_limits));
    // Set intersection of limits for position.
    updated_robot_limits.max_position =
        updated_robot_limits.max_position.cwiseMin(
            next_segment_limits.max_position);
    updated_robot_limits.min_position =
        updated_robot_limits.min_position.cwiseMax(
            next_segment_limits.min_position);
  }
  return updated_robot_limits;
}

absl::StatusOr<std::vector<eigenmath::VectorXd>> ComputeIk(
    const object_world::ObjectWorld& object_world,
    const object_world::KinematicObject& robot,
    const GeometricConstraint& motion_target, KinematicsSystemProxy* proxy,
    const eigenmath::VectorXd& seed_configuration,
    const JointLimitsXd& joint_limits, int max_number_samples,
    bool disable_collision_checking, bool prefer_same_branch_ik = true) {
  const stats::ScopedSpan span("MotionPlanner/ComputeIk");
  absl::string_view kMotionPlanningErrorSuffix = "ComputeIk";
  std::vector<eigenmath::VectorXd> ik_solutions;
  if (!disable_collision_checking && proxy) {
    const JointLimitsXd default_joint_limits_proxy = proxy->GetJointLimits();
    INTR_RETURN_IF_ERROR(proxy->SetJointLimits(joint_limits));
    // Return an error that the target pose is not reachable
    // or solutions are in collisions. Not necessary to do
    // additional testing.
    INTR_ASSIGN_OR_RETURN(
        ik_solutions,
        GetCollisionFreeIkSolutions(
            object_world, *proxy, robot, motion_target,
            ComputeIkOptions{.seed_configuration = seed_configuration,
                             .joint_limits = joint_limits,
                             .max_num_solutions = max_number_samples,
                             .prefer_same_branch = prefer_same_branch_ik}));
    INTR_RETURN_IF_ERROR(proxy->SetJointLimits(default_joint_limits_proxy));
    if (ik_solutions.empty()) {
      absl::string_view error_message =
          "IK solver returned no solutions. The motion target is either "
          "outside the reachable workspace, overconstrained, or in collision.";
      return CreateStatusWithIKError(
          error_message, kMotionPlanningErrorSuffix,
          absl::StatusCode::kNotFound,
          intrinsic_proto::motion_planning::v1::ErrorContext::
              IK_NO_SOLUTIONS_FOUND_W_COLLISION_CHECKING,
          motion_target);
    }
  } else {
    INTR_ASSIGN_OR_RETURN(
        ik_solutions,
        intrinsic::ComputeIk(
            object_world, robot, motion_target,
            ComputeIkOptions{.seed_configuration = seed_configuration,
                             .joint_limits = joint_limits,
                             .max_num_solutions = max_number_samples,
                             .prefer_same_branch = prefer_same_branch_ik}));
    if (ik_solutions.empty()) {
      absl::string_view error_message =
          "IK solver returned no solutions. The motion target is either "
          "outside the reachable workspace or overconstrained.";
      return CreateStatusWithIKError(error_message, kMotionPlanningErrorSuffix,
                                     absl::StatusCode::kNotFound,
                                     intrinsic_proto::motion_planning::v1::
                                         ErrorContext::IK_NO_SOLUTIONS_FOUND,
                                     motion_target);
    }
  }
  return ik_solutions;
}

// Updates the target constraints to include the current segment's and
// the next segment's path constraints if they exist.
absl::StatusOr<GeometricConstraint> UpdateTargetConstraintsSegmentWise(
    const object_world::ObjectWorld& object_world,
    const object_world::KinematicObject& robot,
    const MotionSegment& motion_segment,
    std::optional<MotionSegment> next_motion_segment) {
  std::vector<GeometricConstraint> constraints_for_target;
  // Check if the motion segment has a uniform geometric path constraint
  if (MotionSegmentContainsUniformGeometricPathConstraint(motion_segment)) {
    const UniformGeometricConstraint segment_path_constraints_proto =
        motion_segment.path_constraints();
    INTR_ASSIGN_OR_RETURN(
        constraints_for_target,
        GetGeometricConstraintFrom(segment_path_constraints_proto));
  }
  if (next_motion_segment.has_value() &&
      MotionSegmentContainsUniformGeometricPathConstraint(
          next_motion_segment.value())) {
    UniformGeometricConstraint next_segment_path_constraints_proto =
        next_motion_segment.value().path_constraints();
    INTR_ASSIGN_OR_RETURN(
        std::vector<GeometricConstraint> geometric_constraints_proto,
        GetGeometricConstraintFrom(next_segment_path_constraints_proto));
    for (const GeometricConstraint& geometric_constraint_proto :
         geometric_constraints_proto) {
      constraints_for_target.push_back(geometric_constraint_proto);
    }
  }
  GeometricConstraint updated_target_constraints = motion_segment.target();
  if (!constraints_for_target.empty()) {
    // Add the target constraints
    constraints_for_target.push_back(motion_segment.target());
    INTR_ASSIGN_OR_RETURN(
        updated_target_constraints,
        UpdateTargetConstraints(object_world, robot, constraints_for_target));
  }
  return updated_target_constraints;
}

absl::StatusOr<std::optional<MotionSegment>> GetNextMotionSegment(
    const std::vector<TrajectorySegment>& trajectory_segments,
    int trajectory_segment_index, int motion_segment_index) {
  if (trajectory_segment_index < 0 ||
      trajectory_segment_index >= trajectory_segments.size()) {
    return absl::InternalError(
        "GetNextMotionSegment received an invalid trajectory segment index.");
  }
  if (motion_segment_index < 0 ||
      motion_segment_index >= trajectory_segments[trajectory_segment_index]
                                  .motion_segments.size()) {
    return absl::InternalError(
        "GetNextMotionSegment received an invalid motion segment index.");
  }
  int next_motion_segment_index = motion_segment_index + 1;
  if (next_motion_segment_index <
      trajectory_segments[trajectory_segment_index].motion_segments.size()) {
    return trajectory_segments[trajectory_segment_index]
        .motion_segments[next_motion_segment_index];
  }
  int next_trajectory_segment_index = trajectory_segment_index + 1;
  if ((next_trajectory_segment_index < trajectory_segments.size()) &&
      !trajectory_segments[next_trajectory_segment_index]
           .motion_segments.empty()) {
    return trajectory_segments[next_trajectory_segment_index]
        .motion_segments.front();
  }

  return std::nullopt;
}

absl::Status SampleJointTargets(
    const object_world::ObjectWorld& object_world,
    const object_world::KinematicObject& robot,
    const intrinsic_proto::motion_planning::v1::MotionPlannerConfiguration&
        motion_config,
    KinematicsSystemProxy* proxy,
    const eigenmath::VectorXd& start_configuration,
    std::vector<TrajectorySegment>& trajectory_segments,
    int max_number_samples) {
  const stats::ScopedSpan span("MotionPlanner/SampleJointTargets");
  if (!proxy) {
    return absl::InternalError(
        "No kinematic proxy was defined. Kinematic proxy required to evaluate "
        "validity of configurations.");
  }
  int global_motion_segment_index = 0;

  // For each segment after the first one, we need to set the robot and object
  // world to the end configuration of the previous segment.
  // As of 05/14/2024, creating an object world view takes about 2ms for a
  // typical application with 20 objects.
  // Since we only make an object world view once here, this overhead time is
  // acceptable.
  // We should avoid making object world views repeatedly for each motion
  // segment.
  eigenmath::VectorXd start_robot_configuration_of_segment =
      start_configuration;
  World mutable_world = object_world.GetEntityWorld().Clone();
  INTR_ASSIGN_OR_RETURN(
      std::unique_ptr<object_world::ObjectWorld> mutable_object_world,
      object_world::ObjectWorld::CreateView(mutable_world));
  INTR_ASSIGN_OR_RETURN(
      object_world::KinematicObject * mutable_robot,
      mutable_object_world->GetKinematicObject(robot.GetName()));

  for (int trajectory_segment_index = 0;
       trajectory_segment_index < trajectory_segments.size();
       ++trajectory_segment_index) {
    TrajectorySegment& trajectory_segment =
        trajectory_segments[trajectory_segment_index];

    std::vector<std::vector<eigenmath::VectorXd>> trajectory_samples;
    trajectory_samples.reserve(trajectory_segment.motion_segments.size());
    trajectory_segment.joint_samples.clear();

    // Check if we require a local proxy and use that instead. Note that the
    // trajectory segment only includes multiple motion segments if and only if
    // they have the same properties, i.e., with respect to collision settings.
    // Therefore the proxy is only generated once for each trajectory segment.
    KinematicsSystemProxy* active_proxy = proxy;
    std::unique_ptr<KinematicsSystemProxy> local_proxy_ptr;
    if (!trajectory_segment.motion_segments.empty() &&
        MotionSegmentContainsCollisionCheckingPathConstraint(
            trajectory_segment.motion_segments.front())) {
      // TODO(b/456828157): If concurrent collision checking is enabled, we
      // should create a concurrent proxy here and change ComputeIk() to use
      // KinematicsSystemProxy::AreAllValid().
      INTR_ASSIGN_OR_RETURN(local_proxy_ptr,
                            GetKinematicSystemsProxyForSegment(
                                *mutable_object_world, *mutable_robot,
                                GetCollisionCheckerConfig(motion_config),
                                trajectory_segment.motion_segments.front()));
      active_proxy = local_proxy_ptr.get();
    }

    for (int index = 0; index < trajectory_segment.motion_segments.size();
         ++index, ++global_motion_segment_index) {
      const MotionSegment& motion_segment =
          trajectory_segment.motion_segments[index];
      INTR_ASSIGN_OR_RETURN(
          std::optional<MotionSegment> next_motion_segment,
          GetNextMotionSegment(trajectory_segments, trajectory_segment_index,
                               index));
      if (!motion_segment.has_target()) {
        return absl::InvalidArgumentError(
            "All motion segments need to specify a target.");
      }

      // Get joint limits for motion target that are comprised of the limits of
      // the segment and the next segments. Limits defined for the target itself
      // will be set in the ComputeIk itself.
      INTR_ASSIGN_OR_RETURN(
          const JointLimits path_joint_limits,
          GetJointPositionLimitIntersection(
              *mutable_object_world, *mutable_robot, trajectory_segment,
              motion_segment, next_motion_segment));

      INTR_ASSIGN_OR_RETURN(
          const GeometricConstraint& resolved_target_constraints,
          UpdateTargetConstraintsSegmentWise(*mutable_object_world,
                                             *mutable_robot, motion_segment,
                                             next_motion_segment));

      // TODO(b/302582045) : Update the segment number in error messages to
      // start index at 1.
      std::vector<int> motion_segments_to_report;
      motion_segments_to_report.push_back(global_motion_segment_index);
      LOG(INFO) << "start_robot_configuration_of_segment: "
                << start_robot_configuration_of_segment.transpose();
      INTR_ASSIGN_OR_RETURN(
          std::vector<eigenmath::VectorXd> ik_solutions,
          ComputeIk(*mutable_object_world, *mutable_robot,
                    resolved_target_constraints, active_proxy,
                    /*seed_configuration=*/start_robot_configuration_of_segment,
                    JointLimitsXd::Create(path_joint_limits),
                    max_number_samples,
                    MotionSegmentHasCollisionCheckingDisabled(motion_segment),
                    /*prefer_same_branch_ik=*/true),
          _.With([motion_segments_to_report,
                  global_motion_segment_index](absl::Status status) {
            return UpdateStatusWithPipelineError(
                status, motion_segments_to_report,
                absl::StrFormat(
                    "\n Cannot resolve motion target for segment %d",
                    global_motion_segment_index));
          }));
      trajectory_samples.emplace_back(ik_solutions);
      // Update the start configuration of the next segment to be the first
      // solution of the current segment.
      start_robot_configuration_of_segment = ik_solutions.front();
      INTR_RETURN_IF_ERROR(mutable_robot->SetJointPositions(
          start_robot_configuration_of_segment));
    }
    trajectory_segment.joint_samples = trajectory_samples;
  }

  return absl::OkStatus();
}


absl::StatusOr<std::vector<MotionEventAndJointConfigurationIndex>>
ConvertMotionSegmentEventsToPathMotionEvents(
    absl::Span<const ::intrinsic_proto::motion_planning::v1::MotionSegmentEvent>
        motion_segment_events,
    const PointPath& path) {
  if (path.size() < 2) {
    return absl::InternalError("Path must contain at least two points.");
  }
  std::vector<MotionEventAndJointConfigurationIndex>
      motion_events_and_joint_configuration_indices;
  for (const ::intrinsic_proto::motion_planning::v1::MotionSegmentEvent&
           motion_segment_event : motion_segment_events) {
    ::intrinsic_proto::motion_planning::v1::MotionEvent motion_event;
    // Copy over the event and offset which remain unchanged.
    *motion_event.mutable_event() = motion_segment_event.event();
    *motion_event.mutable_keypoint()->mutable_event_offset() =
        motion_segment_event.event_location().event_offset();

    switch (motion_segment_event.event_location().relative_to()) {
      case intrinsic_proto::motion_planning::v1::MotionEventSegmentLocation::
          SEGMENT_TARGET: {
        const int joint_configuration_index = path.size() - 1;
        motion_events_and_joint_configuration_indices.push_back(
            {.motion_event = motion_event,
             .joint_configuration_index = joint_configuration_index});
        break;
      }
      case intrinsic_proto::motion_planning::v1::MotionEventSegmentLocation::
          SEGMENT_START: {
        motion_events_and_joint_configuration_indices.push_back(
            {.motion_event = motion_event, .joint_configuration_index = 0});
        break;
      }
      default: {
        return absl::InvalidArgumentError(
            "Either SEGMENT_TARGET or SEGMENT_START must be specified for the "
            "MotionEventSegmentLocation relative_to field.");
      }
    }
  }
  return motion_events_and_joint_configuration_indices;
}


absl::StatusOr<std::vector<PathSegment>> PlanPathImpl(
    const object_world::ObjectWorld& object_world,
    const object_world::KinematicObject& robot,
    std::unique_ptr<KinematicsSystemProxy> global_proxy,
    const eigenmath::VectorXd& start_configuration,
    const intrinsic_proto::motion_planning::v1::MotionPlannerConfiguration&
        motion_config,
    absl::Span<const TrajectorySegment> trajectory_segments,
    const MotionPlannerFlags& flags,
    absl::flat_hash_map<std::string, std::vector<int>>&
        path_segment_to_validation_proxies_id_map,
    std::vector<ProxyCreateInfo>& proxy_set,
    DistanceCheckStatistics* distance_check_statistics) {
  const stats::ScopedSpan span("MotionPlanner/PlanPathImpl");
  if (!global_proxy) {
    return absl::InternalError(
        "No kinematic proxy was defined. Kinematic proxy required to evaluate "
        "validity of robot path.");
  }
  const JointLimitsXd original_proxy_limits = global_proxy->GetJointLimits();

  INTR_ASSIGN_OR_RETURN(const JointLimitsXd application_limits_xd,
                        robot.GetJointApplicationLimits());
  INTR_ASSIGN_OR_RETURN(const JointLimits application_limits,
                        ToJointLimits(application_limits_xd));

  // The proxy set of the trajectory segment is composed by the global proxy
  // and the possible local proxies from each motion segment. We store the
  // global proxy at the first position of the proxy set. Local proxies will
  // be stored from the second position on.
  //
  // global_proxy's ProxyCreateInfo was already added by the caller of
  // PlanPathImpl.
  if (proxy_set.size() != 1) {
    return absl::FailedPreconditionError(
        "proxy_set should have exactly one proxy in it (the global proxy)");
  }
  KinematicsSystemProxy* global_proxy_ptr = global_proxy.get();
  path_segment_to_validation_proxies_id_map.clear();

  std::vector<PathSegment> path_segments;
  eigenmath::VectorXd last_segment_config = start_configuration;
  int motion_segment_number = 0;
  for (int traj_seg_idx = 0; traj_seg_idx < trajectory_segments.size();
       ++traj_seg_idx) {
    VLOG(1) << "Planning trajectory segment " << traj_seg_idx << " of "
            << trajectory_segments.size();
    const TrajectorySegment& trajectory_segment =
        trajectory_segments[traj_seg_idx];
    // Check basic assumption that we have a sample for every motion segment.
    if (trajectory_segment.joint_samples.size() !=
        trajectory_segment.motion_segments.size()) {
      return absl::InternalError(
          "Size of motion segments not identical to sampled joint "
          "configs.");
    }

    double low_level_joint_blending_desired_tightness =
        (trajectory_segment.trajectory_type ==
         TrajectorySegment::Type::kBlendedCartesian)
            ? kCartMotionLowLevelJointBlendingDeviationRad
            : kLowLevelJointBlendingDefaultDesiredTightness;
    if (trajectory_segment.blending_parameters.has_value() &&
        trajectory_segment.blending_parameters->has_joint_blending() &&
        trajectory_segment.blending_parameters->joint_blending()
            .has_desired_tightness_rad()) {
      low_level_joint_blending_desired_tightness =
          trajectory_segment.blending_parameters->joint_blending()
              .desired_tightness_rad();
    }

    INTR_ASSIGN_OR_RETURN(const double collision_check_spacing,
                          GetCollisionCheckSpacing(flags, motion_config));
    const PlannerConfigurationOptions planner_options{
        .cart_translation_rounding_m =
            GetCartesianTranslationalRoundingFromProto(
                trajectory_segment.blending_parameters,
                /*default_translational_rounding=*/
                kMinimumTranslationalRoundingM,
                /*minimum_translational_rounding=*/
                kMinimumTranslationalRoundingM),
        .cart_rotation_rounding_rad = GetCartesianRotationalRoundingFromProto(
            trajectory_segment.blending_parameters,
            /*default_rotational_rounding=*/kMinimumRotationalRoundingRad,
            /*minimum_rotational_rounding=*/kMinimumRotationalRoundingRad),
        .cart_translation_sampling_distance_m =
            kDefaultTranslationalSamplingStepM,
        .cart_rotation_sampling_distance_rad =
            kDefaultRotationalSamplingStepRad,
        .joint_sampling_distance_rad =
            kDefaultJointSpaceUniformSamplingDistanceRad,
        .collision_check_spacing_rad = collision_check_spacing,
        .tip_t_tool = trajectory_segment.tip_t_moving_frame,
    };

    // The input path consists of the vector of all joint motion targets within
    // this trajectory segment. The assumption is that all motion segments
    // within the trajectory segment have the same properties with respect to
    // joint limits, cartesian limits, path constraints, and collision checking.
    PointPath input_path;
    input_path.reserve(trajectory_segment.joint_samples.size());
    input_path.push_back(last_segment_config);
    for (int i = 0; i < trajectory_segment.joint_samples.size(); ++i) {
      // Check that a joint configuration was sampled for the target. That
      // should always be the case.
      if (trajectory_segment.joint_samples[i].empty()) {
        return absl::InternalError(
            "Motion segment contains empty joint configuration goal. "
            "Could not generate path.");
      }

      // TODO(b/266781359): Enable iteration over multiple samples.
      if (trajectory_segment.joint_samples[i].size() > 1) {
        LOG(WARNING) << "Motion segment contains multiple motion target joint "
                        "configuration samples. Currently only single joint "
                        "configurations are considered.";
      }

      // Ignore motion segments that do not require any motion. Most
      // likely reason for having such a waypoint is that we are already at
      // the desired location or that frames are specified at the same place.
      if (input_path.empty()) {
        LOG(ERROR)
            << "Path is empty but should at least contain the start "
               "configuration. Error occurred during planning for motion "
               "segment "
            << i + motion_segment_number << ".";
        return absl::InternalError(
            "We could not generate a motion due to an internal error. Please "
            "file a bug and include the motion planning id.");
      }
      const double segment_length =
          (trajectory_segment.joint_samples[i].front() - input_path.back())
              .norm();
      if (segment_length < kMinBlendTightness) {
        LOG(INFO) << "Skipping motion segment " << i + motion_segment_number
                  << " because no motion is required (length: "
                  << segment_length << ").";
        continue;
      }

      input_path.push_back(trajectory_segment.joint_samples[i].front());
    }

    // Check that at least one motion segment is in the trajectory segment
    if (input_path.size() < 2) {
      LOG(INFO) << "Skipping entire trajectory segment, because it does not "
                   "require any motion.";
      continue;
    }

    // This means we have at least one motion segment in the trajectory segment
    // as well as a valid input path. Continue on to path planning and setup the
    // planner based on the properties of one of the motion segment.

    // Update application limits with user input for the section.
    INTR_ASSIGN_OR_RETURN(
        JointLimits segment_path_limits,
        ParseJointLimitsFromProto(trajectory_segment.motion_segments.front(),
                                  application_limits));

    // We override the global validator stored in the first position of the
    // trajectory segment proxy set in case the motion segment explicitly
    // specifies collision settings or in case uniform path constraints are
    // defined.
    const bool use_local_validator =
        MotionSegmentContainsCollisionCheckingPathConstraint(
            trajectory_segment.motion_segments.front()) ||
        MotionSegmentContainsUniformGeometricPathConstraint(
            trajectory_segment.motion_segments.front());

    // By default we use the global proxy.
    KinematicsSystemProxy* active_proxy = global_proxy_ptr;
    int active_proxy_id = 0;
    std::unique_ptr<KinematicsSystemProxy> local_proxy_ptr;
    if (use_local_validator) {
      // TODO(b/467758757): We should be collecting DistanceCheckStatistics
      // here.
      const std::optional<int> maybe_concurrent_thread_count =
          GetConcurrentThreadCount(flags, distance_check_statistics);
      {
        INTR_ASSIGN_OR_RETURN(
            ProxyCreateInfo create_info,
            GetKinematicSystemsProxyCreateInfoForSegment(
                object_world, trajectory_segment.motion_segments.front()));
        INTR_ASSIGN_OR_RETURN(
            local_proxy_ptr,
            CreateKinematicsProxy(
                object_world, robot,
                KinematicsProxyOptions{
                    .collision_checker_config =
                        GetCollisionCheckerConfig(motion_config),
                    .constraints_proto = create_info.constraints_proto,
                    .rule_set = create_info.rule_set,
                    .disable_collision_checking =
                        create_info.disable_collision_checking,
                    .maybe_concurrent_thread_count =
                        maybe_concurrent_thread_count,
                }));

        proxy_set.push_back(std::move(create_info));
      }
      active_proxy_id = proxy_set.size() - 1;
      active_proxy = local_proxy_ptr.get();
    }
    // Set the distance check pointer if available to aggregate statistics over
    // global and local variables.
    if (distance_check_statistics != nullptr) {
      active_proxy->SetDistanceCheckStatistics(distance_check_statistics);
    }

    std::string motion_segment_failure_message;
    std::vector<int> motion_segment_numbers_to_report;
    if (trajectory_segment.motion_segments.size() > 1) {
      motion_segment_failure_message = absl::StrFormat(
          "Error occurred during planning of segments %d to %d.",
          motion_segment_number,
          motion_segment_number + trajectory_segment.motion_segments.size() -
              1);
      motion_segment_numbers_to_report.push_back(motion_segment_number);
      motion_segment_numbers_to_report.push_back(
          motion_segment_number + trajectory_segment.motion_segments.size() -
          1);
    } else {
      motion_segment_failure_message =
          absl::StrFormat("Error occurred during planning in segment %d.",
                          motion_segment_number);
      motion_segment_numbers_to_report.push_back(motion_segment_number);
    }
    // Handle the cases in which collision checking was either not disabled or
    // we have uniform geometric constraints.
    INTR_RETURN_IF_ERROR(active_proxy->SetJointLimits(
        JointLimitsXd::Create(segment_path_limits)));
    bool global_proxy_joint_limits_updated =
        (active_proxy == global_proxy_ptr) ? true : false;

    // Get the path planning pipeline for this segment
    INTR_ASSIGN_OR_RETURN(
        std::unique_ptr<const PointPath> planning_result,
        PlanSegmentPath(input_path, motion_config,
                        trajectory_segment.motion_segments.front(),
                        *active_proxy, planner_options),
        _.With([motion_segment_numbers_to_report,
                motion_segment_failure_message](absl::Status status) {
          return UpdateStatusWithPipelineError(status,
                                               motion_segment_numbers_to_report,
                                               motion_segment_failure_message);
        }));


    std::vector<::intrinsic_proto::motion_planning::v1::MotionSegmentEvent>
        motion_segment_events;
    for (const auto& motion_segment : trajectory_segment.motion_segments) {
      motion_segment_events.insert(
          motion_segment_events.end(),
          motion_segment.motion_segment_events().begin(),
          motion_segment.motion_segment_events().end());
    }
    INTR_ASSIGN_OR_RETURN(std::vector<MotionEventAndJointConfigurationIndex>
                              motion_events_and_joint_configuration_indices,
                          ConvertMotionSegmentEventsToPathMotionEvents(
                              motion_segment_events, *planning_result));


    std::optional<std::multiset<intrinsic_proto::Rule>> collision_rule_set =
        std::nullopt;
    if (active_proxy->GetCollisionChecker() != nullptr) {
      collision_rule_set = active_proxy->GetCollisionChecker()->GetRuleSet();
    }

    // TODO(b/314349803): Enable setting the correct tip_t_moving_frame offset
    // for the segments.
    path_segments.push_back(CreatePathSegment(
        *planning_result, segment_path_limits, trajectory_segment.cart_limits,
        collision_rule_set, low_level_joint_blending_desired_tightness,
        trajectory_segment.tip_t_moving_frame

        ,
        motion_events_and_joint_configuration_indices

        ));

    path_segment_to_validation_proxies_id_map[path_segments.back()
                                                  .GetUniqueId()]
        .push_back(active_proxy_id);

    last_segment_config = planning_result->back();
    if (global_proxy_joint_limits_updated) {
      // Restore the original joint limits for the global proxy. This is
      // necessary to ensure that the global proxy is not modified when used by
      // the next motion segments.
      INTR_RETURN_IF_ERROR(
          global_proxy_ptr->SetJointLimits(original_proxy_limits));
    }
    motion_segment_number += trajectory_segment.motion_segments.size();
  }

  if (path_segments.empty()) {
    path_segments.push_back(
        CreatePathSegment({start_configuration, start_configuration},
                          application_limits, CartesianLimits::Unlimited(),
                          /*collision_rule_set=*/std::nullopt,
                          kLowLevelJointBlendingDefaultDesiredTightness));
  }

  path_segments.front().joint_blending_parameter_rad.front() =
      kMinimumLowLevelJointBlendingDeviationRad;
  path_segments.back().joint_blending_parameter_rad.back() =
      kMinimumLowLevelJointBlendingDeviationRad;

  return path_segments;
}

// Plans a path which consists of a sequence of path segments: It creates the
// kinematics proxy, samples joint targets, and plans the path. Returns an error
// if any of the steps previously mentioned fail.
absl::StatusOr<std::vector<PathSegment>> SampleMotionTargetsAndPlanPath(
    const object_world::ObjectWorld& object_world,
    const RobotSpecification& robot_with_limits,
    std::optional<intrinsic_proto::RuleSet> collision_rule_set,
    const intrinsic_proto::motion_planning::v1::MotionPlannerConfiguration&
        motion_config,
    const intrinsic_proto::motion_planning::v1::MotionSpecification&
        motion_specification,
    const MotionPlannerFlags& flags,
    DistanceCheckStatistics* distance_check_statistics,
    std::vector<ProxyCreateInfo>& proxy_set,
    absl::flat_hash_map<std::string, std::vector<int>>&
        path_segment_to_validation_proxies_id_map) {
  const stats::ScopedSpan span("MotionPlanner/SampleMotionTargetsAndPlanPath");

  // Group the motion specs together into trajectory segments and sanity check
  // the input. This is specific to the current capabilities of the underlying
  // trajectory generator. One trajectory segment represents the set of motion
  // segments that are compatible as input for the trajectory generator.
  INTR_ASSIGN_OR_RETURN(
      std::vector<TrajectorySegment> trajectory_segments,
      ParseTrajectorySegmentsFromMotionSpecification(
          object_world, robot_with_limits, motion_specification));

  if (trajectory_segments.empty()) {
    return intrinsic::InternalErrorBuilder()
           << "No number of trajectory segments could be generated from the "
              "motion specifications despite containing at least one motion "
              "segment.";
  }

  if (trajectory_segments.size() >
      motion_specification.motion_segments_size()) {
    return intrinsic::InternalErrorBuilder()
           << "More trajectory segments than motion segments were generated.";
  }

  // TODO(b/266781359): Allow multiple samples to improve success rate
  // of multi-step planning.
  constexpr int kNumberSamples = 1;

  const object_world::KinematicObject& robot = *robot_with_limits.robot;
  const eigenmath::VectorXd& start_configuration =
      robot_with_limits.start_configuration;

  std::unique_ptr<KinematicsSystemProxy> global_proxy;
  {
    ProxyCreateInfo global_proxy_create_info{
        .constraints_proto = std::nullopt,
        .rule_set = collision_rule_set.value_or(intrinsic_proto::RuleSet()),
        .disable_collision_checking = !collision_rule_set.has_value()};
    const std::optional<int> maybe_concurrent_thread_count =
        GetConcurrentThreadCount(flags, distance_check_statistics);
    INTR_ASSIGN_OR_RETURN(
        global_proxy,
        CreateKinematicsProxy(
            object_world, robot,
            KinematicsProxyOptions{
                .collision_checker_config =
                    GetCollisionCheckerConfig(motion_config),
                .constraints_proto = global_proxy_create_info.constraints_proto,
                .rule_set = global_proxy_create_info.rule_set,
                .disable_collision_checking =
                    global_proxy_create_info.disable_collision_checking,
                .maybe_concurrent_thread_count = maybe_concurrent_thread_count,
            }));

    proxy_set.push_back(std::move(global_proxy_create_info));
  }

  LOG(INFO) << "Sampling joint configuration for motion targets.";
  INTR_RETURN_IF_ERROR(
      SampleJointTargets(object_world, robot, motion_config, global_proxy.get(),
                         start_configuration, trajectory_segments,
                         /*max_number_samples=*/kNumberSamples));

  LOG(INFO) << "Planning path for motion request.";
  return PlanPathImpl(object_world, robot, std::move(global_proxy),
                      start_configuration, motion_config, trajectory_segments,
                      flags, path_segment_to_validation_proxies_id_map,
                      proxy_set, distance_check_statistics);
}
// Plans a path and generates a trajectory from the path.
absl::StatusOr<MotionPlanner::PlanTrajectoryResult> PlanPathInternal(
    const object_world::ObjectWorld& object_world,
    const RobotSpecification& robot_with_limits,
    const intrinsic_proto::motion_planning::v1::MotionPlannerConfiguration&
        motion_config,
    const intrinsic_proto::motion_planning::v1::MotionSpecification&
        motion_specification,
    const MotionPlannerFlags& flags,
    std::optional<RunTimeMotionPlannerFlags> run_time_flags,
    std::vector<ProxyCreateInfo>& proxy_set,
    absl::flat_hash_map<std::string, std::vector<int>>&
        path_segment_to_validation_proxies_id_map) {
  const stats::ScopedSpan span("MotionPlanner/PlanPathInternal");

  // Preparation: Extract collision settings from the world that will be used as
  // a default if nothing is specified in the motion specification.
  INTR_ASSIGN_OR_RETURN(
      const auto& global_collision_settings,
      GetGlobalCollisionSettings(object_world, *robot_with_limits.robot,
                                 motion_specification));

  // Convert collision settings to a rule set which are ultimately used to
  // create the proxy.
  std::optional<intrinsic_proto::RuleSet> collision_rule_set = std::nullopt;
  if (!global_collision_settings.disable_collision_checking()) {
    INTR_ASSIGN_OR_RETURN(collision_rule_set,
                          MakeRuleSet(global_collision_settings, object_world),
                          _.LogError());
  }

  // Sample the joint configurations for each motion segment and plan the path.
  MotionPlanner::PlanTrajectoryResult planning_result;

  DistanceCheckStatistics distance_check_statistics;
  DistanceCheckStatistics* distance_check_statistics_ptr = nullptr;
  bool runtime_enabled_distance_check_statistics =
      run_time_flags.has_value()
          ? run_time_flags->enable_distance_check_statistics
          : false;
  if (runtime_enabled_distance_check_statistics ||
      flags.enable_distance_check_statistics) {
    distance_check_statistics_ptr = &distance_check_statistics;
  }
  const absl::Time planning_start_time = absl::Now();
    INTR_ASSIGN_OR_RETURN(
        planning_result.path_segments,
        SampleMotionTargetsAndPlanPath(
            object_world, robot_with_limits, collision_rule_set, motion_config,
            motion_specification, flags, distance_check_statistics_ptr,
            proxy_set, path_segment_to_validation_proxies_id_map));
  planning_result.motion_planning_statistics.path_planning_statistics
      .path_planning_duration = absl::Now() - planning_start_time;
  const int num_motion_segments = motion_specification.motion_segments_size();
  planning_result.motion_planning_statistics.path_planning_statistics
      .num_motion_segments = num_motion_segments;
  planning_result.motion_planning_statistics.num_motion_segments =
      num_motion_segments;
  LOG(INFO) << "Path planning took "
            << absl::ToDoubleSeconds(
                   planning_result.motion_planning_statistics
                       .path_planning_statistics.path_planning_duration)
            << " s.";
  // TODO (b/473667418): Refactor relationship between `PlanTrajectoryResult`
  // and `DistanceCheckStatistics
  if (distance_check_statistics_ptr != nullptr) {
    planning_result.distance_check_statistics = *distance_check_statistics_ptr;
  }
  return planning_result;
}

}  // namespace

MotionPlanner::MotionPlanner(
    const MotionPlannerFlags& flags,
    std::unique_ptr<TrajectoryParameterizer> trajectory_parameterizer)
    : flags_(flags),
      trajectory_parameterizer_(std::move(trajectory_parameterizer)) {}

absl::StatusOr<MotionPlanner::PlanTrajectoryResult>
MotionPlanner::PlanTrajectory(
    const object_world::ObjectWorld& world,
    const intrinsic_proto::motion_planning::v1::RobotSpecification&
        robot_specification,
    const intrinsic_proto::motion_planning::v1::MotionSpecification&
        motion_specification,
    const intrinsic_proto::motion_planning::v1::MotionPlannerConfiguration&
        motion_config,
    std::optional<RunTimeMotionPlannerFlags> run_time_flags) const {
  const stats::ScopedSpan span("MotionPlanner/PlanTrajectory");
  const absl::Time motion_planning_start_time = absl::Now();
  if (motion_specification.motion_segments_size() < 1) {
    return absl::InvalidArgumentError(
        "Motion Specification does not contain any motion segments. Require at "
        "least one motion segment for motion planning.");
  }
  // Unpack the robot information.
  INTR_ASSIGN_OR_RETURN(const RobotSpecification robot_with_limits,
                        RobotSpecification::Create(world, robot_specification));

  // The set of all specified proxies for each segment.
  std::vector<ProxyCreateInfo> proxy_set;
  // Required to avoid the time overhead with creating the same proxy over and
  // over.
  absl::flat_hash_map<std::string, std::vector<int>>
      path_segment_to_validation_proxies_id_map;

  // Plan the path of the trajectory.
  INTR_ASSIGN_OR_RETURN(
      MotionPlanner::PlanTrajectoryResult planning_result,
      PlanPathInternal(world, robot_with_limits, motion_config,
                       motion_specification, flags_, run_time_flags, proxy_set,
                       path_segment_to_validation_proxies_id_map));

  LOG(INFO) << "Planning trajectory for path.";

  const object_world::KinematicObject& robot = *robot_with_limits.robot;
  const bool force_strict_fallback_for_testing =
      run_time_flags.has_value()
          ? run_time_flags->force_strict_fallback_for_testing
          : false;
  INTR_ASSIGN_OR_RETURN(GenerateTrajectoryResult gen_traj_result,
                        GenerateTrajectoryWithFallback(
                            world, robot, motion_config,
                            absl::MakeSpan(planning_result.path_segments),
                            *trajectory_parameterizer_, flags_, proxy_set,
                            path_segment_to_validation_proxies_id_map,
                            force_strict_fallback_for_testing));

  planning_result.trajectory =
      std::move(gen_traj_result.trajectory_with_samples.topp_trajectory_result
                    .trajectory);
  planning_result.path_samples =
      std::move(*gen_traj_result.trajectory_with_samples.path_samples);

  planning_result.motion_events =
      std::move(gen_traj_result.trajectory_with_samples.topp_trajectory_result
                    .motion_events);


  planning_result.fallback_stage = gen_traj_result.fallback_stage;

  LOG(INFO) << "Planned trajectory with duration "
            << absl::ToDoubleSeconds(planning_result.trajectory.Duration())
            << " s";
  if (gen_traj_result.non_fallback_trajectory.has_value()) {
    LOG(INFO) << "Pre-fallback trajectory had duration "
              << absl::ToDoubleSeconds(
                     gen_traj_result.non_fallback_trajectory.value()
                         .topp_trajectory_result.trajectory.Duration())
              << " s";
  }
  planning_result.motion_planning_statistics.trajectory_generation_statistics
      .path_refinement_and_trajectory_generation_duration =
      gen_traj_result.trajectory_generation_duration +
      gen_traj_result.validation_proxy_creation_duration +
      gen_traj_result.validation_duration;
  LOG(INFO) << "Path refinement and trajectory generation took "
            << absl::ToDoubleSeconds(
                   planning_result.motion_planning_statistics
                       .trajectory_generation_statistics
                       .path_refinement_and_trajectory_generation_duration)
            << " s";
  VLOG(1) << "Path refinement proxy creation took "
          << absl::ToDoubleSeconds(
                 gen_traj_result.validation_proxy_creation_duration)
          << " s";
  planning_result.motion_planning_statistics.trajectory_generation_statistics
      .path_refinement_validation_duration =
      gen_traj_result.validation_duration +
      gen_traj_result.validation_proxy_creation_duration;
  LOG(INFO) << "Path refinement validation took "
            << absl::ToDoubleSeconds(planning_result.motion_planning_statistics
                                         .trajectory_generation_statistics
                                         .path_refinement_validation_duration)
            << " s";
  planning_result.motion_planning_statistics.motion_planning_duration =
      absl::Now() - motion_planning_start_time;
  LOG(INFO) << "Motion planning took "
            << absl::ToDoubleSeconds(planning_result.motion_planning_statistics
                                         .motion_planning_duration)
            << " s";

  return planning_result;
}

absl::StatusOr<MotionPlanner::PlanPathResult> MotionPlanner::PlanPath(
    const object_world::ObjectWorld& world,
    const intrinsic_proto::motion_planning::v1::RobotSpecification&
        robot_specification,
    const intrinsic_proto::motion_planning::v1::MotionSpecification&
        motion_specification,
    const intrinsic_proto::motion_planning::v1::MotionPlannerConfiguration&
        motion_config,
    std::optional<RunTimeMotionPlannerFlags> run_time_flags) const {
  const stats::ScopedSpan span("MotionPlanner/PlanPath");
  if (motion_specification.motion_segments_size() < 1) {
    return absl::InvalidArgumentError(
        "Motion Specification does not contain any motion segments. Require at "
        "least one motion segment for motion planning.");
  }

  // Unpack the robot information.
  INTR_ASSIGN_OR_RETURN(const RobotSpecification robot_with_limits,
                        RobotSpecification::Create(world, robot_specification));

  // The set of all specified proxies for each segment.
  std::vector<ProxyCreateInfo> proxy_set;
  // Required to avoid the time overhead with creating the same proxy over and
  // over.
  absl::flat_hash_map<std::string, std::vector<int>>
      path_segment_to_validation_proxies_id_map;

  INTR_ASSIGN_OR_RETURN(
      MotionPlanner::PlanTrajectoryResult planning_result,
      PlanPathInternal(world, robot_with_limits, motion_config,
                       motion_specification, flags_, run_time_flags, proxy_set,
                       path_segment_to_validation_proxies_id_map));

  // Convert the trajectory result to a path result.
  MotionPlanner::PlanPathResult path_result;
  path_result.path_segments = std::move(planning_result.path_segments);
  for (const auto& path_segment : path_result.path_segments) {
    for (const auto& joint_configuration : path_segment.joint_configurations) {
      path_result.path.push_back(joint_configuration);
    }
    // Remove the last joint configuration as it is redundant. Each segment
    // contains the last and first joint configuration. As such we have the last
    // joint configuration twice for segments in the middle of the path.
    path_result.path.pop_back();
  }
  if (!path_result.path.empty()) {
    path_result.path.push_back(
        path_result.path_segments.back().joint_configurations.back());
  }

  path_result.distance_check_statistics =
      std::move(planning_result.distance_check_statistics);

  path_result.path_planning_statistics =
      planning_result.motion_planning_statistics.path_planning_statistics;

  return path_result;
}

}  // namespace intrinsic
