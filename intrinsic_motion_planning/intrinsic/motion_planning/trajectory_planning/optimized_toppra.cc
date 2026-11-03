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

#include "intrinsic/motion_planning/trajectory_planning/optimized_toppra.h"

#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/dynamics/rigid_body_interface.h"
#include "intrinsic/icon/dynamics/validate_rigid_body_interface.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/icon/utils/realtime_status_macro.h"
#include "intrinsic/icon/utils/realtime_status_or.h"
#include "intrinsic/kinematics/types/check_joint_limits.h"
#include "intrinsic/kinematics/types/dynamic_limits_check_mode.h"
#include "intrinsic/kinematics/types/joint_limits.h"
#include "intrinsic/kinematics/types/joint_state.h"
#include "intrinsic/kinematics/types/joint_trajectories.h"
#include "intrinsic/kinematics/types/to_fixed_string.h"
#include "intrinsic/motion_planning/trajectory_planning/topp/bspline_squared_path_velocity.h"
#include "intrinsic/motion_planning/trajectory_planning/topp/joint_optimization_options.pb.h"
#include "intrinsic/motion_planning/trajectory_planning/topp/path_sample.h"
#include "intrinsic/motion_planning/trajectory_planning/topp/path_velocity_propagator.h"
#include "intrinsic/motion_planning/trajectory_planning/topp/topp_solver_commons.h"
#include "intrinsic/motion_planning/trajectory_planning/topp/topp_trajectory_result.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic::topp {

namespace {

// Constant safety margin that scales down joint and Cartesian limits to
// avoid failures due to numerical tolerances.
constexpr double kJointAndCartesianLimitsSafetyMargin = 0.98;

// Maximum number of attempts that iterative ToppRA has to produce a solution
// that satisfies the given input limits. Each attempt implies a reduction on
// the violated limits at the particular timesteps where the violation happened.
constexpr size_t kMaxSolutionAttemptsToppRA = 10;

// Defines a maximum threshold that a given degree of freedom is allowed to
// violate joint velocity, joint acceleration or joint torque limits when
// evaluating if a JointTrajectoryPVA satisfies the imposed constraints.
constexpr double kSmallToleranceOfAllowedLimitsViolation = 1e-6;

// Type of the joint constraint used: `kJointAcceleration` indicates limits over
// joint accelerations (kinematic constraints), `kJointTorque` indicates limits
// over joint torques (dynamic constraints). `kNone` implies that none of the
// above mentioned constraints are imposed. Only one or the other type of
// constraint is possible, but being able to differentiate them allows to return
// a meaningful message for error handling. Regardless of this flag, joint
// velocity constraints, Cartesian velocity and Cartesian acceleration
// constraints are respected.
enum class JointConstraintType {
  kJointAcceleration,  // Constraint imposes a limit over joint accelerations.
  kJointTorque,        // Constraint imposes a limit over joint torques.
  kNone,               // No joint constraints.
};

// Checks whether the optimized acceleration-limited trajectory satisfies the
// specified joint velocity and joint acceleration limits and returns `true` in
// this case. Otherwise, it returns `false`.
absl::StatusOr<bool> IsAccelerationLimitedTrajectoryWithinLimits(
    const JointTrajectoryPVA& trajectory,
    absl::Span<const PathSample> path_samples,
    const OptimizationOptions& options) {
  if (trajectory.size() != path_samples.size()) {
    return absl::InvalidArgumentError(
        absl::StrCat("The size of the trajectory ", trajectory.size(),
                     " does not match the size of the path samples ",
                     path_samples.size(), "."));
  }
  for (size_t i = 0; i < trajectory.size(); ++i) {
    const JointStatePVA& state = trajectory.data().at(i);
    INTRINSIC_RT_ASSIGN_OR_RETURN(
        auto limit_check_kinematics,
        IsWithinLimits(state, path_samples[i].joint_limits));
    if (!limit_check_kinematics.v_ok ||
        (options.kinematic_constraints()
             .activate_joint_acceleration_constraints() &&
         !limit_check_kinematics.a_ok)) {
      return false;
    }
  }
  return true;
}

// Computes the joint torque corresponding to the input `state` obtained using
// the `dynamics` model of the robot. Returns an error if the dynamics are
// invalid.
icon::RealtimeStatusOr<JointStateT> ComputeTorqueForJointStatePVA(
    const JointStatePVA& state, icon::RigidBodyInterface& dynamics) {
  if (dynamics.GetNumDof() != state.size()) {
    return icon::FailedPreconditionError(
        absl::StrCat("The size of the dynamics ", dynamics.GetNumDof(),
                     " does not match the state ", state.size(), "."));
  }

  JointStateT joint_torque;
  INTRINSIC_RT_RETURN_IF_ERROR(joint_torque.SetSize(state.size()));
  INTRINSIC_RT_ASSIGN_OR_RETURN(
      joint_torque.torque,
      dynamics.ComputeInverseDynamics(state.position, state.velocity,
                                      state.acceleration));
  return joint_torque;
}

// Checks whether the optimized torque-limited trajectory satisfies the
// specified joint velocity and joint torque limits and, returns true in this
// case. Otherwise, it returns false.
absl::StatusOr<bool> IsTorqueLimitedTrajectoryWithinLimits(
    const JointTrajectoryPVA& trajectory,
    absl::Span<const PathSample> path_samples,
    const OptimizationOptions& options, icon::RigidBodyInterface& dynamics) {
  if (trajectory.size() != path_samples.size()) {
    return absl::InvalidArgumentError(
        absl::StrCat("The size of the trajectory ", trajectory.size(),
                     " does not match the size of the path samples ",
                     path_samples.size(), "."));
  }
  for (size_t i = 0; i < trajectory.size(); ++i) {
    const JointStatePVA& state = trajectory.data().at(i);
    INTRINSIC_RT_ASSIGN_OR_RETURN(
        auto limit_check_kinematics,
        IsWithinLimits(state, path_samples[i].joint_limits));
    if (!limit_check_kinematics.v_ok) {
      return false;
    }

    INTRINSIC_RT_ASSIGN_OR_RETURN(
        JointStateT generalized_force,
        ComputeTorqueForJointStatePVA(state, dynamics));
    INTRINSIC_RT_ASSIGN_OR_RETURN(
        LimitCheckResult limit_check_dynamics,
        IsWithinLimits(generalized_force, path_samples[i].joint_limits));
    if (!limit_check_dynamics.t_ok &&
        options.dynamic_constraints().activate_joint_torque_constraints()) {
      return false;
    }
  }
  return true;
}

// Checks whether the optimized trajectory satisfies the specified joint
// velocity and, either joint acceleration or joint torque limits and returns
// true in this case. Otherwise, it returns false to signal that joint velocity
// and, either joint acceleration or joint torque limits should be scaled down
// appropriately for given timestamps.
absl::StatusOr<bool> IsTrajectoryWithinLimits(
    const JointTrajectoryPVA& trajectory,
    absl::Span<const PathSample> path_samples,
    const OptimizationOptions& options, icon::RigidBodyInterface* dynamics) {
  if (trajectory.joint_dynamic_limits_check_mode() ==
      DynamicLimitsCheckMode::kCheckJointAcceleration) {
    return IsAccelerationLimitedTrajectoryWithinLimits(trajectory, path_samples,
                                                       options);
  } else {
    return IsTorqueLimitedTrajectoryWithinLimits(trajectory, path_samples,
                                                 options, *dynamics);
  }
}

// Reduces the joint component limits from
// `joint_component_current_path_sample_joint_limits` by a scaled amount of the
// `joint_component_limit_violation`. This violation is computed as the
// difference between the currently optimized value of the `joint_component` and
// a set of fixed `joint_component_limits_with_safety_margin`. The scaling is
// given by `scaling_limit_factor`. Returns an error if the amount of violation
// goes beyond the current values of
// `joint_component_current_path_sample_joint_limits`. Otherwise, it returns a
// set of appropriately `joint_component_scaled_limits` with which a new
// solution can be constructed.
absl::StatusOr<eigenmath::VectorNd> ScaleJointLimit(
    const eigenmath::VectorNd& joint_component,
    const eigenmath::VectorNd& joint_component_limits_with_safety_margin,
    const eigenmath::VectorNd& joint_component_current_path_sample_joint_limits,
    const double scaling_limit_factor, absl::string_view joint_component_name) {
  eigenmath::VectorNd joint_component_scaled_limits =
      joint_component_current_path_sample_joint_limits;
  for (size_t dof = 0; dof < joint_component.size(); ++dof) {
    double joint_component_limit_violation =
        std::abs(joint_component[dof]) -
        joint_component_limits_with_safety_margin[dof];
    if (joint_component_limit_violation >
        kSmallToleranceOfAllowedLimitsViolation) {
      if (scaling_limit_factor * joint_component_limit_violation >=
          joint_component_current_path_sample_joint_limits[dof]) {
        return absl::InternalError(
            absl::StrCat("ToppRA solver failed at constructing a trajectory "
                         "that satisfies joint ",
                         joint_component_name,
                         " limits, likely due to problems with the "
                         "discretization of inputs samples."));
      }
      joint_component_scaled_limits[dof] -=
          scaling_limit_factor * joint_component_limit_violation;
    }
  }
  return joint_component_scaled_limits;
}

// Reduces the joint velocity limits from `current_path_sample_joint_limits` by
// a scaled amount of the joint velocity limit violation. This violation is
// computed as the difference between the currently optimized `joint_velocity`
// and a set of fixed `limits_with_safety_margin`. The scaling is given by
// `scaling_limit_factor`. Returns an error if the amount of violation goes
// beyond the current values of `current_path_sample_joint_limits`. Otherwise,
// it returns a set of appropriately `scaled_limits` with which a new solution
// can be constructed.
absl::StatusOr<JointLimits> ScaleJointVelocityLimit(
    const eigenmath::VectorNd& joint_velocity,
    const JointLimits& limits_with_safety_margin,
    const JointLimits& current_path_sample_joint_limits,
    const double scaling_limit_factor) {
  JointLimits scaled_limits = current_path_sample_joint_limits;
  INTR_ASSIGN_OR_RETURN(
      scaled_limits.max_velocity,
      ScaleJointLimit(joint_velocity, limits_with_safety_margin.max_velocity,
                      current_path_sample_joint_limits.max_velocity,
                      scaling_limit_factor,
                      /*joint_component_name=*/"velocity"));
  return scaled_limits;
}

// Reduces the joint acceleration limits from `current_path_sample_joint_limits`
// by a scaled amount of the joint acceleration limit violation. This violation
// is computed as the difference between the currently optimized
// `joint_acceleration` and a set of fixed `limits_with_safety_margin`. The
// scaling is given by `scaling_limit_factor`. Returns an error if the amount of
// violation goes beyond the current values of
// `current_path_sample_joint_limits`. Otherwise, it returns a set of
// appropriately `scaled_limits` with which a new solution can be constructed.
absl::StatusOr<JointLimits> ScaleJointAccelerationLimit(
    const eigenmath::VectorNd& joint_acceleration,
    const JointLimits& limits_with_safety_margin,
    const JointLimits& current_path_sample_joint_limits,
    const double scaling_limit_factor) {
  JointLimits scaled_limits = current_path_sample_joint_limits;
  INTR_ASSIGN_OR_RETURN(
      scaled_limits.max_acceleration,
      ScaleJointLimit(joint_acceleration,
                      limits_with_safety_margin.max_acceleration,
                      current_path_sample_joint_limits.max_acceleration,
                      scaling_limit_factor,
                      /*joint_component_name=*/"acceleration"));
  return scaled_limits;
}

// Reduces the joint torque limits from `current_path_sample_joint_limits` by a
// scaled amount of the joint torque limit violation. This violation is computed
// as the difference between the currently optimized `joint_torque` and a set of
// fixed `limits_with_safety_margin`. The scaling is given by
// `scaling_limit_factor`. Returns an error if the amount of violation goes
// beyond the current values of `current_path_sample_joint_limits`. Otherwise,
// it returns a set of appropriately `scaled_limits` with which a new solution
// can be constructed.
absl::StatusOr<JointLimits> ScaleJointTorqueLimit(
    const eigenmath::VectorNd& joint_torque,
    const JointLimits& limits_with_safety_margin,
    const JointLimits& current_path_sample_joint_limits,
    const double scaling_limit_factor) {
  JointLimits scaled_limits = current_path_sample_joint_limits;
  INTR_ASSIGN_OR_RETURN(
      scaled_limits.max_torque,
      ScaleJointLimit(joint_torque, limits_with_safety_margin.max_torque,
                      current_path_sample_joint_limits.max_torque,
                      scaling_limit_factor,
                      /*joint_component_name=*/"torque"));
  return scaled_limits;
}

// Scales down joint velocity and, either joint acceleration or joint torque
// limits that have been violated to resolve the problem subject to the newly
// constrained limits.
absl::Status ScaleLimits(
    const JointTrajectoryPVA& trajectory,
    absl::Span<const PathSample> path_samples_with_safety_margin,
    absl::Span<PathSample> path_samples_with_current_limits,
    const OptimizationOptions& options, icon::RigidBodyInterface* dynamics) {
  if (trajectory.size() != path_samples_with_safety_margin.size()) {
    return absl::InvalidArgumentError(absl::StrCat(
        "The size of the trajectory ", trajectory.size(),
        " does not match the size of the path samples with safety margin ",
        path_samples_with_safety_margin.size(), "."));
  }
  if (trajectory.size() != path_samples_with_current_limits.size()) {
    return absl::InvalidArgumentError(absl::StrCat(
        "The size of the trajectory ", trajectory.size(),
        " does not match the size of the path samples with current limits ",
        path_samples_with_current_limits.size(), "."));
  }

  const bool use_joint_acceleration_constraints =
      options.kinematic_constraints().activate_joint_acceleration_constraints();
  const bool use_joint_torque_constraints =
      options.dynamic_constraints().activate_joint_torque_constraints();
  for (size_t i = 0; i < trajectory.size(); ++i) {
    // Check of kinematic constraints.
    const JointStatePVA& state = trajectory.data().at(i);
    INTRINSIC_RT_ASSIGN_OR_RETURN(
        auto limit_check_kinematics,
        IsWithinLimits(state, path_samples_with_safety_margin[i].joint_limits));

    constexpr double kLimitFactor = 1.05;
    if (!limit_check_kinematics.v_ok) {
      INTR_ASSIGN_OR_RETURN(
          path_samples_with_current_limits[i].joint_limits,
          ScaleJointVelocityLimit(
              state.velocity, path_samples_with_safety_margin[i].joint_limits,
              path_samples_with_current_limits[i].joint_limits, kLimitFactor));
    }
    if (use_joint_acceleration_constraints && !limit_check_kinematics.a_ok) {
      INTR_ASSIGN_OR_RETURN(
          path_samples_with_current_limits[i].joint_limits,
          ScaleJointAccelerationLimit(
              state.acceleration,
              path_samples_with_safety_margin[i].joint_limits,
              path_samples_with_current_limits[i].joint_limits, kLimitFactor));
    }

    // Check of dynamic constraints.
    if (use_joint_torque_constraints && dynamics) {
      INTRINSIC_RT_ASSIGN_OR_RETURN(
          JointStateT generalized_force,
          ComputeTorqueForJointStatePVA(state, *dynamics));
      INTRINSIC_RT_ASSIGN_OR_RETURN(
          LimitCheckResult limit_check_dynamics,
          IsWithinLimits(generalized_force,
                         path_samples_with_safety_margin[i].joint_limits));
      if (!limit_check_dynamics.t_ok) {
        INTR_ASSIGN_OR_RETURN(
            path_samples_with_current_limits[i].joint_limits,
            ScaleJointTorqueLimit(
                generalized_force.torque,
                path_samples_with_safety_margin[i].joint_limits,
                path_samples_with_current_limits[i].joint_limits,
                kLimitFactor));
      }
    }
  }
  return absl::OkStatus();
}

}  // namespace

absl::StatusOr<bool> ToppRA::TimeParametrizePath(
    absl::Span<const PathSample> path_samples) {
  const auto num_points = path_samples.size();
  const auto num_dimensions = path_samples.front().Size();

  INTR_ASSIGN_OR_RETURN(const std::vector<double> path_increments,
                        ComputePathSampleSteps(path_samples));

  // Caching commonly used options variables.
  const bool use_cartesian_velocity_constraints =
      options_.cartesian_constraints()
          .activate_cartesian_velocity_constraints();
  const bool use_cartesian_acceleration_constraints =
      options_.cartesian_constraints()
          .activate_cartesian_acceleration_constraints();
  JointConstraintType joint_constraint_type = JointConstraintType::kNone;
  if (options_.kinematic_constraints()
          .activate_joint_acceleration_constraints()) {
    joint_constraint_type = JointConstraintType::kJointAcceleration;
  } else if (options_.dynamic_constraints()
                 .activate_joint_torque_constraints()) {
    joint_constraint_type = JointConstraintType::kJointTorque;
  }

  // Pre-compute dot product coefficients for Cartesian limits.
  std::vector<eigenmath::Vector6d> jacobian_qp(num_points,
                                               eigenmath::Vector6d::Zero());
  std::vector<eigenmath::Vector6d> jacobian_qpp_plus_jacobian_derivative_qp(
      num_points, eigenmath::Vector6d::Zero());
  if (use_cartesian_velocity_constraints ||
      use_cartesian_acceleration_constraints) {
    if (data_.cartesian_constraints_dot_products == std::nullopt) {
      INTR_ASSIGN_OR_RETURN(
          data_.cartesian_constraints_dot_products,
          ComputeCartesianConstraintsDotProducts(path_samples, *chain_));
    }
    jacobian_qp = data_.cartesian_constraints_dot_products->jacobian_qp;
    jacobian_qpp_plus_jacobian_derivative_qp =
        data_.cartesian_constraints_dot_products
            ->jacobian_qpp_plus_jacobian_derivative_qp;
  }
  std::vector<JointConstraintStep> joint_constraint_steps;
  std::vector<TorqueConstraintPathComponent> torque_constraint_path_components;
  const eigenmath::VectorNd zero_offset =
      eigenmath::VectorNd::Zero(num_dimensions);

  if (joint_constraint_type == JointConstraintType::kJointTorque) {
    // Compute components of the equations of motion required to formulate
    // torque limit constraints in terms of path variables.
    INTR_ASSIGN_OR_RETURN(
        torque_constraint_path_components,
        TorqueConstraintPathComponents(path_samples, dynamics_));

    for (size_t i = 0; i < num_points; i++) {
      // Minimum feasibility check of torque limited planning.
      if (((path_samples[i].joint_limits.max_torque -
            torque_constraint_path_components[i].gravity.cwiseAbs())
               .array() < 0.0)
              .any()) {
        return absl::InternalError(absl::StrCat(
            "The path is not time-parameterizable. Torque limits given by ",
            absl::string_view(eigenmath::ToFixedString(
                path_samples[i].joint_limits.max_torque)),
            " cannot be satisfied. The minimum torque due gravity is ",
            absl::string_view(eigenmath::ToFixedString(
                torque_constraint_path_components[i].gravity)),
            "."));
      }
    }

    INTR_ASSIGN_OR_RETURN(joint_constraint_steps,
                          CreateDynamicJointConstraintSteps(
                              path_samples, torque_constraint_path_components));
  } else if (joint_constraint_type == JointConstraintType::kJointAcceleration) {
    INTR_ASSIGN_OR_RETURN(
        joint_constraint_steps,
        CreateKinematicJointConstraintSteps(path_samples, zero_offset));
  }

  // Construct the structured 2-norm phase-space constraints mapping
  // translational and rotational targets.
  std::vector<PhaseSpaceCartesianConstraint> phase_space_cart_constraints;
  if (use_cartesian_acceleration_constraints) {
    INTR_ASSIGN_OR_RETURN(
        phase_space_cart_constraints,
        ComputePhaseSpaceCartesianConstraints(
            *data_.cartesian_constraints_dot_products, path_samples,
            /*use_inner_approximation=*/true));
  }

  INTR_ASSIGN_OR_RETURN(
      std::vector<double> x_bounds,
      JointVelocityLimitsToSquaredPathSpeedLimits(path_samples));
  if (use_cartesian_velocity_constraints) {
    INTR_ASSIGN_OR_RETURN(const std::vector<double> cart_vel_bnd,
                          CartesianVelocityLimitsToSquaredPathSpeedLimits(
                              path_samples, absl::MakeConstSpan(jacobian_qp)));
    INTR_RETURN_IF_ERROR(CwiseMinimumSquaredPathVelocity(
        /*b_joint_limits=*/absl::MakeSpan(x_bounds),
        /*b_cartesian_limits=*/cart_vel_bnd));
    INTR_RETURN_IF_ERROR(
        ApplyFrameCartesianVelocityLimitsToSquaredPathSpeedLimits(
            path_samples,
            data_.cartesian_constraints_dot_products->frame_jacobian_qp,
            absl::MakeSpan(x_bounds)));
  }

  INTR_ASSIGN_OR_RETURN(data_.squared_path_velocities,
                        PropagateSquaredPathVelocities(
                            path_increments, x_bounds, joint_constraint_steps,
                            phase_space_cart_constraints,
                            /*start_squared_path_velocity=*/0.0,
                            /*skip_data_validation=*/false));

  return true;
}

absl::StatusOr<ToppTrajectoryResult> ToppRA::Solve(
    absl::Span<const PathSample> path_samples) {
  // Validates that there is at least 3 path samples and they are consistent.
  INTR_RETURN_IF_ERROR(
      ValidatePathSamples(path_samples, /*min_num_samples=*/3));

  // Validate that options have been properly set.
  if (!options_.has_cartesian_constraints()) {
    return absl::InvalidArgumentError(
        "Cartesian constraints have not been properly set within the "
        "optimization options.");
  }
  if (!options_.has_kinematic_constraints() &&
      !options_.has_dynamic_constraints()) {
    return absl::InvalidArgumentError(
        "Neither kinematic nor dynamic constraints have been properly set "
        "within the optimization options.");
  }

  // Validate kinematics chain and/or rigid body interface according to the
  // options configured for the solution trajectory.
  if (options_.cartesian_constraints()
          .activate_cartesian_velocity_constraints() ||
      options_.cartesian_constraints()
          .activate_cartesian_acceleration_constraints()) {
    if (chain_ == nullptr) {
      return absl::InvalidArgumentError(
          "Kinematics chain is required when Cartesian constraints are "
          "active.");
    }
    INTR_RETURN_IF_ERROR(
        ValidateKinematicsChain(*chain_, path_samples.front()));
  }
  if (options_.dynamic_constraints().activate_joint_torque_constraints() ||
      options_.dynamic_constraints().activate_joint_torque_rate_constraints()) {
    INTR_RETURN_IF_ERROR(icon::ValidateRigidBodyInterface(
        dynamics_, path_samples.front().q, path_samples.front().qp));
  }

  // A torque-optimized trajectory can violate joint acceleration limits, so
  // we disable acceleration limit checking in ICON. This means we assume
  // responsibility for sending setpoints that won't harm the robot. Note that
  // enforcing torque constraints is heavily dependent on having a good model of
  // the robot dynamics, which is the requirement for this algorithm to produce
  // a meaningful solution for a given robot.
  DynamicLimitsCheckMode joint_dynamic_limits_check_mode =
      DynamicLimitsCheckMode::kCheckJointAcceleration;
  if (options_.dynamic_constraints().activate_joint_torque_constraints() ||
      options_.dynamic_constraints().activate_joint_torque_rate_constraints()) {
    joint_dynamic_limits_check_mode = DynamicLimitsCheckMode::kCheckNone;
  }

  INTR_ASSIGN_OR_RETURN(
      const std::vector<PathSample> path_samples_with_safety_margin,
      ScaleDynamicLimitsBySafetyMargin(
          /*margin=*/kJointAndCartesianLimitsSafetyMargin, path_samples));
  INTR_ASSIGN_OR_RETURN(const std::vector<double> path_variables,
                        GetPathVariables(path_samples_with_safety_margin));

  // It happens sometimes that ToppRA fails at producing a trajectory that
  // satisfies the limits, for instance due to inconsistency in the path samples
  // or numerical issues. In these cases, we attempt to resolve the problem by
  // appropriately adjusting the limits at the timesteps where a violation
  // happened. Refer to go/plots-iterative-toppra for plots comparing the single
  // vs. the multiple iteration ToppRA approach for trajectory generation.
  std::vector<PathSample> path_samples_with_current_limits =
      path_samples_with_safety_margin;

  data_.num_iterations = 0;
  data_.cartesian_constraints_dot_products = std::nullopt;
  JointTrajectoryPVA trajectory;
  while (data_.num_iterations++ < kMaxSolutionAttemptsToppRA) {
    INTR_ASSIGN_OR_RETURN(
        bool solved, TimeParametrizePath(path_samples_with_current_limits));
    if (!solved) {
      return absl::InternalError(absl::StrCat(
          "ToppRA has failed at producing a feasible solution after ",
          data_.num_iterations, " attempts."));
    }

    // Check that optimized trajectory satisfies the limits, return it if it
    // does, otherwise scale the limits where a violation occurred.
    INTR_ASSIGN_OR_RETURN(trajectory,
                          ComposeAccelerationLimitedJointTrajectoryPVA(
                              path_samples, data_.squared_path_velocities,
                              joint_dynamic_limits_check_mode));
    INTR_ASSIGN_OR_RETURN(bool within_limits,
                          IsTrajectoryWithinLimits(trajectory, path_samples,
                                                   options_, dynamics_));
    if (within_limits) {
      INTR_ASSIGN_OR_RETURN(
          std::unique_ptr<BSplineSquaredPathVelocity> squared_path_velocity,
          CreateDegreeOneBSplineSquaredPathVelocity(
              path_variables, data_.squared_path_velocities));
      return ToppTrajectoryResult{
          .trajectory = trajectory,
          .squared_path_velocity = std::move(squared_path_velocity)};
    }
    if (!ScaleLimits(trajectory,
                     absl::MakeConstSpan(path_samples_with_safety_margin),
                     absl::MakeSpan(path_samples_with_current_limits), options_,
                     dynamics_)
             .ok()) {
      break;
    }
  }

  // Returns the best-effort trajectory. This trajectory might slightly violate
  // limits, but it is enough as an initial guess.
  INTR_ASSIGN_OR_RETURN(
      std::unique_ptr<BSplineSquaredPathVelocity> squared_path_velocity,
      CreateDegreeOneBSplineSquaredPathVelocity(path_variables,
                                                data_.squared_path_velocities));
  return ToppTrajectoryResult{
      .trajectory = trajectory,
      .squared_path_velocity = std::move(squared_path_velocity)};
}

}  // namespace intrinsic::topp
