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

#ifndef INTRINSIC_MOTION_PLANNING_MOTION_PLANNER_MOTION_PLANNER_FLAGS_H_
#define INTRINSIC_MOTION_PLANNING_MOTION_PLANNER_MOTION_PLANNER_FLAGS_H_

namespace intrinsic {

// TODO(b/458474652): Re-classify the flags.
// LINT.IfChange(motion_planner_flags)
struct MotionPlannerFlags {
  // Enable a validation step in the motion planning pipeline for
  // PlanTrajectory(), executed after path refinement. Each path sample produced
  // at path refinement is validated against the settings (joint limits,
  // collision, uniform path constraints) of the motion segment it has
  // originated from. See go/intrinsic-topp-validation-step for details.
  bool enable_path_refinement_validation_step = false;

  // If enable_path_refinement_validation_step==true, we validate the final
  // trajectory using slightly relaxed collision margins. For each margin, we
  // compute two candidate relaxations:
  //
  // (1) margin * path_refinement_validation_margin_relative_factor
  //
  // (2) max(0, margin - path_refinement_validation_margin_absolute_factor)
  //
  // We use the _minimum_ of the above 2 candidates.
  double path_refinement_validation_margin_relative_factor = 1.0;

  // See comment for `path_refinement_validation_margin_relative_factor`.
  double path_refinement_validation_margin_absolute_factor = 0.005;
  // Enables the collection of collision checking statistics for the motion
  // planner.
  bool enable_distance_check_statistics = false;

  // Enables using a multithreaded ConcurrentProxy for edge validation in path
  // planning.
  bool enable_concurrent_collision_checking = false;

  // If enable_concurrent_collision_checking == true, then this flag controls
  // how many threads to use for multithreading. We arrived at the default value
  // of 4 empirically. We saw big speedups up to this number, and more marginal
  // gains at higher numbers.
  int concurrent_collision_checking_thread_count = 4;

  // If true: if output trajectory fails refinement validation, we generate a
  // new trajectory with reduced joint blending tightness and try again. If
  // false, we return the refinement validation error.
  bool enable_fallback_trajectory = false;

  // Applies only when `enable_fallback_trajectory == true`.
  // If true: if the output trajectory fails refinement validation, we generate
  // a new trajectory by locally tightening the joint blending only around
  // waypoints that are close to collisions. If false, we tighten the blending
  // radius across all waypoints.
  bool enable_fallback_trajectory_local_adjust = true;
  // LINT.ThenChange(//intrinsic_motion_planning/intrinsic/motion_planning/proto/v1/motion_planner_service.proto:motion_planner_flags)

  // This is the collision check spacing (specified in radians) used for
  // collision checking motions. This can be overridden in
  // `MotionPlannerConfiguration`.
  //
  // LINT.IfChange(collision_check_spacing)
  double default_collision_check_spacing = 0.01;
  // LINT.ThenChange(
  //     //intrinsic_apis/intrinsic/motion_planning/proto/v1/motion_planner_config.proto:collision_check_spacing,
  //     //intrinsic_motion_planning/intrinsic/motion_planning/proto/v1/motion_planner_service.proto:collision_check_spacing,
  //     //intrinsic_motion_planning/intrinsic/motion_planning/skills/move_robot.proto:collision_check_spacing)
};

struct RunTimeMotionPlannerFlags {
  // Enables the collection of collision checking statistics for the motion
  // planner.
  bool enable_distance_check_statistics = false;

  // If true (and if `enable_fallback_trajectory` and
  // `enable_path_refinement_validation_step` are also true), then the motion
  // planner will always execute the strictest fallback trajectory generation
  // strategy. This is intended only to enable testing.
  bool force_strict_fallback_for_testing = false;
};

}  // namespace intrinsic

#endif  // INTRINSIC_MOTION_PLANNING_MOTION_PLANNER_MOTION_PLANNER_FLAGS_H_
