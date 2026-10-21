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

#ifndef INTRINSIC_MOTION_PLANNING_TRAJECTORY_PLANNING_PATH_REFINEMENT_SPLINE_BASED_PATH_REFINEMENT_H_
#define INTRINSIC_MOTION_PLANNING_TRAJECTORY_PLANNING_PATH_REFINEMENT_SPLINE_BASED_PATH_REFINEMENT_H_

#include <memory>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "intrinsic/math/spline/bspline.h"
#include "intrinsic/math/spline/bspline_sampler.h"
#include "intrinsic/math/spline/bspline_utils.h"
#include "intrinsic/motion_planning/path_planning/path_segment.h"

#include "intrinsic/motion_planning/proto/v1/motion_events.pb.h"

#include "intrinsic/motion_planning/trajectory_planning/topp/path_sample.h"

namespace intrinsic {
namespace topp {

// The minimum distance between corners of the spline polyline to insert the
// additional blending endpoints regulating start and end of blending sections.
// Inserting the blending endpoints between corners that are too close can
// cause highly oscillating curvature profiles that can lead to excessively long
// trajectory durations when trying to optimize a motion trajectory
// (go/intrinsic-bspline-inner-point-removal-on-dense-paths).
constexpr double kMinCornerDistanceForBlendingEndpointsInsertion = 0.3;

// Default knot vector selection method for the spline-based path refinement.
constexpr BSplineKnotVectorSelection kDefaultKnotVectorSelection =
    BSplineKnotVectorSelection::kCentripetalKnotVector;

// Settings struct to parameterize the spline-based path refinement.
struct SplineBasedPathRefinementSettings {
  // Method to construct the spline knot vector.
  BSplineKnotVectorSelection knot_vector_selection =
      kDefaultKnotVectorSelection;

  // Whether to zero tangent, curvature and third path derivative at the path
  // boundaries (start/end).
  bool zero_all_path_derivatives_at_path_boundaries = true;

  // Minimum scaling factor for the spline's knot vector. The spline's knot
  // vector is scaled by the maximum between this factor and the total polyline
  // length, which allows to achieve a better conditioning of the path
  // derivatives.
  double minimum_knot_vector_scaling_factor = 0.0;

  // Enables denser sampling close to the path boundaries, independently of the
  // used sampler. Note that the final density of the samples
  // returned by the path refinement will neverthless be regulated by the
  // provided minimum sampling step.
  bool enable_denser_sampling_at_path_boundaries = false;
};

// Struct that holds the result of the spline-based path refinement algorithm.
// This includes the `path_samples` that describe the path, its derivatives and
// limits to be respected along it. In addition, it contains the `spline` which
// is a continuous representation of the path.

// Finally, `motion_events` contains the list of events to be triggered along
// the path. Each motion event returned from this function will have its
// `keypoint_joint_path_variable_from_start` set while other keypoint values
// will not be changed.

struct SplineBasedPathRefinementResult {
  std::vector<PathSample> path_samples;
  std::unique_ptr<BSplineNd> spline;

  std::vector<intrinsic_proto::motion_planning::v1::MotionEvent> motion_events;

};

// Performs path refinement by interpolating the critical points extracted from
// `path_segments` through a 4rd order B-Spline. The B-Spline creates polynomial
// blending arcs around the critical points, and it is constructed such that
// connections between blending arcs closely approximate linear paths. This is
// achieved by inserting suitable intermediate points on the spline polyline.
// The spline is constructed using the specified
// `settings.knot_vector_selection`. Then, it is sampled using the provided
// `sampler` and `reference_sampling_step`. Note that the final sample
// distribution and density will depend on the strategy implemented by
// `sampler`, but the minimum distance between consecutive samples is guaranteed
// to be at least `min_sampling_step`.
absl::StatusOr<SplineBasedPathRefinementResult> RefinePathBSpline(
    absl::Span<const PathSegment> path_segments, double min_sampling_step,
    double reference_sampling_step, const BSplineSampler& sampler,
    const SplineBasedPathRefinementSettings& settings);

// Returns the maximum spline degree used in the spline-based path refinement.
int GetSplineBasedPathRefinementMaxSplineDegree();

}  // namespace topp
}  // namespace intrinsic

#endif  // INTRINSIC_MOTION_PLANNING_TRAJECTORY_PLANNING_PATH_REFINEMENT_SPLINE_BASED_PATH_REFINEMENT_H_
