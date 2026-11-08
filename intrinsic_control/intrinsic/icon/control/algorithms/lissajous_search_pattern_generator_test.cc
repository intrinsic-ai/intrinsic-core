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

#include "intrinsic/icon/control/algorithms/lissajous_search_pattern_generator.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cmath>
#include <optional>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "internal/testing.h"
#include "intrinsic/eigenmath/matchers.h"
#include "intrinsic/eigenmath/pose3_utils.h"
#include "intrinsic/eigenmath/rotation_utils.h"
#include "intrinsic/eigenmath/test_utils.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/actions/impedance_trajectory_generators.pb.h"
#include "intrinsic/icon/control/algorithms/cartesian_impedance_commons.h"
#include "intrinsic/icon/control/algorithms/compute_critical_damping.h"
#include "intrinsic/icon/proto/cart_space_conversion.h"
#include "intrinsic/icon/testing/malloc_test.h"
#include "intrinsic/icon/utils/realtime_status_matchers.h"
#include "intrinsic/kinematics/types/cart_state.h"
#include "intrinsic/kinematics/types/state_rn.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/twist.h"
#include "protobuf-matchers/protocol-buffer-matchers.h"

namespace intrinsic::icon {
namespace {

using ::absl_testing::StatusIs;
using eigenmath::testing::IsApprox;
using ::protobuf_matchers::EqualsProto;
using ::testing::Gt;
using ::testing::HasSubstr;
using ::testing::Optional;

static constexpr double kFrequencyHz = 100.0;
static constexpr double kSmallTolerance = 1e-3;
static constexpr double kLargeTolerance = 1e-1;
static constexpr double kNoSpeedOverride = 1.0;

CartStatePVA GetTestCartStatePVA(const Pose3d& cart_pose) {
  CartStatePVA test_cart_state;
  test_cart_state.pose = cart_pose;
  return test_cart_state;
}

TEST(LissajousPatternSearch2DTest, CreateWorksForDefaultParams) {
  ASSERT_OK(LissajousSearchPattern2D::Create(
      LissajousSearchPattern2D::InputParameters(), kFrequencyHz));
}

TEST(LissajousPatternSearch2DTest, CreateFailsForBadFrequency) {
  EXPECT_THAT(
      LissajousSearchPattern2D::Create(
          LissajousSearchPattern2D::InputParameters(), /*frequency_hz=*/-1.),
      StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(LissajousPatternSearch2DTest, CreateFailsForBadXSize) {
  EXPECT_THAT(LissajousSearchPattern2D::Create(
                  LissajousSearchPattern2D::InputParameters{.x_size = -0.1},
                  kFrequencyHz),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(LissajousPatternSearch2DTest, CreateWorksForZeroXSize) {
  ASSERT_OK(LissajousSearchPattern2D::Create(
      LissajousSearchPattern2D::InputParameters{.x_size = 0.}, kFrequencyHz));
}

TEST(LissajousPatternSearch2DTest, CreateFailsForBadYSize) {
  EXPECT_THAT(LissajousSearchPattern2D::Create(
                  LissajousSearchPattern2D::InputParameters{.y_size = -0.1},
                  kFrequencyHz),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(LissajousPatternSearch2DTest, CreateWorksForZeroYSize) {
  ASSERT_OK(LissajousSearchPattern2D::Create(
      LissajousSearchPattern2D::InputParameters{.y_size = 0.}, kFrequencyHz));
}

TEST(LissajousPatternSearch2DTest, CreateFailsForBadMaxVelocity) {
  EXPECT_THAT(
      LissajousSearchPattern2D::Create(
          LissajousSearchPattern2D::InputParameters{.max_velocity = -0.1},
          /*frequency_hz=*/-1.),
      StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(LissajousPatternSearch2DTest, CreateFailsForBadPatternRatio) {
  EXPECT_THAT(
      LissajousSearchPattern2D::Create(
          LissajousSearchPattern2D::InputParameters{.max_lissajous_integer = 3},
          /*frequency_hz=*/-1.),
      StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(LissajousSearchPattern2D::Create(
                  LissajousSearchPattern2D::InputParameters{
                      .max_lissajous_integer = 10},
                  /*frequency_hz=*/-1.),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(LissajousPatternSearch2DTest, PeriodIsPositive) {
  ASSERT_OK_AND_ASSIGN(
      auto pattern,
      LissajousSearchPattern2D::Create(
          LissajousSearchPattern2D::InputParameters(), kFrequencyHz));
  EXPECT_GT(pattern.GetPeriodSeconds(), 0.0);
}

TEST(LissajousPatternSearch2DTest, ExecutionWithSpeedOverridesWorks) {
  ASSERT_OK_AND_ASSIGN(
      auto pattern_with_speed_override,
      LissajousSearchPattern2D::Create(
          LissajousSearchPattern2D::InputParameters(), kFrequencyHz));

  EXPECT_NEAR(pattern_with_speed_override.GetPhase(), 0.0, kSmallTolerance);

  const double kSpeedOverride = 0.33;
  // Cover a full cycle subject to speed override
  for (double t = 0.0;
       t < pattern_with_speed_override.GetPeriodSeconds() / kSpeedOverride;
       t += 1. / kFrequencyHz) {
    IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
    INTRINSIC_RT_ASSERT_OK(pattern_with_speed_override.Step(kSpeedOverride));
    IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
    EXPECT_GT(pattern_with_speed_override.GetPhase(), 0.0);
  }
  EXPECT_NEAR(pattern_with_speed_override.GetPhase(), 1.0, kSmallTolerance);
}

LissajousImpedanceSearchPattern::InputParameters
GoodLissajousImpedanceParameters() {
  return {
      .search_pattern_2d_parameters =
          {
              .x_size = 0.01,
              .y_size = 0.04,
              .max_velocity = 0.1,
              .max_lissajous_integer = 5,
          },
      .yaw_oscillation_amplitude_rad = 0.05,
      .yaw_oscillation_frequency_hz = 0.3,
      .search_force_newton = 5.0,
      .contact_stiffness = 30000,
      .translational_virtual_stiffness_x_y = 0.0,
      .rotational_virtual_stiffness_roll_pitch = 100,
      .rotational_virtual_stiffness_yaw = 0.0,
      .translational_virtual_inertia = 10,
      .rotational_virtual_inertia = 2,
  };
}

TEST(LissajousImpedanceSearchPatternTest, CreateFailsForBadFrequency) {
  EXPECT_THAT(LissajousImpedanceSearchPattern::Create(
                  GoodLissajousImpedanceParameters(), /*frequency_hz=*/-1.),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(LissajousImpedanceSearchPatternTest,
     CreateFailsForBad2DLissajousParameters) {
  auto lissajous_params = GoodLissajousImpedanceParameters();
  lissajous_params.search_pattern_2d_parameters.max_lissajous_integer = -1;
  EXPECT_THAT(
      LissajousImpedanceSearchPattern::Create(lissajous_params, kFrequencyHz),
      StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(LissajousImpedanceSearchPatternTest, CreateFailsForBadSearchForce) {
  auto lissajous_params = GoodLissajousImpedanceParameters();
  lissajous_params.search_force_newton = -1.0;
  EXPECT_THAT(
      LissajousImpedanceSearchPattern::Create(lissajous_params, kFrequencyHz),
      StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(LissajousImpedanceSearchPatternTest, CreateWorksSearchForceZero) {
  auto lissajous_params = GoodLissajousImpedanceParameters();
  lissajous_params.search_force_newton = 0.0;
  ASSERT_OK(
      LissajousImpedanceSearchPattern::Create(lissajous_params, kFrequencyHz));
}

TEST(LissajousImpedanceSearchPatternTest, CreateFailsForBadContactStiffness) {
  auto lissajous_params = GoodLissajousImpedanceParameters();
  lissajous_params.contact_stiffness = -1.0;
  EXPECT_THAT(
      LissajousImpedanceSearchPattern::Create(lissajous_params, kFrequencyHz),
      StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(LissajousImpedanceSearchPatternTest,
     CreateFailsForBadRotationalRollPitchStiffness) {
  auto lissajous_params = GoodLissajousImpedanceParameters();
  lissajous_params.rotational_virtual_stiffness_roll_pitch = -1.0;
  EXPECT_THAT(
      LissajousImpedanceSearchPattern::Create(lissajous_params, kFrequencyHz),
      StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(LissajousImpedanceSearchPatternTest,
     CreateFailsForBadRotationalYawStiffness) {
  auto lissajous_params = GoodLissajousImpedanceParameters();
  lissajous_params.rotational_virtual_stiffness_yaw = -1.0;
  EXPECT_THAT(
      LissajousImpedanceSearchPattern::Create(lissajous_params, kFrequencyHz),
      StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(LissajousImpedanceSearchPatternTest,
     CreateFailsForBadTranslationalStiffness) {
  auto lissajous_params = GoodLissajousImpedanceParameters();
  lissajous_params.translational_virtual_stiffness_x_y = -1.0;
  EXPECT_THAT(
      LissajousImpedanceSearchPattern::Create(lissajous_params, kFrequencyHz),
      StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(LissajousImpedanceSearchPatternTest,
     CreateFailsForBadTranslationalInertia) {
  auto lissajous_params = GoodLissajousImpedanceParameters();
  lissajous_params.translational_virtual_inertia = 0.0;
  EXPECT_THAT(
      LissajousImpedanceSearchPattern::Create(lissajous_params, kFrequencyHz),
      StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(LissajousImpedanceSearchPatternTest, CreateFailsForBadRotationalInertia) {
  auto lissajous_params = GoodLissajousImpedanceParameters();
  lissajous_params.rotational_virtual_inertia = 0.0;
  EXPECT_THAT(
      LissajousImpedanceSearchPattern::Create(lissajous_params, kFrequencyHz),
      StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(LissajousImpedanceSearchPatternTest,
     CreateFailsForBadYawOscillationAmplitude) {
  auto lissajous_params = GoodLissajousImpedanceParameters();
  lissajous_params.yaw_oscillation_amplitude_rad = -1.0;
  EXPECT_THAT(
      LissajousImpedanceSearchPattern::Create(lissajous_params, kFrequencyHz),
      StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(LissajousImpedanceSearchPatternTest,
     CreateWorksForZeroYawOscillationAmplitude) {
  auto lissajous_params = GoodLissajousImpedanceParameters();
  lissajous_params.yaw_oscillation_amplitude_rad = 0.0;
  EXPECT_OK(
      LissajousImpedanceSearchPattern::Create(lissajous_params, kFrequencyHz));
}

TEST(LissajousImpedanceSearchPatternTest,
     CreateFailsForBadYawOscillationFrequency) {
  auto lissajous_params = GoodLissajousImpedanceParameters();
  lissajous_params.yaw_oscillation_frequency_hz = -1.0;
  EXPECT_THAT(
      LissajousImpedanceSearchPattern::Create(lissajous_params, kFrequencyHz),
      StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(LissajousImpedanceSearchPatternTest,
     CreateWorksForZeroYawOscillationFrequency) {
  auto lissajous_params = GoodLissajousImpedanceParameters();
  lissajous_params.yaw_oscillation_frequency_hz = 0.0;
  EXPECT_OK(
      LissajousImpedanceSearchPattern::Create(lissajous_params, kFrequencyHz));
}

// Test where only mandatory fields are filled.
TEST(LissajousImpedanceSearchPatternTest, FromProtoWorksForMinimalProto) {
  intrinsic_proto::icon::actions::proto::LissajousImpedanceSearchPattern proto;
  proto.mutable_search_pattern_2d()->set_x_size(0.2);
  proto.mutable_search_pattern_2d()->set_y_size(0.3);
  proto.set_search_force_newton(5);
  proto.set_contact_stiffness(1000);
  proto.set_translational_virtual_inertia(10.0);
  proto.set_rotational_virtual_inertia(2.0);

  ASSERT_OK_AND_ASSIGN(auto params,
                       LissajousImpedanceSearchPattern::FromProto(proto));

  // All unset parameters should be at their defaults.
  EXPECT_NEAR(params.search_pattern_2d_parameters.max_velocity,
              LissajousSearchPattern2D::kDefaultSearchPatternMaxVelocity,
              kSmallTolerance);
  EXPECT_NEAR(
      params.search_pattern_2d_parameters.max_lissajous_integer,
      LissajousSearchPattern2D::kDefaultSearchPatternMaxLissajousInteger,
      kSmallTolerance);
  EXPECT_EQ(params.base_t_task, std::nullopt);
  EXPECT_NEAR(params.yaw_oscillation_amplitude_rad, 0.0, kSmallTolerance);
  EXPECT_NEAR(params.yaw_oscillation_frequency_hz, 0.0, kSmallTolerance);
  EXPECT_NEAR(params.translational_virtual_stiffness_x_y, 0.0, kSmallTolerance);
  EXPECT_NEAR(params.rotational_virtual_stiffness_roll_pitch, 0.0,
              kSmallTolerance);
  EXPECT_NEAR(params.rotational_virtual_stiffness_yaw, 0.0, kSmallTolerance);
  EXPECT_THAT(params.robot_tip_t_robot_tool,
              IsApprox(Pose3d::Identity(), kSmallTolerance));

  // Explicitly set parameters should be correct.
  EXPECT_NEAR(params.search_pattern_2d_parameters.x_size,
              proto.search_pattern_2d().x_size(), kSmallTolerance);
  EXPECT_NEAR(params.search_pattern_2d_parameters.y_size,
              proto.search_pattern_2d().y_size(), kSmallTolerance);
  EXPECT_NEAR(params.search_force_newton, proto.search_force_newton(),
              kSmallTolerance);
  EXPECT_NEAR(params.contact_stiffness, proto.contact_stiffness(),
              kSmallTolerance);
  EXPECT_NEAR(params.translational_virtual_inertia,
              proto.translational_virtual_inertia(), kSmallTolerance);
  EXPECT_NEAR(params.rotational_virtual_inertia,
              proto.rotational_virtual_inertia(), kSmallTolerance);
}

// Test where all fields are filled
TEST(LissajousImpedanceSearchPatternTest, FromProtoWorksForMaximalProto) {
  intrinsic_proto::icon::actions::proto::LissajousImpedanceSearchPattern proto;
  Pose3d base_t_task_test_pose = Pose3d(eigenmath::Vector3d(1.1, 1.2, 1.3));
  *proto.mutable_base_t_task() =
      intrinsic::icon::ToProto(base_t_task_test_pose);
  proto.mutable_search_pattern_2d()->set_x_size(0.2);
  proto.mutable_search_pattern_2d()->set_y_size(0.3);
  proto.mutable_search_pattern_2d()->set_max_velocity(0.5);
  proto.mutable_search_pattern_2d()->set_max_lissajous_integer(8);
  proto.set_yaw_oscillation_amplitude_rad(0.5);
  proto.set_yaw_oscillation_frequency_hz(0.5);
  proto.set_translational_virtual_stiffness_x_y(1.5);
  proto.set_rotational_virtual_stiffness_roll_pitch(0.6);
  proto.set_rotational_virtual_stiffness_yaw(0.7);
  proto.set_search_force_newton(5);
  proto.set_contact_stiffness(1000);
  proto.set_translational_virtual_inertia(10.0);
  proto.set_rotational_virtual_inertia(2.0);
  Pose3d robot_tip_t_robot_tool_test_pose =
      Pose3d(eigenmath::Vector3d(1.1, 1.2, 1.3));
  *proto.mutable_robot_tip_t_robot_tool() =
      intrinsic::icon::ToProto(robot_tip_t_robot_tool_test_pose);

  ASSERT_OK_AND_ASSIGN(auto params,
                       LissajousImpedanceSearchPattern::FromProto(proto));

  EXPECT_NEAR(params.search_pattern_2d_parameters.x_size,
              proto.search_pattern_2d().x_size(), kSmallTolerance);
  EXPECT_NEAR(params.search_pattern_2d_parameters.y_size,
              proto.search_pattern_2d().y_size(), kSmallTolerance);
  EXPECT_NEAR(params.search_pattern_2d_parameters.max_velocity,
              proto.search_pattern_2d().max_velocity(), kSmallTolerance);
  EXPECT_NEAR(params.search_pattern_2d_parameters.max_lissajous_integer,
              proto.search_pattern_2d().max_lissajous_integer(),
              kSmallTolerance);
  EXPECT_NEAR(params.yaw_oscillation_amplitude_rad,
              proto.yaw_oscillation_amplitude_rad(), kSmallTolerance);
  EXPECT_NEAR(params.yaw_oscillation_frequency_hz,
              proto.yaw_oscillation_frequency_hz(), kSmallTolerance);
  EXPECT_NEAR(params.translational_virtual_stiffness_x_y,
              proto.translational_virtual_stiffness_x_y(), kSmallTolerance);
  EXPECT_NEAR(params.rotational_virtual_stiffness_roll_pitch,
              proto.rotational_virtual_stiffness_roll_pitch(), kSmallTolerance);
  EXPECT_NEAR(params.rotational_virtual_stiffness_yaw,
              proto.rotational_virtual_stiffness_yaw(), kSmallTolerance);
  EXPECT_THAT(params.robot_tip_t_robot_tool,
              IsApprox(robot_tip_t_robot_tool_test_pose, kSmallTolerance));
  EXPECT_THAT(params.base_t_task,
              Optional(IsApprox(base_t_task_test_pose, kSmallTolerance)));
  EXPECT_NEAR(params.search_force_newton, proto.search_force_newton(),
              kSmallTolerance);
  EXPECT_NEAR(params.contact_stiffness, proto.contact_stiffness(),
              kSmallTolerance);
  EXPECT_NEAR(params.translational_virtual_inertia,
              proto.translational_virtual_inertia(), kSmallTolerance);
  EXPECT_NEAR(params.rotational_virtual_inertia,
              proto.rotational_virtual_inertia(), kSmallTolerance);

  // FromProto / ToProto conversion works.
  EXPECT_THAT(LissajousImpedanceSearchPattern::ToProto(params),
              EqualsProto(proto));
}

constexpr double kTestMass = 10.0;
constexpr double kTestRotationalInertia = 1.0;
constexpr double kMockStiffness = 1.0;

TEST(LissajousImpedanceSearchPatternTest, StepFailsIfNotInitialized) {
  auto lissajous_params = GoodLissajousImpedanceParameters();
  ASSERT_OK_AND_ASSIGN(auto pattern, LissajousImpedanceSearchPattern::Create(
                                         lissajous_params, kFrequencyHz));
  EXPECT_THAT(
      pattern->Step(kNoSpeedOverride),
      RealtimeStatusIs(absl::StatusCode::kFailedPrecondition,
                       HasSubstr("No realtime Cartesian target available")));
}

TEST(LissajousImpedanceSearchPatternTest, OverdampingIsApplied) {
  auto lissajous_params = GoodLissajousImpedanceParameters();

  lissajous_params.contact_stiffness = 10000;
  lissajous_params.translational_virtual_inertia = kTestMass;
  lissajous_params.rotational_virtual_inertia = kTestRotationalInertia;
  lissajous_params.rotational_virtual_stiffness_roll_pitch = 5.0;
  ASSERT_OK_AND_ASSIGN(auto pattern, LissajousImpedanceSearchPattern::Create(
                                         lissajous_params, kFrequencyHz));

  // Choose start position equal to some random value
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  INTRINSIC_RT_ASSERT_OK(pattern->Initialize(
      GetTestCartStatePVA(/*cart_pose=*/eigenmath::getRandomPose3d())));
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();

  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
      cartesian_impedance::RealTimeCartesianTarget cartesian_impedance_target,
      pattern->Step(kNoSpeedOverride));
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();

  // Compute non-overdamped case manually for comparison.
  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
      auto damping_reference,
      ComputeCriticalDampingFromInverseInertiaMatrix(
          eigenmath::Vector6d(kTestMass, kTestMass, kTestMass,
                              kTestRotationalInertia, kTestRotationalInertia,
                              kTestRotationalInertia)
              .asDiagonal(),
          eigenmath::Vector6d(
              kMockStiffness, kMockStiffness, kMockStiffness,
              lissajous_params.rotational_virtual_stiffness_roll_pitch,
              lissajous_params.rotational_virtual_stiffness_yaw, kMockStiffness)
              .asDiagonal()));

  // We know that for above scenario, overdamping should be more than a factor
  // 100.
  EXPECT_THAT(cartesian_impedance_target.cartesian_damping.maxCoeff(),
              Gt(100 * damping_reference.maxCoeff()));
}

TEST(LissajousImpedanceSearchPatternTest, TipTTargetOffsetGetsPassedCorrectly) {
  auto lissajous_params = GoodLissajousImpedanceParameters();
  lissajous_params.base_t_task = eigenmath::getRandomPose3d();
  lissajous_params.robot_tip_t_robot_tool = eigenmath::getRandomPose3d();
  ASSERT_OK_AND_ASSIGN(auto pattern, LissajousImpedanceSearchPattern::Create(
                                         lissajous_params, kFrequencyHz));
  EXPECT_THAT(pattern->GetInitialRobotTipTRobotTool(),
              IsApprox(lissajous_params.robot_tip_t_robot_tool));
  // Choose start position equal to nominal base_t_target configuration.
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  INTRINSIC_RT_ASSERT_OK(pattern->Initialize(CartStatePVA(
      GetTestCartStatePVA(/*cart_pose=*/*lissajous_params.base_t_task))));
  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
      cartesian_impedance::RealTimeCartesianTarget cartesian_impedance_target,
      pattern->Step(kNoSpeedOverride));
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
  EXPECT_THAT(cartesian_impedance_target.robot_tip_t_robot_tool,
              IsApprox(lissajous_params.robot_tip_t_robot_tool));
}

TEST(LissajousImpedanceSearchPatternTest, WrenchFeedForwardGetsSet) {
  auto lissajous_params = GoodLissajousImpedanceParameters();
  const double kSomeValue = 2.22;
  lissajous_params.search_force_newton = kSomeValue;
  ASSERT_OK_AND_ASSIGN(auto pattern, LissajousImpedanceSearchPattern::Create(
                                         lissajous_params, kFrequencyHz));

  // Choose start position equal to nominal base_t_target configuration.
  Pose3d initial_base_t_target = eigenmath::getRandomPose3d();
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  INTRINSIC_RT_ASSERT_OK(
      pattern->Initialize(GetTestCartStatePVA(initial_base_t_target)));
  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
      cartesian_impedance::RealTimeCartesianTarget cartesian_impedance_target,
      pattern->Step(kNoSpeedOverride));
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
  Wrench expected_wrench;
  expected_wrench.z() = kSomeValue;
  EXPECT_THAT(cartesian_impedance_target.tool_reference_wrench,
              IsApprox(expected_wrench));
}

TEST(LissajousImpedanceSearchPatternTest,
     FeedbackMatricesGetRotatedToBaseFrame) {
  auto lissajous_params = GoodLissajousImpedanceParameters();
  lissajous_params.base_t_task =
      Pose3d(eigenmath::QuaternionFromRPY(0., M_PI / 2., 0.));
  lissajous_params.translational_virtual_stiffness_x_y = 1;
  lissajous_params.rotational_virtual_stiffness_roll_pitch = 1;
  lissajous_params.rotational_virtual_stiffness_yaw = 1.;
  lissajous_params.translational_virtual_inertia = 1.0;
  lissajous_params.rotational_virtual_inertia = 1.0;
  lissajous_params.robot_tip_t_robot_tool = Pose3d::Identity();

  ASSERT_OK_AND_ASSIGN(auto pattern, LissajousImpedanceSearchPattern::Create(
                                         lissajous_params, kFrequencyHz));

  // Choose start position equal to nominal base_t_target configuration.
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  INTRINSIC_RT_ASSERT_OK(
      pattern->Initialize(GetTestCartStatePVA(*lissajous_params.base_t_task)));
  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
      cartesian_impedance::RealTimeCartesianTarget cartesian_impedance_target,
      pattern->Step(kNoSpeedOverride));
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();

  // Inertia matrix should be close to Identity - since task frame inertia
  // matrix has same values everywhere in translation or rotation.
  EXPECT_THAT(cartesian_impedance_target.virtual_cartesian_inertia_inverse,
              IsApprox(eigenmath::Matrix6d::Identity(), kSmallTolerance));

  EXPECT_THAT(cartesian_impedance_target.cartesian_stiffness,
              IsApprox(eigenmath::Matrix6d(
                           eigenmath::Vector6d{0, 1, 1, 1, 1, 1}.asDiagonal()),
                       kSmallTolerance));
}

TEST(LissajousImpedanceSearchPatternTest, CorrectInitialPoseIfBaseTTaskNotSet) {
  auto lissajous_params = GoodLissajousImpedanceParameters();
  const double kSomeValue = 2.22;
  lissajous_params.search_force_newton = kSomeValue;
  // Explicitly unset base_t_task, so that the currently given pose is set in
  // Initialize(). We need this because Step() returns an incrementally evolved
  // lissajous pattern and not the start position.
  lissajous_params.base_t_task = std::nullopt;
  // Make pattern size so small that the initial Step()'ed pose is within
  // tolerance.
  lissajous_params.search_pattern_2d_parameters.x_size = kSmallTolerance / 2.;
  lissajous_params.search_pattern_2d_parameters.y_size = kSmallTolerance / 2.;
  ASSERT_OK_AND_ASSIGN(auto pattern, LissajousImpedanceSearchPattern::Create(
                                         lissajous_params, kFrequencyHz));

  Pose3d initial_base_t_tip = eigenmath::getRandomPose3d();
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  INTRINSIC_RT_ASSERT_OK(
      pattern->Initialize(GetTestCartStatePVA(initial_base_t_tip)));
  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
      cartesian_impedance::RealTimeCartesianTarget cartesian_impedance_target,
      pattern->Step(kNoSpeedOverride));
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
  // The pattern is centered around the initial pose.
  const Pose3d expected_target_pose = Pose3d::Identity();

  EXPECT_THAT(cartesian_impedance_target.task_t_tool_reference_position,
              IsApprox(expected_target_pose.translation(), kSmallTolerance));
  EXPECT_THAT(cartesian_impedance_target.task_t_tool_reference_orientation,
              IsApprox(expected_target_pose.quaternion(), kSmallTolerance));
  // Task frame is set to the tool frame.
  EXPECT_THAT(cartesian_impedance_target.robot_base_t_task,
              IsApprox(initial_base_t_tip));
}

TEST(LissajousImpedanceSearchPatternTest, CorrectInitialPoseIfBaseTTaskIsSet) {
  auto lissajous_params = GoodLissajousImpedanceParameters();
  lissajous_params.robot_tip_t_robot_tool = Pose3d::Identity();
  // Explicitly set base_t_task.
  lissajous_params.base_t_task = eigenmath::getRandomPose3d();
  // Make pattern size so small that the initial Step()'ed pose is within
  // tolerance.
  lissajous_params.search_pattern_2d_parameters.x_size = kSmallTolerance / 2.;
  lissajous_params.search_pattern_2d_parameters.y_size = kSmallTolerance / 2.;
  ASSERT_OK_AND_ASSIGN(auto pattern, LissajousImpedanceSearchPattern::Create(
                                         lissajous_params, kFrequencyHz));

  // Use an initial pose which is offset from base_t_task within error bounds.
  constexpr double kSmallOffset = 0.01;
  Pose3d initial_base_t_target =
      *lissajous_params.base_t_task * eigenmath::RotationX(M_PI_4) *
      Pose3d(eigenmath::Vector3d(kSmallOffset, 0, 0));
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  INTRINSIC_RT_ASSERT_OK(
      pattern->Initialize(GetTestCartStatePVA(initial_base_t_target)));
  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
      cartesian_impedance::RealTimeCartesianTarget cartesian_impedance_target,
      pattern->Step(kNoSpeedOverride));
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();

  Pose3d expected_target_pose = eigenmath::RotationX(M_PI_4);
  EXPECT_THAT(cartesian_impedance_target.task_t_tool_reference_position,
              IsApprox(expected_target_pose.translation(), kSmallTolerance));
  EXPECT_THAT(cartesian_impedance_target.task_t_tool_reference_orientation,
              IsApprox(expected_target_pose.quaternion(), kSmallTolerance));
  EXPECT_THAT(cartesian_impedance_target.robot_base_t_task.translation(),
              IsApprox(initial_base_t_target.translation()));
  EXPECT_THAT(cartesian_impedance_target.robot_base_t_task.quaternion(),
              IsApprox(lissajous_params.base_t_task->quaternion()));

  EXPECT_NEAR(cartesian_impedance_target.tool_reference_twist.z(), 0.0,
              kSmallTolerance);
  // Velocity in x-y plane should be start near to 0.
  EXPECT_NEAR(cartesian_impedance_target.tool_reference_twist.x(), 0.0,
              kLargeTolerance);
  EXPECT_NEAR(cartesian_impedance_target.tool_reference_twist.y(), 0.0,
              kLargeTolerance);

  EXPECT_NEAR(cartesian_impedance_target.tool_reference_twist.RX(), 0.0,
              kSmallTolerance);
  EXPECT_NEAR(cartesian_impedance_target.tool_reference_twist.RY(), 0.0,
              kSmallTolerance);
  // Yaw velocity should be start near to 0.
  EXPECT_NEAR(cartesian_impedance_target.tool_reference_twist.RZ(), 0.0,
              kLargeTolerance);
}

TEST(LissajousImpedanceSearchPatternTest,
     YawVelocityOscillatesCorrectlyAroundSearchAxis) {
  auto lissajous_params = GoodLissajousImpedanceParameters();
  const double kTestAmplitude = 2.22;
  const double kTestYawFrequency = 0.7;
  lissajous_params.yaw_oscillation_frequency_hz = kTestYawFrequency;
  lissajous_params.yaw_oscillation_amplitude_rad = kTestAmplitude;
  ASSERT_OK_AND_ASSIGN(auto pattern, LissajousImpedanceSearchPattern::Create(
                                         lissajous_params, kFrequencyHz));
  CartStatePVA test_init_cart_state;
  test_init_cart_state.pose = Pose3d(eigenmath::Vector3d(1.1, 1.2, 1.3));

  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  INTRINSIC_RT_ASSERT_OK(pattern->Initialize(test_init_cart_state));
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();

  for (double time_since_trajectory_start = 0.0;
       time_since_trajectory_start < 1.0;
       time_since_trajectory_start += 1.0 / kFrequencyHz) {
    IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
    INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
        cartesian_impedance::RealTimeCartesianTarget cartesian_impedance_target,
        pattern->Step(kNoSpeedOverride));
    IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();

    // Compute expected velocity.
    double expected_yaw_velocity =
        2 * M_PI * kTestYawFrequency * kTestAmplitude *
        std::cos(2.0 * M_PI * kTestYawFrequency *
                 (time_since_trajectory_start + 1.0 / kFrequencyHz));

    EXPECT_NEAR(cartesian_impedance_target.tool_reference_twist.z(), 0.0,
                kSmallTolerance);
    EXPECT_NEAR(cartesian_impedance_target.tool_reference_twist.RX(), 0.0,
                kSmallTolerance);
    EXPECT_NEAR(cartesian_impedance_target.tool_reference_twist.RY(), 0.0,
                kSmallTolerance);
    ASSERT_NEAR(cartesian_impedance_target.tool_reference_twist.RZ(),
                expected_yaw_velocity, kSmallTolerance);
  }
}

TEST(LissajousImpedanceSearchPatternTest, ToolStaysInSearchPlane) {
  auto lissajous_params = GoodLissajousImpedanceParameters();
  const double kTestAmplitude = 0.2;
  const double kTestYawFrequency = 0.7;
  const double kPatternSize = 0.1;
  const double kMaxPatternVelocity = 0.01;
  const Pose3d kBaseTTask =
      Pose3d(eigenmath::Vector3d(1.0, 1.0, 1.0)) * eigenmath::RotationY(M_PI_4);
  const Pose3d kTipTTool =
      Pose3d(eigenmath::Vector3d(0.0, 0.0, 0.1)) * eigenmath::RotationZ(M_PI);
  lissajous_params.yaw_oscillation_frequency_hz = kTestYawFrequency;
  lissajous_params.yaw_oscillation_amplitude_rad = kTestAmplitude;
  lissajous_params.base_t_task = kBaseTTask;
  lissajous_params.robot_tip_t_robot_tool = kTipTTool;
  lissajous_params.search_pattern_2d_parameters.x_size = kPatternSize;
  lissajous_params.search_pattern_2d_parameters.y_size = kPatternSize;
  lissajous_params.search_pattern_2d_parameters.max_velocity =
      kMaxPatternVelocity;
  ASSERT_OK_AND_ASSIGN(auto pattern, LissajousImpedanceSearchPattern::Create(
                                         lissajous_params, kFrequencyHz));

  const Pose3d kBaseTTipStart = Pose3d(
      eigenmath::Quaterniond(0.0867479, -0.1040975, 0.1301219, 0.9821949),
      eigenmath::Vector3d(1.1, 1.2, 1.3));

  CartStatePVA test_init_cart_state;
  test_init_cart_state.pose = kBaseTTipStart;

  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  INTRINSIC_RT_ASSERT_OK(pattern->Initialize(test_init_cart_state));
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();

  const Pose3d kTaskTToolStart =
      kBaseTTask.inverse() * kBaseTTipStart * kTipTTool;

  for (double time_since_trajectory_start = 0.0;
       time_since_trajectory_start < 1.0;
       time_since_trajectory_start += 1.0 / kFrequencyHz) {
    IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
    INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
        cartesian_impedance::RealTimeCartesianTarget cartesian_impedance_target,
        pattern->Step(kNoSpeedOverride));
    IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();

    EXPECT_NEAR(cartesian_impedance_target.task_t_tool_reference_position.z(),
                0.0, kSmallTolerance);
    EXPECT_NEAR(cartesian_impedance_target.task_t_tool_reference_position.x(),
                0.0, kPatternSize);
    EXPECT_NEAR(cartesian_impedance_target.task_t_tool_reference_position.y(),
                0.0, kPatternSize);
    EXPECT_NEAR(cartesian_impedance_target.task_t_tool_reference_orientation
                    .angularDistance(kTaskTToolStart.quaternion()),
                0.0, kTestAmplitude);
    EXPECT_NEAR(cartesian_impedance_target.tool_reference_twist.z(), 0.0,
                kSmallTolerance);
    EXPECT_NEAR(cartesian_impedance_target.tool_reference_twist.x(), 0.0,
                kMaxPatternVelocity);
    EXPECT_NEAR(cartesian_impedance_target.tool_reference_twist.y(), 0.0,
                kMaxPatternVelocity);

    EXPECT_NEAR(cartesian_impedance_target.tool_reference_twist.RX(), 0.0,
                kSmallTolerance);
    EXPECT_NEAR(cartesian_impedance_target.tool_reference_twist.RY(), 0.0,
                kSmallTolerance);
    // The tool should not oscillate around the z-axis of the task frame.
    EXPECT_NEAR(cartesian_impedance_target.tool_reference_twist.RZ(), 0.0,
                2 * M_PI * kTestYawFrequency * kTestAmplitude);
  }
}

// Variable test parameters.
struct LissajousImpedanceSearchPatternTestParams {
  std::optional<Pose3d> base_t_task;
  double speed_override_factor;
};

using LissajousImpedanceSearchPatternTest =
    ::testing::TestWithParam<LissajousImpedanceSearchPatternTestParams>;
TEST_P(LissajousImpedanceSearchPatternTest,
       LissajousImpedanceSearchPatternTests) {
  auto lissajous_params = GoodLissajousImpedanceParameters();
  // In this test, we explicitly set the optional parameter `base_t_task`.
  lissajous_params.base_t_task = GetParam().base_t_task;
  lissajous_params.search_pattern_2d_parameters.x_size = 0.1;
  lissajous_params.search_pattern_2d_parameters.y_size = 0.5;
  ASSERT_OK_AND_ASSIGN(auto pattern, LissajousImpedanceSearchPattern::Create(
                                         lissajous_params, kFrequencyHz));

  // Create a comparison pattern that is located in the 2d task plane.
  ASSERT_OK_AND_ASSIGN(
      auto comparison_pattern_2d,
      LissajousSearchPattern2D::Create(
          lissajous_params.search_pattern_2d_parameters, kFrequencyHz));

  // Choose start position equal to nominal base_t_target configuration.
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  INTRINSIC_RT_ASSERT_OK(pattern->Initialize(CartStatePVA(GetTestCartStatePVA(
      /*cart_pose=*/*lissajous_params.base_t_task *
      lissajous_params.robot_tip_t_robot_tool.inverse()))));
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();

  const double speed_override_factor = GetParam().speed_override_factor;
  // Loop through the pattern and compare
  for (double time_since_trajectory_start = 0.0;
       time_since_trajectory_start < 10.0;
       time_since_trajectory_start +=
       speed_override_factor * 1.0 / kFrequencyHz) {
    // Compute where we expect to be in the task plane.
    INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
        StateRnPV lissajous_state_task_plane,
        comparison_pattern_2d.Step(speed_override_factor));

    Pose3d lissajous_pose_task_plane(
        eigenmath::Vector3d(lissajous_state_task_plane.position.x(),
                            lissajous_state_task_plane.position.y(), 0.0));

    Pose3d base_t_lissajous_expected =
        *lissajous_params.base_t_task * lissajous_pose_task_plane;

    // Evaluate reference generator.
    IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
    INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
        cartesian_impedance::RealTimeCartesianTarget cartesian_impedance_target,
        pattern->Step(speed_override_factor));
    IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();

    EXPECT_THAT(
        base_t_lissajous_expected.translation(),
        IsApprox(*lissajous_params.base_t_task *
                 cartesian_impedance_target.task_t_tool_reference_position));

    // Evaluate oscillation around expected hole axis.
    const double kOmega =
        2. * M_PI * lissajous_params.yaw_oscillation_frequency_hz;
    const double yaw_oscillation_angle_in_task_frame =
        lissajous_params.yaw_oscillation_amplitude_rad *
        std::sin(kOmega * time_since_trajectory_start +
                 speed_override_factor * 1.0 / kFrequencyHz);
    eigenmath::Quaterniond base_q_lissajous_target_expected =
        lissajous_params.base_t_task->quaternion() *
        eigenmath::QuaternionFromRPY(0., 0.,
                                     yaw_oscillation_angle_in_task_frame);
    EXPECT_THAT(
        lissajous_params.base_t_task->quaternion().inverse() *
            base_q_lissajous_target_expected,
        IsApprox(cartesian_impedance_target.task_t_tool_reference_orientation,
                 kSmallTolerance));

    EXPECT_THAT(cartesian_impedance_target.tool_reference_wrench.head<3>(),
                IsApprox(eigenmath::Vector3d::UnitZ() *
                             lissajous_params.search_force_newton,
                         kSmallTolerance));
  }
}

INSTANTIATE_TEST_SUITE_P(
    LissajousImpedanceSearchPatternTests, LissajousImpedanceSearchPatternTest,
    ::testing::ValuesIn<LissajousImpedanceSearchPatternTestParams>({
        {
            .base_t_task = Pose3d::Identity(),
            .speed_override_factor = kNoSpeedOverride,
        },
        {
            .base_t_task = Pose3d(eigenmath::Vector3d(0.1, 0.2, 0.3)),
            .speed_override_factor = kNoSpeedOverride,
        },
        {
            .base_t_task = Pose3d(eigenmath::QuaternionFromRPY(0.3, -0.1, 0.2)),
            .speed_override_factor = kNoSpeedOverride,
        },
        {
            .base_t_task = Pose3d(eigenmath::QuaternionFromRPY(0.3, -0.1, 0.2),
                                  eigenmath::Vector3d(0.1, 0.2, 0.3)),
            .speed_override_factor = kNoSpeedOverride,
        },
        {
            .base_t_task = Pose3d(eigenmath::QuaternionFromRPY(0.3, -0.1, 0.2),
                                  eigenmath::Vector3d(0.1, 0.2, 0.3)),
            .speed_override_factor = 0.333,
        },
    }));

}  // namespace
}  // namespace intrinsic::icon
