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

#include "intrinsic/icon/control/algorithms/cartesian_rangefinder_distance_controller.h"

#include <cstddef>
#include <memory>

#include "Eigen/Eigenvalues"
#include "absl/log/check.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "intrinsic/eigenmath/clamp.h"
#include "intrinsic/eigenmath/manifolds.h"
#include "intrinsic/eigenmath/pseudo_inverse.h"
#include "intrinsic/eigenmath/so3.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/control/algorithms/cartesian_rangefinder_distance_setpoint_generator.h"
#include "intrinsic/icon/utils/realtime_guard.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/icon/utils/realtime_status_macro.h"
#include "intrinsic/icon/utils/realtime_status_or.h"
#include "intrinsic/kinematics/chain.h"
#include "intrinsic/kinematics/elements.h"
#include "intrinsic/kinematics/state.h"
#include "intrinsic/kinematics/types/cart_state.h"
#include "intrinsic/kinematics/types/joint_state.h"
#include "intrinsic/math/pluecker_transform.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/twist.h"

namespace intrinsic::icon {

CartesianRangefinderDistanceController::CartesianRangefinderDistanceController(
    size_t njoints, const Pose3d& flange_t_rangefinder,
    const Pose3d& flange_t_tcp, double frequency,
    SetpointGenerator& setpoint_generator)
    : njoints_(njoints),
      flange_t_rangefinder_(flange_t_rangefinder),
      flange_t_tcp_(flange_t_tcp),
      reference_(State::Zero(njoints)),
      filter_state_(State::Zero(njoints)),
      frequency_(frequency),
      setpoint_generator_(setpoint_generator),
      settling_time_counter_(frequency) {
  CHECK_OK(joint_state_command_.SetSize(njoints_));
}

absl::StatusOr<std::unique_ptr<CartesianRangefinderDistanceController>>
CartesianRangefinderDistanceController::Create(
    size_t njoints, const Pose3d& flange_t_rangefinder,
    const Pose3d& flange_t_tcp, double frequency,
    SetpointGenerator& setpoint_generator) {
  INTRINSIC_ASSERT_NON_REALTIME();

  if (njoints == 0) {
    return absl::FailedPreconditionError("Number of joints must be > 0.");
  }
  if (frequency <= 0.0) {
    return absl::FailedPreconditionError("Controller frequency must be > 0.");
  }
  return absl::WrapUnique(new CartesianRangefinderDistanceController(
      njoints, flange_t_rangefinder, flange_t_tcp, frequency,
      setpoint_generator));
}

RealtimeStatus CartesianRangefinderDistanceController::SetReference(
    const InputParameters& input, const JointStatePVA& joint_state) {
  if (input.joint_limits.size() != njoints_) {
    return FailedPreconditionError("Joint limits have wrong number of joints.");
  }
  parameters_.joint_limits = input.joint_limits;

  // Assign user-defined reference TCP offset.
  if (input.z_reference_offset_tcp_frame < 0.0) {
    return FailedPreconditionError(
        "Reference TCP offset distance must be >= 0.");
  }
  reference_.z_reference_offset_tcp_frame = input.z_reference_offset_tcp_frame;

  parameters_.z_reference_offset_safety_threshold =
      input.z_reference_offset_safety_threshold.value_or(1.0);
  if (parameters_.z_reference_offset_safety_threshold < 0.0) {
    return FailedPreconditionError(
        "Reference distance safety threshold must be >= 0.");
  }

  // TODO(moritzknaust): Move the Eigen decompositions in the Action factory.
  //  Check and assign user-defined cartesian stiffness.
  if (Eigen::EigenSolver<eigenmath::Matrix6d>(
          input.cartesian_stiffness_tcp_frame)
          .eigenvalues()
          .real()
          .array()
          .minCoeff() < -kNumericPrecision) {
    return FailedPreconditionError(
        "The stiffness matrix must be positive semi-definite.");
  }
  if (!input.cartesian_stiffness_tcp_frame.isApprox(
          input.cartesian_stiffness_tcp_frame.transpose(), kNumericPrecision)) {
    return FailedPreconditionError("The stiffness matrix must be symmetric.");
  }
  reference_.cartesian_stiffness_tcp_frame =
      input.cartesian_stiffness_tcp_frame;

  // Check and assign user-defined cartesian damping or compute
  // automatically-derived damping matrix.
  if (input.cartesian_damping_tcp_frame.has_value()) {
    if (Eigen::EigenSolver<eigenmath::Matrix6d>(
            input.cartesian_damping_tcp_frame.value())
            .eigenvalues()
            .real()
            .array()
            .minCoeff() < -kNumericPrecision) {
      return FailedPreconditionError(
          "The damping matrix must be positive semi-definite.");
    }
    if (!input.cartesian_damping_tcp_frame->isApprox(
            input.cartesian_damping_tcp_frame->transpose(),
            kNumericPrecision)) {
      return FailedPreconditionError("The damping matrix must be symmetric.");
    }
    reference_.cartesian_damping_tcp_frame =
        input.cartesian_damping_tcp_frame.value();
  } else {
    // TODO(giftthaler): derive damping matrix automatically.
    return UnimplementedError(
        "Automatic damping matrix derivation not implemented yet.");
  }

  // Assign user-defined nullspace reference or use current configuration.
  // TODO(giftthaler): assign and use nullspace reference only if njoints > 6 or
  // stiffness matrix not positive definite (free DoF)?
  if (input.joint_position_nullspace.has_value()) {
    if (input.joint_position_nullspace.value().size() != njoints_) {
      return FailedPreconditionError(
          "Dimension of nullspace reference must be equal to the number of "
          "joints.");
    }
    reference_.joint_position_nullspace =
        input.joint_position_nullspace.value();
  } else {
    // Assign current joint position as nullspace reference.
    reference_.joint_position_nullspace = joint_state.position;
  }

  // Check and assign the user-provided nullspace stiffness.
  if (input.nullspace_stiffness.has_value()) {
    if (input.nullspace_stiffness.value() < 0.0) {
      return FailedPreconditionError(
          "The nullspace stiffness must greater or equal than zero.");
    }
    reference_.nullspace_stiffness = input.nullspace_stiffness.value();
  } else {
    reference_.nullspace_stiffness = 1.0;
  }

  // Check, assign or alternatively derive the nullspace damping.
  if (input.nullspace_damping.has_value()) {
    if (input.nullspace_damping.value() < 0.0) {
      return FailedPreconditionError(
          "The nullspace damping must be greater or equal than zero.");
    }
    reference_.nullspace_damping = input.nullspace_damping.value();
  } else {
    reference_.nullspace_damping = 0.1;
  }

  // Check and assign (optional) user-provided low-pass filter gain.
  if (input.low_pass_filter_gain.has_value()) {
    if (input.low_pass_filter_gain <= 0.0 || input.low_pass_filter_gain > 1.0) {
      return FailedPreconditionError(
          "IIR filter lowpass parameter must be chosen from the interval ]0, "
          "1].");
    }
    parameters_.low_pass_filter_gain = input.low_pass_filter_gain.value();
  } else {
    // Set a default value which works from experience.
    parameters_.low_pass_filter_gain = 5e-3;
  }

  // Check and assign (optional) user-provided pseudoinversion damping factor.
  if (input.jacobian_pinv_damping.has_value()) {
    if (input.jacobian_pinv_damping < 0.0) {
      return FailedPreconditionError(
          "The Jacobian pseudoinverse damping must not be smaller than zero.");
    }
    parameters_.jacobian_pinv_damping = input.jacobian_pinv_damping.value();
  } else {
    // Set a default value which works from experience.
    parameters_.jacobian_pinv_damping = 1e-3;
  }

  // Reset the error state.
  error_state_.position_error_in_tcp_frame.setConstant(0.0);
  error_state_.orientation_error_in_tcp_frame = eigenmath::SO3d(0.0, 0.0, 0.0);
  error_state_.pose_in_base_frame.setConstant(0.0);
  error_state_.twist_in_base_frame.setConstant(0.0);

  // Reset the internal state of the controller.
  joint_state_command_ = joint_state;

  return OkStatus();
}

void CartesianRangefinderDistanceController::ResetFilterToReference() {
  filter_state_ = reference_;
  settling_time_counter_.Reset();
}

void CartesianRangefinderDistanceController::StepLowpassFilter(
    const State& reference, const Parameters& params, State& filter_state) {
  // A first-order filter for reference smoothing in Euclidean space.
  auto filter = [&params](const auto& target, auto& current) {
    current = params.low_pass_filter_gain * target +
              (1.0 - params.low_pass_filter_gain) * current;
  };

  filter(reference.z_reference_offset_tcp_frame,
         filter_state.z_reference_offset_tcp_frame);
  filter(reference.cartesian_stiffness_tcp_frame,
         filter_state.cartesian_stiffness_tcp_frame);
  filter(reference.cartesian_damping_tcp_frame,
         filter_state.cartesian_damping_tcp_frame);
  filter(reference.joint_position_nullspace,
         filter_state.joint_position_nullspace);
  filter(reference.nullspace_stiffness, filter_state.nullspace_stiffness);
  filter(reference.nullspace_damping, filter_state.nullspace_damping);
}

RealtimeStatusOr<JointStatePVA>
CartesianRangefinderDistanceController::ComputeControl(
    const double rangefinder_sensed_distance, const kinematics::Chain& chain) {
  kinematics::ElementId tip_id = chain.GetTipId();

  kinematics::State state(&chain);

  // Evaluate the endeffector forward kinematics for the reference motion. All
  // resulting quantities are expressed in the robot's base frame.
  INTRINSIC_RT_RETURN_IF_ERROR(state.SetStatePVA(joint_state_command_));

  // Express the rangefinder measurement as pose in the sensor frame (as z-axis
  // offset).
  Pose3d rangefinder_t_measurement = Pose3d::Identity();
  rangefinder_t_measurement.translation().z() = rangefinder_sensed_distance;

  // Express the rangefinder measurement in the TCP frame by homogeneous
  // transformation.
  Pose3d flange_t_measurement =
      flange_t_rangefinder_ * rangefinder_t_measurement;

  Pose3d tcp_t_measurement = flange_t_tcp_.inverse() * flange_t_measurement;

  // Abort in case of out-of-range sensor readings.
  if (tcp_t_measurement.translation().z() >
      parameters_.z_reference_offset_safety_threshold) {
    return AbortedError(RealtimeStatus::StrCat(
        "The range finder reading is out of the admissible range. Current "
        "measurement: ",
        tcp_t_measurement.translation().z(),
        " Max: ", parameters_.z_reference_offset_safety_threshold, "."));
  }

  // Evaluate the endeffector forward kinematics. All resulting quantities are
  // expressed in the robot's base frame.
  INTRINSIC_RT_ASSIGN_OR_RETURN(const Pose3d base_t_flange,
                                state.GetTransform(tip_id));
  Pose3d base_t_tcp = base_t_flange * flange_t_tcp_;

  INTRINSIC_RT_ASSIGN_OR_RETURN(
      const eigenmath::Matrix6Nd jacobian_in_base_frame,
      state.ComputeJacobian());
  const eigenmath::Matrix3d& base_R_tcp = base_t_tcp.rotationMatrix();
  // Compute the damped Jacobian pseudo-inverse.
  INTRINSIC_RT_ASSIGN_OR_RETURN(
      eigenmath::MatrixNd jacobian_pinv_in_base_frame,
      ComputePseudoInverse(jacobian_in_base_frame,
                           parameters_.jacobian_pinv_damping,
                           /*fail_if_badly_conditioned=*/true));

  INTRINSIC_RT_RETURN_IF_ERROR(
      setpoint_generator_.ComputeSetpoint(base_t_last_commanded_));

  // Compute error w.r.t. desired endeffector position,
  // expressed in base frame.
  eigenmath::Vector3d position_error_in_base_frame =
      base_t_tcp.translation() - base_t_last_commanded_.pose.translation();

  // Rotate the position error into the TCP frame.
  error_state_.position_error_in_tcp_frame =
      base_R_tcp.transpose() * position_error_in_base_frame;

  // Replace the position-error in TCP z-direction with the LRF-sensor error.
  error_state_.position_error_in_tcp_frame.z() =
      filter_state_.z_reference_offset_tcp_frame -
      tcp_t_measurement.translation().z();

  // Rotate position error back to the base frame and assign to error state.
  error_state_.pose_in_base_frame.head<3>() =
      base_R_tcp * error_state_.position_error_in_tcp_frame;

  // Compute error w.r.t. desired endeffector orientation.
  error_state_.orientation_error_in_tcp_frame =
      eigenmath::SO3d(base_t_last_commanded_.pose.quaternion().inverse() *
                      base_t_tcp.quaternion());
  error_state_.pose_in_base_frame.tail<3>() =
      base_t_tcp.rotationMatrix() *
      eigenmath::logSO3(error_state_.orientation_error_in_tcp_frame);

  // Compute twist error.
  error_state_.twist_in_base_frame =
      jacobian_in_base_frame * joint_state_command_.velocity -
      base_t_last_commanded_.velocity;

  // Retrieve spatial Pluecker motion transformations used for the feedback gain
  // coordinate transform.
  const eigenmath::Matrix6d pluecker_transform =
      PlueckerMotionTransform(base_t_tcp);
  const eigenmath::Matrix6d inv_pluecker_transform =
      PlueckerMotionTransformInverse(base_t_tcp);

  // Transform gain matrices to robot base frame.
  const eigenmath::Matrix6d cartesian_stiffness_in_base_frame =
      pluecker_transform * filter_state_.cartesian_stiffness_tcp_frame *
      inv_pluecker_transform;
  const eigenmath::Matrix6d cartesian_damping_in_base_frame =
      pluecker_transform * filter_state_.cartesian_damping_tcp_frame *
      inv_pluecker_transform;

  // Evaluate cartesian feedback control law in robot base frame.
  joint_state_command_.acceleration =
      -jacobian_pinv_in_base_frame *
      (cartesian_stiffness_in_base_frame * error_state_.pose_in_base_frame +
       cartesian_damping_in_base_frame * error_state_.twist_in_base_frame -
       base_t_last_commanded_.acceleration /* + dJdt * dq*/);

  if (njoints_ > 6) {
    // Nullspace PD control law.
    eigenmath::MatrixNMd nullspace_projector(njoints_, njoints_);
    nullspace_projector.setIdentity();
    nullspace_projector -= jacobian_pinv_in_base_frame * jacobian_in_base_frame;
    // Compute nullspace-invariant feedback.
    joint_state_command_.acceleration -=
        nullspace_projector *
        (filter_state_.nullspace_stiffness *
             (joint_state_command_.position -
              filter_state_.joint_position_nullspace) +
         filter_state_.nullspace_damping * joint_state_command_.velocity);
  }

  // Perform reference smoothing step.
  StepLowpassFilter(reference_, parameters_, filter_state_);

  // Saturate the joint acceleration command.
  // TODO(moritzknaust): Use dynamic robot model to compute admissible
  // acceleration online from torque limits.

  // TODO(moritzknaust): The clamping may actually result in a differentially
  // inconsistent command if acceleration and velocity do not match anymore.

  // Saturate the joint acceleration command in a way that preserves the
  // Cartesian motion direction: avoid naive clamping, scale instead.
  const double acc_overshoot_factor =
      (joint_state_command_.acceleration.cwiseAbs().cwiseQuotient(
           parameters_.joint_limits.max_acceleration.cwiseAbs()))
          .maxCoeff();
  if (acc_overshoot_factor > 1.0) {
    joint_state_command_.acceleration /= acc_overshoot_factor;
  }

  // Forward-integrate the motion reference using symplectic integration.
  joint_state_command_.velocity +=
      joint_state_command_.acceleration / frequency_;

  // Saturate the joint velocity command in a way that preserves the
  // Cartesian motion direction: avoid naive clamping, scale instead.
  const double vel_overshoot_factor =
      (joint_state_command_.velocity.cwiseAbs().cwiseQuotient(
           parameters_.joint_limits.max_velocity.cwiseAbs()))
          .maxCoeff();
  if (vel_overshoot_factor > 1.0) {
    joint_state_command_.velocity /= vel_overshoot_factor;
  }

  joint_state_command_.position += joint_state_command_.velocity / frequency_;

  // Saturate the joint position command.
  if (!eigenmath::ClampVector(parameters_.joint_limits.min_position,
                              parameters_.joint_limits.max_position,
                              joint_state_command_.position)) {
    return InternalError("Clamping joint positions failed.");
  }

  return JointStatePVA(joint_state_command_);
}

bool CartesianRangefinderDistanceController::ReferenceReached(
    const JointStatePVA& joint_state, double position_error_threshold,
    double orientation_error_threshold, double translational_velocity_threshold,
    double rotational_velocity_threshold,
    double joint_velocity_threshold) const {
  return (error_state_.position_error_in_tcp_frame.cwiseAbs().maxCoeff() <
          position_error_threshold) &&
         (eigenmath::logSO3(error_state_.orientation_error_in_tcp_frame)
              .array()
              .abs()
              .maxCoeff() < orientation_error_threshold) &&
         (error_state_.twist_in_base_frame.head<3>().cwiseAbs().maxCoeff() <
          translational_velocity_threshold) &&
         (error_state_.twist_in_base_frame.tail<3>().cwiseAbs().maxCoeff() <
          rotational_velocity_threshold) &&
         (joint_state.velocity.cwiseAbs().maxCoeff() <
          joint_velocity_threshold) &&
         setpoint_generator_.LastPointReached();
}

double CartesianRangefinderDistanceController::UpdateSettlingTime(
    const JointStateV& joint_velocity, double translational_velocity_threshold,
    double rotational_velocity_threshold, double joint_velocity_threshold) {
  const double not_settled_value = -1.0 / frequency_;
  if (!setpoint_generator_.LastPointReached()) return not_settled_value;
  return settling_time_counter_.ComputeSettlingTime(
      translational_velocity_threshold, rotational_velocity_threshold,
      joint_velocity_threshold, joint_velocity,
      Twist(error_state_.twist_in_base_frame));
}

}  // namespace intrinsic::icon
