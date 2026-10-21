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

#include "intrinsic/motion_planning/trajectory_planning/topp/topp_solver_commons.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/control/algorithms/joint_limit_checker.h"
#include "intrinsic/icon/dynamics/rigid_body_interface.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/icon/utils/realtime_status_macro.h"
#include "intrinsic/icon/utils/realtime_status_or.h"
#include "intrinsic/kinematics/chain.h"
#include "intrinsic/kinematics/elements.h"
#include "intrinsic/kinematics/state.h"
#include "intrinsic/kinematics/types/cartesian_limits.h"
#include "intrinsic/kinematics/types/dynamic_limits_check_mode.h"
#include "intrinsic/kinematics/types/joint_limits.h"
#include "intrinsic/kinematics/types/joint_state.h"
#include "intrinsic/kinematics/types/joint_trajectories.h"
#include "intrinsic/kinematics/types/to_fixed_string.h"
#include "intrinsic/math/almost_equals.h"
#include "intrinsic/math/ipow.h"
#include "intrinsic/math/within_margin.h"
#include "intrinsic/motion_planning/trajectory_planning/interpolate_joint_trajectories.h"
#include "intrinsic/motion_planning/trajectory_planning/topp/integrate_squared_path_velocity_polynomial.h"
#include "intrinsic/motion_planning/trajectory_planning/topp/joint_optimization_options.pb.h"
#include "intrinsic/motion_planning/trajectory_planning/topp/path_sample.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic::topp {

namespace {

// Checks that the minimum and maximum translational values of the Cartesian
// velocity limits are the symmetric.
absl::Status AreSymmetric(const CartesianLimits& cart_limits) {
  const double kTolerance = 1e-4;
  if ((cart_limits.max_translational_velocity -
       cart_limits.min_translational_velocity.cwiseAbs())
          .norm() > kTolerance) {
    return absl::InvalidArgumentError(absl::StrCat(
        "The Cartesian velocity limits are not symmetric. The maximum "
        "translational velocity is ",
        absl::string_view(
            eigenmath::ToFixedString(cart_limits.max_translational_velocity)),
        " and the minimum translational velocity is ",
        absl::string_view(
            eigenmath::ToFixedString(cart_limits.min_translational_velocity)),
        "."));
  }
  return absl::OkStatus();
}

// Computes the Jacobian time derivative times joint velocity product.
absl::StatusOr<eigenmath::Vector6d>
ComputeJacobianTimeDerivativeVectorWithChain(
    const kinematics::Chain& chain, const eigenmath::VectorNd& joint_positions,
    const eigenmath::VectorNd& joint_velocities,
    const eigenmath::Vector3d& tip_p_target) {
  if (joint_positions.size() != chain.GetNumberDegreesOfFreedom()) {
    return absl::InvalidArgumentError(absl::StrCat(
        "The number of degrees of freedom of the kinematic chain does not "
        "match the size of the joint_positions. ",
        chain.GetNumberDegreesOfFreedom(), " vs. ", joint_positions.size(),
        "."));
  }
  if (joint_velocities.size() != chain.GetNumberDegreesOfFreedom()) {
    return absl::InvalidArgumentError(absl::StrCat(
        "The number of degrees of freedom of the kinematic chain does not "
        "match the size of the joint_velocities. ",
        chain.GetNumberDegreesOfFreedom(), " vs. ", joint_velocities.size(),
        "."));
  }

  // Constants used for the finite difference approximation.
  constexpr bool kCheckLimits = false;
  const kinematics::ElementId tip_id = chain.GetTipId();

  kinematics::State state(&chain);
  INTRINSIC_RT_RETURN_IF_ERROR(
      state.SetDofPositions(joint_positions, kCheckLimits));
  INTRINSIC_RT_RETURN_IF_ERROR(
      state.SetDofVelocities(joint_velocities, kCheckLimits));
  INTRINSIC_RT_RETURN_IF_ERROR(
      state.SetDofAccelerations(0.0 * joint_velocities, kCheckLimits));
  INTRINSIC_RT_ASSIGN_OR_RETURN(
      const eigenmath::Vector6d dJdt_times_dqdt,
      state.ComputeClassicalFrameAcceleration(
          tip_id, tip_p_target,
          kinematics::ReferenceFrame::kLocalRobotBaseAligned));

  return dJdt_times_dqdt;
}

}  // namespace

absl::StatusOr<std::vector<double>> JointVelocityLimitsToSquaredPathSpeedLimits(
    absl::Span<const PathSample> path_samples, double max_bound_value) {
  if (path_samples.empty()) {
    return absl::InvalidArgumentError("The path_samples vector is empty.");
  }
  if (max_bound_value <= 0.0) {
    return absl::InvalidArgumentError(
        absl::StrCat("The value of max_bound_value should be > 0.0, but got ",
                     max_bound_value, "."));
  }
  const auto num_points = path_samples.size();
  const auto num_dimensions = path_samples.front().Size();

  std::vector<double> squared_path_speed_limits;
  squared_path_speed_limits.reserve(num_points);
  for (const PathSample& sample : path_samples) {
    double max_x = std::numeric_limits<double>::infinity();
    for (size_t dof_id = 0; dof_id < num_dimensions; dof_id++) {
      if (sample.qp[dof_id] != 0) {
        const double limit_to_qp_ratio =
            sample.joint_limits.max_velocity[dof_id] / sample.qp[dof_id];
        max_x = std::min(max_x, ::intrinsic::IPow(limit_to_qp_ratio, 2));
      }
    }
    squared_path_speed_limits.push_back(std::min(max_x, max_bound_value));
  }
  return squared_path_speed_limits;
}

absl::StatusOr<std::vector<double>>
CartesianVelocityLimitsToSquaredPathSpeedLimits(
    absl::Span<const PathSample> path_samples,
    absl::Span<const eigenmath::Vector6d> jacobian_qp, double max_bound_value) {
  if (path_samples.empty()) {
    return absl::InvalidArgumentError("The path_samples vector is empty.");
  }
  if (path_samples.size() != jacobian_qp.size()) {
    return absl::InvalidArgumentError(absl::StrCat(
        "The number of rows of `jacobian_qp` is ", jacobian_qp.size(),
        ", but expected is ", path_samples.size(), "."));
  }
  const auto num_points = path_samples.size();

  constexpr double kNumericallyZeroMargin = 1e-9;
  std::vector<double> squared_path_speed_limits;
  squared_path_speed_limits.reserve(num_points);
  for (size_t sample_id = 0; sample_id < num_points; sample_id++) {
    const PathSample& sample = path_samples[sample_id];
    const eigenmath::Vector6d& jac_qp = jacobian_qp[sample_id];
    INTR_RETURN_IF_ERROR(AreSymmetric(sample.cart_limits))
        << " Sample " << sample_id << ".";
    // `max_x` stores the most constraining bound among the robot degrees of
    // freedom for the squared path variable values.
    double max_x = std::numeric_limits<double>::infinity();
    double max_cart_trans_vel =
        sample.cart_limits.max_translational_velocity.minCoeff();
    if (!::intrinsic::WithinMargin(jac_qp.head<3>().squaredNorm(), 0.0,
                                   kNumericallyZeroMargin)) {
      const double limit_to_jac_qp_ratio =
          ::intrinsic::IPow(max_cart_trans_vel, 2) /
          jac_qp.head<3>().squaredNorm();
      max_x = std::min(max_x, limit_to_jac_qp_ratio);
    }

    double max_cart_rot_vel = sample.cart_limits.max_rotational_velocity;
    if (!::intrinsic::WithinMargin(jac_qp.tail<3>().squaredNorm(), 0.0,
                                   kNumericallyZeroMargin)) {
      const double limit_to_jac_qp_ratio =
          ::intrinsic::IPow(max_cart_rot_vel, 2) /
          jac_qp.tail<3>().squaredNorm();
      max_x = std::min(max_x, limit_to_jac_qp_ratio);
    }
    squared_path_speed_limits.push_back(std::min(max_x, max_bound_value));
  }
  return squared_path_speed_limits;
}

absl::Status CwiseMinimumSquaredPathVelocity(
    absl::Span<double> b_joint_limits,
    absl::Span<const double> b_cartesian_limits) {
  if (b_joint_limits.size() != b_cartesian_limits.size()) {
    return absl::InvalidArgumentError(
        "The vectors `b_joint_limits` and `b_cartesian_limits` must match in "
        "size.");
  }

  std::transform(b_joint_limits.begin(), b_joint_limits.end(),
                 b_cartesian_limits.begin(), b_joint_limits.begin(),
                 [](double a, double b) { return std::min(a, b); });

  return absl::OkStatus();
}

absl::Status ValidatePathSamples(absl::Span<const PathSample> path_samples,
                                 size_t min_num_samples) {
  if (path_samples.size() < min_num_samples) {
    return absl::InvalidArgumentError(
        absl::StrCat("Number of path samples should be >= ", min_num_samples,
                     " but got ", path_samples.size(), "."));
  }
  const size_t ndofs = path_samples.front().Size();
  for (size_t i = 0; i < path_samples.size(); ++i) {
    const auto& sample = path_samples[i];
    if (!sample.IsValid() || sample.Size() != ndofs) {
      return absl::InvalidArgumentError(
          absl::StrCat("Path sample ", i, " is invalid. Expected size is ",
                       ndofs, " but got ", sample.Size(), "."));
    }
  }
  return absl::OkStatus();
}

absl::StatusOr<std::vector<double>> GetPathVariables(
    absl::Span<const PathSample> path_samples) {
  if (path_samples.empty()) {
    return absl::InvalidArgumentError(
        "The `path_samples` vector cannot be empty.");
  }
  std::vector<double> path_variables;
  path_variables.reserve(path_samples.size());
  for (const auto& sample : path_samples) {
    path_variables.push_back(sample.s);
  }
  return path_variables;
}

absl::StatusOr<std::vector<double>> ComputePathSampleSteps(
    absl::Span<const PathSample> path_samples) {
  if (path_samples.size() <= 1) {
    return absl::InvalidArgumentError(absl::StrCat(
        "path_samples should be >= 2, but got ", path_samples.size(), "."));
  }
  const size_t num_samples = path_samples.size();
  std::vector<double> ds(num_samples - 1);
  for (size_t i = 0; i < num_samples - 1; ++i) {
    ds[i] = path_samples[i + 1].s - path_samples[i].s;
  }
  return ds;
}

absl::StatusOr<std::vector<double>> ReconstructPathTimeStepsForLinearModel(
    absl::Span<const double> path_sample_steps,
    absl::Span<const double> squared_path_velocities) {
  if (path_sample_steps.size() + 1 != squared_path_velocities.size()) {
    return absl::InvalidArgumentError(absl::StrCat(
        "The size of squared_path_velocities ", squared_path_velocities.size(),
        " and path_sample_steps ", path_sample_steps.size(),
        " are not compatible. The size of path_sample_steps + 1 should be "
        "equal to the size of squared_path_velocities."));
  }
  std::vector<double> path_time_steps(path_sample_steps.size());
  for (size_t i = 0; i < path_sample_steps.size(); ++i) {
    double avg_path_velocity =
        0.5 * (std::sqrt(std::abs(squared_path_velocities[i])) +
               std::sqrt(std::abs(squared_path_velocities[i + 1])));
    path_time_steps[i] = path_sample_steps[i] / avg_path_velocity;
  }
  return path_time_steps;
}

absl::StatusOr<std::vector<double>>
ReconstructPathTimeStepsForEulerQuadraticModel(
    absl::Span<const double> path_sample_steps,
    absl::Span<const double> squared_path_velocities,
    absl::Span<const double> squared_path_velocities_first_derivative,
    absl::Span<const double> squared_path_velocities_second_derivative) {
  if (path_sample_steps.size() + 1 != squared_path_velocities.size()) {
    return absl::InvalidArgumentError(absl::StrCat(
        "The size of squared_path_velocities ", squared_path_velocities.size(),
        " and path_sample_steps ", path_sample_steps.size(),
        " are not compatible. The size of path_sample_steps + 1 should be "
        "equal to the size of squared_path_velocities."));
  }
  if (path_sample_steps.size() + 1 !=
      squared_path_velocities_first_derivative.size()) {
    return absl::InvalidArgumentError(absl::StrCat(
        "The size of squared_path_velocities_first_derivative ",
        squared_path_velocities_first_derivative.size(),
        " and path_sample_steps ", path_sample_steps.size(),
        " are not compatible. The size of path_sample_steps + 1 should be "
        "equal to the size of squared_path_velocities_first_derivative."));
  }
  if (path_sample_steps.size() !=
      squared_path_velocities_second_derivative.size()) {
    return absl::InvalidArgumentError(absl::StrCat(
        "The size of squared_path_velocities_second_derivative ",
        squared_path_velocities_second_derivative.size(),
        " and path_sample_steps ", path_sample_steps.size(),
        " are not compatible. The size of path_sample_steps should be equal to "
        "the size of squared_path_velocities_second_derivative."));
  }
  std::vector<double> path_time_steps(path_sample_steps.size());
  for (int i = 0; i < path_sample_steps.size(); ++i) {
    const double ds = path_sample_steps[i];
    QuadraticPolynomial polynomial{
        .coeffs = eigenmath::Vector3d(
            squared_path_velocities_second_derivative[i] / 2.0,
            squared_path_velocities_first_derivative[i],
            squared_path_velocities[i]),
        .range_start = 0.0,
        .range_end = ds};
    // Typically, the last polynomial is not truncated. But in the case that it
    // is slightly negative (thus the integral is not well defined), we truncate
    // it to be able to compute the integral.
    const bool is_last_polynomial = (i == path_sample_steps.size() - 1);
    INTR_ASSIGN_OR_RETURN(double path_time_step,
                          IntegrateSquaredPathVelocityPolynomial(
                              polynomial, /*truncate=*/is_last_polynomial));
    path_time_steps[i] = path_time_step;
  }
  return path_time_steps;
}

absl::StatusOr<CartesianConstraintsDotProducts>
ComputeCartesianConstraintsDotProducts(
    absl::Span<const PathSample> path_samples,
    icon::RigidBodyInterface* rigid_body_interface) {
  if (rigid_body_interface == nullptr) {
    return absl::InvalidArgumentError(
        "The rigid body interface cannot be a nullptr.");
  }
  if (path_samples.empty()) {
    return absl::InvalidArgumentError("The vector of path_samples is empty.");
  }
  if (path_samples.front().Size() != rigid_body_interface->GetNumDof()) {
    return absl::InvalidArgumentError(absl::StrCat(
        "The size of path_samples (", path_samples.front().Size(),
        ") should match the number of dofs of rigid_body_interface ",
        rigid_body_interface->GetNumDof(), "."));
  }

  const int num_samples = path_samples.size();
  const kinematics::ElementId tip_id =
      rigid_body_interface->GetDefaultTip().value();

  CartesianConstraintsDotProducts cartesian_constraints_dot_products;
  cartesian_constraints_dot_products.jacobian_qp.resize(num_samples);
  cartesian_constraints_dot_products.jacobian_qpp_plus_jacobian_derivative_qp
      .resize(num_samples);
  for (size_t i = 0; i < num_samples; i++) {
    const PathSample& sample = path_samples[i];
    INTRINSIC_RT_ASSIGN_OR_RETURN(
        const eigenmath::Matrix6Nd jacobian,
        rigid_body_interface->ComputeJacobian(
            sample.q, sample.tip_t_target.translation(), tip_id));
    INTRINSIC_RT_ASSIGN_OR_RETURN(
        const eigenmath::Vector6d jacobian_derivative_qp,
        rigid_body_interface->ComputeJacobianTimeDerivativeVector(
            sample.q, sample.qp, sample.tip_t_target.translation(), tip_id));

    cartesian_constraints_dot_products.jacobian_qp[i] = jacobian * sample.qp;
    cartesian_constraints_dot_products
        .jacobian_qpp_plus_jacobian_derivative_qp[i] =
        jacobian * sample.qpp + jacobian_derivative_qp;
  }
  return cartesian_constraints_dot_products;
}

absl::StatusOr<std::vector<PhaseSpaceCartesianConstraint>>
ComputePhaseSpaceCartesianConstraints(
    const CartesianConstraintsDotProducts& cartesian_constraints_dot_products,
    absl::Span<const PathSample> path_samples,
    const bool use_inner_approximation) {
  const size_t num_samples = path_samples.size();
  if ((cartesian_constraints_dot_products.jacobian_qp.size() != num_samples) ||
      (cartesian_constraints_dot_products
           .jacobian_qpp_plus_jacobian_derivative_qp.size() != num_samples)) {
    return absl::InvalidArgumentError(
        "The `path_samples` and `cartesian_constraints_dot_products` must "
        "match in size.");
  }

  std::vector<PhaseSpaceCartesianConstraint> phase_space_constraints;
  phase_space_constraints.reserve(num_samples);

  for (size_t id = 0; id < num_samples; ++id) {
    // Top 3 rows are translational, bottom 3 are rotational
    INTR_ASSIGN_OR_RETURN(
        PhaseSpaceCartAccTwoNormConstraint trans_constraint,
        ConstructPhaseSpaceCartAccTwoNormConstraint(
            cartesian_constraints_dot_products
                .jacobian_qpp_plus_jacobian_derivative_qp[id]
                .head<3>(),
            cartesian_constraints_dot_products.jacobian_qp[id].head<3>(),
            path_samples[id]
                .cart_limits.max_translational_acceleration.minCoeff(),
            use_inner_approximation));
    INTR_ASSIGN_OR_RETURN(
        PhaseSpaceCartAccTwoNormConstraint rot_constraint,
        ConstructPhaseSpaceCartAccTwoNormConstraint(
            cartesian_constraints_dot_products
                .jacobian_qpp_plus_jacobian_derivative_qp[id]
                .tail<3>(),
            cartesian_constraints_dot_products.jacobian_qp[id].tail<3>(),
            path_samples[id].cart_limits.max_rotational_acceleration,
            use_inner_approximation));
    phase_space_constraints.push_back(PhaseSpaceCartesianConstraint{
        .trans_constraint = std::move(trans_constraint),
        .rot_constraint = std::move(rot_constraint)});
  }
  return phase_space_constraints;
}

absl::StatusOr<CartesianConstraintsDotProducts>
ComputeCartesianConstraintsDotProducts(
    absl::Span<const PathSample> path_samples, const kinematics::Chain& chain) {
  if (path_samples.empty()) {
    return absl::InvalidArgumentError("The vector of path_samples is empty.");
  }
  if (path_samples.front().Size() != chain.GetNumberDegreesOfFreedom()) {
    return absl::InvalidArgumentError(absl::StrCat(
        "The size of path_samples (", path_samples.front().Size(),
        ") should match the number of dofs of the kinematics chain ",
        chain.GetNumberDegreesOfFreedom(), "."));
  }

  // Short names for constants.
  constexpr bool kCheckLimits = false;
  const size_t num_samples = path_samples.size();
  const kinematics::ElementId tip_id = chain.GetTipId();

  CartesianConstraintsDotProducts cartesian_constraints_dot_products;
  cartesian_constraints_dot_products.jacobian_qp.resize(num_samples);
  cartesian_constraints_dot_products.jacobian_qpp_plus_jacobian_derivative_qp
      .resize(num_samples);

  kinematics::State state(&chain);
  for (size_t i = 0; i < num_samples; i++) {
    const eigenmath::Vector3d& tip_p_target =
        path_samples[i].tip_t_target.translation();

    // Construct the places where the jacobian should be evaluated.
    const eigenmath::VectorNd& q = path_samples[i].q;
    const eigenmath::VectorNd& qp = path_samples[i].qp;
    const eigenmath::VectorNd& qpp = path_samples[i].qpp;

    INTRINSIC_RT_RETURN_IF_ERROR(state.SetDofPositions(q, kCheckLimits));
    INTRINSIC_RT_ASSIGN_OR_RETURN(const eigenmath::Matrix6Nd J,
                                  state.ComputeJacobian(tip_id, tip_p_target));
    INTR_ASSIGN_OR_RETURN(const eigenmath::Vector6d dJdt_times_dqdt,
                          ComputeJacobianTimeDerivativeVectorWithChain(
                              chain, q, qp, tip_p_target));

    // Compute the Cartesian constraint dot products.
    // xp = J * qp
    cartesian_constraints_dot_products.jacobian_qp[i] = J * qp;
    // xpp = J * qpp + dJdp * qp
    cartesian_constraints_dot_products
        .jacobian_qpp_plus_jacobian_derivative_qp[i] =
        J * qpp + dJdt_times_dqdt;
  }
  return cartesian_constraints_dot_products;
}

absl::StatusOr<TorqueConstraintPathComponent> TorqueConstraintPathComponents(
    const PathSample& path_sample, icon::RigidBodyInterface* dynamics) {
  if (!dynamics) {
    return absl::InvalidArgumentError(
        "The rigid body interface pointer cannot be a nullptr.");
  }
  const int ndof = dynamics->GetNumDof();
  if (path_sample.Size() != ndof) {
    return absl::InvalidArgumentError(
        absl::StrCat("The number of degrees of freedom from the path sample ",
                     path_sample.Size(),
                     " does not match the number of degrees of freedom from "
                     "the rigid body dynamics interface ",
                     ndof, "."));
  }

  INTRINSIC_RT_ASSIGN_OR_RETURN(
      const eigenmath::VectorNd generalized_gravity,
      dynamics->ComputeGeneralizedGravityVector(path_sample.q));
  INTRINSIC_RT_ASSIGN_OR_RETURN(
      const eigenmath::VectorNd coriolis_times_qp,
      dynamics->ComputeCoriolisVector(path_sample.q, path_sample.qp));
  INTRINSIC_RT_ASSIGN_OR_RETURN(
      const eigenmath::MatrixNd joint_space_inertia_matrix,
      dynamics->ComputeJointSpaceInertiaMatrix(path_sample.q));

  TorqueConstraintPathComponent component;
  component.gravity = generalized_gravity;
  component.mass_times_qp = joint_space_inertia_matrix * path_sample.qp;
  component.mass_times_qpp_plus_coriolis_times_qp =
      joint_space_inertia_matrix * path_sample.qpp + coriolis_times_qp;
  return component;
}

absl::StatusOr<std::vector<TorqueConstraintPathComponent>>
TorqueConstraintPathComponents(absl::Span<const PathSample> path_samples,
                               icon::RigidBodyInterface* dynamics) {
  if (path_samples.empty()) {
    return absl::InvalidArgumentError(
        "The vector of path_samples cannot be empty.");
  }

  std::vector<TorqueConstraintPathComponent> torque_constraints_path_components;
  torque_constraints_path_components.reserve(path_samples.size());
  for (const PathSample& path_sample : path_samples) {
    INTR_ASSIGN_OR_RETURN(
        const TorqueConstraintPathComponent component,
        TorqueConstraintPathComponents(path_sample, dynamics));
    torque_constraints_path_components.push_back(component);
  }

  return torque_constraints_path_components;
}

absl::StatusOr<std::vector<JointConstraintStep>>
CreateKinematicJointConstraintSteps(absl::Span<const PathSample> path_samples,
                                    const eigenmath::VectorNd& zero_offset) {
  if (path_samples.empty()) {
    return absl::InvalidArgumentError("The `path_samples` cannot be empty.");
  }
  if (path_samples.front().Size() != zero_offset.size()) {
    return absl::InvalidArgumentError(
        "The degrees of freedom of `path_samples` and `zero_offset` must "
        "match.");
  }
  if (!AlmostEquals(zero_offset.squaredNorm(), 0.0)) {
    return absl::InvalidArgumentError("The `zero_vector` must be zero.");
  }

  std::vector<JointConstraintStep> steps;
  steps.reserve(path_samples.size());
  for (size_t i = 0; i < path_samples.size(); ++i) {
    const PathSample& sample = path_samples[i];
    steps.push_back({
        .coeff_b = sample.qpp,
        .twice_coeff_bp = sample.qp,
        .offset = zero_offset,
        .limits = sample.joint_limits.max_acceleration,
    });
  }
  return steps;
}

absl::StatusOr<std::vector<JointConstraintStep>>
CreateDynamicJointConstraintSteps(
    absl::Span<const PathSample> path_samples,
    absl::Span<const TorqueConstraintPathComponent>
        torque_constraint_path_components) {
  if (path_samples.empty()) {
    return absl::InvalidArgumentError("The `path_samples` cannot be empty.");
  }
  if (path_samples.size() != torque_constraint_path_components.size()) {
    return absl::InvalidArgumentError(
        "The `path_samples` and `torque_constraint_path_components` must match "
        "in size.");
  }
  if (path_samples.front().Size() !=
      torque_constraint_path_components.front().gravity.size()) {
    return absl::InvalidArgumentError(
        "The degrees of freedom of `path_samples` and "
        "`torque_constraint_path_components` must match.");
  }

  std::vector<JointConstraintStep> steps;
  steps.reserve(path_samples.size());
  for (size_t i = 0; i < path_samples.size(); ++i) {
    const PathSample& kin_sample = path_samples[i];
    const TorqueConstraintPathComponent& dyn_sample =
        torque_constraint_path_components[i];
    steps.push_back({
        .coeff_b = dyn_sample.mass_times_qpp_plus_coriolis_times_qp,
        .twice_coeff_bp = dyn_sample.mass_times_qp,
        .offset = dyn_sample.gravity,
        .limits = kin_sample.joint_limits.max_torque,
    });
  }
  return steps;
}

bool PrecomputedPathConstraints::HasValidTorqueConstraintPathComponents(
    size_t expected_size) const {
  return expected_size > 0 &&
         torque_constraint_path_components.size() == expected_size;
}

bool PrecomputedPathConstraints::HasValidCartesianConstraintsDotProducts(
    size_t expected_size) const {
  return expected_size > 0 &&
         cartesian_constraints_dot_products.jacobian_qp.size() ==
             expected_size &&
         cartesian_constraints_dot_products
                 .jacobian_qpp_plus_jacobian_derivative_qp.size() ==
             expected_size;
}

bool PrecomputedPathConstraints::HasValidPhaseSpaceCartConstraints(
    size_t expected_size) const {
  return expected_size > 0 &&
         phase_space_cart_constraints.size() == expected_size;
}

absl::Status ValidateKinematicsChain(const kinematics::Chain& chain,
                                     const PathSample& sample) {
  if (chain.GetNumberDegreesOfFreedom() != sample.Size()) {
    return absl::InvalidArgumentError(absl::StrCat(
        "The number of degrees of freedom of the kinematic chain does not "
        "match the expected value ",
        sample.Size(), ", but got ", chain.GetNumberDegreesOfFreedom(), "."));
  }
  kinematics::State state(&chain);
  eigenmath::Matrix6Nd prev_jacobian;
  INTRINSIC_RT_RETURN_IF_ERROR(
      state.SetDofPositions(sample.q, /*check_limits=*/false));
  icon::RealtimeStatusOr<eigenmath::Matrix6Nd> jacobian_status =
      state.ComputeJacobian(chain.GetTipId(),
                            sample.tip_t_target.translation());
  if (!jacobian_status.ok()) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Chain is invalid, failed at the jacobian computation with error: ",
        jacobian_status.status().message(), "."));
  }
  return absl::OkStatus();
}

absl::StatusOr<std::vector<PathSample>> ScaleDynamicLimitsBySafetyMargin(
    double margin, absl::Span<const PathSample> path_samples) {
  if (margin <= 0.0 || margin > 1.0) {
    return absl::InvalidArgumentError(absl::StrCat(
        "The margin should be in the range (0.0, 1.0], but got ", margin, "."));
  }
  const double margin_pow_2 = margin * margin;
  const double margin_pow_3 = margin_pow_2 * margin;

  std::vector<PathSample> scaled_path_samples(path_samples.begin(),
                                              path_samples.end());
  for (PathSample& path_sample : scaled_path_samples) {
    JointLimits& scaled_joint_limits = path_sample.joint_limits;
    scaled_joint_limits.max_velocity *= margin;
    scaled_joint_limits.max_acceleration *= margin_pow_2;
    scaled_joint_limits.max_jerk *= margin_pow_3;
    scaled_joint_limits.max_torque *= margin_pow_2;

    CartesianLimits& scaled_cart_limits = path_sample.cart_limits;
    scaled_cart_limits.min_translational_velocity *= margin;
    scaled_cart_limits.max_translational_velocity *= margin;
    scaled_cart_limits.min_translational_acceleration *= margin_pow_2;
    scaled_cart_limits.max_translational_acceleration *= margin_pow_2;
    scaled_cart_limits.min_translational_jerk *= margin_pow_3;
    scaled_cart_limits.max_translational_jerk *= margin_pow_3;
    scaled_cart_limits.max_rotational_velocity *= margin;
    scaled_cart_limits.max_rotational_acceleration *= margin_pow_2;
    scaled_cart_limits.max_rotational_jerk *= margin_pow_3;
  }
  return scaled_path_samples;
}

absl::Status ScalePath(absl::Span<PathSample> path_samples,
                       double target_path_length) {
  if (path_samples.size() < 2) {
    return absl::InvalidArgumentError(
        "The path samples size should be larger than or equal to 2.");
  }
  if (target_path_length <= 0.0) {
    return absl::InvalidArgumentError(absl::StrCat(
        "The target path length should be larger than 0.0, but got ",
        target_path_length, "."));
  }

  const double scale = target_path_length / path_samples.back().s;
  for (PathSample& path_sample : path_samples) {
    path_sample.s *= scale;
    path_sample.qp /= scale;
    path_sample.qpp /= intrinsic::IPow(scale, 2);
    path_sample.qppp /= intrinsic::IPow(scale, 3);
  }
  return absl::OkStatus();
}

absl::StatusOr<JointTrajectoryPVA> ComposeAccelerationLimitedJointTrajectoryPVA(
    absl::Span<const PathSample> path_samples,
    absl::Span<const double> squared_path_velocities,
    DynamicLimitsCheckMode joint_dynamic_limits_check_mode) {
  if (path_samples.size() < 2) {
    return absl::InvalidArgumentError(
        absl::StrCat("There must be at least 2 `path_samples`. Got ",
                     path_samples.size(), "."));
  }
  if (path_samples.size() != squared_path_velocities.size()) {
    return absl::InvalidArgumentError(
        absl::StrCat("The sizes of `path_samples` (", path_samples.size(),
                     ") and `squared_path_velocities` (",
                     squared_path_velocities.size(), ") must match."));
  }

  INTR_ASSIGN_OR_RETURN(const std::vector<double> path_increments,
                        ComputePathSampleSteps(path_samples));

  INTR_ASSIGN_OR_RETURN(const std::vector<double> path_time_steps,
                        ReconstructPathTimeStepsForLinearModel(
                            path_increments, squared_path_velocities));

  const size_t num_samples = path_samples.size();
  std::vector<double> timings_at_samples(num_samples);
  timings_at_samples[0] = 0.0;
  for (size_t i = 1; i < num_samples; ++i) {
    timings_at_samples[i] = timings_at_samples[i - 1] + path_time_steps[i - 1];
  }

  std::vector<std::vector<double>> path_derivatives_at_samples(num_samples);
  for (size_t i = 0; i < num_samples; ++i) {
    path_derivatives_at_samples[i] = {squared_path_velocities[i]};
  }

  return ComposeJointTrajectoryPVA(
      path_samples, timings_at_samples, path_derivatives_at_samples,
      joint_dynamic_limits_check_mode,
      /*use_piecewise_constant_acceleration=*/true);
}

absl::StatusOr<JointTrajectoryPVA> ComposeJointTrajectoryPVA(
    absl::Span<const PathSample> path_samples,
    absl::Span<const double> timings_at_samples,
    absl::Span<const std::vector<double>> path_derivatives_at_samples,
    DynamicLimitsCheckMode joint_dynamic_limits_check_mode,
    bool use_piecewise_constant_acceleration) {
  if (path_samples.size() < 2) {
    return absl::InvalidArgumentError(
        absl::StrCat("The size of path_samples should be at least 2. Got ",
                     path_samples.size(), "."));
  }

  const int num_samples = path_samples.size();
  if (timings_at_samples.size() != num_samples ||
      path_derivatives_at_samples.size() != num_samples) {
    return absl::InvalidArgumentError(absl::StrCat(
        "The size of timings_at_samples (", timings_at_samples.size(),
        ") and of path_derivatives_at_samples (",
        path_derivatives_at_samples.size(),
        ") should match the size of path_samples (", path_samples.size(),
        ")."));
  }

  // Set the minimum required derivative size based on whether `bp` is needed.
  const size_t min_derivatives_size =
      use_piecewise_constant_acceleration ? 1 : 2;
  for (size_t i = 0; i < num_samples; ++i) {
    if (path_derivatives_at_samples[i].size() < min_derivatives_size) {
      return absl::InvalidArgumentError(
          absl::StrCat("The size of path_derivatives_at_samples[", i,
                       "] should be at least ", min_derivatives_size, ". Got ",
                       path_derivatives_at_samples[i].size(), "."));
    }
  }

  // Compose vector of time stamps, resulting joint velocities and joint
  // accelerations.
  JointStatePVA state;
  INTRINSIC_RT_RETURN_IF_ERROR(state.SetSize(path_samples.front().q.size()));
  std::vector<absl::Duration> time_stamps(num_samples);
  std::vector<JointStatePVA> joint_states_pva(num_samples, state);
  for (size_t i = 0; i < num_samples; ++i) {
    time_stamps[i] = absl::Seconds(timings_at_samples[i]);
    const double b = path_derivatives_at_samples[i][0];
    const double sdot = sqrt(std::abs(b));

    joint_states_pva[i].position = path_samples[i].q;
    joint_states_pva[i].velocity = sdot * path_samples[i].qp;
    if (use_piecewise_constant_acceleration) {
      if (i > 0) {
        joint_states_pva[i - 1].acceleration =
            (joint_states_pva[i].velocity - joint_states_pva[i - 1].velocity) /
            (timings_at_samples[i] - timings_at_samples[i - 1]);
      }
    } else {
      const double bp = path_derivatives_at_samples[i][1];
      const eigenmath::VectorNd& qp = path_samples[i].qp;
      const eigenmath::VectorNd& qpp = path_samples[i].qpp;
      joint_states_pva[i].acceleration = 0.5 * qp * bp + qpp * b;
    }
  }
  time_stamps[0] = absl::ZeroDuration();

  // Trajectories optimized subject to third-order constraints (such as joint
  // jerk or torque rate constraints: use_piecewise_constant_acceleration =
  // false) require a higher degree of smoothness for their basis functions and
  // thus for the polynomials used to interpolate the trajectory than the
  // trajectories without such constraints (use_piecewise_constant_acceleration
  // = true).
  const JointTrajectoryInterpolationType interpolation_type =
      (use_piecewise_constant_acceleration ? kCubicPolynomial
                                           : kQuinticPolynomial);
  return JointTrajectoryPVA::Create(
      std::move(joint_states_pva), std::move(time_stamps),
      joint_dynamic_limits_check_mode, interpolation_type);
}

absl::StatusOr<std::vector<int>> ComputeIndicesWhereNumericalJerksViolateLimits(
    const JointTrajectoryPVA& trajectory, const eigenmath::VectorNd& max_jerk,
    absl::Duration interpolation_step) {
  if (trajectory.size() < 2) {
    return absl::InvalidArgumentError(
        "The input `trajectory` must have size larger or equal than 2.");
  }
  if (interpolation_step <= absl::ZeroDuration()) {
    return absl::InvalidArgumentError(
        "The input `interpolation_step` must have a positive value.");
  }
  const int ndof = trajectory.data().front().size();
  if (ndof != max_jerk.size()) {
    return absl::InvalidArgumentError(
        absl::StrCat("The size of the trajectory states (", ndof,
                     ") does not match the size of the jerk limits (",
                     max_jerk.size(), ")."));
  }
  if ((max_jerk.array() <= 0.0).any()) {
    return absl::InvalidArgumentError("The jerk limits must be positive.");
  }

  // Define infinite joint limits, except for the jerk limits.
  const double kInfinity = std::numeric_limits<double>::infinity();
  JointLimits joint_limits = CreateSimpleJointLimits(
      /*ndof=*/ndof, /*max_position=*/kInfinity, /*max_velocity=*/kInfinity,
      /*max_acceleration=*/kInfinity, /*max_jerk=*/kInfinity);
  joint_limits.max_jerk = max_jerk;

  // Compute the timestamps where the jerk limit is violated.
  std::vector<absl::Duration> time_stamps;
  icon::JointLimitChecker joint_limit_checker(interpolation_step, ndof);
  for (absl::Duration d = absl::ZeroDuration(); d <= trajectory.Duration();
       d += interpolation_step) {
    INTRINSIC_RT_ASSIGN_OR_RETURN(
        JointStatePVA state,
        InterpolateJointTrajectoryInternalType(trajectory, d));
    if (!joint_limit_checker.Check(state.position, joint_limits).ok()) {
      time_stamps.push_back(d);
    }
  }

  // Compute the corresponding indices in the `trajectory`.
  int index = 0;
  std::vector<int> indices_where_jerk_violates_limits;
  for (const absl::Duration& time_stamp : time_stamps) {
    for (; index < trajectory.size(); ++index) {
      if (trajectory.time_stamps()[index] >= time_stamp) {
        break;
      }
    }
    indices_where_jerk_violates_limits.push_back(index);
  }

  // Remove duplicates.
  indices_where_jerk_violates_limits.erase(
      std::unique(indices_where_jerk_violates_limits.begin(),
                  indices_where_jerk_violates_limits.end()),
      indices_where_jerk_violates_limits.end());

  return indices_where_jerk_violates_limits;
}

absl::StatusOr<TrajectoryOvershoot> ComputeTrajectoryOvershoot(
    const JointTrajectoryPVA& trajectory,
    absl::Span<const PathSample> path_samples,
    const absl::Duration interpolation_step, const double tolerance_factor) {
  if (trajectory.size() < 2) {
    return absl::InvalidArgumentError(
        "The input `trajectory` must have size larger or equal than 2.");
  }
  if (trajectory.size() != path_samples.size()) {
    return absl::InvalidArgumentError(
        absl::StrCat("The trajectory size (", trajectory.size(),
                     ") must match the number of path samples (",
                     path_samples.size(), ")."));
  }
  if (interpolation_step <= absl::ZeroDuration()) {
    return absl::InvalidArgumentError(
        "The input `interpolation_step` must have a positive value.");
  }
  if (tolerance_factor <= 0.0) {
    return absl::InvalidArgumentError(
        absl::StrCat("The `tolerance_factor` must be positive, got ",
                     tolerance_factor, "."));
  }
  const int ndof = trajectory.data().front().size();
  for (int i = 0; i < path_samples.size(); ++i) {
    const auto& limits = path_samples[i].joint_limits;
    if (path_samples[i].Size() != ndof || limits.size() != ndof) {
      return absl::InvalidArgumentError(
          absl::StrCat("Path sample at index ", i, " has size ",
                       path_samples[i].Size(), ", expected ndof ", ndof, "."));
    }
    if ((limits.max_velocity.array() <= 0.0).any() ||
        limits.max_velocity.hasNaN()) {
      return absl::InvalidArgumentError(
          absl::StrCat("Path sample at index ", i,
                       " has non-positive or NaN velocity limits."));
    }
    if ((limits.max_acceleration.array() <= 0.0).any() ||
        limits.max_acceleration.hasNaN()) {
      return absl::InvalidArgumentError(
          absl::StrCat("Path sample at index ", i,
                       " has non-positive or NaN acceleration limits."));
    }
    if ((limits.max_jerk.array() <= 0.0).any() || limits.max_jerk.hasNaN()) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Path sample at index ", i, " has non-positive or NaN jerk limits."));
    }
  }

  TrajectoryOvershoot overshoot;
  overshoot.vel_overshoot.reserve(trajectory.size());
  overshoot.acc_overshoot.reserve(trajectory.size());
  overshoot.jerk_overshoot.reserve(trajectory.size());
  icon::JointLimitChecker joint_limit_checker(interpolation_step, ndof);
  const auto& time_stamps = trajectory.time_stamps();

  // Returns the index of the last trajectory sample whose timestamp is smaller
  // or equal than `time`, i.e. the first sample of the segment containing
  // `time`. Clamped to zero for times before the start of the trajectory.
  auto first_sample_of_segment_containing =
      [&time_stamps](absl::Duration time) {
        const auto it =
            std::upper_bound(time_stamps.begin(), time_stamps.end(), time);
        return std::max(0, static_cast<int>(it - time_stamps.begin()) - 1);
      };

  int index = 0;
  for (absl::Duration d = absl::ZeroDuration(); d <= trajectory.Duration();
       d += interpolation_step) {
    INTRINSIC_RT_ASSIGN_OR_RETURN(
        const JointStatePVA state,
        InterpolateJointTrajectoryInternalType(trajectory, d));

    while (index < trajectory.size() && trajectory.time_stamps()[index] < d) {
      ++index;
    }
    if (index >= trajectory.size()) {
      index = trajectory.size() - 1;
    }

    const JointLimits& sample_limits = path_samples[index].joint_limits;
    JointLimits scaled_limits = sample_limits;
    scaled_limits.max_velocity *= tolerance_factor;
    scaled_limits.max_acceleration *= tolerance_factor;
    scaled_limits.max_jerk *= tolerance_factor;

    const bool check_ok =
        joint_limit_checker.Check(state.position, scaled_limits).ok();
    if (!check_ok) {
      const JointStatePVAJ& last_state = joint_limit_checker.last_state();

      // The k-th backward finite difference at time `d` depends on the
      // positions in the time window `[d - k * interpolation_step, d]`.
      auto check_and_record_overshoot =
          [index, d, interpolation_step, &first_sample_of_segment_containing](
              const int derivative_order,
              const eigenmath::VectorNd& state_derivative,
              const eigenmath::VectorNd& scaled_limit,
              const eigenmath::VectorNd& sample_limit,
              std::vector<Overshoot>& overshoots) {
            if ((state_derivative.cwiseAbs().array() > scaled_limit.array())
                    .any()) {
              const double overshoot_value =
                  (state_derivative.cwiseAbs().array() / sample_limit.array())
                      .maxCoeff();
              const int window_start_index = std::min(
                  index, first_sample_of_segment_containing(
                             d - derivative_order * interpolation_step));
              if (!overshoots.empty() && overshoots.back().index == index) {
                overshoots.back().overshoot_factor = std::max(
                    overshoots.back().overshoot_factor, overshoot_value);
                overshoots.back().window_start_index =
                    std::min(overshoots.back().window_start_index.value_or(
                                 window_start_index),
                             window_start_index);
              } else {
                overshoots.push_back(
                    {.index = index,
                     .overshoot_factor = overshoot_value,
                     .window_start_index = window_start_index});
              }
            }
          };

      check_and_record_overshoot(
          /*derivative_order=*/1, last_state.velocity,
          scaled_limits.max_velocity, sample_limits.max_velocity,
          overshoot.vel_overshoot);
      check_and_record_overshoot(
          /*derivative_order=*/2, last_state.acceleration,
          scaled_limits.max_acceleration, sample_limits.max_acceleration,
          overshoot.acc_overshoot);
      check_and_record_overshoot(/*derivative_order=*/3, last_state.jerk,
                                 scaled_limits.max_jerk, sample_limits.max_jerk,
                                 overshoot.jerk_overshoot);
    }
  }

  return overshoot;
}

double TrajectoryOvershoot::MaxOvershootFactor() const {
  double max_factor = 1.0;
  for (const Overshoot& entry : vel_overshoot) {
    max_factor = std::max(max_factor, entry.overshoot_factor);
  }
  for (const Overshoot& entry : acc_overshoot) {
    max_factor = std::max(max_factor, entry.overshoot_factor);
  }
  for (const Overshoot& entry : jerk_overshoot) {
    max_factor = std::max(max_factor, entry.overshoot_factor);
  }
  return max_factor;
}

absl::Status ScalePathSamplesJointLimitsByOvershoot(
    const TrajectoryOvershoot& overshoot, absl::Span<PathSample> path_samples,
    const ScaleOvershootOptions& options) {
  if (options.vel_neighborhood_window < 0 ||
      options.acc_neighborhood_window < 0 ||
      options.jerk_neighborhood_window < 0) {
    return absl::InvalidArgumentError(
        absl::StrCat("The neighborhood window counts in options must be "
                     "non-negative. Got vel: ",
                     options.vel_neighborhood_window,
                     ", acc: ", options.acc_neighborhood_window,
                     ", jerk: ", options.jerk_neighborhood_window, "."));
  }

  if (path_samples.empty()) {
    if (!overshoot.vel_overshoot.empty() || !overshoot.acc_overshoot.empty() ||
        !overshoot.jerk_overshoot.empty()) {
      return absl::InvalidArgumentError(
          "The input `path_samples` cannot be empty when overshoots are "
          "provided.");
    }
    return absl::OkStatus();
  }

  auto scale_limits_for_overshoots =
      [&path_samples](absl::Span<const Overshoot> overshoots,
                      const int neighborhood_window,
                      auto get_limit_vector) -> absl::Status {
    // Early return if there are no overshoots.
    if (overshoots.empty()) {
      return absl::OkStatus();
    }

    // Define the maximum overshoot factors for the limits of each path sample.
    std::vector<double> max_overshoot_factors(path_samples.size(), 1.0);
    for (const Overshoot& entry : overshoots) {
      if (entry.index < 0 || entry.index >= path_samples.size()) {
        return absl::OutOfRangeError(
            absl::StrCat("Overshoot index ", entry.index,
                         " is out of range [0, ", path_samples.size(), ")."));
      }
      if (entry.overshoot_factor <= 0.0 || std::isnan(entry.overshoot_factor)) {
        return absl::InvalidArgumentError(
            absl::StrCat("The `overshoot_factor` must be positive and non-NaN, "
                         "got ",
                         entry.overshoot_factor, "."));
      }
      // The neighborhood spans `neighborhood_window` samples on both sides of
      // `entry.index`. Since the violation is detected via backward finite
      // differences, the neighborhood is further extended backwards if needed
      // to cover all samples whose segments intersect the finite-difference
      // time window of the overshoot, i.e. down to `window_start_index`.
      const int window_start_index =
          std::min(entry.index, entry.window_start_index.value_or(entry.index));
      if (window_start_index < 0) {
        return absl::OutOfRangeError(
            absl::StrCat("Overshoot window start index ", window_start_index,
                         " is out of range [0, ", path_samples.size(), ")."));
      }
      const int start_idx = std::max(
          0, std::min(entry.index - neighborhood_window, window_start_index));
      const int end_idx = std::min(static_cast<int>(path_samples.size()) - 1,
                                   entry.index + neighborhood_window);
      for (int i = start_idx; i <= end_idx; ++i) {
        max_overshoot_factors[i] =
            std::max(max_overshoot_factors[i], entry.overshoot_factor);
      }
    }

    // Scale path samples limits by the maximum overshoot factor.
    for (int i = 0; i < path_samples.size(); ++i) {
      if (max_overshoot_factors[i] > 1.0) {
        get_limit_vector(path_samples[i].joint_limits) /=
            max_overshoot_factors[i];
      }
    }
    return absl::OkStatus();
  };

  INTR_RETURN_IF_ERROR(scale_limits_for_overshoots(
      overshoot.vel_overshoot, options.vel_neighborhood_window,
      [](JointLimits& limits) -> eigenmath::VectorNd& {
        return limits.max_velocity;
      }));
  INTR_RETURN_IF_ERROR(scale_limits_for_overshoots(
      overshoot.acc_overshoot, options.acc_neighborhood_window,
      [](JointLimits& limits) -> eigenmath::VectorNd& {
        return limits.max_acceleration;
      }));
  INTR_RETURN_IF_ERROR(scale_limits_for_overshoots(
      overshoot.jerk_overshoot, options.jerk_neighborhood_window,
      [](JointLimits& limits) -> eigenmath::VectorNd& {
        return limits.max_jerk;
      }));
  if (options.scale_acceleration_on_jerk_overshoot) {
    INTR_RETURN_IF_ERROR(scale_limits_for_overshoots(
        overshoot.jerk_overshoot, options.acc_neighborhood_window,
        [](JointLimits& limits) -> eigenmath::VectorNd& {
          return limits.max_acceleration;
        }));
  }

  return absl::OkStatus();
}

absl::Status CleanNumericalZeroes(Eigen::Ref<eigenmath::MatrixXd> matrix,
                                  const double tolerance) {
  if (!matrix.allFinite()) {
    return absl::InvalidArgumentError(
        "Matrix contains nan or infinite values.");
  }

  matrix = matrix.unaryExpr([tolerance](const double v) {
    return std::abs(v) < tolerance ? 0.0 : v;
  });

  return absl::OkStatus();
}

}  // namespace intrinsic::topp
