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

#include "intrinsic/icon/control/algorithms/nullspace_stiffness_control.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "absl/status/status_matchers.h"
#include "internal/testing.h"
#include "intrinsic/eigenmath/pseudo_inverse.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/testing/malloc_test.h"
#include "intrinsic/kinematics/types/joint_state.h"

namespace intrinsic {

namespace control {

namespace impedance_control {

namespace {

TEST(NullspaceStiffnessControl, ExpectNoMalloc) {
  static const int ndof = 7;
  eigenmath::VectorNd nullspace_stiffness(ndof);
  eigenmath::VectorNd nullspace_damping(ndof);
  eigenmath::VectorNd nullspace_joint_position_goal(ndof);
  JointStatePV sensed;
  eigenmath::MatrixNMd jacobian(6, ndof);
  nullspace_stiffness.setZero(ndof);
  nullspace_damping.setZero(ndof);
  nullspace_joint_position_goal.setZero(ndof);
  ASSERT_OK(sensed.SetSize(ndof));
  sensed.position.setZero(ndof);
  jacobian.setRandom(6, ndof);
  eigenmath::VectorNd stiffness_torque;
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  stiffness_torque = ComputeNullspaceStiffnessTorque<ndof>(
      nullspace_stiffness, nullspace_damping, nullspace_joint_position_goal,
      sensed, jacobian);
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
}

TEST(NullspaceStiffnessControl, ExpectZeroTorque) {
  static const int ndof = 7;
  eigenmath::VectorNd nullspace_stiffness(ndof);
  eigenmath::VectorNd nullspace_damping(ndof);
  eigenmath::VectorNd nullspace_joint_position_goal(ndof);
  JointStatePV sensed;
  eigenmath::MatrixNMd jacobian(6, ndof);
  nullspace_stiffness.setZero(ndof);
  nullspace_damping.setZero(ndof);
  nullspace_joint_position_goal.setZero(ndof);
  ASSERT_OK(sensed.SetSize(ndof));
  sensed.position.setZero(ndof);
  jacobian.setRandom(6, ndof);
  eigenmath::VectorNd stiffness_torque;
  stiffness_torque = ComputeNullspaceStiffnessTorque<ndof>(
      nullspace_stiffness, nullspace_damping, nullspace_joint_position_goal,
      sensed, jacobian);
  eigenmath::VectorNd test_vec(ndof);
  test_vec.setZero(ndof);
  EXPECT_EQ(test_vec, stiffness_torque);
}

TEST(NullspaceStiffnessControl, ExpectEqualTorque) {
  static const int ndof = 7;
  eigenmath::VectorNd nullspace_stiffness(ndof);
  eigenmath::VectorNd nullspace_damping(ndof);
  eigenmath::VectorNd nullspace_joint_position_goal(ndof);
  JointStatePV sensed;
  eigenmath::MatrixNMd jacobian = eigenmath::MatrixNMd::Identity(6, ndof);
  nullspace_stiffness.setOnes(ndof);
  nullspace_damping.setZero(ndof);
  ASSERT_OK(sensed.SetSize(ndof));
  for (int i = 0; i < sensed.position.size(); ++i) {
    sensed.position(i) = 2;
  }
  for (int i = 0; i < nullspace_joint_position_goal.size(); ++i) {
    nullspace_joint_position_goal(i) = 3;
  }
  eigenmath::VectorNd stiffness_torque;
  stiffness_torque = ComputeNullspaceStiffnessTorque<ndof>(
      nullspace_stiffness, nullspace_damping, nullspace_joint_position_goal,
      sensed, jacobian);
  eigenmath::VectorNd test_vec(ndof);
  test_vec.setZero(ndof);
  eigenmath::MatrixNMd nullspace_projector =
      eigenmath::MatrixNMd::Identity(ndof, ndof) -
      icon::ComputeSmoothRightPseudoInverse(jacobian, 1000).value() * jacobian;
  eigenmath::VectorNd nullspace_torque = -nullspace_stiffness.cwiseProduct(
      sensed.position - nullspace_joint_position_goal);
  test_vec = nullspace_projector * nullspace_torque;
  EXPECT_EQ(test_vec, stiffness_torque);
}

}  // namespace

}  // namespace impedance_control

}  // namespace control

}  // namespace intrinsic
