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

#include "intrinsic/icon/control/algorithms/joint_position_pid_velocity_controller.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <memory>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "internal/testing.h"
#include "intrinsic/eigenmath/matchers.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/proto/joint_position_pid_velocity_controller_config.pb.h"
#include "intrinsic/icon/testing/malloc_test.h"
#include "intrinsic/icon/utils/realtime_status_matchers.h"
#include "intrinsic/icon/utils/realtime_status_or.h"
#include "intrinsic/math/signals/butter_filter2.h"

namespace intrinsic {
namespace icon {
namespace {

using ::absl_testing::StatusIs;
using ::intrinsic::eigenmath::testing::IsApprox;
using ::testing::AllOf;
using ::testing::DoubleNear;
using ::testing::Gt;
using ::testing::HasSubstr;
using ::testing::Lt;

constexpr int kNdof = 3;
constexpr double kDefaultKp = 1;
constexpr double kDefaultKi = 0.02;
constexpr double kDefaultKd = 0.1;
constexpr double kDefaultKff = 1;
constexpr double kDefaultCycleTime = 1;
constexpr double kDefaultMaxIntegralControl = 10;
constexpr double kDefaultMaxVelocityCommand = 20;
constexpr double kSmallValue = 1e-9;

intrinsic_proto::icon::JointPositionPidVelocityControllerConfig CreateConfig(
    double kp, double ki, double kd, double kff, double max_integral_control,
    double max_velocity_command, double cycle_time_seconds, int ndof) {
  intrinsic_proto::icon::JointPositionPidVelocityControllerConfig config;
  // Set a constant gain for each joint and leave the position unfiltered.
  for (int i = 0; i < ndof; ++i) {
    config.add_k_p(kp);
    config.add_k_i(ki);
    config.add_k_d(kd);
    config.add_k_ff(kff);
    config.add_max_velocity_command(max_velocity_command);
    config.add_max_integral_control(max_integral_control);
  }
  config.set_cycle_time_seconds(cycle_time_seconds);

  return config;
}

intrinsic_proto::icon::JointPositionPidVelocityControllerConfig
CreateDefaultConfig() {
  return CreateConfig(kDefaultKp, kDefaultKi, kDefaultKd, kDefaultKff,
                      kDefaultMaxIntegralControl, kDefaultMaxVelocityCommand,
                      kDefaultCycleTime, kNdof);
}

TEST(CreateControllerTest, ValidConfigCreate) {
  auto config = CreateDefaultConfig();
  EXPECT_OK(JointPositionPIDVelocityController::Create(config));
}

TEST(CreateControllerTest, MismatchedGainSize) {
  auto config = CreateDefaultConfig();
  config.add_k_i(1);
  EXPECT_THAT(JointPositionPIDVelocityController::Create(config),
              StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("k_i")));

  config = CreateDefaultConfig();
  config.add_k_d(1);
  EXPECT_THAT(JointPositionPIDVelocityController::Create(config),
              StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("k_d")));
}

TEST(CreateControllerTest, MismatchedFFScalingSize) {
  auto config = CreateDefaultConfig();
  config.add_k_ff(1);
  EXPECT_THAT(JointPositionPIDVelocityController::Create(config),
              StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("k_ff")));
}

TEST(CreateControllerTest, MismatchedMaxIntegralControlSize) {
  auto config = CreateDefaultConfig();
  config.add_max_integral_control(1);
  EXPECT_THAT(
      JointPositionPIDVelocityController::Create(config),
      StatusIs(absl::StatusCode::kInvalidArgument,
               AllOf(HasSubstr("max_integral_control"), HasSubstr("size"))));
}

TEST(CreateControllerTest, MismatchedMaxVelocityCommandSize) {
  auto config = CreateDefaultConfig();
  config.add_max_velocity_command(1);
  EXPECT_THAT(
      JointPositionPIDVelocityController::Create(config),
      StatusIs(absl::StatusCode::kInvalidArgument,
               AllOf(HasSubstr("max_velocity_command"), HasSubstr("size"))));
}

TEST(CreateControllerTest, InvalidKp) {
  auto config = CreateDefaultConfig();
  config.mutable_k_p()->Set(0, -1);
  EXPECT_THAT(JointPositionPIDVelocityController::Create(config),
              StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("k_p")));
}

TEST(CreateControllerTest, InvalidKi) {
  auto config = CreateDefaultConfig();
  config.mutable_k_i()->Set(1, -1);
  EXPECT_THAT(JointPositionPIDVelocityController::Create(config),
              StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("k_i")));
}

TEST(CreateControllerTest, InvalidKiNonZeroKpZero) {
  auto config = CreateDefaultConfig();
  config.mutable_k_p()->Set(1, 0);
  EXPECT_THAT(JointPositionPIDVelocityController::Create(config),
              StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("k_i")));
}

TEST(CreateControllerTest, InvalidKff) {
  auto config = CreateDefaultConfig();
  config.mutable_k_ff()->Set(1, -0.1);
  EXPECT_THAT(JointPositionPIDVelocityController::Create(config),
              StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("k_ff")));
  config.mutable_k_ff()->Set(1, 1.1);
  EXPECT_THAT(JointPositionPIDVelocityController::Create(config),
              StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("k_ff")));
}

TEST(CreateControllerTest, EmptyMaxIntegralControlWorks) {
  auto config = CreateDefaultConfig();
  config.clear_max_integral_control();
  EXPECT_OK(JointPositionPIDVelocityController::Create(config));
}

TEST(CreateControllerTest, InvalidMaxIntegralControl) {
  auto config = CreateDefaultConfig();
  config.mutable_max_integral_control()->Set(2, -0.1);
  EXPECT_THAT(JointPositionPIDVelocityController::Create(config),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("max_integral_control")));
}

TEST(CreateControllerTest, InvalidMaxVelocityCommand) {
  auto config = CreateDefaultConfig();
  config.mutable_max_velocity_command()->Set(2, -0.1);
  EXPECT_THAT(JointPositionPIDVelocityController::Create(config),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("max_velocity_command")));
}

TEST(CreateControllerTest, InvalidCycleTime) {
  auto config = CreateDefaultConfig();
  config.set_cycle_time_seconds(0);
  EXPECT_THAT(JointPositionPIDVelocityController::Create(config),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("cycle_time_seconds")));

  config.set_cycle_time_seconds(-1);
  EXPECT_THAT(JointPositionPIDVelocityController::Create(config),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("cycle_time_seconds")));
}

TEST(CreateControllerTest, InvalidCutoffFrequency) {
  auto config = CreateDefaultConfig();
  config.set_position_filter_cuttoff_frequency_hz(0);
  EXPECT_THAT(JointPositionPIDVelocityController::Create(config),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("position_filter_cuttoff_frequency_hz")));
}

TEST(CreateControllerTest, InvalidCutoffFrequencyNyquist) {
  auto config = CreateDefaultConfig();
  config.set_position_filter_cuttoff_frequency_hz(0.6 /
                                                  config.cycle_time_seconds());
  EXPECT_THAT(JointPositionPIDVelocityController::Create(config),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       AllOf(HasSubstr("position_filter_cuttoff_frequency_hz"),
                             HasSubstr("0.5"))));
}

// Test fixture for testing feedback control of PID joint position controller.
class ControllerTest : public ::testing::Test {
 public:
  void SetUp() override {
    config_ = CreateDefaultConfig();
    ASSERT_OK_AND_ASSIGN(controller_,
                         JointPositionPIDVelocityController::Create(config_));
  }

 protected:
  intrinsic_proto::icon::JointPositionPidVelocityControllerConfig config_;
  std::unique_ptr<JointPositionPIDVelocityController> controller_;
};

TEST_F(ControllerTest, ZeroPositionErrorZeroVelocity) {
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
      eigenmath::VectorNd target_velocities,
      controller_->CalculateSetpoints(
          /*position_desired=*/eigenmath::VectorNd({{1, 1, 1}}),
          /*velocity_feedforward=*/eigenmath::VectorNd({{0.0, 0.0, 0.0}}),
          /*position_state=*/eigenmath::VectorNd({{1, 1, 1}}),
          /*velocity_state=*/eigenmath::VectorNd({{0, 0, 0}})));
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
  EXPECT_THAT(target_velocities, IsApprox(eigenmath::VectorNd({{0, 0, 0}})));
}

TEST_F(ControllerTest, ZeroPositionErrorPositiveVelocity) {
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
      eigenmath::VectorNd target_velocities,
      controller_->CalculateSetpoints(
          /*position_desired=*/eigenmath::VectorNd({{1, 1, 1}}),
          /*velocity_feedforward=*/eigenmath::VectorNd({{0.1, 0.1, 0.1}}),
          /*position_state=*/eigenmath::VectorNd({{1, 1, 1}}),
          /*velocity_state=*/eigenmath::VectorNd({{0.1, 0.1, 0.1}})));
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
  // No tracking error so control velocity should come only from feedforward.
  EXPECT_THAT(target_velocities,
              IsApprox(eigenmath::VectorNd({{0.1, 0.1, 0.1}})));
}

TEST_F(ControllerTest, PositivePositionErrorZeroVelocity) {
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
      eigenmath::VectorNd target_velocities,
      controller_->CalculateSetpoints(
          /*position_desired=*/eigenmath::VectorNd({{1.5, 1.5, 1.5}}),
          /*velocity_feedforward=*/eigenmath::VectorNd({{0, 0, 0}}),
          /*position_state=*/eigenmath::VectorNd({{1, 1, 1}}),
          /*velocity_state=*/eigenmath::VectorNd({{0, 0, 0}})));
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
  EXPECT_THAT(target_velocities,
              IsApprox((kDefaultKi + kDefaultKp) *
                       eigenmath::VectorNd({{0.5, 0.5, 0.5}})));
}

TEST_F(ControllerTest,
       PositivePositionErrorZeroVelocityIntegrationAccumulatesBelowSaturation) {
  eigenmath::VectorNd target_velocities;
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  for (int n = 0; n < 10; ++n) {
    INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
        target_velocities,
        controller_->CalculateSetpoints(
            /*position_desired=*/eigenmath::VectorNd({{1.5, 1.5, 1.5}}),
            /*velocity_feedforward=*/eigenmath::VectorNd({{0, 0, 0}}),
            /*position_state=*/eigenmath::VectorNd({{1, 1, 1}}),
            /*velocity_state=*/eigenmath::VectorNd({{0, 0, 0}})));
  }
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
  EXPECT_THAT(target_velocities,
              IsApprox((10 * kDefaultKi + kDefaultKp) *
                       eigenmath::VectorNd({{0.5, 0.5, 0.5}})));
}

TEST_F(ControllerTest, IntegralSaturates) {
  eigenmath::VectorNd target_velocities;
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  for (int n = 0; n < (int)(kDefaultMaxIntegralControl / (0.5 * kDefaultKi));
       ++n) {
    INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
        target_velocities,
        controller_->CalculateSetpoints(
            /*position_desired=*/eigenmath::VectorNd({{1.5, 1.5, 1.5}}),
            /*velocity_feedforward=*/eigenmath::VectorNd({{0, 0, 0}}),
            /*position_state=*/eigenmath::VectorNd({{1, 1, 2}}),
            /*velocity_state=*/eigenmath::VectorNd({{0, 0, 0}})));
  }
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
  EXPECT_THAT(target_velocities,
              IsApprox(kDefaultKp * eigenmath::VectorNd({{0.5, 0.5, -0.5}}) +
                       eigenmath::VectorNd({{kDefaultMaxIntegralControl,
                                             kDefaultMaxIntegralControl,
                                             -kDefaultMaxIntegralControl}})));
}

TEST_F(ControllerTest, DoesntIntegrateWhenSaturated) {
  eigenmath::VectorNd target_velocities;
  const int kNsteps = 10;
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  for (int n = 0; n < kNsteps; ++n) {
    // Setting a very high error on one joint should saturate it from the
    // second. step from the p gain. So the integral will only increase in the
    // first step.
    INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
        target_velocities,
        controller_->CalculateSetpoints(
            /*position_desired=*/eigenmath::VectorNd({{1.1, 101, 1.1}}),
            /*velocity_feedforward=*/eigenmath::VectorNd({{0, 0, 0}}),
            /*position_state=*/eigenmath::VectorNd({{1, 1, 1}}),
            /*velocity_state=*/eigenmath::VectorNd({{0, 0, 0}})));
  }
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
  EXPECT_THAT(
      target_velocities[0],
      DoubleNear(kDefaultKp * 0.1 + kDefaultKi * kNsteps * 0.1, kSmallValue));
  EXPECT_THAT(target_velocities[1],
              DoubleNear(kDefaultMaxVelocityCommand, kSmallValue));

  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
      target_velocities,
      controller_->CalculateSetpoints(
          /*position_desired=*/eigenmath::VectorNd({{1, 1, 1}}),
          /*velocity_feedforward=*/eigenmath::VectorNd({{0, 0, 0}}),
          /*position_state=*/eigenmath::VectorNd({{1, 1, 1}}),
          /*velocity_state=*/eigenmath::VectorNd({{0, 0, 0}})));

  EXPECT_THAT(target_velocities,
              IsApprox(kDefaultKi * eigenmath::VectorNd({{0.1 * kNsteps, 100,
                                                          0.1 * kNsteps}})));
}

TEST_F(ControllerTest, DoesntIntegrateWhenSaturatedNegative) {
  eigenmath::VectorNd target_velocities;
  const int kNsteps = 10;
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  for (int n = 0; n < kNsteps; ++n) {
    // Setting a very high error on one joint should saturate it from the
    // second. step from the p gain. So the integral will only increase in the
    // first step.
    INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
        target_velocities,
        controller_->CalculateSetpoints(
            /*position_desired=*/eigenmath::VectorNd({{1.1, -99, 1.1}}),
            /*velocity_feedforward=*/eigenmath::VectorNd({{0, 0, 0}}),
            /*position_state=*/eigenmath::VectorNd({{1, 1, 1}}),
            /*velocity_state=*/eigenmath::VectorNd({{0, 0, 0}})));
  }
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
  EXPECT_THAT(
      target_velocities[0],
      DoubleNear(kDefaultKp * 0.1 + kDefaultKi * kNsteps * 0.1, kSmallValue));
  EXPECT_THAT(target_velocities[1],
              DoubleNear(-kDefaultMaxVelocityCommand, kSmallValue));

  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
      target_velocities,
      controller_->CalculateSetpoints(
          /*position_desired=*/eigenmath::VectorNd({{1, 1, 1}}),
          /*velocity_feedforward=*/eigenmath::VectorNd({{0, 0, 0}}),
          /*position_state=*/eigenmath::VectorNd({{1, 1, 1}}),
          /*velocity_state=*/eigenmath::VectorNd({{0, 0, 0}})));

  EXPECT_THAT(target_velocities,
              IsApprox(kDefaultKi * eigenmath::VectorNd({{0.1 * kNsteps, -100,
                                                          0.1 * kNsteps}})));
}

TEST_F(ControllerTest, PositionStateAndPositionCommandSizesMismatched) {
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  RealtimeStatusOr<eigenmath::VectorNd> status_or =
      controller_->CalculateSetpoints(
          /*position_desired=*/eigenmath::VectorNd({{1.5, 1.5}}),
          /*velocity_feedforward=*/eigenmath::VectorNd({{0, 0, 0}}),
          /*position_state=*/eigenmath::VectorNd({{1, 1, 1}}),
          /*velocity_state=*/eigenmath::VectorNd({{0, 0, 0}}));
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
  EXPECT_THAT(
      status_or,
      RealtimeStatusIs(absl::StatusCode::kInvalidArgument,
                       AllOf(HasSubstr("size"), HasSubstr("position_state"),
                             HasSubstr("position_desired"))));
}

TEST_F(ControllerTest, VelocityStateAndPositionDesiredSizesMismatched) {
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  RealtimeStatusOr<eigenmath::VectorNd> status_or =
      controller_->CalculateSetpoints(
          /*position_desired=*/eigenmath::VectorNd({{1.5, 1.5, 1.5}}),
          /*velocity_feedforward=*/eigenmath::VectorNd({{0, 0, 0}}),
          /*position_state=*/eigenmath::VectorNd({{1, 1, 1}}),
          /*velocity_state=*/eigenmath::VectorNd({{0, 0}}));
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
  EXPECT_THAT(
      status_or,
      RealtimeStatusIs(absl::StatusCode::kInvalidArgument,
                       AllOf(HasSubstr("size"), HasSubstr("velocity_state"),
                             HasSubstr("position_desired"))));
}

TEST_F(ControllerTest, VelocityCommandAndPositionCommandSizesMismatched) {
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  RealtimeStatusOr<eigenmath::VectorNd> status_or =
      controller_->CalculateSetpoints(
          /*position_desired=*/eigenmath::VectorNd({{1.5, 1.5, 1.5}}),
          /*velocity_feedforward=*/eigenmath::VectorNd({{0, 0}}),
          /*position_state=*/eigenmath::VectorNd({{1, 1, 1}}),
          /*velocity_state=*/eigenmath::VectorNd({{0.0, 0.0, 0.0}}));
  EXPECT_THAT(status_or,
              RealtimeStatusIs(
                  absl::StatusCode::kInvalidArgument,
                  AllOf(HasSubstr("size"), HasSubstr("velocity_feedforward"),
                        HasSubstr("position_desired"))));
}

TEST_F(ControllerTest, PositionCommandAndControlGainSizesMismatched) {
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  RealtimeStatusOr<eigenmath::VectorNd> status_or =
      controller_->CalculateSetpoints(
          /*position_desired=*/eigenmath::VectorNd({{1.5, 1.5}}),
          /*velocity_feedforward=*/eigenmath::VectorNd({{0, 0}}),
          /*position_state=*/eigenmath::VectorNd({{1, 1}}),
          /*velocity_state=*/eigenmath::VectorNd({{0.0, 0.0}}));
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
  EXPECT_THAT(status_or, RealtimeStatusIs(
                             absl::StatusCode::kInvalidArgument,
                             AllOf(HasSubstr("size"), HasSubstr("control gain"),
                                   HasSubstr("position_desired"))));
}

TEST_F(ControllerTest, DerivativeWorks) {
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
      eigenmath::VectorNd target_velocities,
      controller_->CalculateSetpoints(
          /*position_desired=*/eigenmath::VectorNd({{1, 1, 1}}),
          /*velocity_feedforward=*/eigenmath::VectorNd({{0, 0.5, 0}}),
          /*position_state=*/eigenmath::VectorNd({{1, 1, 1}}),
          /*velocity_state=*/eigenmath::VectorNd({{0.0, 0.0, 0.0}})));
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
  EXPECT_THAT(target_velocities,
              IsApprox((kDefaultKd)*eigenmath::VectorNd({{0.0, 0.5, 0.0}}) +
                       (kDefaultKff)*eigenmath::VectorNd({{0.0, 0.5, 0.0}})));
}

TEST_F(ControllerTest, ResetWorks) {
  eigenmath::VectorNd target_velocities;
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  for (int n = 0; n < 10; ++n) {
    INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
        target_velocities,
        controller_->CalculateSetpoints(
            /*position_desired=*/eigenmath::VectorNd({{1.5, 1.5, 1.5}}),
            /*velocity_feedforward=*/eigenmath::VectorNd({{0, 0, 0}}),
            /*position_state=*/eigenmath::VectorNd({{1, 1, 1}}),
            /*velocity_state=*/eigenmath::VectorNd({{0, 0, 0}})));
  }
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();

  EXPECT_THAT(target_velocities,
              IsApprox((10 * kDefaultKi + kDefaultKp) *
                       eigenmath::VectorNd({{0.5, 0.5, 0.5}})))
      << "The target velocity should be resultant from the integral of the "
         "position error over the time interval (10 steps) as well as the "
         "proportional gain.";

  controller_->Reset();
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
      target_velocities,
      controller_->CalculateSetpoints(
          /*position_desired=*/eigenmath::VectorNd({{1.5, 1.5, 1.5}}),
          /*velocity_feedforward=*/eigenmath::VectorNd({{0, 0, 0}}),
          /*position_state=*/eigenmath::VectorNd({{1, 1, 1}}),
          /*velocity_state=*/eigenmath::VectorNd({{0, 0, 0}})));
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
  EXPECT_THAT(target_velocities,
              IsApprox((kDefaultKi + kDefaultKp) *
                       eigenmath::VectorNd({{0.5, 0.5, 0.5}})))
      << "Due to the reset in state there should only be a single step of "
         "integral control.";
}

TEST(ControllerWithFiltersTest, ControllerWithPositionFiltersWorks) {
  auto config = CreateDefaultConfig();
  for (int i = 0; i < kNdof; ++i) {
    config.set_k_i(i, 0);
  }
  config.set_position_filter_cuttoff_frequency_hz(0.1 / kDefaultCycleTime);
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<JointPositionPIDVelocityController> controller,
      JointPositionPIDVelocityController::Create(config));

  // We replicate part of the internal calculations here to test the
  // controller. This at least allows us to test that the filtering is being
  // called when configured.
  ButterFilter2<eigenmath::VectorNd> butter_pos;
  ASSERT_TRUE(butter_pos.Init(eigenmath::VectorNd({{1, 2, 1}}),
                              1 / kDefaultCycleTime, 0.1 / kDefaultCycleTime));
  eigenmath::VectorNd target_velocities;
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  // Let the filter settle to a stable control command.
  for (int n = 0; n < 100; ++n) {
    butter_pos.Update(eigenmath::VectorNd({{1, 2, 1}}));
    INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
        target_velocities,
        controller->CalculateSetpoints(
            /*position_desired=*/eigenmath::VectorNd({{1.5, 1.5, 1.5}}),
            /*velocity_feedforward=*/eigenmath::VectorNd({{0, 0, 0}}),
            /*position_state=*/eigenmath::VectorNd({{1, 2, 1}}),
            /*velocity_state=*/eigenmath::VectorNd({{0.0, 0.0, 0.0}})));
  }
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
  EXPECT_THAT(target_velocities,
              IsApprox(kDefaultKp * eigenmath::VectorNd({{0.5, -0.5, 0.5}})));
  // Change the desired position for one of the joints.
  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
      target_velocities,
      controller->CalculateSetpoints(
          /*position_desired=*/eigenmath::VectorNd({{1.5, 1.5, 1.5}}),
          /*velocity_feedforward=*/eigenmath::VectorNd({{0, 0, 0}}),
          /*position_state=*/eigenmath::VectorNd({{2, 2, 1}}),
          /*velocity_state=*/eigenmath::VectorNd({{0.0, 0.0, 0.0}})));
  butter_pos.Update(eigenmath::VectorNd({{2, 2, 1}}));
  // The command does not reflect the complete difference between state and
  // setpoint since the filter is not settled yet.
  EXPECT_THAT(target_velocities[0],
              DoubleNear((1.5 - butter_pos.GetOutput()[0]) * kDefaultKp, 1e-6));

  // Now run for more steps to let the filter settle.
  for (int n = 0; n < 100; ++n) {
    INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
        target_velocities,
        controller->CalculateSetpoints(
            /*position_desired=*/eigenmath::VectorNd({{1.5, 1.5, 1.5}}),
            /*velocity_feedforward=*/eigenmath::VectorNd({{0, 0, 0}}),
            /*position_state=*/eigenmath::VectorNd({{2, 2, 1}}),
            /*velocity_state=*/eigenmath::VectorNd({{0.0, 0.0, 0.0}})));
  }
  EXPECT_THAT(target_velocities[0],
              DoubleNear((1.5 - 2.0) * kDefaultKp, kSmallValue));
}

TEST(ControllerWithFiltersTest, ControllerWithVelocityFiltersWorks) {
  auto config = CreateDefaultConfig();
  config.set_velocity_filter_cuttoff_frequency_hz(0.1 / kDefaultCycleTime);
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<JointPositionPIDVelocityController> controller,
      JointPositionPIDVelocityController::Create(config));

  // We replicate part of the internal calculations here to test the
  // controller. This at least allows us to test that the filtering is being
  // called when configured.
  ButterFilter2<eigenmath::VectorNd> butter_vel;
  ASSERT_TRUE(butter_vel.Init(eigenmath::VectorNd({{1.0, 1.0, 1.0}}),
                              1 / kDefaultCycleTime, 0.1 / kDefaultCycleTime));
  eigenmath::VectorNd target_velocities;
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  // Let the filter settle to a stable control command.
  for (int n = 0; n < 100; ++n) {
    butter_vel.Update(eigenmath::VectorNd({{1.0, 1.0, 1.0}}));
    INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
        target_velocities,
        controller->CalculateSetpoints(
            /*position_desired=*/eigenmath::VectorNd({{1, 1, 1}}),
            /*velocity_feedforward=*/eigenmath::VectorNd({{0, 0, 0}}),
            /*position_state=*/eigenmath::VectorNd({{1, 1, 1}}),
            /*velocity_state=*/eigenmath::VectorNd({{1.0, 1.0, 1.0}})));
  }
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
  EXPECT_THAT(target_velocities,
              IsApprox(kDefaultKd * eigenmath::VectorNd({{-1.0, -1.0, -1.0}})));
  // Change the desired position for one of the joints.
  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
      target_velocities,
      controller->CalculateSetpoints(
          /*position_desired=*/eigenmath::VectorNd({{1, 1, 1}}),
          /*velocity_feedforward=*/eigenmath::VectorNd({{0, 0, 0}}),
          /*position_state=*/eigenmath::VectorNd({{1, 1, 1}}),
          /*velocity_state=*/eigenmath::VectorNd({{1.0, 2.0, 1.0}})));
  butter_vel.Update(eigenmath::VectorNd({{1.0, 2.0, 1.0}}));
  // The command does not reflect the complete difference between state and
  // setpoint since the filter is not settled yet.
  EXPECT_THAT(target_velocities[1], Gt((-2.0) * kDefaultKd));
  EXPECT_THAT(target_velocities[1], Lt((-1.0) * kDefaultKd));
  EXPECT_THAT(target_velocities[1],
              DoubleNear((0.0 - butter_vel.GetOutput()[1]) * kDefaultKd, 1e-6));
  // Now run for more steps to let the filter settle.
  for (int n = 0; n < 100; ++n) {
    // There is a
    INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
        target_velocities,
        controller->CalculateSetpoints(
            /*position_desired=*/eigenmath::VectorNd({{1, 1, 1}}),
            /*velocity_feedforward=*/eigenmath::VectorNd({{0, 0, 0}}),
            /*position_state=*/eigenmath::VectorNd({{1, 1, 1}}),
            /*velocity_state=*/eigenmath::VectorNd({{1.0, 2.0, 1.0}})));
  }
  EXPECT_THAT(target_velocities,
              IsApprox(kDefaultKd * eigenmath::VectorNd({{-1.0, -2.0, -1.0}})));
}

TEST(ControllerWithFiltersTest, ControllerWithPositionFiltersResetWorks) {
  auto config = CreateDefaultConfig();
  for (int i = 0; i < kNdof; ++i) {
    config.set_k_i(i, 0);
  }
  config.set_position_filter_cuttoff_frequency_hz(0.1 / kDefaultCycleTime);
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<JointPositionPIDVelocityController> controller,
      JointPositionPIDVelocityController::Create(config));
  eigenmath::VectorNd target_velocities;
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  // Let the filter settle to a stable control command.
  for (int n = 0; n < 100; ++n) {
    INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
        target_velocities,
        controller->CalculateSetpoints(
            /*position_desired=*/eigenmath::VectorNd({{1.5, 1.5, 1.5}}),
            /*velocity_feedforward=*/eigenmath::VectorNd({{0, 0, 0}}),
            /*position_state=*/eigenmath::VectorNd({{1, 2, 1}}),
            /*velocity_state=*/eigenmath::VectorNd({{0.0, 0.0, 0.0}})));
  }
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
  EXPECT_THAT(target_velocities,
              IsApprox(kDefaultKp * eigenmath::VectorNd({{0.5, -0.5, 0.5}})));
  // Change the desired position for one of the joints.
  controller->Reset();
  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
      target_velocities,
      controller->CalculateSetpoints(
          /*position_desired=*/eigenmath::VectorNd({{1.5, 1.5, 1.5}}),
          /*velocity_feedforward=*/eigenmath::VectorNd({{0, 0, 0}}),
          /*position_state=*/eigenmath::VectorNd({{2, 2, 1}}),
          /*velocity_state=*/eigenmath::VectorNd({{0.0, 0.0, 0.0}})));
  // Since we reset the filter, the command now reflects the complete
  // difference between state and setpoint on the first iteration.
  EXPECT_THAT(target_velocities,
              IsApprox(kDefaultKp * eigenmath::VectorNd({{-0.5, -0.5, 0.5}})));
}

TEST(ControllerWithFiltersTest, ControllerWithVelocityFiltersResetWorks) {
  auto config = CreateDefaultConfig();
  config.set_velocity_filter_cuttoff_frequency_hz(0.1 / kDefaultCycleTime);
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<JointPositionPIDVelocityController> controller,
      JointPositionPIDVelocityController::Create(config));
  eigenmath::VectorNd target_velocities;
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  // Let the filter settle to a stable control command.
  for (int n = 0; n < 100; ++n) {
    INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
        target_velocities,
        controller->CalculateSetpoints(
            /*position_desired=*/eigenmath::VectorNd({{1, 1, 1}}),
            /*velocity_feedforward=*/eigenmath::VectorNd({{0, 0, 0}}),
            /*position_state=*/eigenmath::VectorNd({{1, 1, 1}}),
            /*velocity_state=*/eigenmath::VectorNd({{1.0, 1.0, 1.0}})));
  }
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
  EXPECT_THAT(target_velocities,
              IsApprox(kDefaultKd * eigenmath::VectorNd({{-1.0, -1.0, -1.0}})));
  // Reset the filter.
  controller->Reset();
  // Change the desired position for one of the joints.
  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
      target_velocities,
      controller->CalculateSetpoints(
          /*position_desired=*/eigenmath::VectorNd({{1, 1, 1}}),
          /*velocity_feedforward=*/eigenmath::VectorNd({{0, 0, 0}}),
          /*position_state=*/eigenmath::VectorNd({{1, 1, 1}}),
          /*velocity_state=*/eigenmath::VectorNd({{1.0, 2.0, 1.0}})));
  // Since we reset the filter, the command now reflects the complete
  // difference between state and setpoint on the first iteration.
  EXPECT_THAT(target_velocities,
              IsApprox(kDefaultKd * eigenmath::VectorNd({{-1.0, -2.0, -1.0}})));
}

TEST(NoMaxIntegrationControllerTest, EmptyMaxIntegralControlWorks) {
  auto config = CreateDefaultConfig();
  config.clear_max_integral_control();
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<JointPositionPIDVelocityController> controller,
      JointPositionPIDVelocityController::Create(config));
  eigenmath::VectorNd target_velocities;
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  for (int n = 0; n < 10; ++n) {
    INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
        target_velocities,
        controller->CalculateSetpoints(
            /*position_desired=*/eigenmath::VectorNd({{1.5, 1.5, 1.5}}),
            /*velocity_feedforward=*/eigenmath::VectorNd({{0, 0, 0}}),
            /*position_state=*/eigenmath::VectorNd({{1, 2, 1}}),
            /*velocity_state=*/eigenmath::VectorNd({{0.0, 0.0, 0.0}})));
  }
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
  EXPECT_THAT(target_velocities,
              IsApprox(kDefaultKp * eigenmath::VectorNd({{0.5, -0.5, 0.5}})));
}

}  // namespace
}  // namespace icon
}  // namespace intrinsic
