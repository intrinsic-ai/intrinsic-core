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

#include "intrinsic/icon/control/algorithms/quintic_spline_cartesian_impedance_reference_generator.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/strings/str_cat.h"
#include "internal/testing.h"
#include "intrinsic/eigenmath/matchers.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/actions/cartesian_impedance_parameters.h"
#include "intrinsic/icon/control/algorithms/cartesian_impedance_commons.h"
#include "intrinsic/icon/utils/realtime_status_macro.h"
#include "intrinsic/icon/utils/realtime_status_matchers.h"
#include "intrinsic/icon/utils/realtime_status_or.h"
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
using ::intrinsic::eigenmath::testing::IsApprox;
using ::intrinsic::icon::cartesian_impedance::RealTimeCartesianTarget;
using ::intrinsic::icon::cartesian_impedance::RealTimeNullspaceTarget;
using ::testing::AllOf;
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

constexpr double kCommonMovementDurationSeconds = 2.75;
constexpr double kCommonDtSeconds = 1.0e-3;
constexpr double kDefaultSpeedOverride = 1.0;

constexpr double kSmallTolerance = 0.01;

CartesianLimits DefaultCartesianLimits() {
  CartesianLimits limits;
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

RealTimeCartesianTarget DefaultRealTimeCartesianTarget() {
  RealTimeCartesianTarget default_rt_cartesian_target;
  default_rt_cartesian_target.task_t_tool_reference_position.setConstant(0);
  default_rt_cartesian_target.task_t_tool_reference_orientation.setIdentity();
  default_rt_cartesian_target.cartesian_damping.setZero();
  default_rt_cartesian_target.cartesian_stiffness.setZero();
  default_rt_cartesian_target.virtual_cartesian_inertia_inverse.setZero();
  default_rt_cartesian_target.tool_reference_twist.setZero();
  default_rt_cartesian_target.tool_reference_acceleration.setZero();
  default_rt_cartesian_target.tool_reference_wrench.setZero();
  return default_rt_cartesian_target;
}

RealTimeNullspaceTarget DefaultRealTimeNullspaceTarget() {
  RealTimeNullspaceTarget default_rt_nullspace_target;
  CHECK_OK(default_rt_nullspace_target.joint_state.SetSize(kNumJoints));
  default_rt_nullspace_target.joint_state.position.setZero();
  default_rt_nullspace_target.joint_state.velocity.setZero();
  default_rt_nullspace_target.joint_state.acceleration.setZero();
  default_rt_nullspace_target.nullspace_damping = 0.0;
  default_rt_nullspace_target.nullspace_stiffness = 0.0;
  return default_rt_nullspace_target;
}

cartesian_impedance::QuinticSplineCartesianGeneratorParameters
DefaultCartesianGeneratorParameters() {
  return {.movement_duration_seconds = kCommonMovementDurationSeconds};
}

cartesian_impedance::QuinticSplineNullspaceReferenceGeneratorParameters
DefaultNullspaceGeneratorParameters() {
  return {.movement_duration_seconds = kCommonMovementDurationSeconds};
}

TEST(QuinticSplineCartesianImpedanceReferenceGeneratorTest, ConfigureWorks) {
  QuinticSplineCartesianImpedanceReferenceGenerator generator(kCommonDtSeconds);

  auto params = DefaultCartesianGeneratorParameters();
  EXPECT_OK(generator.Configure(params));
  EXPECT_EQ(generator.nominal_movement_duration_seconds(),
            params.movement_duration_seconds);
}

TEST(QuinticSplineCartesianImpedanceReferenceGeneratorTest,
     ConfigurationErrorsForOutofRangeParameters) {
  QuinticSplineCartesianImpedanceReferenceGenerator generator(kCommonDtSeconds);

  auto params = DefaultCartesianGeneratorParameters();
  EXPECT_OK(generator.Configure(params));

  params.movement_duration_seconds = -0.01;
  EXPECT_THAT(generator.Configure(params),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       HasSubstr("must be >= 0.0")));
}

TEST(QuinticSplineCartesianImpedanceReferenceGeneratorTest,
     InvalidSpeedOverride) {
  QuinticSplineCartesianImpedanceReferenceGenerator generator(kCommonDtSeconds);
  RealTimeCartesianTarget start;
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

TEST(QuinticSplineCartesianImpedanceReferenceGeneratorTest,
     ReturnsErrorIfUnconfigured) {
  QuinticSplineCartesianImpedanceReferenceGenerator generator(kCommonDtSeconds);

  RealTimeCartesianTarget target = DefaultRealTimeCartesianTarget();
  CartesianLimits limits = DefaultCartesianLimits();
  EXPECT_THAT(generator.SetReference(target, limits),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       HasSubstr("Movement duration not set")));

  CartStatePVA cart_state_sensed;
  cart_state_sensed.pose = Pose3d(target.task_t_tool_reference_orientation,
                                  target.task_t_tool_reference_position);
  cart_state_sensed.velocity = target.tool_reference_twist;
  cart_state_sensed.acceleration = target.tool_reference_acceleration;
  EXPECT_THAT(
      generator.Evaluate(target, cart_state_sensed, kDefaultSpeedOverride)
          .status(),
      StatusIs(absl::StatusCode::kOutOfRange,
               AllOf(HasSubstr("phase_to_go_ must be >= 0"),
                     HasSubstr("missing a call to SetReference()"))));
}

TEST(QuinticSplineCartesianImpedanceReferenceGeneratorTest,
     InitializationFailsIfReferenceViolatesLimits) {
  QuinticSplineCartesianImpedanceReferenceGenerator generator(kCommonDtSeconds);
  RealTimeCartesianTarget target;
  target.task_t_tool_reference_position.setConstant(1000);
  target.task_t_tool_reference_orientation.setIdentity();
  ASSERT_OK(generator.Configure(DefaultCartesianGeneratorParameters()));
  intrinsic::CartesianLimits limits = DefaultCartesianLimits();
  // Target position is larger than max position in limits
  EXPECT_THAT(generator.SetReference(target, limits),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       HasSubstr("is not within limits")));
}

TEST(QuinticSplineCartesianImpedanceReferenceGeneratorTest,
     InitializationPassesForAdmissibleReference) {
  QuinticSplineCartesianImpedanceReferenceGenerator generator(kCommonDtSeconds);
  EXPECT_OK(generator.Configure(DefaultCartesianGeneratorParameters()));
  RealTimeCartesianTarget target = DefaultRealTimeCartesianTarget();
  EXPECT_OK(generator.SetReference(target, DefaultCartesianLimits()));
}

TEST(QuinticSplineCartesianImpedanceReferenceGeneratorTest,
     ZeroMovementDurationSeconds) {
  QuinticSplineCartesianImpedanceReferenceGenerator generator(kCommonDtSeconds);
  EXPECT_OK(generator.Configure(
      cartesian_impedance::QuinticSplineCartesianGeneratorParameters{
          .movement_duration_seconds = 0.0}));
  RealTimeCartesianTarget target = DefaultRealTimeCartesianTarget();
  target.task_t_tool_reference_position.setConstant(1);
  target.task_t_tool_reference_orientation.coeffs() << 0, 1, 0, 0;
  target.cartesian_damping.setIdentity();
  target.cartesian_stiffness.setIdentity();
  target.virtual_cartesian_inertia_inverse.setIdentity();
  target.tool_reference_wrench.setOnes();
  EXPECT_OK(generator.SetReference(target, DefaultCartesianLimits()));

  // Create an arbitrary initial state, which is not necessarily physically
  // meaningful.
  RealTimeCartesianTarget start = DefaultRealTimeCartesianTarget();
  CartStatePVA cart_state_sensed;
  cart_state_sensed.pose = Pose3d(start.task_t_tool_reference_orientation,
                                  start.task_t_tool_reference_position);
  cart_state_sensed.velocity = start.tool_reference_twist;
  cart_state_sensed.acceleration = start.tool_reference_acceleration;

  // We expect the quantities that get interpolated to be the same as the target
  // after one call to the reference generator.
  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
      RealTimeCartesianTarget current,
      generator.Evaluate(start, cart_state_sensed, kDefaultSpeedOverride));
  EXPECT_EQ(current.task_t_tool_reference_position,
            target.task_t_tool_reference_position);
  EXPECT_EQ(current.task_t_tool_reference_orientation,
            target.task_t_tool_reference_orientation);
  EXPECT_EQ(current.tool_reference_twist, target.tool_reference_twist);
  EXPECT_EQ(current.tool_reference_acceleration,
            target.tool_reference_acceleration);
  EXPECT_EQ(current.cartesian_damping, target.cartesian_damping);
  EXPECT_EQ(current.cartesian_stiffness, target.cartesian_stiffness);
  EXPECT_EQ(current.virtual_cartesian_inertia_inverse,
            target.virtual_cartesian_inertia_inverse);
  EXPECT_EQ(current.tool_reference_wrench, target.tool_reference_wrench);
}

TEST(QuinticSplineCartesianImpedanceReferenceGeneratorTest,
     NonInterpolatedQuantitiesJumpToReference) {
  QuinticSplineCartesianImpedanceReferenceGenerator generator(kCommonDtSeconds);
  EXPECT_OK(generator.Configure(DefaultCartesianGeneratorParameters()));
  RealTimeCartesianTarget target = DefaultRealTimeCartesianTarget();
  target.task_t_tool_reference_position.setConstant(1);
  target.task_t_tool_reference_orientation.coeffs() << 0, 1, 0, 0;
  target.cartesian_damping.setIdentity();
  target.cartesian_stiffness.setIdentity();
  target.virtual_cartesian_inertia_inverse.setIdentity();
  target.tool_reference_wrench.setOnes();
  EXPECT_OK(generator.SetReference(target, DefaultCartesianLimits()));

  // Create an arbitrary initial state, which is not necessarily physically
  // meaningful.
  RealTimeCartesianTarget start = DefaultRealTimeCartesianTarget();
  CartStatePVA cart_state_sensed;
  cart_state_sensed.pose = Pose3d(start.task_t_tool_reference_orientation,
                                  start.task_t_tool_reference_position);
  cart_state_sensed.velocity = start.tool_reference_twist;
  cart_state_sensed.acceleration = start.tool_reference_acceleration;

  // We expect the quantities that get interpolated to be close to the start
  // after one call to the reference generator.
  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
      RealTimeCartesianTarget current,
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
}

TEST(QuinticSplineCartesianImpedanceReferenceGeneratorTest,
     HalfWayToReference) {
  QuinticSplineCartesianImpedanceReferenceGenerator generator(kCommonDtSeconds);
  EXPECT_OK(generator.Configure(DefaultCartesianGeneratorParameters()));
  RealTimeCartesianTarget target = DefaultRealTimeCartesianTarget();
  target.task_t_tool_reference_position.setConstant(1);
  target.task_t_tool_reference_orientation.coeffs() << 0, 1, 0, 0;
  target.cartesian_damping.setIdentity();
  target.cartesian_stiffness.setIdentity();
  target.virtual_cartesian_inertia_inverse.setIdentity();
  target.tool_reference_wrench.setOnes();
  EXPECT_OK(generator.SetReference(target, DefaultCartesianLimits()));

  // Create an arbitrary initial state, which is not necessarily physically
  // meaningful.
  RealTimeCartesianTarget start = DefaultRealTimeCartesianTarget();
  CartStatePVA cart_state_sensed;
  cart_state_sensed.pose = Pose3d(start.task_t_tool_reference_orientation,
                                  start.task_t_tool_reference_position);
  cart_state_sensed.velocity = start.tool_reference_twist;
  cart_state_sensed.acceleration = start.tool_reference_acceleration;
  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
      RealTimeCartesianTarget current,
      generator.Evaluate(start, cart_state_sensed, kDefaultSpeedOverride));

  // Iterate until we expect convergence.
  for (auto i = static_cast<size_t>(
           round(kCommonMovementDurationSeconds / kCommonDtSeconds));
       i >= static_cast<size_t>(
                round(kCommonMovementDurationSeconds / kCommonDtSeconds)) /
                2;
       --i) {
    INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
        current,
        generator.Evaluate(current, cart_state_sensed, kDefaultSpeedOverride));

    cart_state_sensed.pose = Pose3d(current.task_t_tool_reference_orientation,
                                    current.task_t_tool_reference_position);
    cart_state_sensed.velocity = current.tool_reference_twist;
    cart_state_sensed.acceleration = current.tool_reference_acceleration;
  }

  // We expect to reach half-way at this iteration.
  EXPECT_THAT(current.task_t_tool_reference_position,
              IsApprox((start.task_t_tool_reference_position +
                        target.task_t_tool_reference_position) /
                           2,
                       kSmallTolerance));
}

TEST(QuinticSplineCartesianImpedanceReferenceGeneratorTest,
     ConvergesToReference1) {
  QuinticSplineCartesianImpedanceReferenceGenerator generator(kCommonDtSeconds);
  EXPECT_OK(generator.Configure(DefaultCartesianGeneratorParameters()));
  RealTimeCartesianTarget target = DefaultRealTimeCartesianTarget();
  target.task_t_tool_reference_position.setConstant(1);
  target.task_t_tool_reference_orientation.coeffs() << 0, 1, 0, 0;
  target.cartesian_damping.setIdentity();
  target.cartesian_stiffness.setIdentity();
  target.virtual_cartesian_inertia_inverse.setIdentity();
  target.tool_reference_wrench.setOnes();
  EXPECT_OK(generator.SetReference(target, DefaultCartesianLimits()));

  // Create an arbitrary initial state, which is not necessarily physically
  // meaningful.
  RealTimeCartesianTarget start = DefaultRealTimeCartesianTarget();
  CartStatePVA cart_state_sensed;
  cart_state_sensed.pose = Pose3d(start.task_t_tool_reference_orientation,
                                  start.task_t_tool_reference_position);
  cart_state_sensed.velocity = start.tool_reference_twist;
  cart_state_sensed.acceleration = start.tool_reference_acceleration;

  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
      RealTimeCartesianTarget current,
      generator.Evaluate(start, cart_state_sensed, kDefaultSpeedOverride));

  // Iterate until we expect convergence.
  for (auto i = static_cast<size_t>(
           round(kCommonMovementDurationSeconds / kCommonDtSeconds));
       i > 0; --i) {
    INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
        current,
        generator.Evaluate(current, cart_state_sensed, kDefaultSpeedOverride));

    if (i <= 2) {
      // We expect to be close to the target after iterating.
      EXPECT_THAT(
          current.task_t_tool_reference_position,
          IsApprox(target.task_t_tool_reference_position, kSmallTolerance));
      EXPECT_THAT(
          current.task_t_tool_reference_orientation,
          IsApprox(target.task_t_tool_reference_orientation, kSmallTolerance));
      EXPECT_THAT(current.tool_reference_twist,
                  IsApprox(target.tool_reference_twist, kSmallTolerance));
      EXPECT_THAT(
          current.tool_reference_acceleration,
          IsApprox(target.tool_reference_acceleration, kSmallTolerance));
    }

    cart_state_sensed.pose = Pose3d(current.task_t_tool_reference_orientation,
                                    current.task_t_tool_reference_position);
    cart_state_sensed.velocity = current.tool_reference_twist;
    cart_state_sensed.acceleration = current.tool_reference_acceleration;
  }
}

TEST(QuinticSplineCartesianImpedanceReferenceGeneratorTest,
     ConvergesToReference2) {
  // Now we choose a combination such that the remainder of the division
  // (kCustomMovementDurationSeconds / kCustomDtSeconds) is non-zero:
  const double kCustomDtSeconds = 0.4;
  const double kCustomMovementDurationSeconds = 1.0;
  QuinticSplineCartesianImpedanceReferenceGenerator generator(kCustomDtSeconds);
  EXPECT_OK(generator.Configure(
      cartesian_impedance::QuinticSplineCartesianGeneratorParameters{
          .movement_duration_seconds = kCustomMovementDurationSeconds}));
  RealTimeCartesianTarget target = DefaultRealTimeCartesianTarget();
  target.task_t_tool_reference_position.setConstant(1);
  target.task_t_tool_reference_orientation.coeffs() << 0, 1, 0, 0;
  target.cartesian_damping.setIdentity();
  target.cartesian_stiffness.setIdentity();
  target.virtual_cartesian_inertia_inverse.setIdentity();
  target.tool_reference_wrench.setOnes();
  EXPECT_OK(generator.SetReference(target, DefaultCartesianLimits()));

  // Create an arbitrary initial state, which is not necessarily physically
  // meaningful.
  RealTimeCartesianTarget start = DefaultRealTimeCartesianTarget();
  CartStatePVA cart_state_sensed;
  cart_state_sensed.pose = Pose3d(start.task_t_tool_reference_orientation,
                                  start.task_t_tool_reference_position);
  cart_state_sensed.velocity = start.tool_reference_twist;
  cart_state_sensed.acceleration = start.tool_reference_acceleration;

  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
      RealTimeCartesianTarget current,
      generator.Evaluate(start, cart_state_sensed, kDefaultSpeedOverride));

  // Iterate until we expect convergence.
  for (auto i = static_cast<size_t>(
           round(kCustomMovementDurationSeconds / kCustomDtSeconds));
       i > 0; --i) {
    INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
        current,
        generator.Evaluate(current, cart_state_sensed, kDefaultSpeedOverride));

    cart_state_sensed.pose = Pose3d(current.task_t_tool_reference_orientation,
                                    current.task_t_tool_reference_position);
    cart_state_sensed.velocity = current.tool_reference_twist;
    cart_state_sensed.acceleration = current.tool_reference_acceleration;
  }

  // We expect the generator to converge to the target.
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

TEST(QuinticSplineCartesianImpedanceReferenceGeneratorTest,
     ComparableValuesWithSpeedOverride) {
  // For this test, it is important to select `kTestSpeedOverride` to be a clean
  // fraction (in this case 1/4). Otherwise, the ticking logic below would break
  // down with very unhelpful errors.
  const double kTestSpeedOverride = 0.25;
  const int kNumComparisonTests = 1000;

  QuinticSplineCartesianImpedanceReferenceGenerator
      generator_with_default_speed_override(kCommonDtSeconds),
      generator_with_test_speed_override(kCommonDtSeconds);
  EXPECT_OK(generator_with_default_speed_override.Configure(
      DefaultCartesianGeneratorParameters()));
  EXPECT_OK(generator_with_test_speed_override.Configure(
      DefaultCartesianGeneratorParameters()));
  RealTimeCartesianTarget target = DefaultRealTimeCartesianTarget();
  target.task_t_tool_reference_position.setConstant(1);
  target.task_t_tool_reference_orientation.coeffs() << 0, 1, 0, 0;
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
  RealTimeCartesianTarget start = DefaultRealTimeCartesianTarget();
  CartStatePVA cart_state_sensed_with_default_speed_override;
  cart_state_sensed_with_default_speed_override.pose =
      Pose3d(start.task_t_tool_reference_orientation,
             start.task_t_tool_reference_position);
  cart_state_sensed_with_default_speed_override.velocity =
      start.tool_reference_twist;
  cart_state_sensed_with_default_speed_override.acceleration =
      start.tool_reference_acceleration;
  CartStatePVA cart_state_sensed_with_test_speed_override =
      cart_state_sensed_with_default_speed_override;

  RealTimeCartesianTarget current_with_default_speed_override = start;
  RealTimeCartesianTarget current_with_test_speed_override = start;

  // Iterate until we expect convergence.
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

TEST(QuinticSplineCartesianImpedanceReferenceGeneratorTest,
     SpeedOverrideIsInverselyProportionalToConvergenceTime) {
  const int kConvergenceMaxNumIteration = 200000;
  const double kTargetPosition = 1.0;
  const double kConvergenceThreshold = 0.999;
  const double kFractionDoubleSimilarityThreshold = 0.005;

  QuinticSplineCartesianImpedanceReferenceGenerator generator(kCommonDtSeconds);
  EXPECT_OK(generator.Configure(DefaultCartesianGeneratorParameters()));
  RealTimeCartesianTarget target = DefaultRealTimeCartesianTarget();
  target.task_t_tool_reference_position.setConstant(kTargetPosition);
  target.task_t_tool_reference_orientation.coeffs() << 0, 1, 0, 0;
  target.tool_reference_twist.setOnes();
  target.tool_reference_acceleration.setOnes();
  target.cartesian_damping.setIdentity();
  target.cartesian_stiffness.setIdentity();
  target.virtual_cartesian_inertia_inverse.setIdentity();
  target.tool_reference_wrench.setOnes();
  EXPECT_OK(generator.SetReference(target, DefaultCartesianLimits()));

  // Create an arbitrary initial state, which is not necessarily physically
  // meaningful.
  RealTimeCartesianTarget start = DefaultRealTimeCartesianTarget();
  CartStatePVA cart_state_sensed;
  cart_state_sensed.pose = Pose3d(start.task_t_tool_reference_orientation,
                                  start.task_t_tool_reference_position);
  cart_state_sensed.velocity = start.tool_reference_twist;
  cart_state_sensed.acceleration = start.tool_reference_acceleration;

  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
      RealTimeCartesianTarget current,
      generator.Evaluate(start, cart_state_sensed, kDefaultSpeedOverride));

  int num_iter_converged_with_default_speed_override = -1;
  // Iterate until we expect convergence for the reference generator with the
  // default speed override = 1.0.
  for (int i = 0; i < kConvergenceMaxNumIteration; i++) {
    INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
        current,
        generator.Evaluate(current, cart_state_sensed, kDefaultSpeedOverride));

    cart_state_sensed.pose = Pose3d(current.task_t_tool_reference_orientation,
                                    current.task_t_tool_reference_position);
    cart_state_sensed.velocity = current.tool_reference_twist;
    cart_state_sensed.acceleration = current.tool_reference_acceleration;

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

    // Reset the reference generator:
    EXPECT_OK(generator.SetReference(target, DefaultCartesianLimits()));

    // Reset state:
    cart_state_sensed.pose = Pose3d(start.task_t_tool_reference_orientation,
                                    start.task_t_tool_reference_position);
    cart_state_sensed.velocity = start.tool_reference_twist;
    cart_state_sensed.acceleration = start.tool_reference_acceleration;

    INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
        current, generator.Evaluate(start, cart_state_sensed, speed_override));

    int num_iter_converged_with_test_speed_override = -1;
    // Iterate until we expect convergence for the reference generator with the
    // test speed override value.
    for (int i = 0; i < kConvergenceMaxNumIteration; i++) {
      INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
          current,
          generator.Evaluate(current, cart_state_sensed, speed_override));

      cart_state_sensed.pose = Pose3d(current.task_t_tool_reference_orientation,
                                      current.task_t_tool_reference_position);
      cart_state_sensed.velocity = current.tool_reference_twist;
      cart_state_sensed.acceleration = current.tool_reference_acceleration;

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

TEST(QuinticSplineNullspaceReferenceGeneratorTest, ConfigureWorks) {
  QuinticSplineNullspaceReferenceGenerator generator(kCommonDtSeconds);
  cartesian_impedance::QuinticSplineNullspaceReferenceGeneratorParameters
      params;
  params.movement_duration_seconds = 22;
  EXPECT_OK(generator.Configure(params));
  EXPECT_EQ(generator.nominal_movement_duration_seconds(),
            params.movement_duration_seconds);
}

TEST(QuinticSplineNullspaceReferenceGeneratorTest,
     ConfigureErrorsForOutofRangeParameters) {
  QuinticSplineNullspaceReferenceGenerator generator(kCommonDtSeconds);
  cartesian_impedance::QuinticSplineNullspaceReferenceGeneratorParameters
      params;
  params.movement_duration_seconds = -0.01;
  EXPECT_THAT(generator.Configure(params),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       HasSubstr("must be >= 0.0")));
}

TEST(QuinticSplineNullspaceReferenceGeneratorTest, InvalidSpeedOverride) {
  QuinticSplineNullspaceReferenceGenerator generator(kCommonDtSeconds);
  RealTimeNullspaceTarget start;

  EXPECT_THAT(
      generator.Evaluate(start, -0.001),
      RealtimeStatusIs(absl::StatusCode::kInvalidArgument,
                       AllOf(HasSubstr("speed_override should be in range ["),
                             HasSubstr("], but instead has value -0.001."))));

  EXPECT_THAT(
      generator.Evaluate(start, 1.01),
      RealtimeStatusIs(absl::StatusCode::kInvalidArgument,
                       AllOf(HasSubstr("speed_override should be in range ["),
                             HasSubstr("], but instead has value 1.01."))));
}

TEST(QuinticSplineNullspaceReferenceGeneratorTest, ReturnsErrorIfUnconfigured) {
  QuinticSplineNullspaceReferenceGenerator generator(kCommonDtSeconds);

  RealTimeNullspaceTarget target = DefaultRealTimeNullspaceTarget();
  JointLimits limits = DefaultJointLimits();
  EXPECT_THAT(generator.SetReference(target, limits),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       HasSubstr("Movement duration not set")));

  EXPECT_THAT(generator.Evaluate(target, kDefaultSpeedOverride).status(),
              StatusIs(absl::StatusCode::kOutOfRange,
                       AllOf(HasSubstr("phase_to_go_ must be >= 0"),
                             HasSubstr("missing a call to SetReference()"))));
}

TEST(QuinticSplineNullspaceReferenceGeneratorTest,
     InitializationFailsIfReferenceViolatesLimits) {
  QuinticSplineNullspaceReferenceGenerator generator(kCommonDtSeconds);
  ASSERT_OK(generator.Configure(DefaultNullspaceGeneratorParameters()));
  RealTimeNullspaceTarget target = DefaultRealTimeNullspaceTarget();
  target.joint_state.position.setConstant(1000);
  intrinsic::JointLimits limits = DefaultJointLimits();
  // Target position is larger than max position in limits
  EXPECT_THAT(generator.SetReference(target, limits),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       HasSubstr("is not within limits")));
}

TEST(QuinticSplineNullspaceReferenceGeneratorTest,
     InitializationPassesForAdmissibleReference) {
  QuinticSplineNullspaceReferenceGenerator generator(kCommonDtSeconds);
  EXPECT_OK(generator.Configure(DefaultNullspaceGeneratorParameters()));
  RealTimeNullspaceTarget target = DefaultRealTimeNullspaceTarget();
  EXPECT_OK(generator.SetReference(target, DefaultJointLimits()));
}

TEST(QuinticSplineNullspaceReferenceGeneratorTest,
     ZeroMovementDurationSeconds) {
  QuinticSplineNullspaceReferenceGenerator generator(kCommonDtSeconds);
  EXPECT_OK(generator.Configure(
      cartesian_impedance::QuinticSplineNullspaceReferenceGeneratorParameters{
          .movement_duration_seconds = 0.0}));
  RealTimeNullspaceTarget target = DefaultRealTimeNullspaceTarget();
  target.joint_state.position.setOnes();
  EXPECT_OK(generator.SetReference(target, DefaultJointLimits()));

  // Create an arbitrary initial state, which is not necessarily physically
  // meaningful.
  RealTimeNullspaceTarget start = DefaultRealTimeNullspaceTarget();
  start.nullspace_damping = 1.0;
  start.nullspace_stiffness = -1.0;

  // The quantities that get interpolated are expected to be the same as the
  // target after one call to the reference generator.
  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
      RealTimeNullspaceTarget current,
      generator.Evaluate(start, kDefaultSpeedOverride));
  EXPECT_EQ(current.joint_state.position, target.joint_state.position);
  EXPECT_EQ(current.joint_state.velocity, target.joint_state.velocity);
  EXPECT_EQ(current.joint_state.acceleration, target.joint_state.acceleration);
  EXPECT_DOUBLE_EQ(current.nullspace_damping, target.nullspace_damping);
  EXPECT_DOUBLE_EQ(current.nullspace_stiffness, target.nullspace_stiffness);
}

TEST(QuinticSplineNullspaceReferenceGeneratorTest,
     NonInterpolatedQuantitiesJumpToReference) {
  QuinticSplineNullspaceReferenceGenerator generator(kCommonDtSeconds);
  EXPECT_OK(generator.Configure(DefaultNullspaceGeneratorParameters()));
  RealTimeNullspaceTarget target = DefaultRealTimeNullspaceTarget();
  target.joint_state.position.setOnes();
  EXPECT_OK(generator.SetReference(target, DefaultJointLimits()));

  // Create an arbitrary initial state, which is not necessarily physically
  // meaningful.
  RealTimeNullspaceTarget start = DefaultRealTimeNullspaceTarget();
  start.nullspace_damping = 1.0;
  start.nullspace_stiffness = -1.0;

  // The quantities that get interpolated are expected to be close to the start
  // after one call to the reference generator.
  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
      RealTimeNullspaceTarget current,
      generator.Evaluate(start, kDefaultSpeedOverride));
  EXPECT_THAT(current.joint_state.position,
              IsApprox(start.joint_state.position, kSmallTolerance));
  EXPECT_THAT(current.joint_state.velocity,
              IsApprox(start.joint_state.velocity, kSmallTolerance));
  EXPECT_THAT(current.joint_state.acceleration,
              IsApprox(start.joint_state.acceleration, kSmallTolerance));

  // The quantities that don't get interpolated are expected to "jump" to the
  // target.
  EXPECT_DOUBLE_EQ(current.nullspace_damping, target.nullspace_damping);
  EXPECT_DOUBLE_EQ(current.nullspace_stiffness, target.nullspace_stiffness);
}

TEST(QuinticSplineNullspaceReferenceGeneratorTest, HalfWayToReference) {
  QuinticSplineNullspaceReferenceGenerator generator(kCommonDtSeconds);
  EXPECT_OK(generator.Configure(DefaultNullspaceGeneratorParameters()));
  RealTimeNullspaceTarget target = DefaultRealTimeNullspaceTarget();
  target.joint_state.position.setOnes();
  EXPECT_OK(generator.SetReference(target, DefaultJointLimits()));

  // Create an arbitrary initial state, which is not necessarily physically
  // meaningful.
  RealTimeNullspaceTarget start = DefaultRealTimeNullspaceTarget();
  start.nullspace_damping = 1.0;
  start.nullspace_stiffness = -1.0;

  // The quantities that get interpolated are expected to be close to the start
  // after one call to the reference generator.
  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
      RealTimeNullspaceTarget current,
      generator.Evaluate(start, kDefaultSpeedOverride));

  // Iterate until we expect convergence.
  for (auto i = static_cast<size_t>(
           round(kCommonMovementDurationSeconds / kCommonDtSeconds));
       i >= static_cast<size_t>(
                round(kCommonMovementDurationSeconds / kCommonDtSeconds)) /
                2;
       --i) {
    INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
        current, generator.Evaluate(current, kDefaultSpeedOverride));
  }

  // We expect to reach half-way at this iteration.
  EXPECT_THAT(
      current.joint_state.position,
      IsApprox((start.joint_state.position + target.joint_state.position) / 2,
               kSmallTolerance));
}

TEST(QuinticSplineNullspaceReferenceGeneratorTest, ConvergesToReference1) {
  QuinticSplineNullspaceReferenceGenerator generator(kCommonDtSeconds);
  EXPECT_OK(generator.Configure(DefaultNullspaceGeneratorParameters()));
  RealTimeNullspaceTarget target = DefaultRealTimeNullspaceTarget();
  target.joint_state.position.setOnes();
  EXPECT_OK(generator.SetReference(target, DefaultJointLimits()));

  // Create an arbitrary initial state, which is not necessarily physically
  // meaningful.
  RealTimeNullspaceTarget start = DefaultRealTimeNullspaceTarget();
  start.nullspace_damping = 1.0;
  start.nullspace_stiffness = -1.0;

  // The quantities that get interpolated are expected to be close to the start
  // after one call to the reference generator.
  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
      RealTimeNullspaceTarget current,
      generator.Evaluate(start, kDefaultSpeedOverride));

  // Iterate until we expect convergence.
  for (auto i = static_cast<size_t>(
           round(kCommonMovementDurationSeconds / kCommonDtSeconds));
       i > 0; --i) {
    INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
        current, generator.Evaluate(current, kDefaultSpeedOverride));

    if (i <= 2) {
      // We expect to be close to the target after iterating.
      EXPECT_THAT(current.joint_state.position,
                  IsApprox(target.joint_state.position, kSmallTolerance));
      EXPECT_THAT(current.joint_state.velocity,
                  IsApprox(target.joint_state.velocity, kSmallTolerance));
      EXPECT_THAT(current.joint_state.acceleration,
                  IsApprox(target.joint_state.acceleration, kSmallTolerance));
    }
  }
}

TEST(QuinticSplineNullspaceReferenceGeneratorTest, ConvergesToReference2) {
  // Now we choose a combination such that the remainder of the division
  // (kCustomMovementDurationSeconds / kCustomDtSeconds) is non-zero:
  const double kCustomDtSeconds = 0.3;
  const double kCustomMovementDurationSeconds = 1.1;
  QuinticSplineNullspaceReferenceGenerator generator(kCustomDtSeconds);
  EXPECT_OK(generator.Configure(
      cartesian_impedance::QuinticSplineNullspaceReferenceGeneratorParameters{
          .movement_duration_seconds = kCustomMovementDurationSeconds}));
  RealTimeNullspaceTarget target = DefaultRealTimeNullspaceTarget();
  target.joint_state.position.setOnes();
  EXPECT_OK(generator.SetReference(target, DefaultJointLimits()));

  // Create an arbitrary initial state, which is not necessarily physically
  // meaningful.
  RealTimeNullspaceTarget start = DefaultRealTimeNullspaceTarget();
  start.nullspace_damping = 1.0;
  start.nullspace_stiffness = -1.0;

  // The quantities that get interpolated are expected to be close to the start
  // after one call to the reference generator.
  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
      RealTimeNullspaceTarget current,
      generator.Evaluate(start, kDefaultSpeedOverride));

  // Iterate until we expect convergence.
  for (auto i = static_cast<size_t>(
           round(kCustomMovementDurationSeconds / kCustomDtSeconds));
       i > 0; --i) {
    INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
        current, generator.Evaluate(current, kDefaultSpeedOverride));
  }

  // We expect the generator to converge to the target.
  EXPECT_THAT(current.joint_state.position,
              IsApprox(target.joint_state.position, kSmallTolerance));
  EXPECT_THAT(current.joint_state.velocity,
              IsApprox(target.joint_state.velocity, kSmallTolerance));
  EXPECT_THAT(current.joint_state.acceleration,
              IsApprox(target.joint_state.acceleration, kSmallTolerance));
}

TEST(QuinticSplineNullspaceReferenceGeneratorTest,
     ComparableValuesWithSpeedOverride) {
  // For this test, it is important to select `kTestSpeedOverride` to be a clean
  // fraction (in this case 1/5). Otherwise, the ticking logic below would break
  // down with very unhelpful errors.
  const double kTestSpeedOverride = 0.2;
  const int kNumComparisonTests = 1000;

  QuinticSplineNullspaceReferenceGenerator
      generator_with_default_speed_override(kCommonDtSeconds),
      generator_with_test_speed_override(kCommonDtSeconds);
  EXPECT_OK(generator_with_default_speed_override.Configure(
      DefaultNullspaceGeneratorParameters()));
  EXPECT_OK(generator_with_test_speed_override.Configure(
      DefaultNullspaceGeneratorParameters()));
  RealTimeNullspaceTarget target = DefaultRealTimeNullspaceTarget();
  target.joint_state.position.setOnes();
  EXPECT_OK(generator_with_default_speed_override.SetReference(
      target, DefaultJointLimits()));
  EXPECT_OK(generator_with_test_speed_override.SetReference(
      target, DefaultJointLimits()));

  // Create an arbitrary initial state, which is not necessarily physically
  // meaningful.
  RealTimeNullspaceTarget start = DefaultRealTimeNullspaceTarget();
  start.nullspace_damping = 1.0;
  start.nullspace_stiffness = -1.0;

  RealTimeNullspaceTarget current_with_default_speed_override = start;
  RealTimeNullspaceTarget current_with_test_speed_override = start;

  // Iterate until we expect convergence.
  for (size_t i = 0; i < kNumComparisonTests; i++) {
    // Advance the generator_with_default_speed_override one step ahead.
    INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
        current_with_default_speed_override,
        generator_with_default_speed_override.Evaluate(
            current_with_default_speed_override, kDefaultSpeedOverride));

    // Advance the generator_with_test_speed_override (1.0 / kTestSpeedOverride)
    // steps ahead, so that after these many steps the state will be comparable.
    for (size_t j = 0; j < static_cast<int>(1.0 / kTestSpeedOverride); j++) {
      INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
          current_with_test_speed_override,
          generator_with_test_speed_override.Evaluate(
              current_with_test_speed_override, kTestSpeedOverride));
    }

    // We expect that the positions for the one with the default speed
    // override versus the one with the test speed override to be close, while
    // the velocities and accelerations to be scaled accordingly after iterating
    // inversely-proportional to the speed override:
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

TEST(QuinticSplineNullspaceReferenceGeneratorTest,
     SpeedOverrideIsInverselyProportionalToConvergenceTime) {
  const int kConvergenceMaxNumIteration = 200000;
  const double kTargetPosition = 1.0;
  const double kConvergenceThreshold = 0.999;
  const double kFractionDoubleSimilarityThreshold = 0.005;

  QuinticSplineNullspaceReferenceGenerator generator(kCommonDtSeconds);
  EXPECT_OK(generator.Configure(DefaultNullspaceGeneratorParameters()));
  RealTimeNullspaceTarget target = DefaultRealTimeNullspaceTarget();
  target.joint_state.position.setConstant(kTargetPosition);
  target.joint_state.velocity.setOnes();
  target.joint_state.acceleration.setOnes();
  EXPECT_OK(generator.SetReference(target, DefaultJointLimits()));

  // Create an arbitrary initial state, which is not necessarily physically
  // meaningful.
  RealTimeNullspaceTarget start = DefaultRealTimeNullspaceTarget();
  start.nullspace_damping = 1.0;
  start.nullspace_stiffness = -1.0;

  // The quantities that get interpolated are expected to be close to the start
  // after one call to the reference generator.
  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
      RealTimeNullspaceTarget current,
      generator.Evaluate(start, kDefaultSpeedOverride));

  int num_iter_converged_with_default_speed_override = -1;
  // Iterate until we expect convergence for the reference generator with the
  // default speed override = 1.0.
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

    // Reset the reference generator:
    EXPECT_OK(generator.SetReference(target, DefaultJointLimits()));

    INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
        current, generator.Evaluate(start, speed_override));

    int num_iter_converged_with_test_speed_override = -1;
    // Iterate until we expect convergence for the reference generator with the
    // test speed override value.
    for (int i = 0; i < kConvergenceMaxNumIteration; i++) {
      INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
          current, generator.Evaluate(current, speed_override));

      // For the purpose of this test, we only check the 1st dimension of
      // position for convergence:
      if (current.joint_state.position[0] >=
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

}  // namespace
}  // namespace intrinsic::icon
