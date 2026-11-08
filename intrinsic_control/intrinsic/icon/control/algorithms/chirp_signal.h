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

#ifndef INTRINSIC_ICON_CONTROL_ALGORITHMS_CHIRP_SIGNAL_H_
#define INTRINSIC_ICON_CONTROL_ALGORITHMS_CHIRP_SIGNAL_H_

#include <vector>

#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "intrinsic/icon/utils/realtime_status_or.h"
#include "intrinsic/kinematics/types/joint_state.h"

namespace intrinsic::icon {

class ChirpSignal {
 public:
  struct Result {
    double position;
    double velocity;
    double acceleration;
  };

  // Instantiates a chirp signal generator. The chirp signal is defined by its
  // initial 'amplitude', an overall 'time_horizon', a 'starting_frequency' and
  // a 'final_frequency', which is reached exactly when 'time_horizon' has
  // passed.
  ChirpSignal(double amplitude, double time_horizon, double starting_frequency,
              double final_frequency);

  // Returns a triple of position, velocity and acceleration for the chirp
  // signal at time 't'.
  Result Evaluate(double t);

 private:
  const double A;       // Chirp initial amplitude. NOLINT.
  const double T;       // Time at reaching target frequency [sec]. NOLINT.
  const double f0;      // Starting frequency [Hz]. NOLINT.
  const double f1;      // Target frequency [Hz]. NOLINT.
  const double scale_;  // Convenience definition.
};

class MultiJointChirpSignal {
 public:
  // Creates a chirp signal generator for multiple joints. For each joint,
  // the chirp signal is defined by its initial 'joint_amplitudes', an overall
  // 'time_horizon', a 'starting_frequency' and a 'final_frequency', which is
  // reached exactly when 'time_horizon' has passed. The number of joints is
  // assumed to be equal to the number of provided 'joint_amplitudes'. Returns
  // 'kFailedPreconditionError' in case of empty 'joint_amplitudes'.
  static absl::StatusOr<MultiJointChirpSignal> Create(
      absl::Span<const double> joint_amplitudes, double time_horizon,
      double starting_frequency, double final_frequency);

  // Returns a joint state consisting of position, velocity and acceleration for
  // the chirp signal at time 't' for every individual joint.
  RealtimeStatusOr<JointStatePVA> Evaluate(double t);

 private:
  MultiJointChirpSignal(absl::Span<const double> joint_amplitudes,
                        double time_horizon, double starting_frequency,
                        double final_frequency);

  std::vector<ChirpSignal> joint_chirps_;
};

}  // namespace intrinsic::icon

#endif  // INTRINSIC_ICON_CONTROL_ALGORITHMS_CHIRP_SIGNAL_H_
