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

#include "intrinsic/icon/control/algorithms/testing/cartesian_impedance_controller_test_utils.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "Eigen/Eigenvalues"
#include "intrinsic/eigenmath/matchers.h"
#include "intrinsic/eigenmath/rotation_utils.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/actions/cartesian_impedance_parameters.h"
#include "intrinsic/kinematics/types/cartesian_limits.h"
#include "intrinsic/kinematics/types/joint_limits.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/twist.h"

namespace intrinsic::icon {

CartesianImpedanceParameters CartesianImpedanceMinimalTestParams() {
  CartesianImpedanceParameters params;
  params.cartesian_target.cartesian_stiffness = eigenmath::Matrix6d::Identity();
  params.cartesian_target.virtual_cartesian_inertia =
      eigenmath::Matrix6d::Identity();
  params.nullspace_target.nullspace_stiffness = 5.0;

  return params;
}

CartesianImpedanceParameters CartesianImpedanceCompleteTestParams(int ndof) {
  CartesianImpedanceParameters params = CartesianImpedanceMinimalTestParams();
  params.cartesian_target.robot_tip_t_robot_tool =
      Pose3d(eigenmath::QuaternionFromRPY(0.2, 0.3, 0.4),
             eigenmath::Vector3d{0.1, 0.2, 0.3});
  params.cartesian_target.task_t_tool_reference_pose = Pose3d::Identity();
  params.cartesian_target.tool_reference_wrench = -Wrench::Ones();
  params.cartesian_target.cartesian_damping = eigenmath::Matrix6d::Identity();
  params.cartesian_target.wrench_selection_matrix =
      eigenmath::Matrix6d::Identity();
  params.cartesian_target.tool_reference_twist = Twist::ZERO;
  params.cartesian_target.tool_reference_acceleration = Acceleration::ZERO;
  params.nullspace_target.joint_position_nullspace =
      eigenmath::VectorNd::Zero(ndof);
  params.nullspace_target.nullspace_damping = 4.0;
  params.algorithm_configuration.jacobian_pinv_damping = 0.001;
  params.algorithm_configuration.sensed_wrench_deadband = 0.1 * Wrench::Ones();
  params.algorithm_configuration.pose_error_integrator_gain.setConstant(0.01);
  params.algorithm_configuration.pose_error_integrator_bound.setConstant(2.0);
  params.state_variable_configuration.position_error_threshold = 0.1;
  params.state_variable_configuration.orientation_error_threshold = 1e-2;
  params.state_variable_configuration.translational_velocity_error_threshold =
      1e-2;
  params.state_variable_configuration.angular_velocity_error_threshold = 1e-2;
  params.state_variable_configuration.joint_velocity_threshold = 0.5;
  params.state_variable_configuration.translational_distance_along =
      eigenmath::Vector3d(1., 0., 0.);
  params.state_variable_configuration.force_direction_1_in_task =
      eigenmath::Vector3d(1.0, 1.0, 0.0);
  params.state_variable_configuration.force_direction_2_in_task =
      eigenmath::Vector3d(0., 1., 0.);
  params.state_variable_configuration.force_direction_3_in_task =
      eigenmath::Vector3d(-1.0, 0.0, 1.0);
  params.constraints.cartesian_limits = CartesianLimits();

  params.constraints.cartesian_limits->max_translational_position.setConstant(
      10);
  params.constraints.cartesian_limits->min_translational_position.setConstant(
      -10);
  params.constraints.cartesian_limits->max_translational_velocity.setConstant(
      10);
  params.constraints.cartesian_limits->min_translational_velocity.setConstant(
      -10);
  params.constraints.cartesian_limits->max_translational_acceleration
      .setConstant(10);
  params.constraints.cartesian_limits->min_translational_acceleration
      .setConstant(-10);
  params.constraints.cartesian_limits->max_translational_jerk.setConstant(10);
  params.constraints.cartesian_limits->min_translational_jerk.setConstant(-10);
  params.constraints.cartesian_limits->max_rotational_jerk = 10;
  params.constraints.cartesian_limits->max_rotational_acceleration = 10;
  params.constraints.cartesian_limits->max_rotational_velocity = 10;

  params.constraints.joint_limits = JointLimits::Unlimited(ndof).value();
  params.constraints.joint_limits->max_velocity.setConstant(10);
  params.constraints.joint_limits->max_acceleration.setConstant(10);

  params.reference_generators.cartesian_reference_generator =
      cartesian_impedance::
          CartesianPositionReflexxesImpedanceGeneratorParameters();
  params.reference_generators.nullspace_reference_generator =
      cartesian_impedance::SimpleNullspaceReferenceGeneratorParameters();

  return params;
}

eigenmath::MatrixNd GenerateSPDMatrix(int dim) {
  eigenmath::MatrixNd A = eigenmath::MatrixNd::Random(dim, dim);
  eigenmath::MatrixNd B = 0.5 * (A + A.transpose());
  // Normalize
  const double max_coeff = B.cwiseAbs().maxCoeff();
  if (max_coeff > 1.0) {
    B /= max_coeff;
  }

  // Since B(i,j) < 1 by construction and a symmetric diagonally dominant matrix
  //  is symmetric positive definite, which can be ensured by adding
  //  dim*Identity.
  eigenmath::MatrixNd result =
      B + static_cast<double>(dim) * eigenmath::MatrixNd::Identity(dim, dim);

  // Check for positive definiteness.
  Eigen::SelfAdjointEigenSolver<eigenmath::MatrixNd> eigensolver(result);
  const double kTinyTolerance = 1e-8;
  EXPECT_GT(eigensolver.eigenvalues().minCoeff(), kTinyTolerance);

  // Check for symmetry.
  EXPECT_THAT(result, ::intrinsic::eigenmath::testing::IsApprox(
                          result.transpose(), kTinyTolerance));

  return result;
}

}  // namespace intrinsic::icon
