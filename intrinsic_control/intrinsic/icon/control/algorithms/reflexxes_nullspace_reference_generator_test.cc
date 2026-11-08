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

#include "intrinsic/icon/control/algorithms/reflexxes_nullspace_reference_generator.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstddef>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/strings/str_cat.h"
#include "internal/testing.h"
#include "intrinsic/eigenmath/matchers.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/control/algorithms/cartesian_impedance_commons.h"
#include "intrinsic/icon/utils/realtime_status_macro.h"
#include "intrinsic/icon/utils/realtime_status_matchers.h"
#include "intrinsic/kinematics/types/joint_limits.h"
#include "intrinsic/kinematics/types/joint_state.h"
#include "intrinsic/math/ipow.h"

namespace intrinsic::icon {
namespace {

using ::absl_testing::StatusIs;
using eigenmath::testing::IsApprox;
using ::testing::DoubleNear;
using ::testing::HasSubstr;

constexpr double kMinPos = -3;
constexpr double kMaxPos = 3;
constexpr double kMaxVel = 2;
constexpr double kMaxAcc = 2;
constexpr double kMaxJer = 2;

constexpr double kFrequencyHz = 100;
constexpr double kSmallTolerance = 0.05;
constexpr int kNumJoints = 7;
constexpr double kDefaultSpeedOverride = 1.0;

JointLimits DefaultJointLimits() {
  INTRINSIC_RT_ASSIGN_OR_DIE(auto limits, JointLimits::Unlimited(kNumJoints));
  limits.min_position.setConstant(kMinPos);
  limits.max_position.setConstant(kMaxPos);
  limits.max_velocity.setConstant(kMaxVel);
  limits.max_acceleration.setConstant(kMaxAcc);
  limits.max_jerk.setConstant(kMaxJer);

  return limits;
}

TEST(ReflexxesNullspaceReferenceGeneratorTest,
     InitializationFailsIfReferenceViolatesLimits) {
  ReflexxesNullspaceReferenceGenerator generator(kNumJoints, kFrequencyHz);
  cartesian_impedance::RealTimeNullspaceTarget target;
  ASSERT_OK(target.joint_state.SetSize(kNumJoints));
  target.joint_state.position.setConstant(1000);
  target.joint_state.velocity.setConstant(1000);
  target.joint_state.acceleration.setConstant(1000);
  EXPECT_THAT(generator.SetReference(target, DefaultJointLimits()),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       HasSubstr("is not within limits")));
}

TEST(ReflexxesNullspaceReferenceGeneratorTest,
     InitializationPassesForAdmissibleReference) {
  ReflexxesNullspaceReferenceGenerator generator(kNumJoints, kFrequencyHz);
  cartesian_impedance::RealTimeNullspaceTarget target;
  ASSERT_OK(target.joint_state.SetSize(kNumJoints));
  target.joint_state.position.setZero();
  target.joint_state.velocity.setZero();
  target.joint_state.acceleration.setZero();
  EXPECT_OK(generator.SetReference(target, DefaultJointLimits()));
}

TEST(ReflexxesNullspaceReferenceGeneratorTest, InvalidSpeedOverride) {
  ReflexxesNullspaceReferenceGenerator generator(kNumJoints, kFrequencyHz);
  cartesian_impedance::RealTimeNullspaceTarget start;

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

TEST(ReflexxesNullspaceReferenceGeneratorTest, ConvergesToReference) {
  ReflexxesNullspaceReferenceGenerator generator(kNumJoints, kFrequencyHz);
  cartesian_impedance::RealTimeNullspaceTarget target;
  ASSERT_OK(target.joint_state.SetSize(kNumJoints));
  target.joint_state.position.setOnes();
  target.joint_state.velocity.setZero();
  target.joint_state.acceleration.setZero();
  target.nullspace_damping = 0.0;
  target.nullspace_stiffness = 0.0;
  EXPECT_OK(generator.SetReference(target, DefaultJointLimits()));

  // Create an arbitrary initial  state, which is not necessarily physically
  // meaningful.
  cartesian_impedance::RealTimeNullspaceTarget start;
  ASSERT_OK(start.joint_state.SetSize(kNumJoints));
  start.joint_state.position.setZero();
  start.joint_state.velocity.setZero();
  start.joint_state.acceleration.setZero();
  start.nullspace_damping = 1.0;
  start.nullspace_stiffness = -1.0;

  // The quantities that get interpolated are expected to be close to the start
  // after one call to the reference generator.
  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
      cartesian_impedance::RealTimeNullspaceTarget current,
      generator.Evaluate(start, kDefaultSpeedOverride));
  EXPECT_THAT(current.joint_state.position,
              IsApprox(start.joint_state.position, kSmallTolerance));
  EXPECT_THAT(current.joint_state.velocity,
              IsApprox(start.joint_state.velocity, kSmallTolerance));
  EXPECT_THAT(current.joint_state.acceleration,
              IsApprox(start.joint_state.acceleration, kSmallTolerance));

  // The quantities that don't get interpolated are expected to "jump" to the
  // target.
  EXPECT_NEAR(current.nullspace_damping, target.nullspace_damping, 1e-12);
  EXPECT_NEAR(current.nullspace_stiffness, target.nullspace_stiffness, 1e-12);

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
}

TEST(ReflexxesNullspaceReferenceGeneratorTest,
     ComparableValuesWithSpeedOverride) {
  // For this test, it is important to select `kTestSpeedOverride` to be a clean
  // fraction (in this case 1/4). Otherwise, the ticking logic below would break
  // down with very unhelpful errors.
  const double kTestSpeedOverride = 0.25;
  const int kNumComparisonTests = 1000;

  ReflexxesNullspaceReferenceGenerator generator_with_default_speed_override(
      kNumJoints, kFrequencyHz),
      generator_with_test_speed_override(kNumJoints, kFrequencyHz);
  cartesian_impedance::RealTimeNullspaceTarget target;
  ASSERT_OK(target.joint_state.SetSize(kNumJoints));
  target.joint_state.position.setOnes();
  target.joint_state.velocity.setZero();
  target.joint_state.acceleration.setZero();
  target.nullspace_damping = 0.0;
  target.nullspace_stiffness = 0.0;
  EXPECT_OK(generator_with_default_speed_override.SetReference(
      target, DefaultJointLimits()));
  EXPECT_OK(generator_with_test_speed_override.SetReference(
      target, DefaultJointLimits()));

  // Create an arbitrary initial  state, which is not necessarily physically
  // meaningful.
  cartesian_impedance::RealTimeNullspaceTarget start;
  ASSERT_OK(start.joint_state.SetSize(kNumJoints));
  start.joint_state.position.setZero();
  start.joint_state.velocity.setZero();
  start.joint_state.acceleration.setZero();
  start.nullspace_damping = 1.0;
  start.nullspace_stiffness = -1.0;

  cartesian_impedance::RealTimeNullspaceTarget
      current_with_default_speed_override = start;
  cartesian_impedance::RealTimeNullspaceTarget
      current_with_test_speed_override = start;

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
  }
}

TEST(ReflexxesNullspaceReferenceGeneratorTest,
     SpeedOverrideIsInverselyProportionalToConvergenceTime) {
  const int kConvergenceMaxNumIteration = 200000;
  const double kTargetPosition = 1.0;
  const double kConvergenceThreshold = 0.999;
  const double kFractionDoubleSimilarityThreshold = 0.0075;

  ReflexxesNullspaceReferenceGenerator generator(kNumJoints, kFrequencyHz);
  cartesian_impedance::RealTimeNullspaceTarget target;
  ASSERT_OK(target.joint_state.SetSize(kNumJoints));
  target.joint_state.position.setOnes();
  target.joint_state.velocity.setZero();
  target.joint_state.acceleration.setZero();
  target.nullspace_damping = 0.0;
  target.nullspace_stiffness = 0.0;
  EXPECT_OK(generator.SetReference(target, DefaultJointLimits()));

  // Create an arbitrary initial  state, which is not necessarily physically
  // meaningful.
  cartesian_impedance::RealTimeNullspaceTarget start;
  ASSERT_OK(start.joint_state.SetSize(kNumJoints));
  start.joint_state.position.setZero();
  start.joint_state.velocity.setZero();
  start.joint_state.acceleration.setZero();
  start.nullspace_damping = 1.0;
  start.nullspace_stiffness = -1.0;

  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
      cartesian_impedance::RealTimeNullspaceTarget current,
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
