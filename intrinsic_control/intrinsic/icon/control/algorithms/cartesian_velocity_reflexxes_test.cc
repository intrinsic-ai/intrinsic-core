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

#include "intrinsic/icon/control/algorithms/cartesian_velocity_reflexxes.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <limits>
#include <random>
#include <vector>

#include "Eigen/Core"
#include "finite_difference_test.h"
#include "intrinsic/eigenmath/manifolds.h"
#include "intrinsic/eigenmath/matchers.h"
#include "intrinsic/eigenmath/so3.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/reflexxes/constants_external.h"
#include "intrinsic/icon/reflexxes/status.h"
#include "intrinsic/icon/testing/malloc_test.h"
#include "intrinsic/kinematics/types/cart_state.h"
#include "intrinsic/kinematics/types/cartesian_limits.h"
#include "intrinsic/math/pose3.h"

using ::intrinsic::eigenmath::testing::IsApprox;
using ::testing::SizeIs;

constexpr double kAlmostZero = 1.5e-12;

namespace intrinsic {
namespace icon {
namespace {

class CartesianVelocityReflexxesTest : public ::testing::Test {
 public:
  // A single function to call the setpoint generator for a given set of inputs
  // (this also EXPECTs the lack of errors, and ensures no memory allocations
  std::vector<CartStatePVA> GetCartesianTrajectory(
      double frequency_hz, const CartStatePVA& start,
      const CartesianLimits& limits, const CartStateV& target) {
    CartesianVelocityReflexxes generator(frequency_hz);
    std::vector<CartStatePVA> trajectory;
    CartStatePVA previous = start;
    trajectory.push_back(start);
    do {
      std::vector<bool> rets(2);
      IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
      EXPECT_TRUE(rets[0] = generator.SetLimits(limits));
      generator.SetTarget(target);
      generator.SetPrevious(previous);

      EXPECT_TRUE(rets[1] = generator.ComputeSetpoint(&previous));
      IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();

      if ((std::count(rets.begin(), rets.end(), false) > 0) ||
          ((generator.GetReflexxesStatus() !=
            reflexxes::Status::kFinalStateReached) &&
           (generator.GetReflexxesStatus() != reflexxes::Status::kWorking))) {
        return std::vector<CartStatePVA>();
      }

      trajectory.push_back(previous);
    } while (generator.GetReflexxesStatus() !=
             reflexxes::Status::kFinalStateReached);

    return trajectory;
  }

  // generate random pose
  Pose3d RandomPose3d() {
    eigenmath::Vector3d axis;
    double length = 0;
    do {
      axis =
          eigenmath::Vector3d(rand_dir_(rand_engine_), rand_dir_(rand_engine_),
                              rand_dir_(rand_engine_));
      length = axis.norm();
    } while (length < 0.01);
    axis = axis / length;
    eigenmath::Matrix3d rotation(
        Eigen::AngleAxisd(rand_dir_(rand_engine_) * M_PI, axis).matrix());
    eigenmath::Vector3d translation(rand_dir_(rand_engine_) * 10,
                                    rand_dir_(rand_engine_) * 10,
                                    rand_dir_(rand_engine_) * 10);
    return Pose3d(rotation, translation);
  }
  eigenmath::Vector6d RandomVelocity() {
    const double vscale = 10;
    eigenmath::Vector6d vel;
    vel[0] = vscale * rand_dir_(rand_engine_);
    vel[1] = vscale * rand_dir_(rand_engine_);
    vel[2] = vscale * rand_dir_(rand_engine_);
    vel[3] = vscale * rand_dir_(rand_engine_);
    vel[4] = vscale * rand_dir_(rand_engine_);
    vel[5] = vscale * rand_dir_(rand_engine_);
    return vel;
  }

  // generate random limits
  CartesianLimits RandomLimits() {
    CartesianLimits limits;
    const double ascale = 100;
    const double jscale = 1000;
    limits.max_translational_position.setConstant(
        reflexxes::kMaxPositionalLimit);
    limits.min_translational_position.setConstant(
        -reflexxes::kMaxPositionalLimit);

    limits.max_translational_acceleration[0] = ascale * rand_pos_(rand_engine_);
    limits.max_translational_acceleration[1] = ascale * rand_pos_(rand_engine_);
    limits.max_translational_acceleration[2] = ascale * rand_pos_(rand_engine_);
    limits.min_translational_acceleration =
        -limits.max_translational_acceleration;

    limits.max_translational_jerk[0] = jscale * rand_pos_(rand_engine_);
    limits.max_translational_jerk[1] = jscale * rand_pos_(rand_engine_);
    limits.max_translational_jerk[2] = jscale * rand_pos_(rand_engine_);
    limits.min_translational_jerk = -limits.max_translational_jerk;

    limits.max_rotational_acceleration = ascale * rand_pos_(rand_engine_);
    limits.max_rotational_jerk = jscale * rand_pos_(rand_engine_);

    return limits;
  }

  // generate symmetric, uniform limits
  CartesianLimits SymmetricUniformRandomLimits() {
    CartesianLimits limits;
    const double vscale = 20;
    const double ascale = 100;
    const double jscale = 1000;

    limits.max_translational_position.setConstant(
        reflexxes::kMaxPositionalLimit);
    limits.min_translational_position.setConstant(
        -reflexxes::kMaxPositionalLimit);

    const double max_vel = vscale * rand_pos_(rand_engine_);
    limits.max_translational_velocity.setConstant(max_vel);
    limits.min_translational_velocity.setConstant(-max_vel);

    const double max_acc = ascale * rand_pos_(rand_engine_);
    limits.max_translational_acceleration.setConstant(max_acc);
    limits.min_translational_acceleration.setConstant(-max_acc);

    const double max_jerk = jscale * rand_pos_(rand_engine_);
    limits.max_translational_jerk.setConstant(max_jerk);
    limits.min_translational_jerk.setConstant(-max_jerk);

    limits.max_rotational_velocity = max_vel;
    limits.max_rotational_acceleration = max_acc;
    limits.max_rotational_jerk = max_jerk;

    return limits;
  }

 private:
  std::default_random_engine rand_engine_;
  std::uniform_real_distribution<double> rand_dir_ =
      std::uniform_real_distribution<double>(-1.0, 1.0);
  std::uniform_real_distribution<double> rand_pos_ =
      std::uniform_real_distribution<double>(0.01, 1.0);
};

// This case reproduces an error case that happened with CartesianJoggingAction,
// where a path that was clearly within limits reported a breach of positional
// limits.
TEST_F(CartesianVelocityReflexxesTest, ReproActionFailure) {
  CartesianLimits limits;

  limits.max_translational_position.setConstant(30);
  limits.min_translational_position.setConstant(-30);
  limits.max_translational_velocity.setConstant(0.05);
  limits.min_translational_velocity.setConstant(-0.05);
  limits.max_translational_acceleration.setConstant(100000000);
  limits.min_translational_acceleration.setConstant(-100000000);
  limits.max_translational_jerk.setConstant(10000000000);
  limits.min_translational_jerk.setConstant(-10000000000);
  limits.max_rotational_velocity = 0.05;
  limits.max_rotational_acceleration = 100000000;
  limits.max_rotational_jerk = 10000000000;

  CartStatePVA initial;
  initial.pose = Pose3d(eigenmath::Quaterniond::Identity(),
                        eigenmath::Vector3d(1.0, 0.0, 0.0));
  initial.velocity.setZero();
  initial.acceleration.setZero();

  CartStateV target;
  target.velocity.setZero();

  auto trajectory =
      GetCartesianTrajectory(/*frequency_hz=*/100, initial, limits, target);
  // We start at the goal, so the trajectory has 2 elements (the
  // initial pose and the result after running Reflexxes once).
  EXPECT_THAT(trajectory, SizeIs(2));
}

TEST_F(CartesianVelocityReflexxesTest, WorksWithInfiniteVAJLimits) {
  CartesianLimits limits;

  limits.max_translational_position.setConstant(30);
  limits.min_translational_position.setConstant(-30);

  constexpr double kInfinite = std::numeric_limits<double>::infinity();
  limits.max_translational_velocity.setConstant(kInfinite);
  limits.min_translational_velocity.setConstant(-kInfinite);
  limits.max_translational_acceleration.setConstant(kInfinite);
  limits.min_translational_acceleration.setConstant(-kInfinite);
  limits.max_translational_jerk.setConstant(kInfinite);
  limits.min_translational_jerk.setConstant(-kInfinite);
  limits.max_rotational_velocity = kInfinite;
  limits.max_rotational_acceleration = kInfinite;
  limits.max_rotational_jerk = kInfinite;

  CartStatePVA initial;
  initial.pose = Pose3d(eigenmath::Quaterniond::Identity(),
                        eigenmath::Vector3d(1.0, 0.0, 0.0));
  initial.velocity.setOnes();
  initial.acceleration.setOnes();

  CartStateV target;
  target.velocity.setZero();

  auto trajectory =
      GetCartesianTrajectory(/*frequency_hz=*/100, initial, limits, target);

  EXPECT_THAT(trajectory.back().velocity,
              IsApprox(eigenmath::Vector6d::Zero(), kAlmostZero));
}

// Test if checks for invalid input
TEST_F(CartesianVelocityReflexxesTest, TestInputErrors) {
  CartesianVelocityReflexxes generator(1000);
  CartesianLimits limits;

  limits.max_translational_position.setConstant(1);
  limits.min_translational_position.setConstant(-1);
  limits.max_translational_acceleration.setConstant(1);
  limits.min_translational_acceleration.setConstant(-1);
  limits.max_translational_jerk.setConstant(1);
  limits.min_translational_jerk.setConstant(-1);
  limits.max_rotational_acceleration = 1;
  limits.max_rotational_jerk = 1;

  EXPECT_TRUE(generator.SetLimits(limits));

  {
    CartesianLimits lmt = limits;
    lmt.max_translational_position[0] = -10;
    EXPECT_FALSE(generator.SetLimits(lmt));
  }

  {
    CartesianLimits lmt = limits;
    lmt.min_translational_position[0] = 10;
    EXPECT_FALSE(generator.SetLimits(lmt));
  }

  {
    CartesianLimits lmt = limits;
    lmt.max_translational_acceleration[0] = -1;
    EXPECT_FALSE(generator.SetLimits(lmt));
  }

  {
    CartesianLimits lmt = limits;
    lmt.min_translational_acceleration[0] = 1;
    EXPECT_FALSE(generator.SetLimits(lmt));
  }

  {
    CartesianLimits lmt = limits;
    lmt.max_translational_jerk[0] = -1;
    EXPECT_FALSE(generator.SetLimits(lmt));
  }

  {
    CartesianLimits lmt = limits;
    lmt.min_translational_jerk[0] = 1;
    EXPECT_FALSE(generator.SetLimits(lmt));
  }

  {
    CartesianLimits lmt = limits;
    lmt.max_rotational_acceleration = -1;
    EXPECT_FALSE(generator.SetLimits(lmt));
  }
  {
    CartesianLimits lmt = limits;
    lmt.max_rotational_jerk = -1;
    EXPECT_FALSE(generator.SetLimits(lmt));
  }
}

// Test that a simple motion (with very high limits on derivatives) is completed
// in the requested duration and the target point is reached.
TEST_F(CartesianVelocityReflexxesTest, TestSimpleTrajector) {
  CartStatePVA initial;
  initial.pose = Pose3d(
      Eigen::AngleAxisd(0.5 * M_PI, eigenmath::Vector3d::UnitX()).matrix(),
      eigenmath::Vector3d(1.0, 2.0, 3.0));
  initial.velocity = RandomVelocity();
  initial.acceleration.setZero();

  CartStateV target;
  target.velocity.setZero();

  CartesianLimits limits;
  limits.max_rotational_acceleration = 100;
  limits.max_rotational_jerk = 1000;

  limits.min_translational_position.setConstant(
      -reflexxes::kMaxPositionalLimit);
  limits.max_translational_position.setConstant(reflexxes::kMaxPositionalLimit);
  limits.min_translational_acceleration.setConstant(
      -limits.max_rotational_acceleration);
  limits.max_translational_acceleration.setConstant(
      limits.max_rotational_acceleration);
  limits.min_translational_jerk.setConstant(-limits.max_rotational_jerk);
  limits.max_translational_jerk.setConstant(limits.max_rotational_jerk);

  double frequency_hz = 1000.0;
  double dt_s = 1 / frequency_hz;
  auto trajectory =
      GetCartesianTrajectory(frequency_hz, initial, limits, target);

  double total_time = dt_s * trajectory.size();
  EXPECT_GT(0.2, total_time);
  EXPECT_LT(0.1, total_time);

  EXPECT_GT(1e-12, (trajectory.back().velocity - target.velocity).norm());
}

// Test that position limit breach is detected
TEST_F(CartesianVelocityReflexxesTest, TestPositionLimits) {
  CartStatePVA initial;
  initial.pose = Pose3d(
      Eigen::AngleAxisd(0.5 * M_PI, eigenmath::Vector3d::UnitX()).matrix(),
      eigenmath::Vector3d(-0.0001, -0.0001, -0.0001));
  initial.velocity << 1, 1, 1, 0, 0, 0;
  initial.acceleration.setZero();

  CartStateV target;
  target.velocity << 2, 2, 2, 0, 0, 0;

  CartesianLimits limits;
  limits.max_rotational_acceleration = 100;
  limits.max_rotational_jerk = 1000;

  limits.min_translational_position.setConstant(-0.1);
  limits.max_translational_position.setConstant(0.0);
  limits.min_translational_acceleration.setConstant(
      -limits.max_rotational_acceleration);
  limits.max_translational_acceleration.setConstant(
      limits.max_rotational_acceleration);
  limits.min_translational_jerk.setConstant(-limits.max_rotational_jerk);
  limits.max_translational_jerk.setConstant(limits.max_rotational_jerk);

  double frequency_hz = 1000.0;
  CartesianVelocityReflexxes generator(frequency_hz);
  CartStatePVA previous = initial;
  bool have_failure = false;
  do {
    generator.SetLimits(limits);
    generator.SetTarget(target);
    generator.SetPrevious(previous);

    if (!generator.ComputeSetpoint(&previous)) {
      have_failure = true;
      break;
    }
  } while (generator.GetReflexxesStatus() !=
           reflexxes::Status::kFinalStateReached);
  EXPECT_TRUE(have_failure);
}

// tests that stopping from constant velocity trajectory gives a straight line.
TEST_F(CartesianVelocityReflexxesTest, TestStopStraightLine) {
  constexpr double kAlmostZero = 1.5e-12;

  CartStatePVA initial_state;
  CartStateV target_state;
  CartesianLimits limits;

  for (size_t i = 0; i < 100; ++i) {
    initial_state.pose = RandomPose3d();
    initial_state.velocity = RandomVelocity();
    initial_state.acceleration.setZero();

    target_state.velocity.setZero();
    limits = SymmetricUniformRandomLimits();

    double frequency_hz = 1000.0;
    std::vector<CartStatePVA> trajectory = GetCartesianTrajectory(
        frequency_hz, initial_state, limits, target_state);

    ASSERT_LE(1, trajectory.size());
    const auto& last_velocity = trajectory.back().velocity;
    const auto& last_pose = trajectory.back().pose;
    // make sure we arrive at the target
    EXPECT_GE(kAlmostZero, (target_state.velocity - last_velocity).norm());
    EXPECT_GE(kAlmostZero, (initial_state.pose.translation() -
                            trajectory.front().pose.translation())
                               .norm());
    EXPECT_GE(kAlmostZero,
              (initial_state.pose.rotationMatrix() *
                   trajectory.front().pose.rotationMatrix().transpose() -
               eigenmath::Matrix3d::Identity())
                  .norm());
    EXPECT_GE(kAlmostZero,
              (initial_state.velocity - trajectory.front().velocity).norm());

    // check for straight line
    const eigenmath::Matrix3d Rstart =
        initial_state.pose.rotationMatrix().transpose();
    eigenmath::Vector3d start_axis_angle = intrinsic::eigenmath::logSO3(
        eigenmath::SO3d(Rstart * initial_state.pose.rotationMatrix()));

    EXPECT_NEAR(0, start_axis_angle.norm(), kAlmostZero);
    eigenmath::Vector3d end_axis_angle = intrinsic::eigenmath::logSO3(
        eigenmath::SO3d(Rstart * last_pose.rotationMatrix()));

    eigenmath::Vector3d start_to_end =
        last_pose.translation() - initial_state.pose.translation();
    eigenmath::Vector3d mid_omega =
        trajectory[trajectory.size() / 2].velocity.tail<3>();
    double end_dist = start_to_end.norm();

    for (const CartStatePVA& state : trajectory) {
      // current position is on line from start to end
      const eigenmath::Vector3d x_from_start =
          state.pose.translation() - initial_state.pose.translation();
      const eigenmath::Vector3d cross_pos = x_from_start.cross(start_to_end);
      EXPECT_GE(kAlmostZero, cross_pos.norm());
      // point doesn't overshoot
      EXPECT_TRUE(end_dist + kAlmostZero >= x_from_start.norm());
      const eigenmath::Vector3d vel = state.velocity.head<3>();
      //  velocity directed towards goal
      EXPECT_LE(0, start_to_end.dot(vel));
      // velocity colinear to straight line
      const eigenmath::Vector3d cross_vel = vel.cross(start_to_end);
      EXPECT_GE(kAlmostZero, cross_vel.norm());
      // same for accelerations
      const eigenmath::Vector3d accel = state.acceleration.head<3>();
      const eigenmath::Vector3d cross_accel = accel.cross(start_to_end);
      EXPECT_GE(kAlmostZero, cross_accel.norm());
      // rotation planar?
      const eigenmath::Vector3d x_axis_angle = intrinsic::eigenmath::logSO3(
          eigenmath::SO3d(Rstart * state.pose.rotationMatrix()));
      const eigenmath::Vector3d cross_axis_angle =
          x_axis_angle.cross(end_axis_angle);

      EXPECT_GE(kAlmostZero, cross_axis_angle.norm());
      // angular velocity in correct plane
      eigenmath::Vector3d omega = state.velocity.tail<3>();
      const eigenmath::Vector3d cross_omega = omega.cross(mid_omega);
      EXPECT_NEAR(0, cross_omega.norm(), kAlmostZero);

      // angular acceleration in correct plane
      const eigenmath::Vector3d alpha = state.acceleration.tail<3>();
      const eigenmath::Vector3d cross_alpha = alpha.cross(mid_omega);
      EXPECT_GE(kAlmostZero, cross_alpha.norm());
    }
  }
}

// Check that the angular velocity and orientation are kinematically consistent.
TEST_F(CartesianVelocityReflexxesTest, TestKinematicConsistency) {
  CartStatePVA initial_state;
  CartStateV target_state;
  CartesianLimits limits;

  for (size_t i = 0; i < 10; ++i) {
    initial_state.pose = RandomPose3d();
    initial_state.velocity = RandomVelocity();
    initial_state.acceleration = RandomVelocity();

    target_state.velocity = RandomVelocity();
    limits = RandomLimits();

    double frequency_hz =
        10000.0;  // use higher value for lower fd-approximation error
    double dt_s = 1 / frequency_hz;
    std::vector<CartStatePVA> trajectory = GetCartesianTrajectory(
        frequency_hz, initial_state, limits, target_state);

    ASSERT_LE(1, trajectory.size());

    control::test::FiniteDifferenceTest<eigenmath::Vector3d,
                                        eigenmath::Vector3d>
        fd_linear_velocity;
    control::test::FiniteDifferenceTest<eigenmath::Matrix3d,
                                        eigenmath::Vector3d>
        fd_angular_velocity;
    control::test::FiniteDifferenceTest<eigenmath::Vector3d,
                                        eigenmath::Vector3d>
        fd_linear_acceleration;
    control::test::FiniteDifferenceTest<eigenmath::Vector3d,
                                        eigenmath::Vector3d>
        fd_angular_acceleration;

    fd_linear_velocity.init("linear_velocity", dt_s);
    fd_angular_velocity.init("angular_velocity", dt_s);
    fd_linear_acceleration.init("linear_acceleration", dt_s);
    fd_angular_acceleration.init("angular_acceleration", dt_s);

    for (const CartStatePVA& state : trajectory) {
      // velocity vector layout is [translation;rotation]
      fd_linear_velocity.update(state.pose.translation(),
                                state.velocity.head<3>());
      fd_angular_velocity.update(state.pose.quaternion().toRotationMatrix(),
                                 state.velocity.tail<3>());
      // fd_linear_velocity.printCurrent();
      // fd_angular_velocity.printCurrent();

      fd_linear_acceleration.update(state.velocity.head<3>(),
                                    state.acceleration.head<3>());
      fd_angular_acceleration.update(state.velocity.tail<3>(),
                                     state.acceleration.tail<3>());
      // fd_linear_acceleration.printCurrent();
      // fd_angular_acceleration.printCurrent();
    }
    // fd_linear_velocity.printMaxError();
    // fd_angular_velocity.printMaxError();
    // fd_linear_acceleration.printMaxError();
    // fd_angular_acceleration.printMaxError();

    // This is really dependent on time step and trajectory.
    // A more complete test would check how this changes as dt decreases to
    // confirm error order of finite difference approximation ...
    EXPECT_LE(fd_linear_velocity.getMaxError(), 1e-5);
    EXPECT_LE(fd_linear_acceleration.getMaxError(), 1e-1);
    EXPECT_LE(fd_angular_velocity.getMaxError(), 1e-5);
    EXPECT_LE(fd_angular_acceleration.getMaxError(), 1e-1);
  }
}

TEST_F(CartesianVelocityReflexxesTest, LogErrorNoAlloc) {
  CartStatePVA initial;
  initial.velocity[0] = 10.0;  // overshoot due to insufficient min acceleration

  CartStateV target;

  CartesianLimits limits;
  limits.min_translational_position.setConstant(-0.1);
  limits.max_translational_position.setConstant(0.1);
  limits.min_translational_acceleration.setConstant(-1.0);
  limits.max_translational_acceleration.setConstant(1.0);
  limits.min_translational_jerk.setConstant(-1.0);
  limits.max_translational_jerk.setConstant(1.0);
  limits.max_rotational_acceleration = 1.0;
  limits.max_rotational_jerk = 1.0;

  CartesianVelocityReflexxes generator(100.0);
  CartStatePVA setpoint;

  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();

  ASSERT_TRUE(generator.SetLimits(limits));
  generator.SetTarget(target);
  generator.SetPrevious(initial);
  ASSERT_FALSE(generator.ComputeSetpoint(&setpoint));

  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
}

TEST_F(CartesianVelocityReflexxesTest,
       PhaseSynchronizationFailureRegressionTest) {
  CartStatePVA previous;
  previous.pose.translation() =
      eigenmath::Vector3d({0.0411626, -0.298335, 0.0970784});
  previous.pose.setQuaternion(
      eigenmath::Quaterniond({0.706014, 0.708198, 6.81593e-05, 9.87979e-08}));
  previous.velocity =
      eigenmath::Vector6d({-0.199921, 1.16609e-10, 1.36088e-10, 5.5953e-16,
                           4.71791e-16, 4.44089e-16});
  previous.acceleration =
      eigenmath::Vector6d({0.0394279, 3.38609e-05, 3.65983e-05, 5.60762e-08,
                           7.72073e-08, 1.8139e-12});

  CartStateV target;
  target.velocity.setZero();

  CartesianLimits limits;
  limits.min_translational_position.setConstant(-5);
  limits.max_translational_position.setConstant(5);
  limits.min_translational_velocity.setConstant(-0.2);
  limits.max_translational_velocity.setConstant(0.2);
  limits.min_translational_acceleration.setConstant(-5.0);
  limits.max_translational_acceleration.setConstant(5.0);
  limits.min_translational_jerk.setConstant(-10);
  limits.max_translational_jerk.setConstant(10);
  limits.max_rotational_acceleration = 5.0;
  limits.max_rotational_jerk = 10.0;
  limits.max_rotational_velocity = 0.3;

  CartesianVelocityReflexxes generator(500.0);

  generator.SetSelection(
      /*translation_selection=*/{true, true, true},
      /*rotation_selection=*/true);

  CartStatePVA setpoint;

  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();

  ASSERT_TRUE(generator.SetLimits(limits));
  generator.SetTarget(target);
  generator.SetPrevious(previous);
  ASSERT_TRUE(generator.ComputeSetpoint(&setpoint));

  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
}

TEST_F(CartesianVelocityReflexxesTest, limitErrorChecking) {
  CartesianLimits limits;
  limits.min_translational_position.setConstant(-1.0);
  limits.max_translational_position.setConstant(1.0);
  limits.min_translational_acceleration.setConstant(-1.0);
  limits.max_translational_acceleration.setConstant(1.0);
  limits.min_translational_jerk.setConstant(-1.0);
  limits.max_translational_jerk.setConstant(1.0);
  limits.max_rotational_acceleration = 1.0;
  limits.max_rotational_jerk = 1.0;

  CartesianVelocityReflexxes generator(100.0);

  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();

  ASSERT_TRUE(generator.SetLimits(limits));

  for (size_t dof = 0; dof < 3; ++dof) {
    limits.min_translational_position[dof] = 1.1;
    ASSERT_FALSE(generator.SetLimits(limits));

    limits.min_translational_position[dof] = -reflexxes::kMaxPositionalLimit;
    ASSERT_TRUE(generator.SetLimits(limits));

    limits.min_translational_position[dof] =
        -1.01 * reflexxes::kMaxPositionalLimit;
    ASSERT_TRUE(generator.SetLimits(limits));
    limits.min_translational_position[dof] = -1.0;
    ASSERT_TRUE(generator.SetLimits(limits));

    limits.max_translational_position[dof] = reflexxes::kMaxPositionalLimit;
    ASSERT_TRUE(generator.SetLimits(limits));
    limits.max_translational_position[dof] =
        1.01 * reflexxes::kMaxPositionalLimit;
    ASSERT_TRUE(generator.SetLimits(limits));
    limits.max_translational_position[dof] = -1.1;
    ASSERT_FALSE(generator.SetLimits(limits));
    limits.max_translational_position[dof] = 1.0;
    ASSERT_TRUE(generator.SetLimits(limits));

    limits.min_translational_acceleration[dof] = 0.0;
    ASSERT_FALSE(generator.SetLimits(limits));
    limits.min_translational_acceleration[dof] = 0.01;
    ASSERT_FALSE(generator.SetLimits(limits));
    limits.min_translational_acceleration[dof] = -1.0;
    ASSERT_TRUE(generator.SetLimits(limits));

    limits.max_translational_acceleration[dof] = 0.0;
    ASSERT_FALSE(generator.SetLimits(limits));
    limits.max_translational_acceleration[dof] = -0.01;
    ASSERT_FALSE(generator.SetLimits(limits));
    limits.max_translational_acceleration[dof] = 1.0;
    ASSERT_TRUE(generator.SetLimits(limits));

    limits.min_translational_jerk[dof] = 0.0;
    ASSERT_FALSE(generator.SetLimits(limits));
    limits.min_translational_jerk[dof] = 0.01;
    ASSERT_FALSE(generator.SetLimits(limits));
    limits.min_translational_jerk[dof] = -1.0;
    ASSERT_TRUE(generator.SetLimits(limits));

    limits.max_translational_jerk[dof] = 0.0;
    ASSERT_FALSE(generator.SetLimits(limits));
    limits.max_translational_jerk[dof] = -0.01;
    ASSERT_FALSE(generator.SetLimits(limits));
    limits.max_translational_jerk[dof] = 1.0;
    ASSERT_TRUE(generator.SetLimits(limits));
  }

  limits.max_rotational_acceleration = 0.0;
  ASSERT_FALSE(generator.SetLimits(limits));
  limits.max_rotational_acceleration = -0.01;
  ASSERT_FALSE(generator.SetLimits(limits));
  limits.max_rotational_acceleration = 1.0;
  ASSERT_TRUE(generator.SetLimits(limits));

  limits.max_rotational_jerk = 0.0;
  ASSERT_FALSE(generator.SetLimits(limits));
  limits.max_rotational_jerk = -0.01;
  ASSERT_FALSE(generator.SetLimits(limits));
  limits.max_rotational_jerk = 1.0;
  ASSERT_TRUE(generator.SetLimits(limits));

  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
}

}  // namespace
}  // namespace icon
}  // namespace intrinsic
