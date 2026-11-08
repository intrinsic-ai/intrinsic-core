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

#include "intrinsic/icon/control/algorithms/joint_velocity_reflexxes.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstddef>
#include <string>

#include "Eigen/Core"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "internal/testing.h"
#include "intrinsic/eigenmath/matchers.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/control/algorithms/reflexxes_test_utils.h"
#include "intrinsic/icon/reflexxes/constants_external.h"
#include "intrinsic/icon/reflexxes/flags.h"
#include "intrinsic/icon/reflexxes/status.h"
#include "intrinsic/icon/testing/malloc_test.h"
#include "intrinsic/icon/utils/realtime_status_matchers.h"
#include "intrinsic/kinematics/types/joint_limits.h"
#include "intrinsic/kinematics/types/joint_state.h"

namespace intrinsic {
namespace icon {
namespace {

using ::testing::HasSubstr;

// Test that a simple motion (with very high limits on derivatives) is completed
// in the requested duration and the target point is reached.
TEST_F(JointVelocityReflexxesTest, TestSimpleTrajectory) {
  const std::size_t ndof = 4;
  JointStatePVA initial;
  ASSERT_OK(initial.SetSize(ndof));
  initial.position.setZero();
  initial.velocity << 1.0, 2.0, 3.0, 4.0;
  initial.acceleration.setZero();

  JointStateV target;
  ASSERT_OK(target.SetSize(ndof));
  target.velocity << 2.0, -1.0, 1.0, 2.0;

  JointLimits limits;
  ASSERT_OK(limits.SetSize(ndof));
  limits.min_position.setConstant(-1000000);
  limits.max_position.setConstant(10000000);
  limits.max_acceleration.setConstant(100);
  limits.max_jerk.setConstant(1000);

  double frequency_hz = 1000.0;
  double dt_s = 1 / frequency_hz;
  auto trajectory =
      GetJointTrajectory(ndof, frequency_hz, initial, limits, target);

  double total_time = dt_s * trajectory.size();
  EXPECT_GT(0.5, total_time);
  EXPECT_LT(0.1, total_time);
  EXPECT_GT(1e-12, (trajectory.back().velocity - target.velocity).norm());
}

TEST_F(JointVelocityReflexxesTest, TestHoldPosition) {
  const std::size_t ndof = 4;
  JointStatePVA initial;
  ASSERT_OK(initial.SetSize(ndof));
  initial.position << 1.0, 2.0, 3.0, 4.0;
  initial.velocity.setZero();
  initial.acceleration.setZero();

  JointStateV target;
  ASSERT_OK(target.SetSize(ndof));
  target.velocity.setZero();

  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(auto limits, JointLimits::Unlimited(ndof));

  double frequency_hz = 1000.0;
  auto trajectory =
      GetJointTrajectory(ndof, frequency_hz, initial, limits, target);

  EXPECT_EQ(trajectory.size(), 2);
  EXPECT_THAT(trajectory.back().position,
              intrinsic::eigenmath::testing::IsApprox(initial.position));
}

// Test that a positional limits error is produced when Reflexxes cannot plan a
// motion from the current state to the target using the velocity, acceleration,
// and jerk limits without violating the position limits
TEST_F(JointVelocityReflexxesTest, TestPositionLimitViolation) {
  const std::size_t ndof = 2;
  JointStatePVA initial;
  ASSERT_OK(initial.SetSize(ndof));

  JointStateV target;
  ASSERT_OK(target.SetSize(ndof));
  target.velocity.setZero();

  JointLimits limits;
  ASSERT_OK(limits.SetSize(ndof));
  limits.min_position.setConstant(-10);
  limits.max_position.setConstant(10);
  limits.max_acceleration.setConstant(10);
  limits.max_jerk.setConstant(100);

  double frequency_hz = 1000.0;

  JointVelocityReflexxes generator(ndof, frequency_hz);
  generator.SetPositionalLimitsBehavior(
      reflexxes::Flags::PositionalLimitsBehavior::kErrorMsgOnly);

  // violation, but not getting better
  initial.position = limits.max_position * 2;
  initial.velocity.setConstant(-1);
  EXPECT_TRUE(generator.SetLimits(limits));
  EXPECT_TRUE(generator.SetTarget(target));
  EXPECT_TRUE(generator.SetPrevious(initial));
  EXPECT_TRUE(generator.ComputeSetpoint(&initial));
  EXPECT_EQ(generator.GetReflexxesStatus(), reflexxes::Status::kWorking);

  // violation and getting worse
  initial.position = limits.max_position * 2;
  initial.velocity.setConstant(1);
  EXPECT_TRUE(generator.SetLimits(limits));
  EXPECT_TRUE(generator.SetTarget(target));
  EXPECT_TRUE(generator.SetPrevious(initial));
  EXPECT_FALSE(generator.ComputeSetpoint(&initial));
  EXPECT_EQ(generator.GetReflexxesStatus(),
            reflexxes::Status::kErrorPositionalLimits);
}

// Test that a positional limits error is produced when Reflexxes cannot plan a
// motion from the current state to the target using the velocity, acceleration,
// and jerk limits without violating the position limits
TEST_F(JointVelocityReflexxesTest, TestGetPositionAtTargetVelocity) {
  const std::size_t ndof = 2;
  const double max_acceleration = 0.5;
  const double velocity_initial = 2;

  JointStatePVA initial;
  ASSERT_OK(initial.SetSize(ndof));
  initial.position.setZero();
  initial.velocity.setConstant(velocity_initial);
  initial.acceleration.setZero();

  JointStateV target;
  ASSERT_OK(target.SetSize(ndof));
  target.velocity.setZero();

  JointLimits limits;
  ASSERT_OK(limits.SetSize(ndof));
  limits.SetUnlimited();
  limits.max_acceleration.setConstant(max_acceleration);

  double frequency_hz = 10.0;

  JointVelocityReflexxes generator(ndof, frequency_hz);
  generator.SetPositionalLimitsBehavior(
      reflexxes::Flags::PositionalLimitsBehavior::kErrorMsgOnly);

  // violation, but not getting better
  EXPECT_TRUE(generator.SetLimits(limits));
  EXPECT_TRUE(generator.SetTarget(target));
  EXPECT_TRUE(generator.SetPrevious(initial));

  eigenmath::VectorNb selection(ndof);
  selection.setConstant(true);
  EXPECT_TRUE(generator.SetSelection(selection));

  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  auto position_or_status = generator.PositionAtTargetVelocity();
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();

  EXPECT_THAT(position_or_status,
              RealtimeStatusIs(absl::StatusCode::kFailedPrecondition,
                               HasSubstr("You must call ComputeSetpoint() "
                                         "before calling this function.")));

  JointStatePVA setpoint;
  ASSERT_OK(setpoint.SetSize(ndof));
  EXPECT_TRUE(generator.ComputeSetpoint(&setpoint));

  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(auto joint_state_at_target,
                                    generator.PositionAtTargetVelocity());

  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
  double t_stop =
      velocity_initial / max_acceleration;  // Assumes infinite jerk.
  double t_sync = generator.GetSyncTime();
  EXPECT_EQ(t_stop, t_sync);
  EXPECT_THAT(joint_state_at_target.position,
              eigenmath::testing::IsApprox(
                  (initial.position + initial.velocity * t_sync) -
                      (0.5 * t_sync * t_sync * limits.max_acceleration),
                  1e-5));
}

// Test that a random motion starts at the initial state and ends at the target
// state.
TEST_F(JointVelocityReflexxesTest, RandomSetup) {
  static constexpr size_t ndof = 5;
  static constexpr size_t ntrials = 5;
  static constexpr double frequency_hz = 100.0;
  static constexpr double dt = 1.0 / frequency_hz;
  eigenmath::VectorNb selection(ndof);
  selection.setConstant(true);
  for (size_t trial = 0; trial < ntrials; ++trial) {
    JointStatePVA initial;
    JointStateV target;
    JointLimits limits;
    RandomSetup(ndof, &initial, &target, &limits);

    Enclosure enclosure;
    ASSERT_TRUE(
        enclosure.Compute(dt, target, -limits.max_jerk, limits.max_jerk));

    const auto trajectory =
        GetJointTrajectory(ndof, frequency_hz, initial, limits, target);
    ASSERT_LE(1, trajectory.size());
    ASSERT_EQ(1, enclosure.Contains(trajectory.back(), selection, false));
  }
}

// Test that the selection mechanism works.
TEST_F(JointVelocityReflexxesTest, Selection) {
  static constexpr size_t ndof = 4;
  static constexpr size_t ntrials = 5;
  static constexpr double frequency_hz = 100.0;
  static constexpr double dt = 1.0 / frequency_hz;
  eigenmath::VectorNb selection(ndof);
  for (size_t trial = 0; trial < ntrials; ++trial) {
    JointStatePVA initial;
    JointStateV target;
    JointLimits limits;
    RandomSetup(ndof, &initial, &target, &limits);

    Enclosure enclosure;
    ASSERT_TRUE(
        enclosure.Compute(dt, target, -limits.max_jerk, limits.max_jerk));

    const auto full =
        GetJointTrajectory(ndof, frequency_hz, initial, limits, target);
    ASSERT_LE(1, full.size());

    for (int bits = 0x1; bits < 0xf; ++bits) {
      for (size_t dof = 0; dof < ndof; ++dof) {
        const int mask = 1 << dof;
        selection[dof] = (bits & mask);
      }

      const auto partial = GetJointTrajectory(
          ndof, frequency_hz, initial, limits, target, selection, full.size());
      ASSERT_LE(1, partial.size());
      ASSERT_EQ(1, enclosure.Contains(partial.back(), selection, false));
    }
  }
}

TEST_F(JointVelocityReflexxesTest, LogErrorNoAlloc) {
  constexpr size_t ndof = 3;
  constexpr double frequency_hz = 100.0;
  JointStatePVA initial;
  JointStateV target;
  JointLimits limits;
  RandomSetup(ndof, &initial, &target, &limits);
  initial.position[0] = 10.0 * limits.max_position[0];  // make it invalid
  initial.velocity[0] = 10.0;                           // make it invalid

  JointVelocityReflexxes generator(ndof, frequency_hz);
  generator.SetPositionalLimitsBehavior(
      reflexxes::Flags::PositionalLimitsBehavior::kErrorMsgOnly);
  JointStatePVA setpoint;
  ASSERT_OK(setpoint.SetSize(ndof));

  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();

  ASSERT_TRUE(generator.SetLimits(limits));
  ASSERT_TRUE(generator.SetTarget(target));
  ASSERT_TRUE(generator.SetPrevious(initial));
  ASSERT_FALSE(generator.ComputeSetpoint(&setpoint));

  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
}

TEST_F(JointVelocityReflexxesTest, LimitErrorChecking) {
  JointLimits limits;
  ASSERT_OK(limits.SetSize(1));
  limits.min_position[0] = -1.0;
  limits.max_position[0] = 1.0;
  limits.max_acceleration[0] = 1.0;
  limits.max_jerk[0] = 1.0;

  JointVelocityReflexxes generator(1, 100.0);

  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();

  ASSERT_TRUE(generator.SetLimits(limits));

  limits.min_position[0] = 1.1;
  ASSERT_FALSE(generator.SetLimits(limits));

  limits.min_position[0] = -reflexxes::kMaxPositionalLimit;
  ASSERT_TRUE(generator.SetLimits(limits));

  limits.min_position[0] = -1.01 * reflexxes::kMaxPositionalLimit;
  ASSERT_TRUE(generator.SetLimits(limits));
  limits.min_position[0] = -1.0;
  ASSERT_TRUE(generator.SetLimits(limits));

  limits.min_position[0] = -1.0;
  limits.max_position[0] = -1.0;
  ASSERT_FALSE(generator.SetLimits(limits));

  limits.min_position[0] = -1.0;
  limits.max_position[0] = -2.0;
  ASSERT_FALSE(generator.SetLimits(limits));

  limits.max_position[0] = reflexxes::kMaxPositionalLimit;
  ASSERT_TRUE(generator.SetLimits(limits));
  limits.max_position[0] = 1.01 * reflexxes::kMaxPositionalLimit;
  ASSERT_TRUE(generator.SetLimits(limits));
  limits.max_position[0] = -1.1;
  ASSERT_FALSE(generator.SetLimits(limits));
  limits.max_position[0] = 1.0;
  ASSERT_TRUE(generator.SetLimits(limits));

  limits.max_acceleration[0] = 0.0;
  ASSERT_FALSE(generator.SetLimits(limits));
  limits.max_acceleration[0] = -0.01;
  ASSERT_FALSE(generator.SetLimits(limits));
  limits.max_acceleration[0] = 1.0;
  ASSERT_TRUE(generator.SetLimits(limits));

  limits.max_jerk[0] = 0.0;
  ASSERT_FALSE(generator.SetLimits(limits));
  limits.max_jerk[0] = -0.01;
  ASSERT_FALSE(generator.SetLimits(limits));
  limits.max_jerk[0] = 1.0;
  ASSERT_TRUE(generator.SetLimits(limits));

  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
}

// Skip TEST_F(JointVelocityReflexxesTest, finalMotionBehavior) for
// now. It would become relevant if we encounter infinite loops in
// getJointTrajectory().

struct BreachPositionalLimitsTestCase {
  std::string test_name;
  reflexxes::Flags::PositionalLimitsBehavior behavior;
};

// Tests the functionality to query if the stop trajectory is within positional
// limits for a given set of parameters.
class BreachPositionalLimitsPTest
    : public ::testing::TestWithParam<BreachPositionalLimitsTestCase> {
 public:
  static constexpr size_t kNDofs = 2;
  static constexpr size_t kFrequencyHz = 500;

  BreachPositionalLimitsPTest()
      : generator_(/*ndof=*/kNDofs, /*frequency=*/kFrequencyHz) {}

  void SetUp() override {
    ASSERT_OK(initial_.SetSize(kNDofs));
    initial_.position << 1.49008, 0.0;
    initial_.velocity << 3.5, 0.0;
    initial_.acceleration.setZero();

    ASSERT_OK(target_.SetSize(kNDofs));
    target_.velocity.setZero();

    ASSERT_OK(limits_.SetSize(kNDofs));
    limits_.min_position << -2.87979, -1.48353;
    limits_.max_position << 2.87979, 2.70526;
    limits_.max_velocity.setZero();
    limits_.max_acceleration.setConstant(4.5);
    limits_.max_jerk.setConstant(240);

    generator_.SetPositionalLimitsBehavior(GetParam().behavior);

    // Set all parameters and query if the motion breaches positional limits.
    EXPECT_TRUE(generator_.SetLimits(limits_));
    EXPECT_TRUE(generator_.SetTarget(target_));
    EXPECT_TRUE(generator_.SetPrevious(initial_));
    EXPECT_TRUE(generator_.ComputeSetpoint(&initial_));
    EXPECT_THAT(generator_.DoesStopMotionFromNewStateBreachPositionalLimits(),
                icon::RealtimeIsOk());
  }

 protected:
  JointLimits limits_;
  JointStateV target_;
  JointStatePVA initial_;
  JointVelocityReflexxes generator_;
};

TEST_P(BreachPositionalLimitsPTest, Succeeds) {
  // The initial state is within limits, but the acceleration limits are low.
  // This implies that the trajectory generator can reach the target velocity,
  // but the state (at which it will come back to a stop afterwards) will
  // violate positional limits. The returned reflexxes status from
  // GetReflexxesStatus() is not as expected `kErrorPositionalLimits` because
  // it is internally overwritten. But this result can be recovered by
  // querying DoesStopMotionFromNewStateBreachPositionalLimits(), which will
  // report that the stop trajectory will breach positional limits.
  EXPECT_EQ(generator_.GetReflexxesStatus(), reflexxes::Status::kWorking);
  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
      bool trajectory_breaches_limits,
      generator_.DoesStopMotionFromNewStateBreachPositionalLimits());
  EXPECT_TRUE(trajectory_breaches_limits);
}

TEST_P(BreachPositionalLimitsPTest, FailsWhenQueryingDueToResetOfJointLimits) {
  EXPECT_TRUE(generator_.SetLimits(limits_));
  EXPECT_THAT(generator_.DoesStopMotionFromNewStateBreachPositionalLimits(),
              RealtimeStatusIs(absl::StatusCode::kFailedPrecondition,
                               HasSubstr("You must call ComputeSetpoint() "
                                         "before calling this function.")));
}

TEST_P(BreachPositionalLimitsPTest, FailsWhenQueryingDueToResetOfTargetState) {
  EXPECT_TRUE(generator_.SetTarget(target_));
  EXPECT_THAT(generator_.DoesStopMotionFromNewStateBreachPositionalLimits(),
              RealtimeStatusIs(absl::StatusCode::kFailedPrecondition,
                               HasSubstr("You must call ComputeSetpoint() "
                                         "before calling this function.")));
}

TEST_P(BreachPositionalLimitsPTest,
       FailsWhenQueryingDueToResetOfPreviousState) {
  EXPECT_TRUE(generator_.SetPrevious(initial_));
  EXPECT_THAT(generator_.DoesStopMotionFromNewStateBreachPositionalLimits(),
              RealtimeStatusIs(absl::StatusCode::kFailedPrecondition,
                               HasSubstr("You must call ComputeSetpoint() "
                                         "before calling this function.")));
}

INSTANTIATE_TEST_SUITE_P(
    BreachPositionalLimitsPTests, BreachPositionalLimitsPTest,
    ::testing::Values(
        BreachPositionalLimitsTestCase{
            .test_name = "TestingIgnoreBehavior",
            .behavior = reflexxes::Flags::PositionalLimitsBehavior::kIgnore},
        BreachPositionalLimitsTestCase{
            .test_name = "TestingActivelyPreventBehavior",
            .behavior =
                reflexxes::Flags::PositionalLimitsBehavior::kActivelyPrevent}),
    [](const ::testing::TestParamInfo<BreachPositionalLimitsPTest::ParamType>&
           info) { return info.param.test_name; });

}  // namespace
}  // namespace icon
}  // namespace intrinsic
