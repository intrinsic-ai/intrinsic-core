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

#include "intrinsic/icon/control/algorithms/trajectory_residual_controller.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <vector>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "internal/testing.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/control/algorithms/joint_limit_checker.h"
#include "intrinsic/icon/testing/malloc_test.h"
#include "intrinsic/icon/utils/realtime_status_matchers.h"
#include "intrinsic/kinematics/types/joint_limits.h"
#include "intrinsic/kinematics/types/joint_state.h"
#include "intrinsic/kinematics/types/joint_state_matcher.h"
#include "intrinsic/math/ipow.h"

namespace intrinsic::icon {
namespace {

using ::absl_testing::StatusIs;
using ::intrinsic::testing::JointStateIsApprox;
using ::testing::DoubleNear;
using ::testing::Each;
using ::testing::Gt;
using ::testing::HasSubstr;
using ::testing::Le;
using ::testing::Lt;

static constexpr double kNumericalTolerance = 1e-10;

static constexpr double kHighControlFrequencyHz = 1000;
static constexpr double kControlFrequencyHz = 100;

static constexpr int kSixDofs = 6;
static constexpr int kNumDof = 3;

// These joint limits with low jerk limits are inspired by the Fanuc R2000 joint
// limits.
const JointLimits JointLimitsWithLowJerk{
    .min_position = eigenmath::Vector6d{-3.12886, -0.93720, -2.51799, -6.18319,
                                        -2.08166, -6.18319},
    .max_position = eigenmath::Vector6d{3.12886, 1.22645, 2.51799, 6.18319,
                                        2.08166, 6.18319},
    .max_velocity = eigenmath::Vector6d{1.722639972, 1.476548547, 1.394518072,
                                        1.968731396, 1.968731396, 3.281218994},
    .max_acceleration =
        eigenmath::Vector6d{2.34905749, 2.013470817, 1.901614062, 2.684627756,
                            2.684627756, 4.474385063},
    .max_jerk = eigenmath::Vector6d{6.406514461, 5.491300453, 5.186229117,
                                    7.321728469, 7.321728469, 12.20288625},
    .max_torque = eigenmath::Vector6d{8100, 8100, 4500, 1530, 1530, 810},
};

// Scales the maximum velocity, maximum acceleration, and maximum jerk of the
// given `limits` by the given `factor` to the powers of 1, 2, and 3,
// respectively.
JointLimits ScaledJointLimits(const JointLimits& limits, double factor) {
  JointLimits scaled_limits = limits;
  scaled_limits.max_velocity *= factor;
  scaled_limits.max_acceleration *= ::intrinsic::IPow(factor, 2);
  scaled_limits.max_jerk *= ::intrinsic::IPow(factor, 3);
  return scaled_limits;
}

TEST(TrajectoryResidualControllerTest, FailsForNoVelocityLimitMargin) {
  JointLimits planning_limits = CreateSimpleJointLimits(
      kNumDof, /*max_position=*/1.0, /*max_velocity=*/2.0,
      /*max_acceleration=*/10.0, /*max_jerk=*/100.0);
  JointLimits system_limits = planning_limits;
  system_limits.max_acceleration += eigenmath::VectorNd::Ones(kNumDof);
  system_limits.max_jerk += eigenmath::VectorNd::Ones(kNumDof);
  EXPECT_THAT(
      TrajectoryResidualController::Create(kControlFrequencyHz, planning_limits,
                                           system_limits),
      StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("limits")));
}

TEST(TrajectoryResidualControllerTest, FailsForNoAccelerationLimitMargin) {
  JointLimits planning_limits = CreateSimpleJointLimits(
      kNumDof, /*max_position=*/1.0, /*max_velocity=*/2.0,
      /*max_acceleration=*/10.0, /*max_jerk=*/100.0);
  JointLimits system_limits = planning_limits;
  system_limits.max_velocity += eigenmath::VectorNd::Ones(kNumDof);
  system_limits.max_jerk += eigenmath::VectorNd::Ones(kNumDof);
  EXPECT_THAT(
      TrajectoryResidualController::Create(kControlFrequencyHz, planning_limits,
                                           system_limits),
      StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("limits")));
}

TEST(TrajectoryResidualControllerTest, FailsForNoJerkLimitMargin) {
  JointLimits planning_limits = CreateSimpleJointLimits(
      kNumDof, /*max_position=*/1.0, /*max_velocity=*/2.0,
      /*max_acceleration=*/10.0, /*max_jerk=*/100.0);
  JointLimits system_limits = planning_limits;
  system_limits.max_velocity += eigenmath::VectorNd::Ones(kNumDof);
  system_limits.max_acceleration += eigenmath::VectorNd::Ones(kNumDof);
  EXPECT_THAT(
      TrajectoryResidualController::Create(kControlFrequencyHz, planning_limits,
                                           system_limits),
      StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("limits")));
}

TEST(TrajectoryResidualControllerTest, FailsForInvalidFrequency) {
  JointLimits planning_limits = CreateSimpleJointLimits(
      kNumDof, /*max_position=*/1.0, /*max_velocity=*/2.0,
      /*max_acceleration=*/10.0, /*max_jerk=*/100.0);
  JointLimits system_limits = CreateSimpleJointLimits(
      kNumDof, /*max_position=*/1.0, /*max_velocity=*/40.0,
      /*max_acceleration=*/150.0, /*max_jerk=*/120.0);
  EXPECT_THAT(
      TrajectoryResidualController::Create(
          /*control_frequency_hz=*/-0.01, planning_limits, system_limits),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("control_frequency")));
}

TEST(TrajectoryResidualControllerTest, ResidualRemainsZeroIfNoDeviation) {
  JointLimits planning_limits = CreateSimpleJointLimits(
      kNumDof, /*max_position=*/1.0, /*max_velocity=*/2.0,
      /*max_acceleration=*/10.0, /*max_jerk=*/100.0);
  JointLimits system_limits = CreateSimpleJointLimits(
      kNumDof, /*max_position=*/1.0, /*max_velocity=*/40.0,
      /*max_acceleration=*/150.0, /*max_jerk=*/120.0);

  ASSERT_OK_AND_ASSIGN(
      auto residual_controller,
      TrajectoryResidualController::Create(kControlFrequencyHz, planning_limits,
                                           system_limits));

  JointStatePVA residual;
  ASSERT_OK(residual.SetSize(kNumDof));
  residual.position.setZero();
  residual.velocity.setZero();
  residual.acceleration.setZero();

  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
      auto new_residual,
      residual_controller->ComputeResidualUsingLimitMargin(residual));
  EXPECT_THAT(new_residual, JointStateIsApprox(residual, 1e-4));

  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
      new_residual,
      residual_controller->ComputeResidualUsingPlanningLimits(residual));
  EXPECT_THAT(new_residual, JointStateIsApprox(residual, 1e-4));
}

TEST(TrajectoryResidualControllerTest,
     ResidualDecaysAsExpectedUsingLimitMargin) {
  const double kPlanningJerkLimit = 1.0;
  const double kSystemJerkLimit = 1.5;
  JointLimits planning_limits = CreateSimpleJointLimits(
      kNumDof, /*max_position=*/10.0, /*max_velocity=*/2.0,
      /*max_acceleration=*/1.0, kPlanningJerkLimit);
  JointLimits system_limits = CreateSimpleJointLimits(
      kNumDof, /*max_position=*/10.5, /*max_velocity=*/2.1,
      /*max_acceleration=*/1.2, kSystemJerkLimit);

  ASSERT_OK_AND_ASSIGN(
      auto residual_controller,
      TrajectoryResidualController::Create(kControlFrequencyHz, planning_limits,
                                           system_limits));

  JointStatePVA residual;
  ASSERT_OK(residual.SetSize(kNumDof));
  residual.position << .001, -0.002, 0.0007;
  residual.velocity.setConstant(1e-3);
  residual.acceleration.setZero();

  eigenmath::Vector3d previous_position = residual.position;
  eigenmath::Vector3d previous_velocity = residual.velocity;
  eigenmath::Vector3d previous_acceleration = residual.acceleration;

  const double expected_decay_duration_sec = 20.0;
  for (double t = 0.0; t < expected_decay_duration_sec;
       t += 1. / kControlFrequencyHz) {
    SCOPED_TRACE(absl::StrCat("Residual controller at time ", t));
    IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
    INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
        auto new_residual,
        residual_controller->ComputeResidualUsingLimitMargin(residual));
    IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
    residual = new_residual;

    eigenmath::Vector3d numdiff_velocity =
        (residual.position - previous_position) * kControlFrequencyHz;
    eigenmath::Vector3d numdiff_acceleration =
        (numdiff_velocity - previous_velocity) * kControlFrequencyHz;
    eigenmath::Vector3d numdiff_jerk =
        (numdiff_acceleration - previous_acceleration) * kControlFrequencyHz;

    constexpr double kLimitViolationToleranceFactor = 1.01;
    EXPECT_THAT(numdiff_jerk.cwiseAbs() / kLimitViolationToleranceFactor,
                Each(Le(kSystemJerkLimit - kPlanningJerkLimit)));

    previous_position = residual.position;
    previous_velocity = numdiff_velocity;
    previous_acceleration = numdiff_acceleration;
  }

  EXPECT_THAT(residual.position.cwiseAbs(), Each(Lt(kNumericalTolerance)));
  EXPECT_THAT(residual.velocity.cwiseAbs(), Each(Lt(kNumericalTolerance)));
  EXPECT_THAT(residual.acceleration.cwiseAbs(), Each(Lt(kNumericalTolerance)));
}

TEST(TrajectoryResidualControllerTest,
     ResidualDecaysAsExpectedUsingPlanningLimits) {
  JointLimits planning_limits = CreateSimpleJointLimits(
      kNumDof, /*max_position=*/10.0, /*max_velocity=*/2.0,
      /*max_acceleration=*/10.0, /*max_jerk=*/100.0);
  JointLimits system_limits = CreateSimpleJointLimits(
      kNumDof, /*max_position=*/10.5, /*max_velocity=*/40.0,
      /*max_acceleration=*/150.0, /*max_jerk=*/120.0);

  ASSERT_OK_AND_ASSIGN(
      auto residual_controller,
      TrajectoryResidualController::Create(kControlFrequencyHz, planning_limits,
                                           system_limits));

  JointStatePVA residual;
  ASSERT_OK(residual.SetSize(kNumDof));
  residual.position << 1, 2, 3;
  residual.velocity.setConstant(0.1);
  residual.acceleration.setZero();

  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
      residual,
      residual_controller->ComputeResidualUsingPlanningLimits(residual));
  // Residual state should be non-zero after first cycle.
  EXPECT_THAT(residual.position.cwiseAbs(), Each(Gt(1e-2)));
  EXPECT_THAT(residual.velocity.cwiseAbs(), Each(Gt(1e-2)));
  EXPECT_THAT(residual.acceleration.cwiseAbs(), Each(Gt(1e-2)));

  const double expected_decay_duration_sec = 2.0;
  for (double t = 0.0; t < expected_decay_duration_sec;
       t += 1. / kControlFrequencyHz) {
    IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
    auto maybe_residual =
        residual_controller->ComputeResidualUsingPlanningLimits(residual);
    IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
    INTRINSIC_RT_EXPECT_OK(maybe_residual.status());
    residual = maybe_residual.value();
  }

  EXPECT_THAT(residual.position.cwiseAbs(), Each(Lt(1e-3)));
  EXPECT_THAT(residual.velocity.cwiseAbs(), Each(Lt(1e-3)));
  EXPECT_THAT(residual.acceleration.cwiseAbs(), Each(Lt(1e-3)));
}

TEST(TrajectoryResidualControllerTest,
     ResidualDecaysSatisfyingSmallMarginLimits) {
  const absl::Duration sampling_step =
      absl::Seconds(1. / kHighControlFrequencyHz);

  JointLimits planning_limits = JointLimitsWithLowJerk;
  ASSERT_EQ(planning_limits.size(), kSixDofs);

  for (const double system_limits_factor : {1.05, 1.01, 1.002}) {
    SCOPED_TRACE(
        absl::StrCat("System limits factor: ", system_limits_factor, "."));
    JointLimits system_limits =
        ScaledJointLimits(planning_limits, system_limits_factor);
    // The JointLimitChecker checks that the trajectory is within limits and
    // Reflexxes fully saturates the limits completely. We need to check against
    // slightly higher limits to handle this.
    constexpr double kLimitsRelaxMultiplier = 1.001;
    JointLimits check_limits =
        ScaledJointLimits(system_limits, kLimitsRelaxMultiplier);
    for (const double position_residual :
         {1e-11, 1e-10, 1e-9, 1e-8, 1e-7, 1e-6, 1e-5, 1e-4}) {
      SCOPED_TRACE(absl::StrCat("Residual controller with position residual ",
                                position_residual, "."));

      ASSERT_OK_AND_ASSIGN(
          auto residual_controller,
          TrajectoryResidualController::Create(kHighControlFrequencyHz,
                                               planning_limits, system_limits));

      JointStatePVA residual;
      ASSERT_OK(residual.SetSize(kSixDofs));
      // Use non-trivial initial state. It will be updated within the loop.
      residual.position.setZero();
      residual.position[2] = position_residual;
      residual.velocity.setZero();
      residual.acceleration.setZero();

      std::vector<JointStatePVA> states(4, residual);
      const double expected_decay_duration_sec = 5.0;
      for (absl::Duration t = absl::ZeroDuration();
           t < absl::Seconds(expected_decay_duration_sec); t += sampling_step) {
        SCOPED_TRACE(absl::StrCat("Residual controller at time ", t));
        INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
            residual,
            residual_controller->ComputeResidualUsingLimitMargin(residual));
        states.push_back(residual);
      }

      JointLimitChecker joint_limit_checker(sampling_step, kSixDofs);
      for (const auto& state : states) {
        EXPECT_OK(joint_limit_checker.Check(state.position, check_limits));
      }

      EXPECT_THAT(residual.position.cwiseAbs(), Each(Lt(kNumericalTolerance)));
      EXPECT_THAT(residual.velocity.cwiseAbs(), Each(Lt(kNumericalTolerance)));
      EXPECT_THAT(residual.acceleration.cwiseAbs(),
                  Each(Lt(kNumericalTolerance)));

      EXPECT_THAT(states.size() * absl::ToDoubleSeconds(sampling_step),
                  DoubleNear(expected_decay_duration_sec,
                             5 * absl::ToDoubleSeconds(sampling_step)));
    }
  }
}

TEST(TrajectoryResidualControllerTest,
     ResidualDecaysSatisfyingPlanningLimitsForVaryingResiduals) {
  const absl::Duration sampling_step =
      absl::Seconds(1. / kHighControlFrequencyHz);

  for (const double planning_limits_factor : {1.05, 1.5, 5.0}) {
    SCOPED_TRACE(
        absl::StrCat("Planning limits factor: ", planning_limits_factor, "."));
    JointLimits planning_limits =
        ScaledJointLimits(JointLimitsWithLowJerk, planning_limits_factor);

    constexpr double kSystemLimitsFactor = 1.05;
    JointLimits system_limits =
        ScaledJointLimits(planning_limits, kSystemLimitsFactor);

    // The JointLimitChecker checks that the trajectory is within limits and
    // Reflexxes fully saturates the limits completely. We need to check against
    // slightly higher limits to handle this.
    constexpr double kLimitsRelaxMultiplier = 1.001;
    JointLimits check_limits =
        ScaledJointLimits(system_limits, kLimitsRelaxMultiplier);

    for (const double position_residual :
         {1e-11, 1e-10, 1e-9, 1e-8, 1e-7, 1e-6, 1e-5, 1e-4}) {
      SCOPED_TRACE(absl::StrCat("Residual controller with position residual ",
                                position_residual, "."));

      ASSERT_OK_AND_ASSIGN(
          auto residual_controller,
          TrajectoryResidualController::Create(kHighControlFrequencyHz,
                                               planning_limits, system_limits));

      JointStatePVA residual;
      ASSERT_OK(residual.SetSize(kSixDofs));
      // Use non-trivial initial state. It will be updated within the loop.
      residual.position.setZero();
      residual.position[2] = position_residual;
      residual.velocity.setZero();
      residual.acceleration.setZero();

      std::vector<JointStatePVA> states(4, residual);
      const double expected_decay_duration_sec = 5.0;
      for (absl::Duration t = absl::ZeroDuration();
           t < absl::Seconds(expected_decay_duration_sec); t += sampling_step) {
        SCOPED_TRACE(absl::StrCat("Residual controller at time ", t));
        INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
            residual,
            residual_controller->ComputeResidualUsingPlanningLimits(residual));
        states.push_back(residual);
      }

      JointLimitChecker joint_limit_checker(sampling_step, kSixDofs);
      for (const auto& state : states) {
        EXPECT_OK(joint_limit_checker.Check(state.position, check_limits));
      }

      EXPECT_THAT(residual.position.cwiseAbs(), Each(Lt(kNumericalTolerance)));
      EXPECT_THAT(residual.velocity.cwiseAbs(), Each(Lt(kNumericalTolerance)));
      EXPECT_THAT(residual.acceleration.cwiseAbs(),
                  Each(Lt(kNumericalTolerance)));

      EXPECT_THAT(states.size() * absl::ToDoubleSeconds(sampling_step),
                  DoubleNear(expected_decay_duration_sec,
                             5 * absl::ToDoubleSeconds(sampling_step)));
    }
  }
}

}  // namespace
}  // namespace intrinsic::icon
