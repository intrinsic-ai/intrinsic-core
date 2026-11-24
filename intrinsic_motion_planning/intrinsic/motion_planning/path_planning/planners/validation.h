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

#ifndef INTRINSIC_MOTION_PLANNING_PATH_PLANNING_PLANNERS_VALIDATION_H_
#define INTRINSIC_MOTION_PLANNING_PATH_PLANNING_PLANNERS_VALIDATION_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/kinematics/types/joint_limits.h"
#include "intrinsic/motion_planning/path_planning/kinematics_system_proxy.h"
#include "intrinsic/motion_planning/path_planning/path_planner_definitions.h"
#include "intrinsic/motion_planning/path_planning/path_segment.h"
#include "intrinsic/motion_planning/path_planning/validators.h"
#include "intrinsic/motion_planning/proto/v1/motion_planner_service.pb.h"
#include "intrinsic/motion_planning/proto/v1/motion_planning_error.pb.h"
#include "intrinsic/motion_planning/proto/v1/motion_specification.pb.h"
#include "intrinsic/motion_planning/trajectory_planning/topp/path_sample.h"
#include "intrinsic/world/collision/collision_context.pb.h"
#include "intrinsic/world/objects/object_world.h"
#include "intrinsic/world/objects/world_object_internal.h"
#include "intrinsic/world/proto/collision_checker_config.pb.h"

// This file defines functions that can be used for validating paths in various
// ways. It is just for convenience as many PathPlanners perform similar
// checks on their input.

namespace intrinsic {
// Validates the point and segments in a path using the validator given by the
// proxy.
absl::StatusOr<bool> ValidatePath(const EdgeValidator& edge_validator,
                                  const PointValidator& point_validator,
                                  const std::vector<eigenmath::VectorXd>& path);

// Returns ok if the path
//   * has at least two points
//   * the first and last points are within limits
//   * the first and last points are valid
// and otherwise returns an appropriate error message.
absl::Status ValidateWithinLimitsAndValidStartAndEnd(
    const PointPath& path, const KinematicsSystemProxy& proxy);

// Returns ok if the configuration is valid. Otherwise, it returns an error
// describing the problem (e.g. the collisions causing the configuration to not
// be valid).
absl::Status ValidateConfiguration(const KinematicsSystemProxy& proxy,
                                   const eigenmath::VectorXd& configuration);

// Checks the line segment between start and end up to the given resolution and
// returns true if the passed `point_validator` returns true for all the points
// checked.
absl::StatusOr<bool> CheckEdgeWithFunction(
    const eigenmath::VectorXd& start, const eigenmath::VectorXd& end,
    double resolution, const PointValidator& point_validator);

// Splits a path at its points to create a list of subpaths are each completely
// valid. The subpaths are returned as lists of indices to make it easier for
// the caller to determine which points were left out. This function is
// typically useful for the case where one wants to identify the already valid
// segments of a path and then examine the gaps to perform some operation to
// fill them in.
//
// Note, the beginning point of the first subpath and the final point of the
// last subpath may not always correspond to the first and last points in the
// original path respectively.
//
// If validity represents a connection, you can think of this function as
// returning the connected components of the path.
//
// To give an example, consider the following path
//
//     A -> B -> C -> D -> E -> F,
//
// where point B and segment D -> E are invalid. In this case, the function
// would return
//
//     ((A), (C, D), (E, F))
//
// which has the invalid point B removed and the invalid segment D -> E broken.
absl::StatusOr<std::vector<std::vector<int>>> GetValidPathSegments(
    const PointPath& path, const PointValidator& point_validator,
    const EdgeValidator& edge_validator);

// Information about an invalid path sample, including details of the failure
// and which proxies detected it.
struct InvalidPathSampleInfo {
  // Details of the invalid joint configuration.
  InvalidJointConfigurationInfo invalid_config_info;

  // The index of the proxy in the input `proxies` span that detected this
  // invalid sample.
  int proxy_index;
};

// Searches for invalid path samples.
//
// Checks if each element in `path_samples` is valid with respect to the
// settings of all the kinematics system proxies it is associated with.
//
// A path sample can be associated with one or multiple proxies, e.g., in case
// of blending between different motion segments. The mapping is specified by
// `path_segments_id_to_proxy_index_map`, which maps each path segment ID to a
// vector of indices into `proxies`.
//
// Consecutive samples on the same path segment are additionally edge-validated
// for all associated proxies using `collision_check_spacing`.
//
// Deduplication: Multiple invalid configurations along the same edge (mapping
// to the same path sample index) and the same configuration flagged by multiple
// proxies are counted once. We record only the first invalid configuration that
// maps to that path sammple index.
//
// Validation stops when all samples have been checked or `max_invalid_results`
// invalid samples have been found.
absl::StatusOr<std::vector<InvalidPathSampleInfo>> FindInvalidPathSamples(
    absl::Span<const topp::PathSample> path_samples,
    absl::Span<const std::unique_ptr<KinematicsSystemProxy>> proxies,
    const absl::flat_hash_map<std::string, std::vector<int>>&
        path_segments_id_to_proxy_index_map,
    double collision_check_spacing,
    std::optional<int> max_invalid_results = std::nullopt);

// Result of validating path samples against kinematics proxies.
struct PathValidationResult {
  std::optional<absl::Status> validation_status = std::nullopt;

  bool IsValid() const { return !validation_status.has_value(); }
};

// Similar to FindInvalidPathSamples. Returns early if an invalid
// configuration is found.
absl::StatusOr<PathValidationResult> ValidatePathSamples(
    absl::Span<const topp::PathSample> path_samples,
    absl::Span<const std::unique_ptr<KinematicsSystemProxy>> proxies,
    const absl::flat_hash_map<std::string, std::vector<int>>&
        path_segments_id_to_proxy_index_map,
    double collision_check_spacing);

struct CheckLimitsAndCollisionsForPointPathResult {
  // true if all points in the given path are within limits and collisions free.
  const bool is_valid;
  // Empty if the path is valid.
  const std::string debug_message;
  // The structured reason why the path is not valid. Unset if the path is
  // valid, or if the check that failed did not produce a structured error
  // (such as a failure on an intermediate waypoint or along an edge between
  // waypoints).
  const std::optional<intrinsic_proto::motion_planning::v1::MotionPlanningError>
      error = std::nullopt;
};

// Check if all points in the given path are within limits and collisions free.
absl::StatusOr<CheckLimitsAndCollisionsForPointPathResult>
CheckLimitsAndCollisionsForPointPath(const KinematicsSystemProxy& proxy,
                                     const PointPath& path,
                                     double collision_check_spacing);

// Check if the given configuration is within limits and collisions free.
absl::StatusOr<CheckLimitsAndCollisionsForPointPathResult>
CheckLimitsAndCollisionsForConfiguration(
    const KinematicsSystemProxy& proxy,
    const eigenmath::VectorXd& configuration);

// Return true if all path segments are within limits and collisions free based
// on the given limits and rule sets stored in the path segments. False
// otherwise.
absl::StatusOr<bool> CheckLimitsAndCollisionsForPathSegments(
    const object_world::ObjectWorld& object_world,
    const object_world::KinematicObject& robot,
    const intrinsic_proto::world::CollisionCheckerConfig&
        collision_checker_config,
    absl::Span<const PathSegment> path_segments,
    double collision_check_spacing);

PathValidator CreateDefaultPathValidator(const KinematicsSystemProxy& proxy,
                                         double collision_check_spacing);

// Takes a path validation `invalid_config_info` and proxy and generates a
// status with a useful error message.
absl::StatusOr<absl::Status> MakeValidationErrorStatus(
    const InvalidJointConfigurationInfo& invalid_config_info,
    const KinematicsSystemProxy& proxy);

}  // namespace intrinsic

#endif  // INTRINSIC_MOTION_PLANNING_PATH_PLANNING_PLANNERS_VALIDATION_H_
