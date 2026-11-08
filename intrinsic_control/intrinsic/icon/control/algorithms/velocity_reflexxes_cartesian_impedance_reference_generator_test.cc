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

#include "intrinsic/icon/control/algorithms/velocity_reflexxes_cartesian_impedance_reference_generator.h"

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
#include "intrinsic/icon/control/algorithms/cartesian_impedance_commons.h"
#include "intrinsic/icon/testing/malloc_test.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/icon/utils/realtime_status_matchers.h"
#include "intrinsic/icon/utils/realtime_status_or.h"
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

constexpr double kMinPos = -3;
constexpr double kMaxPos = 3;
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

TEST(VelocityReflexxesCartesianImpedanceReferenceGeneratorTest,
     InitializationFailsIfReferenceViolatesLimits) {
  VelocityReflexxesCartesianImpedanceReferenceGenerator generator(kFrequencyHz);
  cartesian_impedance::RealTimeCartesianTarget target;
  target.tool_reference_twist.setConstant(1000);
  target.task_t_tool_reference_orientation.setIdentity();
  EXPECT_THAT(generator.SetReference(target, DefaultCartesianLimits()),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       HasSubstr("is not within limits")));
}

TEST(VelocityReflexxesCartesianImpedanceReferenceGeneratorTest,
     InitializationPassesForAdmissibleReference) {
  VelocityReflexxesCartesianImpedanceReferenceGenerator generator(kFrequencyHz);
  cartesian_impedance::RealTimeCartesianTarget target;
  target.task_t_tool_reference_position.setZero();
  target.task_t_tool_reference_orientation.setIdentity();
  EXPECT_OK(generator.SetReference(target, DefaultCartesianLimits()));
}

TEST(VelocityReflexxesCartesianImpedanceReferenceGeneratorTest, DoesNotMalloc) {
  VelocityReflexxesCartesianImpedanceReferenceGenerator generator(kFrequencyHz);
  cartesian_impedance::RealTimeCartesianTarget target;
  target.task_t_tool_reference_position.setZero();
  target.task_t_tool_reference_orientation.setIdentity();
  CartStatePVA cart_state_sensed;
  cart_state_sensed.pose = Pose3d(target.task_t_tool_reference_orientation,
                                  target.task_t_tool_reference_position);
  cart_state_sensed.velocity = target.tool_reference_twist;
  cart_state_sensed.acceleration = target.tool_reference_acceleration;

  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  RealtimeStatus s = generator.SetReference(target, DefaultCartesianLimits());
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
  EXPECT_THAT(s, RealtimeIsOk());

  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  auto current =
      generator.Evaluate(target, cart_state_sensed, kDefaultSpeedOverride);
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
  EXPECT_THAT(current.status(), RealtimeIsOk());
}

TEST(VelocityReflexxesCartesianImpedanceReferenceGeneratorTest,
     InvalidSpeedOverride) {
  VelocityReflexxesCartesianImpedanceReferenceGenerator generator(kFrequencyHz);
  cartesian_impedance::RealTimeCartesianTarget target;
  CartStatePVA cart_state_sensed;

  EXPECT_THAT(
      generator.Evaluate(target, cart_state_sensed, -0.001),
      RealtimeStatusIs(absl::StatusCode::kInvalidArgument,
                       AllOf(HasSubstr("speed_override should be in range ["),
                             HasSubstr("], but instead has value -0.001."))));

  EXPECT_THAT(
      generator.Evaluate(target, cart_state_sensed, 1.01),
      RealtimeStatusIs(absl::StatusCode::kInvalidArgument,
                       AllOf(HasSubstr("speed_override should be in range ["),
                             HasSubstr("], but instead has value 1.01."))));
}

TEST(VelocityReflexxesCartesianImpedanceReferenceGeneratorTest,
     ConvergesToReferenceInRotation) {
  VelocityReflexxesCartesianImpedanceReferenceGenerator generator(kFrequencyHz);
  cartesian_impedance::RealTimeCartesianTarget target;
  target.task_t_tool_reference_position.setConstant(0);
  target.task_t_tool_reference_orientation.setIdentity();
  target.cartesian_damping.setIdentity();
  target.cartesian_stiffness.setIdentity();
  target.virtual_cartesian_inertia_inverse.setIdentity();
  target.tool_reference_twist.setZero();
  target.tool_reference_twist.RX() =
      0.1;  // Assume small rotation around X-axis.
  target.tool_reference_acceleration.setZero();
  target.tool_reference_wrench.setZero();
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

  // Iterate until we expect convergence to the given reference velocity.
  const int kNumSteps = 1000;
  for (size_t i = 0; i < kNumSteps; i++) {
    INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
        current,
        generator.Evaluate(current, cart_state_sensed, kDefaultSpeedOverride));

    // Assume perfect realization of the command: step forward "measured" state
    // with explicit Euler integration, transcribe velocity and acceleration.
    eigenmath::Quaterniond orientation_increment = eigenmath::QuaternionFromRPY(
        current.tool_reference_twist(3) / kFrequencyHz,
        current.tool_reference_twist(4) / kFrequencyHz,
        current.tool_reference_twist(5) / kFrequencyHz);
    cart_state_sensed.pose =
        Pose3d(cart_state_sensed.pose.quaternion() * orientation_increment,
               cart_state_sensed.pose.translation() +
                   current.tool_reference_twist.head<3>() / kFrequencyHz);
    cart_state_sensed.velocity = current.tool_reference_twist;
    cart_state_sensed.acceleration = current.tool_reference_acceleration;
  }

  // We expect to be close to the (twist) target after iterating for kNumSteps.
  EXPECT_THAT(current.task_t_tool_reference_position,
              IsApprox(target.task_t_tool_reference_position, kSmallTolerance));
  eigenmath::Quaterniond expected_orientation = eigenmath::QuaternionFromRPY(
      target.tool_reference_twist(3) *
          (static_cast<double>(kNumSteps) / kFrequencyHz),
      target.tool_reference_twist(4) *
          (static_cast<double>(kNumSteps) / kFrequencyHz),
      target.tool_reference_twist(5) *
          (static_cast<double>(kNumSteps) / kFrequencyHz));
  EXPECT_THAT(current.task_t_tool_reference_orientation,
              IsApprox(expected_orientation, kSmallTolerance))
      << " expected RPY: "
      << target.tool_reference_twist(3) *
             (static_cast<double>(kNumSteps) / kFrequencyHz)
      << ", "
      << target.tool_reference_twist(4) *
             (static_cast<double>(kNumSteps) / kFrequencyHz)
      << ", "
      << target.tool_reference_twist(5) *
             (static_cast<double>(kNumSteps) / kFrequencyHz);
  EXPECT_THAT(current.tool_reference_twist,
              IsApprox(target.tool_reference_twist, kSmallTolerance));
  EXPECT_THAT(current.tool_reference_acceleration,
              IsApprox(target.tool_reference_acceleration, kSmallTolerance));
}

TEST(VelocityReflexxesCartesianImpedanceReferenceGeneratorTest,
     ConvergesToReferenceInTranslation) {
  VelocityReflexxesCartesianImpedanceReferenceGenerator generator(kFrequencyHz);
  cartesian_impedance::RealTimeCartesianTarget target;
  target.task_t_tool_reference_position.setConstant(0);
  target.task_t_tool_reference_orientation.setIdentity();
  target.cartesian_damping.setIdentity();
  target.cartesian_stiffness.setIdentity();
  target.virtual_cartesian_inertia_inverse.setIdentity();
  target.tool_reference_twist.setZero();
  target.tool_reference_twist.x() =
      0.1;  // Assume small translation along X-axis.
  target.tool_reference_acceleration.setZero();
  target.tool_reference_wrench.setZero();
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

  // Iterate until we expect convergence to the given reference velocity.
  const int kNumSteps = 1000;
  for (size_t i = 0; i < kNumSteps; i++) {
    INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
        current,
        generator.Evaluate(current, cart_state_sensed, kDefaultSpeedOverride));

    // Assume perfect realization of the command: step forward "measured" state
    // with explicit Euler integration, transcribe velocity and acceleration.
    cart_state_sensed.pose =
        Pose3d(cart_state_sensed.pose.quaternion(),
               cart_state_sensed.pose.translation() +
                   current.tool_reference_twist.head<3>() / kFrequencyHz);
    cart_state_sensed.velocity = current.tool_reference_twist;
    cart_state_sensed.acceleration = current.tool_reference_acceleration;
  }

  // We expect to be close to the (twist and acceleration) target after
  // iterating for kNumSteps. We expected the position to have been integrated
  // correspondingly.
  EXPECT_THAT(current.task_t_tool_reference_position,
              IsApprox(cart_state_sensed.pose.translation(), kSmallTolerance));
  EXPECT_THAT(current.tool_reference_twist,
              IsApprox(target.tool_reference_twist, kSmallTolerance));
  EXPECT_THAT(current.tool_reference_acceleration,
              IsApprox(target.tool_reference_acceleration, kSmallTolerance));

  // Continue iterating but do not update the sensed pose, set sensed velocity
  // to zero, as if we were in contact:
  cart_state_sensed.velocity.setZero();
  cart_state_sensed.acceleration.setZero();
  for (size_t i = 0; i < kNumSteps; i++) {
    INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
        current,
        generator.Evaluate(current, cart_state_sensed, kDefaultSpeedOverride));
  }

  // The resulting reference pose should be close to the previously sensed
  // state, velocity and acceleration should be zero.
  EXPECT_THAT(current.task_t_tool_reference_position,
              IsApprox(cart_state_sensed.pose.translation(), kSmallTolerance));
  EXPECT_THAT(current.tool_reference_twist,
              IsApprox(Twist::ZERO, kSmallTolerance));
  EXPECT_THAT(current.tool_reference_acceleration,
              IsApprox(Acceleration::ZERO, kSmallTolerance));
}

TEST(VelocityReflexxesCartesianImpedanceReferenceGeneratorTest,
     ComparableValuesWithSpeedOverrideInRotation) {
  const double kTestSpeedOverride = 0.25;
  const int kNumComparisonTests = 1000;

  VelocityReflexxesCartesianImpedanceReferenceGenerator
      generator_with_default_speed_override(kFrequencyHz),
      generator_with_test_speed_override(kFrequencyHz);

  cartesian_impedance::RealTimeCartesianTarget target;
  target.task_t_tool_reference_position.setConstant(0);
  target.task_t_tool_reference_orientation.setIdentity();
  target.cartesian_damping.setIdentity();
  target.cartesian_stiffness.setIdentity();
  target.virtual_cartesian_inertia_inverse.setIdentity();
  target.tool_reference_twist.setZero();
  target.tool_reference_twist.RX() =
      0.1;  // Assume small rotation around X-axis.
  target.tool_reference_acceleration.setZero();
  target.tool_reference_wrench.setZero();
  EXPECT_OK(generator_with_default_speed_override.SetReference(
      target, DefaultCartesianLimits()));
  EXPECT_OK(generator_with_test_speed_override.SetReference(
      target, DefaultCartesianLimits()));

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
    INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
        current_with_default_speed_override,
        generator_with_default_speed_override.Evaluate(
            current_with_default_speed_override,
            cart_state_sensed_with_default_speed_override,
            kDefaultSpeedOverride));

    // Assume perfect realization of the command: step forward "measured" state
    // with explicit Euler integration, transcribe velocity and acceleration.
    eigenmath::Quaterniond orientation_increment_with_default_speed_override =
        eigenmath::QuaternionFromRPY(
            current_with_default_speed_override.tool_reference_twist(3) /
                kFrequencyHz,
            current_with_default_speed_override.tool_reference_twist(4) /
                kFrequencyHz,
            current_with_default_speed_override.tool_reference_twist(5) /
                kFrequencyHz);
    cart_state_sensed_with_default_speed_override.pose = Pose3d(
        cart_state_sensed_with_default_speed_override.pose.quaternion() *
            orientation_increment_with_default_speed_override,
        cart_state_sensed_with_default_speed_override.pose.translation() +
            current_with_default_speed_override.tool_reference_twist.head<3>() /
                kFrequencyHz);
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

      // Assume perfect realization of the command: step forward "measured"
      // state with explicit Euler integration, transcribe velocity and
      // acceleration.
      eigenmath::Quaterniond orientation_increment_with_test_speed_override =
          eigenmath::QuaternionFromRPY(
              current_with_test_speed_override.tool_reference_twist(3) /
                  kFrequencyHz,
              current_with_test_speed_override.tool_reference_twist(4) /
                  kFrequencyHz,
              current_with_test_speed_override.tool_reference_twist(5) /
                  kFrequencyHz);
      cart_state_sensed_with_test_speed_override.pose = Pose3d(
          cart_state_sensed_with_test_speed_override.pose.quaternion() *
              orientation_increment_with_test_speed_override,
          cart_state_sensed_with_test_speed_override.pose.translation() +
              current_with_test_speed_override.tool_reference_twist.head<3>() /
                  kFrequencyHz);
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

TEST(VelocityReflexxesCartesianImpedanceReferenceGeneratorTest,
     ComparableValuesWithSpeedOverrideInTranslation) {
  const double kTestSpeedOverride = 0.2;
  const int kNumComparisonTests = 1000;

  VelocityReflexxesCartesianImpedanceReferenceGenerator
      generator_with_default_speed_override(kFrequencyHz),
      generator_with_test_speed_override(kFrequencyHz);

  cartesian_impedance::RealTimeCartesianTarget target;
  target.task_t_tool_reference_position.setConstant(0);
  target.task_t_tool_reference_orientation.setIdentity();
  target.cartesian_damping.setIdentity();
  target.cartesian_stiffness.setIdentity();
  target.virtual_cartesian_inertia_inverse.setIdentity();
  target.tool_reference_twist.setZero();
  target.tool_reference_twist.x() =
      0.1;  // Assume small translation along X-axis.
  target.tool_reference_acceleration.setZero();
  target.tool_reference_wrench.setZero();
  EXPECT_OK(generator_with_default_speed_override.SetReference(
      target, DefaultCartesianLimits()));
  EXPECT_OK(generator_with_test_speed_override.SetReference(
      target, DefaultCartesianLimits()));

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
    INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
        current_with_default_speed_override,
        generator_with_default_speed_override.Evaluate(
            current_with_default_speed_override,
            cart_state_sensed_with_default_speed_override,
            kDefaultSpeedOverride));

    // Assume perfect realization of the command: step forward "measured" state
    // with explicit Euler integration, transcribe velocity and acceleration.
    cart_state_sensed_with_default_speed_override.pose = Pose3d(
        cart_state_sensed_with_default_speed_override.pose.quaternion(),
        cart_state_sensed_with_default_speed_override.pose.translation() +
            current_with_default_speed_override.tool_reference_twist.head<3>() /
                kFrequencyHz);
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

      // Assume perfect realization of the command: step forward "measured"
      // state with explicit Euler integration, transcribe velocity and
      // acceleration.
      cart_state_sensed_with_test_speed_override.pose = Pose3d(
          cart_state_sensed_with_test_speed_override.pose.quaternion(),
          cart_state_sensed_with_test_speed_override.pose.translation() +
              current_with_test_speed_override.tool_reference_twist.head<3>() /
                  kFrequencyHz);
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

  // For the case of the generator with the test speed override: continue
  // iterating but do not update the sensed pose, set sensed velocity to zero,
  // as if we were in contact:
  cart_state_sensed_with_test_speed_override.velocity.setZero();
  cart_state_sensed_with_test_speed_override.acceleration.setZero();
  const int kNumSteps = 1000;
  for (size_t i = 0; i < kNumSteps; i++) {
    INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
        current_with_test_speed_override,
        generator_with_test_speed_override.Evaluate(
            current_with_test_speed_override,
            cart_state_sensed_with_test_speed_override, kTestSpeedOverride));
  }

  // The resulting reference pose should be close to the previously sensed
  // state, velocity and acceleration should be zero.
  EXPECT_THAT(
      current_with_test_speed_override.task_t_tool_reference_position,
      IsApprox(cart_state_sensed_with_test_speed_override.pose.translation(),
               kSmallTolerance));
  EXPECT_THAT(current_with_test_speed_override.tool_reference_twist,
              IsApprox(Twist::ZERO, kSmallTolerance));
  EXPECT_THAT(current_with_test_speed_override.tool_reference_acceleration,
              IsApprox(Acceleration::ZERO, kSmallTolerance));
}

TEST(VelocityReflexxesCartesianImpedanceReferenceGeneratorTest,
     SpeedOverrideIsInverselyProportionalToConvergenceTimeInTranslation) {
  const int kConvergenceMaxNumIteration = 200000;
  const double kConvergenceThreshold = 0.999;
  const double kFractionDoubleSimilarityThreshold = 0.02;

  VelocityReflexxesCartesianImpedanceReferenceGenerator generator(kFrequencyHz);
  cartesian_impedance::RealTimeCartesianTarget target;
  target.task_t_tool_reference_position.setConstant(0);
  target.task_t_tool_reference_orientation.setIdentity();
  target.cartesian_damping.setIdentity();
  target.cartesian_stiffness.setIdentity();
  target.virtual_cartesian_inertia_inverse.setIdentity();
  target.tool_reference_twist.setZero();
  target.tool_reference_twist.x() =
      0.1;  // Assume small translation along X-axis.
  target.tool_reference_acceleration.setZero();
  target.tool_reference_wrench.setZero();
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

  int num_iter_converged_with_default_speed_override = -1;
  // Iterate until we expect convergence for the reference generator with the
  // default speed override = 1.0.
  for (int i = 0; i < kConvergenceMaxNumIteration; i++) {
    INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
        current,
        generator.Evaluate(current, cart_state_sensed, kDefaultSpeedOverride));

    // Assume perfect realization of the command: step forward "measured" state
    // with explicit Euler integration, transcribe velocity and acceleration.
    cart_state_sensed.pose =
        Pose3d(cart_state_sensed.pose.quaternion(),
               cart_state_sensed.pose.translation() +
                   current.tool_reference_twist.head<3>() / kFrequencyHz);
    cart_state_sensed.velocity = current.tool_reference_twist;
    cart_state_sensed.acceleration = current.tool_reference_acceleration;

    // For the purpose of this test, we only check the twist for convergence:
    if ((target.tool_reference_twist - current.tool_reference_twist).norm() <
        (1.0 - kConvergenceThreshold) *
            (target.tool_reference_twist - start.tool_reference_twist).norm()) {
      num_iter_converged_with_default_speed_override = i;
      break;
    }
  }

  for (double speed_override : {0.0, 0.25, 0.50, 0.75, 1.00}) {
    SCOPED_TRACE(absl::StrCat("Speed override ", speed_override));

    // Reset state:
    cart_state_sensed.pose = Pose3d(start.task_t_tool_reference_orientation,
                                    start.task_t_tool_reference_position);
    cart_state_sensed.velocity = start.tool_reference_twist;
    cart_state_sensed.acceleration = start.tool_reference_acceleration;

    INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
        current, generator.Evaluate(start, cart_state_sensed, speed_override));

    int num_iter_converged_with_test_speed_override = -1;
    // Iterate until we expect convergence.
    for (int i = 0; i < kConvergenceMaxNumIteration; i++) {
      INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
          current,
          generator.Evaluate(current, cart_state_sensed, speed_override));

      // Assume perfect realization of the command: step forward "measured"
      // state with explicit Euler integration, transcribe velocity and
      // acceleration.
      cart_state_sensed.pose =
          Pose3d(cart_state_sensed.pose.quaternion(),
                 cart_state_sensed.pose.translation() +
                     current.tool_reference_twist.head<3>() / kFrequencyHz);
      cart_state_sensed.velocity = current.tool_reference_twist;
      cart_state_sensed.acceleration = current.tool_reference_acceleration;

      if (speed_override == 0.0) {
        // When the speed override is 0, we expect the current twist to stay
        // zero throughout.
        EXPECT_THAT(current.tool_reference_twist,
                    IsApprox(Twist::ZERO, kSmallTolerance));
      } else {
        // For the purpose of this test, we only check the twist for
        // convergence:
        if ((target.tool_reference_twist * speed_override -
             current.tool_reference_twist)
                .norm() <
            (1.0 - kConvergenceThreshold) *
                (target.tool_reference_twist - start.tool_reference_twist)
                    .norm()) {
          num_iter_converged_with_test_speed_override = i;
          break;
        }
      }
    }

    if (speed_override > 0.0) {
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
