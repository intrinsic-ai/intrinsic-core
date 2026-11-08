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

#include "intrinsic/icon/control/algorithms/joint_position_pid_torque_controller.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <memory>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "internal/testing.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/control/joint_position_command.h"
#include "intrinsic/icon/proto/joint_position_pid_torque_controller_config.pb.h"
#include "intrinsic/kinematics/types/joint_state.h"

namespace intrinsic {

namespace icon {

namespace {

using ::absl_testing::StatusIs;
using ::testing::AllOf;
using ::testing::DoubleEq;
using ::testing::HasSubstr;

intrinsic_proto::icon::JointPositionPidTorqueControllerConfig CreateConfig(
    double kp, double ki, double kd, double max_torque, double alpha,
    double max_torque_delta_per_timestep, double max_integral_torque,
    int integration_window_size, double integration_enable_range,
    double timestep) {
  intrinsic_proto::icon::JointPositionPidTorqueControllerConfig config;
  config.set_kp(kp);
  config.set_ki(ki);
  config.set_kd(kd);
  config.set_max_torque(max_torque);
  config.set_alpha(alpha);
  config.set_max_torque_delta_per_timestep(max_torque_delta_per_timestep);
  config.set_max_integral_torque(max_integral_torque);
  config.set_integration_window_seconds(integration_window_size * timestep);
  config.set_integration_enable_range(integration_enable_range);
  config.set_timestep(timestep);
  return config;
}

intrinsic_proto::icon::JointPositionPidTorqueControllerConfig
CreateDefaultConfig() {
  return CreateConfig(
      /*kp =*/1,
      /*ki =*/1,
      /*kd =*/1,
      /*max_torque =*/1,
      /*alpha =*/1.0,  // When alpha is one, the smoothed velocity
                       // estimate is equal to the sensed velocity.
      /*max torque delta =*/100,
      /*max windup =*/10.,
      /*integration window size =*/100,
      /*integration_enable_range =*/0.,
      /*timestep =*/0.001);
}

TEST(CreateControllerTest, InvalidKp) {
  auto config = CreateDefaultConfig();
  config.set_kp(-1);
  EXPECT_THAT(JointPositionPidTorqueController::Create(config),
              StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("kp")));
}

TEST(CreateControllerTest, InvalidKi) {
  auto config = CreateDefaultConfig();
  config.set_ki(-1);
  EXPECT_THAT(JointPositionPidTorqueController::Create(config),
              StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("ki")));
}

TEST(CreateControllerTest, InvalidKd) {
  auto config = CreateDefaultConfig();
  config.set_kd(-1);
  EXPECT_THAT(JointPositionPidTorqueController::Create(config),
              StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("kd")));
}

TEST(CreateControllerTest, InvalidMaxTorque) {
  auto config = CreateDefaultConfig();
  config.set_max_torque(-1);
  EXPECT_THAT(
      JointPositionPidTorqueController::Create(config),
      StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("max_torque")));
}

TEST(CreateControllerTest, InvalidAlpha) {
  auto config = CreateDefaultConfig();
  config.set_alpha(-1);
  EXPECT_THAT(JointPositionPidTorqueController::Create(config),
              StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("alpha")));
  config.set_alpha(1.5);
  EXPECT_THAT(JointPositionPidTorqueController::Create(config),
              StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("alpha")));
}

TEST(CreateControllerTest, InvalidMaxTorqueDeltaPerTimestep) {
  auto config = CreateDefaultConfig();
  config.set_max_torque_delta_per_timestep(-1);
  EXPECT_THAT(JointPositionPidTorqueController::Create(config),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("max_torque_delta_per_timestep")));
}

TEST(CreateControllerTest, InvalidMaxIntegralTorque) {
  auto config = CreateDefaultConfig();
  config.set_max_integral_torque(-1);
  EXPECT_THAT(JointPositionPidTorqueController::Create(config),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("max_integral_torque")));
}

TEST(CreateControllerTest, InvalidIntegrationWindowSeconds) {
  auto config = CreateDefaultConfig();
  config.set_integration_window_seconds(-1);
  EXPECT_THAT(JointPositionPidTorqueController::Create(config),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("integration_window_seconds")));
}

TEST(CreateControllerTest, InvalidIntegrationWindowSAndTimestep) {
  auto config = CreateDefaultConfig();
  config.set_integration_window_seconds(0.1);
  config.set_timestep(0.2);
  EXPECT_THAT(JointPositionPidTorqueController::Create(config),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       AllOf(HasSubstr("integration_window_seconds"),
                             HasSubstr("timestep"))));
}

TEST(CreateControllerTest, InvalidIntegrationEnableRange) {
  auto config = CreateDefaultConfig();
  config.set_integration_enable_range(-1);
  EXPECT_THAT(JointPositionPidTorqueController::Create(config),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("integration_enable_range")));
}
TEST(CreateControllerTest, InvalidTimestep) {
  auto config = CreateDefaultConfig();
  config.set_timestep(0);
  EXPECT_THAT(
      JointPositionPidTorqueController::Create(config),
      StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("timestep")));
}

// Test fixture for testing feedback control of PID joint position controller.
class TorqueModeHybridControllerTest : public ::testing::Test {
 public:
  void SetUp() override {
    config_ = CreateDefaultConfig();
    ASSERT_OK_AND_ASSIGN(controller_,
                         JointPositionPidTorqueController::Create(config_));
  }

  void SetNewConfig(double kp, double ki, double kd, double max_torque,
                    double alpha, double max_torque_delta_per_timestep,
                    double max_integral_torque, int integration_window_size,
                    double integration_enable_range, double timestep) {
    config_ = CreateConfig(kp, ki, kd, max_torque, alpha,
                           max_torque_delta_per_timestep, max_integral_torque,
                           integration_window_size, integration_enable_range,
                           timestep);
    ASSERT_OK_AND_ASSIGN(controller_,
                         JointPositionPidTorqueController::Create(config_));
  }

 protected:
  intrinsic_proto::icon::JointPositionPidTorqueControllerConfig config_;
  std::unique_ptr<JointPositionPidTorqueController> controller_;
};

// Test for successful setting of drive set points with a good configuration
//   and zero error (targets = sensors).
TEST_F(TorqueModeHybridControllerTest, ZeroError) {
  auto command = JointPositionCommand::Create(eigenmath::VectorNd({{1}}),
                                              eigenmath::VectorNd({{1}}))
                     .value();
  JointStatePV state;
  state.position = eigenmath::VectorNd({{1}});
  state.velocity = eigenmath::VectorNd({{1}});

  controller_->CalculateFeedbackSetPoints(command, state);
  controller_->GetTargetTorque();
  EXPECT_EQ(controller_->GetTargetTorque(), 0);
}

// Test for PD control with torque output inside min/max torque bounds and
// position error greater than deadband.
TEST_F(TorqueModeHybridControllerTest, PDControlWithinTorqueBounds) {
  auto command = JointPositionCommand::Create(eigenmath::VectorNd({{1}}),
                                              eigenmath::VectorNd({{1}}))
                     .value();
  JointStatePV state;
  state.position = eigenmath::VectorNd({{1.01}});
  state.velocity = eigenmath::VectorNd({{1}});

  controller_->CalculateFeedbackSetPoints(command, state);
  EXPECT_EQ(controller_->GetTargetTorque(), config_.kp() * (1 - 1.01));
}

// Test for PD control with torque output outside min/max torque bounds.
TEST_F(TorqueModeHybridControllerTest, PDControlOutsideTorqueBoundsHigh) {
  auto command = JointPositionCommand::Create(eigenmath::VectorNd({{1}}),
                                              eigenmath::VectorNd({{1}}))
                     .value();
  JointStatePV state;
  state.position = eigenmath::VectorNd({{10}});
  state.velocity = eigenmath::VectorNd({{1}});

  controller_->CalculateFeedbackSetPoints(command, state);
  EXPECT_EQ(controller_->GetTargetTorque(), -config_.max_torque());
}

// Test for PD control with torque output outside min/max torque bounds.
TEST_F(TorqueModeHybridControllerTest, PDControlOutsideTorqueBoundsLow) {
  auto command = JointPositionCommand::Create(eigenmath::VectorNd({{1}}),
                                              eigenmath::VectorNd({{1}}))
                     .value();
  JointStatePV state;
  state.position = eigenmath::VectorNd({{-10}});
  state.velocity = eigenmath::VectorNd({{1}});

  controller_->CalculateFeedbackSetPoints(command, state);
  controller_->GetTargetTorque();
  EXPECT_EQ(controller_->GetTargetTorque(), config_.max_torque());
}

// Test for PD control with torque output inside min/max torque bounds and
// velocity error, with smoothing enabled.
TEST_F(TorqueModeHybridControllerTest, PDControlWithVelocityErrorAndSmoothing) {
  SetNewConfig(/*kp =*/1,
               /*ki =*/1,
               /*kd =*/1,
               /*max_torque =*/1,
               /*alpha =*/0.5,  // Enable velocity smoothing
               /*max_torque_delta_per_timestep =*/100,
               /*max_integral_torque =*/10.,
               /*integration_window_size =*/100,
               /*integration_enable_range =*/0.,
               /*timestep =*/0.001);

  auto command = JointPositionCommand::Create(eigenmath::VectorNd({{1}}),
                                              eigenmath::VectorNd({{0}}))
                     .value();
  JointStatePV state;
  state.position = eigenmath::VectorNd({{1}});
  state.velocity = eigenmath::VectorNd({{1}});

  // With alpha = 0.5, one cycle should update the velocity estimate to
  // 0.5 * 1.0 + (1 - 0.5) * 0.0 = 0.5
  controller_->CalculateFeedbackSetPoints(command, state);
  EXPECT_EQ(controller_->GetTargetTorque(), config_.kd() * (0.0 - 0.5));

  controller_->CalculateFeedbackSetPoints(command, state);
  // 0.5 * 1.0 + (1 - 0.5) * 0.5 = 0.75
  EXPECT_EQ(controller_->GetTargetTorque(), config_.kd() * (0.0 - 0.75));
}

// Test for PID control with torque output inside min/max torque bounds and
// small position error with enabled integrator.
TEST_F(TorqueModeHybridControllerTest,
       PIDControlWithPositionErrorAndIntegrator) {
  SetNewConfig(/*kp =*/0,
               /*ki =*/1000,
               /*kd =*/1,
               /*max_torque =*/1,
               /*alpha =*/1.0,  // Enable velocity smoothing
               /*max_torque_delta_per_timestep =*/100,
               /*max_integral_torque =*/10.,
               /*integration_window_size =*/2,
               /*integration_enable_range =*/0.5,
               /*timestep =*/0.001);

  auto command = JointPositionCommand::Create(eigenmath::VectorNd({{1}}),
                                              eigenmath::VectorNd({{0}}))
                     .value();
  JointStatePV state;
  state.position = eigenmath::VectorNd({{0.6}});
  state.velocity = eigenmath::VectorNd({{0}});

  controller_->CalculateFeedbackSetPoints(command, state);
  // The integrator has one timestep's worth of accumulated error.
  // 1000 * (1.0 - 0.6) * 0.001 = 0.4
  EXPECT_EQ(controller_->GetTargetTorque(), 0.4);

  controller_->CalculateFeedbackSetPoints(command, state);
  // Two timesteps, so the i component is twice as large (2 * 0.4 = 0.8)
  EXPECT_EQ(controller_->GetTargetTorque(), 0.8);
}

// Test for PD control with torque output inside min/max torque bounds and
// velocity error, with smoothing enabled.
TEST_F(TorqueModeHybridControllerTest,
       PDControlWithVelocityErrorAndSmoothingShouldResetCorrectly) {
  SetNewConfig(/*kp =*/1,
               /*ki =*/1,
               /*kd =*/1,
               /*max_torque =*/1,
               /*alpha =*/0.5,  // Enable velocity smoothing
               /*max_torque_delta_per_timestep =*/100,
               /*max_integral_torque =*/10.,
               /*integration_window_size =*/100,
               /*integration_enable_range =*/0.,
               /*timestep =*/0.001);

  auto command = JointPositionCommand::Create(eigenmath::VectorNd({{1}}),
                                              eigenmath::VectorNd({{0}}))
                     .value();
  JointStatePV state;
  state.position = eigenmath::VectorNd({{1}});
  state.velocity = eigenmath::VectorNd({{1}});

  controller_->CalculateFeedbackSetPoints(command, state);
  controller_->CalculateFeedbackSetPoints(command, state);

  EXPECT_EQ(controller_->GetTargetTorque(), -0.75);

  controller_->Reset();
  state.velocity = eigenmath::VectorNd({{0}});
  controller_->CalculateFeedbackSetPoints(command, state);
  // Any feedback controller state from before the restart should be gone.
  EXPECT_EQ(controller_->GetTargetTorque(), 0);
}

// Test for PID control with torque output inside min/max torque bounds and
// small position error with enabled integrator.
TEST_F(TorqueModeHybridControllerTest,
       PIDControlWithPositionErrorAndIntegratorShouldResetOnEnable) {
  SetNewConfig(/*kp =*/0,
               /*ki =*/1000,
               /*kd =*/1,
               /*max_torque =*/10,
               /*alpha =*/1.0,  // Enable velocity smoothing
               /*max_torque_delta_per_timestep =*/100,
               /*max_integral_torque =*/10.,
               /*integration_window_size =*/3,
               /*integration_enable_range =*/0.5,
               /*timestep =*/0.001);

  auto command = JointPositionCommand::Create(eigenmath::VectorNd({{1}}),
                                              eigenmath::VectorNd({{0}}))
                     .value();
  JointStatePV state;
  state.position = eigenmath::VectorNd({{0.6}});
  state.velocity = eigenmath::VectorNd({{0}});

  controller_->CalculateFeedbackSetPoints(command, state);
  controller_->CalculateFeedbackSetPoints(command, state);
  controller_->CalculateFeedbackSetPoints(command, state);

  EXPECT_THAT(controller_->GetTargetTorque(), DoubleEq(1.2));

  controller_->Reset();
  state.position = eigenmath::VectorNd({{1}});
  controller_->CalculateFeedbackSetPoints(command, state);
  // Any feedback controller state from before the restart should be gone.
  EXPECT_EQ(controller_->GetTargetTorque(), 0);
}

// Test for pure P control with torque output limited by
// max_torque_delta_per_timestep. We only want the delta to kick in _after_ the
// first time step.
TEST_F(TorqueModeHybridControllerTest,
       PControlWithMaxTorqueDeltaShouldIgnoreDeltaInFirstStep) {
  SetNewConfig(/*kp =*/1,
               /*ki =*/0,
               /*kd =*/0,
               /*max_torque =*/1,
               /*alpha =*/1,  // no smoothing
               /*max_torque_delta_per_timestep =*/0.1,
               /*max_integral_torque =*/10.,
               /*integration_window_size =*/100,
               /*integration_enable_range =*/0.,
               /*timestep =*/0.001);

  JointStatePV state;
  state.position = eigenmath::VectorNd({{0}});
  state.velocity = eigenmath::VectorNd({{0}});
  EXPECT_TRUE(controller_->CalculateFeedbackSetPoints(
      JointPositionCommand::Create(eigenmath::VectorNd({{1}}),
                                   eigenmath::VectorNd({{0}}))
          .value(),
      state));
  // Not limited by max_delta, because this is the first timestep.
  EXPECT_EQ(controller_->GetTargetTorque(), 1);

  EXPECT_TRUE(controller_->CalculateFeedbackSetPoints(
      JointPositionCommand::Create(eigenmath::VectorNd({{0}}),
                                   eigenmath::VectorNd({{0}}))
          .value(),
      state));
  // In every subsequent step, max_delta should apply (with a higher max_delta,
  // the output would jump straight back to zero, since state and command are
  // the same now).
  EXPECT_EQ(controller_->GetTargetTorque(), 0.9);
}

}  // namespace
}  // namespace icon
}  // namespace intrinsic
