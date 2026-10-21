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

#include "intrinsic/motion_planning/service/nonvolatile_cache/motion_planner_nonvolatile_cache_utils.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_join.h"
#include "absl/strings/substitute.h"
#include "absl/types/span.h"
#include "intrinsic/motion_planning/motion_planner/motion_planner.h"
#include "intrinsic/motion_planning/motion_planner/motion_planning_utils.h"
#include "intrinsic/motion_planning/motion_planner/robot_specification.h"
#include "intrinsic/motion_planning/path_planning/path_segment.h"
#include "intrinsic/motion_planning/path_planning/planners/validation.h"
#include "intrinsic/motion_planning/proto/v1/geometric_constraints.pb.h"
#include "intrinsic/motion_planning/proto/v1/motion_planner_config.pb.h"
#include "intrinsic/motion_planning/proto/v1/motion_planner_service.pb.h"
#include "intrinsic/motion_planning/proto/v1/motion_specification.pb.h"
#include "intrinsic/motion_planning/service/nonvolatile_cache/motion_planner_nonvolatile_cache.h"
#include "intrinsic/util/eigen.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/objects/object_world.h"
#include "intrinsic/world/proto/object_world_refs.pb.h"

namespace intrinsic {

using ::intrinsic_proto::motion_planning::v1::MotionPlanningRequest;

std::optional<intrinsic_proto::motion_planning::v1::LockMotionConfiguration>
GetLockMotionConfiguration(const MotionPlanningRequest& request) {
  if (request.has_motion_planner_config() &&
      request.motion_planner_config().has_lock_motion_configuration()) {
    return request.motion_planner_config().lock_motion_configuration();
  }
  return std::nullopt;
}

absl::StatusOr<std::optional<MotionPlanner::PlanTrajectoryResult>>
LoadMotionFromNonvolatileCacheIfRequested(
    const MotionPlanner& motion_planner,
    MotionPlannerNonvolatileCache* plan_trajectory_nonvolatile_cache,
    const object_world::ObjectWorld& object_world,
    const MotionPlanningRequest& request, const double collision_check_spacing,
    std::optional<std::string> mps_asset_major_version) {
  std::optional<intrinsic_proto::motion_planning::v1::LockMotionConfiguration>
      lock_motion_configuration = GetLockMotionConfiguration(request);
  if (!lock_motion_configuration.has_value()) {
    LOG(INFO)
        << "Skip load motion since there is no lock_motion_configuration.";
    return std::nullopt;
  }

  if (!lock_motion_configuration.value().has_load_motion_command()) {
    LOG(INFO) << "Skip load motion since there is no load_motion_command.";
    return std::nullopt;
  }

  if (lock_motion_configuration.value()
          .load_motion_command()
          .motion_id()
          .empty()) {
    return absl::InvalidArgumentError(
        "Cannot load motion since motion_id is not specified.");
  }

  if (plan_trajectory_nonvolatile_cache == nullptr) {
    return absl::InvalidArgumentError(
        "Cannot load motion since plan_trajectory_nonvolatile_cache is null.");
  }

  INTR_ASSIGN_OR_RETURN(
      MotionPlannerNonvolatileCacheKey cache_key,
      MotionPlannerNonvolatileCacheKey::Create(
          object_world, request.motion_specification(),
          request.robot_specification(),
          /*uuid=*/
          lock_motion_configuration.value().load_motion_command().motion_id(),
          /*mps_asset_major_version=*/mps_asset_major_version.value_or("")));

  LOG(INFO) << "Loading motion with ID: " << cache_key.uuid;
  INTR_ASSIGN_OR_RETURN(
      std::optional<MotionPlannerNonvolatileCacheEntry> found_entry,
      plan_trajectory_nonvolatile_cache->Lookup(cache_key));

  if (!found_entry.has_value()) {
    return absl::NotFoundError(
        "Cannot load motion. No motion is saved with ID: " + cache_key.uuid);
  }

  absl::flat_hash_set<uint32_t> replan_motion_segment_ids;
  for (const uint32_t motion_segment_id : lock_motion_configuration.value()
                                              .load_motion_command()
                                              .replan_motion_segment_ids()) {
    if (motion_segment_id >=
        request.motion_specification().motion_segments_size()) {
      return absl::InvalidArgumentError(absl::Substitute(
          "The motion segment id $0 in replan_motion_segment_ids "
          "is greater or equal to the number of motion segments $1 in the "
          "motion specification.",
          motion_segment_id,
          request.motion_specification().motion_segments_size()));
    }
    replan_motion_segment_ids.insert(motion_segment_id);
  }

  if (!found_entry.value().key.IsApproximate(cache_key,
                                             replan_motion_segment_ids)) {
    return absl::NotFoundError(
        "Cannot load motion. The motion saved with the given ID does "
        "not match the given request.");
  }

  if (replan_motion_segment_ids.empty()) {
    // Unpack the robot information.
    INTR_ASSIGN_OR_RETURN(const RobotSpecification robot_specification,
                          RobotSpecification::Create(
                              object_world, request.robot_specification()));

    INTR_ASSIGN_OR_RETURN(
        const bool in_limits_and_collision_free,
        CheckLimitsAndCollisionsForPathSegments(
            object_world, *robot_specification.robot,
            request.motion_planner_config().collision_checker_config(),
            found_entry.value().value.path_segments, collision_check_spacing));
    if (!in_limits_and_collision_free) {
      return absl::NotFoundError(
          "Cannot load motion. The motion saved with the given ID is not "
          "in limits or collision free.");
    }

    LOG(INFO) << "The found motion matches the given request, is in limits "
                 "and collision free.";
    MotionPlanner::PlanTrajectoryResult result{
        .trajectory = found_entry.value().value.trajectory,
        .path_segments = found_entry.value().value.path_segments

        ,
        .motion_events = found_entry.value().value.motion_events

    };
    const int num_motion_segments =
        request.motion_specification().motion_segments_size();
    result.motion_planning_statistics.num_motion_segments = num_motion_segments;
    result.motion_planning_statistics.path_planning_statistics
        .num_motion_segments = num_motion_segments;
    return result;
  }

  LOG(INFO) << "Construct a new motion specification and plan trajectory. "
               "replan_motion_segment_ids: "
            << absl::StrJoin(replan_motion_segment_ids, ", ");
  return ConstructNewMotionSpecificationAndPlanTrajectory(
      motion_planner, object_world, request, replan_motion_segment_ids,
      found_entry.value().value.path_segments);
}

absl::StatusOr<std::optional<std::string>>
SaveMotionToNonvolatileCacheIfRequested(
    MotionPlannerNonvolatileCache* plan_trajectory_nonvolatile_cache,
    const object_world::ObjectWorld& object_world,
    const MotionPlanningRequest& request,
    const MotionPlanner::PlanTrajectoryResult& plan_trajectory_result,
    std::optional<std::string> mps_asset_major_version) {
  std::optional<intrinsic_proto::motion_planning::v1::LockMotionConfiguration>
      lock_motion_configuration = GetLockMotionConfiguration(request);
  if (!lock_motion_configuration.has_value()) {
    LOG(INFO)
        << "Skip save motion since there is no lock_motion_configuration.";
    return std::nullopt;
  }

  if (!lock_motion_configuration.value().has_save_motion_command()) {
    LOG(INFO) << "Skip save motion since there is no save_motion_command.";
    return std::nullopt;
  }

  if (plan_trajectory_nonvolatile_cache == nullptr) {
    return absl::InvalidArgumentError(
        "Cannot save motion since plan_trajectory_nonvolatile_cache is null.");
  }

  INTR_ASSIGN_OR_RETURN(
      MotionPlannerNonvolatileCacheKey cache_key,
      MotionPlannerNonvolatileCacheKey::Create(
          object_world, request.motion_specification(),
          request.robot_specification(),
          /*uuid=*/"",
          /*mps_asset_major_version=*/mps_asset_major_version.value_or("")));
  MotionPlannerNonvolatileCacheValue cache_value{
      .trajectory = plan_trajectory_result.trajectory,
      .path_segments = plan_trajectory_result.path_segments

      ,
      .motion_events = plan_trajectory_result.motion_events

  };

  INTR_ASSIGN_OR_RETURN(const std::string uuid,
                        plan_trajectory_nonvolatile_cache->Insert(
                            {.key = cache_key, .value = cache_value}));
  LOG(INFO) << "Saved motion with ID: " << uuid;
  return uuid;
}

std::vector<intrinsic_proto::motion_planning::v1::JointPositionEquality>
GetJointPositionEqualityConstraintsFromPathSegment(
    const PathSegment& path_segment,
    const intrinsic_proto::world::ObjectReference& robot_id) {
  std::vector<intrinsic_proto::motion_planning::v1::JointPositionEquality>
      constraints;
  constraints.reserve(path_segment.joint_configurations.size());
  for (const auto& joint_configuration : path_segment.joint_configurations) {
    intrinsic_proto::motion_planning::v1::JointPositionEquality constraint;
    *constraint.mutable_object_id() = robot_id;
    VectorNdToRepeatedDouble(
        joint_configuration,
        constraint.mutable_joint_positions()->mutable_joints());
    constraints.push_back(constraint);
  }
  return constraints;
}

std::vector<intrinsic_proto::motion_planning::v1::MotionSegment>
GetMotionSegmentsFromPathSegment(
    const intrinsic_proto::motion_planning::v1::MotionSegment&
        original_motion_segment,
    const PathSegment& path_segment,
    const intrinsic_proto::world::ObjectReference& robot_id) {
  std::vector<intrinsic_proto::motion_planning::v1::MotionSegment>
      motion_segments;
  motion_segments.reserve(path_segment.joint_configurations.size());

  // TODO(b/): Use the new proto for joint position equality constraints.
  std::vector<intrinsic_proto::motion_planning::v1::JointPositionEquality>
      constraints = GetJointPositionEqualityConstraintsFromPathSegment(
          path_segment, robot_id);

  for (const auto& constraint : constraints) {
    // Starts from the original motion segment and replace the motion target
    // with a joint position equality constraint.
    intrinsic_proto::motion_planning::v1::MotionSegment motion_segment =
        original_motion_segment;
    // Remove the linear path constraint and use joint move motion type.
    motion_segment.set_motion_type(
        intrinsic_proto::motion_planning::v1::MotionSegment::JOINT);
    *motion_segment.mutable_target()->mutable_joint_position() =
        constraint.joint_positions();
    motion_segments.push_back(motion_segment);
  }
  return motion_segments;
}

absl::StatusOr<intrinsic_proto::motion_planning::v1::MotionSpecification>
GetMotionSpecificationFromPathSegments(
    const intrinsic_proto::motion_planning::v1::MotionSpecification&
        original_motion_specification,
    const absl::flat_hash_set<uint32_t>& motion_segment_ids_to_keep,
    absl::Span<const PathSegment> path_segments,
    const intrinsic_proto::world::ObjectReference& robot_id) {
  if (original_motion_specification.motion_segments_size() !=
      path_segments.size()) {
    return absl::InvalidArgumentError(absl::Substitute(
        "The number of motion segments $0 in the given motion specification "
        "does not match the number of path segments $1.",
        original_motion_specification.motion_segments_size(),
        path_segments.size()));
  }

  const uint32_t max_motion_segment_id = *std::max_element(
      motion_segment_ids_to_keep.begin(), motion_segment_ids_to_keep.end());
  if (max_motion_segment_id >=
      original_motion_specification.motion_segments_size()) {
    return absl::InvalidArgumentError(absl::Substitute(
        "The max motion segment id $0 in the given motion_segment_ids_to_keep "
        "is "
        "greater or equal to the number of motion segments $1 in the given "
        "motion specification.",
        max_motion_segment_id,
        original_motion_specification.motion_segments_size()));
  }

  // Make a copy of the original motion specification and clear the motion
  // segments.
  intrinsic_proto::motion_planning::v1::MotionSpecification
      motion_specification = original_motion_specification;
  motion_specification.clear_motion_segments();

  for (int i = 0; i < original_motion_specification.motion_segments_size();
       ++i) {
    if (motion_segment_ids_to_keep.contains(i)) {
      // Use the original motion segment.
      *motion_specification.add_motion_segments() =
          original_motion_specification.motion_segments(i);
    } else {
      // Get the new motion segments from the path segment.
      for (const auto& motion_segment : GetMotionSegmentsFromPathSegment(
               original_motion_specification.motion_segments(i),
               path_segments[i], robot_id)) {
        *motion_specification.add_motion_segments() = motion_segment;
      }
    }
  }
  return motion_specification;
}

absl::StatusOr<MotionPlanner::PlanTrajectoryResult>
ConstructNewMotionSpecificationAndPlanTrajectory(
    const MotionPlanner& motion_planner,
    const object_world::ObjectWorld& object_world,
    const intrinsic_proto::motion_planning::v1::MotionPlanningRequest& request,
    const absl::flat_hash_set<uint32_t>& motion_segment_ids_to_keep,
    absl::Span<const PathSegment> path_segments) {
  INTR_ASSIGN_OR_RETURN(
      intrinsic_proto::motion_planning::v1::MotionSpecification
          new_motion_specification,
      GetMotionSpecificationFromPathSegments(
          request.motion_specification(), motion_segment_ids_to_keep,
          path_segments,
          request.robot_specification().robot_reference().object_id()));

  INTR_ASSIGN_OR_RETURN(
      MotionPlanner::PlanTrajectoryResult result,
      motion_planner.PlanTrajectory(
          object_world, request.robot_specification(), new_motion_specification,
          request.motion_planner_config(), /*run_time_flags=*/std::nullopt));
  // `num_motion_segments` intentionally reflects the original request's total
  // segments rather than the expanded segments in `new_motion_specification`.
  const int num_motion_segments =
      request.motion_specification().motion_segments_size();
  result.motion_planning_statistics.num_motion_segments = num_motion_segments;
  result.motion_planning_statistics.path_planning_statistics
      .num_motion_segments = num_motion_segments;
  return result;
}

}  // namespace intrinsic
