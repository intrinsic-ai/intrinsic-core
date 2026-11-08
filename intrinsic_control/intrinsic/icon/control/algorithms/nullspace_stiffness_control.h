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

#ifndef INTRINSIC_ICON_CONTROL_ALGORITHMS_NULLSPACE_STIFFNESS_CONTROL_H_
#define INTRINSIC_ICON_CONTROL_ALGORITHMS_NULLSPACE_STIFFNESS_CONTROL_H_

#include "absl/log/check.h"
#include "absl/strings/str_format.h"
#include "intrinsic/eigenmath/pseudo_inverse.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/kinematics/types/joint_state.h"

namespace intrinsic {
namespace control {
namespace impedance_control {

// Returns the nullspace stiffness control torque for a desired nullspace
// configuration given a measured joint configuration with joint space damping.
// Real time safe with a static N_DOF.
// Params
// nullspace_stiffness
//   the control stiffness for each joint
// nullspace_joint_position_goal
//   the desired nullsapce joint configuration
// sensed_joint_position
//   the current sensed position of the joints
// jacobian
//   the current jacobian of the robot
// svd_threshold
//   the threshold percentage over the maximum singular value used to cancel
//   small singular values. Passed as input to the pseudoinverse function.
template <int N_DOF>
eigenmath::VectorNd ComputeNullspaceStiffnessTorque(
    const eigenmath::VectorNd& nullspace_stiffness,
    const eigenmath::VectorNd& nullspace_damping,
    const eigenmath::VectorNd& nullspace_joint_position_goal,
    const JointStatePV& sensed_joint_state,
    const eigenmath::MatrixNMd& jacobian, double svd_threshold = 0.001);

template <int N_DOF>
eigenmath::VectorNd ComputeNullspaceStiffnessTorque(
    const eigenmath::VectorNd& nullspace_stiffness,
    const eigenmath::VectorNd& nullspace_damping,
    const eigenmath::VectorNd& nullspace_joint_position_goal,
    const JointStatePV& sensed_joint_state,
    const eigenmath::MatrixNMd& jacobian, double svd_threshold) {
  static_assert(N_DOF > 6, "nullspace DOF must be > 6");
  CHECK_EQ(N_DOF, nullspace_joint_position_goal.size()) << absl::StrFormat(
      "joint position goal/nullspace dof size mismatch. "
      "Expected: %d, Actual: %ld",
      N_DOF, nullspace_joint_position_goal.size());
  CHECK_EQ(sensed_joint_state.size(), N_DOF) << absl::StrFormat(
      "joint position/nullspace dof size mismatch. Expected: %d, Actual: %ld",
      N_DOF, sensed_joint_state.size());
  CHECK_EQ(jacobian.rows(), 6) << absl::StrFormat(
      "jacobian transpose/nullspace dof size mismatch. "
      "Expected row size: %d, Actual: %ld",
      6, jacobian.rows());

  CHECK_EQ(jacobian.cols(), N_DOF) << absl::StrFormat(
      "jacobian transpose/nullspace dof size mismatch. "
      "Expected col size: %d, Actual: %ld",
      N_DOF, jacobian.cols());
  eigenmath::VectorNd nullspace_stiffness_torque;
  // Compute the control torque resulting from the nullspace stiffness.
  eigenmath::Vectord<N_DOF> nullspace_torque_ =
      -nullspace_stiffness.cwiseProduct(sensed_joint_state.position -
                                        nullspace_joint_position_goal);
  // Compute the nullspace damping.
  nullspace_torque_ -=
      nullspace_damping.cwiseProduct(sensed_joint_state.velocity);

  double condition_number_threshold = 1500;
  if (svd_threshold > 0) {
    condition_number_threshold = 1.0 / svd_threshold;
  }
  auto pseudoinverse_ = icon::ComputeSmoothRightPseudoInverse(
                            jacobian, condition_number_threshold)
                            .value();
  eigenmath::Matrixd<N_DOF> identity_ =
      eigenmath::Matrixd<N_DOF>::Identity(N_DOF, N_DOF);
  eigenmath::Matrixd<N_DOF> nullspace_projector_ =
      identity_ - (pseudoinverse_ * jacobian);

  // Sanity check whether the underlying model is correct. All nullspace
  // projector matrix rows/columns cannot exceed unit length. The nullspace
  // projector matrix is symmetric.
  auto do_rows_exceed_unit_length =
      nullspace_projector_.rowwise().norm().array() > 1.0;
  CHECK(!do_rows_exceed_unit_length.any())
      << "nullspace projector rows exceed unit length";

  nullspace_stiffness_torque = nullspace_projector_ * nullspace_torque_;

  return nullspace_stiffness_torque;
}

}  // namespace impedance_control
}  // namespace control
}  // namespace intrinsic

#endif  // INTRINSIC_ICON_CONTROL_ALGORITHMS_NULLSPACE_STIFFNESS_CONTROL_H_
