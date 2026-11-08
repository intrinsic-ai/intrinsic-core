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

#include "intrinsic/icon/control/algorithms/simple_cartesian_impedance_reference_generator.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstddef>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/strings/str_cat.h"
#include "internal/testing.h"
#include "intrinsic/eigenmath/matchers.h"
#include "intrinsic/eigenmath/rotation_utils.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/actions/cartesian_impedance_parameters.h"
#include "intrinsic/icon/control/algorithms/cartesian_impedance_commons.h"
#include "intrinsic/icon/utils/realtime_status_macro.h"
#include "intrinsic/icon/utils/realtime_status_matchers.h"
#include "intrinsic/kinematics/types/cart_state.h"
#include "intrinsic/kinematics/types/cartesian_limits.h"
#include "intrinsic/kinematics/types/joint_limits.h"
#include "intrinsic/kinematics/types/joint_state.h"
#include "intrinsic/math/ipow.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/twist.h"

namespace intrinsic::icon {
namespace {

using ::absl_testing::StatusIs;
using eigenmath::testing::IsApprox;
using ::testing::DoubleNear;
using ::testing::HasSubstr;

constexpr double kMinPos = -3;
constexpr double kMaxPos = 3;
constexpr double kMinVel = -10;
constexpr double kMaxVel = 10;
constexpr double kMinAcc = -2;
constexpr double kMaxAcc = 2;
constexpr double kMinJer = -4;
constexpr double kMaxJer = 4;

constexpr int kNumJoints = 7;

constexpr double kSmallTolerance = 0.02;

constexpr double kDefaultSpeedOverride = 1.0;

CartesianLimits DefaultCartesianLimits() {
  auto limits = CartesianLimits();
  limits.SetUnlimited();
  limits.min_translational_position.setConstant(kMinPos);
  limits.max_translational_position.setConstant(kMaxPos);
  limits.min_translational_velocity.setConstant(kMinVel);
  limits.max_translational_velocity.setConstant(kMaxVel);
  limits.min_translational_acceleration.setConstant(kMinAcc);
  limits.max_translational_acceleration.setConstant(kMaxAcc);
  limits.min_translational_jerk.setConstant(kMinJer);
  limits.max_translational_jerk.setConstant(kMaxJer);
  limits.max_rotational_velocity = kMaxVel;
  limits.max_rotational_acceleration = kMaxAcc;
  limits.max_rotational_jerk = kMaxJer;

  return limits;
}

JointLimits DefaultJointLimits() {
  INTRINSIC_RT_ASSIGN_OR_DIE(auto limits, JointLimits::Unlimited(kNumJoints));
  limits.min_position.setConstant(kMinPos);
  limits.max_position.setConstant(kMaxPos);
  limits.max_velocity.setConstant(kMaxVel);
  limits.max_acceleration.setConstant(kMaxAcc);
  limits.max_jerk.setConstant(kMaxJer);

  return limits;
}

TEST(SimpleCartesianImpedanceReferenceGeneratorTest, ConfigureWorks) {
  SimpleCartesianImpedanceReferenceGenerator generator;

  cartesian_impedance::SimpleImpedanceGeneratorParameters params;
  params.lowpass_filter_constant = 0.2;
  EXPECT_OK(generator.Configure(params));
  EXPECT_EQ(generator.alpha(), params.lowpass_filter_constant);
}

TEST(SimpleCartesianImpedanceReferenceGeneratorTest,
     ConfigureErrorsForOutOfRangeParameters) {
  SimpleCartesianImpedanceReferenceGenerator generator;
  cartesian_impedance::SimpleImpedanceGeneratorParameters params;

  params.lowpass_filter_constant = -0.001;
  EXPECT_THAT(
      generator.Configure(params),
      StatusIs(absl::StatusCode::kFailedPrecondition,
               HasSubstr("Lowpass filter constant out of bound ]0 1]")));

  params.lowpass_filter_constant = 1.001;
  EXPECT_THAT(
      generator.Configure(params),
      StatusIs(absl::StatusCode::kFailedPrecondition,
               HasSubstr("Lowpass filter constant out of bound ]0 1]")));
}

TEST(SimpleCartesianImpedanceReferenceGeneratorTest,
     InitializationFailsIfReferenceViolatesLimits) {
  SimpleCartesianImpedanceReferenceGenerator generator;
  cartesian_impedance::RealTimeCartesianTarget target;
  target.task_t_tool_reference_position.setConstant(1000);
  target.task_t_tool_reference_orientation.setIdentity();
  EXPECT_THAT(generator.SetReference(target, DefaultCartesianLimits()),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       HasSubstr("is not within limits")));
}

TEST(SimpleCartesianImpedanceReferenceGeneratorTest,
     InitializationPassesForAdmissibleReference) {
  SimpleCartesianImpedanceReferenceGenerator generator;
  cartesian_impedance::RealTimeCartesianTarget target;
  target.task_t_tool_reference_position.setZero();
  target.task_t_tool_reference_orientation.setIdentity();
  EXPECT_OK(generator.SetReference(target, DefaultCartesianLimits()));
}

TEST(SimpleCartesianImpedanceReferenceGeneratorTest, InvalidSpeedOverride) {
  SimpleCartesianImpedanceReferenceGenerator generator;

  // Create an arbitrary initial state, which is not necessarily physically
  // meaningful.
  cartesian_impedance::RealTimeCartesianTarget start;
  CartStatePVA cart_state_sensed;

  EXPECT_THAT(
      generator.Evaluate(start, cart_state_sensed, -0.001),
      RealtimeStatusIs(absl::StatusCode::kInvalidArgument,
                       AllOf(HasSubstr("speed_override should be in range ["),
                             HasSubstr("], but instead has value -0.001."))));

  EXPECT_THAT(
      generator.Evaluate(start, cart_state_sensed, 1.01),
      RealtimeStatusIs(absl::StatusCode::kInvalidArgument,
                       AllOf(HasSubstr("speed_override should be in range ["),
                             HasSubstr("], but instead has value 1.01."))));
}

TEST(SimpleCartesianImpedanceReferenceGeneratorTest, ConvergesToReference) {
  SimpleCartesianImpedanceReferenceGenerator generator;
  cartesian_impedance::RealTimeCartesianTarget target;
  target.task_t_tool_reference_position.setConstant(1);
  target.task_t_tool_reference_orientation.setIdentity();
  target.cartesian_damping.setIdentity();
  target.cartesian_stiffness.setIdentity();
  target.wrench_selection_matrix.setIdentity();
  target.virtual_cartesian_inertia_inverse.setIdentity();
  target.tool_reference_twist.setOnes();
  target.tool_reference_wrench.setOnes();
  target.robot_tip_t_robot_tool.translation().setConstant(1.5);
  target.robot_tip_t_robot_tool.setQuaternion(
      eigenmath::Quaterniond::Identity());
  target.robot_tip_t_robot_tool.translation().setConstant(1.33);
  target.robot_tip_t_robot_tool.setQuaternion(
      eigenmath::QuaternionFromRPY(0.1, 0.2, 0.3));
  EXPECT_OK(generator.SetReference(target, DefaultCartesianLimits()));

  // Create an arbitrary initial state, which is not necessarily physically
  // meaningful.
  cartesian_impedance::RealTimeCartesianTarget start;
  start.task_t_tool_reference_position.setConstant(0);
  start.task_t_tool_reference_orientation.coeffs() << 0, 1, 0, 0;
  start.cartesian_damping.setZero();
  start.cartesian_stiffness.setZero();
  start.wrench_selection_matrix.setZero();
  start.virtual_cartesian_inertia_inverse.setZero();
  start.tool_reference_twist.setZero();
  start.tool_reference_wrench.setZero();
  start.robot_tip_t_robot_tool.translation().setZero();
  start.robot_tip_t_robot_tool.setQuaternion(
      eigenmath::Quaterniond{0.0, 1.0, 0.0, 0.0});
  start.robot_base_t_task.translation().setZero();
  start.robot_base_t_task.setQuaternion(
      eigenmath::Quaterniond{0.0, 1.0, 0.0, 0.0});
  CartStatePVA cart_state_sensed;
  cart_state_sensed.pose = Pose3d(start.task_t_tool_reference_orientation,
                                  start.task_t_tool_reference_position);
  cart_state_sensed.velocity = start.tool_reference_twist;
  cart_state_sensed.acceleration = start.tool_reference_acceleration;

  // We expect to be close to the start after one call to the reference
  // generator.
  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
      cartesian_impedance::RealTimeCartesianTarget current,
      generator.Evaluate(start, cart_state_sensed, kDefaultSpeedOverride));
  EXPECT_THAT(current.task_t_tool_reference_position,
              IsApprox(start.task_t_tool_reference_position, kSmallTolerance));
  EXPECT_THAT(
      current.task_t_tool_reference_orientation,
      IsApprox(start.task_t_tool_reference_orientation, kSmallTolerance));
  EXPECT_THAT(current.cartesian_damping,
              IsApprox(start.cartesian_damping, kSmallTolerance));
  EXPECT_THAT(current.cartesian_stiffness,
              IsApprox(start.cartesian_stiffness, kSmallTolerance));
  EXPECT_THAT(current.wrench_selection_matrix,
              IsApprox(start.wrench_selection_matrix, kSmallTolerance));
  EXPECT_THAT(
      current.virtual_cartesian_inertia_inverse,
      IsApprox(start.virtual_cartesian_inertia_inverse, kSmallTolerance));
  EXPECT_THAT(current.tool_reference_twist,
              IsApprox(start.tool_reference_twist, kSmallTolerance));
  EXPECT_THAT(current.tool_reference_wrench,
              IsApprox(start.tool_reference_wrench, kSmallTolerance));
  EXPECT_THAT(current.robot_tip_t_robot_tool,
              IsApprox(start.robot_tip_t_robot_tool, kSmallTolerance));
  EXPECT_THAT(current.robot_base_t_task,
              IsApprox(start.robot_base_t_task, kSmallTolerance));

  // Iterate until we expect convergence.
  for (size_t i = 0; i < 2000; i++) {
    INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
        current,
        generator.Evaluate(current, cart_state_sensed, kDefaultSpeedOverride));

    cart_state_sensed.pose = Pose3d(current.task_t_tool_reference_orientation,
                                    current.task_t_tool_reference_position);
    cart_state_sensed.velocity = current.tool_reference_twist;
    cart_state_sensed.acceleration = current.tool_reference_acceleration;
  }

  // We expect to be close to the target after iterating.
  EXPECT_THAT(current.task_t_tool_reference_position,
              IsApprox(target.task_t_tool_reference_position, kSmallTolerance));
  EXPECT_THAT(
      current.task_t_tool_reference_orientation,
      IsApprox(target.task_t_tool_reference_orientation, kSmallTolerance));
  EXPECT_THAT(current.cartesian_damping,
              IsApprox(target.cartesian_damping, kSmallTolerance));
  EXPECT_THAT(current.cartesian_stiffness,
              IsApprox(target.cartesian_stiffness, kSmallTolerance));
  EXPECT_THAT(current.wrench_selection_matrix,
              IsApprox(target.wrench_selection_matrix, kSmallTolerance));
  EXPECT_THAT(
      current.virtual_cartesian_inertia_inverse,
      IsApprox(target.virtual_cartesian_inertia_inverse, kSmallTolerance));
  EXPECT_THAT(current.tool_reference_twist,
              IsApprox(target.tool_reference_twist, kSmallTolerance));
  EXPECT_THAT(current.tool_reference_wrench,
              IsApprox(target.tool_reference_wrench, kSmallTolerance));
  EXPECT_THAT(current.robot_tip_t_robot_tool,
              IsApprox(target.robot_tip_t_robot_tool, kSmallTolerance));
  EXPECT_THAT(current.robot_base_t_task,
              IsApprox(target.robot_base_t_task, kSmallTolerance));
}

TEST(SimpleCartesianImpedanceReferenceGeneratorTest,
     ComparableValuesWithSpeedOverride) {
  const double kTestSpeedOverride = 0.2;
  const int kNumComparisonTests = 1000;

  SimpleCartesianImpedanceReferenceGenerator
      generator_with_default_speed_override,
      generator_with_test_speed_override;
  cartesian_impedance::RealTimeCartesianTarget target;
  target.task_t_tool_reference_position.setConstant(1);
  target.task_t_tool_reference_orientation.setIdentity();
  target.tool_reference_twist.setOnes();
  target.tool_reference_acceleration.setOnes();
  target.cartesian_damping.setIdentity();
  target.cartesian_stiffness.setIdentity();
  target.wrench_selection_matrix.setIdentity();
  target.virtual_cartesian_inertia_inverse.setIdentity();
  target.tool_reference_wrench.setOnes();
  target.robot_tip_t_robot_tool.translation().setConstant(1.5);
  target.robot_tip_t_robot_tool.setQuaternion(
      eigenmath::Quaterniond::Identity());
  target.robot_base_t_task.translation().setConstant(1.33);
  target.robot_base_t_task.setQuaternion(
      eigenmath::QuaternionFromRPY(0.1, 0.2, 0.3));
  EXPECT_OK(generator_with_default_speed_override.SetReference(
      target, DefaultCartesianLimits()));
  EXPECT_OK(generator_with_test_speed_override.SetReference(
      target, DefaultCartesianLimits()));

  // Create an arbitrary initial state, which is not necessarily physically
  // meaningful.
  cartesian_impedance::RealTimeCartesianTarget start;
  start.task_t_tool_reference_position.setConstant(0);
  start.task_t_tool_reference_orientation.coeffs() << 0, 1, 0, 0;
  start.tool_reference_twist.setZero();
  start.tool_reference_acceleration.setZero();
  start.cartesian_damping.setZero();
  start.cartesian_stiffness.setZero();
  start.wrench_selection_matrix.setZero();
  start.virtual_cartesian_inertia_inverse.setZero();
  start.tool_reference_wrench.setZero();
  start.robot_tip_t_robot_tool.translation().setZero();
  start.robot_tip_t_robot_tool.setQuaternion(
      eigenmath::Quaterniond{0.0, 1.0, 0.0, 0.0});
  start.robot_base_t_task.translation().setZero();
  start.robot_base_t_task.setQuaternion(
      eigenmath::Quaterniond{0.0, 1.0, 0.0, 0.0});

  CartStatePVA cart_state_sensed_with_default_speed_override;
  cart_state_sensed_with_default_speed_override.pose =
      Pose3d(start.task_t_tool_reference_orientation,
             start.task_t_tool_reference_position);
  cart_state_sensed_with_default_speed_override.velocity =
      start.tool_reference_twist;
  cart_state_sensed_with_default_speed_override.acceleration =
      start.tool_reference_acceleration;
  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
      cartesian_impedance::RealTimeCartesianTarget
          current_with_default_speed_override,
      generator_with_default_speed_override.Evaluate(
          start, cart_state_sensed_with_default_speed_override,
          kDefaultSpeedOverride));

  CartStatePVA cart_state_sensed_with_test_speed_override;
  cart_state_sensed_with_test_speed_override.pose =
      Pose3d(start.task_t_tool_reference_orientation,
             start.task_t_tool_reference_position);
  cart_state_sensed_with_test_speed_override.velocity =
      start.tool_reference_twist;
  cart_state_sensed_with_test_speed_override.acceleration =
      start.tool_reference_acceleration;
  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
      cartesian_impedance::RealTimeCartesianTarget
          current_with_test_speed_override,
      generator_with_test_speed_override.Evaluate(
          start, cart_state_sensed_with_test_speed_override,
          kTestSpeedOverride));

  // Iterate to the number comparison tests.
  for (size_t i = 0; i < kNumComparisonTests; i++) {
    INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
        current_with_default_speed_override,
        generator_with_default_speed_override.Evaluate(
            current_with_default_speed_override,
            cart_state_sensed_with_default_speed_override,
            kDefaultSpeedOverride));

    cart_state_sensed_with_default_speed_override.pose = Pose3d(
        current_with_default_speed_override.task_t_tool_reference_orientation,
        current_with_default_speed_override.task_t_tool_reference_position);
    cart_state_sensed_with_default_speed_override.velocity =
        current_with_default_speed_override.tool_reference_twist;
    cart_state_sensed_with_default_speed_override.acceleration =
        current_with_default_speed_override.tool_reference_acceleration;

    for (size_t j = 0; j < static_cast<int>(1.0 / kTestSpeedOverride); j++) {
      INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
          current_with_test_speed_override,
          generator_with_test_speed_override.Evaluate(
              current_with_test_speed_override,
              cart_state_sensed_with_test_speed_override, kTestSpeedOverride));

      cart_state_sensed_with_test_speed_override.pose = Pose3d(
          current_with_test_speed_override.task_t_tool_reference_orientation,
          current_with_test_speed_override.task_t_tool_reference_position);
      cart_state_sensed_with_test_speed_override.velocity =
          current_with_test_speed_override.tool_reference_twist;
      cart_state_sensed_with_test_speed_override.acceleration =
          current_with_test_speed_override.tool_reference_acceleration;
    }

    // We expect that the current state for the one with the default speed
    // override versus the one with the test speed override to be close after
    // iterating inversely-proportional to the speed override:
    EXPECT_THAT(
        current_with_default_speed_override.task_t_tool_reference_position,
        IsApprox(
            current_with_test_speed_override.task_t_tool_reference_position,
            kSmallTolerance));
    EXPECT_THAT(current_with_default_speed_override.tool_reference_twist *
                    kTestSpeedOverride,
                IsApprox(current_with_test_speed_override.tool_reference_twist,
                         kSmallTolerance));
    EXPECT_THAT(
        current_with_default_speed_override.tool_reference_acceleration *
            ::intrinsic::IPow(kTestSpeedOverride, 2),
        IsApprox(current_with_test_speed_override.tool_reference_acceleration,
                 kSmallTolerance));
    EXPECT_THAT(
        current_with_default_speed_override.task_t_tool_reference_orientation,
        IsApprox(
            current_with_test_speed_override.task_t_tool_reference_orientation,
            kSmallTolerance));
    EXPECT_THAT(current_with_default_speed_override.cartesian_damping,
                IsApprox(current_with_test_speed_override.cartesian_damping,
                         kSmallTolerance));
    EXPECT_THAT(current_with_default_speed_override.cartesian_stiffness,
                IsApprox(current_with_test_speed_override.cartesian_stiffness,
                         kSmallTolerance));
    EXPECT_THAT(
        current_with_default_speed_override.wrench_selection_matrix,
        IsApprox(current_with_test_speed_override.wrench_selection_matrix,
                 kSmallTolerance));
    EXPECT_THAT(
        current_with_default_speed_override.virtual_cartesian_inertia_inverse,
        IsApprox(
            current_with_test_speed_override.virtual_cartesian_inertia_inverse,
            kSmallTolerance));
    EXPECT_THAT(current_with_default_speed_override.tool_reference_wrench,
                IsApprox(current_with_test_speed_override.tool_reference_wrench,
                         kSmallTolerance));
    EXPECT_THAT(
        current_with_default_speed_override.robot_tip_t_robot_tool,
        IsApprox(current_with_test_speed_override.robot_tip_t_robot_tool,
                 kSmallTolerance));
    EXPECT_THAT(current_with_default_speed_override.robot_base_t_task,
                IsApprox(current_with_test_speed_override.robot_base_t_task,
                         kSmallTolerance));
  }
}

TEST(SimpleCartesianImpedanceReferenceGeneratorTest,
     SpeedOverrideIsInverselyProportionalToConvergenceTime) {
  const int kConvergenceMaxNumIteration = 200000;
  const double kTargetPosition = 1.0;
  const double kConvergenceThreshold = 0.999;
  const double kFractionDoubleSimilarityThreshold = 0.005;

  SimpleCartesianImpedanceReferenceGenerator generator;
  cartesian_impedance::RealTimeCartesianTarget target;
  target.task_t_tool_reference_position.setConstant(kTargetPosition);
  target.task_t_tool_reference_orientation.setIdentity();
  EXPECT_OK(generator.SetReference(target, DefaultCartesianLimits()));

  // Create an arbitrary initial state, which is not necessarily physically
  // meaningful. Here we mainly care about the pose.
  cartesian_impedance::RealTimeCartesianTarget start;
  start.task_t_tool_reference_position.setConstant(0);
  start.task_t_tool_reference_orientation.coeffs() << 0, 1, 0, 0;
  CartStatePVA cart_state_sensed;
  cart_state_sensed.pose = Pose3d(start.task_t_tool_reference_orientation,
                                  start.task_t_tool_reference_position);

  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
      cartesian_impedance::RealTimeCartesianTarget current,
      generator.Evaluate(start, cart_state_sensed, kDefaultSpeedOverride));

  int num_iter_converged_with_default_speed_override = -1;
  // Iterate until we expect convergence.
  for (int i = 0; i < kConvergenceMaxNumIteration; i++) {
    INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
        current,
        generator.Evaluate(current, cart_state_sensed, kDefaultSpeedOverride));

    cart_state_sensed.pose = Pose3d(current.task_t_tool_reference_orientation,
                                    current.task_t_tool_reference_position);

    // For the purpose of this test, we only check the 1st dimension of position
    // for convergence:
    if (current.task_t_tool_reference_position[0] >=
        kConvergenceThreshold * kTargetPosition) {
      num_iter_converged_with_default_speed_override = i;
      break;
    }
  }

  for (double speed_override : {0.0, 0.10, 0.25, 0.50, 0.75, 1.00}) {
    SCOPED_TRACE(absl::StrCat("Speed override ", speed_override));

    // Reset state:
    cart_state_sensed.pose = Pose3d(start.task_t_tool_reference_orientation,
                                    start.task_t_tool_reference_position);

    INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
        current, generator.Evaluate(start, cart_state_sensed, speed_override));

    int num_iter_converged_with_test_speed_override = -1;
    // Iterate until we expect convergence.
    for (int i = 0; i < kConvergenceMaxNumIteration; i++) {
      INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
          current,
          generator.Evaluate(current, cart_state_sensed, speed_override));

      cart_state_sensed.pose = Pose3d(current.task_t_tool_reference_orientation,
                                      current.task_t_tool_reference_position);

      // For the purpose of this test, we only check the 1st dimension of
      // position for convergence:
      if (current.task_t_tool_reference_position[0] >=
          kConvergenceThreshold * kTargetPosition) {
        num_iter_converged_with_test_speed_override = i;
        break;
      }
    }

    if (speed_override == 0.0) {
      // If speed override is zero, the expected convergence time is infinity,
      // which in this case translates to:
      EXPECT_EQ(num_iter_converged_with_test_speed_override, -1);
    } else {
      // We expect the number of iterations to achieve the convergence to be
      // inversely-proportional to the speed override.
      EXPECT_THAT(
          static_cast<double>(num_iter_converged_with_test_speed_override) *
              speed_override / kDefaultSpeedOverride,
          DoubleNear(static_cast<double>(
                         num_iter_converged_with_default_speed_override),
                     kFractionDoubleSimilarityThreshold *
                         num_iter_converged_with_default_speed_override));
    }
  }
}

TEST(SimpleNullspaceReferenceGeneratorTest, ConfigureWorks) {
  SimpleNullspaceReferenceGenerator generator;

  cartesian_impedance::SimpleNullspaceReferenceGeneratorParameters params;
  params.lowpass_filter_constant = 0.2;
  EXPECT_OK(generator.Configure(params));
  EXPECT_EQ(generator.alpha(), params.lowpass_filter_constant);
}

TEST(SimpleNullspaceReferenceGeneratorTest,
     ConfigureErrorsForOutOfRangeParameters) {
  SimpleNullspaceReferenceGenerator generator;
  cartesian_impedance::SimpleNullspaceReferenceGeneratorParameters params;

  params.lowpass_filter_constant = -0.001;
  EXPECT_THAT(
      generator.Configure(params),
      StatusIs(absl::StatusCode::kFailedPrecondition,
               HasSubstr("Lowpass filter constant out of bound ]0 1]")));

  params.lowpass_filter_constant = 1.001;
  EXPECT_THAT(
      generator.Configure(params),
      StatusIs(absl::StatusCode::kFailedPrecondition,
               HasSubstr("Lowpass filter constant out of bound ]0 1]")));
}

TEST(SimpleNullspaceReferenceGeneratorTest,
     InitializationFailsIfReferenceViolatesLimits) {
  SimpleNullspaceReferenceGenerator generator;
  cartesian_impedance::RealTimeNullspaceTarget target;
  ASSERT_OK(target.joint_state.SetSize(kNumJoints));
  target.joint_state.position.setConstant(1000);
  EXPECT_THAT(generator.SetReference(target, DefaultJointLimits()),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       HasSubstr("is not within limits")));
}

TEST(SimpleNullspaceReferenceGeneratorTest,
     InitializationPassesForAdmissibleReference) {
  SimpleNullspaceReferenceGenerator generator;
  cartesian_impedance::RealTimeNullspaceTarget target;
  ASSERT_OK(target.joint_state.SetSize(kNumJoints));
  EXPECT_OK(generator.SetReference(target, DefaultJointLimits()));
}

TEST(SimpleNullspaceReferenceGeneratorTest, InvalidSpeedOverride) {
  SimpleNullspaceReferenceGenerator generator;
  cartesian_impedance::RealTimeNullspaceTarget start;

  EXPECT_THAT(
      generator.Evaluate(start, -0.05),
      RealtimeStatusIs(absl::StatusCode::kInvalidArgument,
                       AllOf(HasSubstr("speed_override should be in range ["),
                             HasSubstr("], but instead has value -0.05."))));

  EXPECT_THAT(
      generator.Evaluate(start, 1.07),
      RealtimeStatusIs(absl::StatusCode::kInvalidArgument,
                       AllOf(HasSubstr("speed_override should be in range ["),
                             HasSubstr("], but instead has value 1.07."))));
}

TEST(SimpleNullspaceReferenceGeneratorTest, ConvergesToReference) {
  SimpleNullspaceReferenceGenerator generator;
  cartesian_impedance::RealTimeNullspaceTarget target;
  ASSERT_OK(target.joint_state.SetSize(kNumJoints));
  target.joint_state.position.setOnes();
  target.joint_state.velocity.setOnes();
  target.joint_state.acceleration.setOnes();
  target.nullspace_damping = 0.0;
  target.nullspace_stiffness = 0.0;
  EXPECT_OK(generator.SetReference(target, DefaultJointLimits()));

  // Create an arbitrary initial state, which is not necessarily physically
  // meaningful.
  cartesian_impedance::RealTimeNullspaceTarget start;
  ASSERT_OK(start.joint_state.SetSize(kNumJoints));
  start.joint_state.position.setZero();
  start.joint_state.position.setZero();
  start.joint_state.position.setZero();
  start.nullspace_damping = 1.0;
  start.nullspace_stiffness = -1.0;

  // We expect to be close to the start after one call to the reference
  // generator.
  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
      cartesian_impedance::RealTimeNullspaceTarget current,
      generator.Evaluate(start, kDefaultSpeedOverride));
  EXPECT_THAT(current.joint_state.position,
              IsApprox(start.joint_state.position, kSmallTolerance));
  EXPECT_THAT(current.joint_state.velocity,
              IsApprox(start.joint_state.velocity, kSmallTolerance));
  EXPECT_THAT(current.joint_state.acceleration,
              IsApprox(start.joint_state.acceleration, kSmallTolerance));
  EXPECT_NEAR(current.nullspace_damping, start.nullspace_damping,
              kSmallTolerance);
  EXPECT_NEAR(current.nullspace_stiffness, start.nullspace_stiffness,
              kSmallTolerance);

  // Iterate until we expect convergence.
  for (size_t i = 0; i < 2000; i++) {
    INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
        current, generator.Evaluate(current, kDefaultSpeedOverride));
  }

  // We expect to be close to the target after iterating.
  EXPECT_THAT(current.joint_state.position,
              IsApprox(target.joint_state.position, kSmallTolerance));
  EXPECT_THAT(current.joint_state.velocity,
              IsApprox(target.joint_state.velocity, kSmallTolerance));
  EXPECT_THAT(current.joint_state.acceleration,
              IsApprox(target.joint_state.acceleration, kSmallTolerance));
  EXPECT_NEAR(current.nullspace_damping, target.nullspace_damping,
              kSmallTolerance);
  EXPECT_NEAR(current.nullspace_stiffness, target.nullspace_stiffness,
              kSmallTolerance);
}

TEST(SimpleNullspaceReferenceGeneratorTest, ComparableValuesWithSpeedOverride) {
  const double kTestSpeedOverride = 0.2;
  const int kNumComparisonTests = 1000;

  SimpleNullspaceReferenceGenerator generator_with_default_speed_override,
      generator_with_test_speed_override;
  cartesian_impedance::RealTimeNullspaceTarget target;
  ASSERT_OK(target.joint_state.SetSize(kNumJoints));
  target.joint_state.position.setOnes();
  target.joint_state.velocity.setOnes();
  target.joint_state.acceleration.setOnes();
  target.nullspace_damping = 0.0;
  target.nullspace_stiffness = 0.0;
  EXPECT_OK(generator_with_default_speed_override.SetReference(
      target, DefaultJointLimits()));
  EXPECT_OK(generator_with_test_speed_override.SetReference(
      target, DefaultJointLimits()));

  // Create an arbitrary initial state, which is not necessarily physically
  // meaningful.
  cartesian_impedance::RealTimeNullspaceTarget start;
  ASSERT_OK(start.joint_state.SetSize(kNumJoints));
  start.joint_state.position.setZero();
  start.joint_state.position.setZero();
  start.joint_state.position.setZero();
  start.nullspace_damping = 1.0;
  start.nullspace_stiffness = -1.0;

  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
      cartesian_impedance::RealTimeNullspaceTarget
          current_with_default_speed_override,
      generator_with_default_speed_override.Evaluate(start,
                                                     kDefaultSpeedOverride));

  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
      cartesian_impedance::RealTimeNullspaceTarget
          current_with_test_speed_override,
      generator_with_test_speed_override.Evaluate(start, kTestSpeedOverride));

  // Iterate to the number comparison tests.
  for (size_t i = 0; i < kNumComparisonTests; i++) {
    INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
        current_with_default_speed_override,
        generator_with_default_speed_override.Evaluate(
            current_with_default_speed_override, kDefaultSpeedOverride));

    for (size_t j = 0; j < static_cast<int>(1.0 / kTestSpeedOverride); j++) {
      INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
          current_with_test_speed_override,
          generator_with_test_speed_override.Evaluate(
              current_with_test_speed_override, kTestSpeedOverride));
    }

    // We expect that the current state for the one with the default speed
    // override versus the one with the test speed override to be close after
    // iterating inversely-proportional to the speed override:
    EXPECT_THAT(current_with_default_speed_override.joint_state.position,
                IsApprox(current_with_test_speed_override.joint_state.position,
                         kSmallTolerance));
    EXPECT_THAT(current_with_default_speed_override.joint_state.velocity *
                    kTestSpeedOverride,
                IsApprox(current_with_test_speed_override.joint_state.velocity,
                         kSmallTolerance));
    EXPECT_THAT(
        current_with_default_speed_override.joint_state.acceleration *
            ::intrinsic::IPow(kTestSpeedOverride, 2),
        IsApprox(current_with_test_speed_override.joint_state.acceleration,
                 kSmallTolerance));
    EXPECT_NEAR(current_with_default_speed_override.nullspace_damping,
                current_with_test_speed_override.nullspace_damping,
                kSmallTolerance);
    EXPECT_NEAR(current_with_default_speed_override.nullspace_stiffness,
                current_with_test_speed_override.nullspace_stiffness,
                kSmallTolerance);
  }
}

TEST(SimpleNullspaceReferenceGeneratorTest,
     SpeedOverrideIsInverselyProportionalToConvergenceTime) {
  const int kConvergenceMaxNumIteration = 200000;
  const double kTargetPosition = 1.0;
  const double kConvergenceThreshold = 0.999;
  const double kFractionDoubleSimilarityThreshold = 0.005;

  SimpleNullspaceReferenceGenerator generator;
  cartesian_impedance::RealTimeNullspaceTarget target;
  ASSERT_OK(target.joint_state.SetSize(kNumJoints));
  target.joint_state.position.setConstant(kTargetPosition);
  EXPECT_OK(generator.SetReference(target, DefaultJointLimits()));

  // Create an arbitrary initial state, which is not necessarily physically
  // meaningful. Here we mainly care about the pose.
  cartesian_impedance::RealTimeNullspaceTarget start;
  ASSERT_OK(start.joint_state.SetSize(kNumJoints));
  start.joint_state.position.setConstant(0);

  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
      cartesian_impedance::RealTimeNullspaceTarget current,
      generator.Evaluate(start, kDefaultSpeedOverride));

  int num_iter_converged_with_default_speed_override = -1;
  // Iterate until we expect convergence.
  for (int i = 0; i < kConvergenceMaxNumIteration; i++) {
    INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
        current, generator.Evaluate(current, kDefaultSpeedOverride));

    // For the purpose of this test, we only check the 1st dimension of position
    // for convergence:
    if (current.joint_state.position[0] >=
        kConvergenceThreshold * kTargetPosition) {
      num_iter_converged_with_default_speed_override = i;
      break;
    }
  }

  for (double speed_override : {0.0, 0.10, 0.25, 0.50, 0.75, 1.00}) {
    SCOPED_TRACE(absl::StrCat("Speed override ", speed_override));

    INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
        current, generator.Evaluate(start, speed_override));

    int num_iter_converged_with_test_speed_override = -1;
    // Iterate until we expect convergence.
    for (int i = 0; i < kConvergenceMaxNumIteration; i++) {
      INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
          current, generator.Evaluate(current, speed_override));

      // For the purpose of this test, we only check the 1st dimension of
      // position for convergence:
      if (current.joint_state.position[0] >= kConvergenceThreshold) {
        num_iter_converged_with_test_speed_override = i;
        break;
      }
    }

    if (speed_override == 0.0) {
      // If speed override is zero, the expected convergence time is infinity,
      // which in this case translates to:
      EXPECT_EQ(num_iter_converged_with_test_speed_override, -1);
    } else {
      // We expect the number of iterations to achieve the convergence to be
      // inversely-proportional to the speed override.
      EXPECT_THAT(
          static_cast<double>(num_iter_converged_with_test_speed_override) *
              speed_override / kDefaultSpeedOverride,
          DoubleNear(static_cast<double>(
                         num_iter_converged_with_default_speed_override),
                     kFractionDoubleSimilarityThreshold *
                         num_iter_converged_with_default_speed_override));
    }
  }
}

}  // namespace
}  // namespace intrinsic::icon
