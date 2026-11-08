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

#include "intrinsic/icon/control/algorithms/cartesian_impedance_trajectory.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "internal/testing.h"
#include "intrinsic/eigenmath/matchers.h"
#include "intrinsic/eigenmath/rotation_utils.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/actions/cartesian_impedance_parameters.h"
#include "intrinsic/icon/control/algorithms/testing/cartesian_impedance_controller_test_utils.h"
#include "intrinsic/icon/testing/malloc_test.h"
#include "intrinsic/icon/utils/realtime_status_matchers.h"
#include "intrinsic/kinematics/types/cart_state.h"
#include "intrinsic/kinematics/types/joint_state.h"
#include "intrinsic/math/pose3.h"

namespace intrinsic::icon {
namespace {

using eigenmath::testing::IsApprox;
using icon::RealtimeStatusIs;
using ::testing::HasSubstr;

constexpr int kNDof = 7;

TEST(ConstantCartesianImpedanceTest, FactoryWorksForMinimalParams) {
  auto impedance_proto_params = ToProto(CartesianImpedanceMinimalTestParams());
  EXPECT_OK(ConstantCartesianImpedance::Create(
      impedance_proto_params.cartesian_target()));
}

TEST(ConstantCartesianImpedanceTest, FactoryWorksForCompleteParams) {
  auto impedance_proto_params =
      ToProto(CartesianImpedanceCompleteTestParams(kNDof));
  EXPECT_OK(ConstantCartesianImpedance::Create(
      impedance_proto_params.cartesian_target()));
}

TEST(ConstantCartesianImpedanceTest, InitFailsForTooLargePositionOffset) {
  auto original_params = CartesianImpedanceCompleteTestParams(kNDof);
  original_params.cartesian_target.robot_tip_t_robot_tool = Pose3d::Identity();
  auto impedance_proto_params = ToProto(original_params);

  ASSERT_OK_AND_ASSIGN(auto generator,
                       ConstantCartesianImpedance::Create(
                           impedance_proto_params.cartesian_target()));

  CartStatePVA start_state_pva;
  start_state_pva.pose =
      original_params.cartesian_target.task_t_tool_reference_pose.value() *
      Pose3d(eigenmath::Vector3d(0, 0, .11));
  EXPECT_THAT(generator->Initialize(start_state_pva),
              RealtimeStatusIs(absl::StatusCode::kFailedPrecondition,
                               HasSubstr("position exceeds")));
}

TEST(ConstantCartesianImpedanceTest, InitFailsForTooLargeOrientationOffset) {
  auto original_params = CartesianImpedanceCompleteTestParams(kNDof);
  original_params.cartesian_target.robot_tip_t_robot_tool = Pose3d::Identity();
  auto impedance_proto_params = ToProto(original_params);

  ASSERT_OK_AND_ASSIGN(auto generator,
                       ConstantCartesianImpedance::Create(
                           impedance_proto_params.cartesian_target()));

  CartStatePVA start_state_pva;
  start_state_pva.pose =
      original_params.cartesian_target.task_t_tool_reference_pose.value();
  start_state_pva.pose.setQuaternion(start_state_pva.pose.quaternion() *
                                     eigenmath::QuaternionFromRPY(1.3, 0., 0.));

  EXPECT_THAT(generator->Initialize(start_state_pva),
              RealtimeStatusIs(absl::StatusCode::kFailedPrecondition,
                               HasSubstr("orientation exceeds")));
}

TEST(ConstantCartesianImpedanceTest, StepReturnsCorrectResult) {
  const auto original_params = CartesianImpedanceCompleteTestParams(kNDof);
  auto impedance_proto_params = ToProto(original_params);

  ASSERT_OK_AND_ASSIGN(auto generator,
                       ConstantCartesianImpedance::Create(
                           impedance_proto_params.cartesian_target()));
  EXPECT_THAT(
      generator->GetInitialRobotTipTRobotTool(),
      IsApprox(original_params.cartesian_target.robot_tip_t_robot_tool));

  // Make sure the start state accounts for tip_t_target offset.
  CartStatePVA start_state_pva;
  start_state_pva.pose =
      original_params.cartesian_target.task_t_tool_reference_pose.value();

  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  INTRINSIC_RT_ASSERT_OK(generator->Initialize(start_state_pva));
  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(auto rt_setpoint,
                                    generator->Step(/*speed_override=*/1.0));
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();

  // Test expectation on some setpoint values. These parameters do not need to
  // be tested exhaustively since the "cartesian_impedance_commons_test.cc"
  // already covers the conversions.
  EXPECT_THAT(rt_setpoint.tool_reference_twist,
              IsApprox(original_params.cartesian_target.tool_reference_twist));
  EXPECT_THAT(rt_setpoint.tool_reference_wrench,
              IsApprox(original_params.cartesian_target.tool_reference_wrench));
  EXPECT_THAT(rt_setpoint.cartesian_stiffness,
              IsApprox(original_params.cartesian_target.cartesian_stiffness));
}

TEST(ConstantCartesianImpedanceTest, ReturnsPhase) {
  auto impedance_proto_params =
      ToProto(CartesianImpedanceCompleteTestParams(kNDof));
  ASSERT_OK_AND_ASSIGN(auto generator,
                       ConstantCartesianImpedance::Create(
                           impedance_proto_params.cartesian_target()));

  EXPECT_DOUBLE_EQ(generator->GetPhase(), 1.0);
}

TEST(ConstantNullspaceTargetTest, FactoryWorksForMinimalParams) {
  auto impedance_proto_params =
      ToProto(CartesianImpedanceCompleteTestParams(kNDof));
  EXPECT_OK(ConstantNullspaceTarget::Create(
      impedance_proto_params.nullspace_target()));
}

TEST(ConstantNullspaceTargetTest, FactoryWorksForCompleteParams) {
  auto impedance_proto_params =
      ToProto(CartesianImpedanceCompleteTestParams(kNDof));
  EXPECT_OK(ConstantNullspaceTarget::Create(
      impedance_proto_params.nullspace_target()));
}

TEST(ConstantNullspaceTargetTest, ReturnsPhase) {
  auto impedance_proto_params =
      ToProto(CartesianImpedanceCompleteTestParams(kNDof));
  ASSERT_OK_AND_ASSIGN(auto generator,
                       ConstantNullspaceTarget::Create(
                           impedance_proto_params.nullspace_target()));

  EXPECT_DOUBLE_EQ(generator->GetPhase(), 1.0);
}

TEST(ConstantNullspaceTargetTest, InitializeReturnsOkStatus) {
  auto impedance_proto_params =
      ToProto(CartesianImpedanceCompleteTestParams(kNDof));
  ASSERT_OK_AND_ASSIGN(auto generator,
                       ConstantNullspaceTarget::Create(
                           impedance_proto_params.nullspace_target()));

  JointStatePVA start_state_pva;
  INTRINSIC_RT_ASSERT_OK(start_state_pva.SetSize(kNDof));
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  INTRINSIC_RT_ASSERT_OK(generator->Initialize(start_state_pva));
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
}

TEST(ConstantNullspaceTargetTest, StepReturnsCorrectResult) {
  const auto original_params = CartesianImpedanceCompleteTestParams(kNDof);
  auto impedance_proto_params = ToProto(original_params);

  ASSERT_OK_AND_ASSIGN(auto generator,
                       ConstantNullspaceTarget::Create(
                           impedance_proto_params.nullspace_target()));

  JointStatePVA start_state_pva;
  INTRINSIC_RT_ASSERT_OK(start_state_pva.SetSize(kNDof));
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  INTRINSIC_RT_ASSERT_OK(generator->Initialize(start_state_pva));
  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(auto rt_setpoint,
                                    generator->Step(/*speed_override=*/1.0));
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();

  // Test expectation on some setpoint values. These parameters do not need to
  // be tested exhaustively since the "cartesian_impedance_commons_test.cc"
  // already covers the conversions.
  EXPECT_THAT(
      rt_setpoint.joint_state.position,
      IsApprox(
          original_params.nullspace_target.joint_position_nullspace.value()));
}

}  // namespace
}  // namespace intrinsic::icon
