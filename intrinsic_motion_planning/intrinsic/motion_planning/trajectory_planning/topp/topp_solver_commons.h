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

#ifndef INTRINSIC_MOTION_PLANNING_TRAJECTORY_PLANNING_TOPP_TOPP_SOLVER_COMMONS_H_
#define INTRINSIC_MOTION_PLANNING_TRAJECTORY_PLANNING_TOPP_TOPP_SOLVER_COMMONS_H_

#include <cstddef>
#include <optional>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/dynamics/rigid_body_interface.h"
#include "intrinsic/kinematics/chain.h"
#include "intrinsic/kinematics/types/dynamic_limits_check_mode.h"
#include "intrinsic/kinematics/types/joint_trajectories.h"
#include "intrinsic/motion_planning/trajectory_planning/topp/joint_optimization_options.pb.h"
#include "intrinsic/motion_planning/trajectory_planning/topp/path_sample.h"
#include "intrinsic/motion_planning/trajectory_planning/topp/phase_space_cartesian_acceleration_two_norm_constraint.h"

namespace intrinsic::topp {

// Minimum allowed value for the squared path velocity `b`. This guarantees
// that the linear approximation of jerk constraints and reachability sets is
// valid.
constexpr double kMinimumSquaredPathVelocity = 1.0e-3;

// Default value to upper bound the squared path velocity bounds.
constexpr double kMaxBoundSquaredPathVelocity = 1.0e6;

// Threshold below which a value is considered numerically zero. Elements below
// this threshold will be zeroed to improve numerical conditioning of the
// matrices describing the optimization problem.
constexpr double kMatrixNumericalValueConsideredZero = 1.0e-13;

// Details about a dynamic limit overshoot at a trajectory index.
struct Overshoot {
  // Index of the trajectory sample where the limit is violated. This is the
  // first trajectory sample whose timestamp is larger or equal than the time at
  // which the violation was detected.
  int index = 0;

  // Factor by which the limit is exceeded relative to the nominal limit.
  double overshoot_factor = 0.0;

  // Index of the first trajectory sample that influences the violation. Limit
  // violations are detected via backward finite differences of positions
  // sampled uniformly in time, i.e. a violation of the k-th derivative detected
  // at time `t` depends on the positions in the time window
  // `[t - k * interpolation_step, t]`. This window can span many trajectory
  // samples when the samples are denser in time than the `interpolation_step`.
  // All samples in `[window_start_index, index]` contribute to the violation.
  // If unset, it is assumed to be equal to `index`.
  std::optional<int> window_start_index = std::nullopt;
};

// Result of checking trajectory dynamic limit overshoots for velocity,
// acceleration, and jerk. Each vector contains unique elements per trajectory
// index. When multiple time steps at the same trajectory index violate limits,
// the one with the largest overshoot factor is preserved.
struct TrajectoryOvershoot {
  std::vector<Overshoot> vel_overshoot;
  std::vector<Overshoot> acc_overshoot;
  std::vector<Overshoot> jerk_overshoot;

  // Returns the maximum overshoot factor across velocity, acceleration, and
  // jerk violations, or 1.0 if there are no violations.
  double MaxOvershootFactor() const;
};

// Options controlling the neighborhood window of path sample indices to scale
// around each overshoot index for each dynamic limit derivative type. For an
// overshoot at index `id`, samples in `[start, id + window]` are scaled, where
// `start = min(id - window, window_start_index)`. Overshoots are detected via
// backward finite differences, so the neighborhood is extended backwards to
// `Overshoot::window_start_index` (if set) when the finite-difference time
// window spans more than `window` samples.
struct ScaleOvershootOptions {
  // Neighborhood window (half-width) to scale around velocity limit overshoots.
  // For an overshoot at index `id`, samples in
  // `[min(id - vel_neighborhood_window, window_start_index),
  //   id + vel_neighborhood_window]` are scaled.
  int vel_neighborhood_window = 1;

  // Neighborhood window (half-width) to scale around acceleration limit
  // overshoots. For an overshoot at index `id`, samples in
  // `[min(id - acc_neighborhood_window, window_start_index),
  //   id + acc_neighborhood_window]` are scaled.
  int acc_neighborhood_window = 2;

  // Neighborhood window (half-width) to scale around jerk limit overshoots.
  // For an overshoot at index `id`, samples in
  // `[min(id - jerk_neighborhood_window, window_start_index),
  //   id + jerk_neighborhood_window]` are scaled.
  int jerk_neighborhood_window = 3;

  // If true, also scales acceleration limits when jerk overshoots occur.
  // Because jerk represents the time rate of change of acceleration, lowering
  // peak acceleration in the vicinity of a jerk violation bounds the
  // acceleration transitions when jerk-only scaling is insufficient.
  bool scale_acceleration_on_jerk_overshoot = false;
};

// Converts the constraint `|dq/dt| <= joint_velocity_limits` into an upper
// bound on the path speed. The time derivative of the path dq/dt can be
// expressed by the chain rule as dq/ds * ds/dt. Thus, for each degree of
// freedom we have (dq/ds * ds/dt)[dof] <= joint_velocity_limits[dof]. Then,
// only the most restrictive bound is maintained. ds/dt <=
// joint_velocity_limits[dof] / (dq/ds[dof]). Algorithmically, we need bounds on
// the squared path speed, thus we return the squared bound on the path speed.
// The `max_bound_value` optionally imposes a maximum value on these limits to
// improve numerical conditioning. In particular, when dq/ds[dof] = 0 or close
// to zero for all the degrees of freedom, the bound is very large. In this
// case, the `max_bound_value` would limit the bound to make the problem
// definition more numerically stable.
absl::StatusOr<std::vector<double>> JointVelocityLimitsToSquaredPathSpeedLimits(
    absl::Span<const PathSample> path_samples,
    double max_bound_value = kMaxBoundSquaredPathVelocity);

// Converts the constraint `|dx/dt| <= cart_velocity_limits` into an upper
// bound on the path speed. This constraint can be written as `|J * dq/dt| <=
// cart_velocity_limits`, which is similar to joint velocity limit constraints
// and can thus be treated in the same way. It becomes `|J * dq/ds * ds/dt| <=
// cart_velocity_limits`, where `jacobian_qp` represents `J * dq/ds`.
// Algorithmically, we need bounds on the squared path speed, thus we return the
// squared bound on the path speed. The `max_bound_value` optionally imposes a
// maximum value on these limits to improve numerical conditioning.
absl::StatusOr<std::vector<double>>
CartesianVelocityLimitsToSquaredPathSpeedLimits(
    absl::Span<const PathSample> path_samples,
    absl::Span<const eigenmath::Vector6d> jacobian_qp,
    double max_bound_value = kMaxBoundSquaredPathVelocity);

// Computes the component-wise minimum between two vectors of squared path
// velocities and overwrites the first vector. This function performs an
// in-place component-wise update: `b_joint_limits[i] = min(b_joint_limits[i],
// b_cartesian_limits[i])` for all i.
absl::Status CwiseMinimumSquaredPathVelocity(
    absl::Span<double> b_joint_limits,
    absl::Span<const double> b_cartesian_limits);

// Validates that the input vector of `path_samples` is consistent and has the
// required minimum number of path samples (which is algorithm dependent) is
// satisfied. Returns an invalid argument error if:
// - The number of samples within `path_samples` is below specified threshold
//   `min_num_samples`.
// - Any sample of `path_samples` is invalid or has an incorrect size.
absl::Status ValidatePathSamples(absl::Span<const PathSample> path_samples,
                                 size_t min_num_samples);

// Returns a vector composed by the path variables of each path sample in
// `path_samples`.
absl::StatusOr<std::vector<double>> GetPathVariables(
    absl::Span<const PathSample> path_samples);

// Returns the path samples increments of ds for a vector of `path_samples`.
// Returns an invalid argument error if the input vector is not large enough. It
// has to contain at least two samples.
absl::StatusOr<std::vector<double>> ComputePathSampleSteps(
    absl::Span<const PathSample> path_samples);

// Returns the optimal time steps from the path sample steps and optimal
// squared path velocities. The `squared_path_velocities` should be of the size
// of `path_sample_steps` + 1. The reconstruction of the path time steps assumes
// the `squared_path_velocities` are piecewise linear. The formula comes from
// the minimization of time over the entire trajectory, as in:
//     t = T         s = 1   1          s = 1     1
//   Integral dt = Integral --- ds =  Integral ------- ds.
//     t = 0         s = 0  s_d         s = 0  sqrt(b)
// which can be numerically approximated as:
//  K - 1           2.0 delta_s
//   Sum    ---------------------------
//  k = 0   sqrt(b[k]) + sqrt(b[k + 1])
// Thus, this function returns each of the terms of the above sum.
absl::StatusOr<std::vector<double>> ReconstructPathTimeStepsForLinearModel(
    absl::Span<const double> path_sample_steps,
    absl::Span<const double> squared_path_velocities);

// Returns the optimal time steps from the path sample steps and optimal
// squared path velocities and derivatives. The `squared_path_velocities` should
// be of the size of `path_sample_steps` + 1. The
// `squared_path_velocities_first_derivative` should be of the size of
// `path_sample_steps` + 1. The `squared_path_velocities_second_derivative`
// should be of the size of `path_sample_steps`. The reconstruction of the path
// time steps assumes the `squared_path_velocities` are piecewise quadratic
// based on an Euler based integration method. The time steps comes from the
// minimization of time over the entire trajectory, as in:
//     t = T         s = 1   1          s = 1     1
//   Integral dt = Integral --- ds =  Integral ------- ds.
//     t = 0         s = 0  s_d         s = 0  sqrt(b)
// where b[k+1] = b[k] + ds[k] * bp[k] + 0.5 * ds[k] * ds[k] * bpp[k]:
//  - b[k]: squared path velocity at sample k.
//  - bp[k]: first derivative of squared path velocity at sample k.
//  - bpp[k]: second derivative of squared path velocity at sample k.
//  - ds[k]: path sample step at sample k.
// Then, the above integral can be computed as:
//  K - 1   s = ds[k]                          1.0
//   Sum    Integral  ------------------------------------------------- ds
//  k = 0    s = 0     sqrt(b[k] + ds * bp[k] + 0.5 * ds * ds * bpp[k])
// where the integral is computed analytically.
// Thus, this function returns the timesteps corresponding to the each path
// sample step.
absl::StatusOr<std::vector<double>>
ReconstructPathTimeStepsForEulerQuadraticModel(
    absl::Span<const double> path_sample_steps,
    absl::Span<const double> squared_path_velocities,
    absl::Span<const double> squared_path_velocities_first_derivative,
    absl::Span<const double> squared_path_velocities_second_derivative);

// Pre-computed dot products to impose Cartesian velocity and acceleration
// limits.
struct CartesianConstraintsDotProducts {
  std::vector<eigenmath::Vector6d> jacobian_qp;
  std::vector<eigenmath::Vector6d> jacobian_qpp_plus_jacobian_derivative_qp;
};

// Pre-computes dot products to impose Cartesian velocity and acceleration
// limits. The required dot products are
//  - jacobian times q_prime,
//  - jacobian times q_double_prime + jacobian path derivative times
//  q_prime.
// This comes from the following derivation. Joint accelerations are given
// by:
//   q_dd = q_p * a + q_pp * b   (where a = s_dd and b = s_d^2).
// Here _d means a derivative with respect to time and _p a derivative with
// respect to the path variable 's' being optimized.
// Cartesian accelerations can then be written as:
//   x_dd = J * q_dd + J_d * q_d   (where J_d = J_p * s_d)
//        = (J * q_p) * a + (J * q_pp + J_p * q_p) * b
// Notice that Cartesian velocities also require J * q_p. Thus nothing else
// needs to be pre-computed.
// It fills out `jacobian_q_double_prime_plus_jacobian_derivative_q_prime`
// and `jacobian_qp` based on the input `path_samples` (that provides joint
// positions, joint velocities with respect to the path variable) for the
// given model described by `rigid_body_interface`. The
// `rigid_body_interface` implements derivatives analytically.
absl::StatusOr<CartesianConstraintsDotProducts>
ComputeCartesianConstraintsDotProducts(
    absl::Span<const PathSample> path_samples,
    icon::RigidBodyInterface* rigid_body_interface);

// Same as above, but uses a kinematics chain instead of a rigid body interface,
// which allows to compute the Jacobian derivative only in a numerical fashion.
absl::StatusOr<CartesianConstraintsDotProducts>
ComputeCartesianConstraintsDotProducts(
    absl::Span<const PathSample> path_samples, const kinematics::Chain& chain);

// Pre-computed phase-space (`b`-`bp` space) affine approximation of 2-norm
// Cartesian acceleration constraints. The Cartesian acceleration norm is
// defined on the Cartesian space, but for time-optimal path parametrization,
// these Cartesian space constraints are mapped into the phase-space domain,
// which is a single dimensional space. In this space, `b` denotes the squared
// path velocity (`ds/dt^2`) and `bp` denotes its first derivative (`bp =
// db(s)/ds`).
struct PhaseSpaceCartesianConstraint {
  PhaseSpaceCartAccTwoNormConstraint trans_constraint;
  PhaseSpaceCartAccTwoNormConstraint rot_constraint;
};

// Constructs the phase-space affine approximation of the 2-norm constraint on
// the Cartesian acceleration for each of the provided
// `cartesian_constraints_dot_products`. Each sample contains the Cartesian
// products `j_qpp_plus_jp_qp`, and `j_qp`, that define the Cartesian
// acceleration as follows:
//   cart_acc = j_qpp_plus_jp_qp * b + 0.5 * j_qp * bp
// In the above notation `j` denotes the Jacobian, `qp` and `qpp` are the first
// and second order path derivatives. The path variables `b` and `bp` denote the
// squared path velocity (`b(s) = ds/dt^2`) and its first derivative (`bp =
// db(s)/ds`) with respect to the path correspondingly. The Cartesian rotational
// and translational limits are defined within the `path_samples`. Note that
// only the minimum coefficient of the translational acceleration limits is
// used. Based on those elements, the phase-space approximation can be
// constructed either using an interior approximation of the true 2-norm
// constraint (`use_inner_approximation` set to true) or an outer approximation.
// `use_inner_approximation` defaults to true.
absl::StatusOr<std::vector<PhaseSpaceCartesianConstraint>>
ComputePhaseSpaceCartesianConstraints(
    const CartesianConstraintsDotProducts& cartesian_constraints_dot_products,
    absl::Span<const PathSample> path_samples,
    const bool use_inner_approximation = true);

// Pre-computes components to impose joint torque constraints, given by:
//   τmin <= M(q)q_dd + c(q, q_d) + g(q) <= τmax
// Where q,q_d,q_dd are the joint coordinates, velocities, and accelerations.
// M(q) is the joint space inertia matrix.
// c(q, q_d) are quadratic velocity forces resulting from the Coriolis matrix.
// g(q) are gravitational forces.
// τ is the vector of joint torques.
// τmin and τmax are the torque limits.
// In terms of path variables, this equation can be formulated as in:
//   q_d = q_p * s_d
//   q_dd = q_p * s_dd + q_pp * s_d^2
//   τmin <= M(q)(q_p * s_dd + q_pp * s_d^2) + c(q, q_p)s_d^2 + g(q) <= τmax
//   τmin <= (M(q) q_p) * s_dd + (M(q) q_pp + c(q, q_p)) * s_d^2  + g(q) <= τmax
// Thus to write the torque constraints as a function of the path variables, we
// require the above detailed quantities. For now, no external forces are
// considered. However, they could be easily incorporated.
struct TorqueConstraintPathComponent {
  eigenmath::VectorNd gravity;
  eigenmath::VectorNd mass_times_qp;
  eigenmath::VectorNd mass_times_qpp_plus_coriolis_times_qp;
};

// Computes the fixed terms from the above equations (used to construct a torque
// constraint) at the `path_sample`'s joint position and velocity using the
// `dynamics` rigid body interface pointer. Returns an error if the `dynamics`
// is invalid or if the `path_sample`'s dofs does not match the expected dofs of
// the `dynamics`.
absl::StatusOr<TorqueConstraintPathComponent> TorqueConstraintPathComponents(
    const PathSample& path_sample, icon::RigidBodyInterface* dynamics);

// Same as above but for a vector of `path_samples`.
absl::StatusOr<std::vector<TorqueConstraintPathComponent>>
TorqueConstraintPathComponents(absl::Span<const PathSample> path_samples,
                               icon::RigidBodyInterface* dynamics);

// Stores precomputed path-dependent constraint components (dynamic constraints
// to enforce torque limits, Cartesian kinematics dot products, and phase-space
// Cartesian constraints to enforce Cartesian acceleration constraints) that can
// be reused across trajectory optimization calls on the same geometric path.
struct PrecomputedPathConstraints {
  // Components to impose joint torque constraints.
  std::vector<TorqueConstraintPathComponent> torque_constraint_path_components;

  // Dot products to impose Cartesian velocity and acceleration limits.
  CartesianConstraintsDotProducts cartesian_constraints_dot_products;

  // Phase-space Cartesian acceleration constraints.
  std::vector<PhaseSpaceCartesianConstraint> phase_space_cart_constraints;

  // Returns true if `torque_constraint_path_components` has the expected number
  // of samples and `expected_size` is larger than zero.
  bool HasValidTorqueConstraintPathComponents(size_t expected_size) const;

  // Returns true if `cartesian_constraints_dot_products` has the expected
  // number of samples and `expected_size` is larger than zero.
  bool HasValidCartesianConstraintsDotProducts(size_t expected_size) const;

  // Returns true if `phase_space_cart_constraints` has the expected number of
  // samples and `expected_size` is larger than zero.
  bool HasValidPhaseSpaceCartConstraints(size_t expected_size) const;
};

// Stores the definition of a second-order constraint, i.e. either a kinematics
// joint acceleration constraint or a dynamics joint torque constraint.
struct JointConstraintStep {
  // Coefficient of the squared path velocity `b = ds/dt^2`.
  // - For kinematics: `qpp`.
  // - For dynamics: `M * qpp + C * qp`.
  Eigen::Ref<const eigenmath::VectorNd> coeff_b;

  // Contains exactly twice the mathematical coefficient of `bp = db(s)/ds` that
  // needs to be used to enforce the joint constraint. It is used to avoid
  // temporary allocations during step factory construction.
  // - For kinematics: `qp` (The true coefficient is
  //   `0.5 * twice_coeff_bp = 0.5 * qp`).
  // - For dynamics: `M * qp` (The true coefficient is
  //   `0.5 * twice_coeff_bp = 0.5 * M * qp`).
  Eigen::Ref<const eigenmath::VectorNd> twice_coeff_bp;

  // Offset.
  // - For kinematics: Zero vector.
  // - For dynamics: gravity `g`.
  Eigen::Ref<const eigenmath::VectorNd> offset;

  // Limits.
  // - For kinematics: `max_acceleration`.
  // - For dynamics: `max_torque`.
  Eigen::Ref<const eigenmath::VectorNd> limits;
};

// Constructs a vector of kinematic joint constraint steps from path samples.
// LIFETIME WARNING: The returned JointConstraintStep objects contain views
// (Eigen::Ref) pointing directly to data inside `path_samples` and
// `zero_offset`. The underlying inputs MUST outlive the returned vector.
//
// `path_samples` are a non-empty collection of path trajectory points.
// `zero_offset` is a persistent pre-allocated zero vector representing the
// kinematic offset. Returns a vector of constraint steps if successful. Returns
// an error if `path_samples` is empty, or if the DoF size of `zero_offset`
// doesn't match the path samples.
absl::StatusOr<std::vector<JointConstraintStep>>
CreateKinematicJointConstraintSteps(
    absl::Span<const PathSample> path_samples ABSL_ATTRIBUTE_LIFETIME_BOUND,
    const eigenmath::VectorNd& zero_offset ABSL_ATTRIBUTE_LIFETIME_BOUND);

// Constructs a vector of dynamic torque constraint steps mapping rigid-body
// terms.
//
// LIFETIME WARNING: The returned JointConstraintStep objects contain views
// (Eigen::Ref) pointing directly to data inside `path_samples` and
// `torque_constraint_path_components`. The underlying inputs MUST outlive the
// returned vector.
//
// `path_samples` is a non-empty collection of path trajectory points.
// `torque_constraint_path_components` are evaluated dynamic parameters matching
// the path samples. Returns a vector of constraint steps if successful. Returns
// an error if
// - `path_samples` is empty.
// - the sizes of the two input spans do not match.
// - the DoF size of the dynamic components doesn't match the path.
absl::StatusOr<std::vector<JointConstraintStep>>
CreateDynamicJointConstraintSteps(
    absl::Span<const PathSample> path_samples ABSL_ATTRIBUTE_LIFETIME_BOUND,
    absl::Span<const TorqueConstraintPathComponent>
        torque_constraint_path_components ABSL_ATTRIBUTE_LIFETIME_BOUND);

// Validates that the kinematics `chain` is of the same size as the input
// `sample` and that the construction of jacobians to impose Cartesian
// constraints works as expected.
absl::Status ValidateKinematicsChain(const kinematics::Chain& chain,
                                     const PathSample& sample);

// Scales all joint and Cartesian limits by a safety margin between 0 and 1,
// to have a small margin of numerical tolerance for the construction of the
// motion trajectory. Returns a vector of path samples where only joint and
// Cartesian limits were scaled. Joint and Cartesian positional limits are not
// affected by this function.
absl::StatusOr<std::vector<PathSample>> ScaleDynamicLimitsBySafetyMargin(
    double margin, absl::Span<const PathSample> path_samples);

// Scales in place the path variable and path derivatives of the input
// `path_samples` with a constant squared path velocity model to a desired
// `target_path_length`.
absl::Status ScalePath(absl::Span<PathSample> path_samples,
                       double target_path_length = 1.0);

// Constructs the output JointTrajectoryPVA for an acceleration-limited motion
// plan. The `path_samples` provide the input joint configurations and basis
// functions to compose joint velocities and joint accelerations.
// The `squared_path_velocities` provide the optimal piecewise linear squared
// path velocities at the `path_samples`. The flag
// `joint_dynamic_limits_check_mode` marks the trajectory as acceleration or
// torque limited.
absl::StatusOr<JointTrajectoryPVA> ComposeAccelerationLimitedJointTrajectoryPVA(
    absl::Span<const PathSample> path_samples,
    absl::Span<const double> squared_path_velocities,
    DynamicLimitsCheckMode joint_dynamic_limits_check_mode);

// Constructs the output JointTrajectoryPVA. `path_samples` provides the input
// joint configurations and basis functions to compose joint velocities and
// joint accelerations based on the `path_derivatives_at_samples` which provides
// the values of squared path velocities and its derivatives.
// `timings_at_samples` provides the time steps at each path sample position.
// The flag `joint_dynamic_limits_check_mode` marks the trajectory as
// acceleration or torque limited. The flag
// `use_piecewise_constant_acceleration` switches between composing the
// accelerations as a difference of velocities
// (`use_piecewise_constant_acceleration`=true) and computing the accelerations
// based on the basis functions evaluated at the grid points
// (`use_piecewise_constant_acceleration`=false).
absl::StatusOr<JointTrajectoryPVA> ComposeJointTrajectoryPVA(
    absl::Span<const PathSample> path_samples,
    absl::Span<const double> timings_at_samples,
    absl::Span<const std::vector<double>> path_derivatives_at_samples,
    DynamicLimitsCheckMode joint_dynamic_limits_check_mode,
    bool use_piecewise_constant_acceleration);

// Uniformly interpolates in time the input `trajectory` with the given
// `interpolation_step`. Computes the jerks as third order backward finite
// differences of the positions sampled uniformly in time. Computes the
// timestamps where the numerically computed jerks violate the `max_jerk` limit.
// A vector of `trajectory` indices that correspond to the `timestamps` is
// finally returned. The indices are computed by finding the first index where
// the time of the `trajectory` is larger than the previously computed
// timestamps where the jerk limit is violated. If no jerk limit is violated,
// an empty vector is returned.
absl::StatusOr<std::vector<int>> ComputeIndicesWhereNumericalJerksViolateLimits(
    const JointTrajectoryPVA& trajectory, const eigenmath::VectorNd& max_jerk,
    absl::Duration interpolation_step);

// Uniformly interpolates in time the input `trajectory` with the given
// `interpolation_step`. Computes velocity, acceleration, and jerk using
// numerical backward finite differences. For each time step, queries joint
// limits from the corresponding `path_samples` (using the lower-bound index of
// the timestamp in `trajectory.time_stamps()`), scales velocity, acceleration,
// and jerk limits by `tolerance_factor`, and checks for limit violations. The
// `tolerance_factor` should typically be >= 1.0, a reasonable range is
// `[1.05, 1.10]`. Returns a `TrajectoryOvershoot` containing unique trajectory
// indices and maximum violation factors (relative to nominal limits) for
// velocity, acceleration, and jerk overshoots. Each overshoot also reports the
// `window_start_index`, i.e. the first trajectory sample whose segment
// intersects the finite-difference time window of the violation.
absl::StatusOr<TrajectoryOvershoot> ComputeTrajectoryOvershoot(
    const JointTrajectoryPVA& trajectory,
    absl::Span<const PathSample> path_samples,
    absl::Duration interpolation_step, double tolerance_factor = 1.0);

// Scales down in place the joint velocity, acceleration, and jerk limits of the
// input `path_samples` based on the detected `overshoot`. For each limit type
// and each overshoot index `id`, scales the limits of the path samples in the
// neighborhood `[min(id - neighborhood_window, start), id +
// neighborhood_window]`, where `start` is the overshoot `window_start_index` if
// set and `id` otherwise (configured via `options`, clamped to valid sample
// indices) by dividing by the maximum overshoot factor covering each index.
absl::Status ScalePathSamplesJointLimitsByOvershoot(
    const TrajectoryOvershoot& overshoot, absl::Span<PathSample> path_samples,
    const ScaleOvershootOptions& options = {});

// Numbers of the `matrix` whose absolute value falls below the provided
// `tolerance` are considered zero and thus explicitly set to zero.
absl::Status CleanNumericalZeroes(
    Eigen::Ref<eigenmath::MatrixXd> matrix,
    const double tolerance = kMatrixNumericalValueConsideredZero);

}  // namespace intrinsic::topp

#endif  // INTRINSIC_MOTION_PLANNING_TRAJECTORY_PLANNING_TOPP_TOPP_SOLVER_COMMONS_H_
