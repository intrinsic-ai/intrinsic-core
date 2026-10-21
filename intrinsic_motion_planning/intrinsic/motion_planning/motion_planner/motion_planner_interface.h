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

#ifndef INTRINSIC_MOTION_PLANNING_MOTION_PLANNER_MOTION_PLANNER_INTERFACE_H_
#define INTRINSIC_MOTION_PLANNING_MOTION_PLANNER_MOTION_PLANNER_INTERFACE_H_

#include <optional>
#include <vector>

#include "absl/status/statusor.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/geometry/api/distance_stats.h"
#include "intrinsic/kinematics/types/joint_trajectories.h"
#include "intrinsic/motion_planning/motion_planner/motion_planner_flags.h"
#include "intrinsic/motion_planning/path_planning/path_segment.h"
#include "intrinsic/motion_planning/proto/v1/motion_planner_config.pb.h"
#include "intrinsic/motion_planning/proto/v1/motion_planner_service.pb.h"
#include "intrinsic/motion_planning/proto/v1/motion_specification.pb.h"
#include "intrinsic/motion_planning/trajectory_planning/topp/path_sample.h"
#include "intrinsic/world/objects/object_world.h"

namespace intrinsic {
// Main interface for all motion planners that create a path or a trajectory.
class MotionPlannerInterface {
 public:
  virtual ~MotionPlannerInterface() = default;
  // Path planning statistics, such as the path planning duration.
  struct PathPlanningStatistics {
    absl::Duration path_planning_duration;
    // Number of motion segments in the path planning request.
    int num_motion_segments = 0;
  };

  // Trajectory generation statistics, such as the combined duration of path
  // refinement and trajectory generation.
  struct TrajectoryGenerationStatistics {
    // The combined duration of path refinement and trajectory generation.
    absl::Duration path_refinement_and_trajectory_generation_duration;

    // The duration of the validation process of the path refinement results.
    // This can be expected to be smaller than
    // `path_refinement_and_trajectory_generation_duration`, because
    // `path_refinement_validation_duration` is a sub-duration of
    // `path_refinement_and_trajectory_generation_duration`.
    absl::Duration path_refinement_validation_duration;
  };

  // Motion planning statistics, including (but not limited to) the motion
  // planning duration and other duration breakdowns such as the path planning
  // statistics and trajectory generation statistics.
  struct MotionPlanningStatistics {
    absl::Duration motion_planning_duration;
    PathPlanningStatistics path_planning_statistics;
    TrajectoryGenerationStatistics trajectory_generation_statistics;
    // Number of motion segments in the motion planning request.
    int num_motion_segments = 0;
  };

  // These are all the types of path generation fallback strategies that the
  // motion planner can attempt to construct a trajectory.
  //
  // TODO(b/552524594): Expose this so that we can analyze logs for fallbacks.
  enum class FallbackStage {
    // No fallback needed.
    kNone,

    // Reduce joint blending at configurations that failed validation.
    kReduceBlendingLocal,

    // Reduce joint blending at and near configurations that failed validation.
    kReduceBlendingLocalStrict,

    // Reduce joint blending along the entire trajectory.
    kReduceBlendingGlobal,

    // Apply absolutely no blending along path segments where a configuration
    // failed validation.
    kStrictlyFollowPathLocal,

    // Apply absolutely no blending along the entire trajectory.
    kStrictlyFollowPathGlobal,
  };

  // Result of PlanTrajectory
  struct PlanTrajectoryResult {
    // The planned trajectory.
    JointTrajectoryPVA trajectory;

    // The path segments used to generate the planned trajectory. Could be
    // empty.
    std::vector<PathSegment> path_segments;

    // The path samples used to generate the planned trajectory.
    std::vector<topp::PathSample> path_samples = {};

    // The collision checking statistics for the planned trajectory.
    DistanceCheckStatistics distance_check_statistics = {};


    // The motion events corresponding to the planned trajectory.
    // Could be empty if there are no events or before trajectory generation.
    std::vector<intrinsic_proto::motion_planning::v1::MotionEvent>
        motion_events = {};

    // Contains information about the motion planning duration, and its
    // breakdown, such as into path planning duration and trajectory generation
    // duration.
    MotionPlanningStatistics motion_planning_statistics;

    // If fallback trajectory generation was not required to successfully plan
    // this trajectory, this will be `FallbackStage::kNone`. Otherwise, this
    // contains the fallback stage that produced this trajectory.
    FallbackStage fallback_stage = FallbackStage::kNone;
  };

  // Result of PlanPath
  struct PlanPathResult {
    // The planned path.
    std::vector<eigenmath::VectorXd> path;

    // The path segments used to generate the planned path. Could be empty.
    std::vector<PathSegment> path_segments;

    // The collision checking statistics for the planned path.
    DistanceCheckStatistics distance_check_statistics = {};

    // Contains information about the path planning duration.
    PathPlanningStatistics path_planning_statistics;
  };

  // Creates a path, i.e., a sequence of waypoints, for the robot that fulfills
  // the motion specifications. The linear joint interpolation between the
  // waypoints is guaranteed to be valid (i.e., collision-free if defined,
  // within joint position limits, and path constraints are satisfied). Validity
  // constraints, such as collision properties are defined within the motion
  // specifications. If no collision settings are defined, the collision
  // settings in the world are used.
  virtual absl::StatusOr<PlanPathResult> PlanPath(
      const object_world::ObjectWorld& world,
      const intrinsic_proto::motion_planning::v1::RobotSpecification&
          robot_specification,
      const intrinsic_proto::motion_planning::v1::MotionSpecification&
          motion_specification,
      const intrinsic_proto::motion_planning::v1::MotionPlannerConfiguration&
          motion_config,
      std::optional<RunTimeMotionPlannerFlags> run_time_flags) const = 0;

  // Creates a trajectory for the robot that fulfils the motion specifications.
  // Collision properties are defined within the motion specifications. If
  // collision checking is requested (default), the function is expected to
  // return a collision free trajectory.
  virtual absl::StatusOr<PlanTrajectoryResult> PlanTrajectory(
      const object_world::ObjectWorld& world,
      const intrinsic_proto::motion_planning::v1::RobotSpecification&
          robot_specification,
      const intrinsic_proto::motion_planning::v1::MotionSpecification&
          motion_specification,
      const intrinsic_proto::motion_planning::v1::MotionPlannerConfiguration&
          motion_config,
      std::optional<RunTimeMotionPlannerFlags> run_time_flags) const = 0;
};
}  // namespace intrinsic

#endif  // INTRINSIC_MOTION_PLANNING_MOTION_PLANNER_MOTION_PLANNER_INTERFACE_H_
