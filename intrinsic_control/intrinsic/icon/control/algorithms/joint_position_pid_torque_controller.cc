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

#include <algorithm>
#include <cmath>
#include <memory>
#include <numeric>
#include <optional>

#include "absl/algorithm/container.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "intrinsic/icon/control/joint_position_command.h"
#include "intrinsic/icon/proto/joint_position_pid_torque_controller_config.pb.h"
#include "intrinsic/icon/utils/log.h"
#include "intrinsic/kinematics/types/joint_state.h"

namespace intrinsic {
namespace icon {

// static
absl::StatusOr<std::unique_ptr<JointPositionPidTorqueController>>
JointPositionPidTorqueController::Create(
    intrinsic_proto::icon::JointPositionPidTorqueControllerConfig config) {
  // Basic sanity checks on the config parameters.
  if (!std::isfinite(config.kp()) || config.kp() < 0) {
    return absl::InvalidArgumentError(
        absl::StrCat("kp must be finite and >= 0 but is: ", config.kp()));
  }
  if (!std::isfinite(config.ki()) || config.ki() < 0) {
    return absl::InvalidArgumentError(
        absl::StrCat("ki must be finite and >= 0 but is: ", config.ki()));
  }
  if (!std::isfinite(config.kd()) || config.kd() < 0) {
    return absl::InvalidArgumentError(
        absl::StrCat("kd must be finite and >= 0 but is: ", config.kd()));
  }
  if (!std::isfinite(config.max_torque()) || config.max_torque() < 0) {
    return absl::InvalidArgumentError(absl::StrCat(
        "max_torque must be finite and >= 0 but is: ", config.max_torque()));
  }
  if (!std::isfinite(config.alpha()) || config.alpha() < 0 ||
      config.alpha() > 1) {
    return absl::InvalidArgumentError(
        absl::StrCat("alpha must be finite and in the interval [0,1] but is: ",
                     config.alpha()));
  }
  if (!std::isfinite(config.max_torque_delta_per_timestep()) ||
      config.max_torque_delta_per_timestep() < 0) {
    return absl::InvalidArgumentError(absl::StrCat(
        "max_torque_delta_per_timestep must be finite and >= 0 but is: ",
        config.max_torque_delta_per_timestep()));
  }
  if (!std::isfinite(config.max_integral_torque()) ||
      config.max_integral_torque() < 0) {
    return absl::InvalidArgumentError(
        absl::StrCat("max_integral_torque must be finite and >= 0 but is: ",
                     config.max_integral_torque()));
  }
  if (!std::isfinite(config.integration_enable_range()) ||
      config.integration_enable_range() < 0) {
    return absl::InvalidArgumentError(absl::StrCat(
        "integration_enable_range must be finite and >= 0 but is: ",
        config.integration_enable_range()));
  }
  if (!std::isfinite(config.timestep()) || config.timestep() <= 0) {
    return absl::InvalidArgumentError(absl::StrCat(
        "timestep must be finite and > 0 but is: ", config.timestep()));
  }
  if (!std::isfinite(config.integration_window_seconds()) ||
      config.integration_window_seconds() < 0) {
    return absl::InvalidArgumentError(absl::StrCat(
        "integration_window_seconds must be finite and >= 0 but is: ",
        config.integration_window_seconds()));
  }
  if (config.integration_window_seconds() < config.timestep()) {
    return absl::InvalidArgumentError(
        absl::StrCat("integration_window_seconds must be >= timestep but is: ",
                     config.integration_window_seconds()));
  }
  constexpr int kMaxIntegrationWindowSteps = 1'000'000;
  const double window_steps =
      config.integration_window_seconds() / config.timestep();
  if (!std::isfinite(window_steps) || window_steps < 1.0 ||
      window_steps > static_cast<double>(kMaxIntegrationWindowSteps)) {
    return absl::InvalidArgumentError(absl::StrCat(
        "integration_window_seconds / timestep must be finite and in [1, ",
        kMaxIntegrationWindowSteps, "] but is: ", window_steps));
  }
  return absl::WrapUnique(new JointPositionPidTorqueController(Params(
      {.kp = config.kp(),
       .ki = config.ki(),
       .kd = config.kd(),
       .max_torque = config.max_torque(),
       .alpha = config.alpha(),
       .max_torque_delta_per_timestep = config.max_torque_delta_per_timestep(),
       .max_integral_torque = config.max_integral_torque(),
       .integration_window_seconds = config.integration_window_seconds(),
       .integration_enable_range = config.integration_enable_range(),
       .timestep = config.timestep()})));
}

JointPositionPidTorqueController::JointPositionPidTorqueController(
    JointPositionPidTorqueController::Params params)
    : params_(params),
      state_(params.integration_window_seconds / params.timestep) {}

bool JointPositionPidTorqueController::CalculateFeedbackSetPoints(
    const JointPositionCommand& input_command, const JointStatePV& state) {
  // Run PD control on current position setpoint to convert to torque.
  // motor_torque = kp * (motor_pos_des - motor_pos_obs) + kd * (motor_vel_des -
  // motor_vel_obs)
  double position_error = input_command.position()[0] - state.position[0];
  double new_torque = position_error * params_.kp;

  // Here we implement smoothing to reduce the standard deviation of the
  // velocity estimate from the motor. There is a trade-off between lower
  // std_dev and greater delay in tracking changes to the true velocity signal.
  state_.smoothed_velocity_estimate =
      params_.alpha * state.velocity[0] +
      (1 - params_.alpha) * state_.smoothed_velocity_estimate;

  if (!input_command.velocity_feedforward().has_value()) {
    return false;
  }
  // Use the smoothed estimate for feedback control.
  double velocity_error = input_command.velocity_feedforward().value()[0] -
                          state_.smoothed_velocity_estimate;
  new_torque += velocity_error * params_.kd;

  // If we are close to the goal, turn on the integrator.
  if (std::fabs(position_error) < params_.integration_enable_range) {
    *state_.integral_itr = position_error * params_.timestep;
    state_.integral_control_effort =
        params_.ki *
        std::accumulate(state_.integral.begin(), state_.integral.end(), 0.);
  } else {
    // set the integral to 0 to prevent windup.
    *state_.integral_itr = 0;
    state_.integral_control_effort = 0;
  }

  // reset the integral_itr if it's greater than the buffer size
  if (++state_.integral_itr == state_.integral.end()) {
    state_.integral_itr = state_.integral.begin();
  }

  state_.integral_control_effort =
      std::clamp(state_.integral_control_effort, -params_.max_integral_torque,
                 params_.max_integral_torque);

  new_torque += state_.integral_control_effort;

  if (state_.target_torque.has_value()) {
    // First order torque smoothing
    if (new_torque - *state_.target_torque >
        params_.max_torque_delta_per_timestep) {
      new_torque =
          *state_.target_torque + params_.max_torque_delta_per_timestep;
      INTRINSIC_RT_LOG_THROTTLED(WARNING)
          << "requested torque delta greater than configured max.";
    } else if (new_torque - *state_.target_torque <
               -params_.max_torque_delta_per_timestep) {
      new_torque =
          *state_.target_torque - params_.max_torque_delta_per_timestep;
      INTRINSIC_RT_LOG_THROTTLED(WARNING)
          << "requested torque delta greater than configured max.";
    }
  }
  // Check for exceeding the user defined min/max torque.
  state_.target_torque =
      std::clamp(new_torque, -params_.max_torque, params_.max_torque);
  return true;
}

double JointPositionPidTorqueController::GetTargetTorque() {
  return state_.target_torque.value_or(0.0);
}

void JointPositionPidTorqueController::Reset() {
  state_.smoothed_velocity_estimate = 0.0;
  state_.target_torque = std::nullopt;
  // Reset the integral
  state_.integral_itr = state_.integral.begin();
  absl::c_fill(state_.integral, 0);
  state_.integral_control_effort = 0;
}
}  // namespace icon
}  // namespace intrinsic
