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

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <optional>
#include <utility>

#include "absl/memory/memory.h"
#include "absl/status/statusor.h"
#include "intrinsic/eigenmath/clamp.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/proto/joint_position_pid_velocity_controller_config.pb.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/icon/utils/realtime_status_or.h"
#include "intrinsic/math/signals/butter_filter2.h"

namespace intrinsic {
namespace icon {

// static
absl::StatusOr<std::unique_ptr<JointPositionPIDVelocityController>>
JointPositionPIDVelocityController::Create(
    intrinsic_proto::icon::JointPositionPidVelocityControllerConfig config) {
  if (config.k_p_size() > eigenmath::MAX_EIGEN_VECTOR_SIZE) {
    return InvalidArgumentError(
        "Number of joints exceeds maximum supported vector size.");
  }
  if (config.k_i_size() != config.k_p_size()) {
    return InvalidArgumentError("There is a mismatch in k_i and k_p size.");
  }
  if (config.k_d_size() != config.k_p_size()) {
    return InvalidArgumentError("There is a mismatch in k_d and k_p size.");
  }
  if (config.k_ff_size() != config.k_p_size()) {
    return InvalidArgumentError("There is a mismatch in k_ff and k_p size.");
  }
  if (std::any_of(config.k_p().cbegin(), config.k_p().cend(),
                  [](double k) { return !std::isfinite(k) || k < 0; })) {
    return InvalidArgumentError("All values in k_p should be finite and >= 0");
  }
  if (std::any_of(config.k_i().cbegin(), config.k_i().cend(),
                  [](double k) { return !std::isfinite(k) || k < 0; })) {
    return InvalidArgumentError("All values in k_i should be finite and >= 0");
  }
  if (std::any_of(config.k_d().cbegin(), config.k_d().cend(),
                  [](double k) { return !std::isfinite(k) || k < 0; })) {
    return InvalidArgumentError("All values in k_d should be finite and >= 0");
  }
  if (std::any_of(config.k_ff().cbegin(), config.k_ff().cend(), [](double k) {
        return !std::isfinite(k) || k < 0 || k > 1;
      })) {
    return InvalidArgumentError(
        "All values in k_ff should be finite and between 0 and 1.");
  }
  for (int i = 0; i < config.k_p_size(); ++i) {
    if (config.k_p().at(i) == 0 && config.k_i().at(i) > 0) {
      return InvalidArgumentError(
          "All values in k_p should be > 0 for degrees of freedom where k_i > "
          "0");
    }
  }
  if (!std::isfinite(config.cycle_time_seconds()) ||
      config.cycle_time_seconds() <= 0 ||
      !std::isfinite(1.0 / config.cycle_time_seconds())) {
    return InvalidArgumentError("cycle_time_seconds should be finite and > 0");
  }
  const double sampling_frequency_hz = 1.0 / config.cycle_time_seconds();
  if (config.has_position_filter_cuttoff_frequency_hz()) {
    const double cutoff = config.position_filter_cuttoff_frequency_hz();
    if (!std::isfinite(cutoff) || cutoff <= 0 ||
        !std::isfinite(cutoff * 2.0 * M_PI)) {
      return InvalidArgumentError(
          "position_filter_cuttoff_frequency_hz should be finite and > 0");
    }
    if (!(sampling_frequency_hz > std::numeric_limits<double>::epsilon()) ||
        !(cutoff < 0.5 * sampling_frequency_hz)) {
      return InvalidArgumentError(
          "position_filter_cuttoff_frequency_hz should be < "
          "(0.5/cycle_time_seconds).");
    }
  }
  if (config.has_velocity_filter_cuttoff_frequency_hz()) {
    const double cutoff = config.velocity_filter_cuttoff_frequency_hz();
    if (!std::isfinite(cutoff) || cutoff <= 0 ||
        !std::isfinite(cutoff * 2.0 * M_PI)) {
      return InvalidArgumentError(
          "velocity_filter_cuttoff_frequency_hz should be finite and > 0");
    }
    if (!(sampling_frequency_hz > std::numeric_limits<double>::epsilon()) ||
        !(cutoff < 0.5 * sampling_frequency_hz)) {
      return InvalidArgumentError(
          "velocity_filter_cuttoff_frequency_hz should be < "
          "(0.5/cycle_time_seconds).");
    }
  }
  eigenmath::VectorNd k_p = eigenmath::VectorNd::Zero(config.k_p_size());
  eigenmath::VectorNd k_i = eigenmath::VectorNd::Zero(config.k_p_size());
  eigenmath::VectorNd k_d = eigenmath::VectorNd::Zero(config.k_p_size());
  eigenmath::VectorNd k_ff = eigenmath::VectorNd::Zero(config.k_p_size());
  for (int i = 0; i < config.k_p_size(); ++i) {
    k_p[i] = config.k_p().at(i);
    k_i[i] = config.k_i().at(i);
    k_d[i] = config.k_d().at(i);
    k_ff[i] = config.k_ff().at(i);
  }

  eigenmath::VectorNd max_integral_control =
      eigenmath::VectorNd::Zero(config.k_p_size());
  if (config.max_integral_control_size() != 0) {
    if (config.max_integral_control_size() != config.k_p_size()) {
      return InvalidArgumentError(
          "There is a mismatch in max_integral_control and k_p size.");
    }
    if (std::any_of(config.max_integral_control().cbegin(),
                    config.max_integral_control().cend(),
                    [](double m) { return !std::isfinite(m) || m < 0; })) {
      return InvalidArgumentError(
          "All values in max_integral_control should be finite and >= 0");
    }
    for (int i = 0; i < config.max_integral_control_size(); ++i) {
      max_integral_control[i] = config.max_integral_control().at(i);
    }
  }

  if (config.max_velocity_command_size() != config.k_p_size()) {
    return InvalidArgumentError(
        "There is a mismatch in max_velocity_command and k_p size.");
  }
  if (std::any_of(config.max_velocity_command().cbegin(),
                  config.max_velocity_command().cend(),
                  [](double k) { return !std::isfinite(k) || k < 0; })) {
    return InvalidArgumentError(
        "All values in max_velocity_command should be finite and >= 0.");
  }
  eigenmath::VectorNd max_velocity_command =
      eigenmath::VectorNd::Zero(config.k_p_size());
  for (int i = 0; i < config.k_p_size(); ++i) {
    max_velocity_command[i] = config.max_velocity_command().at(i);
  }
  std::unique_ptr<ButterFilter2<eigenmath::VectorNd>> butter_pos = nullptr;
  std::optional<double> pos_cutoff_frequency = std::nullopt;
  if (config.position_filter_cuttoff_frequency_hz()) {
    pos_cutoff_frequency = config.position_filter_cuttoff_frequency_hz();
    butter_pos = std::make_unique<ButterFilter2<eigenmath::VectorNd>>();
  }
  std::unique_ptr<ButterFilter2<eigenmath::VectorNd>> butter_vel = nullptr;
  std::optional<double> vel_cutoff_frequency = std::nullopt;
  if (config.velocity_filter_cuttoff_frequency_hz()) {
    vel_cutoff_frequency = config.velocity_filter_cuttoff_frequency_hz();
    butter_vel = std::make_unique<ButterFilter2<eigenmath::VectorNd>>();
  }
  return absl::WrapUnique(new JointPositionPIDVelocityController(
      Params({
          .k_p = k_p,
          .k_i = k_i,
          .k_d = k_d,
          .k_ff = k_ff,
          .max_integral_control = max_integral_control,
          .max_velocity_command = max_velocity_command,
          .cycle_time_sec = config.cycle_time_seconds(),
          .position_filter_cuttoff_frequency_hz = pos_cutoff_frequency,
          .velocity_filter_cuttoff_frequency_hz = vel_cutoff_frequency,
      }),
      std::move(butter_pos), std::move(butter_vel)));
}

JointPositionPIDVelocityController::JointPositionPIDVelocityController(
    JointPositionPIDVelocityController::Params params,
    std::unique_ptr<ButterFilter2<eigenmath::VectorNd>>
        butterworth_position_filter,
    std::unique_ptr<ButterFilter2<eigenmath::VectorNd>>
        butterworth_velocity_filter)
    : params_(params),
      state_(params.k_p.size()),
      filters_({.butterworth_position_filter =
                    std::move(butterworth_position_filter),
                .butterworth_velocity_filter =
                    std::move(butterworth_velocity_filter)}) {}

RealtimeStatusOr<eigenmath::VectorNd>
JointPositionPIDVelocityController::CalculateSetpoints(
    const eigenmath::VectorNd& position_desired,
    const eigenmath::VectorNd& velocity_feedforward,
    const eigenmath::VectorNd& position_state,
    const eigenmath::VectorNd& velocity_state) {
  if (position_state.size() != position_desired.size()) {
    return InvalidArgumentError(
        "position_state and position_desired sizes don't match.");
  }
  if (velocity_feedforward.size() != position_desired.size()) {
    return InvalidArgumentError(
        "velocity_feedforward and position_desired sizes don't match.");
  }
  if (velocity_state.size() != position_desired.size()) {
    return InvalidArgumentError(
        "velocity_state and position_desired sizes don't match.");
  }
  if (position_desired.size() != params_.k_p.size()) {
    return InvalidArgumentError(
        "The position_desired and control gain sizes don't match.");
  }
  const eigenmath::VectorNd position_error = position_desired - position_state;
  const eigenmath::VectorNd velocity_error =
      velocity_feedforward - velocity_state;

  eigenmath::VectorNd position_error_filtered;
  eigenmath::VectorNd velocity_error_filtered;

  if (filters_.butterworth_position_filter != nullptr &&
      params_.position_filter_cuttoff_frequency_hz.has_value()) {
    if (!filters_.position_butterworth_initialized) {
      if (!filters_.butterworth_position_filter->Init(
              position_state, 1 / params_.cycle_time_sec,
              params_.position_filter_cuttoff_frequency_hz.value())) {
        return InternalError(
            "Failed to initialize Butterworth position filter. It may be worth "
            "checking the cuttoff frequency.");
      }
      filters_.position_butterworth_initialized = true;
    }
    filters_.butterworth_position_filter->Update(position_state);
    position_error_filtered =
        position_desired - filters_.butterworth_position_filter->GetOutput();
  } else {
    position_error_filtered = position_error;
  }

  if (filters_.butterworth_velocity_filter != nullptr &&
      params_.velocity_filter_cuttoff_frequency_hz.has_value()) {
    if (!filters_.velocity_butterworth_initialized) {
      if (!filters_.butterworth_velocity_filter->Init(
              velocity_state, 1 / params_.cycle_time_sec,
              params_.velocity_filter_cuttoff_frequency_hz.value())) {
        return InternalError(
            "Failed to initialize Butterworth velocity filter. It may be worth "
            "checking the cuttoff frequency.");
      }
      filters_.velocity_butterworth_initialized = true;
    }
    filters_.butterworth_velocity_filter->Update(velocity_state);
    velocity_error_filtered = velocity_feedforward -
                              filters_.butterworth_velocity_filter->GetOutput();
  } else {
    velocity_error_filtered = velocity_error;
  }

  eigenmath::VectorNd is_not_saturated =
      state_.previous_velocity_command.cwiseAbs()
          .cwiseLess(params_.max_velocity_command)
          .cast<double>();
  state_.integral_control += is_not_saturated.cwiseProduct(
      params_.k_i.cwiseProduct(position_error * params_.cycle_time_sec));
  // Saturate the integral term to +/- the max_integral_control for each DoF.
  if (!eigenmath::ClampVector(-params_.max_integral_control,
                              params_.max_integral_control,
                              state_.integral_control)) {
    return InternalError("Failed to clamp integral control term!");
  }

  eigenmath::VectorNd velocity_control_command =
      params_.k_p.cwiseProduct(position_error_filtered) +
      state_.integral_control +
      params_.k_d.cwiseProduct(velocity_error_filtered) +
      params_.k_ff.cwiseProduct(velocity_feedforward);

  if (!eigenmath::ClampVector(-params_.max_velocity_command,
                              params_.max_velocity_command,
                              velocity_control_command)) {
    return InternalError("Failed to clamp velocity command!");
  }
  state_.previous_velocity_command = velocity_control_command;
  return velocity_control_command;
}

void JointPositionPIDVelocityController::Reset() {
  state_ = State(params_.k_p.size());
  filters_.position_butterworth_initialized = false;
  filters_.velocity_butterworth_initialized = false;
}
}  // namespace icon
}  // namespace intrinsic
