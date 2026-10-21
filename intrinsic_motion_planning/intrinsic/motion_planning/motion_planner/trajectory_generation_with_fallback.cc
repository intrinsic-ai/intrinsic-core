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

#include "intrinsic/motion_planning/motion_planner/trajectory_generation_with_fallback.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "intrinsic/kinematics/types/joint_trajectories.h"
#include "intrinsic/motion_planning/motion_planner/motion_planner_flags.h"
#include "intrinsic/motion_planning/motion_planner/motion_planning_error_utils.h"
#include "intrinsic/motion_planning/motion_planner/motion_planning_utils.h"
#include "intrinsic/motion_planning/motion_planner/trajectory_generation_utils.h"
#include "intrinsic/motion_planning/motion_planner/trajectory_parameterizer.h"
#include "intrinsic/motion_planning/path_planning/kinematics_system_proxy.h"
#include "intrinsic/motion_planning/path_planning/kinematics_system_proxy_util.h"
#include "intrinsic/motion_planning/path_planning/path_segment.h"
#include "intrinsic/motion_planning/path_planning/planners/validation.h"
#include "intrinsic/motion_planning/proto/v1/motion_planner_service.pb.h"
#include "intrinsic/motion_planning/proto/v1/motion_planning_error.pb.h"
#include "intrinsic/motion_planning/trajectory_planning/path_refinement/path_refinement_utils.h"
#include "intrinsic/motion_planning/trajectory_planning/topp/trajectory_with_optional_path_samples.h"
#include "intrinsic/motion_planning/trajectory_planning/trajectory_utils.h"
#include "intrinsic/util/proto_time.h"
#include "intrinsic/util/status/annotate.h"
#include "intrinsic/util/status/ret_check.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/objects/kinematic_object_internal.h"
#include "intrinsic/world/objects/object_world.h"

namespace intrinsic {

namespace {

// We use this function to generate a sequence of trajectories that exactly
// track the geometric path described by `path_segment`. To do this, we generate
// a trajectory for every adjacent pair (or "edge") of joint configurations in
// the path segment. This works because, given a pair of joint configurations,
// the trajectory generator always returns a trajectory that exactly tracks the
// linear path in joint space between those configurations.
absl::Status GenerateTrajectoryForEachEdgeInPathSegment(
    const object_world::ObjectWorld& world,
    const object_world::KinematicObject& robot, const PathSegment& path_segment,
    const TrajectoryParameterizer& trajectory_parameterizer,
    const MotionPlannerFlags& flags,
    std::vector<topp::TrajectoryWithOptionalPathSamples>& trajectories) {
  INTR_RET_CHECK_GE(path_segment.joint_configurations.size(), 2);
  INTR_RET_CHECK_EQ(path_segment.joint_configurations.size(),
                    path_segment.joint_blending_parameter_rad.size());

  // Create a copy of `path_segment` that will contain only 2 configurations;
  // the resulting trajectory will be a strict linear connection between
  // them. We perform a full (wasteful) copy of the `PathSegment` in order to
  // avoid future bugs where we add a field to `PathSegment` and forget to copy
  // it here. We will re-use `sub_segment` for each of the sub-trajectories we
  // generate (but with different start/end configurations each time).
  PathSegment sub_segment = path_segment;
  sub_segment.joint_configurations.resize(2);
  sub_segment.joint_blending_parameter_rad.resize(2);

  // For each edge in `path_segment`, generate a trajectory along that edge and
  // add it to `trajectories`.
  const size_t num_edges = path_segment.joint_configurations.size() - 1;
  trajectories.reserve(trajectories.size() + num_edges);
  for (int config_idx = 0;
       config_idx + 1 < path_segment.joint_configurations.size();
       ++config_idx) {

    const bool is_last_edge_in_segment =
        config_idx + 2 >= path_segment.joint_configurations.size();

    // Add motion events that apply to the start point of this edge. If this is
    // the last edge, also add any motion events that apply to the end point.
    sub_segment.indexed_motion_events.clear();
    for (const MotionEventAndJointConfigurationIndex& event :
         path_segment.indexed_motion_events) {
      if (event.joint_configuration_index == config_idx) {
        sub_segment.indexed_motion_events.push_back(event);
        sub_segment.indexed_motion_events.back().joint_configuration_index = 0;
      } else if (event.joint_configuration_index == config_idx + 1 &&
                 is_last_edge_in_segment) {
        sub_segment.indexed_motion_events.push_back(event);
        sub_segment.indexed_motion_events.back().joint_configuration_index = 1;
      }
    }


    const eigenmath::VectorNd& q_start =
        path_segment.joint_configurations.at(config_idx);
    const eigenmath::VectorNd& q_end =
        path_segment.joint_configurations.at(config_idx + 1);

    INTR_RET_CHECK_EQ(sub_segment.joint_configurations.size(), 2);
    INTR_RET_CHECK_EQ(sub_segment.joint_blending_parameter_rad.size(), 2);
    sub_segment.joint_configurations.front() = q_start;
    sub_segment.joint_configurations.back() = q_end;
    sub_segment.joint_blending_parameter_rad.front() =
        path_segment.joint_blending_parameter_rad.at(config_idx);
    sub_segment.joint_blending_parameter_rad.back() =
        path_segment.joint_blending_parameter_rad.at(config_idx + 1);

    INTR_ASSIGN_OR_RETURN(
        topp::TrajectoryWithOptionalPathSamples sub_trajectory,
        trajectory_parameterizer.ComputeTrajectory(
            world, robot, absl::MakeConstSpan(&sub_segment, 1), flags));

    trajectories.push_back(std::move(sub_trajectory));
  }

  return absl::OkStatus();
}

}  // namespace

// First, this function computes a series of sub-trajectories that span
// `path_segments`, where some of the trajectories are strict and some are not
// (depending on the `path_segment_is_strict` argument). Then, we update the
// sub-trajectories so that their timestamps, arc lengths, etc. will be
// monotonically increasing when concatenated. Finally, we concatenate the
// sub-trajectories into a single trajectory.
absl::StatusOr<topp::TrajectoryWithOptionalPathSamples>
GenerateTrajectoryWithStrictPathFollowingSegments(
    const object_world::ObjectWorld& world,
    const object_world::KinematicObject& robot,
    absl::Span<const PathSegment> path_segments,
    const TrajectoryParameterizer& trajectory_parameterizer,
    const MotionPlannerFlags& flags,
    const std::vector<bool>& path_segment_is_strict) {
  INTR_RET_CHECK(!path_segments.empty());
  INTR_RET_CHECK_EQ(path_segments.size(), path_segment_is_strict.size());

  // Generate "strictness intervals" for `path_segments`; i.e., sequences of
  // path segments that are all strict or all non-strict. We especially need to
  // track non-strict intervals so that we can combine them into one
  // `ComputeTrajectory()` call and blend them together.
  struct PathSegmentInterval {
    int first_path_segment_idx;
    int last_path_segment_idx;
    bool is_strict;
  };
  std::vector<PathSegmentInterval> intervals;
  intervals.reserve(path_segments.size());
  intervals.push_back(
      PathSegmentInterval{.first_path_segment_idx = 0,
                          .last_path_segment_idx = 0,
                          .is_strict = path_segment_is_strict.at(0)});

  for (int ii = 1; ii < path_segments.size(); ++ii) {
    if (path_segment_is_strict[ii] == intervals.back().is_strict) {
      intervals.back().last_path_segment_idx = ii;
    } else {
      intervals.push_back(
          PathSegmentInterval{.first_path_segment_idx = ii,
                              .last_path_segment_idx = ii,
                              .is_strict = path_segment_is_strict[ii]});
    }
  }

  std::vector<topp::TrajectoryWithOptionalPathSamples> sub_trajectories;
  sub_trajectories.reserve(path_segments.size());

  for (const PathSegmentInterval& interval : intervals) {
    if (interval.is_strict) {
      // Generate a strict path-following trajectory for this interval of path
      // segments.
      for (int path_segment_idx = interval.first_path_segment_idx;
           path_segment_idx <= interval.last_path_segment_idx;
           ++path_segment_idx) {
        const PathSegment& path_segment = path_segments.at(path_segment_idx);
        INTR_RETURN_IF_ERROR(GenerateTrajectoryForEachEdgeInPathSegment(
            world, robot, path_segment, trajectory_parameterizer, flags,
            sub_trajectories));
      }
    } else {
      // Generate a regular trajectory for this interval of path segments.
      const int path_segment_count =
          (interval.last_path_segment_idx - interval.first_path_segment_idx) +
          1;
      absl::Span<const PathSegment> interval_segments = path_segments.subspan(
          interval.first_path_segment_idx, path_segment_count);
      INTR_ASSIGN_OR_RETURN(
          topp::TrajectoryWithOptionalPathSamples sub_trajectory,
          trajectory_parameterizer.ComputeTrajectory(world, robot,
                                                     interval_segments, flags));
      sub_trajectories.push_back(std::move(sub_trajectory));
    }
  }

  INTR_RET_CHECK(!sub_trajectories.empty());
  const JointTrajectoryPVA& first_sub_traj_pva =
      sub_trajectories.front().topp_trajectory_result.trajectory;

  // Each sub-trajectory has its timestamps, arc-lengths, events, etc starting
  // at 0.0. Here we accumulate these values across the trajectories so they are
  // monotonically increasing.
  std::vector<JointStatePVA> accumulated_joint_states;
  std::vector<absl::Duration> accumulated_time_stamps;
  std::optional<std::vector<double>> maybe_accumulated_cartesian_arc_lengths;
  if (first_sub_traj_pva.HasCartesianArcLength()) {
    maybe_accumulated_cartesian_arc_lengths = std::vector<double>();
  }
  std::vector<topp::PathSample> accumulated_path_samples;

  std::vector<intrinsic_proto::motion_planning::v1::MotionEvent>
      accumulated_motion_events;

  double accumulated_cartesian_arc_length = 0.0;
  absl::Duration accumulated_time;
  for (int sub_trajectory_idx = 0; sub_trajectory_idx < sub_trajectories.size();
       ++sub_trajectory_idx) {
    const topp::TrajectoryWithOptionalPathSamples& sub_trajectory =
        sub_trajectories[sub_trajectory_idx];

    const bool is_last_traj = sub_trajectory_idx + 1 >= sub_trajectories.size();

    const JointTrajectoryPVA& sub_traj_pva =
        sub_trajectory.topp_trajectory_result.trajectory;
    INTR_RET_CHECK_GT(sub_traj_pva.size(), 0) << "Unexpected empty trajectory!";

    // Accumulate joint states.
    accumulated_joint_states.insert(accumulated_joint_states.end(),
                                    sub_traj_pva.data().begin(),
                                    sub_traj_pva.data().end());

    // Accumulate time stamps.
    accumulated_time_stamps.reserve(accumulated_time_stamps.size() +
                                    sub_traj_pva.size());
    for (int ii = 0; ii < sub_traj_pva.size(); ++ii) {
      const absl::Duration adjusted_time_stamp =
          sub_traj_pva.time_stamps().at(ii) + accumulated_time;
      accumulated_time_stamps.push_back(adjusted_time_stamp);
    }

    // Accumulate cartesian arc lengths.
    if (maybe_accumulated_cartesian_arc_lengths.has_value()) {
      INTR_RET_CHECK(sub_traj_pva.HasCartesianArcLength())
          << "Unexpected: not all sub-trajectories have cartesian arc length!";
      maybe_accumulated_cartesian_arc_lengths->reserve(
          maybe_accumulated_cartesian_arc_lengths->size() +
          sub_traj_pva.size());
      for (int ii = 0; ii < sub_traj_pva.size(); ++ii) {
        const double adjusted_cartesian_arc_length =
            sub_traj_pva.cartesian_arc_lengths()->at(ii) +
            accumulated_cartesian_arc_length;
        maybe_accumulated_cartesian_arc_lengths->push_back(
            adjusted_cartesian_arc_length);
      }
    }

    // If this is not the last trajectory, we prune the last state; this
    // state would be a duplicate of the first state of the next trajectory.
    if (!is_last_traj) {
      accumulated_joint_states.pop_back();
      accumulated_time_stamps.pop_back();
      if (maybe_accumulated_cartesian_arc_lengths.has_value()) {
        maybe_accumulated_cartesian_arc_lengths->pop_back();
      }
    }

    // Accumulate path samples.
    INTR_RET_CHECK(sub_trajectory.path_samples.has_value());
    INTR_RET_CHECK(!sub_trajectory.path_samples->empty());
    double s_offset = 0.0;
    double s_c_offset = 0.0;
    if (!accumulated_path_samples.empty()) {
      s_offset = accumulated_path_samples.back().s;
      s_c_offset = accumulated_path_samples.back().s_c;
      // If this isn't the first trajectory, remove the last path sample because
      // it would be a duplicate of the first path sample of this trajectory.
      accumulated_path_samples.pop_back();
    }
    accumulated_path_samples.reserve(accumulated_path_samples.size() +
                                     sub_trajectory.path_samples->size());
    for (const topp::PathSample& sample : *sub_trajectory.path_samples) {
      accumulated_path_samples.push_back(sample);
      accumulated_path_samples.back().s += s_offset;
      accumulated_path_samples.back().s_c += s_c_offset;
    }


    // Accumulate motion events
    accumulated_motion_events.reserve(
        accumulated_motion_events.size() +
        sub_trajectory.topp_trajectory_result.motion_events.size());
    for (const intrinsic_proto::motion_planning::v1::MotionEvent& motion_event :
         sub_trajectory.topp_trajectory_result.motion_events) {
      intrinsic_proto::motion_planning::v1::MotionEvent new_event =
          motion_event;
      // Accumulate arc length
      const double new_arc_length =
          new_event.keypoint()
              .keypoint_cartesian_arc_length_from_start_meters() +
          accumulated_cartesian_arc_length;
      new_event.mutable_keypoint()
          ->set_keypoint_cartesian_arc_length_from_start_meters(new_arc_length);

      // Accumulate duration
      INTR_ASSIGN_OR_RETURN(
          absl::Duration dur,
          ToAbslDuration(new_event.keypoint().keypoint_time_from_start()));
      dur += accumulated_time;
      INTR_ASSIGN_OR_RETURN(
          *new_event.mutable_keypoint()->mutable_keypoint_time_from_start(),
          FromAbslDuration(dur));
      // NOTE: We are not filling out the
      // keypoint_joint_path_variable_from_start field. We don't have enough
      // info to populate this and it is not used after trajectory generation.
      accumulated_motion_events.push_back(new_event);
    }


    INTR_RET_CHECK(
        !sub_trajectory.topp_trajectory_result.trajectory.time_stamps()
             .empty());

    // Update our cumulative arc length, time, and cartesian arc length.
    accumulated_time +=
        sub_trajectory.topp_trajectory_result.trajectory.Duration();
    if (sub_traj_pva.HasCartesianArcLength()) {
      accumulated_cartesian_arc_length +=
          sub_traj_pva.cartesian_arc_lengths()->back();
    }
  }

  // Create our combined trajectory out of the accumulated data.
  INTR_ASSIGN_OR_RETURN(
      JointTrajectoryPVA combined_traj_pva,
      JointTrajectoryPVA::Create(
          std::move(accumulated_joint_states),
          std::move(accumulated_time_stamps),
          first_sub_traj_pva.joint_dynamic_limits_check_mode(),
          first_sub_traj_pva.interpolation_type(),
          std::move(maybe_accumulated_cartesian_arc_lengths)));

  // Combine our new trajectory with
  // the accumulated motion events and 
  // path samples.
  return topp::TrajectoryWithOptionalPathSamples{
      .topp_trajectory_result =
          topp::ToppTrajectoryResult{
              .trajectory = std::move(combined_traj_pva),
              // Unused after trajectory generation
              //.squared_path_velocity = {},

              .motion_events = std::move(accumulated_motion_events),

          },
      .path_samples = std::move(accumulated_path_samples)};
}

absl::StatusOr<GenerateTrajectoryResult> GenerateTrajectoryWithFallback(
    const object_world::ObjectWorld& world,
    const object_world::KinematicObject& robot,
    const intrinsic_proto::motion_planning::v1::MotionPlannerConfiguration&
        motion_config,
    absl::Span<const PathSegment> path_segments,
    const TrajectoryParameterizer& trajectory_parameterizer,
    const MotionPlannerFlags& flags,
    const std::vector<ProxyCreateInfo>& proxy_set,
    const absl::flat_hash_map<std::string, std::vector<int>>&
        path_segment_to_validation_proxies_id_map,
    const bool force_strict_fallback_for_testing) {
  GenerateTrajectoryResult result;

  // Check that path segments are set up correctly.
  for (const PathSegment& segment : path_segments) {
    if (segment.joint_configurations.size() < 2) {
      return absl::InternalError(
          "Too few path segment joint configurations defined for segment. "
          "Expected at least two configurations but found segment with less.");
    }
  }

  // Handle almost zero motion.
  // TODO b/527403767 - move to motion_planner_utils.h
  if (path_segments.size() == 1 &&
      path_segments.front().joint_configurations.size() == 2) {
    const double segment_length =
        (path_segments.front().joint_configurations.front() -
         path_segments.front().joint_configurations.back())
            .norm();
    if (segment_length <= topp::kJointConfigEqualityMarginRad) {
      LOG(INFO) << "No waypoints provided that would require motion (length: "
                << segment_length << "). Returning zero motion.";
      INTR_ASSIGN_OR_RETURN(
          result.trajectory_with_samples.topp_trajectory_result.trajectory,
          ZeroMotionTrajectory(
              path_segments.front().joint_configurations.back()));
      INTR_ASSIGN_OR_RETURN(
          result.trajectory_with_samples.path_samples,
          ZeroMotionPath(path_segments.front().joint_configurations.back()));
      return result;
    }
  }

  // Computes trajectory from current path segments and accumulates elapsed
  // execution time into `result.trajectory_generation_duration`. Shared between
  // initial trajectory generation and fallback generation (with reduced
  // blending).
  const auto compute_trajectory = [&]() -> absl::Status {
    const absl::Time before_t = absl::Now();
    INTR_ASSIGN_OR_RETURN(result.trajectory_with_samples,
                          trajectory_parameterizer.ComputeTrajectory(
                              world, robot, path_segments, flags),
                          _ << "Not able to compute trajectory for formulated "
                               "planning problem. ");
    result.trajectory_generation_duration += absl::Now() - before_t;
    return absl::OkStatus();
  };

  INTR_RETURN_IF_ERROR(compute_trajectory());

  if (!flags.enable_path_refinement_validation_step) {
    return result;
  }

  if (!result.trajectory_with_samples.path_samples.has_value()) {
    LOG(ERROR) << "Path refinement validation was requested but trajectory has "
                  "no path samples!";
    return absl::InternalError(
        "Motion Planner was unable to validate trajectory.");
  }

  // Create proxies with relaxed margins for validation.
  std::vector<std::unique_ptr<KinematicsSystemProxy>> validation_proxies;
  {
    validation_proxies.reserve(proxy_set.size());
    const absl::Time before_t = absl::Now();
    const std::optional<int> maybe_concurrent_thread_count =
        GetConcurrentThreadCount(flags, /*distance_check_statistics=*/nullptr);
    for (const ProxyCreateInfo& create_info : proxy_set) {
      INTR_ASSIGN_OR_RETURN(
          std::unique_ptr<KinematicsSystemProxy> proxy,
          CreateProxyWithRelaxedMargins(
              world, robot,
              KinematicsProxyOptions{
                  .collision_checker_config =
                      motion_config.collision_checker_config(),
                  .constraints_proto = create_info.constraints_proto,
                  .rule_set = create_info.rule_set,
                  .disable_collision_checking =
                      create_info.disable_collision_checking,
                  .maybe_concurrent_thread_count =
                      maybe_concurrent_thread_count,
              },
              flags.path_refinement_validation_margin_relative_factor,
              flags.path_refinement_validation_margin_absolute_factor));
      validation_proxies.push_back(std::move(proxy));
    }
    result.validation_proxy_creation_duration = absl::Now() - before_t;
    VLOG(1) << "Validation proxy creation took "
            << absl::ToDoubleSeconds(result.validation_proxy_creation_duration)
            << " s";
  }

  INTR_ASSIGN_OR_RETURN(const double collision_check_spacing,
                        GetCollisionCheckSpacing(flags, motion_config));

  // Allow local adjust only if fallback is enabled.
  const bool enable_fallback_trajectory_local_adjust =
      flags.enable_fallback_trajectory_local_adjust &&
      flags.enable_fallback_trajectory;

  const std::optional<int> max_collisions =
      !enable_fallback_trajectory_local_adjust ? std::make_optional(1)
                                               : std::nullopt;

  std::vector<InvalidPathSampleInfo> invalid_samples_info;
  // Validate trajectory.
  {
    const absl::Time before_t = absl::Now();

    // Use `FindInvalidPathSamples` instead of `ValidatePathSamples` so that if
    // local blending adjustment is enabled, we do not have to scan the path
    // again for collisions.
    INTR_ASSIGN_OR_RETURN(
        invalid_samples_info,
        FindInvalidPathSamples(*result.trajectory_with_samples.path_samples,
                               validation_proxies,
                               path_segment_to_validation_proxies_id_map,
                               collision_check_spacing, max_collisions));

    result.validation_duration += absl::Now() - before_t;

    absl::Status validation_error_status;
    if (!invalid_samples_info.empty()) {
      const InvalidPathSampleInfo& invalid_info = invalid_samples_info.front();
      INTR_ASSIGN_OR_RETURN(validation_error_status,
                            MakeValidationErrorStatus(
                                invalid_info.invalid_config_info,
                                *validation_proxies[invalid_info.proxy_index]));
    }
    if (validation_error_status.ok() && force_strict_fallback_for_testing) {
      validation_error_status = absl::InternalError("Force strict fallback");
    }

    if (validation_error_status.ok()) {
      return result;
    }

    if (!flags.enable_fallback_trajectory) {
      return validation_error_status;
    }

    LOG(INFO) << "Main trajectory generation failed validation: "
              << validation_error_status.message();
  }

  // If validation failed, we try a fallback: we reduce joint blending,
  // regenerate the trajectory, and revalidate.
  //
  // TODO(b/493955608): Is there a way for us to communicate to the user that
  // this happened so that they understand why this motion was slower to
  // execute?
  result.non_fallback_trajectory = std::move(result.trajectory_with_samples);

  auto generate_and_validate_trajectory =
      [&](absl::Span<const PathSegment> path_segments)
      -> absl::StatusOr<PathValidationResult> {
    absl::Time before_t = absl::Now();
    INTR_ASSIGN_OR_RETURN(result.trajectory_with_samples,
                          trajectory_parameterizer.ComputeTrajectory(
                              world, robot, path_segments, flags),
                          _ << "Not able to compute trajectory for formulated "
                               "planning problem. ");
    result.trajectory_generation_duration += absl::Now() - before_t;

    // Validate.
    before_t = absl::Now();
    INTR_ASSIGN_OR_RETURN(
        PathValidationResult validation_result,
        ValidatePathSamples(*result.trajectory_with_samples.path_samples,
                            validation_proxies,
                            path_segment_to_validation_proxies_id_map,
                            collision_check_spacing));
    result.validation_duration += absl::Now() - before_t;

    if (validation_result.IsValid() && force_strict_fallback_for_testing) {
      validation_result.validation_status =
          absl::InternalError("Force strict fallback");
    }

    return validation_result;
  };

  std::vector<AdjustedLineSegment> adjusted_line_segments;
  std::vector<PathSegment> adjusted_path_segments(path_segments.begin(),
                                                  path_segments.end());
  if (flags.enable_fallback_trajectory_local_adjust) {
    LOG(INFO) << "Generating fallback trajectory with locally reduced joint "
                 "blending.";

    result.fallback_stage =
        MotionPlannerInterface::FallbackStage::kReduceBlendingLocal;

    const std::vector<int> invalid_sample_indices = [&]() {
      std::vector<int> indices;
      indices.reserve(invalid_samples_info.size());
      for (const InvalidPathSampleInfo& invalid_info : invalid_samples_info) {
        indices.push_back(invalid_info.invalid_config_info.index);
      }
      return indices;
    }();

    INTR_ASSIGN_OR_RETURN(
        adjusted_line_segments,
        AdjustPathBlendingLocally(
            absl::MakeSpan(adjusted_path_segments),
            *result.non_fallback_trajectory.value().path_samples,
            invalid_sample_indices));

    INTR_ASSIGN_OR_RETURN(
        const PathValidationResult local_validate_result,
        generate_and_validate_trajectory(adjusted_path_segments));

    if (local_validate_result.IsValid()) {
      return result;
    }

    LOG(INFO) << "Local fallback trajectory generation failed validation: "
              << local_validate_result.validation_status->message()
              << "\nProceeding to strict local fallback.";

    result.fallback_stage =
        MotionPlannerInterface::FallbackStage::kReduceBlendingLocalStrict;

    for (const AdjustedLineSegment& line_segment : adjusted_line_segments) {
      INTR_RETURN_IF_ERROR(SetWaypointBlendingToMinimum(
          line_segment.segment_index, line_segment.first_point_index,
          absl::MakeSpan(adjusted_path_segments)));
      INTR_RETURN_IF_ERROR(SetWaypointBlendingToMinimum(
          line_segment.segment_index, line_segment.second_point_index,
          absl::MakeSpan(adjusted_path_segments)));
    }

    INTR_ASSIGN_OR_RETURN(
        const PathValidationResult strict_validate_result,
        generate_and_validate_trajectory(adjusted_path_segments));

    if (strict_validate_result.IsValid()) {
      return result;
    }

    LOG(WARNING)
        << "Strict local fallback trajectory generation failed validation: "
        << strict_validate_result.validation_status->message();
  }

  LOG(INFO) << "Generating global fallback trajectory with minimum joint "
               "blending across all points.";

  result.fallback_stage =
      MotionPlannerInterface::FallbackStage::kReduceBlendingGlobal;

  // Global fallback: reduce all blending to minimum.
  for (PathSegment& segment : adjusted_path_segments) {
    for (double& joint_blending_rad : segment.joint_blending_parameter_rad) {
      joint_blending_rad = kMinimumLowLevelJointBlendingDeviationRad;
    }
  }

  INTR_ASSIGN_OR_RETURN(
      const PathValidationResult global_validate_result,
      generate_and_validate_trajectory(absl::MakeSpan(adjusted_path_segments)));

  if (global_validate_result.IsValid()) {
    return result;
  }

  LOG(WARNING) << "Global fallback trajectory generation failed validation: "
               << global_validate_result.validation_status->message();

  if (motion_config.has_enable_strict_trajectory_fallback() &&
      !motion_config.enable_strict_trajectory_fallback()) {
    LOG(INFO) << "Strict trajectory fallback is disabled.";
    return *global_validate_result.validation_status;
  }

  if (flags.enable_fallback_trajectory_local_adjust) {
    // Local strict fallback: generate a trajectory that strictly follows the
    // path along segments where collisions were detected.
    LOG(INFO) << "Generating locally strict path-following fallback trajectory";

    result.fallback_stage =
        MotionPlannerInterface::FallbackStage::kStrictlyFollowPathLocal;

    std::vector<bool> path_segment_is_strict(path_segments.size(), false);
    for (const AdjustedLineSegment& adjusted_segment : adjusted_line_segments) {
      INTR_RET_CHECK_LT(adjusted_segment.segment_index,
                        path_segment_is_strict.size());
      path_segment_is_strict.at(adjusted_segment.segment_index) = true;
    }

    absl::Time before_t = absl::Now();
    INTR_ASSIGN_OR_RETURN(
        result.trajectory_with_samples,
        GenerateTrajectoryWithStrictPathFollowingSegments(
            world, robot, path_segments, trajectory_parameterizer, flags,
            path_segment_is_strict));
    result.trajectory_generation_duration += absl::Now() - before_t;

    // Validate.
    before_t = absl::Now();
    INTR_ASSIGN_OR_RETURN(
        PathValidationResult validation_result,
        ValidatePathSamples(*result.trajectory_with_samples.path_samples,
                            validation_proxies,
                            path_segment_to_validation_proxies_id_map,
                            collision_check_spacing));
    result.validation_duration += absl::Now() - before_t;

    if (validation_result.IsValid() && force_strict_fallback_for_testing) {
      validation_result.validation_status =
          absl::InternalError("Force strict fallback");
    }

    if (validation_result.IsValid()) {
      return result;
    }

    LOG(WARNING)
        << "Locally strict fallback trajectory generation failed validation:"
        << validation_result.validation_status->message();
  }

  // Strict fallback: generate a trajectory that strictly follows the entire
  // path.
  LOG(INFO) << "Generating globally strict path-following fallback trajectory";

  result.fallback_stage =
      MotionPlannerInterface::FallbackStage::kStrictlyFollowPathGlobal;

  std::vector<bool> path_segment_is_strict(path_segments.size(), true);

  absl::Time before_t = absl::Now();
  INTR_ASSIGN_OR_RETURN(
      result.trajectory_with_samples,
      GenerateTrajectoryWithStrictPathFollowingSegments(
          world, robot, path_segments, trajectory_parameterizer, flags,
          path_segment_is_strict));
  result.trajectory_generation_duration += absl::Now() - before_t;

  // Validate.
  before_t = absl::Now();
  INTR_ASSIGN_OR_RETURN(
      const PathValidationResult validation_result,
      ValidatePathSamples(
          *result.trajectory_with_samples.path_samples, validation_proxies,
          path_segment_to_validation_proxies_id_map, collision_check_spacing));
  result.validation_duration += absl::Now() - before_t;

  if (validation_result.IsValid()) {
    return result;
  }

  LOG(WARNING) << "Strict fallback trajectory generation failed validation:"
               << validation_result.validation_status->message();

  // TODO(b/552523691): We should be using this for all the error returns in
  // this function (and elsewhere in motion planner service.
  return UpdateStatusWithPipelineError(*validation_result.validation_status,
                                       /*segment_index=*/{},
                                       "Trajectory failed validation.");
}

}  // namespace intrinsic
