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

#include "intrinsic/icon/control/algorithms/joint_limit_checker.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <array>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/time/time.h"
#include "internal/testing.h"
#include "intrinsic/eigenmath/matchers.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/testing/malloc_test.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/icon/utils/realtime_status_matchers.h"
#include "intrinsic/kinematics/types/joint_limits.h"
#include "intrinsic/kinematics/types/joint_state.h"

namespace intrinsic::icon {

namespace {

using ::intrinsic::eigenmath::testing::IsApprox;
using ::testing::HasSubstr;

constexpr int kNdof = 2;
constexpr double kMaxPosition = 100.0;
constexpr double kMinPosition = -8.0;
constexpr double kMaxVelocity = 20.0;
constexpr double kMaxAcceleration = 6.0;
constexpr double kMaxJerk = 4.0;
constexpr absl::Duration kCycleDuration = absl::Seconds(1);

JointLimits DefaultLimits() {
  JointLimits joint_limits;
  EXPECT_OK(joint_limits.SetSize(kNdof));
  joint_limits.max_position.setConstant(kMaxPosition);
  joint_limits.min_position.setConstant(kMinPosition);
  joint_limits.max_velocity.setConstant(kMaxVelocity);
  joint_limits.max_acceleration.setConstant(kMaxAcceleration);
  joint_limits.max_jerk.setConstant(kMaxJerk);
  return joint_limits;
}

TEST(JointLimitCheckerTest, PassesForValidTrajectory) {
  JointLimits joint_limits = DefaultLimits();
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  JointLimitChecker joint_limit_checker(kCycleDuration, kNdof);
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
  std::vector<std::array<double, kNdof>> positions{
      {0, 0},    //  v1=0 a1=0 j1=0, v2=0 a2=0 j2=0
      {0, 0},    //  v1=0 a1=0 j1=0, v2=0 a2=0 j2=0
      {0, 0.1},  //  v1=0 a1=0 j1=0, v2=0.1 a2=0.1 j2=0.1
      {0, 0.2},  //  v1=0 a1=0 j1=0, v2=0.1 a2=0.0 j2=-0.1
  };

  for (const auto& position : positions) {
    IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
    INTRINSIC_RT_EXPECT_OK(joint_limit_checker.Check(position, joint_limits));
    IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
  }
}

TEST(JointLimitCheckerTest, PassesForValidNonZeroConstantTrajectory) {
  JointLimits joint_limits = DefaultLimits();
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  JointLimitChecker joint_limit_checker(kCycleDuration, kNdof);
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
  std::vector<std::array<double, kNdof>> positions{
      {100, 100},
      {100, 100},
      {100, 100},
      {100, 100},
  };

  for (const auto& position : positions) {
    IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
    INTRINSIC_RT_EXPECT_OK(joint_limit_checker.Check(position, joint_limits));
    IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
  }
}

TEST(JointLimitCheckerTest, FailsForJerkViolation) {
  JointLimits joint_limits = DefaultLimits();

  JointLimitChecker joint_limit_checker(kCycleDuration, kNdof);

  // Initialize the internal state.
  for (int i = 0; i < 3; ++i) {
    IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
    RealtimeStatus status = joint_limit_checker.Check({0.0, 0.0}, joint_limits);
    IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
    EXPECT_THAT(status, RealtimeIsOk());
  }

  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  RealtimeStatus status =
      joint_limit_checker.Check({0.0, kMaxJerk + 0.001}, joint_limits);
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();

  EXPECT_THAT(status,
              RealtimeStatusIs(absl::StatusCode::kInternal, HasSubstr("[j]")));
}

TEST(JointLimitCheckerTest, FailsForAccelerationViolation) {
  JointLimits joint_limits = DefaultLimits();

  JointLimitChecker joint_limit_checker(kCycleDuration, kNdof);

  // Initialize the internal state.
  for (int i = 0; i < 2; ++i) {
    IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
    RealtimeStatus status = joint_limit_checker.Check({0.0, 0.0}, joint_limits);
    IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
    EXPECT_THAT(status, RealtimeIsOk());
  }

  {
    IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
    RealtimeStatus status =
        joint_limit_checker.Check({0.0, kMaxJerk}, joint_limits);
    IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
    EXPECT_OK(status);
  }
  {
    IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
    RealtimeStatus status =
        joint_limit_checker.Check({0.0, 4.0 * kMaxJerk}, joint_limits);
    IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
    EXPECT_THAT(status, RealtimeStatusIs(absl::StatusCode::kInternal,
                                         HasSubstr("[a]")));
  }
}

TEST(JointLimitCheckerTest, FailsForVelocityViolation) {
  JointLimits joint_limits = DefaultLimits();

  JointLimitChecker joint_limit_checker(kCycleDuration, kNdof);

  // Initialize the internal state.

  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  RealtimeStatus status = joint_limit_checker.Check({0.0, 0.0}, joint_limits);
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
  EXPECT_THAT(status, RealtimeIsOk());

  {
    IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
    RealtimeStatus status =
        joint_limit_checker.Check({0.0, 1.1 * kMaxVelocity}, joint_limits);
    IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
    EXPECT_THAT(status, RealtimeStatusIs(absl::StatusCode::kInternal,
                                         HasSubstr("[v]")));
  }
}

TEST(JointLimitCheckerTest, FailsForJerkViolationinFirstCycleWithInitialState) {
  JointLimits joint_limits = DefaultLimits();

  JointLimitChecker joint_limit_checker(
      kCycleDuration, kNdof,
      /*initial_state=*/JointStatePVA::Zero(kNdof));

  {
    IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
    RealtimeStatus status =
        joint_limit_checker.Check({0.0, kMaxJerk + 0.001}, joint_limits);
    IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
    EXPECT_THAT(status, RealtimeStatusIs(absl::StatusCode::kInternal,
                                         HasSubstr("[j]")));
  }
}

TEST(JointLimitCheckerTest, FailsForVelocityViolationAfterReset) {
  JointLimits joint_limits = DefaultLimits();

  JointLimitChecker joint_limit_checker(kCycleDuration, kNdof);

  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  for (int i = 0; i < 10; ++i) {
    INTRINSIC_RT_EXPECT_OK(joint_limit_checker.Check({0.0, 0.0}, joint_limits));
  }

  joint_limit_checker.Reset();
  INTRINSIC_RT_EXPECT_OK(joint_limit_checker.Check({50.0, 50.0}, joint_limits));
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();

  {
    IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
    RealtimeStatus status = joint_limit_checker.Check(
        {50.0, 50.0 + 1.1 * kMaxVelocity}, joint_limits);
    IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
    EXPECT_THAT(status, RealtimeStatusIs(absl::StatusCode::kInternal,
                                         HasSubstr("[v]")));
  }
}

TEST(JointLimitCheckerTest,
     LastStateReflectsFiniteDifferencesAndWarmUpZeroing) {
  JointLimits joint_limits = DefaultLimits();

  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  JointLimitChecker joint_limit_checker(kCycleDuration, kNdof);
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();

  // Initial `last_state()` before any `Check()` should be zeroed.
  EXPECT_THAT(joint_limit_checker.last_state().position,
              IsApprox(eigenmath::VectorNd::Zero(kNdof)));
  EXPECT_THAT(joint_limit_checker.last_state().velocity,
              IsApprox(eigenmath::VectorNd::Zero(kNdof)));
  EXPECT_THAT(joint_limit_checker.last_state().acceleration,
              IsApprox(eigenmath::VectorNd::Zero(kNdof)));
  EXPECT_THAT(joint_limit_checker.last_state().jerk,
              IsApprox(eigenmath::VectorNd::Zero(kNdof)));

  // Cycle 0: Check position0 = {1.0, 2.0}.
  // Vel, acc, jerk are zeroed during warm-up cycle 0.
  {
    SCOPED_TRACE("Last state on cycle 0.");
    IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
    INTRINSIC_RT_EXPECT_OK(joint_limit_checker.Check({1.0, 2.0}, joint_limits));
    const auto& last_state = joint_limit_checker.last_state();
    IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();

    eigenmath::VectorNd expected_pos(kNdof);
    expected_pos << 1.0, 2.0;

    EXPECT_THAT(last_state.position, IsApprox(expected_pos));
    EXPECT_THAT(last_state.velocity,
                IsApprox(eigenmath::VectorNd::Zero(kNdof)));
    EXPECT_THAT(last_state.acceleration,
                IsApprox(eigenmath::VectorNd::Zero(kNdof)));
    EXPECT_THAT(last_state.jerk, IsApprox(eigenmath::VectorNd::Zero(kNdof)));
  }

  // Cycle 1: Check position1 = {2.0, 4.0}.
  // vel = {1.0, 2.0}; acc and jerk are zeroed during warm-up cycle 1.
  {
    SCOPED_TRACE("Last state on cycle 1.");
    IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
    INTRINSIC_RT_EXPECT_OK(joint_limit_checker.Check({2.0, 4.0}, joint_limits));
    const auto& last_state = joint_limit_checker.last_state();
    IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();

    eigenmath::VectorNd expected_pos(kNdof);
    expected_pos << 2.0, 4.0;
    eigenmath::VectorNd expected_vel(kNdof);
    expected_vel << 1.0, 2.0;

    EXPECT_THAT(last_state.position, IsApprox(expected_pos));
    EXPECT_THAT(last_state.velocity, IsApprox(expected_vel));
    EXPECT_THAT(last_state.acceleration,
                IsApprox(eigenmath::VectorNd::Zero(kNdof)));
    EXPECT_THAT(last_state.jerk, IsApprox(eigenmath::VectorNd::Zero(kNdof)));
  }

  // Cycle 2: Check position2 = {4.0, 7.0}.
  // vel = {2.0, 3.0}, acc = {1.0, 1.0}; jerk is zeroed during warm-up cycle 2.
  {
    SCOPED_TRACE("Last state on cycle 2.");
    IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
    INTRINSIC_RT_EXPECT_OK(joint_limit_checker.Check({4.0, 7.0}, joint_limits));
    const auto& last_state = joint_limit_checker.last_state();
    IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();

    eigenmath::VectorNd expected_pos(kNdof);
    expected_pos << 4.0, 7.0;
    eigenmath::VectorNd expected_vel(kNdof);
    expected_vel << 2.0, 3.0;
    eigenmath::VectorNd expected_acc(kNdof);
    expected_acc << 1.0, 1.0;

    EXPECT_THAT(last_state.position, IsApprox(expected_pos));
    EXPECT_THAT(last_state.velocity, IsApprox(expected_vel));
    EXPECT_THAT(last_state.acceleration, IsApprox(expected_acc));
    EXPECT_THAT(last_state.jerk, IsApprox(eigenmath::VectorNd::Zero(kNdof)));
  }

  // Cycle 3: Check position3 = {7.0, 11.0}.
  // Full PVAJ computed: vel = {3.0, 4.0}, acc = {1.0, 1.0}, jerk = {0.0, 0.0}.
  {
    SCOPED_TRACE("Last state on cycle 3.");
    IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
    INTRINSIC_RT_EXPECT_OK(
        joint_limit_checker.Check({7.0, 11.0}, joint_limits));
    const auto& last_state = joint_limit_checker.last_state();
    IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();

    eigenmath::VectorNd expected_pos(kNdof);
    expected_pos << 7.0, 11.0;
    eigenmath::VectorNd expected_vel(kNdof);
    expected_vel << 3.0, 4.0;
    eigenmath::VectorNd expected_acc(kNdof);
    expected_acc << 1.0, 1.0;
    eigenmath::VectorNd expected_jerk(kNdof);
    expected_jerk << 0.0, 0.0;

    EXPECT_THAT(last_state.position, IsApprox(expected_pos));
    EXPECT_THAT(last_state.velocity, IsApprox(expected_vel));
    EXPECT_THAT(last_state.acceleration, IsApprox(expected_acc));
    EXPECT_THAT(last_state.jerk, IsApprox(expected_jerk));
  }

  // Cycle 4: Check position4 = {12.0, 18.0}.
  // vel = {5.0, 7.0}, acc = {2.0, 3.0}, jerk = {1.0, 2.0}.
  {
    SCOPED_TRACE("Last state on cycle 4.");
    IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
    INTRINSIC_RT_EXPECT_OK(
        joint_limit_checker.Check({12.0, 18.0}, joint_limits));
    const auto& last_state = joint_limit_checker.last_state();
    IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();

    eigenmath::VectorNd expected_pos(kNdof);
    expected_pos << 12.0, 18.0;
    eigenmath::VectorNd expected_vel(kNdof);
    expected_vel << 5.0, 7.0;
    eigenmath::VectorNd expected_acc(kNdof);
    expected_acc << 2.0, 3.0;
    eigenmath::VectorNd expected_jerk(kNdof);
    expected_jerk << 1.0, 2.0;

    EXPECT_THAT(last_state.position, IsApprox(expected_pos));
    EXPECT_THAT(last_state.velocity, IsApprox(expected_vel));
    EXPECT_THAT(last_state.acceleration, IsApprox(expected_acc));
    EXPECT_THAT(last_state.jerk, IsApprox(expected_jerk));
  }
}

TEST(JointLimitCheckerTest,
     LastStateWithInitialStateComputesFullPVAJOnFirstCycle) {
  JointLimits joint_limits = DefaultLimits();

  JointStatePVA initial_state = JointStatePVA::Zero(kNdof);
  initial_state.position << 1.0, 2.0;
  initial_state.velocity << 0.5, 1.0;
  initial_state.acceleration << 0.2, 0.4;

  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  JointLimitChecker joint_limit_checker(kCycleDuration, kNdof, initial_state);
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();

  // Cycle 0 with initial state skips warm-up.
  // pos = {1.6, 3.2} -> vel = {0.6, 1.2}, acc = {0.1, 0.2}, jerk = {-0.1, -0.2}
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  INTRINSIC_RT_EXPECT_OK(joint_limit_checker.Check({1.6, 3.2}, joint_limits));
  const auto& last_state = joint_limit_checker.last_state();
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();

  eigenmath::VectorNd expected_pos(kNdof);
  expected_pos << 1.6, 3.2;
  eigenmath::VectorNd expected_vel(kNdof);
  expected_vel << 0.6, 1.2;
  eigenmath::VectorNd expected_acc(kNdof);
  expected_acc << 0.1, 0.2;
  eigenmath::VectorNd expected_jerk(kNdof);
  expected_jerk << -0.1, -0.2;

  EXPECT_THAT(last_state.position, IsApprox(expected_pos));
  EXPECT_THAT(last_state.velocity, IsApprox(expected_vel));
  EXPECT_THAT(last_state.acceleration, IsApprox(expected_acc));
  EXPECT_THAT(last_state.jerk, IsApprox(expected_jerk));
}

TEST(JointLimitCheckerTest, LastStateContainsViolatingStateOnError) {
  JointLimits joint_limits = DefaultLimits();

  JointLimitChecker joint_limit_checker(kCycleDuration, kNdof,
                                        JointStatePVA::Zero(kNdof));

  // pos = {0.0, 5.0} -> vel = {0.0, 5.0}, acc = {0.0, 5.0}, jerk = {0.0, 5.0}
  // (violates max_jerk = 4.0)
  EXPECT_THAT(joint_limit_checker.Check({0.0, 5.0}, joint_limits),
              RealtimeStatusIs(absl::StatusCode::kInternal, HasSubstr("[j]")));

  eigenmath::VectorNd expected_pos(kNdof);
  expected_pos << 0.0, 5.0;
  eigenmath::VectorNd expected_vel(kNdof);
  expected_vel << 0.0, 5.0;
  eigenmath::VectorNd expected_acc(kNdof);
  expected_acc << 0.0, 5.0;
  eigenmath::VectorNd expected_jerk(kNdof);
  expected_jerk << 0.0, 5.0;

  const auto& last_state = joint_limit_checker.last_state();
  EXPECT_THAT(last_state.position, IsApprox(expected_pos));
  EXPECT_THAT(last_state.velocity, IsApprox(expected_vel));
  EXPECT_THAT(last_state.acceleration, IsApprox(expected_acc));
  EXPECT_THAT(last_state.jerk, IsApprox(expected_jerk));
}

}  // namespace

}  // namespace intrinsic::icon
