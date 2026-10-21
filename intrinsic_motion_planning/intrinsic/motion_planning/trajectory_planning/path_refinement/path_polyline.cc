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

#include "intrinsic/motion_planning/trajectory_planning/path_refinement/path_polyline.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <memory>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/types/span.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/utils/realtime_status_macro.h"
#include "intrinsic/kinematics/types/check_joint_limits.h"
#include "intrinsic/math/almost_equals.h"
#include "intrinsic/math/spline/bspline_utils.h"
#include "intrinsic/math/time_series_utils.h"
#include "intrinsic/motion_planning/trajectory_planning/path_refinement/path_refinement_utils.h"
#include "intrinsic/motion_planning/trajectory_planning/topp/path_sample.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic {
namespace topp {

namespace {

constexpr double kNumericalTolerance = 1e-10;
constexpr double kJointEqualitySegmentMarginRad = kJointConfigEqualityMarginRad;
constexpr double kJointEqualityNumericalMarginRad = kNumericalTolerance;
constexpr double kDefaultPathVariableTolerance = 1e-4;

// Validates the structural and numerical consistency of a `PathSegment`.
//
// Specifically, it checks that:
// - The list of joint configurations is not empty.
// - The number of joint configurations matches the number of joint blending
//   parameters.
// - Both the joint limits and Cartesian limits are valid.
// - For each joint configuration:
//   - The joint vector is non-empty and matches the dimension (size) of the
//     first configuration.
//   - The joint values are within the specified joint limits.
// - For each joint blending parameter:
//   - The value is strictly positive (> 0.0).
//
// Returns `absl::OkStatus()` if all checks pass, or
// `absl::InvalidArgumentError` otherwise.
absl::Status ValidatePathSegment(const PathSegment& segment) {
  if (segment.joint_configurations.empty()) {
    return absl::InvalidArgumentError(
        "Path segment has an empty sequence of joint configurations.");
  }

  if (segment.joint_configurations.size() !=
      segment.joint_blending_parameter_rad.size()) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Path segment joint configurations and blending deviations must have "
        "the same size. Got ",
        segment.joint_configurations.size(), " joint configurations and ",
        segment.joint_blending_parameter_rad.size(), " blending parameters."));
  }

  if (!segment.joint_limits.IsValid()) {
    return absl::InvalidArgumentError("Path segment joint limits are invalid.");
  }

  if (!segment.cartesian_limits.IsValid()) {
    return absl::InvalidArgumentError(
        "Path segment Cartesian limits are invalid.");
  }

  for (int i = 0; i < segment.joint_configurations.size(); ++i) {
    if (segment.joint_configurations[i].size() == 0) {
      return absl::InvalidArgumentError(
          absl::StrCat("Path segment joint configurations have an empty "
                       "element at index ",
                       i, "."));
    }

    if (segment.joint_configurations[i].size() !=
        segment.joint_configurations.front().size()) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Size mismatch in path segment joint configurations. Need ",
          segment.joint_configurations.front().size(),
          " joint values but joint configuration at index ", i, " has ",
          segment.joint_configurations[i].size(), "."));
    }

    INTRINSIC_RT_ASSIGN_OR_RETURN(
        const auto joint_config_within_limits,
        IsWithinLimits(segment.joint_configurations[i], segment.joint_limits));
    if (!joint_config_within_limits) {
      return absl::InvalidArgumentError(
          absl::StrCat("Path segment joint configurations have an element "
                       "violating segment joint limits at index ",
                       i, "."));
    }

    if (segment.joint_blending_parameter_rad[i] <= 0.0) {
      return absl::InvalidArgumentError(
          absl::StrCat("Path segment blending deviations have a non positive "
                       "element at index ",
                       i, "."));
    }
  }

  return absl::OkStatus();
}

// Validates that the provided path `segment` is compatible with a
// `previous_segment`, i.e., it has the same number of joints and starts from
// the same configuration `previous_segment` ends.
// Validates that a `PathSegment` is compatible and continuous with a
// `previous_segment`.
//
// Specifically, it checks that:
// - The current segment's joint configurations are not empty.
// - Both segments have joint configurations of the same dimension.
// - The end joint configuration of the previous segment is close enough (within
//   `kJointConfigEqualityMarginRad`) to the start joint configuration of the
//   current segment.
//
// Returns `absl::OkStatus()` if all checks pass, `absl::InternalError` if the
// current segment is empty, or `absl::InvalidArgumentError` if compatibility or
// continuity is violated.
absl::Status ValidateSegmentCompatibility(const PathSegment& segment,
                                          const PathSegment& previous_segment) {
  if (segment.joint_configurations.empty()) {
    return absl::InternalError(
        "Cannot validate segment compatibility for empty joint "
        "configurations.");
  }
  const int expected_joint_size =
      previous_segment.joint_configurations.front().size();
  if (segment.joint_configurations.front().size() != expected_joint_size) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Path segments must contain joint configurations of the same "
        "size. Got joint configurations of size ",
        segment.joint_configurations.front().size(), ", need ",
        expected_joint_size, "."));
  }

  const double distance_btw_boundary_points =
      (segment.joint_configurations.front() -
       previous_segment.joint_configurations.back())
          .norm();
  if (distance_btw_boundary_points > kJointConfigEqualityMarginRad) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Continuity of path segments violated. End joint configuration of "
        "previous segment is different from the start configuration of current "
        "segment: distance is ",
        distance_btw_boundary_points, "."));
  }

  return absl::OkStatus();
}

// Returns the most conservative joint limits (smallest upper bounds, largest
// lower bounds) between `a` and `b`.
//
// Specifically, it checks that:
// - Both sets of limits are valid.
// - Both sets of limits have the same number of joints (dimension).
//
// Returns the intersection of the limits, or `absl::InternalError` if
// validation fails.
absl::StatusOr<JointLimits> TakeMostConservativeLimitsBetween(
    const JointLimits& a, const JointLimits& b) {
  if (!a.IsValid()) {
    return absl::InternalError(
        "Invalid joint limits passed as first argument.");
  }
  if (!b.IsValid()) {
    return absl::InternalError(
        "Invalid joint limits passed as second argument.");
  }
  if (a.max_position.size() != b.max_position.size()) {
    return absl::InternalError(absl::StrCat(
        "The two sets of joint limits must have the same size. Got ",
        a.max_position.size(), " != ", b.max_position.size(), "."));
  }

  JointLimits joint_limits;
  joint_limits.max_position = a.max_position.cwiseMin(b.max_position);
  joint_limits.min_position = a.min_position.cwiseMax(b.min_position);
  joint_limits.max_velocity = a.max_velocity.cwiseMin(b.max_velocity);
  joint_limits.max_acceleration =
      a.max_acceleration.cwiseMin(b.max_acceleration);
  joint_limits.max_jerk = a.max_jerk.cwiseMin(b.max_jerk);
  joint_limits.max_torque = a.max_torque.cwiseMin(b.max_torque);

  return joint_limits;
}

// Returns the most conservative Cartesian limits (smallest upper bounds,
// largest lower bounds) between `a` and `b`.
//
// Specifically, it checks that:
// - Both sets of limits are valid.
//
// Returns the intersection of the limits, or `absl::InternalError` if
// validation fails.
absl::StatusOr<CartesianLimits> TakeMostConservativeLimitsBetween(
    const CartesianLimits& a, const CartesianLimits& b) {
  if (!a.IsValid()) {
    return absl::InternalError(
        "Invalid Cartesian limits passed as first argument.");
  }
  if (!b.IsValid()) {
    return absl::InternalError(
        "Invalid Cartesian limits passed as second argument.");
  }

  CartesianLimits cart_limits;
  cart_limits.max_translational_position =
      a.max_translational_position.cwiseMin(b.max_translational_position);
  cart_limits.min_translational_position =
      a.min_translational_position.cwiseMax(b.min_translational_position);
  cart_limits.max_translational_velocity =
      a.max_translational_velocity.cwiseMin(b.max_translational_velocity);
  cart_limits.min_translational_velocity =
      a.min_translational_velocity.cwiseMax(b.min_translational_velocity);
  cart_limits.max_translational_acceleration =
      a.max_translational_acceleration.cwiseMin(
          b.max_translational_acceleration);
  cart_limits.min_translational_acceleration =
      a.min_translational_acceleration.cwiseMax(
          b.min_translational_acceleration);
  cart_limits.max_translational_jerk =
      a.max_translational_jerk.cwiseMin(b.max_translational_jerk);
  cart_limits.min_translational_jerk =
      a.min_translational_jerk.cwiseMax(b.min_translational_jerk);
  cart_limits.max_rotational_velocity =
      std::min(a.max_rotational_velocity, b.max_rotational_velocity);
  cart_limits.max_rotational_acceleration =
      std::min(a.max_rotational_acceleration, b.max_rotational_acceleration);
  cart_limits.max_rotational_jerk =
      std::min(a.max_rotational_jerk, b.max_rotational_jerk);

  return cart_limits;
}

// Validates that all points in a path polyline have consistent and non-empty
// joint configurations.
//
// Specifically, it checks that:
// - The first point in the polyline has a non-empty joint configuration.
// - Every subsequent point in the polyline has a joint configuration of the
//   exact same size.
//
// Returns `absl::OkStatus()` if all checks pass, or
// `absl::InvalidArgumentError` otherwise.
absl::Status ValidatePathPolylineJointConfigurations(
    absl::Span<const PathPolylinePoint> polyline_points) {
  const size_t num_dof = polyline_points.front().joint_configuration.size();
  if (num_dof == 0) {
    return absl::InvalidArgumentError(
        "First sample of polyline has zero size.");
  }
  for (int i = 0; i < polyline_points.size(); ++i) {
    if (polyline_points[i].joint_configuration.size() != num_dof) {
      return absl::InvalidArgumentError(
          absl::StrCat("Polyline points must contain joint configurations of "
                       "the same size. Got ",
                       polyline_points[i].joint_configuration.size(),
                       " at index ", i, ". Expected ", num_dof, "."));
    }
  }

  return absl::OkStatus();
}
// Returns whether the point at `corner_idx` is the start of a blending segment
// sequence.
//
// It checks that:
// - The input points are not empty and `corner_idx` is within valid bounds.
// - The segment IDs for the point and its predecessor are not empty.
//
// A point is considered the start of a blending segment sequence if it is
// marked as being on a blending segment and:
// - It is the first point in the polyline, or the preceding point is not on a
//   blending segment.
// - Or, if the preceding point is also on a blending segment, the current point
//   belongs to a new segment ID and is not a continuation of the previous
//   segment.
//
// Returns `true` or `false` on success, or an error status if any validation
// fails.
absl::StatusOr<bool> IsCornerBlendingSegmentStart(
    const absl::Span<const PathPolylinePoint> points, const int corner_idx) {
  if (points.empty()) {
    return absl::InternalError("Polyline points must not be empty.");
  }
  if (corner_idx < 0 || corner_idx > points.size() - 1) {
    return absl::InvalidArgumentError(
        absl::StrCat("Provided indices are out of range. Got ", corner_idx,
                     ", valid range is [0, ", points.size() - 1, "]."));
  }

  const PathPolylinePoint& corner = points[corner_idx];
  if (corner.path_segment_ids.empty()) {
    return absl::InternalError("Path segment IDs must not be empty.");
  }

  if (!corner.is_on_blending_segment) {
    return false;
  }
  // Point is on blending segment. Check if it is the first of a sequence.
  if (corner_idx == 0 || !points[corner_idx - 1].is_on_blending_segment) {
    // Point is on blending segment but previous point is not.
    return true;
  }
  // Sequence of points on blending segment. Check if the point exclusively
  // belongs to a new segment or is the continuation / end of the previous one.
  const PathPolylinePoint& prev_corner = points[corner_idx - 1];
  if (prev_corner.path_segment_ids.empty()) {
    return absl::InternalError("Path segment IDs must not be empty.");
  }
  const bool is_corner_new_segment_start =
      corner.path_segment_ids.back() != prev_corner.path_segment_ids.back();
  const bool is_corner_on_previous_blending_segment =
      corner.path_segment_ids.front() == prev_corner.path_segment_ids.back();
  return is_corner_new_segment_start && !is_corner_on_previous_blending_segment;
}

// Returns whether the point at `corner_idx` is the end of a blending segment
// sequence.
//
// It checks that:
// - The input points are not empty and `corner_idx` is within valid bounds.
// - The segment IDs for the point and its successor are not empty.
//
// A point is considered the end of a blending segment sequence if it is marked
// as being on a blending segment AND:
// - It is the last point in the polyline, or the succeeding point is not on a
//   blending segment.
// - Or, if the succeeding point is also on a blending segment, the current
//   point belongs to a segment ID that is ending, and is not a
//   continuation/transition into the next segment.
//
// Returns `true` or `false` on success, or an error status if any validation
// fails.
absl::StatusOr<bool> IsCornerBlendingSegmentEnd(
    const absl::Span<const PathPolylinePoint> points, const int corner_idx) {
  if (points.empty()) {
    return absl::InternalError("Polyline points must not be empty.");
  }
  if (corner_idx < 0 || corner_idx > points.size() - 1) {
    return absl::InvalidArgumentError(
        absl::StrCat("Provided indices are out of range. Got ", corner_idx,
                     ", valid range is [0, ", points.size() - 1, "]."));
  }

  const PathPolylinePoint& corner = points[corner_idx];
  if (corner.path_segment_ids.empty()) {
    return absl::InternalError("Path segment IDs must not be empty.");
  }

  if (!corner.is_on_blending_segment) {
    return false;
  }
  // Point is on blending segment. Check if it is the last of a sequence.
  if (corner_idx == points.size() - 1 ||
      !points[corner_idx + 1].is_on_blending_segment) {
    // Point is on blending segment but next point is not.
    return true;
  }

  // Sequence of points on blending segment. Check if the point exclusively
  // belong to the previous segment or is the transition / start point of the
  // next one.
  const PathPolylinePoint& next_corner = points[corner_idx + 1];
  if (next_corner.path_segment_ids.empty()) {
    return absl::InternalError("Path segment IDs must not be empty.");
  }
  const bool is_corner_segment_end =
      corner.path_segment_ids.front() != next_corner.path_segment_ids.front();
  const bool is_corner_on_next_blending_segment =
      corner.path_segment_ids.back() == next_corner.path_segment_ids.front();
  return is_corner_segment_end && !is_corner_on_next_blending_segment;
}

// Extracts the sequence of `PathPolylinePoint`s from the provided
// `path_segments`. Performs sanity checks on each segment, verifies that the
// segments are connected and removes duplicated points (at segment connection
// or within the segment). When resolving duplicates, the least restrictive
// blending deviation is considered. In case two consecutive path segments
// present different joint/Cartesian limits, the most conservative limits are
// considered for the point at the segment connection. Points at the connection
// between two segments are assigned the IDs from both segments.
// Extracts a sequence of `PathPolylinePoint`s from a list of `PathSegment`s.
//
// Specifically, it performs the following steps and checks:
// - Verifies that the input path segments list is not empty.
// - Validates the structure and limits of each path segment.
// - Checks that consecutive path segments are compatible and continuous.

// - Ensures all indexed motion event indices are within valid bounds.

// - Deduplicates consecutive coincident points (using
//   `kJointEqualitySegmentMarginRad` at segment connections `i_wp == 0` to
//   match `ValidateSegmentCompatibility`, and
//   `kJointEqualityNumericalMarginRad` within a segment `i_wp > 0` so that
//   closely spaced waypoints within a segment are preserved) by:
//      - Keeping the largest (least restrictive) blending deviation.
//      - Taking the most conservative joint and Cartesian limits (at segment
//        connections).
//      - Retaining the segment IDs of both connected segments.
//      - Marking the point as blending if either of the coinciding points is on
//        a blending segment.
//
// Returns a vector of consolidated `PathPolylinePoint`s, or an error status if
// any check fails.
absl::StatusOr<std::vector<PathPolylinePoint>> CreatePathPolylinePoints(
    absl::Span<const PathSegment> path_segments) {
  if (path_segments.empty()) {
    return absl::InvalidArgumentError("Path segments are empty.");
  }

  std::vector<PathPolylinePoint> polyline_points;
  for (int segment_idx = 0; segment_idx < path_segments.size(); ++segment_idx) {
    const PathSegment& segment = path_segments[segment_idx];

    absl::Status segment_validation_result = ValidatePathSegment(segment);
    if (!segment_validation_result.ok()) {
      return absl::InvalidArgumentError(
          absl::StrCat(segment_validation_result.message(), " (at segment ",
                       segment_idx, ")"));
    }
    if (segment_idx > 0) {
      // From the second segment on, check that path segments are compatible
      // (have same number of joints and connect).
      INTR_RETURN_IF_ERROR(
          ValidateSegmentCompatibility(segment, path_segments[segment_idx - 1]))
          << " (at segment " << segment_idx << ")";
    }


    // For convenience we put the motion events into a map with the path segment
    // joint configuration indices as keys. In this way we can efficiently
    // access the motion events for a given sample index without searching the
    // vector. We also check here to make sure that the indices are valid.
    absl::flat_hash_map<
        int, std::vector<intrinsic_proto::motion_planning::v1::MotionEvent>>
        joint_configuration_index_to_motion_event;
    for (const auto& motion_event_and_joint_configuration_index :
         segment.indexed_motion_events) {
      if (motion_event_and_joint_configuration_index.joint_configuration_index <
              0 ||
          motion_event_and_joint_configuration_index
                  .joint_configuration_index >=
              segment.joint_configurations.size()) {
        return absl::InvalidArgumentError(
            absl::StrCat("Joint configuration index ",
                         motion_event_and_joint_configuration_index
                             .joint_configuration_index,
                         " is out of bounds [0, ",
                         segment.joint_configurations.size() - 1, "]."));
      }
      joint_configuration_index_to_motion_event
          [motion_event_and_joint_configuration_index.joint_configuration_index]
              .push_back(
                  motion_event_and_joint_configuration_index.motion_event);
    }


    for (int i_wp = 0; i_wp < segment.joint_configurations.size(); ++i_wp) {
      // Deduplicate coincident consecutive points within the segment and at
      // segments connection.
      // At segment connections (`i_wp == 0`), `ValidateSegmentCompatibility`
      // allows an offset up to `kJointEqualitySegmentMarginRad`
      // (`kJointConfigEqualityMarginRad`) between the last waypoint of the
      // previous segment and the first waypoint of the current segment, so
      // `kJointEqualitySegmentMarginRad` is used to merge the boundary points
      // into a single connection point.
      // Within a segment (`i_wp > 0`), only numerically identical points
      // (within `kJointEqualityNumericalMarginRad`) are deduplicated so that
      // closely spaced waypoints within a segment are not discarded.
      const double deduplication_threshold_rad =
          (i_wp == 0) ? kJointEqualitySegmentMarginRad
                      : kJointEqualityNumericalMarginRad;
      if (!polyline_points.empty() &&
          (segment.joint_configurations[i_wp] -
           polyline_points.back().joint_configuration)
                  .norm() <= deduplication_threshold_rad) {
        // Take the largest blending deviation.
        polyline_points.back().blending_deviation_rad =
            std::max(polyline_points.back().blending_deviation_rad,
                     segment.joint_blending_parameter_rad[i_wp]);
        if (i_wp == 0) {
          // At segments connection, take the most conservative joint and
          // Cartesian limits.
          INTR_ASSIGN_OR_RETURN(
              polyline_points.back().joint_limits,
              TakeMostConservativeLimitsBetween(
                  polyline_points.back().joint_limits, segment.joint_limits));
          INTR_ASSIGN_OR_RETURN(polyline_points.back().cartesian_limits,
                                TakeMostConservativeLimitsBetween(
                                    polyline_points.back().cartesian_limits,
                                    segment.cartesian_limits));
          // Add (or replace) second segment ID to the point at segments
          // connection. In case of multiple trivial segments with only one
          // point, the connection point might already have two IDs.
          if (polyline_points.back().path_segment_ids.size() < 2) {
            polyline_points.back().path_segment_ids.push_back(
                std::string(segment.GetUniqueId()));
          } else {
            polyline_points.back().path_segment_ids.back() =
                std::string(segment.GetUniqueId());
          }
        }
      } else {
        PathPolylinePoint point{
            .joint_configuration = segment.joint_configurations[i_wp],
            .blending_deviation_rad =
                segment.joint_blending_parameter_rad[i_wp],
            .joint_limits = segment.joint_limits,
            .cartesian_limits = segment.cartesian_limits,
            .tip_t_target = segment.tip_t_target,
            .path_segment_ids = {std::string(segment.GetUniqueId())},
        };
        polyline_points.push_back(std::move(point));
      }

      // Regardless of whether a new point was added or not, we need to add the
      // motion events to the last added path polyline point. This accounts for
      // the deduplication above.
      if (joint_configuration_index_to_motion_event.contains(i_wp)) {
        for (const auto& motion_event :
             joint_configuration_index_to_motion_event[i_wp]) {
          polyline_points.back().motion_events.push_back(motion_event);
        }
      }

    }
  }

  return polyline_points;
}

// Inserts blending endpoints around polyline corners.
//
// Specifically, it performs the following steps and checks:
// - Verifies that the input corners list is not empty.
// - Ensures that `min_corner_distance_for_addition_rad` is non-negative.
// - Validates the consistency of the corners' joint configurations.
// - Generates blending endpoints at a maximum of one-third the segment length
//   on both sides of each corner.
// - Inserts these blending endpoints into the polyline if the distance to the
//   adjacent corner is at least `min_corner_distance_for_addition_rad` and:
//   - `corner.is_on_blending_segment` == false.
//   - Or, the corner is the start (for left insertion) or end (for right
//   insertion) of a blending segment.
// - Assigns zero blending deviation to the newly created points, and
//   properly propagates the relevant joint limits, Cartesian limits, and
//   segment IDs (handling segment switches).
//
// Returns a vector of `PathPolylinePoint`s with the added inner points, or an
// error status.
absl::StatusOr<std::vector<PathPolylinePoint>> AddBlendingEndpoints(
    const absl::Span<const PathPolylinePoint> path_polyline_corners,
    const double min_corner_distance_for_addition_rad) {
  if (path_polyline_corners.empty()) {
    return absl::InvalidArgumentError("Polyline points must not be empty.");
  }
  if (min_corner_distance_for_addition_rad < 0) {
    return absl::InvalidArgumentError(
        absl::StrCat("Minimum corner distance for adding blending endpoints "
                     "must be positive. Got ",
                     min_corner_distance_for_addition_rad, "."));
  }
  if (path_polyline_corners.size() == 1) {
    return std::vector<PathPolylinePoint>{path_polyline_corners.front()};
  }

  INTR_RETURN_IF_ERROR(
      ValidatePathPolylineJointConfigurations(path_polyline_corners));

  // Extract all joint configurations and introduce "support" points (blending
  // endpoints) between them according to the specified blending deviations and
  // the relative distance between corners. Blending endpoints are inserted at
  // both sides of a corner, if the corner has
  // `min_corner_distance_for_addition_rad` from at least one of its neighbor
  // corners. The maximum blending endpoint distance from its relative corner is
  // a third of the length of the segment containing the blending endpoint.
  //          corner                                    next corner
  //             o----------x--------------------x----------o
  //            /   blending endpoint    blending endpoint
  //           /
  //          x blending endpoint
  //         /
  //        /
  //       x blending endpoint
  //      /
  //     /
  //    o prev corner
  std::vector<eigenmath::VectorNd> corner_configurations;
  corner_configurations.reserve(path_polyline_corners.size());
  std::vector<double> corner_blending_deviations;
  corner_blending_deviations.reserve(path_polyline_corners.size());
  for (const PathPolylinePoint& corner : path_polyline_corners) {
    corner_configurations.push_back(corner.joint_configuration);
    corner_blending_deviations.push_back(corner.blending_deviation_rad);
  }
  INTR_ASSIGN_OR_RETURN(const std::vector<eigenmath::VectorNd>
                            corner_configurations_with_blending_endpoints,
                        PolyLineToBspline3Waypoints(
                            corner_configurations, corner_blending_deviations));

  std::vector<PathPolylinePoint> result_polyline_points;
  result_polyline_points.reserve(
      corner_configurations_with_blending_endpoints.size());

  // A function to generate a blending endpoint out of its reference corner.
  // Blending endpoints get zero blending deviation, since they should be
  // matched precisely.
  auto make_blending_endpoint =
      [](const eigenmath::VectorNd& config, const PathPolylinePoint& ref_corner,
         const absl::string_view segment_id,
         const bool is_on_blending_segment) -> PathPolylinePoint {
    return PathPolylinePoint{
        .joint_configuration = config,
        .blending_deviation_rad = 0.0,
        .joint_limits = ref_corner.joint_limits,
        .cartesian_limits = ref_corner.cartesian_limits,
        .tip_t_target = ref_corner.tip_t_target,
        .path_segment_ids = {std::string{segment_id}},
        .is_on_blending_segment = is_on_blending_segment,
    };
  };

  for (int i = 0; i < path_polyline_corners.size(); ++i) {
    const PathPolylinePoint& corner = path_polyline_corners[i];
    const PathPolylinePoint& prev_corner =
        (i > 0) ? path_polyline_corners[i - 1] : corner;
    const PathPolylinePoint& next_corner =
        (i < path_polyline_corners.size() - 1) ? path_polyline_corners[i + 1]
                                               : corner;
    const double distance_from_prev_corner =
        (prev_corner.joint_configuration - corner.joint_configuration).norm();
    const double distance_from_next_corner =
        (next_corner.joint_configuration - corner.joint_configuration).norm();
    const bool valid_distance_for_left_insertion =
        distance_from_prev_corner >= min_corner_distance_for_addition_rad;
    const bool valid_distance_for_right_insertion =
        distance_from_next_corner >= min_corner_distance_for_addition_rad;
    const bool add_blending_endpoints_to_corner_not_on_blending_segment =
        !corner.is_on_blending_segment && (valid_distance_for_left_insertion ||
                                           valid_distance_for_right_insertion);
    INTR_ASSIGN_OR_RETURN(
        const bool is_blending_segment_start,
        IsCornerBlendingSegmentStart(path_polyline_corners, i));
    INTR_ASSIGN_OR_RETURN(const bool is_blending_segment_end,
                          IsCornerBlendingSegmentEnd(path_polyline_corners, i));
    const bool add_left_blending_endpoint_to_corner_on_blending_segment =
        is_blending_segment_start && valid_distance_for_left_insertion;
    const bool add_right_blending_endpoint_to_corner_on_blending_segment =
        is_blending_segment_end && valid_distance_for_right_insertion;

    if (i > 0 && (add_blending_endpoints_to_corner_not_on_blending_segment ||
                  add_left_blending_endpoint_to_corner_on_blending_segment)) {
      // Add blending endpoint on `corner`'s left side.
      const bool is_on_blending_segment =
          corner.is_on_blending_segment && prev_corner.is_on_blending_segment;
      result_polyline_points.push_back(make_blending_endpoint(
          corner_configurations_with_blending_endpoints[3 * i - 1], corner,
          corner.path_segment_ids.front(), is_on_blending_segment));
    }

    // Add the original corner. The corner configuration coincide with
    // `corner_configurations_with_blending_endpoints[3 * i]`;
    result_polyline_points.push_back(corner);

    if (i < path_polyline_corners.size() - 1 &&
        (add_blending_endpoints_to_corner_not_on_blending_segment ||
         add_right_blending_endpoint_to_corner_on_blending_segment)) {
      // Add blending endpoint on `corner`'s right side. In case of a path
      // segment switch at `corner`, the blending endpoint is given limits and
      // tip_t_target from the next corner to obtain a correct switch of
      // planning data.
      const bool path_segment_switch = corner.path_segment_ids.size() == 2;
      const PathPolylinePoint& ref_corner =
          path_segment_switch ? next_corner : corner;
      const bool is_on_blending_segment =
          corner.is_on_blending_segment && next_corner.is_on_blending_segment;
      result_polyline_points.push_back(make_blending_endpoint(
          corner_configurations_with_blending_endpoints[3 * i + 1], ref_corner,
          corner.path_segment_ids.back(), is_on_blending_segment));
    }
  }

  return result_polyline_points;
}

// Returns true if `point_a`, `point_b` and `point_c` are collinear, i.e., they
// form a line segment. `zero_angle_threshold_rad` specifies the angular
// threshold (in radians) used to determine collinearity.
absl::StatusOr<bool> IsSequenceALineSegment(
    const eigenmath::VectorNd& point_a, const eigenmath::VectorNd& point_b,
    const eigenmath::VectorNd& point_c, const double zero_angle_threshold_rad) {
  if (point_a.size() != point_b.size() || point_a.size() != point_c.size()) {
    return absl::InternalError(
        "Cannot check for collinearity: points must have the same dimension.");
  }
  const eigenmath::VectorNd ab = point_b - point_a;
  const eigenmath::VectorNd bc = point_c - point_b;
  const double ab_norm = ab.norm();
  const double bc_norm = bc.norm();

  if (AlmostEquals(ab_norm, 0.0) || AlmostEquals(bc_norm, 0.0)) {
    return true;
  }

  const double cos_angle_between_ab_and_bc =
      std::clamp(ab.dot(bc) / ab_norm / bc_norm, -1.0, 1.0);
  const double angle_between_ab_and_bc = std::acos(cos_angle_between_ab_and_bc);

  return angle_between_ab_and_bc <= zero_angle_threshold_rad;
}

// Filters a sequence of `polyline_points` by removing redundant collinear
// points. Iterating from the start of the sequence, any point that forms a
// straight line with its immediate predecessor and successor is removed. Thus,
// no more than two points exist on any single line segment after filtering.
// `zero_angle_threshold_rad` specifies the angular threshold (in radians) used
// to determine collinearity.
absl::Status FilterCollinearPoints(
    std::vector<PathPolylinePoint>& polyline_points,
    double zero_angle_threshold_rad) {
  if (polyline_points.size() < 3) {
    // At least three points are needed to apply the filter.
    return absl::OkStatus();
  }

  int write_idx = 2;
  for (int read_idx = 2; read_idx < polyline_points.size(); ++read_idx) {
    INTR_ASSIGN_OR_RETURN(
        const bool is_line_segment,
        IsSequenceALineSegment(
            polyline_points[write_idx - 2].joint_configuration,
            polyline_points[write_idx - 1].joint_configuration,
            polyline_points[read_idx].joint_configuration,
            zero_angle_threshold_rad));
    if (is_line_segment) {
      polyline_points[write_idx - 1] = std::move(polyline_points[read_idx]);
    } else {
      if (write_idx != read_idx) {
        polyline_points[write_idx] = std::move(polyline_points[read_idx]);
      }
      write_idx++;
    }
  }

  polyline_points.resize(write_idx);
  return absl::OkStatus();
}

}  // namespace

absl::StatusOr<std::unique_ptr<PathPolyline>> PathPolyline::Create(
    absl::Span<const PathSegment> path_segments,
    const PathPolylineOptions& options) {
  INTR_ASSIGN_OR_RETURN(std::vector<PathPolylinePoint> points,
                        CreatePathPolylinePoints(path_segments));
  if (points.size() < 2) {
    return absl::InvalidArgumentError(
        "Path segments result in a polyline with zero length.");
  }

  switch (options.mode) {
    case PathProcessingMode::kRaw:
      break;
    case PathProcessingMode::kAddBlendingEndpoints: {
      INTR_ASSIGN_OR_RETURN(
          points,
          AddBlendingEndpoints(
              points, options.min_distance_for_blending_endpoint_addition_rad));
      break;
    }
    case PathProcessingMode::kFilterCollinearAndAddBlendingEndpoints: {
      INTR_RETURN_IF_ERROR(FilterCollinearPoints(
          points, options.collinearity_angle_tolerance_rad));
      INTR_ASSIGN_OR_RETURN(
          points,
          AddBlendingEndpoints(
              points, options.min_distance_for_blending_endpoint_addition_rad));
      break;
    }
  }

  // Calculate cumulative distances.
  std::vector<double> point_distances_from_start;
  point_distances_from_start.reserve(points.size());
  point_distances_from_start.push_back(0.0);

  for (int i = 1; i < points.size(); ++i) {
    point_distances_from_start.push_back(
        point_distances_from_start[i - 1] +
        (points[i].joint_configuration - points[i - 1].joint_configuration)
            .norm());
  }

  // WrapUnique() due to private constructor.
  return absl::WrapUnique(new PathPolyline(points, point_distances_from_start));
}

absl::StatusOr<double> PathPolyline::GetLengthAtIndex(const int index) const {
  if (index < 0) {
    return absl::InvalidArgumentError(
        absl::StrCat("Index ", index, " is negative."));
  }
  if (index >= point_distances_from_start_.size()) {
    return absl::InvalidArgumentError(
        absl::StrCat("Index ", index, " is larger than the number of points ",
                     points_.size()));
  }
  return point_distances_from_start_[index];
}

absl::StatusOr<std::vector<PathSample>> PathPolyline::GetSamples(
    absl::Span<const double> path_variables) const {
  if (path_variables.empty()) {
    return absl::InvalidArgumentError("Path variables must not be empty.");
  }

  if (!std::is_sorted(path_variables.begin(), path_variables.end())) {
    return absl::InvalidArgumentError("Path variables must be sorted.");
  }

  std::vector<PathSample> samples;
  samples.reserve(path_variables.size());

  const double polyline_length = point_distances_from_start_.back();
  for (const double path_var : path_variables) {
    if (path_var < -kDefaultPathVariableTolerance ||
        path_var > polyline_length + kDefaultPathVariableTolerance) {
      return absl::InvalidArgumentError(
          absl::StrCat("Path variable must be between 0.0 and path length (",
                       polyline_length, "). Got ", path_var, "."));
    }
    // Handle numerical inaccuracies within tolerance.
    const double s = std::clamp(path_var, 0.0, polyline_length);

    DVLOG(2) << "path_var: " << std::setprecision(10) << s
             << " out of total_polyline_length: " << polyline_length;

    // Search for the index of the point where the polyline segment
    // corresponding to current path variable s starts. The search includes
    // handling of the special case for the rightmost end of the path variable
    // interval.
    int current_edge_start_point_index = GetLowerIndexForValue<double>(
        absl::MakeConstSpan(point_distances_from_start_), s);
    if (s >= polyline_length - kNumericalTolerance) {
      current_edge_start_point_index =
          std::max(0, static_cast<int>(points_.size()) - 2);
    }
    DVLOG(2) << "current_edge_start_point_index: "
             << current_edge_start_point_index
             << " out of  points_.size(): " << points_.size();

    const eigenmath::VectorNd current_edge_direction =
        (points_[current_edge_start_point_index + 1].joint_configuration -
         points_[current_edge_start_point_index].joint_configuration)
            .normalized();

    const int ndof = points_.front().joint_configuration.size();
    samples.push_back(PathSample{
        .s = s,
        .q = points_[current_edge_start_point_index].joint_configuration +
             (s - point_distances_from_start_[current_edge_start_point_index]) *
                 current_edge_direction,
        .qp = current_edge_direction,
        .qpp = eigenmath::VectorNd::Zero(ndof),
        .qppp = eigenmath::VectorNd::Zero(ndof),
        .joint_limits = points_[current_edge_start_point_index].joint_limits,
        .cart_limits = points_[current_edge_start_point_index].cartesian_limits,
        .tip_t_target = points_[current_edge_start_point_index].tip_t_target,
        .segment_id =
            points_[current_edge_start_point_index].path_segment_ids.back(),
    });
  }

  return samples;
}

}  // namespace topp
}  // namespace intrinsic
