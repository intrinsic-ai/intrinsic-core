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

#include "intrinsic/icon/control/algorithms/chirp_signal.h"

#include <cmath>
#include <cstddef>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "intrinsic/icon/utils/realtime_status_macro.h"
#include "intrinsic/icon/utils/realtime_status_or.h"
#include "intrinsic/kinematics/types/joint_state.h"

namespace intrinsic::icon {

ChirpSignal::ChirpSignal(double amplitude, double time_horizon,
                         double starting_frequency, double final_frequency)
    : A(amplitude),
      T(time_horizon),
      f0(starting_frequency),
      f1(final_frequency),
      scale_((f1 - f0) / T) {}

ChirpSignal::Result ChirpSignal::Evaluate(double t) {
  Result result;

  const double t_squared = std::pow(t, 2);

  // Position chirp signal.
  result.position = A * (1 - t / T) *
                    std::sin(2 * M_PI * (f0 * t + (scale_ * t_squared) / 2));

  // First derivative (velocity), equation generated via Wolfram alpha.
  result.velocity =
      2 * M_PI * A * (1 - t / T) * (f0 + scale_ * t) *
          std::cos(2 * M_PI * (f0 * t + (scale_ * t_squared) / 2)) -
      (A * std::sin(2 * M_PI * (f0 * t + (scale_ * t_squared) / 2))) / T;

  // Second derivative (acceleration), equation generated via Wolfram alpha.
  result.acceleration =
      -4 * std::pow(M_PI, 2) * A * (1 - t / T) *
          std::pow((f0 + scale_ * t), 2) *
          std::sin(2 * M_PI * (f0 * t + (scale_ * t_squared) / 2)) -
      (4 * M_PI * A * (f0 + scale_ * t) *
       std::cos(2 * M_PI * (f0 * t + (scale_ * t_squared) / 2))) /
          T +
      2 * M_PI * A * scale_ * (1 - t / T) *
          std::cos(2 * M_PI * (f0 * t + (scale_ * t_squared) / 2));

  return result;
}

/*static*/
absl::StatusOr<MultiJointChirpSignal> MultiJointChirpSignal::Create(
    const absl::Span<const double> joint_amplitudes, double time_horizon,
    double starting_frequency, double final_frequency) {
  if (joint_amplitudes.empty()) {
    return absl::FailedPreconditionError("joint_amplitudes must not be empty.");
  }

  return MultiJointChirpSignal(joint_amplitudes, time_horizon,
                               starting_frequency, final_frequency);
}

MultiJointChirpSignal::MultiJointChirpSignal(
    const absl::Span<const double> joint_amplitudes, double time_horizon,
    double starting_frequency, double final_frequency) {
  for (size_t i = 0; i < joint_amplitudes.size(); i++) {
    joint_chirps_.push_back(ChirpSignal(joint_amplitudes[i], time_horizon,
                                        starting_frequency, final_frequency));
  }
}

RealtimeStatusOr<JointStatePVA> MultiJointChirpSignal::Evaluate(double t) {
  JointStatePVA joint_state;
  INTRINSIC_RT_RETURN_IF_ERROR(joint_state.SetSize(joint_chirps_.size()));

  // Evaluate chirp for all joint DoF.
  for (size_t i = 0; i < joint_chirps_.size(); i++) {
    ChirpSignal::Result result = joint_chirps_[i].Evaluate(t);
    joint_state.position(i) = result.position;
    joint_state.velocity(i) = result.velocity;
    joint_state.acceleration(i) = result.acceleration;
  }
  return joint_state;
}

}  // namespace intrinsic::icon
