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

#include "intrinsic/icon/control/algorithms/position_reflexxes_cartesian_impedance_reference_generator.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/strings/str_cat.h"
#include "internal/testing.h"
#include "intrinsic/eigenmath/matchers.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/control/algorithms/cartesian_impedance_commons.h"
#include "intrinsic/icon/utils/realtime_status_matchers.h"
#include "intrinsic/kinematics/types/cart_state.h"
#include "intrinsic/kinematics/types/cartesian_limits.h"
#include "intrinsic/math/ipow.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/twist.h"

namespace intrinsic::icon {
namespace {

using ::absl_testing::StatusIs;
using eigenmath::testing::IsApprox;
using ::testing::DoubleNear;
using ::testing::HasSubstr;

constexpr double kMinPos = -4;
constexpr double kMaxPos = 4;
constexpr double kMinVel = -2;
constexpr double kMaxVel = 2;
constexpr double kMinAcc = -2;
constexpr double kMaxAcc = 2;
constexpr double kMinJer = -2;
constexpr double kMaxJer = 2;

constexpr double kFrequencyHz = 100;
constexpr double kSmallTolerance = 0.05;
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

TEST(PositionReflexxesCartesianImpedanceReferenceGeneratorTest,
     InitializationFailsIfReferenceViolatesLimits) {
  PositionReflexxesCartesianImpedanceReferenceGenerator generator(kFrequencyHz);
  cartesian_impedance::RealTimeCartesianTarget target;
  target.task_t_tool_reference_position.setConstant(1000);
  target.task_t_tool_reference_orientation.setIdentity();
  EXPECT_THAT(generator.SetReference(target, DefaultCartesianLimits()),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       HasSubstr("is not within limits")));
}

TEST(PositionReflexxesCartesianImpedanceReferenceGeneratorTest,
     InitializationPassesForAdmissibleReference) {
  PositionReflexxesCartesianImpedanceReferenceGenerator generator(kFrequencyHz);
  cartesian_impedance::RealTimeCartesianTarget target;
  target.task_t_tool_reference_position.setZero();
  target.task_t_tool_reference_orientation.setIdentity();
  EXPECT_OK(generator.SetReference(target, DefaultCartesianLimits()));
}

TEST(PositionReflexxesCartesianImpedanceReferenceGeneratorTest,
     InvalidSpeedOverride) {
  PositionReflexxesCartesianImpedanceReferenceGenerator generator(kFrequencyHz);

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

TEST(PositionReflexxesCartesianImpedanceReferenceGeneratorTest,
     ConvergesToReference) {
  PositionReflexxesCartesianImpedanceReferenceGenerator generator(kFrequencyHz);
  cartesian_impedance::RealTimeCartesianTarget target;
  target.task_t_tool_reference_position.setConstant(1);
  target.task_t_tool_reference_orientation.setIdentity();
  target.cartesian_damping.setIdentity();
  target.cartesian_stiffness.setIdentity();
  target.virtual_cartesian_inertia_inverse.setIdentity();
  target.tool_reference_twist.setZero();
  target.tool_reference_acceleration.setZero();
  target.tool_reference_wrench.setOnes();
  EXPECT_OK(generator.SetReference(target, DefaultCartesianLimits()));

  // Create an arbitrary initial state, which is not necessarily physically
  // meaningful.
  cartesian_impedance::RealTimeCartesianTarget start;
  start.task_t_tool_reference_position.setConstant(0);
  start.task_t_tool_reference_orientation.setIdentity();
  start.cartesian_damping.setZero();
  start.cartesian_stiffness.setZero();
  start.virtual_cartesian_inertia_inverse.setZero();
  start.tool_reference_twist.setZero();
  start.tool_reference_acceleration.setZero();
  start.tool_reference_wrench.setZero();
  CartStatePVA cart_state_sensed;
  cart_state_sensed.pose = Pose3d(start.task_t_tool_reference_orientation,
                                  start.task_t_tool_reference_position);
  cart_state_sensed.velocity = start.tool_reference_twist;
  cart_state_sensed.acceleration = start.tool_reference_acceleration;

  // We expect the quantities that get interpolated to be close to the start
  // after one call to the reference generator.
  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
      cartesian_impedance::RealTimeCartesianTarget current,
      generator.Evaluate(start, cart_state_sensed, kDefaultSpeedOverride));
  EXPECT_THAT(current.task_t_tool_reference_position,
              IsApprox(start.task_t_tool_reference_position, kSmallTolerance));
  EXPECT_THAT(
      current.task_t_tool_reference_orientation,
      IsApprox(start.task_t_tool_reference_orientation, kSmallTolerance));
  EXPECT_THAT(current.tool_reference_twist,
              IsApprox(start.tool_reference_twist, kSmallTolerance));
  EXPECT_THAT(current.tool_reference_acceleration,
              IsApprox(start.tool_reference_acceleration, kSmallTolerance));

  // Compare the quantities that don't get interpolated, they should "jump" to
  // the reference value.
  EXPECT_THAT(current.cartesian_damping, IsApprox(target.cartesian_damping));
  EXPECT_THAT(current.cartesian_stiffness,
              IsApprox(target.cartesian_stiffness));
  EXPECT_THAT(current.virtual_cartesian_inertia_inverse,
              IsApprox(target.virtual_cartesian_inertia_inverse));
  EXPECT_THAT(current.tool_reference_wrench,
              IsApprox(target.tool_reference_wrench));
  EXPECT_THAT(current.robot_tip_t_robot_tool,
              IsApprox(target.robot_tip_t_robot_tool));
  EXPECT_THAT(current.robot_base_t_task, IsApprox(target.robot_base_t_task));

  // Iterate until we expect convergence.
  for (size_t i = 0; i < 1000; i++) {
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
  EXPECT_THAT(current.tool_reference_twist,
              IsApprox(target.tool_reference_twist, kSmallTolerance));
  EXPECT_THAT(current.tool_reference_acceleration,
              IsApprox(target.tool_reference_acceleration, kSmallTolerance));
}

TEST(PositionReflexxesCartesianImpedanceReferenceGeneratorTest,
     ComparableValuesWithSpeedOverride) {
  const double kTestSpeedOverride = 0.25;
  const int kNumComparisonTests = 100;

  PositionReflexxesCartesianImpedanceReferenceGenerator
      generator_with_default_speed_override(kFrequencyHz),
      generator_with_test_speed_override(kFrequencyHz);
  cartesian_impedance::RealTimeCartesianTarget target;
  target.task_t_tool_reference_position.setConstant(1);
  target.task_t_tool_reference_orientation.coeffs() << 0, 0, 0.7071068,
      0.7071068;
  target.tool_reference_twist.setOnes();
  target.tool_reference_acceleration.setOnes();
  target.cartesian_damping.setIdentity();
  target.cartesian_stiffness.setIdentity();
  target.virtual_cartesian_inertia_inverse.setIdentity();
  target.tool_reference_wrench.setOnes();
  EXPECT_OK(generator_with_default_speed_override.SetReference(
      target, DefaultCartesianLimits()));
  EXPECT_OK(generator_with_test_speed_override.SetReference(
      target, DefaultCartesianLimits()));

  // Create an arbitrary initial state, which is not necessarily physically
  // meaningful.
  cartesian_impedance::RealTimeCartesianTarget start;
  start.task_t_tool_reference_position.setConstant(0);
  start.task_t_tool_reference_orientation.setIdentity();
  start.tool_reference_twist.setZero();
  start.tool_reference_acceleration.setZero();
  start.cartesian_damping.setZero();
  start.cartesian_stiffness.setZero();
  start.virtual_cartesian_inertia_inverse.setZero();
  start.tool_reference_wrench.setZero();

  CartStatePVA cart_state_sensed_with_default_speed_override;
  cart_state_sensed_with_default_speed_override.pose =
      Pose3d(start.task_t_tool_reference_orientation,
             start.task_t_tool_reference_position);
  cart_state_sensed_with_default_speed_override.velocity =
      start.tool_reference_twist;
  cart_state_sensed_with_default_speed_override.acceleration =
      start.tool_reference_acceleration;
  cartesian_impedance::RealTimeCartesianTarget
      current_with_default_speed_override = start;

  CartStatePVA cart_state_sensed_with_test_speed_override =
      cart_state_sensed_with_default_speed_override;
  cartesian_impedance::RealTimeCartesianTarget
      current_with_test_speed_override = start;

  // Iterate to the number comparison tests.
  for (size_t i = 0; i < kNumComparisonTests; i++) {
    // Advance the generator_with_default_speed_override one step ahead.
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

    // Advance the generator_with_test_speed_override (1.0 / kTestSpeedOverride)
    // steps ahead, so that after these many steps the state will be comparable.
    for (size_t j = 0; j < static_cast<int>(round(1.0 / kTestSpeedOverride));
         j++) {
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

    // We expect that the positions for the one with the default speed
    // override versus the one with the test speed override to be close, while
    // the velocities and accelerations to be scaled accordingly after iterating
    // inversely-proportional to the speed override:
    EXPECT_THAT(
        current_with_default_speed_override.task_t_tool_reference_position,
        IsApprox(
            current_with_test_speed_override.task_t_tool_reference_position,
            kSmallTolerance));
    EXPECT_THAT(
        current_with_default_speed_override.task_t_tool_reference_orientation,
        IsApprox(
            current_with_test_speed_override.task_t_tool_reference_orientation,
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
  }
}

TEST(PositionReflexxesCartesianImpedanceReferenceGeneratorTest,
     SpeedOverrideIsInverselyProportionalToConvergenceTime) {
  const int kConvergenceMaxNumIteration = 20000;
  const double kTargetPosition = 1.0;
  const double kConvergenceThreshold = 0.999;
  const double kFractionDoubleSimilarityThreshold = 0.0075;

  PositionReflexxesCartesianImpedanceReferenceGenerator generator(kFrequencyHz);
  cartesian_impedance::RealTimeCartesianTarget target;
  target.task_t_tool_reference_position.setConstant(kTargetPosition);
  target.task_t_tool_reference_orientation.coeffs() << 0, 0, 0.7071068,
      0.7071068;
  target.tool_reference_twist.setOnes();
  target.tool_reference_acceleration.setOnes();
  EXPECT_OK(generator.SetReference(target, DefaultCartesianLimits()));

  // Create an arbitrary initial state, which is not necessarily physically
  // meaningful.
  cartesian_impedance::RealTimeCartesianTarget start;
  start.task_t_tool_reference_position.setConstant(0);
  start.task_t_tool_reference_orientation.setIdentity();
  start.tool_reference_twist.setZero();
  start.tool_reference_acceleration.setZero();
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

    // For the purpose of this test, we only check the position for convergence:
    if ((target.task_t_tool_reference_position -
         current.task_t_tool_reference_position)
            .norm() <
        (1.0 - kConvergenceThreshold) * (target.task_t_tool_reference_position -
                                         start.task_t_tool_reference_position)
                                            .norm()) {
      num_iter_converged_with_default_speed_override = i;
      break;
    }
  }

  for (double speed_override :
       {0.0, 0.1, 0.15, 0.2, 0.225, 0.25, 0.50, 0.75, 1.00}) {
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

      // For the purpose of this test, we only check the position for
      // convergence:
      if ((target.task_t_tool_reference_position -
           current.task_t_tool_reference_position)
              .norm() < (1.0 - kConvergenceThreshold) *
                            (target.task_t_tool_reference_position -
                             start.task_t_tool_reference_position)
                                .norm()) {
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
