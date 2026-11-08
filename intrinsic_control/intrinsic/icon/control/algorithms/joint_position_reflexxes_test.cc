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

#include "intrinsic/icon/control/algorithms/joint_position_reflexxes.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <memory>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status_matchers.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "internal/testing.h"
#include "intrinsic/eigenmath/matchers.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/control/algorithms/joint_limit_checker.h"
#include "intrinsic/icon/control/algorithms/reflexxes_test_utils.h"
#include "intrinsic/icon/reflexxes/constants_external.h"
#include "intrinsic/icon/reflexxes/flags.h"
#include "intrinsic/icon/reflexxes/position_flags.h"
#include "intrinsic/icon/reflexxes/status.h"
#include "intrinsic/icon/testing/malloc_test.h"
#include "intrinsic/icon/utils/realtime_status_matchers.h"
#include "intrinsic/kinematics/testing/trajectory_plot.h"
#include "intrinsic/kinematics/types/check_joint_limits.h"
#include "intrinsic/kinematics/types/dynamic_limits_check_mode.h"
#include "intrinsic/kinematics/types/joint_limits.h"
#include "intrinsic/kinematics/types/joint_state.h"
#include "intrinsic/kinematics/types/joint_trajectories.h"
#include "intrinsic/math/signals/numerical_differentiation.h"

namespace intrinsic {
namespace icon {
namespace {

using eigenmath::testing::IsApprox;

// Threshold for comparing Reflexxes output positions.
constexpr double kDefaultPositionAccuracy = 1e-10;

// Threshold for comparing Reflexxes output velocities against numerically
// estimated velocities.
constexpr double kDefaultVelocityAccuracy = 0.01;
// Threshold for comparing Reflexxes output accelerations against numerically
// estimated acceleration. Acceleration threshold is chosen liberally in order
// to account for delay of the backward finite difference pattern. So this check
// only asserts that the order of acceleration magnitude is okay.
constexpr double kDefaultAccelerationAccuracy = 2.0;

// Checks if a PVA trajectory output by Reflexxes is differentially consistent,
// i.e. the derivatives match numerical estimates.
void CheckDifferentialConsistency(const std::vector<JointStatePVA>& trajectory,
                                  double frequency_hz, double vel_accuracy,
                                  double acc_accuracy) {
  std::vector<eigenmath::VectorNd> joint_positions;
  for (int i = 0; i < trajectory.size(); i++) {
    joint_positions.push_back(trajectory[i].position);
  }

  ASSERT_OK_AND_ASSIGN(
      std::vector<eigenmath::VectorNd> velocities_est,
      ComputeFirstDerivativesCentral(absl::MakeConstSpan(joint_positions),
                                     1. / frequency_hz,
                                     /*accuracy_order=*/4));
  ASSERT_OK_AND_ASSIGN(
      std::vector<eigenmath::VectorNd> accelerations_est,
      ComputeSecondDerivativesCentral(absl::MakeConstSpan(joint_positions),
                                      1. / frequency_hz,
                                      /*accuracy_order=*/4));

  for (int i = 0; i < trajectory.size(); i++) {
    // Compare estimated velocity and acceleration against trajectory velocity
    // and acceleration. Print verbose output in case of mismatch.
    if ((velocities_est[i] - trajectory[i].velocity).cwiseAbs().maxCoeff() >
        vel_accuracy) {
      LOG(INFO) << "Velocity inconsistency at index " << i
                << "\n-- expected velocity: "
                << trajectory[i].velocity.transpose()
                << "\n-- numdiff result: " << velocities_est[i].transpose();
    }
    EXPECT_LT(
        (velocities_est[i] - trajectory[i].velocity).cwiseAbs().maxCoeff(),
        vel_accuracy);

    if ((accelerations_est[i] - trajectory[i].acceleration)
            .cwiseAbs()
            .maxCoeff() > acc_accuracy) {
      LOG(INFO) << "Acceleration inconsistency at index " << i
                << "\n-- expected acceleration: "
                << trajectory[i].acceleration.transpose()
                << "\n-- numdiff result: " << accelerations_est[i].transpose();
    }
    EXPECT_LT((accelerations_est[i] - trajectory[i].acceleration)
                  .cwiseAbs()
                  .maxCoeff(),
              acc_accuracy);
  }
}

// Test that a simple motion (with very high limits) is completed in the
// requested duration and the target point is reached.
TEST_F(JointPositionReflexxesTest, TestSimpleTrajectoryHighLimits) {
  const size_t ndof = 4;
  JointStatePVA initial;
  ASSERT_OK(initial.SetSize(ndof));
  initial.position.setZero();
  initial.velocity.setZero();
  initial.acceleration.setZero();

  JointStatePV target;
  ASSERT_OK(target.SetSize(ndof));
  target.position << 1.0, 2.0, 3.0, 4.0;
  target.velocity << 0, 0, 0, 0;

  JointLimits limits;
  ASSERT_OK(limits.SetSize(ndof));
  limits.min_position.setConstant(-100);
  limits.max_position.setConstant(100);
  limits.max_velocity.setConstant(10);
  limits.max_acceleration.setConstant(100);
  limits.max_jerk.setConstant(1000);

  double frequency_hz = 1000.0;
  double dt_s = 1 / frequency_hz;
  auto trajectory =
      GetJointTrajectory(ndof, frequency_hz, initial, limits, target);

  double total_time = dt_s * trajectory.size();
  EXPECT_GT(0.7, total_time);
  EXPECT_LT(0.5, total_time);
  EXPECT_GT(1e-12, (trajectory.back().position - target.position).norm());
  EXPECT_GT(1e-12, (trajectory.back().velocity - target.velocity).norm());

  CheckDifferentialConsistency(trajectory, frequency_hz,
                               kDefaultVelocityAccuracy,
                               kDefaultAccelerationAccuracy);
}

class JointPositionReflexxesSyncBehaviorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Set up the default test parameters.
    ndofs_ = 2;
    frequency_hz_ = 125.0;

    // Define two states so that we test a motion in both directions.
    ASSERT_OK(state1_.SetSize(ndofs_));
    state1_.position.setConstant(0.05);
    state1_.velocity.setZero();
    state1_.acceleration.setZero();
    ASSERT_OK(state2_.SetSize(ndofs_));
    state2_.position << 0.135, 0.135;
    state2_.velocity.setZero();
    state2_.acceleration.setZero();

    // Prepare the new target state.
    ASSERT_OK(new_target_.SetSize(ndofs_));

    // Set up the default joint limits.
    ASSERT_OK(joint_limits_.SetSize(ndofs_));
    joint_limits_.min_position << 0.0, 0.0;
    joint_limits_.max_position << 0.7, 0.7;
    joint_limits_.max_velocity << 6.0, 6.0;
    joint_limits_.max_acceleration << 4.0, 4.0;
    joint_limits_.max_jerk << 15.0, 15.0;
  }

  std::unique_ptr<JointPositionReflexxes> CreateGenerator(
      reflexxes::Flags::SyncBehavior sync_behavior) {
    auto generator =
        std::make_unique<JointPositionReflexxes>(ndofs_, frequency_hz_);
    CHECK(generator->SetSelection(
        eigenmath::VectorNb::Constant(ndofs_, 1, true)));
    generator->SetBehaviorIfInitialStateBreachesConstraints(
        reflexxes::PositionFlags::BehaviorIfInitialStateBreachesConstraints::
            kGetIntoBoundariesAtZeroAcceleration);
    generator->SetPositionalLimitsBehavior(
        reflexxes::Flags::PositionalLimitsBehavior::kIgnore);
    generator->SetSyncBehavior(sync_behavior);
    CHECK(generator->SetLimits(joint_limits_));
    return generator;
  }

 public:
  int ndofs_;
  double frequency_hz_;
  JointStatePVA state1_;
  JointStatePVA state2_;
  JointStatePVA new_target_;

  JointLimits joint_limits_;
  std::unique_ptr<JointPositionReflexxes> generator_;
};

// This test and the next one only differ in the synchronization behavior.
// They should produce the same exact results. This test is currently disabled
// because it produces an unexpected behaviour.
// TODO(b/444215190): Re-enable this test once the bug is fixed.
TEST_F(JointPositionReflexxesSyncBehaviorTest,
       DISABLED_ReflexxesSyncTimeWithSynchronization) {
  auto generator = CreateGenerator(
      reflexxes::Flags::SyncBehavior::kPhaseSynchronizationIfPossible);

  // Motion in both directions with synchronous behavior should match.
  // Forward motion.
  ASSERT_TRUE(generator->SetTarget(state1_));
  ASSERT_TRUE(generator->SetPrevious(state2_));
  ASSERT_TRUE(generator->ComputeSetpoint(&new_target_));
  const double sync_time1 = generator->GetSyncTime();

  // Backward motion.
  ASSERT_TRUE(generator->SetTarget(state2_));
  ASSERT_TRUE(generator->SetPrevious(state1_));
  ASSERT_TRUE(generator->ComputeSetpoint(&new_target_));
  const double sync_time2 = generator->GetSyncTime();

  const double kTolerance = 1.0e-6;
  EXPECT_NEAR(sync_time1, sync_time2, kTolerance);
}

TEST_F(JointPositionReflexxesSyncBehaviorTest,
       ReflexxesSyncTimeWithoutSynchronization) {
  auto generator =
      CreateGenerator(reflexxes::Flags::SyncBehavior::kNoSynchronization);

  // Motion in both directions with asynchronous behavior should match.
  // Forward motion.
  ASSERT_TRUE(generator->SetTarget(state1_));
  ASSERT_TRUE(generator->SetPrevious(state2_));
  ASSERT_TRUE(generator->ComputeSetpoint(&new_target_));
  const double sync_time1 = generator->GetSyncTime();

  // Backward motion.
  ASSERT_TRUE(generator->SetTarget(state2_));
  ASSERT_TRUE(generator->SetPrevious(state1_));
  ASSERT_TRUE(generator->ComputeSetpoint(&new_target_));
  const double sync_time2 = generator->GetSyncTime();

  const double kTolerance = 1.0e-6;
  EXPECT_NEAR(sync_time1, sync_time2, kTolerance);
}

// Test that a simple motion (with very low limits) is completed in the
// requested duration and the target point is reached.
// Use non-trivial values for targets and limits to reveal potential numerical
// issues.
TEST_F(JointPositionReflexxesTest, TestSimpleTrajectoryLowLimits) {
  const size_t ndof = 4;
  JointStatePVA initial;
  ASSERT_OK(initial.SetSize(ndof));
  // Use non-trivial initial state.
  initial.position << 0.001, M_PI_2, M_PI_4, 1.11;
  initial.velocity.setConstant(1e-5);
  initial.acceleration << 1e-4, -1e-4, 1e-10, 1e-10;

  JointStatePV target;
  ASSERT_OK(target.SetSize(ndof));
  target.position << 1.4234234, -2.2989879289289839, 3.01, 4.011227777777;
  target.velocity << 0, 0, 0, 0;

  JointLimits limits;
  ASSERT_OK(limits.SetSize(ndof));
  limits.min_position.setConstant(-std::numeric_limits<double>::infinity());
  limits.max_position.setConstant(std::numeric_limits<double>::infinity());
  limits.max_velocity << std::sqrt(2), std::sqrt(3), 2.0, std::sqrt(1.9);
  limits.max_acceleration << std::sqrt(5), std::sqrt(7), std::sqrt(10), M_PI;
  limits.max_jerk << 0.411324141231, 0.51232123, 0.152343242, 0.21234234;

  double frequency_hz = 125.0;
  auto trajectory =
      GetJointTrajectory(ndof, frequency_hz, initial, limits, target);

  EXPECT_GT(1e-12, (trajectory.back().position - target.position).norm());
  EXPECT_GT(1e-12, (trajectory.back().velocity - target.velocity).norm());

  CheckDifferentialConsistency(trajectory, frequency_hz,
                               kDefaultVelocityAccuracy,
                               kDefaultAccelerationAccuracy);
}

TEST_F(JointPositionReflexxesTest, TestSimpleTrajectorySmallInitialState) {
  const size_t ndof = 4;
  JointStatePVA initial;
  ASSERT_OK(initial.SetSize(ndof));
  // Use non-trivial initial state.
  initial.position << 1e-6, M_PI_2 / 10e4, M_PI_4 / 10e4, 1e-6;
  initial.velocity.setZero();
  initial.acceleration.setZero();

  JointStatePV target;
  ASSERT_OK(target.SetSize(ndof));
  target.position.setZero();
  target.velocity.setZero();

  JointLimits limits;
  ASSERT_OK(limits.SetSize(ndof));
  limits.min_position.setConstant(-std::numeric_limits<double>::infinity());
  limits.max_position.setConstant(std::numeric_limits<double>::infinity());
  limits.max_velocity << std::sqrt(2), std::sqrt(3), 2.0, std::sqrt(1.9);
  limits.max_acceleration << std::sqrt(5), std::sqrt(7), std::sqrt(10), M_PI;
  limits.max_jerk << 0.411324141231, 0.51232123, 0.152343242, 0.21234234;

  double frequency_hz = 125.0;
  auto trajectory =
      GetJointTrajectory(ndof, frequency_hz, initial, limits, target);

  EXPECT_GT(1e-12, (trajectory.back().position - target.position).norm());
  EXPECT_GT(1e-12, (trajectory.back().velocity - target.velocity).norm());

  CheckDifferentialConsistency(trajectory, frequency_hz,
                               kDefaultVelocityAccuracy,
                               kDefaultAccelerationAccuracy);
}

// Reproduces the jerk limit violation on Fanuc robots
TEST_F(JointPositionReflexxesTest,
       TestZeroTargetSmallInitialStateIsJerkLimited) {
  const size_t ndof = 4;
  JointStatePVA initial;
  ASSERT_OK(initial.SetSize(ndof));
  // Use non-trivial initial state. The third value is the critical one.
  initial.position << 0.0, 0.00, 0.0000127, 0.0;
  initial.velocity.setZero();
  initial.acceleration.setZero();

  JointStatePV target;
  ASSERT_OK(target.SetSize(ndof));
  target.position.setZero();
  target.velocity.setZero();

  JointLimits limits;
  ASSERT_OK(limits.SetSize(ndof));
  limits.min_position.setConstant(-std::numeric_limits<double>::infinity());
  limits.max_position.setConstant(std::numeric_limits<double>::infinity());
  limits.max_velocity << std::sqrt(2), std::sqrt(3), 2.0, std::sqrt(1.9);
  limits.max_acceleration << std::sqrt(5), std::sqrt(7), std::sqrt(10), M_PI;
  limits.max_jerk << 0.411324141231, 0.51232123, 0.152343242, 0.21234234;

  // The JointLimitChecker checks that the trajectory is within limits and
  // Reflexxes fully saturates the limits completely. We need to check against
  // slightly higher limits to handle this.
  JointLimits check_limits = limits;
  check_limits.max_velocity *= 1.001;
  check_limits.max_acceleration *= 1.001;
  check_limits.max_jerk *= 1.001;

  double frequency_hz = 125.0;
  auto trajectory =
      GetJointTrajectory(ndof, frequency_hz, initial, limits, target);

  ASSERT_OK_AND_ASSIGN(auto traj,
                       JointTrajectoryPVA::Create(
                           trajectory, absl::Seconds(1.0 / frequency_hz),
                           DynamicLimitsCheckMode::kCheckJointAcceleration,
                           JointTrajectoryInterpolationType::kCubicPolynomial));
  ASSERT_OK(CreateAndSavePlot(traj, "joints"));

  EXPECT_GT(1e-12, (trajectory.back().position - target.position).norm());
  EXPECT_GT(1e-12, (trajectory.back().velocity - target.velocity).norm());

  CheckDifferentialConsistency(trajectory, frequency_hz,
                               kDefaultVelocityAccuracy,
                               kDefaultAccelerationAccuracy);

  JointLimitChecker joint_limit_checker(absl::Seconds(1. / frequency_hz), ndof);
  for (const auto& state : trajectory) {
    EXPECT_OK(joint_limit_checker.Check(state.position, check_limits));
  }
}

// Test that a positional limits error is produced when Reflexxes cannot plan a
// motion from the current state to the target using the velocity, acceleration,
// and jerk limits without violating the position limits
TEST_F(JointPositionReflexxesTest, TestPositionLimitViolation) {
  const size_t ndof = 2;
  JointStatePVA initial;
  ASSERT_OK(initial.SetSize(ndof));

  JointStatePV target;
  ASSERT_OK(target.SetSize(ndof));
  target.position.setZero();
  target.velocity.setZero();

  JointLimits limits;
  ASSERT_OK(limits.SetSize(ndof));
  limits.min_position.setConstant(-10);
  limits.max_position.setConstant(10);
  limits.max_velocity.setConstant(10);
  limits.max_acceleration.setConstant(10);
  limits.max_jerk.setConstant(100);

  double frequency_hz = 1000.0;
  JointPositionReflexxes generator(ndof, frequency_hz);

  generator.SetPositionalLimitsBehavior(
      reflexxes::Flags::PositionalLimitsBehavior::kErrorMsgOnly);

  // violation, but getting better
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

// Test that we can achieve a target position at the position limit despite
// having the positional limits behaviour `kActivelyPrevent` set.
TEST_F(JointPositionReflexxesTest, PositionTargetCoincidingWithPositionLimit) {
  const size_t ndof = 2;
  JointStatePVA current;
  ASSERT_OK(current.SetSize(ndof));

  JointLimits limits;
  ASSERT_OK(limits.SetSize(ndof));
  limits.min_position.setConstant(-10);
  limits.max_position.setConstant(10);
  limits.max_velocity.setConstant(10);
  limits.max_acceleration.setConstant(10);
  limits.max_jerk.setConstant(100);

  double frequency_hz = 1000.0;
  JointPositionReflexxes generator(ndof, frequency_hz);
  generator.SetPositionalLimitsBehavior(
      reflexxes::Flags::PositionalLimitsBehavior::kActivelyPrevent);

  JointStatePV target;
  ASSERT_OK(target.SetSize(ndof));
  target.position = limits.max_position;  // equal to maximum position limit.
  target.velocity.setZero();

  // Initialize and run one iteration to initialize the reflexxes status.
  EXPECT_TRUE(generator.SetLimits(limits));
  EXPECT_TRUE(generator.SetTarget(target));
  EXPECT_TRUE(generator.SetPrevious(current));
  EXPECT_TRUE(generator.ComputeSetpoint(&current));

  const int kMaxIter = 100000;
  int iter = 0;
  while (generator.GetReflexxesStatus() !=
         reflexxes::Status::kFinalStateReached) {
    EXPECT_TRUE(generator.SetPrevious(current));
    EXPECT_TRUE(generator.ComputeSetpoint(&current));
    ++iter;
    if (iter > kMaxIter) FAIL() << "Max iterations reached";
  }
  EXPECT_THAT(current.position,
              IsApprox(limits.max_position, kDefaultPositionAccuracy));
}

// Test that we can achieve a target position at the zero position limit despite
// having the positional limits behaviour `kActivelyPrevent` set.
TEST_F(JointPositionReflexxesTest,
       PositionTargetCoincidingWithZeroPositionLimit) {
  const size_t ndof = 6;
  JointStatePVA current;
  ASSERT_OK(current.SetSize(ndof));
  current.position.setConstant(.1);

  JointLimits limits;
  ASSERT_OK(limits.SetSize(ndof));
  limits.min_position.setConstant(0.0);
  limits.max_position.setConstant(0.7);
  limits.max_velocity.setConstant(30);
  limits.max_acceleration.setConstant(3);
  limits.max_jerk.setConstant(120);

  double frequency_hz = 250.0;
  JointPositionReflexxes generator(ndof, frequency_hz);
  generator.SetPositionalLimitsBehavior(
      reflexxes::Flags::PositionalLimitsBehavior::kActivelyPrevent);

  JointStatePV target;
  ASSERT_OK(target.SetSize(ndof));
  target.position = limits.min_position;  // equal to zero position limit.
  target.velocity.setZero();

  // Initialize and run one iteration to initialize the reflexxes status.
  EXPECT_TRUE(generator.SetLimits(limits));
  EXPECT_TRUE(generator.SetTarget(target));
  EXPECT_TRUE(generator.SetPrevious(current));
  EXPECT_TRUE(generator.ComputeSetpoint(&current));

  const int kMaxIter = 100000;
  int iter = 0;
  while (generator.GetReflexxesStatus() !=
         reflexxes::Status::kFinalStateReached) {
    EXPECT_TRUE(generator.SetPrevious(current));
    EXPECT_TRUE(generator.ComputeSetpoint(&current));
    ++iter;
    if (iter > kMaxIter) FAIL() << "Max iterations reached";
  }
  EXPECT_THAT(current.position,
              IsApprox(limits.min_position, kDefaultPositionAccuracy));
}

// Test that a random motion starts at the initial state and ends at the target
// state.
TEST_F(JointPositionReflexxesTest, RandomSetup) {
  static constexpr size_t ndof = 5;
  static constexpr size_t ntrials = 5;
  static constexpr double frequency_hz = 100.0;
  static constexpr double dt = 1.0 / frequency_hz;
  eigenmath::VectorNb selection(ndof);
  selection.setConstant(true);
  for (size_t trial = 0; trial < ntrials; ++trial) {
    JointStatePVA initial;
    JointStatePV target;
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
TEST_F(JointPositionReflexxesTest, Selection) {
  static constexpr size_t ndof = 4;
  static constexpr size_t ntrials = 5;
  static constexpr double frequency_hz = 100.0;
  static constexpr double dt = 1.0 / frequency_hz;
  eigenmath::VectorNb selection(ndof);
  for (size_t trial = 0; trial < ntrials; ++trial) {
    JointStatePVA initial;
    JointStatePV target;
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

TEST_F(JointPositionReflexxesTest, FinalMotionBehavior) {
  static constexpr size_t ndof = 3;
  static constexpr size_t ntrials = 5;
  static constexpr size_t max_step = 10000;
  static constexpr double frequency_hz = 100.0;
  static constexpr double dt = 1.0 / frequency_hz;
  eigenmath::VectorNb selection(ndof);
  selection.setConstant(true);

  JointPositionReflexxes generator(ndof, frequency_hz);

  for (size_t trial = 0; trial < ntrials; ++trial) {
    JointStatePVA initial;
    JointStatePV target;
    JointLimits limits;
    RandomSetup(ndof, &initial, &target, &limits);

    Enclosure enclosure;
    ASSERT_TRUE(
        enclosure.Compute(dt, target, -limits.max_jerk, limits.max_jerk));

    ASSERT_TRUE(generator.SetLimits(limits));
    ASSERT_TRUE(generator.SetTarget(target));

    JointStatePVA setpoint = initial;
    size_t step = 0;
    for (/**/; step < max_step; ++step) {
      ASSERT_TRUE(generator.SetPrevious(setpoint));
      ASSERT_TRUE(generator.ComputeSetpoint(&setpoint));

      const auto rml_status = generator.GetReflexxesStatus();
      ASSERT_TRUE(reflexxes::Status::kFinalStateReached == rml_status ||
                  reflexxes::Status::kWorking == rml_status);

      // TODO(rphilipp) come up with a check against
      // kWorking when it should be
      // kFinalStateReached. The Enclosure is too wide
      // of a bound to be useful here. For now, just use
      // max_step to detect if the final state is never reached.
      if (reflexxes::Status::kFinalStateReached == rml_status) {
        ASSERT_EQ(1, enclosure.Contains(setpoint, selection, false));
        break;
      }
    }
    ASSERT_LT(step, max_step);
  }
}

TEST_F(JointPositionReflexxesTest, LogErrorNoAlloc) {
  constexpr size_t ndof = 3;
  constexpr double frequency_hz = 100.0;
  JointStatePVA initial;
  JointStatePV target;
  JointLimits limits;
  RandomSetup(ndof, &initial, &target, &limits);
  target.velocity[0] = 10.0 * limits.max_velocity[0];  // make it invalid

  JointPositionReflexxes generator(ndof, frequency_hz);
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

TEST_F(JointPositionReflexxesTest, LimitErrorChecking) {
  JointLimits limits;
  ASSERT_OK(limits.SetSize(1));
  limits.min_position[0] = -1.0;
  limits.max_position[0] = 1.0;
  limits.max_velocity[0] = 1.0;
  limits.max_acceleration[0] = 1.0;
  limits.max_jerk[0] = 1.0;

  JointPositionReflexxes generator(1, 100.0);

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

  limits.max_velocity[0] = 0.0;
  ASSERT_FALSE(generator.SetLimits(limits));
  limits.max_velocity[0] = -0.01;
  ASSERT_FALSE(generator.SetLimits(limits));
  limits.max_velocity[0] = 1.0;
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

// Test that motion along starting and stopping exactly on one limit works.
TEST_F(JointPositionReflexxesTest, TestMoveAlongBoundary) {
  const size_t ndof = 6;
  JointStatePVA initial;
  // This data is from a failure case hit on robot.
  ASSERT_OK(initial.SetSize(ndof));
  initial.position << 0.000000000000000000000000000000000e+00,
      -1.570799999999999974065190144756343e+00,
      0.000000000000000000000000000000000e+00,
      2.920000000000000373034936274052598e+00,
      1.570799999999999974065190144756343e+00,
      -1.570799999999999974065190144756343e+00;
  initial.velocity << 0.000000000000000000000000000000000e+00,
      0.000000000000000000000000000000000e+00,
      0.000000000000000000000000000000000e+00,
      -5.551115123125782702118158340454102e-17,
      0.000000000000000000000000000000000e+00,
      0.000000000000000000000000000000000e+00;
  initial.acceleration << 0.000000000000000000000000000000000e+00,
      0.000000000000000000000000000000000e+00,
      0.000000000000000000000000000000000e+00,
      -1.000002747275630099466070532798767e-09,
      0.000000000000000000000000000000000e+00,
      0.000000000000000000000000000000000e+00;

  JointStatePV target;
  ASSERT_OK(target.SetSize(ndof));
  target.position << 0.0, -1.5708, 0.0, -2.92, 1.5708, -1.5708;
  target.velocity.setZero();

  JointLimits limits;
  ASSERT_OK(limits.SetSize(ndof));
  limits.max_position << 1.35, 3.18, 3.80, 2.92, 1.79, 2.92;
  limits.min_position << -4.49, -2.66, -2.05, -2.92, -1.79, -2.92;
  limits.max_velocity.setConstant(1);
  limits.max_acceleration.setConstant(2);
  limits.max_jerk.setConstant(10);

  double frequency_hz = 250.0;
  auto trajectory =
      GetJointTrajectory(ndof, frequency_hz, initial, limits, target);

  EXPECT_GT(trajectory.size(), 200);
  for (size_t dof = 0; dof < ndof; dof++) {
    EXPECT_DOUBLE_EQ(trajectory.back().position[dof], target.position[dof]);
    EXPECT_DOUBLE_EQ(trajectory.back().velocity[dof], target.velocity[dof]);
  }

  CheckDifferentialConsistency(trajectory, frequency_hz,
                               kDefaultVelocityAccuracy,
                               kDefaultAccelerationAccuracy);
}

// Test that a simple motion (with very high limits) is completed in the
// requested duration and the target point is reached.
TEST_F(JointPositionReflexxesTest, TestWithNonzeroUniformTargetVelocity) {
  const size_t ndof = 4;
  JointStatePVA initial;
  ASSERT_OK(initial.SetSize(ndof));
  initial.position.setZero();
  initial.velocity.setZero();
  initial.acceleration.setZero();

  JointStatePV target;
  ASSERT_OK(target.SetSize(ndof));
  target.position << 1.0, 2.0, 3.0, 4.0;
  target.velocity.setConstant(1.0);

  JointLimits limits;
  ASSERT_OK(limits.SetSize(ndof));
  limits.SetUnlimited();
  limits.max_velocity.setConstant(2.0);
  limits.max_acceleration.setConstant(10);
  limits.max_jerk.setConstant(100);

  const double frequency_hz = 200.0;
  std::vector<JointStatePVA> trajectory =
      GetJointTrajectory(ndof, frequency_hz, initial, limits, target);

  // Velocity target gets met exactly.
  EXPECT_LT((trajectory.back().velocity - target.velocity).norm(), 1e-6);
  // Position target gets met exactly.
  EXPECT_LT((trajectory.back().position - target.position).norm(), 1e-6);
}

// Returns true if the trajectory is numerically validated as homothetic, false
// otherwise.
bool IsHomothethic(const std::vector<JointStatePVA>& trajectory) {
  const auto& p0 = trajectory.front().position;
  const auto& pt = trajectory.back().position;
  const eigenmath::VectorNd delta_p_normalized = (pt - p0).normalized();

  // intermediate positions should be collinear with direct connection between
  // initial and target position.
  for (int i = 1; i < trajectory.size() - 1; i++) {
    if (std::abs(std::abs((trajectory[i].position - p0)
                              .normalized()
                              .dot(delta_p_normalized)) -
                 1.0) > 1e-4) {
      LOG(INFO) << "Position trajectory deviates from homothethic trajectory";
      return false;
    }
  }

  // intermediate non-zero velocities should be collinear with direct connection
  // between initial and target position.
  for (int i = 0; i < trajectory.size(); i++) {
    if (trajectory[i].velocity.norm() > 1e-3) {  // skip zero velocities
      if (std::abs(std::abs(trajectory[i].velocity.normalized().dot(
                       delta_p_normalized)) -
                   1.0) > 1e-3) {
        LOG(INFO) << "Velocity trajectory deviates from homothethic trajectory";
        return false;
      }
    }
  }

  return true;
}

void CheckLimits(const std::vector<JointStatePVA>& trajectory,
                 const JointLimits& limits) {
  for (const auto& el : trajectory) {
    INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(auto limit_check_result,
                                      IsWithinLimits(el, limits));
    if (!limit_check_result.v_ok) {
      LOG(INFO) << el.velocity.transpose() << " vs "
                << limits.max_velocity.transpose();
    }
    if (!limit_check_result.a_ok) {
      LOG(INFO) << el.acceleration.transpose() << " vs "
                << limits.max_acceleration.transpose();
    }
    ASSERT_TRUE(limit_check_result.v_ok);
    ASSERT_TRUE(limit_check_result.a_ok);
  }
}

TEST_F(JointPositionReflexxesTest,
       TestHomotheticTrajectoryWithNonUniformVelocityLimits) {
  const double frequency_hz = 1000.0;
  const size_t ndof = 4;
  JointStatePVA initial;
  ASSERT_OK(initial.SetSize(ndof));
  JointStatePV target;
  ASSERT_OK(target.SetSize(ndof));

  // Create a test setup in which the vector from initial to target position and
  // the velocities are collinear.
  initial.position << 1, 0, -1, 1;
  const double c = 0.5;
  target.position = initial.position + eigenmath::Vector4d{c, c, c, 2 * c};
  initial.velocity = target.position - initial.position;
  target.velocity = target.position - initial.position;

  JointLimits limits;
  ASSERT_OK(limits.SetSize(ndof));
  limits.SetUnlimited();
  limits.max_jerk.setConstant(100);
  limits.max_acceleration.setConstant(10.0);
  // ! non-uniform velocity limit !
  limits.max_velocity << 2, 3, 4, 1;

  auto trajectory =
      GetJointTrajectory(ndof, frequency_hz, initial, limits, target);

  // Resulting trajectory should be differentially consistent.
  CheckDifferentialConsistency(trajectory, frequency_hz,
                               kDefaultVelocityAccuracy,
                               kDefaultAccelerationAccuracy);

  // Resulting trajectories should be homothetic.
  EXPECT_TRUE(IsHomothethic(trajectory));

  // Velocity target gets met exactly.
  EXPECT_LT(
      (trajectory.back().velocity - target.velocity).cwiseAbs().maxCoeff(),
      1e-6);

  // Position target gets met approximately.
  EXPECT_LT(
      (trajectory.back().position - target.position).cwiseAbs().maxCoeff(),
      1e-3);
}

// This is a setup in which phase synchronization is currently not possible.
// Reflexxes is expected to fall back to standard time synchronization, without
// phase synchronization. We test this by expecting that the resulting
// trajectory violates homothety.
TEST_F(JointPositionReflexxesTest, TestFallbackToNonPhaseSynchronization) {
  const double frequency_hz = 1000.0;
  const size_t ndof = 2;
  JointStatePVA initial;
  ASSERT_OK(initial.SetSize(ndof));
  JointStatePV target;
  ASSERT_OK(target.SetSize(ndof));

  // Create a test setup in which the vector from initial to target position and
  // the velocities are collinear.
  initial.position.setZero();
  initial.velocity.setZero();
  initial.acceleration.setZero();
  target.position << 2, 3;
  target.velocity << 0.2, 0.3;

  JointLimits limits;
  ASSERT_OK(limits.SetSize(ndof));
  limits.SetUnlimited();
  limits.max_velocity << 2.4, 40;  // Make velocity limit strongly non uniform,
                                   // thus breaking the phase synchronization.
  limits.max_acceleration << 20, 20;
  limits.max_jerk << 40, 40;

  auto trajectory =
      GetJointTrajectory(ndof, frequency_hz, initial, limits, target);

  // Resulting trajectory should be differentially consistent.
  CheckDifferentialConsistency(trajectory, frequency_hz,
                               kDefaultVelocityAccuracy,
                               kDefaultAccelerationAccuracy);

  // Resulting trajectories should not be homothetic, since we expect the
  // fallback to normal time synchronization to happen.
  EXPECT_FALSE(IsHomothethic(trajectory));

  CheckLimits(trajectory, limits);

  // Velocity target gets met exactly.
  EXPECT_LT(
      (trajectory.back().velocity - target.velocity).cwiseAbs().maxCoeff(),
      1e-6);

  // Position target gets met approximately.
  EXPECT_LT(
      (trajectory.back().position - target.position).cwiseAbs().maxCoeff(),
      1e-3);
}

// This test covers an edge-case previously reported as failing (phase
// synchronization error, b/231425833).
TEST_F(JointPositionReflexxesTest, EdgeCaseWithPhaseSyncFailureWorks) {
  const double frequency_hz = 1000.0;
  const size_t ndof = 6;
  JointStatePVA initial;
  ASSERT_OK(initial.SetSize(ndof));
  initial.position << 0.512126766830390267593032, 0.000248467669471237552394,
      0.415231693782203925557894, 0.000000000000000000000000,
      0.000000000000000000000000, 0.000000000230099527973038;
  initial.velocity << 0.000000000000000000000000, 0.000000000000000000000000,
      0.000000000000000000000000, -0.000000000000000000057657,
      0.000000000000000002509105, 0.000000046134314893396959;
  initial.acceleration.setZero();

  JointStatePV target;
  ASSERT_OK(target.SetSize(ndof));
  target.position.setZero();
  target.position[0] = initial.position[0];
  target.position[1] = 0.100248467669471319485552;
  target.position[2] = initial.position[2];
  target.velocity.setZero();

  JointLimits limits;
  ASSERT_OK(limits.SetSize(ndof));
  limits.SetUnlimited();
  limits.max_velocity.setConstant(0.299999999999999988897770);
  limits.max_acceleration.setConstant(1.500000000000000000000000);
  limits.max_jerk.setConstant(300.0);

  auto trajectory =
      GetJointTrajectory(ndof, frequency_hz, initial, limits, target);

  // Resulting trajectory should be differentially consistent.
  CheckDifferentialConsistency(trajectory, frequency_hz,
                               kDefaultVelocityAccuracy,
                               kDefaultAccelerationAccuracy);

  CheckLimits(trajectory, limits);

  // Velocity target gets met exactly.
  EXPECT_LT(
      (trajectory.back().velocity - target.velocity).cwiseAbs().maxCoeff(),
      1e-6);

  // Position target gets met approximately.
  EXPECT_LT(
      (trajectory.back().position - target.position).cwiseAbs().maxCoeff(),
      1e-3);
}

TEST_F(JointPositionReflexxesTest, BehaviorIfInitialStateBreachesConstraints) {
  // Test parameters.
  const double kMaxVel = 0.5;
  const size_t kNDofs = 2;
  const double kTolerance = 0.001;
  const double kFrequencyHz = 1000.0;
  const int kNumControllerCycles = 4;

  // Initial state with velocity outside the velocity limits.
  JointStatePVA initial;
  ASSERT_OK(initial.SetSize(kNDofs));
  initial.position.setZero();
  initial.velocity.setConstant(2 * kMaxVel);

  // Target state with zero goal velocity and goal position within limits.
  JointStatePV target;
  ASSERT_OK(target.SetSize(kNDofs));
  target.position.setOnes();
  target.velocity.setZero();

  // Definition of joint limits
  JointLimits limits;
  ASSERT_OK(limits.SetSize(kNDofs));
  limits.min_position.setConstant(-10);
  limits.max_position.setConstant(10);
  limits.max_velocity.setConstant(kMaxVel);
  limits.max_acceleration.setConstant(1000000);
  limits.max_jerk.setConstant(1000000);

  JointPositionReflexxes generator(kNDofs, kFrequencyHz);
  EXPECT_TRUE(generator.SetLimits(limits));
  EXPECT_TRUE(generator.SetTarget(target));
  generator.SetPositionalLimitsBehavior(
      reflexxes::Flags::PositionalLimitsBehavior::kErrorMsgOnly);

  // Case 1 - GetIntoBoundariesAtZeroAcceleration:  Tests that the velocity
  // remains inside limits given by initial value (2 * kMaxVel) and max velocity
  // (kMaxVel).
  {
    SCOPED_TRACE("Case 1: GetIntoBoundariesAtZeroAcceleration");
    generator.SetBehaviorIfInitialStateBreachesConstraints(
        reflexxes::PositionFlags::BehaviorIfInitialStateBreachesConstraints::
            kGetIntoBoundariesAtZeroAcceleration);

    bool get_into_boundaries_at_zero_acceleration_test_passes = true;
    JointStatePVA current = initial;
    for (int i = 0; i < kNumControllerCycles; ++i) {
      EXPECT_TRUE(generator.SetPrevious(current));
      EXPECT_TRUE(generator.ComputeSetpoint(&current));
      for (int j = 0; j < kNDofs; ++j) {
        get_into_boundaries_at_zero_acceleration_test_passes &=
            ((current.velocity[j] >= kMaxVel - kTolerance) &&
             (current.velocity[j] <= 2.0 * kMaxVel + kTolerance));
      }
    }
    EXPECT_TRUE(get_into_boundaries_at_zero_acceleration_test_passes);
  }

  // Case 2 - GetIntoBoundariesFast: Tests that the velocity goes outside limits
  // given by initial value (2 * kMaxVel) and max velocity (kMaxVel).
  {
    SCOPED_TRACE("Case 2: GetIntoBoundariesFast");
    generator.SetBehaviorIfInitialStateBreachesConstraints(
        reflexxes::PositionFlags::BehaviorIfInitialStateBreachesConstraints::
            kGetIntoBoundariesFast);

    bool get_into_boundaries_fast_test_passes = true;
    JointStatePVA current = initial;
    for (int i = 0; i < kNumControllerCycles; ++i) {
      EXPECT_TRUE(generator.SetPrevious(current));
      EXPECT_TRUE(generator.ComputeSetpoint(&current));
      for (int j = 0; j < kNDofs; ++j) {
        get_into_boundaries_fast_test_passes &=
            ((current.velocity[j] >= kMaxVel - kTolerance) &&
             (current.velocity[j] <= 2.0 * kMaxVel + kTolerance));
      }
    }
    EXPECT_FALSE(get_into_boundaries_fast_test_passes);
  }
}

TEST_F(JointPositionReflexxesTest,
       ExpectedTimeSynchronizedBehaviorUnderCollinearInputs) {
  // A reflexxes-based position trajectory generator can be configured to
  // generate trajectories with two behaviors when the constraints are
  // initially violated, namely kGetIntoBoundariesAtZeroAcceleration and
  // kGetIntoBoundariesFast. With kGetIntoBoundariesAtZeroAcceleration it is
  // expected that the target is reached without overshooting, while with
  // kGetIntoBoundariesFast the target is reached as fast as possible with
  // overshooting.
  // A workaround for the case of collinear inputs for which phase
  // synchronization produces trajectories that breach positional limits has
  // been implemented within the reflexxes api, where in case a phase
  // synchronized solution for collinear inputs generates a solution the
  // violates positional limits, only a time synchronized solution is computed
  // instead.

  // Test parameters.
  const size_t kNDofs = 2;
  const double kFrequencyHz = 100.0;
  const int kNumControllerCycles = 60;
  const double kTolerance = 0.01;
  const double kMaxVel = 0.1;

  // Initial state with velocity outside the velocity limits.
  JointStatePVA initial;
  ASSERT_OK(initial.SetSize(kNDofs));
  initial.position.setConstant(1.27639);
  initial.velocity.setConstant(0.2);
  initial.acceleration.setZero();

  // Target state with zero goal velocity and goal position within limits.
  JointStatePV target;
  ASSERT_OK(target.SetSize(kNDofs));
  target.position.setConstant(2.0);
  target.velocity.setConstant(0.0);

  // Definition of joint limits.
  JointLimits limits;
  ASSERT_OK(limits.SetSize(kNDofs));
  limits.min_position.setConstant(-3);
  limits.max_position.setConstant(3);
  limits.max_velocity.setConstant(kMaxVel);
  limits.max_acceleration.setConstant(5);
  limits.max_jerk.setConstant(20);

  // Setup the reflexxes-based trajectory generator.
  JointPositionReflexxes generator(kNDofs, kFrequencyHz);
  EXPECT_TRUE(generator.SetLimits(limits));
  EXPECT_TRUE(generator.SetTarget(target));
  generator.SetPositionalLimitsBehavior(
      reflexxes::Flags::PositionalLimitsBehavior::kActivelyPrevent);

  // Case 1 - GetIntoBoundariesAtZeroAcceleration:  Tests that the velocity
  // remains inside limits given by initial value (2 * kMaxVel) and max velocity
  // (kMaxVel).
  {
    SCOPED_TRACE("Case 1: GetIntoBoundariesAtZeroAcceleration");
    generator.SetBehaviorIfInitialStateBreachesConstraints(
        reflexxes::PositionFlags::BehaviorIfInitialStateBreachesConstraints::
            kGetIntoBoundariesAtZeroAcceleration);
    // Loop a given number of control cycles `kNumControllerCycles` to observe
    // the behavior. `current` stores the position, velocity and acceleration
    // trajectories.
    JointStatePVA current = initial;
    bool behavior_get_into_boundaries_zero_acc = true;
    for (int i = 0; i < kNumControllerCycles; ++i) {
      EXPECT_TRUE(generator.SetPrevious(current));
      EXPECT_TRUE(generator.ComputeSetpoint(&current));

      for (int j = 0; j < kNDofs; ++j) {
        behavior_get_into_boundaries_zero_acc &=
            ((current.velocity[j] >= kMaxVel - kTolerance) &&
             (current.velocity[j] <= 2.0 * kMaxVel + kTolerance));
      }
    }
    EXPECT_TRUE(behavior_get_into_boundaries_zero_acc);
  }

  // Case 2 - GetIntoBoundariesFast: Tests that the velocity goes outside limits
  // given by initial value (2 * kMaxVel) and max velocity (kMaxVel).
  {
    SCOPED_TRACE("Case 2: GetIntoBoundariesFast");
    generator.SetBehaviorIfInitialStateBreachesConstraints(
        reflexxes::PositionFlags::BehaviorIfInitialStateBreachesConstraints::
            kGetIntoBoundariesFast);

    // Loop a given number of control cycles `kNumControllerCycles` to observe
    // the behavior. `current` stores the position, velocity and acceleration
    // trajectories.
    JointStatePVA current = initial;
    bool behavior_get_into_boundaries_fast = true;
    for (int i = 0; i < kNumControllerCycles; ++i) {
      EXPECT_TRUE(generator.SetPrevious(current));
      EXPECT_TRUE(generator.ComputeSetpoint(&current));

      for (int j = 0; j < kNDofs; ++j) {
        behavior_get_into_boundaries_fast &=
            ((current.velocity[j] >= kMaxVel - kTolerance) &&
             (current.velocity[j] <= 2.0 * kMaxVel + kTolerance));
      }
    }
    EXPECT_FALSE(behavior_get_into_boundaries_fast);
  }
}

}  // namespace
}  // namespace icon
}  // namespace intrinsic
