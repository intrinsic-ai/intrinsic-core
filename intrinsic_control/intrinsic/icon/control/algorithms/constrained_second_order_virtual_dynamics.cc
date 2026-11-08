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

#include "intrinsic/icon/control/algorithms/constrained_second_order_virtual_dynamics.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

#include "intrinsic/eigenmath/manifolds.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/control/algorithms/cartesian_impedance_commons.h"
#include "intrinsic/icon/control/algorithms/constrained_harmonic_oscillator.h"
#include "intrinsic/icon/utils/fixed_str_cat.h"
#include "intrinsic/icon/utils/log.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/icon/utils/realtime_status_macro.h"
#include "intrinsic/icon/utils/realtime_status_or.h"
#include "intrinsic/kinematics/types/to_fixed_string.h"
#include "intrinsic/math/almost_equals.h"
#include "intrinsic/math/ipow.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/twist.h"

// List of open questions for this implementation that need to be addressed
// mid-term.
//
// TODO(b/369329511): need to handle scenario where the current or newly
// commanded pose is at a singular point of the tool-frame tangent space.
// TODO(b/369332170): experiment with other jerk limit hyperellipsoid shapes,
// beyond the Chebychev ball.

namespace intrinsic::icon {

using cartesian_impedance::RotateCartesianVector;
using cartesian_impedance::TransformTaskMatrix;

namespace {

constexpr bool kPrintDebugInfo = false;
constexpr double kAlmostZero = 1e-20;
constexpr double kSmallThreshold = 1e-6;

// Result of the position increment in the local tangent space, also contains
// the acceleration and jerk in the local tangent space.
struct TangentSpaceSolution {
  eigenmath::VectorNd q_k;  // Position increment in the local tangent space.
  eigenmath::VectorNd a_k;  // Acceleration in the local tangent space.
  eigenmath::VectorNd j_k;  // Jerk in the local tangent space.
};

double MatrixInfinityNorm(const eigenmath::MatrixNMd& m) {
  return m.rowwise().lpNorm<1>().maxCoeff();
}

// Computes axis-aligned bounding box for a hyperellipsoid following
// https://math.stackexchange.com/questions/3926884/smallest-axis-aligned-bounding-box-of-hyper-ellipsoid.
// Returns axis-wise bounding box half-lengths.
eigenmath::VectorNd GetHyperellipsoidAxisAlignedBoundingBox(
    const eigenmath::MatrixNd& A_inv) {
  int ndof = A_inv.rows();
  eigenmath::VectorNd result(ndof);
  for (int i = 0; i < ndof; ++i) {
    // Define a unit-vector for every direction
    eigenmath::VectorNd e = eigenmath::VectorNd::Zero(ndof);
    e(i) = 1.0;
    result(i) = std::sqrt(std::max(0.0, (e.transpose() * A_inv * e)(0, 0)));
  }
  return result;
}

// Computes axis-aligned bounding box for a hyperellipsoid defined by the
// quadratic form
//  V.transpose() * diag(1/max_scalar^2) * V <= 1`.
// Returns axis-wise bounding box half-lengths for the hyperellipsoid.
eigenmath::VectorNd
GetHyperellipsoidAxisAlignedBoundingBoxFromVinvAndScalarLimit(
    const eigenmath::MatrixNd& Vinv, double max_scalar) {
  eigenmath::MatrixNd Ainv =
      Vinv * Vinv.transpose() * ::intrinsic::IPow(max_scalar, 2);
  return GetHyperellipsoidAxisAlignedBoundingBox(Ainv);
}

// Solves a quadratic equation of the form `a*x^2 + b*x + c = 0`.
// For simplicity (and in the context of this particular application) we can
// ignore the imaginary solutions and return an error status if the discriminant
// is negative. Returns sorted list of solutions in ascending order.
icon::RealtimeStatusOr<std::array<double, 2>>
SolveScalarValuedQuadraticEquation(double a, double b, double c) {
  const double discriminant = b * b - 4.0 * a * c;
  if (discriminant > 0) {
    // Roots are real and different.
    const double x1 = (-b + std::sqrt(discriminant)) / (2.0 * a);
    const double x2 = (-b - std::sqrt(discriminant)) / (2.0 * a);
    return std::array<double, 2>{std::min(x1, x2), std::max(x1, x2)};
  } else if (intrinsic::AlmostEquals(discriminant, 0.0)) {
    // Roots are real and the same.
    const double x1 = -b / (2.0 * a);
    return std::array<double, 2>{x1, x1};
  } else {
    INTRINSIC_RT_LOG(ERROR) << "Roots are complex and different.";
    const double real_part = -b / (2.0 * a);
    const double imaginary_part = std::sqrt(-discriminant) / (2.0 * a);
    INTRINSIC_RT_LOG(ERROR)
        << "x1 = " << real_part << "+" << imaginary_part << "i";
    INTRINSIC_RT_LOG(ERROR)
        << "x2 = " << real_part << "-" << imaginary_part << "i";
    return icon::InternalError(
        "Roots are complex and different, but this might just be a numerical "
        "issue. Fix!");
  }
}

// Returns a convex combination of v_feasible and v_infeasible which lies
// exactly on the hyperellipsoid boundary defined by the quadratic form
// `v^T*A*v=1`. Note that `v_feasible` will lie within or exactly on the
// hyperellipsoid boundary, while `v_infeasible` will lie outside of the
// hyperellipsoid. A linear combination of the two input vectors gives rise
// to a quadratic equation which can be solved for the interpolation
// weight achieving a direct hit to the ellipsoid surface.
icon::RealtimeStatusOr<eigenmath::VectorNd>
RescaleInfeasibleVectorToMeetHyperellipsoid(
    const eigenmath::VectorNd& v_feasible,
    const eigenmath::VectorNd& v_infeasible,
    const eigenmath::MatrixNd& hyperellipsoid_weight_matrix) {
  // Define scalar valued quadratic equation which defines the solution lying
  // exactly on the hyperellipsoid boundary.
  const double c0 =
      v_feasible.transpose() * hyperellipsoid_weight_matrix * v_feasible;
  const double c1 =
      v_infeasible.transpose() * hyperellipsoid_weight_matrix * v_infeasible;
  const double c_mix = 2.0 * v_feasible.transpose() *
                       hyperellipsoid_weight_matrix * v_infeasible;
  // Coefficients of the quadratic equation.
  const double a = c1 - 2.0 * c_mix + c0;
  const double b = 2.0 * (c_mix - c0);
  const double c = c0 - 1.0;

  INTRINSIC_RT_ASSIGN_OR_RETURN(const auto alpha_candidates,
                                SolveScalarValuedQuadraticEquation(a, b, c));
  // Filter out a valid solution.
  double alpha = 0.0;
  if (alpha_candidates[0] < 0.0 && alpha_candidates[1] >= 1.0) {
    return icon::InternalError("Should not happen. Infeasibility detected. ");
  } else if (alpha_candidates[0] < 0.0) {
    alpha = alpha_candidates[1];
  } else if (alpha_candidates[1] >= 1.0) {
    alpha = alpha_candidates[0];
  } else {
    alpha = std::min(alpha_candidates[0], alpha_candidates[1]);
  }

  // Check if we at limit saturation, in this case alpha is very close to zero.
  const double kSaturationThreshold = 1.0e-6;
  if (alpha < kSaturationThreshold) {
    INTRINSIC_RT_LOG_THROTTLED(INFO)
        << "We seem to be close to limit saturation. "
           "Rescaling to zero to avoid creeping into limit.";
    alpha = 0.0;
  }

  const eigenmath::VectorNd v_rescaled =
      (1.0 - alpha) * v_feasible + alpha * v_infeasible;

  // Double check that the rescaled vector lies on the hyperellipsoid.
  const double weighted_norm_of_rescaled_vector = std::sqrt(
      v_rescaled.transpose() * hyperellipsoid_weight_matrix * v_rescaled);
  if (weighted_norm_of_rescaled_vector > 1.0 + 1e-12) {
    return icon::InternalError(
        icon::FixedStrCat<RealtimeStatus::kMaxMessageLength>(
            "Rescaled vector does not lie on hyperellipsoid, got norm ",
            weighted_norm_of_rescaled_vector));
  }

  return eigenmath::VectorNd(v_rescaled);
}

// Compute the position increment of a fully diagonalized (constrained) virtual
// dynamics system. This method can be applied to the SE(3) tangent spaces or to
// Rn.
// * `tangent_space_q_k_minus_1` is the position from the last
// control cycle expressed in the local tangent space of the position coordinate
// of the _last_ control cycle as Nd vector.
// * `tangent_space_q_k_minus_2` is the position of the 2nd last
// control cycle expressed in the local tangent space of the position coordinate
// of the _last_ control cycle as Nd vector.
// * `tangent_space_q_k_minus_3` is the position of the
// 3rd last control cycle expressed in the local tangent space of the position
// coordinate of the _last_ control cycle as Nd vector.
// * `tangent_space_p_d_k` is the reference position of the current control
// cycle expressed in the local tangent space of the position coordinate of the
// _last_ control cycle as Nd vector.
// * `tangent_space_p_d_k_minus_1` is the reference position of the previous
// control cycle expressed in the local tangent space of the position coordinate
// of the _last_ control cycle as Nd vector.
// `tangent_space_wrench_error_with_feedforwards` is the error wrench that is
// applied to the system, however also including feedforward velocity,  deadband
// etc.
// * `tangent_space_inertia_matrix_diagonal` is the inertia matrix of the
// diagonalized system in the local tangent space.
// * `tangent_space_damping_matrix_diagonal` is the damping matrix of the
// diagonalized system in the local tangent space.
// * `tangent_space_stiffness_matrix_diagonal` is the stiffness matrix of the
// diagonalized system in the local tangent space.
// * `tangent_space_v_max` is the velocity limit in the local tangent space.
// This limit might be relaxed to ensure feasibility.
// * `tangent_space_a_max` is the acceleration limit in the local tangent space
// for the diagonalized system and for the current control cycle.
// * `tangent_space_j_max` is the jerk limit in the local tangent space for the
// diagonalized system and for the current control cycle.
// * `limit_hyperparameters` are the hyperparameters for the jerk limit.
// * `frequency_hz` is the control frequency.
// * `jerk_limit_ellipsoid_weight_matrix` is the matrix A in the quadratic form
// inequality `x^T A x <= 1` that defines the shape of the jerk limit
// hyperellipsoid centered around the origin of the local tangent space. This
// defines a limit hyperellipsoid that is different(!) from the limits hypercube
// that is implied by the individual jerk limits acting on decoupled DoFs!
// (Explanation: the l_inf norm of a vector applied as upper bound results in a
// hypercube-shaped structure, the l2 norm of a vector applied as upper bound
// results in a hyperellipsoid-shaped structure.)
// By dynamically re-scaling the jerk in case of violations, we can make the
// controller adhere to the jerk limits defined by the hyperellipsoid `x^T A x
// <= 1`. The jerk limits will be scaled it meets exactly the boundaries of the
// hyperellipsoid.
icon::RealtimeStatusOr<TangentSpaceSolution>
ComputePositionIncrementInRnTangentSpace(
    const eigenmath::VectorNd& tangent_space_q_k_minus_1,
    const eigenmath::VectorNd& tangent_space_q_k_minus_2,
    const eigenmath::VectorNd& tangent_space_q_k_minus_3,
    const eigenmath::VectorNd& tangent_space_p_d_k,
    const eigenmath::VectorNd& tangent_space_p_d_k_minus_1,
    const eigenmath::VectorNd& tangent_space_p_d_k_minus_2,
    const eigenmath::VectorNd& tangent_space_wrench_error_with_feedforwards,
    const eigenmath::VectorNd& tangent_space_inertia_matrix_diagonal,
    const eigenmath::VectorNd& tangent_space_damping_matrix_diagonal,
    const eigenmath::VectorNd& tangent_space_stiffness_matrix_diagonal,
    const eigenmath::VectorNd& tangent_space_v_max,
    const eigenmath::VectorNd& tangent_space_a_max,
    const JerkLimitHyperparameters& limit_hyperparameters, double frequency_hz,
    const eigenmath::VectorNd& jerk_limit_bounding_box_lengths,
    const eigenmath::MatrixNd& jerk_limit_ellipsoid_weight_matrix) {
  const int ndof = tangent_space_q_k_minus_2.size();
  //
  // STEP 1 - DoF-wise evaluation of jerk-limited harmonic oscillators with
  // liberal bounding box constraints.
  //
  // Resulting position increment expressed in the local tangent space.
  eigenmath::VectorNd tangent_space_new_q(ndof);
  // Resulting jerk in the local tangent space for a constrained oscillator.
  eigenmath::VectorNd tangent_space_j_k_constrained(ndof);
  eigenmath::VectorNd tangent_space_a_k(ndof);
  // Loop over all `ndof` of the diagonalized Virtual Dynamics system.
  for (int dof = 0; dof < ndof; ++dof) {
    INTRINSIC_RT_ASSIGN_OR_RETURN(
        const ConstrainedHarmonicOscillatorOutput dof_ctrl_output,
        Step(
            {
                .T = 1. / frequency_hz,
                .M = tangent_space_inertia_matrix_diagonal[dof],
                .B = tangent_space_damping_matrix_diagonal[dof],
                .K = tangent_space_stiffness_matrix_diagonal[dof],
                .v_max = tangent_space_v_max[dof],
                .a_max = tangent_space_a_max[dof],
                .j_max = jerk_limit_bounding_box_lengths[dof],
                .limit_hyperparams = limit_hyperparameters,
                .f_k = tangent_space_wrench_error_with_feedforwards[dof],
                .q_k_minus_1 = tangent_space_q_k_minus_1[dof],
                .q_k_minus_2 = tangent_space_q_k_minus_2[dof],
                .q_k_minus_3 = tangent_space_q_k_minus_3[dof],
                .p_d_k = tangent_space_p_d_k[dof],
                .p_d_k_minus_1 = tangent_space_p_d_k_minus_1[dof],
                .p_d_k_minus_2 = tangent_space_p_d_k_minus_2[dof],
                // The feasibility restoration strategy is hard-coded, currently
                // no other meaningful strategies are available.
                .feasibility_restoration_strategy =
                    FeasibilityRestorationStrategy::kRelaxVelocityLimit,
            },
            /*verbose_log_at_failure=*/false));
    tangent_space_new_q[dof] = dof_ctrl_output.qk;
    tangent_space_j_k_constrained[dof] = dof_ctrl_output.j_k;
    tangent_space_a_k[dof] = dof_ctrl_output.a_k;
  }

  //
  // STEP 2 - jerk limiting with jerk vector direction preservation using
  // rescaling to match a hyperellipsoid boundary.
  //
  // If one (or more) jerk limits are over-saturated wrt the limits
  // hyperellipsoid (which always lies within the bounding box), we can apply a
  // simple backscaling mechanism in order to maintain the direction of the jerk
  // vector. The main idea is to scale the jerk limits such that the resulting
  // jerk vector is collinear with the jerk vector resulting from Step 1, but
  // exactly meets the jerk limits given by the hyperellipsoid `x^T A x <= 1`
  // where `A` is the jerk_limit_ellipsoid_weight_matrix. This is only
  // implemented at jerk level because TODO(b/369172643): we don't know how to
  // synchronously throttle both jerk and acceleration using the hyperellipsoid
  // method. For acceleration, we restrict limits to be a hypercube instead (see
  // further below). As a consequence, the closed loop motions currently do not
  // behave in a homothetic way, for instance when doing point-to-point motions.
  //
  // Intuition:
  // * The hyperellipsoid equation `x^T A x <= 1` defines a set of points that
  // are "allowed" for the jerk vector `x` in terms of a squared norm. In case
  // of a jerk limit saturation, the tip of the jerk vector will be exactly at
  // the boundary of the hyperellipsoid. Conversely, if the weighted norm would
  // exceed 1.0, the jerk-limits hyperellipsoid would be violated.
  // * By evaluating the hyperellipsoid with the respective jerk vectors
  // `x^T A x` and we can determine a scaling factors that will make the
  // constrained jerk vector exactly meet the hyperellipsoid boundary.
  // Differentially consistent acceleration, velocity and position increments
  // are computed from the scaled jerk.
  //
  // TODO(b/369294897): there is one problem that needs further investigation.
  // In fact we use jerk limit scaling factors Jy, Js, etc. The selective (and
  // non-synchronized) activation of these scaling factors will again lead
  // to a motion that is not direction-preserving, or could even lead to
  // instability of the algorithm.

  const double weighted_norm_of_constrained_jerk = std::sqrt(
      tangent_space_j_k_constrained.transpose() *
      jerk_limit_ellipsoid_weight_matrix * tangent_space_j_k_constrained);

  // Return if jerk limits are not exceeded. Otherwise, perform rescaling.
  if (weighted_norm_of_constrained_jerk <= 1.0) {
    return TangentSpaceSolution{.q_k = tangent_space_new_q,
                                .a_k = tangent_space_a_k,
                                .j_k = tangent_space_j_k_constrained};
  }

  // Compute the new (scaled) jerk DoF-wise.
  const double jerk_scaling_factor = 1.0 / weighted_norm_of_constrained_jerk;
  for (int dof = 0; dof < ndof; ++dof) {
    const auto rescaled_result = ComputeOutputsFromJerk(
        1. / frequency_hz, tangent_space_q_k_minus_1[dof],
        tangent_space_q_k_minus_2[dof], tangent_space_q_k_minus_3[dof],
        jerk_scaling_factor * tangent_space_j_k_constrained[dof]);
    tangent_space_new_q[dof] = rescaled_result.qk;
    tangent_space_a_k[dof] = rescaled_result.a_k;
    tangent_space_j_k_constrained[dof] = rescaled_result.j_k;
  }

  return TangentSpaceSolution{.q_k = tangent_space_new_q,
                              .a_k = tangent_space_a_k,
                              .j_k = tangent_space_j_k_constrained};
}

// Computes a tangent space solution from a desired acceleration and previous
// positions by integrating twice.
TangentSpaceSolution ComputeTangentSpaceSolutionFromAcceleration(
    const eigenmath::VectorNd& q_k_minus_1,
    const eigenmath::VectorNd& q_k_minus_2, const eigenmath::VectorNd& a_k,
    const eigenmath::VectorNd& a_k_minus_1, const double dt,
    const double dt_squared) {
  return TangentSpaceSolution{
      .q_k = a_k * dt_squared + 2.0 * q_k_minus_1 - q_k_minus_2,
      .a_k = a_k,
      .j_k = (a_k - a_k_minus_1) / dt,
  };
}

// Simplest possible strategy to restore acceleration-level feasibility by
// moving towards zero acceleration (subject to jerk limits).
icon::RealtimeStatusOr<TangentSpaceSolution>
RestoreAccelerationFeasibilityByMovingTowardsZeroAcceleration(
    const eigenmath::VectorNd& tangent_space_q_k_minus_1,
    const eigenmath::VectorNd& tangent_space_q_k_minus_2,
    const eigenmath::VectorNd& tangent_space_a_k_minus_1,
    const eigenmath::MatrixNd& acceleration_limit_ellipsoid_weight_matrix,
    const eigenmath::MatrixNd& jerk_limit_ellipsoid_weight_matrix,
    double dt_sec, double dt_squared) {
  const int ndof = tangent_space_a_k_minus_1.size();
  INTRINSIC_RT_ASSIGN_OR_RETURN(
      eigenmath::VectorNd a_rescaled_towards_zero,
      RescaleInfeasibleVectorToMeetHyperellipsoid(
          /*v_feasible=*/eigenmath::VectorNd::Zero(ndof),
          tangent_space_a_k_minus_1,
          acceleration_limit_ellipsoid_weight_matrix));

  // Check if increment towards `a_rescaled_towards_zero` violates jerk limits.
  eigenmath::VectorNd jerk =
      (a_rescaled_towards_zero - tangent_space_a_k_minus_1) / dt_sec;
  const double jerk_squared_weighted_norm =
      jerk.transpose() * jerk_limit_ellipsoid_weight_matrix * jerk;
  if (jerk_squared_weighted_norm > 1.0) {
    INTRINSIC_RT_ASSIGN_OR_RETURN(
        const eigenmath::VectorNd rescaled_jerk,
        RescaleInfeasibleVectorToMeetHyperellipsoid(
            /*v_feasible=*/eigenmath::VectorNd::Zero(ndof), jerk,
            jerk_limit_ellipsoid_weight_matrix));
    a_rescaled_towards_zero =
        tangent_space_a_k_minus_1 + rescaled_jerk * dt_sec;
  }

  return ComputeTangentSpaceSolutionFromAcceleration(
      tangent_space_q_k_minus_1, tangent_space_q_k_minus_2,
      a_rescaled_towards_zero, tangent_space_a_k_minus_1, dt_sec, dt_squared);
}

// Computes a position increment in local tangent space (or in Rn).
// Relies on the fact that acceleration and jerk hyperellipsoids are oriented
// the same way, basically they look like two "potatoes" lying next to each
// other.
// TODO(b/369332170): document the fact that this can potentially be extended to
// operate on general ellipsoid limits, not just perfect "balls" in Rn (before
// bi-diagonalization). This way, we could start using the Cohen algorithm again
icon::RealtimeStatusOr<TangentSpaceSolution>
ComputePositionIncrementWithPotatoeAlgorithm(
    const eigenmath::VectorNd& tangent_space_q_k_minus_1,
    const eigenmath::VectorNd& tangent_space_q_k_minus_2,
    const eigenmath::VectorNd& tangent_space_q_k_minus_3,
    const eigenmath::VectorNd& tangent_space_p_d_k,
    const eigenmath::VectorNd& tangent_space_p_d_k_minus_1,
    const eigenmath::VectorNd& tangent_space_p_d_k_minus_2,
    const eigenmath::VectorNd& tangent_space_wrench_error_with_feedforwards,
    const eigenmath::VectorNd& tangent_space_inertia_matrix_diagonal,
    const eigenmath::VectorNd& tangent_space_damping_matrix_diagonal,
    const eigenmath::VectorNd& tangent_space_stiffness_matrix_diagonal,
    const eigenmath::VectorNd& tangent_space_v_max,
    const JerkLimitHyperparameters& limit_hyperparameters, double frequency_hz,
    const eigenmath::VectorNd& acceleration_limit_bounding_box_lengths,
    const eigenmath::MatrixNd& acceleration_limit_ellipsoid_weight_matrix,
    const eigenmath::VectorNd& jerk_limit_bounding_box_lengths,
    const eigenmath::MatrixNd& jerk_limit_ellipsoid_weight_matrix) {
  // POTATOE algorithm steps.
  //
  // (0) Compute the acceleration from the previous step a_prev.
  // (1) Evaluate constrained oscillator with rough acceleration limits bounding
  // box and jerk limits hyperellipsoid constraint. This can be done efficiently
  // by calling the existing algorithm above. The result will
  //    * stick to the correct l2 jerk limits. Particularly, the l2 norm of
  //    a_new - a_prev will stick to the l2 jerk limit ellipsoid.
  //    * might violate the acceleration limits, by returning an acceleration
  //    that is between the acceleration hyperellipsoid and the acceleration
  //    bounding box.
  // (2) compute weighted l2-norm of a_new. If <= 1.0, return. If > 1.0, we
  // violate acceleration limits and we need to scale back. See next step.
  // (3) Did a_prev satisfy acceleration limits? If yes, form a convex
  // combination of a_prev and a_new that lies exactly on the acceleration limit
  // boundary. Return.
  //   * Note that there might be other projection methods. We can do at least
  //      (I) convex combination between a_prev and a_new.
  //      (II) convex combination between zero and a_new (not convinced this
  //      makes sense, needs thinking.)
  //      (III) projection onto the acceleration limit hyperellipsoid. Here we
  //      need to evaluate if this is guaranteed to be jerk-limited.
  // (4) Did a_prev violate acceleration limits? Now we need special recovery,
  // since both a_prev was not feasible _and_ a_new did not drive us into
  // feasibility. Now we need to handle two options.
  //      * compute how to go back from a_prev to within the acceleration limit
  //      hyperellipsoid, which pratically means we can use the same projection
  //      options (II) and (III) as above.
  //      * use a weighted combination of a projection going directly back to
  //      the hyperellipsoid and a_new. Make sure the former outweights the
  //      latter to converge back to feasibility.

  // Implementation.
  const double dt_sec = 1. / frequency_hz;
  const double dt_squared = ::intrinsic::IPow(dt_sec, 2);

  // (0) Compute a_prev.
  const eigenmath::VectorNd tangent_space_a_prev =
      (tangent_space_q_k_minus_1 - 2.0 * tangent_space_q_k_minus_2 +
       tangent_space_q_k_minus_3) /
      dt_squared;

  // (1) Evaluation of update with rough acceleration limits.
  INTRINSIC_RT_ASSIGN_OR_RETURN(
      const TangentSpaceSolution candidate_solution,
      ComputePositionIncrementInRnTangentSpace(
          tangent_space_q_k_minus_1, tangent_space_q_k_minus_2,
          tangent_space_q_k_minus_3, tangent_space_p_d_k,
          tangent_space_p_d_k_minus_1, tangent_space_p_d_k_minus_2,
          tangent_space_wrench_error_with_feedforwards,
          tangent_space_inertia_matrix_diagonal,
          tangent_space_damping_matrix_diagonal,
          tangent_space_stiffness_matrix_diagonal, tangent_space_v_max,
          /*tangent_space_a_max=*/acceleration_limit_bounding_box_lengths,
          limit_hyperparameters, frequency_hz, jerk_limit_bounding_box_lengths,
          jerk_limit_ellipsoid_weight_matrix));

  // (2) Check for acceleration limit violation.
  const double weighted_squared_norm_of_a_new =
      candidate_solution.a_k.transpose() *
      acceleration_limit_ellipsoid_weight_matrix * candidate_solution.a_k;
  if (weighted_squared_norm_of_a_new <= 1.0) {
    // First solution candidate already ended up in feasible region. Return
    // early.
    return TangentSpaceSolution(candidate_solution);
  }

  // (3) Find acceleration-limit aware convex combination between a_prev and
  // a_new. We know that this combination is also jerk-limit aware, since the
  // increment between a_prev and a_new is jerk-limited.
  const double weighted_squared_norm_of_a_prev =
      tangent_space_a_prev.transpose() *
      acceleration_limit_ellipsoid_weight_matrix * tangent_space_a_prev;
  if (weighted_squared_norm_of_a_prev <= 1.0) {
    // TODO(giftthaler): should we use other projection methods here?
    INTRINSIC_RT_ASSIGN_OR_RETURN(
        const eigenmath::VectorNd a_rescaled,
        RescaleInfeasibleVectorToMeetHyperellipsoid(
            tangent_space_a_prev, candidate_solution.a_k,
            acceleration_limit_ellipsoid_weight_matrix));

    return ComputeTangentSpaceSolutionFromAcceleration(
        tangent_space_q_k_minus_1, tangent_space_q_k_minus_2, a_rescaled,
        tangent_space_a_prev, dt_sec, dt_squared);
  }

  // (4) Oops, we need special recovery, since even the previous a_prev is not
  // feasible wrt current acceleration limits.
  INTRINSIC_RT_LOG_THROTTLED(INFO)
      << "Entering recovery step since a_prev is not feasible.";

  // TODO(giftthaler): implement different projection / recovery techniques
  // here. We start with the simplest possible recovery technique which is to
  // go towards the hyperellipsoid boundary starting from `a_prev`.
  return RestoreAccelerationFeasibilityByMovingTowardsZeroAcceleration(
      tangent_space_q_k_minus_1, tangent_space_q_k_minus_2,
      tangent_space_a_prev, acceleration_limit_ellipsoid_weight_matrix,
      jerk_limit_ellipsoid_weight_matrix, dt_sec, dt_squared);
}

}  // namespace

void ConstrainedCartesianVirtualDynamics::Initialize(
    const Pose3d& initial_base_t_tool_frame,
    const Twist& initial_twist_in_tool_frame,
    const Acceleration& initial_acceleration_in_tool_frame,
    std::optional<Pose3d> previous_base_t_tool_desired_pose,
    std::optional<Twist> previous_reference_twist_in_tool_frame) {
  const double dt_sec = 1. / control_frequency_hz_;

  const Twist twist_in_tool_frame_k_minus_1 = initial_twist_in_tool_frame;
  const Acceleration acceleration_in_tool_frame_k_minus_1 =
      initial_acceleration_in_tool_frame;

  // Initialize history of commanded (output) poses from differential initial
  // state. Use linear velocity / constant acceleration model for
  // initialization. To initialize velocities, we generalize the simple scalar
  // relationship `v_k_minus_1 = (p_k_minus_1 - p_k_minus_2) / dt_sec` and solve
  // for `p_k_minus_2`. Analogously, in the local tangent space, we construct
  // the commanded pose for second-to-last cycle from the last pose and the last
  // twist.
  q_k_minus_1_ = initial_base_t_tool_frame;
  q_k_minus_2_ = q_k_minus_1_ * eigenmath::expRiemann(eigenmath::Vector6d(
                                    -twist_in_tool_frame_k_minus_1 * dt_sec));

  // Reconstruct the commanded pose for third-to-last cycle from the initial
  // acceleration and the initial twist inspired by the relation `dt_sec² *
  // a_k_minus_1 = p_k_minus_1 - 2* p_k_minus_2 + p_k_minus_3`. Analogously, in
  // the local tangent space, we construct the commanded pose for third-to-last
  // cycle from the last pose, last twist and last acceleration as:
  q_k_minus_3_ = q_k_minus_1_ *
                 eigenmath::expRiemann(eigenmath::Vector6d(
                     acceleration_in_tool_frame_k_minus_1 * dt_sec * dt_sec +
                     2.0 * (-twist_in_tool_frame_k_minus_1 * dt_sec)));

  // Initialize reference pose history from differential reference state.
  // Initialize pose reference for last time step. If no previous pose reference
  // has been provided, we use the initial pose as pose reference for the last
  // timestep.
  p_d_k_minus_1_ =
      previous_base_t_tool_desired_pose.value_or(initial_base_t_tool_frame);
  // Initialize the pose reference from two time steps ago. If no previous twist
  // reference has been provided, we assume zero twist to initialize.
  p_d_k_minus_2_ =
      p_d_k_minus_1_ *
      eigenmath::expRiemann(eigenmath::Vector6d(
          -previous_reference_twist_in_tool_frame.value_or(Twist::ZERO) *
          dt_sec));

  initialized_ = true;
}

icon::RealtimeStatusOr<Pose3d> ConstrainedCartesianVirtualDynamics::Step(
    const cartesian_impedance::RealTimeCartesianTarget& cartesian_target,
    const Wrench& sensed_wrench_in_tool_frame,
    const Wrench& wrench_deadband_in_tool_frame, double max_cartesian_velocity,
    double max_cartesian_acceleration, double max_cartesian_jerk) {
  if (!initialized_) {
    return icon::FailedPreconditionError(
        "ConstrainedCartesianVirtualDynamics is not initialized. Did you "
        "forget to call Initialize() before Step()?");
  }

  if (max_cartesian_velocity < kAlmostZero) {
    return InvalidArgumentError(FixedStrCat<RealtimeStatus::kMaxMessageLength>(
        "max_cartesian_velocity has a negative or close to zero entry. Got ",
        max_cartesian_velocity));
  }
  if (max_cartesian_acceleration < kAlmostZero) {
    return InvalidArgumentError(FixedStrCat<RealtimeStatus::kMaxMessageLength>(
        "max_cartesian_acceleration has a negative or close to zero entry. "
        "Got ",
        max_cartesian_acceleration));
  }
  if (max_cartesian_jerk < kAlmostZero) {
    return InvalidArgumentError(FixedStrCat<RealtimeStatus::kMaxMessageLength>(
        "max_cartesian_jerk has a negative or close to zero entry. Got ",
        max_cartesian_jerk));
  }

  // TODO(b/375460419): constrained harmonic oscillator does not work with
  // infinite acceleration limit. Because of many recursive (and limit-adapted)
  // calls to the underlying 1 DoF constrained harmonic oscillator, we put a
  // (temporary) heuristic for limiting acceleration and jerk here. It should be
  // investigated if the algorithm works with infinite jerk limits.
  const double kMaxAccelerationLimitMultiplier = 1000.0;
  const double kMaxJerkLimitMultiplier = 2500.0;
  max_cartesian_acceleration =
      std::min(max_cartesian_acceleration,
               kMaxAccelerationLimitMultiplier * max_cartesian_velocity);
  max_cartesian_jerk = std::min(
      max_cartesian_jerk, kMaxJerkLimitMultiplier * max_cartesian_acceleration);

  if (max_cartesian_acceleration_last_cycle_.has_value()) {
    if ((max_cartesian_acceleration - *max_cartesian_acceleration_last_cycle_) *
            control_frequency_hz_ <
        -(1.0 + kSmallThreshold) * max_cartesian_jerk) {
      INTRINSIC_RT_LOG(ERROR)
          << "Cartesian acceleration limit changes faster than jerk limit.";
    }
  }
  max_cartesian_acceleration_last_cycle_ = max_cartesian_acceleration;

  // The input parameters `cartesian_target` are still expressed w.r.t. the
  // task frame. In a first step, they are transformed to a local frame located
  // at the current tool pose (current nominal pose).
  const Pose3d base_t_current_tool_frame = q_k_minus_1_;
  const Pose3d current_tool_frame_t_base = base_t_current_tool_frame.inverse();

  // First we formulate a "fully-coupled" system that is centered at the origin
  // of the current tool pose. We express the history of commanded and
  // reference poses in the local tangent space located at the current tool
  // frame.
  eigenmath::Vector6d tangent_space_q_k_minus_1 =
      eigenmath::logRiemann(current_tool_frame_t_base * q_k_minus_1_);
  eigenmath::Vector6d tangent_space_q_k_minus_2 =
      eigenmath::logRiemann(current_tool_frame_t_base * q_k_minus_2_);
  eigenmath::Vector6d tangent_space_q_k_minus_3 =
      eigenmath::logRiemann(current_tool_frame_t_base * q_k_minus_3_);
  eigenmath::Vector6d tangent_space_p_d_k = eigenmath::logRiemann(
      current_tool_frame_t_base * cartesian_target.robot_base_t_task *
      Pose3d(cartesian_target.task_t_tool_reference_orientation,
             cartesian_target.task_t_tool_reference_position));
  eigenmath::Vector6d tangent_space_p_d_k_minus_1 =
      eigenmath::logRiemann(current_tool_frame_t_base * p_d_k_minus_1_);
  eigenmath::Vector6d tangent_space_p_d_k_minus_2 =
      eigenmath::logRiemann(current_tool_frame_t_base * p_d_k_minus_2_);

  // Also express reference twist and -wrench in the current tool frame.
  Wrench reference_wrench_in_tool_frame =
      RotateCartesianVector(current_tool_frame_t_base.quaternion() *
                                cartesian_target.robot_base_t_task.quaternion(),
                            cartesian_target.tool_reference_wrench);
  Twist reference_twist_in_tool_frame =
      cartesian_impedance::RotateCartesianVector(
          current_tool_frame_t_base.quaternion() *
              cartesian_target.robot_base_t_task.quaternion(),
          cartesian_target.tool_reference_twist);

  if (kPrintDebugInfo) {
    INTRINSIC_RT_LOG(INFO) << "q_k_minus_1_: "
                           << eigenmath::ToFixedString(q_k_minus_1_);
    INTRINSIC_RT_LOG(INFO) << "base_t_current_tool_frame: "
                           << eigenmath::ToFixedString(
                                  base_t_current_tool_frame);
    INTRINSIC_RT_LOG(INFO)
        << "cartesian_target.task_t_tool_reference_orientation: "
        << eigenmath::ToFixedString(
               cartesian_target.task_t_tool_reference_orientation);
    INTRINSIC_RT_LOG(INFO) << "tangent_space_p_d_k: "
                           << eigenmath::ToFixedString(
                                  tangent_space_p_d_k.transpose());
    INTRINSIC_RT_LOG(INFO) << "tangent_space_p_d_k_minus_1: "
                           << eigenmath::ToFixedString(
                                  tangent_space_p_d_k_minus_1.transpose());
    INTRINSIC_RT_LOG(INFO) << "tangent_space_p_d_k_minus_2: "
                           << eigenmath::ToFixedString(
                                  tangent_space_p_d_k_minus_2.transpose());
    INTRINSIC_RT_LOG(INFO) << "tangent_space_q_k_minus_1: "
                           << eigenmath::ToFixedString(
                                  tangent_space_q_k_minus_1.transpose());
    INTRINSIC_RT_LOG(INFO) << "tangent_space_q_k_minus_2: "
                           << eigenmath::ToFixedString(
                                  tangent_space_q_k_minus_2.transpose());
    INTRINSIC_RT_LOG(INFO) << "tangent_space_q_k_minus_3: "
                           << eigenmath::ToFixedString(
                                  tangent_space_q_k_minus_3.transpose());
    INTRINSIC_RT_LOG(INFO) << "sensed_wrench_in_tool_frame: "
                           << eigenmath::ToFixedString(
                                  sensed_wrench_in_tool_frame.transpose());
    INTRINSIC_RT_LOG(INFO) << "reference_wrench_in_tool_frame: "
                           << eigenmath::ToFixedString(
                                  reference_wrench_in_tool_frame.transpose());
  }

  // Transform impedance gain and selection matrix into the current tool frame.
  const eigenmath::Matrix6d K_in_tool_frame = TransformTaskMatrix(
      current_tool_frame_t_base * cartesian_target.robot_base_t_task,
      cartesian_target.cartesian_stiffness);
  const eigenmath::Matrix6d M_in_tool_frame = TransformTaskMatrix(
      current_tool_frame_t_base * cartesian_target.robot_base_t_task,
      cartesian_target.virtual_cartesian_inertia_inverse.inverse());
  const eigenmath::Matrix6d wrench_selection_matrix_in_tool_frame =
      TransformTaskMatrix(
          current_tool_frame_t_base * cartesian_target.robot_base_t_task,
          cartesian_target.wrench_selection_matrix);
  const eigenmath::Matrix6d damping_matrix_in_tool_frame = TransformTaskMatrix(
      current_tool_frame_t_base * cartesian_target.robot_base_t_task,
      cartesian_target.cartesian_damping);

  // Computation of the fully decoupled inertia and stiffness via
  // bi-diagonalization. The bi-diagonalization can be understood as a change of
  // basis, which transforms the impedance dynamics to a form where inertia and
  // stiffness matrix are strictly diagonal. We mark all quantities in the
  // diagonalized frame with a "diag" suffix.
  INTRINSIC_RT_RETURN_IF_ERROR(impedance_bidiagonalization_.Diagonalize(
      M_in_tool_frame, K_in_tool_frame));
  const eigenmath::Vector6d M_diag =
      impedance_bidiagonalization_.InertiaMatrixDiagonalized().diagonal();
  const eigenmath::Vector6d K_diag =
      impedance_bidiagonalization_.StiffnessMatrixDiagonalized().diagonal();

  // Retrieve the transformation matrices that transform quantities from the
  // tool frame into the diagonalized frame and vice versa. We define x = Vz
  // where `x` is a vector in the tool frame and `z` is a vector in the
  // diagonalized frame.
  const eigenmath::Matrix6d V =
      impedance_bidiagonalization_.EigenVectorBasisV();
  const eigenmath::Matrix6d Vinv =
      impedance_bidiagonalization_.EigenVectorBasisVInverse();

  // Computing damping matrix in the diagonalized frame using the result from
  // virtual_impedance_dynamics_bidiagonalization.h", D_diag = V^T*D*V.
  // Important sidenote: properties of the damping matrix are preserved under
  // this transformation. For example,  _critical_ damping computed in the robot
  // base frame or tool frame will also remain to be critical damping for the
  // bi-diagonalized system. The reason is that the matrix version of the
  // computation of critical damping is "linear" in the relevant parts.
  eigenmath::Matrix6d D_diag_matrix =
      (V.transpose() * damping_matrix_in_tool_frame * V);
  const double kDiagonalCheckTolerance = 1e-5;
  if (!D_diag_matrix.isDiagonal(kDiagonalCheckTolerance)) {
    return icon::InternalError(
        "Damping matrix is not diagonal in diagonalized system.");
  }
  eigenmath::Vector6d D_diag = D_diag_matrix.diagonal();
  if (D_diag.minCoeff() < kDiagonalCheckTolerance) {
    return icon::InternalError(
        "Damping matrix is singular, which can lead to instability.");
  }

  // Prepare input quantities for the diagonalized control law in transformed
  // coordinates, i.e. the following section implements the control law
  //
  // z = Vinv * x.
  // ddz + D_diag*dz + K_diag*z = V^T*(f - f_ref + M*a_ref + D*v_ref + K*x_ref)
  //
  // where M_diag has been omitted because it is guaranteed to be Identity.
  Wrench wrench_error_in_tool_frame(sensed_wrench_in_tool_frame -
                                    reference_wrench_in_tool_frame);
  INTRINSIC_RT_RETURN_IF_ERROR(cartesian_impedance::ApplyDeadband(
      wrench_deadband_in_tool_frame, wrench_error_in_tool_frame));
  eigenmath::Vector6d wrench_diagonal_space_with_feedforwards =
      -V.transpose() * wrench_selection_matrix_in_tool_frame *
          wrench_error_in_tool_frame +
      D_diag.asDiagonal() * Vinv * reference_twist_in_tool_frame;

  // Cartesian limits formulated as hyper-ellipsoid or hyper-cuboids.
  //
  // Hyperellipsoid Preliminaries: When we interprete Cartesian limits as
  // inequalities on the l2-norm of a Cartesian vector, we can write this as
  // |x|_2 <= x_max, or alternatively as quadratic form as x^T*I*x <= x_max².
  //
  // For JERK and ACCELERATION limits we are formulating a limit hyperellipsoid
  // subject to a change of basis since they can be handled when coupled across
  // DoFs by re-scaling solutions at acceleration and jerk level. After
  // bi-diagonalization, the jerk-limits and acceleration limits are degenerated
  // to a generic hyper-ellipsoid instead of a hyper-sphere. We can use the
  // equation z'Az <= 1 to evaluate how much a 6 DoF vector z (in the
  // bi-diagonalized frame) violates the original jerk or acceleration limit.
  //
  // For VELOCITY limits, the algorithm here does not yet support coupling of
  // DoFs. We need to apply a conservative approximation which ensures that the
  // 6 DoF-vector, when all DoFs are treated in a decoupled way, stays within
  // the l2-norm defined by the Cartesian velocity limits. To this end, we fit a
  // cube into the limit hypersphere. Even if all DoFs saturate their individual
  // DoF limits, the l2-norm of the combined 6 DoF vector must remain within the
  // limit sphere. To this end we create a conservative approximation of the
  // l2-norm through the infinity norm as follows. Using the lemma
  // `|x|_2 <= sqrt(kNDof)*|x|_inf` we develop the following approximation:
  //
  // `|x|_2 = |V*z|_2 <= sqrt(kNDof)*|V*z|_inf` <= sqrt(kNDof)*|V|_inf*|z|_inf`
  //
  // where |z|_inf can be treated as an upper bound on the optimization vector z
  // in the diagonalized frame. From here, it follows that for the diagonalized
  // system, we can choose the upper bound
  //
  // |z|_inf = |x|_2 / (sqrt(kNDof)*|V|_inf)
  //
  // TODO(b/376460047): split the limits up into a rotational and a
  // translational component.
  // Use conservative limit cuboid for velocity limits.
  const double V_inf_norm = MatrixInfinityNorm(V);
  constexpr int kNumDof = 6;
  eigenmath::Vector6d bidiag_v_max = eigenmath::Vector6d::Constant(
      max_cartesian_velocity / (std::sqrt(kNumDof) * V_inf_norm));

  if (kPrintDebugInfo) {
    INTRINSIC_RT_LOG(INFO) << "bidiag_v_max: "
                           << eigenmath::ToFixedString(
                                  bidiag_v_max.transpose());
  }

  // Construct the positive semi-definite jerk- and acceleration limit
  // hyperellipsoid weight matrices. By construction, these matrices are
  // guaranteed to be symmetric positive semi-definite because for an arbitrary
  // matrix V, V.transpose() * V will be symmetric and positive semi-definite.
  const eigenmath::Matrix6d jerk_limit_hyperellipsoid_weight_matrix =
      (V.transpose() * V) / ::intrinsic::IPow(max_cartesian_jerk, 2);
  const eigenmath::VectorNd jerk_limit_bounding_box_lengths =
      GetHyperellipsoidAxisAlignedBoundingBoxFromVinvAndScalarLimit(
          Vinv, max_cartesian_jerk);

  const eigenmath::Matrix6d acc_limit_hyperellipsoid_weight_matrix =
      (V.transpose() * V) / ::intrinsic::IPow(max_cartesian_acceleration, 2);
  const eigenmath::VectorNd acc_limit_bounding_box_lengths =
      GetHyperellipsoidAxisAlignedBoundingBoxFromVinvAndScalarLimit(
          Vinv, max_cartesian_acceleration);

  // The limit hyperparameters are manually tuned by giftthaler@.
  const JerkLimitHyperparameters dof_limit_hyperparameters{
      .jerk_y_scaling = 0.7,  // Jerk limit at high acceleration multiplier
      .jerk_s_scaling = 1.3,  // Jerk limit intercept multiplier
  };

  INTRINSIC_RT_ASSIGN_OR_RETURN(
      const auto position_increment_in_tangent_space_z,
      ComputePositionIncrementWithPotatoeAlgorithm(
          /*tangent_space_q_k_minus_1=*/eigenmath::Vector6d::Zero(6),
          Vinv * tangent_space_q_k_minus_2, Vinv * tangent_space_q_k_minus_3,
          Vinv * tangent_space_p_d_k, Vinv * tangent_space_p_d_k_minus_1,
          Vinv * tangent_space_p_d_k_minus_2,
          wrench_diagonal_space_with_feedforwards, M_diag, D_diag, K_diag,
          bidiag_v_max, dof_limit_hyperparameters, control_frequency_hz_,
          acc_limit_bounding_box_lengths,
          acc_limit_hyperellipsoid_weight_matrix,
          jerk_limit_bounding_box_lengths,
          jerk_limit_hyperellipsoid_weight_matrix));

  // Get pose increment wrt current pose frame.
  eigenmath::Vector6d tangent_space_new_q =
      V * position_increment_in_tangent_space_z.q_k;
  Pose3d current_tool_frame_t_new_q =
      eigenmath::expRiemann(tangent_space_new_q);
  Pose3d base_t_updated_pose =
      base_t_current_tool_frame * current_tool_frame_t_new_q;

  // Transcribe pose state history.
  q_k_minus_3_ = q_k_minus_2_;
  q_k_minus_2_ = q_k_minus_1_;
  q_k_minus_1_ = base_t_updated_pose;
  p_d_k_minus_2_ = p_d_k_minus_1_;
  p_d_k_minus_1_ = cartesian_target.robot_base_t_task *
                   Pose3d(cartesian_target.task_t_tool_reference_orientation,
                          cartesian_target.task_t_tool_reference_position);

  return Pose3d(base_t_updated_pose);
}

void ConstrainedVirtualDynamicsNd::Initialize(
    const eigenmath::VectorNd& initial_position,
    const eigenmath::VectorNd& initial_velocity,
    const eigenmath::VectorNd& initial_acceleration,
    std::optional<eigenmath::VectorNd> previous_reference_position,
    std::optional<eigenmath::VectorNd> previous_reference_velocity) {
  const double dt_sec = 1. / control_frequency_hz_;

  // Initialize history of commanded (output) position from differential initial
  // state. Use linear velocity / constant acceleration model for
  // initialization. To initialize velocities, we generalize the simple scalar
  // relationship `v_k_minus_1 = (p_k_minus_1 - p_k_minus_2) / dt_sec` and solve
  // for `p_k_minus_2`. Analogously, we construct the commanded position for the
  // second-to-last cycle from the last pose and the last velocity.
  q_k_minus_1_ = initial_position;
  q_k_minus_2_ = q_k_minus_1_ - initial_velocity * dt_sec;

  // Reconstruct the commanded position for the third-to-last cycle from the
  // initial acceleration and the initial velocity inspired by the relation
  // `dt_sec² * a_k_minus_1 = p_k_minus_1 - 2* p_k_minus_2 + p_k_minus_3`.
  // Analogously, we construct the commanded position for third-to-last cycle
  // from the last position, last velocity and last acceleration as:
  q_k_minus_3_ = q_k_minus_1_ + initial_acceleration * dt_sec * dt_sec -
                 2.0 * initial_velocity * dt_sec;

  // Initialize reference position history from differential reference state.
  // Initialize position reference for last time step. If no previous position
  // reference has been provided, we use the initial position as position
  // reference for the last timestep.
  p_d_k_minus_1_ = previous_reference_position.value_or(initial_position);
  // Initialize the position reference from two time steps ago. If no previous
  // velocity reference has been provided, we assume zero velocity to
  // initialize.
  p_d_k_minus_2_ = p_d_k_minus_1_ - previous_reference_velocity.value_or(
                                        eigenmath::VectorNd::Zero(ndof_)) *
                                        dt_sec;

  initialized_ = true;
}

icon::RealtimeStatusOr<eigenmath::VectorNd> ConstrainedVirtualDynamicsNd::Step(
    const Parameters& input_parameters, const eigenmath::VectorNd& sensed_force,
    const eigenmath::VectorNd& deadband_force, double max_velocity,
    double max_acceleration, double max_jerk) {
  iteration_count_++;

  if (!initialized_) {
    return icon::FailedPreconditionError(
        "ConstrainedCartesianVirtualDynamics is not initialized. Did you "
        "forget to call Initialize() before Step()?");
  }

  if (max_velocity < kAlmostZero) {
    return InvalidArgumentError(FixedStrCat<RealtimeStatus::kMaxMessageLength>(
        "max_velocity has a negative or close to zero entry. Got ",
        max_velocity));
  }
  if (max_acceleration < kAlmostZero) {
    return InvalidArgumentError(FixedStrCat<RealtimeStatus::kMaxMessageLength>(
        "max_acceleration has a negative or close to zero entry. "
        "Got ",
        max_acceleration));
  }
  if (max_jerk < kAlmostZero) {
    return InvalidArgumentError(FixedStrCat<RealtimeStatus::kMaxMessageLength>(
        "max_jerk has a negative or close to zero entry. Got ", max_jerk));
  }

  // TODO(b/375460419): constrained harmonic oscillator does not work with
  // infinite acceleration limit. Because of many recursive (and limit-adapted)
  // calls to the underlying 1 DoF constrained harmonic oscillator, we put a
  // (temporary) heuristic for limiting acceleration and jerk here. It should be
  // investigated if the algorithm works with infinite jerk limits.
  const double kMaxAccelerationLimitMultiplier = 1000.0;
  const double kMaxJerkLimitMultiplier = 2500.0;
  max_acceleration = std::min(max_acceleration,
                              kMaxAccelerationLimitMultiplier * max_velocity);
  max_jerk = std::min(max_jerk, kMaxJerkLimitMultiplier * max_acceleration);

  if (max_acceleration_last_cycle_.has_value()) {
    if ((max_acceleration - *max_acceleration_last_cycle_) *
            control_frequency_hz_ <
        -(1.0 + kSmallThreshold) * max_jerk) {
      INTRINSIC_RT_LOG(ERROR)
          << "Cartesian acceleration limit changes faster than jerk limit.";
    }
  }
  max_acceleration_last_cycle_ = max_acceleration;

  // Computation of the fully decoupled inertia and stiffness via
  // bi-diagonalization. The bi-diagonalization can be understood as a change of
  // basis, which transforms the impedance dynamics to a form where inertia and
  // stiffness matrix are strictly diagonal. We mark all quantities in the
  // diagonalized frame with a "diag" suffix.
  INTRINSIC_RT_RETURN_IF_ERROR(impedance_bidiagonalization_.Diagonalize(
      input_parameters.virtual_inertia_inverse.inverse(),
      input_parameters.stiffness));
  const eigenmath::VectorNd M_diag =
      impedance_bidiagonalization_.InertiaMatrixDiagonalized().diagonal();
  const eigenmath::VectorNd K_diag =
      impedance_bidiagonalization_.StiffnessMatrixDiagonalized().diagonal();

  // Retrieve the transformation matrices that transform quantities from the
  // current frame frame into the diagonalized frame and vice versa. We define x
  // = Vz where `x` is a vector in the current frame and `z` is a vector in
  // the diagonalized frame.
  const eigenmath::MatrixNd V =
      impedance_bidiagonalization_.EigenVectorBasisV();
  const eigenmath::MatrixNd Vinv =
      impedance_bidiagonalization_.EigenVectorBasisVInverse();

  // Computing damping matrix in the diagonalized frame using the result from
  // virtual_impedance_dynamics_bidiagonalization.h", D_diag = V^T*D*V.
  // Important sidenote: properties of the damping matrix are preserved under
  // this transformation. The reason is that the matrix version of the
  // computation of critical damping is "linear" in the relevant parts.
  eigenmath::MatrixNd D_diag_matrix =
      (V.transpose() * input_parameters.damping * V);
  const double kDiagonalCheckTolerance = 1e-5;
  if (!D_diag_matrix.isDiagonal(kDiagonalCheckTolerance)) {
    return icon::InternalError(
        "Damping matrix is not diagonal in diagonalized system.");
  }
  eigenmath::VectorNd D_diag = D_diag_matrix.diagonal();
  if (D_diag.minCoeff() < kDiagonalCheckTolerance) {
    return icon::InternalError(
        "Damping matrix is singular, which can lead to instability.");
  }

  // Prepare input quantities for the diagonalized control law in transformed
  // coordinates, i.e. the following section implements the control law
  //
  // z = Vinv * x.
  // ddz + D_diag*dz + K_diag*z = V^T*(f - f_ref + M*a_ref + D*v_ref + K*x_ref)
  //
  // where M_diag has been omitted because it is guaranteed to be Identity.
  eigenmath::VectorNd force_error(sensed_force -
                                  input_parameters.reference_force);

  INTRINSIC_RT_RETURN_IF_ERROR(
      cartesian_impedance::ApplyDeadband(deadband_force, force_error));
  eigenmath::VectorNd force_diagonal_space_with_feedforwards =
      -V.transpose() * input_parameters.force_selection_matrix * force_error +
      D_diag.asDiagonal() * Vinv * input_parameters.reference_velocity;

  // Multidimensional limits formulated as hyper-ellipsoid or hyper-cuboids.
  //
  // Hyperellipsoid Preliminaries: When we interprete Rn limits as inequalities
  // on the l2-norm of an Rn vector, we can write this as |x|_2 <= x_max, or
  // alternatively as quadratic form as x^T*I*x <= x_max².
  //
  // For JERK and ACCELERATION limits we are formulating a limit hyperellipsoid
  // subject to a change of basis since they can be handled when coupled across
  // DoFs by re-scaling solutions at acceleration and jerk level. After
  // bi-diagonalization, the jerk-limits and acceleration limits are degenerated
  // to a generic hyper-ellipsoid instead of a hyper-sphere. We can use the
  // equation z'Az <= 1 to evaluate how much a n-DoF vector z (in the
  // bi-diagonalized frame) violates the original jerk or acceleration limit.
  //
  // For VELOCITY limits, the algorithm here does not yet support coupling of
  // DoFs. We need to apply a conservative approximation which ensures that the
  // n-DoF-vector, when all DoFs are treated in a decoupled way, stays within
  // the l2-norm defined by the Rn velocity limits. To this end, we fit a
  // cube into the limit hypersphere. Even if all DoFs saturate their individual
  // DoF limits, the l2-norm of the combined n-DoF vector must remain within the
  // limit sphere. To this end we create a conservative approximation of the
  // l2-norm through the infinity norm as follows. Using the lemma
  // `|x|_2 <= sqrt(kNDof)*|x|_inf` we develop the following approximation:
  //
  // `|x|_2 = |V*z|_2 <= sqrt(kNDof)*|V*z|_inf` <= sqrt(kNDof)*|V|_inf*|z|_inf`
  //
  // where |z|_inf can be treated as an upper bound on the optimization vector z
  // in the diagonalized frame. From here, it follows that for the diagonalized
  // system, we can choose the upper bound
  //
  // |z|_inf = |x|_2 / (sqrt(kNDof)*|V|_inf)
  //
  // Use conservative limit hypercube for velocity and acceleration limits.
  const double V_inf_norm = MatrixInfinityNorm(V);
  const int ndof = V.rows();
  eigenmath::VectorNd bidiag_v_max = eigenmath::VectorNd::Constant(
      ndof, max_velocity / (std::sqrt(ndof) * V_inf_norm));

  if (kPrintDebugInfo) {
    INTRINSIC_RT_LOG(INFO) << "V_inf_norm: " << V_inf_norm;
    INTRINSIC_RT_LOG(INFO) << "bidiag_v_max: "
                           << eigenmath::ToFixedString(
                                  bidiag_v_max.transpose());
  }

  // Construct the positive semi-definite jerk limit hyperellipsoid weight
  // matrix. By construction, this matrix is guaranteed to be symmetric
  // positive semi-definite because for an arbitrary matrix V, V.transpose() * V
  // will be symmetric and positive semi-definite.
  eigenmath::MatrixNd jerk_limit_hyperellipsoid_weight_matrix =
      (V.transpose() * V) / ::intrinsic::IPow(max_jerk, 2);
  eigenmath::VectorNd jerk_limit_bounding_box_lengths =
      GetHyperellipsoidAxisAlignedBoundingBoxFromVinvAndScalarLimit(Vinv,
                                                                    max_jerk);

  const eigenmath::MatrixNd acc_limit_hyperellipsoid_weight_matrix =
      (V.transpose() * V) / ::intrinsic::IPow(max_acceleration, 2);
  const eigenmath::VectorNd acc_limit_bounding_box_lengths =
      GetHyperellipsoidAxisAlignedBoundingBoxFromVinvAndScalarLimit(
          Vinv, max_acceleration);

  // The limit hyperparameters are manually tuned by giftthaler@.
  const JerkLimitHyperparameters dof_limit_hyperparameters{
      .jerk_y_scaling = 0.7,  // Jerk limit at high acceleration multiplier
      .jerk_s_scaling = 1.3,  // Jerk limit intercept multiplier
  };

  INTRINSIC_RT_ASSIGN_OR_RETURN(
      const auto result,
      ComputePositionIncrementWithPotatoeAlgorithm(
          Vinv * q_k_minus_1_, Vinv * q_k_minus_2_, Vinv * q_k_minus_3_,
          Vinv * input_parameters.reference_position, Vinv * p_d_k_minus_1_,
          Vinv * p_d_k_minus_2_, force_diagonal_space_with_feedforwards, M_diag,
          D_diag, K_diag, bidiag_v_max, dof_limit_hyperparameters,
          control_frequency_hz_, acc_limit_bounding_box_lengths,
          acc_limit_hyperellipsoid_weight_matrix,
          jerk_limit_bounding_box_lengths,
          jerk_limit_hyperellipsoid_weight_matrix));

  // Transcribe pose state history.
  q_k_minus_3_ = q_k_minus_2_;
  q_k_minus_2_ = q_k_minus_1_;
  q_k_minus_1_ = V * result.q_k;
  p_d_k_minus_2_ = p_d_k_minus_1_;
  p_d_k_minus_1_ = input_parameters.reference_position;

  return eigenmath::VectorNd(q_k_minus_1_);
}

}  // namespace intrinsic::icon
