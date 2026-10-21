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

#ifndef INTRINSIC_MOTION_PLANNING_TRAJECTORY_PLANNING_PATH_REFINEMENT_PATH_REFINEMENT_UTILS_H_
#define INTRINSIC_MOTION_PLANNING_TRAJECTORY_PLANNING_PATH_REFINEMENT_PATH_REFINEMENT_UTILS_H_

#include <memory>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/kinematics/chain.h"
#include "intrinsic/kinematics/types/joint_trajectories.h"
#include "intrinsic/math/spline/bspline.h"
#include "intrinsic/motion_planning/path_planning/path_segment.h"

#include "intrinsic/motion_planning/proto/v1/motion_events.pb.h"

#include "intrinsic/motion_planning/trajectory_planning/topp/path_sample.h"
#include "intrinsic/motion_planning/trajectory_planning/topp/topp_trajectory_result.h"

namespace intrinsic {
namespace topp {

// A threshold used to determine whether two joint configurations coincide.
// Tuned considering an accuracy requirement of 0.1 mm on Cartesian paths for
// different supported robots (ur5, mh12, mh24, n220), as well as trying to
// minimize the number of linear sections between blends during path refinement.
constexpr double kJointConfigEqualityMarginRad = 5e-5;

// A threshold used to determine whether two Cartesian poses coincide.
constexpr double kPoseEqualityMargin = 1e-4;

// Returns an exactly uniformly discretized path variable, ranging from 0.0 to
// `path_length`, with discretization `sampling_step`. `sampling_step` is
// rounded to the nearest value that achieves an integer number of samples
// within `path_length`. If `force_odd_number_of_samples` is true, the number
// of samples is forced to be odd. This is achieved by increasing the number of
// samples by 1 if the original number is even.
absl::StatusOr<std::vector<double>> ComputeUniformlyDiscretizedPathVariables(
    double path_length, double sampling_step,
    bool force_odd_number_of_samples = false);

// Filters out elements from `path_vars` to guarantee a distance between
// elements that is greater than `min_path_var_distance`. In particular, it
// iterates over `path_vars` and removes an element as soon as it is found to
// have a distance from the previous one that is less or equal to
// `min_path_var_distance`. The first and last elements are never filtered out.
absl::Status FilterOutByDistanceToNeighbour(double min_path_var_distance,
                                            std::vector<double>& path_vars);

// Adds translational Cartesian arc lengths to the `path_samples` and
// `trajectory` based on the provided `chain`. Will return an error if the
// `path_samples` and `trajectory` have different sizes, or if the
// `path_samples` has fewer than 2 samples.
absl::Status AddTranslationalCartesianArcLengthsToPathAndTrajectory(
    const kinematics::Chain& chain, std::vector<PathSample>& path_samples,
    JointTrajectoryPVA& trajectory);


// Adds keypoint `keypoint_time_from_start` to each event in `motion_events`
// based on the provided `topp_trajectory_result`. The
// `keypoint_time_from_start` is calculated by extracting it from the
// `BSplineSquaredPathVelocity` in the `topp_trajectory_result`.  This method
// expects that the `motion_events` have joint path variable data.
absl::Status AddEventKeypointTime(
    const ToppTrajectoryResult& topp_trajectory_result,
    absl::Span<intrinsic_proto::motion_planning::v1::MotionEvent>
        motion_events);

// Adds keypoint `keypoint_cartesian_arc_length_from_start_meters` to each event
// in `motion_events` based on the provided `path_samples`. The
// `keypoint_cartesian_arc_length_from_start_meters` is calculated by linearly
// interpolating the `s_c` variable in the `path_samples` in relation to the
// path variable. This method expects that the `motion_events` have joint path
// variable data which is used to interpolate `s_c`. Returns an error if the
// `path_samples` has fewer than 2 samples.
absl::Status AddEventKeypointCartesianArcLength(
    absl::Span<const PathSample> path_samples,
    absl::Span<intrinsic_proto::motion_planning::v1::MotionEvent>
        motion_events);


// Computes the length of the path defined by the `path_segments`. The length is
// computed on the polyline defined by the sequence of joint configurations
// in the `path_segments`.
absl::StatusOr<double> ComputePathLength(
    absl::Span<const PathSegment> path_segments);

// Same as above, but for a vector of `path_waypoints`.
absl::StatusOr<double> ComputePathLength(
    absl::Span<const eigenmath::VectorNd> path_waypoints);

// Same as above, but for a `path_samples`.
absl::StatusOr<double> ComputePathLength(
    absl::Span<const PathSample> path_samples);

}  // namespace topp
}  // namespace intrinsic

#endif  // INTRINSIC_MOTION_PLANNING_TRAJECTORY_PLANNING_PATH_REFINEMENT_PATH_REFINEMENT_UTILS_H_
