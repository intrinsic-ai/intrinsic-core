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

// Custom RT-log macro definition to allow for easy switching between RT-log
// types during debugging sessions.
#define RT_LOG_TYPE(SEVERITY) INTRINSIC_RT_LOG_THROTTLED(SEVERITY)

#include "intrinsic/icon/control/algorithms/constrained_harmonic_oscillator.h"

#include <algorithm>
#include <array>
#include <cstdlib>

#include "intrinsic/icon/utils/fixed_str_cat.h"
#include "intrinsic/icon/utils/log.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/icon/utils/realtime_status_macro.h"
#include "intrinsic/icon/utils/realtime_status_or.h"
#include "intrinsic/math/ipow.h"

namespace intrinsic::icon {

namespace {

constexpr double kSmallNumericThreshold = 1e-9;

// A manually tuned minimum P value. Experimentally found that instability
// occurs if P is chosen lower than this.
const double kPmin = 1e-5;

// This multiplier is used to slightly relax VAJ limits in recursive calls for
// infeasibility mitigation. This makes inequality comparisons robust to tiny
// numerical deviations and is hardly visible in the returned output.
constexpr double kLimitsRelaxationMultiplier = 1.0 + kSmallNumericThreshold;

// Implements projection as in Equation (1) in reference [1], which is a trivial
// clamping operation.
icon::RealtimeStatusOr<double> Project(double lower, double upper, double x) {
  if (lower > upper) {
    return icon::InternalError(
        RealtimeStatus::StrCat("Lower > upper, got ", lower, " > ", upper));
  }
  return std::clamp(x, lower, upper);
}

}  // namespace

ConstrainedHarmonicOscillatorOutput ComputeOutputsFromJerk(double T,
                                                           double q_k_minus_1,
                                                           double q_k_minus_2,
                                                           double q_k_minus_3,
                                                           double input_jerk) {
  const double T_cubed = ::intrinsic::IPow(T, 3);
  const double T_squared = ::intrinsic::IPow(T, 2);
  const double qk =
      3. * q_k_minus_1 - 3. * q_k_minus_2 + q_k_minus_3 + T_cubed * input_jerk;
  const double ak = (qk - 2. * q_k_minus_1 + q_k_minus_2) / T_squared;
  return {
      .qk = qk,
      .a_k = ak,
      .j_k = input_jerk,
  };
}

icon::RealtimeStatusOr<ConstrainedHarmonicOscillatorOutput> Step(
    ConstrainedHarmonicOscillatorInputs in, bool verbose_log_at_failure) {
  in.recursion_number++;
  // Check recursion depth.
  if (in.recursion_number > in.max_recursion_depth) {
    return InternalError(FixedStrCat<RealtimeStatus::kMaxMessageLength>(
        "Recursion depth exceeded max recursion depth of ",
        in.max_recursion_depth, "."));
  }

  // Some basic validity checking of input parameters.
  if (in.T <= 0.0) {
    return InvalidArgumentError(FixedStrCat<RealtimeStatus::kMaxMessageLength>(
        "Sampling time must be positive, but got ", in.T, "."));
  }
  if (in.M <= 0.0) {
    return InvalidArgumentError(FixedStrCat<RealtimeStatus::kMaxMessageLength>(
        "Inertia must be positive, but got ", in.M, "."));
  }
  if (in.B < 0.0) {
    return InvalidArgumentError(FixedStrCat<RealtimeStatus::kMaxMessageLength>(
        "Damping must be non-negative, but got ", in.B, "."));
  }
  if (in.K < -kSmallNumericThreshold) {
    return InvalidArgumentError(FixedStrCat<RealtimeStatus::kMaxMessageLength>(
        "Stiffness must be non-negative, but got ", in.K, "."));
  }
  // We tolerate small numeric deviations from perfectly zero stiffness and
  // rectify them to zero.
  in.K = std::max(0.0, in.K);
  if (in.v_max < 0.0) {
    return InvalidArgumentError(FixedStrCat<RealtimeStatus::kMaxMessageLength>(
        "v_max must be non-negative, but got ", in.v_max, "."));
  }
  if (in.a_max < 0.0) {
    return InvalidArgumentError(FixedStrCat<RealtimeStatus::kMaxMessageLength>(
        "a_max must be non-negative, but got ", in.a_max, "."));
  }
  if (in.j_max < 0.0) {
    return InvalidArgumentError(FixedStrCat<RealtimeStatus::kMaxMessageLength>(
        "j_max must be non-negative, but got ", in.j_max, "."));
  }
  if (in.limit_hyperparams.jerk_y_scaling <= 0.0 ||
      in.limit_hyperparams.jerk_y_scaling > 1.0) {
    return InvalidArgumentError(FixedStrCat<RealtimeStatus::kMaxMessageLength>(
        "jerk_y_scaling must be > 0.0 and <= 1.0, but got ",
        in.limit_hyperparams.jerk_y_scaling, "."));
  }
  if (in.limit_hyperparams.jerk_s_scaling < 1.0) {
    return InvalidArgumentError(FixedStrCat<RealtimeStatus::kMaxMessageLength>(
        "jerk_s_scaling must be >= 1.0, but got ",
        in.limit_hyperparams.jerk_s_scaling, "."));
  }

  // We need to avoid extremely high acceleration and jerk limits to avoid a
  // degeneration of the limits polytope (c.f. Figure 2 in [1]) and a bad
  // conditioning of the parameters K and P. To this end, we introduce
  // hard-coded finite maximum values for acceleration limits.
  const double kInternalMaximumAccelerationLimit = 1000.0;

  // Introduce convenience abbreviations to maximize overlap with the notation
  // in Section 3.3 from [1].
  const double V = in.v_max;
  const double A = std::min(in.a_max, kInternalMaximumAccelerationLimit);
  // We rely on automatic derivation of some of the most important
  // hyperparameters, since manual tuning would be unintuitive and not scalable.
  // The following choice of parameters guarantees stability / feasibility under
  // some assumptions. See Equation (28) and the following section in [1].
  // TODO(giftthaler): Need to investigate if dynamically deriving the minimum
  // bound for J is okay in recursive calls.
  const double J_max = A / (kPmin + in.T);
  const double J = std::min(in.j_max, J_max);
  const double P = (A / J) - in.T;  // See Eq (28) in [1].
  const double H = 1. / P;          // See Eq (28) in [1].
  if (P < kPmin - kSmallNumericThreshold) {
    RT_LOG_TYPE(ERROR) << "P < kPmin, got P = " << P
                       << ". Likely limits are ill-conditioned.";
    // Due to our definition of J_max, this point should never be reached.
    return InternalError(FixedStrCat<RealtimeStatus::kMaxMessageLength>(
        "P < kPmin, got ", P, ". Likely limits are ill-conditioned."));
  }

  const double T_squared = ::intrinsic::IPow(in.T, 2);
  const double T_cubed = ::intrinsic::IPow(in.T, 3);

  // Equation (24a) and (24b) in [1].
  // Important note: different from the classical harmonic oscillator, position
  // references do not enter the control law through feedback on a position
  // error term. Instead, position references are directly used as feedforwards
  // on the position signal, but they are rate-limited to adhere to limits.
  // Equation (24a) in [1].
  const double q_star_k =
      in.p_d_k +
      ((2.0 * in.M + in.B * in.T) * (in.q_k_minus_1 - in.p_d_k_minus_1)) /
          (in.M + in.B * in.T + in.K * T_squared) -
      (in.M * (in.q_k_minus_2 - in.p_d_k_minus_2) - T_squared * (in.f_k)) /
          (in.M + in.B * in.T + in.K * T_squared);

  // Equation (24b) in [1].
  const double j_star_k = (q_star_k - 3.0 * in.q_k_minus_1 +
                           3.0 * in.q_k_minus_2 - in.q_k_minus_3) /
                          T_cubed;

  // Equation (24c) in [1].
  const double a_k_minus_1 =
      (in.q_k_minus_1 - 2. * in.q_k_minus_2 + in.q_k_minus_3) / T_squared;

  // Equation (24d) in [1].
  const double v_k_minus_1 = (in.q_k_minus_1 - in.q_k_minus_2) / in.T;

  // Equation (24e) in [1].
  const double Vbar_b_k =
      (-V - v_k_minus_1 - (in.T + P) * a_k_minus_1) / (T_squared + P * in.T);

  // Equation (24f) in [1].
  const double Vbar_t_k =
      (V - v_k_minus_1 - (in.T + P) * a_k_minus_1) / (T_squared + P * in.T);

  // Equation (24g) in [1].
  const double Abar_b_k = (-A - a_k_minus_1) / in.T;

  // Equation (24h) in [1].
  const double Abar_t_k = (A - a_k_minus_1) / in.T;

  // Equation (24i) in [1].
  const double J_y = in.limit_hyperparams.jerk_y_scaling * J;
  const double J_s = in.limit_hyperparams.jerk_s_scaling * J;
  const double arg = (-H * a_k_minus_1 - J_s) / (1.0 + H * in.T);
  INTRINSIC_RT_ASSIGN_OR_RETURN(const double Jbar_b_k, Project(-J, -J_y, arg));

  // Equation (24j) in [1].
  const double brg = (-H * a_k_minus_1 + J_s) / (1. + H * in.T);
  INTRINSIC_RT_ASSIGN_OR_RETURN(const double Jbar_t_k, Project(J_y, J, brg));

  // Equation (24k) in [1].
  const double Lk_lower = std::max(std::max(Vbar_b_k, Abar_b_k), Jbar_b_k);
  const double Lk_upper = std::min(std::min(Vbar_t_k, Abar_t_k), Jbar_t_k);

  // CHECK FEASIBILITY CONDITIONS AND HANDLE LIMIT BOUNDARIES.
  //
  // Check conditions of Equation (26) in [1], which should be satisfied if the
  // previous state's (v,a,j) lies within the sets of limits defined by Equation
  // (8) in [1]. If the conditions are violated we apply a restoration strategy,
  // which might involve a recursive call to Step(). Since only trivial
  // computations are involved, recursive calls don't threaten real-time
  // compatibility.

  if (Abar_t_k < 0.0 || Abar_b_k > 0.0) {
    // If this case is reached the acceleration from the previous cycle
    // `a_k_minus_1`is not within the acceleration limits from this cycle, i.e.
    // the previous acceleration is higher than the new maximum acceleration.
    // The recovery strategy is to walk towards and on the new acceleration
    // limit as long as the acceleration computed by the nominal control law
    // would be violating the acceleration limits.
    //
    // Important notes:
    // (1) we prioritize sticking to acceleration-limits over maintaining
    // compliance and reaction to external forces.
    // (2) we limit the change rate of the acceleration limit to the jerk limit.

    RT_LOG_TYPE(INFO) << "Abar_t_k < 0 or Abar_b_k > 0. Prev acc was "
                      << a_k_minus_1 << " but acc limit is " << A;

    // Compute a position update candidate with relaxed acceleration
    // constraints.
    in.a_max = kLimitsRelaxationMultiplier * std::abs(a_k_minus_1);
    INTRINSIC_RT_ASSIGN_OR_RETURN(
        const ConstrainedHarmonicOscillatorOutput out_candidate,
        Step(in, verbose_log_at_failure));

    // Compute the maximally shrinked acceleration limit `A_min_rate_limited`
    // for which the change in A still respects jerk constraints J. Return the
    // candidate if it is within the new acceleration limit.
    const double A_min_rate_limited = std::abs(a_k_minus_1) - J * in.T;
    const double A_boundary_walk = std::max(A, A_min_rate_limited);
    if (std::abs(out_candidate.a_k) <= A_boundary_walk) {
      return out_candidate;
    }

    // Acceleration boundary walk in case acceleration limits are contracting
    // and normal control would violate the new limits.
    RT_LOG_TYPE(INFO) << "Acc limits are contracting. Entering Acc Boundary "
                         "Walk. Rate limited A is "
                      << A_min_rate_limited;
    double acc_sgn = 1.0;
    if (Abar_b_k > 0.0) acc_sgn = -1.0;  // acceleration is negative.
    const double qk = acc_sgn * A_boundary_walk * T_squared +
                      2.0 * in.q_k_minus_1 - in.q_k_minus_2;
    const double ak = acc_sgn * A_boundary_walk;
    const double jk =
        (qk - 3.0 * in.q_k_minus_1 + 3.0 * in.q_k_minus_2 - in.q_k_minus_3) /
        T_cubed;
    return ConstrainedHarmonicOscillatorOutput{
        .qk = qk,
        .a_k = ak,
        .j_k = jk,
    };
  }

  // Helper function to print out logs for unhandled error cases.
  auto PrintVariablesRtLog = [in, P, H, J, J_y, J_s, V, A, q_star_k, j_star_k,
                              a_k_minus_1, v_k_minus_1, Lk_lower, Lk_upper,
                              Vbar_b_k, Vbar_t_k, Abar_b_k, Abar_t_k, Jbar_b_k,
                              Jbar_t_k]() {
    INTRINSIC_RT_LOG(ERROR) << "T: " << in.T;
    INTRINSIC_RT_LOG(ERROR) << "M: " << in.M;
    INTRINSIC_RT_LOG(ERROR) << "B: " << in.B;
    INTRINSIC_RT_LOG(ERROR) << "K: " << in.K;
    INTRINSIC_RT_LOG(ERROR) << "P: " << P;
    INTRINSIC_RT_LOG(ERROR) << "H: " << H;
    INTRINSIC_RT_LOG(ERROR) << "J: " << J;
    INTRINSIC_RT_LOG(ERROR) << "J_y: " << J_y;
    INTRINSIC_RT_LOG(ERROR) << "J_s: " << J_s;
    INTRINSIC_RT_LOG(ERROR) << "V: " << V;
    INTRINSIC_RT_LOG(ERROR) << "A: " << A;
    INTRINSIC_RT_LOG(ERROR) << "f_k: " << in.f_k;
    INTRINSIC_RT_LOG(ERROR) << "q_k_minus_1: " << in.q_k_minus_1;
    INTRINSIC_RT_LOG(ERROR) << "q_k_minus_2: " << in.q_k_minus_2;
    INTRINSIC_RT_LOG(ERROR) << "q_k_minus_3: " << in.q_k_minus_3;
    INTRINSIC_RT_LOG(ERROR) << "p_d_k: " << in.p_d_k;
    INTRINSIC_RT_LOG(ERROR) << "p_d_k_minus_1: " << in.p_d_k_minus_1;
    INTRINSIC_RT_LOG(ERROR) << "p_d_k_minus_2: " << in.p_d_k_minus_2;
    INTRINSIC_RT_LOG(ERROR) << "q_star_k: " << q_star_k;
    INTRINSIC_RT_LOG(ERROR) << "j_star_k: " << j_star_k;
    INTRINSIC_RT_LOG(ERROR) << "a_k_minus_1: " << a_k_minus_1;
    INTRINSIC_RT_LOG(ERROR) << "v_k_minus_1: " << v_k_minus_1;
    INTRINSIC_RT_LOG(ERROR) << "Vbar_b_k: " << Vbar_b_k;
    INTRINSIC_RT_LOG(ERROR) << "Vbar_t_k: " << Vbar_t_k;
    INTRINSIC_RT_LOG(ERROR) << "Abar_b_k: " << Abar_b_k;
    INTRINSIC_RT_LOG(ERROR) << "Abar_t_k: " << Abar_t_k;
    INTRINSIC_RT_LOG(ERROR) << "Jbar_b_k: " << Jbar_b_k;
    INTRINSIC_RT_LOG(ERROR) << "Jbar_t_k: " << Jbar_t_k;
    INTRINSIC_RT_LOG(ERROR) << "Lk_lower: " << Lk_lower;
    INTRINSIC_RT_LOG(ERROR) << "Lk_upper: " << Lk_upper;
  };

  // Equation (26) in [1] states a few error cases that we have not yet
  // encountered in practice, and for which no restoration strategy has been
  // designed. We check them for completeness and return an error status. If one
  // of these error cases shows up, reach out to giftthaler@.
  if (Jbar_b_k > kSmallNumericThreshold) {
    PrintVariablesRtLog();
    return icon::InternalError("Jbar_b_k > 0. Unhandled error case.");
  }
  if (Jbar_t_k < -kSmallNumericThreshold) {
    PrintVariablesRtLog();
    return icon::InternalError("Jbar_t_k < 0. Unhandled error case.");
  }
  if (Vbar_b_k >= Vbar_t_k + kSmallNumericThreshold) {
    PrintVariablesRtLog();
    return icon::InternalError("Vbar_b_k >= Vbar_t_k. Unhandled error case.");
  }

  // Start checking for error cases for which a restoration strategy exists.
  // First, check for infeasibility related to velocity and jerk limits.
  if (Vbar_b_k > Jbar_t_k) {
    switch (in.feasibility_restoration_strategy) {
      case FeasibilityRestorationStrategy::kNone: {
        PrintVariablesRtLog();
        return InternalError(FixedStrCat<RealtimeStatus::kMaxMessageLength>(
            "Cond 27/2 violated, expected Vbar_b_k <= Jbar_t_k, got ", Vbar_b_k,
            " <= ", Jbar_t_k));
      }
      case FeasibilityRestorationStrategy::kRelaxVelocityLimit: {
        // We select P and H according to Equation (28) in [1]. An important
        // insight is that P and H are not a function of V. Therefore P, H
        // remain unchanged if we rescale V - or alternatively we can solve for
        // a V that is guaranteed to satisfy Equation (27/2) in [1], and use it
        // in a recursive call to Step().
        in.v_max = kLimitsRelaxationMultiplier *
                   (-v_k_minus_1 - (in.T + P) * a_k_minus_1 -
                    Jbar_t_k * (T_squared + P * in.T));
        RT_LOG_TYPE(INFO)
            << "Trying to recover from condition 27/2. Original V was " << V
            << ". Recovered V is " << in.v_max;
        return Step(in, verbose_log_at_failure);
      }
      default: {
        return icon::InternalError("Unknown recovery strategy defined.");
      }
    }
  }

  if (Jbar_b_k > Vbar_t_k) {
    switch (in.feasibility_restoration_strategy) {
      case FeasibilityRestorationStrategy::kNone: {
        PrintVariablesRtLog();
        return InternalError(FixedStrCat<RealtimeStatus::kMaxMessageLength>(
            "Cond 27/4 violated, expected Jbar_b_k <= Vbar_t_k, got ", Jbar_b_k,
            " <= ", Vbar_t_k, ". Likely due to a too low velocity limit."));
      }
      case FeasibilityRestorationStrategy::kRelaxVelocityLimit: {
        // We select P and H according to Equation (28) in [1]. An important
        // insight is that P and H are not a function of V. Therefore P, H
        // remain unchanged if we rescale V - or alternatively we can solve for
        // a V that is guaranteed to satisfy Equation (27/4) in [1], and use it
        // in a recursive call to Step().
        in.v_max = kLimitsRelaxationMultiplier *
                   (v_k_minus_1 + (in.T + P) * a_k_minus_1 +
                    Jbar_b_k * (T_squared + P * in.T));
        RT_LOG_TYPE(INFO)
            << "Trying to recover from condition 27/4. Original V was " << V
            << ". Recovered V is " << in.v_max;
        return Step(in, verbose_log_at_failure);
      }
      default: {
        return icon::InternalError("Unknown recovery strategy defined.");
      }
    }
  }

  // Second, check for infeasibility related to velocity limits.
  if (Vbar_b_k > Abar_t_k) {
    switch (in.feasibility_restoration_strategy) {
      case FeasibilityRestorationStrategy::kNone: {
        PrintVariablesRtLog();
        return InternalError(FixedStrCat<RealtimeStatus::kMaxMessageLength>(
            "Cond 27/1 violated, expected Vbar_b_k <= Abar_t_k, evals to ",
            Vbar_b_k, " <= ", Abar_t_k));
      }
      case FeasibilityRestorationStrategy::kRelaxVelocityLimit: {
        // We select P and H according to Equation (28) in [1]. An important
        // insight is that P and H are not a function of V. Therefore P, H
        // remain unchanged if we rescale V - or alternatively we can solve for
        // a V that is guaranteed to satisfy Equation (27/1) in [1], and use it
        // in a recursive call to Step(). Here, we set Vbar_b_k equal to
        // Abar_t_k and solve for relaxed V.
        in.v_max = kLimitsRelaxationMultiplier *
                   (-v_k_minus_1 - (in.T + P) * a_k_minus_1 -
                    (T_squared + P * in.T) * Abar_t_k);
        RT_LOG_TYPE(INFO)
            << "Trying to recover from condition 27/1. Original V was " << V
            << ". Recovered V is " << in.v_max;
        return Step(in, verbose_log_at_failure);
      }
      default: {
        return icon::InternalError("Unknown recovery strategy defined.");
      }
    }
  }
  if (Abar_b_k > Vbar_t_k) {
    switch (in.feasibility_restoration_strategy) {
      case FeasibilityRestorationStrategy::kNone: {
        PrintVariablesRtLog();
        return InternalError(FixedStrCat<RealtimeStatus::kMaxMessageLength>(
            "Cond 27/3 violated, expected Abar_b_k <= Vbar_t_k, got ", Abar_b_k,
            " <= ", Vbar_t_k, ". Likely due to a too low velocity limit."));
      }
      case FeasibilityRestorationStrategy::kRelaxVelocityLimit: {
        // We select P and H according to Equation (28) in [1]. An important
        // insight is that P and H are not a function of V. Therefore P, H
        // remain unchanged if we rescale V - or alternatively we can solve for
        // a V that is guaranteed to satisfy Equation (27/3) in [1], and use it
        // in a recursive call to Step(). Here, we set Vbar_t_k equal to
        // Abar_b_k and solve for relaxed V.
        in.v_max = kLimitsRelaxationMultiplier *
                   (Abar_b_k * (T_squared + P * in.T) + v_k_minus_1 +
                    (in.T + P) * a_k_minus_1);
        RT_LOG_TYPE(INFO)
            << "Trying to recover from condition 27/3. Original V was " << V
            << ". Recovered V is " << in.v_max;
        return Step(in, verbose_log_at_failure);
      }
      default: {
        return icon::InternalError("Unknown recovery strategy defined.");
      }
    }
  }

  // One more unhandled error case that has never shown up in practice, contact
  // giftthaler@ if encountered.
  if (Lk_lower > Lk_upper) {
    PrintVariablesRtLog();
    return InternalError(FixedStrCat<RealtimeStatus::kMaxMessageLength>(
        "Expected Lk_lower <= Lk_upper, but got ", Lk_lower, " <= ", Lk_upper,
        ". Unhandled error case."));
  }

  // At this point, we know that the solution is feasible and we can compute the
  // final jerk increment. See Equation (24l) in [1].
  INTRINSIC_RT_ASSIGN_OR_RETURN(const double j_k,
                                Project(Lk_lower, Lk_upper, j_star_k));

  return ComputeOutputsFromJerk(in.T, in.q_k_minus_1, in.q_k_minus_2,
                                in.q_k_minus_3, j_k);
}

std::array<double, 3> ReconstructPositionHistory(double p, double v, double a,
                                                 double dt_sec) {
  std::array<double, 3> p_k_history;
  p_k_history[0] = p;
  // From v = (p_k - p_k_minus_1) / dt_sec
  p_k_history[1] = p - v * dt_sec;
  // From a = q_k - 2. * in.q_k_minus_1 + in.q_k_minus_2) / (dt_sec)²;
  p_k_history[2] = (a * dt_sec * dt_sec) - p + 2. * p_k_history[1];
  return p_k_history;
}

}  // namespace intrinsic::icon
