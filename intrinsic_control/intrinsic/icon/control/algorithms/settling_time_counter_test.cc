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

#include "intrinsic/icon/control/algorithms/settling_time_counter.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstddef>

#include "absl/status/status_matchers.h"
#include "internal/testing.h"
#include "intrinsic/icon/testing/malloc_test.h"
#include "intrinsic/kinematics/types/joint_state.h"
#include "intrinsic/math/twist.h"

constexpr int kNumDof = 7;
constexpr double kFrequencyHz = 100.0;
constexpr double kTranslationalVelocityErrorBand = 0.01;
constexpr double kRotationalVelocityErrorBand = 0.02;
constexpr double kJointVelocityErrorBand = 0.03;
constexpr int kNumTests = 100;

namespace intrinsic::icon {
namespace {

TEST(SettlingTimeCounterTest, ComputeSettlingTimeDoesNotMalloc) {
  // Create a zero joint state and twist.
  JointStateV joint_velocity;
  ASSERT_OK(joint_velocity.SetSize(kNumDof));
  joint_velocity.velocity.setZero();
  Twist twist = Twist::ZERO;

  SettlingTimeCounter counter(kFrequencyHz);
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  counter.ComputeSettlingTime(kTranslationalVelocityErrorBand,
                              kRotationalVelocityErrorBand,
                              kJointVelocityErrorBand, joint_velocity, twist);
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
}

TEST(SettlingTimeCounterTest, ResetDoesNotMalloc) {
  SettlingTimeCounter counter(kFrequencyHz);
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  counter.Reset();
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
}

TEST(SettlingTimeCounterTest, CountingWorks) {
  SettlingTimeCounter counter(kFrequencyHz);

  // Create a zero joint state and twist.
  JointStateV joint_velocity;
  ASSERT_OK(joint_velocity.SetSize(kNumDof));
  joint_velocity.velocity.setZero();
  Twist twist = Twist::ZERO;

  // Settling time should monotonically increase.
  for (size_t i = 0; i < kNumTests; i++) {
    double settling_time = counter.ComputeSettlingTime(
        kTranslationalVelocityErrorBand, kRotationalVelocityErrorBand,
        kJointVelocityErrorBand, joint_velocity, twist);

    EXPECT_NEAR(settling_time, static_cast<double>(i) / kFrequencyHz, 1e-6);
  }
}

TEST(SettlingTimeCounterTest, NonZeroJointVelocityResetsCounter) {
  SettlingTimeCounter counter(kFrequencyHz);

  // Create a zero joint state and twist.
  JointStateV joint_velocity;
  ASSERT_OK(joint_velocity.SetSize(kNumDof));
  joint_velocity.velocity.setZero();
  Twist twist = Twist::ZERO;

  // Settling time should monotonically increase.
  for (size_t i = 0; i < kNumTests; i++) {
    double settling_time = counter.ComputeSettlingTime(
        kTranslationalVelocityErrorBand, kRotationalVelocityErrorBand,
        kJointVelocityErrorBand, joint_velocity, twist);

    EXPECT_NEAR(settling_time, static_cast<double>(i) / kFrequencyHz, 1e-6);
  }

  // Set joint velocity to a value above threshold.
  joint_velocity.velocity(0) = kJointVelocityErrorBand * 2.0;

  // Settling time should drop back to negative cycle time.
  EXPECT_NEAR(counter.ComputeSettlingTime(
                  kTranslationalVelocityErrorBand, kRotationalVelocityErrorBand,
                  kJointVelocityErrorBand, joint_velocity, twist),
              -1.0 / kFrequencyHz, 1e-6);
}

TEST(SettlingTimeCounterTest, NonZeroTwistResetsCounter) {
  SettlingTimeCounter counter(kFrequencyHz);

  // Create a zero joint state and twist.
  JointStateV joint_velocity;
  ASSERT_OK(joint_velocity.SetSize(kNumDof));
  joint_velocity.velocity.setZero();
  Twist twist = Twist::ZERO;

  // Settling time should monotonically increase.
  for (size_t i = 0; i < kNumTests; i++) {
    double settling_time = counter.ComputeSettlingTime(
        kTranslationalVelocityErrorBand, kRotationalVelocityErrorBand,
        kJointVelocityErrorBand, joint_velocity, twist);

    EXPECT_NEAR(settling_time, static_cast<double>(i) / kFrequencyHz, 1e-6);
  }

  // Set twist to a value above threshold.
  twist(0) = 2.0 * kTranslationalVelocityErrorBand;
  twist(3) = 2.0 * kRotationalVelocityErrorBand;

  // Settling time should drop back to negative cycle time.
  EXPECT_NEAR(counter.ComputeSettlingTime(
                  kTranslationalVelocityErrorBand, kRotationalVelocityErrorBand,
                  kJointVelocityErrorBand, joint_velocity, twist),
              -1.0 / kFrequencyHz, 1e-6);
}

TEST(SettlingTimeCounterTest, ResetMethodWorks) {
  SettlingTimeCounter counter(kFrequencyHz);

  // Create a zero joint state and twist.
  JointStateV joint_velocity;
  ASSERT_OK(joint_velocity.SetSize(kNumDof));
  joint_velocity.velocity.setZero();
  Twist twist = Twist::ZERO;

  // Settling time should monotonically increase.
  for (size_t i = 0; i < kNumTests; i++) {
    double settling_time = counter.ComputeSettlingTime(
        kTranslationalVelocityErrorBand, kRotationalVelocityErrorBand,
        kJointVelocityErrorBand, joint_velocity, twist);

    EXPECT_NEAR(settling_time, static_cast<double>(i) / kFrequencyHz, 1e-6);
  }

  // Reset counter, expect counting to start from beginning, settled since once
  // cycle due to zero velocities.
  counter.Reset();
  EXPECT_NEAR(counter.ComputeSettlingTime(
                  kTranslationalVelocityErrorBand, kRotationalVelocityErrorBand,
                  kJointVelocityErrorBand, joint_velocity, twist),
              0.0, 1e-6);
}

}  // namespace

}  // namespace intrinsic::icon
