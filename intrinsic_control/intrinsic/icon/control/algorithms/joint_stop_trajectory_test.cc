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

#include "intrinsic/icon/control/algorithms/joint_stop_trajectory.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>

#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/testing/malloc_test.h"

namespace intrinsic {
namespace control {
namespace test {

TEST(JointStopTrajectoryTest, Initialization) {
  JointStopTrajectory traj;

  EXPECT_FALSE(traj.Init(-1));
  EXPECT_FALSE(traj.Init(0));
  EXPECT_TRUE(traj.Init(1));

  EXPECT_EQ(1, traj.NumDoFs());

  eigenmath::VectorNd acc(2);
  // wrong size
  EXPECT_FALSE(traj.SetAcceleration(acc));
  acc.resize(1);
  // not >0
  acc[0] = 0;
  EXPECT_FALSE(traj.SetAcceleration(acc));
  acc[0] = -1;
  EXPECT_FALSE(traj.SetAcceleration(acc));
}

// Invalid arguments, good initialization
TEST(JointStopTrajectoryTest, InvalidArguments) {
  JointStopTrajectory traj;
  EXPECT_TRUE(traj.Init(2));
  eigenmath::VectorNd acc(2);
  acc[0] = 2;
  acc[1] = 1;
  EXPECT_TRUE(traj.SetAcceleration(acc));
  double time{};

  // initial conditions not set, expect error
  EXPECT_FALSE(traj.GetStopPositionAndTime(nullptr, nullptr));

  // wrong size
  eigenmath::VectorNd position(4);
  eigenmath::VectorNd velocity(2);
  EXPECT_FALSE(traj.SetInitialConditions(position, velocity, time));
  position.resize(2);
  velocity.resize(4);
  EXPECT_FALSE(traj.SetInitialConditions(position, velocity, time));

  // OK
  velocity.resize(2);
  velocity.setZero();
  position.setZero();
  ASSERT_TRUE(traj.SetInitialConditions(position, velocity, time));

  // time in the past
  eigenmath::VectorNd vec(2);
  EXPECT_FALSE(traj.GetPositionAtTime(time - 1.0, &vec));
  EXPECT_FALSE(traj.GetVelocityAtTime(time - 1.0, &vec));
  EXPECT_FALSE(traj.GetAccelerationAtTime(time - 1.0, &vec));
  // nullptr
  EXPECT_FALSE(traj.GetPositionAtTime(time + 1.0, nullptr));
  EXPECT_FALSE(traj.GetVelocityAtTime(time + 1.0, nullptr));
  EXPECT_FALSE(traj.GetAccelerationAtTime(time + 1.0, nullptr));

  // wrong sizes
  eigenmath::VectorNd times(1);
  EXPECT_FALSE(traj.GetStopPositionAndTime(&position, &times));
  position.resize(1);
  times.resize(2);
  EXPECT_FALSE(traj.GetStopPositionAndTime(&position, &times));
}

// Normal case, verify math
TEST(JointStopTrajectoryTest, NormalCase) {
  JointStopTrajectory traj;
  EXPECT_TRUE(traj.Init(2));

  eigenmath::VectorNd acc(2);
  acc[0] = 2;
  acc[1] = 1;
  EXPECT_TRUE(traj.SetAcceleration(acc));
  eigenmath::VectorNd initial_position(2);
  eigenmath::VectorNd initial_velocity(2);
  initial_position[0] = 1.1;
  initial_velocity[0] = 1.2;
  initial_position[1] = 2.1;
  initial_velocity[1] = -2.2;

  eigenmath::VectorNd position(2);
  eigenmath::VectorNd velocity(2);
  eigenmath::VectorNd acceleration(2);
  double time = 0.0;

  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  traj.SetInitialConditions(initial_position, initial_velocity, time);
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();

  eigenmath::VectorNd stop_times(2);
  ASSERT_TRUE(traj.GetStopPositionAndTime(&position, &stop_times));
  time = 0.8 * stop_times.minCoeff();  // smaller than smallest stop time

  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  EXPECT_TRUE(traj.GetPositionAtTime(time, &position));
  EXPECT_TRUE(traj.GetVelocityAtTime(time, &velocity));
  EXPECT_TRUE(traj.GetAccelerationAtTime(time, &acceleration));
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();

  std::array<double, 2> expected_position = {
      {initial_position[0] + initial_velocity[0] * time -
           acc[0] * 0.5 * time * time,
       initial_position[1] + initial_velocity[1] * time +
           acc[1] * 0.5 * time * time}};
  std::array<double, 2> expected_velocity = {
      {initial_velocity[0] - acc[0] * time,
       initial_velocity[1] + acc[1] * time}};
  std::array<double, 2> expected_acceleration = {{-acc[0], acc[1]}};

  EXPECT_DOUBLE_EQ(position[0], expected_position[0]);
  EXPECT_DOUBLE_EQ(velocity[0], expected_velocity[0]);
  EXPECT_DOUBLE_EQ(acceleration[0], expected_acceleration[0]);

  EXPECT_DOUBLE_EQ(position[1], expected_position[1]);
  EXPECT_DOUBLE_EQ(velocity[1], expected_velocity[1]);
  EXPECT_DOUBLE_EQ(acceleration[1], expected_acceleration[1]);

  eigenmath::VectorNd times(2);
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  traj.GetStopPositionAndTime(&position, &times);
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();

  std::array<double, 2> expected_times = {
      {initial_velocity[0] / acc[0], -initial_velocity[1] / acc[1]}};

  expected_position = {
      {initial_position[0] + 0.5 * std::pow(initial_velocity[0], 2) / acc[0],
       initial_position[1] - 0.5 * std::pow(initial_velocity[1], 2) / acc[1]}};

  EXPECT_DOUBLE_EQ(times[0], expected_times[0]);
  EXPECT_DOUBLE_EQ(times[1], expected_times[1]);

  EXPECT_DOUBLE_EQ(position[0], expected_position[0]);
  EXPECT_DOUBLE_EQ(position[1], expected_position[1]);

  // anything past final stop time should agree with stopping position
  double stop_time;
  EXPECT_TRUE(traj.GetFinalStopTime(&stop_time));

  EXPECT_TRUE(traj.GetPositionAtTime(stop_time + 1, &position));
  EXPECT_TRUE(traj.GetVelocityAtTime(stop_time + 1, &velocity));
  EXPECT_TRUE(traj.GetAccelerationAtTime(stop_time + 1, &acceleration));
  EXPECT_DOUBLE_EQ(position[0], expected_position[0]);
  EXPECT_DOUBLE_EQ(position[1], expected_position[1]);
  EXPECT_DOUBLE_EQ(velocity[0], 0.0);
  EXPECT_DOUBLE_EQ(velocity[1], 0.0);
  EXPECT_DOUBLE_EQ(acceleration[0], 0.0);
  EXPECT_DOUBLE_EQ(acceleration[1], 0.0);

  // check different stopping times are handled correctly
  // check test assumptions
  ASSERT_LE(stop_times.minCoeff(), stop_times.maxCoeff());
  ASSERT_EQ(stop_times.minCoeff(), stop_times[0]);

  time = 0.2 * stop_times.minCoeff() + 0.8 * stop_times.maxCoeff();
  const double time2 = time * time;
  expected_position[1] =
      initial_position[1] + initial_velocity[1] * time + acc[1] * 0.5 * time2;
  expected_velocity[1] = initial_velocity[1] + acc[1] * time;
  expected_acceleration[1] = acc[1];

  EXPECT_TRUE(traj.GetPositionAtTime(time, &position));
  EXPECT_TRUE(traj.GetVelocityAtTime(time, &velocity));
  EXPECT_TRUE(traj.GetAccelerationAtTime(time, &acceleration));

  EXPECT_DOUBLE_EQ(position[0], expected_position[0]);
  EXPECT_DOUBLE_EQ(position[1], expected_position[1]);
  EXPECT_DOUBLE_EQ(velocity[0], 0.0);
  EXPECT_DOUBLE_EQ(velocity[1], expected_velocity[1]);
  EXPECT_DOUBLE_EQ(acceleration[0], 0.0);
  EXPECT_DOUBLE_EQ(acceleration[1], expected_acceleration[1]);
}
}  // namespace test
}  // namespace control
}  // namespace intrinsic
