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

#ifndef INTRINSIC_MOTION_PLANNING_TRAJECTORY_PLANNING_PATH_REFINEMENT_PATH_POLYLINE_H_
#define INTRINSIC_MOTION_PLANNING_TRAJECTORY_PLANNING_PATH_REFINEMENT_PATH_POLYLINE_H_

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/kinematics/types/cartesian_limits.h"
#include "intrinsic/kinematics/types/joint_limits.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/motion_planning/path_planning/path_segment.h"

#include "intrinsic/motion_planning/proto/v1/motion_events.pb.h"

#include "intrinsic/motion_planning/trajectory_planning/topp/path_sample.h"
#include "intrinsic/util/fixed_vector.h"

namespace intrinsic {
namespace topp {

// The geometric processing mode to apply when creating a `PathPolyline`.
enum class PathProcessingMode {
  // Vertices are placed exactly at the path segment configurations without any
  // further processing.
  kRaw,

  // Blending endpoints are added around path segment configurations to
  // represent blending regions. These additional blending endpoints are placed
  // at a distance from the relative corner that is equal to the corner blending
  // deviation and at most to a third of the distance from the next/previous
  // corner. The blending endpoints have zero blending deviation and are set
  // with the path segment ID of their closest corners. In case a corner has two
  // segment IDs (path segment switch), the ID is taken, which fulfills the
  // point ordering. Joint and Cartesian limits are also taken from the closest
  // corner, unless the blending endpoints precede a corner corresponding to a
  // segment switch. In this case, they are both given the limits from the
  // corner at segment switch. Sequence of corners for which
  // `.is_on_blending_segment` is true are considered like a single corner;
  // thus, blending endpoints are only inserted before and after the sequence.
  kAddBlendingEndpoints,

  // Collinear points are filtered out, and then blending endpoints are added.
  // (Recommended mode for constructing B-spline control polygons). Note that
  // filtering out a point means that all the information related to that point

  // is lost, including its motion events.


  kFilterCollinearAndAddBlendingEndpoints,
};

// Options to customize the construction of the `PathPolyline`.
struct PathPolylineOptions {
  PathProcessingMode mode = PathProcessingMode::kRaw;

  // The angle tolerance used to check whether three joint configurations `q_a`,
  // `q_b`, and `q_c` are collinear in the Euclidean sense (only used in
  // `kFilterCollinearAndAddBlendingEndpoints` mode). The tolerance is intended
  // to be applied to the angle between the line segments `q_a`-`q_b` and
  // `q_b`-`q_c`.
  double collinearity_angle_tolerance_rad = 1.0e-4;  // ~0.005 deg

  // The minimum distance from a corner to add blending endpoints around it.
  // Insertion of the blending endpoints is skipped for those corners whose
  // distance from both adjacent corners is smaller than this threshold.
  double min_distance_for_blending_endpoint_addition_rad = 0.0;
};

// Holds a point of a joint-space piecewise linear path (`PathPolyline`) and its
// planning-related properties.
struct PathPolylinePoint {
  // The point coordinates.
  eigenmath::VectorNd joint_configuration;

  // The allowed blending deviation (radius) at the point [rad].
  double blending_deviation_rad;

  // The joint limits associated with the point.
  JointLimits joint_limits;

  // The Cartesian limits associated with the point.
  CartesianLimits cartesian_limits;

  // The transformation from tip to target frame associated with the point.
  Pose3d tip_t_target;

  // The ID(s) of the path segment(s) the point belongs to. A point might belong
  // to two path segments, e.g., if it is located right at their connection.
  FixedVector<std::string, 2> path_segment_ids;

  // Indicates whether the segment the point belongs to is a blending segment.
  bool is_on_blending_segment;


  // The list of motion events associated with the point.
  std::vector<intrinsic_proto::motion_planning::v1::MotionEvent> motion_events;

};

// A joint-space piecewise linear path.
class PathPolyline {
 public:
  // Creates a `PathPolyline` from a sequence of `path_segments` and applies
  // the requested geometric processing `options`.
  //
  // Specifically, it performs the following steps and checks:
  // - Performs sanity checks on each segment and verifies that the segments
  //   are connected.
  // - Removes duplicated points (using `kJointEqualitySegmentMarginRad` at
  //   segment connections and `kJointEqualityNumericalMarginRad` within a
  //   segment), considering the least restrictive blending deviation and the
  //   most conservative joint/Cartesian limits.
  // - Assigns the IDs from both segments to points at the connection between
  //   two segments.
  static absl::StatusOr<std::unique_ptr<PathPolyline>> Create(
      absl::Span<const PathSegment> path_segments,
      const PathPolylineOptions& options = {});

  // Samples the `PathPolyline` at the path variable values provided in
  // `sorted_path_variables`.
  //
  // Returns a vector of `PathSample`s with planning data (e.g., limits)
  // corresponding to the preceding `PathPolylinePoint`, or an error status if
  // `sorted_path_variables` are empty, not sorted, or contain values outside
  // the valid range [0, path length].
  absl::StatusOr<std::vector<PathSample>> GetSamples(
      absl::Span<const double> sorted_path_variables) const;

  std::vector<PathPolylinePoint> GetPoints() const { return points_; }

  absl::StatusOr<double> GetLengthAtIndex(int index) const;

  double GetLength() const { return point_distances_from_start_.back(); };

  size_t GetNumPoints() const { return points_.size(); }

 private:
  PathPolyline(absl::Span<const PathPolylinePoint> points,
               absl::Span<const double> points_distance_from_start)
      : points_(points.begin(), points.end()),
        point_distances_from_start_(points_distance_from_start.begin(),
                                    points_distance_from_start.end()) {}

  std::vector<PathPolylinePoint> points_;
  std::vector<double> point_distances_from_start_;
};

}  // namespace topp
}  // namespace intrinsic

#endif  // INTRINSIC_MOTION_PLANNING_TRAJECTORY_PLANNING_PATH_REFINEMENT_PATH_POLYLINE_H_
