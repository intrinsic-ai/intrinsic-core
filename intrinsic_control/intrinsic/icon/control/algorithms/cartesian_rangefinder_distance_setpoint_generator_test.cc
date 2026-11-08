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

#include "intrinsic/icon/control/algorithms/cartesian_rangefinder_distance_setpoint_generator.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstddef>
#include <memory>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "internal/testing.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/control/algorithms/testing/cartesian_rangefinder_distance_controller_test_utils.h"
#include "intrinsic/icon/testing/malloc_test.h"
#include "intrinsic/icon/utils/realtime_status_matchers.h"
#include "intrinsic/kinematics/testing/trajectory_plot.h"
#include "intrinsic/kinematics/types/cart_state.h"
#include "intrinsic/math/pose3.h"

namespace intrinsic::icon {

namespace {

using ::absl_testing::StatusIs;
using ::testing ::DoubleNear;

constexpr double kFrequency = 100;
constexpr size_t kJoints = 6;
constexpr double kLowPassFilterGain = 0.1;
constexpr double kPositionErrorThreshold = 1e-3;
constexpr double kOrientationErrorThreshold = 1e-2;
constexpr double kSmallTolerance = 1e-6;

TEST(CartesianBSplineSetpointGeneratorTest, SetReferenceWorks) {
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<CartesianBSplineSetpointGenerator> setpoint_generator,
      CartesianBSplineSetpointGenerator::Create(kFrequency));

  auto p = CreateValidParameters(kJoints);
  std::vector<Pose3d> points;
  points.push_back(Pose3d(eigenmath::Vector3d(1.0, 2.0, 3.0)));
  points.push_back(Pose3d(eigenmath::Vector3d(1.0, 2.0, 4.0)));
  points.push_back(Pose3d(eigenmath::Vector3d(1.0, 2.0, 5.0)));
  points.push_back(Pose3d(eigenmath::Vector3d(1.0, 2.0, 6.0)));
  EXPECT_OK(setpoint_generator->SetReferencePoints(p.cartesian_limits, points));
}

TEST(CartesianBSplineSetpointGeneratorTest, ComputeSetpointWorks) {
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<CartesianBSplineSetpointGenerator> setpoint_generator,
      CartesianBSplineSetpointGenerator::Create(kFrequency));

  CartStatePVA state;
  state.pose.translation() << 0.1, 0.2, 0.3;

  CartStatePVA goal_state;
  goal_state.pose.translation() << 0.1, 0.2, 0.4;

  auto p = CreateValidParameters(kJoints);
  std::vector<Pose3d> points;
  points.push_back(Pose3d(eigenmath::Vector3d(0.1, 0.2, 0.3)));
  points.push_back(Pose3d(eigenmath::Vector3d(0.1, 0.2, 0.4)));
  points.push_back(Pose3d(eigenmath::Vector3d(0.1, 0.2, 0.5)));
  points.push_back(Pose3d(eigenmath::Vector3d(0.1, 0.2, 0.4)));
  EXPECT_OK(setpoint_generator->SetReferencePoints(p.cartesian_limits, points));

  const int kMaxNumIterations = 20;
  std::vector<CartStatePVA> states;
  std::vector<double> timestamps;

  for (int i = 0; i < kMaxNumIterations; i++) {
    CartStatePVA setpoint;
    EXPECT_OK(setpoint_generator->ComputeSetpoint(setpoint));
    if (setpoint_generator->LastPointReached()) {
      break;
    }

    EXPECT_NEAR(setpoint.pose.translation().x(), state.pose.translation().x(),
                kSmallTolerance);
    EXPECT_NEAR(setpoint.pose.translation().y(), state.pose.translation().y(),
                kSmallTolerance);

    states.push_back(setpoint);
    timestamps.push_back(i);
  }

  EXPECT_OK(CreateAndSavePlot(states, timestamps, "b_spline_reference"));

  EXPECT_THAT(states.back().pose.translation().z(),
              DoubleNear(goal_state.pose.translation().z(), kSmallTolerance));
}

TEST(CartesianBSplineSetpointGeneratorTest,
     ComputeSetpointWorksFailsWithoutReference) {
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<CartesianBSplineSetpointGenerator> setpoint_generator,
      CartesianBSplineSetpointGenerator::Create(kFrequency));

  CartStatePVA setpoint;
  EXPECT_THAT(setpoint_generator->ComputeSetpoint(setpoint),
              RealtimeStatusIs(absl::StatusCode::kInternal,
                               "The cartesian bspline is not working."));
}

TEST(CartesianBSplineSetpointGeneratorTest,
     SetReferenceFailsForEmptyPoseArray) {
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<CartesianBSplineSetpointGenerator> setpoint_generator,
      CartesianBSplineSetpointGenerator::Create(kFrequency));

  auto p = CreateValidParameters(kJoints);
  std::vector<Pose3d> points;
  EXPECT_THAT(
      setpoint_generator->SetReferencePoints(p.cartesian_limits, points),
      RealtimeStatusIs(absl::StatusCode::kFailedPrecondition,
                       "No data points in trajectory."));
}

TEST(CartesianBSplineSetpointGeneratorTest, SetReferenceFailsTooManyPoses) {
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<CartesianBSplineSetpointGenerator> setpoint_generator,
      CartesianBSplineSetpointGenerator::Create(kFrequency));

  auto p = CreateValidParameters(kJoints);
  std::vector<Pose3d> points;
  points.resize(CartesianBSplineSetpointGenerator::kMaxNumberControlPoints + 1);
  EXPECT_THAT(
      setpoint_generator->SetReferencePoints(p.cartesian_limits, points),
      StatusIs(absl::StatusCode::kFailedPrecondition,
               "Too many data points in trajectory."));
}

TEST(CartesianBSplineSetpointGeneratorTest, PerformsNoAllocation) {
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<CartesianBSplineSetpointGenerator> setpoint_generator,
      CartesianBSplineSetpointGenerator::Create(kFrequency));

  std::vector<Pose3d> points;
  // Four points are needed for a valid spline.
  points.push_back(Pose3d(eigenmath::Vector3d(0.1, 0.2, 0.3)));
  points.push_back(Pose3d(eigenmath::Vector3d(0.1, 0.2, 0.4)));
  points.push_back(Pose3d(eigenmath::Vector3d(0.1, 0.2, 0.5)));
  points.push_back(Pose3d(eigenmath::Vector3d(0.1, 0.2, 0.6)));
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  EXPECT_OK(
      setpoint_generator->SetReferencePoints(DefaultCartesianLimits(), points));
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();

  CartStatePVA setpoint;
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  EXPECT_OK(setpoint_generator->ComputeSetpoint(setpoint));
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
}

TEST(FilterSetpointGeneratorTest, ResetSetpointWorks) {
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<FilterSetpointGenerator> setpoint_generator,
      FilterSetpointGenerator::Create(kLowPassFilterGain,
                                      kPositionErrorThreshold,
                                      kOrientationErrorThreshold));

  CartStatePVA state;
  state.pose.translation() = eigenmath::Vector3d(0.1, 0.2, 0.3);
  auto limits = DefaultCartesianLimits();
  ASSERT_OK(setpoint_generator->Initialize(limits, state));

  CartStatePVA new_reference;
  new_reference.pose.translation() = eigenmath::Vector3d(0.1, 0.2, 0.4);
  ASSERT_OK(setpoint_generator->SetReferencePoint(new_reference));

  {
    CartStatePVA new_setpoint;
    EXPECT_OK(setpoint_generator->ComputeSetpoint(new_setpoint));
    EXPECT_OK(setpoint_generator->ComputeSetpoint(new_setpoint));
    EXPECT_EQ(new_setpoint.pose.translation().x(), 0.1);
    EXPECT_EQ(new_setpoint.pose.translation().y(), 0.2);
    EXPECT_GT(new_setpoint.pose.translation().z(), 0.3);
  }

  ASSERT_OK(setpoint_generator->Initialize(limits, state));
  {
    CartStatePVA new_setpoint;
    EXPECT_OK(setpoint_generator->ComputeSetpoint(new_setpoint));
    EXPECT_OK(setpoint_generator->ComputeSetpoint(new_setpoint));
    EXPECT_DOUBLE_EQ(new_setpoint.pose.translation().x(),
                     state.pose.translation().x());
    EXPECT_DOUBLE_EQ(new_setpoint.pose.translation().y(),
                     state.pose.translation().y());
    EXPECT_DOUBLE_EQ(new_setpoint.pose.translation().z(),
                     state.pose.translation().z());
  }
}

TEST(FilterSetpointGeneratorTest, ComputeSetpointWorks) {
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<FilterSetpointGenerator> setpoint_generator,
      FilterSetpointGenerator::Create(kLowPassFilterGain,
                                      kPositionErrorThreshold,
                                      kOrientationErrorThreshold));

  CartStatePVA state;
  state.pose.translation() = eigenmath::Vector3d(0.1, 0.2, 0.3);
  ASSERT_OK(setpoint_generator->Initialize(DefaultCartesianLimits(), state));

  CartStatePVA new_reference;
  new_reference.pose.translation() = eigenmath::Vector3d(0.1, 0.2, 0.4);
  ASSERT_OK(setpoint_generator->SetReferencePoint(new_reference));

  const int num_iterations = 100;
  std::vector<CartStatePVA> states(num_iterations);
  std::vector<double> timestamps(num_iterations);
  timestamps.at(0) = 0;
  states.at(0) = state;

  for (int i = 1; i < num_iterations; i++) {
    CartStatePVA setpoint;
    EXPECT_OK(setpoint_generator->ComputeSetpoint(setpoint));
    EXPECT_EQ(setpoint.pose.translation().x(), state.pose.translation().x());
    EXPECT_EQ(setpoint.pose.translation().y(), state.pose.translation().y());
    EXPECT_GT(setpoint.pose.translation().z(),
              states.at(i - 1).pose.translation().z());

    states.at(i) = setpoint;
    timestamps.at(i) = i;
  }

  ASSERT_OK(CreateAndSavePlot(states, timestamps, "filtered_reference"));

  EXPECT_THAT(states.back().pose.translation().z(),
              DoubleNear(new_reference.pose.translation().z(), 1e-4));
}

TEST(FilterSetpointGeneratorTest, PerformsNoAllocation) {
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<FilterSetpointGenerator> setpoint_generator,
      FilterSetpointGenerator::Create(kLowPassFilterGain,
                                      kPositionErrorThreshold,
                                      kOrientationErrorThreshold));

  CartStatePVA state;
  state.pose.translation() = eigenmath::Vector3d(0.1, 0.2, 0.3);
  ASSERT_OK(setpoint_generator->Initialize(DefaultCartesianLimits(), state));

  CartStatePVA new_reference;
  new_reference.pose.translation() = eigenmath::Vector3d(0.1, 0.2, 0.4);
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  ASSERT_OK(setpoint_generator->SetReferencePoint(new_reference));
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();

  CartStatePVA setpoint;
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  EXPECT_OK(setpoint_generator->ComputeSetpoint(setpoint));
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
}

TEST(FilterSetpointGeneratorTest, ComputeSetpointWorksFailsWithoutReference) {
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<FilterSetpointGenerator> setpoint_generator,
      FilterSetpointGenerator::Create(kLowPassFilterGain,
                                      kPositionErrorThreshold,
                                      kOrientationErrorThreshold));

  CartStatePVA setpoint;
  EXPECT_THAT(
      setpoint_generator->ComputeSetpoint(setpoint),
      RealtimeStatusIs(
          absl::StatusCode::kFailedPrecondition,
          "cartesian_limits are not set. Call SetReferencePoint() first."));
}

TEST(FilterSetpointGeneratorTest, LastPointReachedWorks) {
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<FilterSetpointGenerator> setpoint_generator,
      FilterSetpointGenerator::Create(kLowPassFilterGain,
                                      kPositionErrorThreshold,
                                      kOrientationErrorThreshold));

  CartStatePVA start_state;
  start_state.pose.translation() = eigenmath::Vector3d(0.0, 0.0, 0.2);
  ASSERT_OK(
      setpoint_generator->Initialize(DefaultCartesianLimits(), start_state));

  EXPECT_FALSE(setpoint_generator->LastPointReached());

  CartStatePVA goal_state;
  goal_state.pose.translation() = eigenmath::Vector3d(0.1, 0.0, 0.3);
  ASSERT_OK(setpoint_generator->SetReferencePoint(goal_state));

  EXPECT_FALSE(setpoint_generator->LastPointReached());

  CartStatePVA setpoint;
  for (int i = 0; i < 50; i++) {
    EXPECT_OK(setpoint_generator->ComputeSetpoint(setpoint));
  }

  EXPECT_TRUE(setpoint_generator->LastPointReached());
  EXPECT_LT(
      (setpoint.pose.translation() - goal_state.pose.translation()).norm(),
      kPositionErrorThreshold)
      << "setpoint.translation: " << setpoint.pose.translation();
}

TEST(FilterSetpointGeneratorTest, CreateFailsForInvalidFilterGain) {
  EXPECT_THAT(FilterSetpointGenerator::Create(0.0, kPositionErrorThreshold,
                                              kOrientationErrorThreshold),
              StatusIs(absl::StatusCode::kInvalidArgument));

  EXPECT_THAT(FilterSetpointGenerator::Create(1.1, kPositionErrorThreshold,
                                              kOrientationErrorThreshold),
              StatusIs(absl::StatusCode::kInvalidArgument));

  EXPECT_THAT(FilterSetpointGenerator::Create(-1.0, kPositionErrorThreshold,
                                              kOrientationErrorThreshold),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(FilterSetpointGeneratorTest, CreateFailsForInvalidPositionErrorThreshold) {
  EXPECT_THAT(FilterSetpointGenerator::Create(kLowPassFilterGain, 0.0,
                                              kOrientationErrorThreshold),
              StatusIs(absl::StatusCode::kInvalidArgument));

  EXPECT_THAT(FilterSetpointGenerator::Create(kLowPassFilterGain, -0.1,
                                              kOrientationErrorThreshold),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(FilterSetpointGeneratorTest,
     CreateFailsForInvalidOrientationErrorThreshold) {
  EXPECT_THAT(FilterSetpointGenerator::Create(kLowPassFilterGain,
                                              kPositionErrorThreshold, 0.0),
              StatusIs(absl::StatusCode::kInvalidArgument));

  EXPECT_THAT(FilterSetpointGenerator::Create(kLowPassFilterGain,
                                              kPositionErrorThreshold, -0.1),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(FilterSetpointGeneratorTest, InitializeFailsIfOutsideLimits) {
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<FilterSetpointGenerator> setpoint_generator,
      FilterSetpointGenerator::Create(kLowPassFilterGain,
                                      kPositionErrorThreshold,
                                      kOrientationErrorThreshold));

  CartStatePVA start_state;
  start_state.pose.translation() = eigenmath::Vector3d(0.0, 0.0, 2.0);
  EXPECT_THAT(
      setpoint_generator->Initialize(DefaultCartesianLimits(), start_state),
      StatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST(FilterSetpointGeneratorTest, SetReferenceFailsIfOutsideLimits) {
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<FilterSetpointGenerator> setpoint_generator,
      FilterSetpointGenerator::Create(kLowPassFilterGain,
                                      kPositionErrorThreshold,
                                      kOrientationErrorThreshold));

  CartStatePVA start_state;
  start_state.pose.translation() = eigenmath::Vector3d(0.0, 0.0, 0.0);
  ASSERT_OK(
      setpoint_generator->Initialize(DefaultCartesianLimits(), start_state));

  CartStatePVA goal_state;
  goal_state.pose.translation() = eigenmath::Vector3d(0.0, 0.0, 2.0);
  EXPECT_THAT(setpoint_generator->SetReferencePoint(goal_state),
              StatusIs(absl::StatusCode::kFailedPrecondition));
}

}  // namespace
}  // namespace intrinsic::icon
