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

#include "intrinsic/motion_planning/trajectory_planning/path_refinement/spline_based_path_refinement.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/types/span.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/math/almost_equals.h"
#include "intrinsic/math/linspace.h"
#include "intrinsic/math/spline/bspline.h"
#include "intrinsic/math/spline/bspline_parameter_integral_transform_function.h"
#include "intrinsic/math/spline/bspline_parameter_transform_function_integrand.h"
#include "intrinsic/math/spline/bspline_utils.h"
#include "intrinsic/math/spline/spline_parameter_converter.h"
#include "intrinsic/math/spline/spline_parameter_transform_function.h"
#include "intrinsic/motion_planning/path_planning/path_segment.h"

#include "intrinsic/motion_planning/proto/v1/motion_events.pb.h"

#include "intrinsic/motion_planning/trajectory_planning/path_refinement/path_polyline.h"
#include "intrinsic/motion_planning/trajectory_planning/path_refinement/path_refinement_utils.h"
#include "intrinsic/motion_planning/trajectory_planning/topp/path_sample.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic {
namespace topp {

namespace {

// The minimum degree of the B-spline used to refine the path. This is set to 3
// since we want to be able to compute path derivatives up to third order.
constexpr int kMinSplineDegree = 3;

// Quartic splines are used when possible because their third order path
// derivatives are continuous. This leads to smoother outputs of the subsequent
// trajectory generation step.
constexpr int kMaxSplineDegree = 4;

// The angle tolerance used to check whether three joint configurations q_a,
// q_b, and q_c are collinear in the Euclidean sense. The tolerance is intended
// to be applied to the angle between the line segments q_a-q_b and q_b-q_c.
constexpr double kCollinearityAngleToleranceRad = 1e-4;  // ~0.005 deg

// Creates the control points of the path-refining B-spline. The input polyline
// points are first filtered out to remove intermediate collinear control
// points. Then, blending endpoints are inserted between each pair of
// consecutive corners to ensure the best spline adherence to the original
// polyline. If `zero_all_path_derivatives_at_path_boundaries` is true, the
// first and last control points are duplicated to ensure that the initial and
// final samples have zero first and second order path derivatives. If the final
// number of control points is still less than `min_spline_degree` + 1, the
// initial and final control points are further duplicated.
absl::StatusOr<std::vector<eigenmath::VectorNd>>
CreatePathRefiningBSplineControlPoints(
    absl::Span<const PathSegment> path_segments,
    const bool zero_all_path_derivatives_at_path_boundaries,
    const int min_spline_degree) {
  if (min_spline_degree < 0) {
    return absl::InvalidArgumentError(
        absl::StrCat("Minimum spline degree must be non-negative. Got ",
                     min_spline_degree, "."));
  }

  // Create a `PathPolyline` filtering out collinear points and adding blending
  // endpoints. Removing collinear points prevents unnecessary path derivative
  // oscillations of the B-spline. For the same reason, we set a minimum corner
  // distance for the blending endpoint insertion.
  INTR_ASSIGN_OR_RETURN(
      std::unique_ptr<PathPolyline> control_points_polyline,
      PathPolyline::Create(
          path_segments,
          {.mode = PathProcessingMode::kFilterCollinearAndAddBlendingEndpoints,
           .collinearity_angle_tolerance_rad = kCollinearityAngleToleranceRad,
           .min_distance_for_blending_endpoint_addition_rad =
               kMinCornerDistanceForBlendingEndpointsInsertion}));

  std::vector<eigenmath::VectorNd> control_points;
  control_points.reserve(control_points_polyline->GetNumPoints());
  for (const PathPolylinePoint& point : control_points_polyline->GetPoints()) {
    control_points.push_back(point.joint_configuration);
  }

  // If requested, add duplicates at the beginning and end of the path to
  // make the initial and final samples have zero first and second order path
  // derivatives.
  if (zero_all_path_derivatives_at_path_boundaries) {
    for (int i = 0; i < 3; ++i) {
      control_points.insert(control_points.begin(), control_points.front());
      control_points.push_back(control_points.back());
    }
  }

  // If there are still less than min_spline_degree + 1 points, duplicate the
  // endpoints until the minimum number of points is reached.
  while (control_points.size() < min_spline_degree + 1) {
    control_points.insert(control_points.begin(), control_points.front());
    control_points.push_back(control_points.back());
  }

  return control_points;
}

// Creates the path-refining B-spline with the given `control_points`. The
// spline degree is the minimum of `max_spline_degree` and the number of control
// points minus one. The knot vector is selected according to
// `knot_vector_selection` and scaled by the given `knot_vector_scaling_factor`.
absl::StatusOr<std::unique_ptr<BSplineNd>> CreatePathRefiningBSpline(
    absl::Span<const eigenmath::VectorNd> control_points,
    const size_t max_spline_degree,
    const BSplineKnotVectorSelection knot_vector_selection,
    const double knot_vector_scaling_factor) {
  if (max_spline_degree < 0) {
    return absl::InvalidArgumentError(
        absl::StrCat("Maximum spline degree must be non-negative. Got ",
                     max_spline_degree, "."));
  }
  if (knot_vector_scaling_factor <= 0.0) {
    return absl::InvalidArgumentError(
        absl::StrCat("Knot vector scaling factor must be positive. Got ",
                     knot_vector_scaling_factor, "."));
  }

  const size_t spline_degree =
      std::min(max_spline_degree, control_points.size() - 1);

  INTR_ASSIGN_OR_RETURN(
      std::vector<double> knots,
      MakeKnotVector(knot_vector_selection, control_points, spline_degree));
  Eigen::Map<eigenmath::VectorNd>(knots.data(), knots.size()) *=
      knot_vector_scaling_factor / knots.back();

  auto spline = std::make_unique<BSplineNd>();
  const int ndof = control_points.front().size();
  if (!spline->Init(spline_degree, knots.size(), ndof)) {
    return absl::InternalError("Couldn't init spline.");
  }
  if (!spline->SetKnotVector(knots)) {
    return absl::InternalError("Coudn't set knot vector.");
  }
  if (!spline->SetControlPoints(control_points)) {
    return absl::InternalError("Couldn't set data points.");
  }

  return spline;
}

// Adds extra samples at the start and end of the knot vector. The number of
// samples added is equal to max degree of the spline.
absl::Status AddExtraSamplesAtPathBoundaries(
    std::vector<double>& knot_path_vars) {
  if (knot_path_vars.size() < 2) {
    return absl::InvalidArgumentError(
        "Knot path variables vector must have at least 2 elements.");
  }

  const int last_index = knot_path_vars.size() - 2;
  for (int index : {0, last_index}) {
    const double knot0 = knot_path_vars[index];
    const double knot1 = knot_path_vars[index + 1];
    for (int j = 1; j <= kMaxSplineDegree; ++j) {
      const double interp_knot =
          knot0 + (knot1 - knot0) * static_cast<double>(j) /
                      static_cast<double>(kMaxSplineDegree + 1);
      knot_path_vars.push_back(interp_knot);
    }
  }
  std::sort(knot_path_vars.begin(), knot_path_vars.end());
  return absl::OkStatus();
}


// Extracts the motion events which are associated with points on the
// `path_polyline`. The event and offset of the motion event are passed through
// and the keypoint path variable is estimated based on the spline.
absl::StatusOr<std::vector<intrinsic_proto::motion_planning::v1::MotionEvent>>
ExtractMotionEvents(const BSplineNd& spline,
                    const PathPolyline& path_polyline) {
  // We compute the arclength of the spline. We use this to generate the
  // approximate arclength at polyline points which have an associated event.
  INTR_ASSIGN_OR_RETURN(
      std::unique_ptr<SplineParameterTransformFunction>
          arc_length_parameter_transform_function,
      BSplineParameterIntegralTransformFunction<SplineTraitsNd<>>::Create(
          &spline, BSplineArcLengthIntegrand<SplineTraitsNd<>>()));
  const std::vector<double> knot_spans_endpoints =
      arc_length_parameter_transform_function->GetKnotSpansEndpoints();
  INTR_ASSIGN_OR_RETURN(const double spline_length,
                        arc_length_parameter_transform_function->Evaluate(
                            knot_spans_endpoints.back()));
  const double path_polyline_length = path_polyline.GetLength();

  // Now, we can generate the path variables for the events.
  std::vector<double> events_arc_lengths;
  std::vector<std::vector<intrinsic_proto::motion_planning::v1::MotionEvent>>
      motion_events_per_arc_length;
  for (int i = 0; const PathPolylinePoint& point : path_polyline.GetPoints()) {
    if (!point.motion_events.empty()) {
      INTR_ASSIGN_OR_RETURN(const double polyline_length_at_event,
                            path_polyline.GetLengthAtIndex(i));
      // We estimate the path variable of the event on the spline by scaling the
      // path variable of the event on the polyline by the ratio of the
      // spline length to the polyline length. This is an assumption that
      // progress along the polyline corresponds to progress along the spline:
      // s_event = spline_length * (s_event_polyline / polyline_length)
      const double polyline_progress_fraction =
          polyline_length_at_event / path_polyline_length;
      const double arc_length_at_event =
          spline_length * polyline_progress_fraction;
      events_arc_lengths.push_back(arc_length_at_event);
      // Add all the motion events corresponding to this point to the vector.
      // That way we can add the path variable for this arc_length_at_event to
      // all these  motion events. We need to do this because the conversion
      // from arc length to knot curve parameter method use below does not
      // support repeated curve parameters.
      motion_events_per_arc_length.push_back(point.motion_events);
    }
    i++;
  }
  std::vector<intrinsic_proto::motion_planning::v1::MotionEvent> motion_events;
  if (!events_arc_lengths.empty()) {
    // Now we can convert the event arclength back into the knot domain which
    // goes into the motion event.
    INTR_ASSIGN_OR_RETURN(
        const std::vector<double> knot_curve_parameters,
        ConvertToKnotCurveParameterViaSingleApproximation(
            events_arc_lengths, *arc_length_parameter_transform_function));
    for (int i = 0; i < knot_curve_parameters.size(); ++i) {
      for (const intrinsic_proto::motion_planning::v1::MotionEvent&
               motion_event : motion_events_per_arc_length[i]) {
        intrinsic_proto::motion_planning::v1::MotionEvent new_motion_event =
            motion_event;
        new_motion_event.mutable_keypoint()
            ->set_keypoint_joint_path_variable_from_start(
                knot_curve_parameters[i]);
        motion_events.push_back(new_motion_event);
      }
    }
  }
  return motion_events;
}


// Evaluates the left-side (incoming) third derivative at interior knots and
// returns it if there is a discontinuity with the right-side (outgoing) third
// derivative `qppp_out`. Returns std::nullopt if the point is continuous, not
// an interior knot, or evaluation fails.
std::optional<eigenmath::VectorNd>
ComputeIncomingThirdDerivativeIfDiscontinuous(
    const BSplineNd& spline, const double path_variable,
    const absl::Span<const double> knots,
    const absl::Span<const double> unique_knots,
    const eigenmath::VectorNd& qppp_out) {
  // A B-spline of degree `p` has `C^(p-m)` continuity across a knot of
  // multiplicity `m`.
  // For degree < 3, the third derivative is identically zero everywhere.
  // For degree >= 4 with simple interior knots (multiplicity 1), the spline
  // is at least `C^3` continuous everywhere, guaranteeing a continuous third
  // derivative across the entire domain.
  if (spline.Degree() < 3) {
    return std::nullopt;
  }
  if (spline.Degree() >= 4 &&
      knots.size() == unique_knots.size() + 2 * spline.Degree()) {
    return std::nullopt;
  }

  if (unique_knots.size() <= 2) {
    return std::nullopt;
  }

  constexpr double kEpsilonTolerance = 1.0e-9;
  constexpr double kDiscontinuityTolerance = 1.0e-6;

  auto it = absl::c_upper_bound(unique_knots, path_variable);
  std::optional<double> interior_knot;

  // Only interior points can have discontinuous derivatives.
  if (it != unique_knots.end() && !AlmostEquals(*it, unique_knots.front()) &&
      !AlmostEquals(*it, unique_knots.back()) &&
      AlmostEquals(path_variable, *it)) {
    interior_knot = *it;
  } else if (it != unique_knots.begin()) {
    auto prev_it = std::prev(it);
    if (!AlmostEquals(*prev_it, unique_knots.front()) &&
        !AlmostEquals(*prev_it, unique_knots.back()) &&
        AlmostEquals(path_variable, *prev_it)) {
      interior_knot = *prev_it;
    }
  }

  if (!interior_knot.has_value()) {
    return std::nullopt;
  }

  // At an interior knot of multiplicity `m`, continuity is `C^(p-m)`. The third
  // path derivative is continuous if and only if `p - m >= 3 (m <= p - 3)`.
  const int knot_multiplicity =
      std::count_if(knots.begin(), knots.end(),
                    [&](double k) { return AlmostEquals(k, *interior_knot); });
  if (static_cast<int>(spline.Degree()) - knot_multiplicity >= 3) {
    return std::nullopt;
  }

  const double left_eval_path_variable = std::clamp(
      *interior_knot - kEpsilonTolerance, knots.front(), knots.back());
  const int kNumDerivatives = 4;
  std::vector<eigenmath::VectorNd> left_sample(
      kNumDerivatives, eigenmath::VectorNd::Zero(qppp_out.size()));
  if (spline.EvalCurveAndDerivatives(left_eval_path_variable, &left_sample)) {
    if ((left_sample[3] - qppp_out).norm() > kDiscontinuityTolerance) {
      return left_sample[3];
    }
  }

  return std::nullopt;
}

}  // namespace

absl::StatusOr<SplineBasedPathRefinementResult> RefinePathBSpline(
    const absl::Span<const PathSegment> path_segments,
    const double min_sampling_step, const double reference_sampling_step,
    const BSplineSampler& sampler,
    const SplineBasedPathRefinementSettings& settings) {
  if (min_sampling_step < 0.0) {
    return absl::InvalidArgumentError(
        absl::StrCat("Min sampling step must be non-negative. Got ",
                     min_sampling_step, ".'"));
  }
  if (reference_sampling_step <= 0.0) {
    return absl::InvalidArgumentError(
        absl::StrCat("Reference sampling step must be strictly positive. Got ",
                     reference_sampling_step, ".'"));
  }
  if (settings.minimum_knot_vector_scaling_factor < 0.0) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Invalid minimum knot vector scaling factor. Should be >= 0, but got ",
        settings.minimum_knot_vector_scaling_factor, "."));
  }

  // In order to produce path samples with complete information we construct the
  // path refining B-spline and control polyline as separate objects.
  // In the end, we will use the control polyline to retrieve planning data
  // (e.g. joint limits) of a path sample, and the B-spline to retrieve the
  // curve points (joint position) and their derivatives.
  INTR_ASSIGN_OR_RETURN(
      std::unique_ptr<PathPolyline> path_polyline,
      PathPolyline::Create(
          path_segments, {.mode = PathProcessingMode::kAddBlendingEndpoints}));

  INTR_ASSIGN_OR_RETURN(
      const std::vector<eigenmath::VectorNd> spline_control_points,
      CreatePathRefiningBSplineControlPoints(
          path_segments, settings.zero_all_path_derivatives_at_path_boundaries,
          kMinSplineDegree));

  // Create the path-refining B-spline. The knot vector is scaled by the maximum
  // between the total polyline length and the minimum knot vector scaling
  // factor to improve the numerical conditioning of the path derivatives.
  const double path_polyline_length = path_polyline->GetLength();
  const double knot_vector_scaling_factor = std::max(
      path_polyline_length, settings.minimum_knot_vector_scaling_factor);
  INTR_ASSIGN_OR_RETURN(
      std::unique_ptr<BSplineNd> spline,
      CreatePathRefiningBSpline(spline_control_points, kMaxSplineDegree,
                                settings.knot_vector_selection,
                                knot_vector_scaling_factor));


  INTR_ASSIGN_OR_RETURN(
      const std::vector<intrinsic_proto::motion_planning::v1::MotionEvent>
          motion_events,
      ExtractMotionEvents(*spline, *path_polyline));


  // Generate the vector of path variables we want to sample at. The sample
  // distribution will depend on the selected sampling strategy.
  const double scaled_sampling_step = reference_sampling_step *
                                      knot_vector_scaling_factor /
                                      path_polyline_length;
  const double scaled_min_sampling_step =
      min_sampling_step * knot_vector_scaling_factor / path_polyline_length;
  INTR_ASSIGN_OR_RETURN(
      std::vector<double> knot_path_vars,
      sampler.GenerateCurveParameters(*spline, scaled_sampling_step));

  if (settings.enable_denser_sampling_at_path_boundaries) {
    INTR_RETURN_IF_ERROR(AddExtraSamplesAtPathBoundaries(knot_path_vars));
  }

  // Filter out knot parameters that do not satisfy the minimum sampling step.
  INTR_RETURN_IF_ERROR(
      FilterOutByDistanceToNeighbour(scaled_min_sampling_step, knot_path_vars));

  // Generate path samples by sampling both the spline and its control polyline.
  // From the spline we retrieve curve points and their derivatives. From the
  // control polyline we retrieve the associated planning data (joint limits,
  // Cartesian limits, tip_t_target, path segment id).
  std::vector<PathSample> result_samples;
  result_samples.reserve(knot_path_vars.size());

  const size_t num_dof =
      path_polyline->GetPoints().front().joint_configuration.size();
  std::vector<double> knots(
      spline->NumKnots(spline->NumPoints(), spline->Degree()));
  if (!spline->GetKnotVector(&knots)) {
    return absl::InternalError("Couldn't get spline knot vector.");
  }

  std::vector<double> unique_knots;
  spline->GetValidUniqueKnots(unique_knots);

  for (int i = 0; i < knot_path_vars.size(); ++i) {
    // Evaluate derivatives up to third-order.
    const int kNumDerivatives = 4;
    std::vector<eigenmath::VectorNd> spline_sample(
        kNumDerivatives, eigenmath::VectorNd::Zero(num_dof));

    const double path_variable =
        std::clamp(knot_path_vars[i], knots.front(), knots.back());
    if (!spline->EvalCurveAndDerivatives(path_variable, &spline_sample)) {
      return absl::InternalError(
          "Couldn't evaluate spline curve and derivatives.");
    }

    const std::optional<eigenmath::VectorNd> qppp_in =
        ComputeIncomingThirdDerivativeIfDiscontinuous(
            *spline, path_variable, knots, unique_knots, spline_sample[3]);

    result_samples.push_back(PathSample{
        .s = knot_path_vars[i],
        .q = spline_sample[0],
        .qp = spline_sample[1],
        .qpp = spline_sample[2],
        .qppp = spline_sample[3],
        .qppp_in = qppp_in,
    });
  }
  // Make sure that derivatives are zero to full precision at path boundaries if
  // the flag `zero_all_path_derivatives_at_path_boundaries` is true.
  if (settings.zero_all_path_derivatives_at_path_boundaries) {
    result_samples.front().qp.setZero();
    result_samples.front().qpp.setZero();
    result_samples.front().qppp.setZero();
    result_samples.front().qppp_in = std::nullopt;
    result_samples.back().qp.setZero();
    result_samples.back().qpp.setZero();
    result_samples.back().qppp.setZero();
    result_samples.back().qppp_in = std::nullopt;
  }

  // Since the `path_polyline.GetSamples()` function expects a vector that
  // specifies where to sample in the arc-length joint space domain, we use the
  // `result_samples.q` joint configurations to construct this vector and scale
  // it appropriately to match the `path_polyline_length`.
  std::vector<double> arc_length_distances;
  arc_length_distances.reserve(result_samples.size());
  arc_length_distances.push_back(0.0);
  for (int i = 1; i < result_samples.size(); ++i) {
    arc_length_distances.push_back(
        arc_length_distances.back() +
        (result_samples[i].q - result_samples[i - 1].q).norm());
  }

  const double polyline_arc_length_scaling =
      path_polyline_length / arc_length_distances.back();
  std::vector<double> scaled_arc_length_distances(arc_length_distances.size());
  absl::c_transform(
      arc_length_distances, scaled_arc_length_distances.begin(),
      [polyline_arc_length_scaling](const double arc_length_distance) {
        return arc_length_distance * polyline_arc_length_scaling;
      });
  INTR_ASSIGN_OR_RETURN(const std::vector<PathSample> polyline_samples,
                        path_polyline->GetSamples(scaled_arc_length_distances));
  for (int i = 0; i < polyline_samples.size(); ++i) {
    result_samples[i].joint_limits = polyline_samples[i].joint_limits;
    result_samples[i].cart_limits = polyline_samples[i].cart_limits;
    result_samples[i].tip_t_target = polyline_samples[i].tip_t_target;
    result_samples[i].segment_id = polyline_samples[i].segment_id;
  }

  SplineBasedPathRefinementResult spline_based_path_refinement_result;
  spline_based_path_refinement_result.spline = std::move(spline);
  spline_based_path_refinement_result.path_samples = std::move(result_samples);

  spline_based_path_refinement_result.motion_events = std::move(motion_events);

  return spline_based_path_refinement_result;
}

int GetSplineBasedPathRefinementMaxSplineDegree() { return kMaxSplineDegree; }

}  // namespace topp
}  // namespace intrinsic
