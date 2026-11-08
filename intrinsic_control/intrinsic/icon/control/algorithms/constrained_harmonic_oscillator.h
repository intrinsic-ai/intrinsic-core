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

#ifndef INTRINSIC_ICON_CONTROL_ALGORITHMS_CONSTRAINED_HARMONIC_OSCILLATOR_H_
#define INTRINSIC_ICON_CONTROL_ALGORITHMS_CONSTRAINED_HARMONIC_OSCILLATOR_H_

#include <array>

#include "intrinsic/icon/utils/realtime_status_or.h"

namespace intrinsic::icon {

// Implements a velocity-, acceleration- and jerk-constrained single-DoF
// harmonic oscillator equation. The original idea is based on the paper:
//
// [1] "An Admittance Controller with a Jerk Limiter for Position-Controlled
// Robots" by Mae Ryusei and Kikuuwe Ryo, Hiroshima University, Japan.
// https://doi.org/10.51094/jxiv.444
//
// In this implementation, we extend the original single-DoF algorithm and offer
// the option to relax velocity limits in case of infeasibility, which is
// sufficient to restore feasibility while maintaining strict acceleration and
// jerk limits, by construction of the algorithm.
//
// We use a discrete-time formulation on position-level only, which implies that
// the history of the last three position coordinate values must be provided by
// the caller. This allows a straight-forward application of the algorithm to
// non-Euclidean manifolds.
//
// Important notice: different from the classical harmonic oscillator, position
// references do not enter the control law through feedback on a position error
// term. Instead, position references are directly used as feedforwards on the
// position signal, but they are rate-limited to adhere to limits.

// Default jerk limit multiplier at high acceleration
constexpr double kDefaultJyScaling = 0.5;
// Default jerk limit intercept
constexpr double kDefaultJsScaling = 1.3;

// Jerk limit hyperparameters, c.f. Section 3.2 in [1]
struct JerkLimitHyperparameters {
  double jerk_y_scaling =
      kDefaultJyScaling;  // Jerk limit multiplier at high acceleration
  double jerk_s_scaling = kDefaultJsScaling;  // Defines jerk limit intercept
};

// If the provided VAJ limits are not constant (may vary over repeated calls to
// the control law) the system might become infeasible (have no solution subject
// to the provided constraints). This struct defines options to restore
// feasibility, e.g. by automatic relaxation of selected limits.
enum class FeasibilityRestorationStrategy {
  // No automatic relaxation of limits. An error status will be returned if the
  // system is infeasible.
  kNone = 0,
  // Relax velocity limits to restore feasibility. Acceleration and jerk limits
  // will be maintained and strictly met.
  kRelaxVelocityLimit,
};

// Inputs to a discrete constrained harmonic oscillator forward propagation
// step. The naming of variables is as close to the original paper [1],
// Section 3.3. "Discrete time representation" as possible.
struct ConstrainedHarmonicOscillatorInputs {
  // NOLINTBEGIN
  double T;  // (uniform) sampling time T in seconds.
  double M;  // Mass
  double B;  // Damping
  double K;  // Stiffness
  // NOLINTEND
  double v_max;  // Maximum velocity (velocity limit is symmetric).
  double a_max;  // Maximum acceleration (acceleration limit is symmetric).
  double j_max;  // Maximum jerk (jerk limit is symmetric).
  const JerkLimitHyperparameters limit_hyperparams;
  double f_k;  // Driving force acting on the oscillator. This field can also be
               // used to inject reference velocities as "virtual forces" via
               // B*v_ref.
  double q_k_minus_1;  // Position coordinate at previous time step.
  double q_k_minus_2;  // Position coordinate two time steps ago.
  double q_k_minus_3;  // Position coordinate three time steps ago.

  double p_d_k;          // Desired (reference) position at current time step.
  double p_d_k_minus_1;  // Desired (reference) position one time step ago.
  double p_d_k_minus_2;  // Desired (reference) position two time steps ago.

  FeasibilityRestorationStrategy feasibility_restoration_strategy =
      FeasibilityRestorationStrategy::kNone;
  int recursion_number = 0;
  int max_recursion_depth = 10;  // Max number of recursions for feasibility
                                 // restoration before erroring out.
};

// Outputs of a discrete constrained harmonic oscillator forward propagation.
struct ConstrainedHarmonicOscillatorOutput {
  double qk;   // New position coordinate.
  double a_k;  // Acceleration in this cycle.
  double j_k;  // Jerk in this cycle.
};

// Step the constrained harmonic oscillator based on inputs `in` and return
// `ConstrainedHarmonicOscillatorOutput`. Prints real-time logs with verbose
// outputs if `verbose_log_at_failure` is set to true.
icon::RealtimeStatusOr<ConstrainedHarmonicOscillatorOutput> Step(
    ConstrainedHarmonicOscillatorInputs in,
    bool verbose_log_at_failure = false);

// Compute the outputs of the constrained harmonic oscillator based on the
// previous position historyand the jerk at the current time step.
ConstrainedHarmonicOscillatorOutput ComputeOutputsFromJerk(double T,
                                                           double q_k_minus_1,
                                                           double q_k_minus_2,
                                                           double q_k_minus_3,
                                                           double input_jerk);

// Returns a history of states [p_k, p_k_minus_1, p_k_minus2] that realizes a
// differential state with position `p`, velocity `v` and acceleration `a` at
// time step `k`.
std::array<double, 3> ReconstructPositionHistory(double p, double v, double a,
                                                 double dt_sec);

}  // namespace intrinsic::icon

#endif  // INTRINSIC_ICON_CONTROL_ALGORITHMS_CONSTRAINED_HARMONIC_OSCILLATOR_H_
