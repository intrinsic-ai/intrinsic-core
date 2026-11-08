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

#include "intrinsic/icon/control/algorithms/constrained_harmonic_oscillator.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>  // NOLINT
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/functional/any_invocable.h"
#include "absl/log/log.h"
#include "absl/random/random.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/types/span.h"
#include "internal/testing.h"
#include "intrinsic/eigenmath/eigen_file_export.h"
#include "intrinsic/eigenmath/random_number_utils.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/testing/malloc_test.h"
#include "intrinsic/icon/utils/realtime_status_macro.h"
#include "intrinsic/icon/utils/realtime_status_matchers.h"
#include "intrinsic/math/gaussian_noise.h"
#include "intrinsic/math/ipow.h"

//
// Example how to run test and generate CSV files:
//
// ibazel test
// intrinsic/icon/control/algorithms:constrained_harmonic_oscillator_test
// --test_arg=--alsologtostderr --test_output=streamed --test_arg=--write_to_csv
// --log_root_dir=/tmp/
//
ABSL_FLAG(bool, write_to_csv, false,
          "Set to true to generate *.csv files for debugging.");
ABSL_FLAG(std::string, log_root_dir, "/tmp/",
          "Directory to write log files to.");

namespace intrinsic::icon {

namespace {

using ::absl_testing::IsOkAndHolds;
using ::intrinsic::icon::RealtimeStatusIs;
using ::testing::DoubleNear;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::Not;
using ::testing::Pointwise;

constexpr double kInfinity = std::numeric_limits<double>::infinity();
constexpr double kDefaultSamplingTimeSec = 0.005;
constexpr double kDefaultMaxVelocity = 1.0;
constexpr double kDefaultMaxAcceleration = 5.0;
constexpr double kDefaultMaxJerk = 100.0;
constexpr double kSmallTolerance = 1e-6;
constexpr double kMediumTolerance = 1e-3;

// Tolerance how much limit violation we allow for a test to pass.
const double kLimitViolationToleranceFactor = 1.001;

// Builds ConstraintHarmonicOscillatorInputs from test parameters. The previous
// commands are also initialized from `initial_position`.
ConstrainedHarmonicOscillatorInputs GetOscillatorInputsFromTestParameters(
    double initial_position, double initial_velocity = 0.0,
    double initial_acceleration = 0.0, double v_max = kDefaultMaxVelocity,
    double a_max = kDefaultMaxAcceleration, double j_max = kDefaultMaxJerk) {
  return {
      .T = kDefaultSamplingTimeSec,
      .M = 2.0,
      .B = 2.0 * std::sqrt(2.0 * 100.0),  // critical damping 2.0 * sqrt(M*K)
      .K = 100.0,
      .v_max = v_max,
      .a_max = a_max,
      .j_max = j_max,
      .f_k = 0.0,
      // Initialize the position history to reflect desired initial position,
      // velocity and acceleration.
      .q_k_minus_1 = initial_position,
      .q_k_minus_2 = ReconstructPositionHistory(
          initial_position, initial_velocity, initial_acceleration,
          kDefaultSamplingTimeSec)[1],
      .q_k_minus_3 = ReconstructPositionHistory(
          initial_position, initial_velocity, initial_acceleration,
          kDefaultSamplingTimeSec)[2],
      .p_d_k = initial_position,
      .p_d_k_minus_1 = initial_position,
      .p_d_k_minus_2 = initial_position,
      .feasibility_restoration_strategy = FeasibilityRestorationStrategy::kNone,
  };
}

TEST(ConstrainedHarmonicOscillatorTest, FailsForInvalidT) {
  auto params = GetOscillatorInputsFromTestParameters(/*initial_position=*/0.0);
  params.T = -1.0;
  EXPECT_THAT(Step(params), RealtimeStatusIs(absl::StatusCode::kInvalidArgument,
                                             HasSubstr("Sampling time")));
}

TEST(ConstrainedHarmonicOscillatorTest, FailsForInvalidM) {
  auto params = GetOscillatorInputsFromTestParameters(/*initial_position=*/0.0);
  params.M = -1.0;
  EXPECT_THAT(Step(params), RealtimeStatusIs(absl::StatusCode::kInvalidArgument,
                                             HasSubstr("Inertia")));
}

TEST(ConstrainedHarmonicOscillatorTest, FailsForInvalidB) {
  auto params = GetOscillatorInputsFromTestParameters(/*initial_position=*/0.0);
  params.B = -1.0;
  EXPECT_THAT(Step(params), RealtimeStatusIs(absl::StatusCode::kInvalidArgument,
                                             HasSubstr("Damping")));
}

TEST(ConstrainedHarmonicOscillatorTest, FailsForInvalidK) {
  auto params = GetOscillatorInputsFromTestParameters(/*initial_position=*/0.0);
  params.K = -1.0;
  EXPECT_THAT(Step(params), RealtimeStatusIs(absl::StatusCode::kInvalidArgument,
                                             HasSubstr("Stiffness")));
}

TEST(ConstrainedHarmonicOscillatorTest, FailsForInvalidv_max) {
  auto params = GetOscillatorInputsFromTestParameters(/*initial_position=*/0.0);
  params.v_max = -1.0;
  EXPECT_THAT(Step(params), RealtimeStatusIs(absl::StatusCode::kInvalidArgument,
                                             HasSubstr("v_max")));
}

TEST(ConstrainedHarmonicOscillatorTest, FailsForInvalida_max) {
  auto params = GetOscillatorInputsFromTestParameters(/*initial_position=*/0.0);
  params.a_max = -1.0;
  EXPECT_THAT(Step(params), RealtimeStatusIs(absl::StatusCode::kInvalidArgument,
                                             HasSubstr("a_max")));
}

TEST(ConstrainedHarmonicOscillatorTest, FailsForInvalidj_max) {
  auto params = GetOscillatorInputsFromTestParameters(/*initial_position=*/0.0);
  params.j_max = -1.0;
  EXPECT_THAT(Step(params), RealtimeStatusIs(absl::StatusCode::kInvalidArgument,
                                             HasSubstr("j_max")));
}

// Test that we accurately reconstruct scalar valued position histories from
// differential PVA states.
TEST(ReconstructPositionHistoryTest, Works) {
  const double dt_sec = kDefaultSamplingTimeSec;
  // A randomly chosen position history.
  const double p_k = 1.0;
  const double p_k_minus_1 = 1.01;
  const double p_k_minus_2 = 1.03;
  // Compute vel and acc from finite difference rules.
  const double v = (p_k - p_k_minus_1) / dt_sec;
  const double a = (p_k - 2.0 * p_k_minus_1 + p_k_minus_2) / (dt_sec * dt_sec);
  EXPECT_THAT(
      ReconstructPositionHistory(p_k, v, a, dt_sec),
      Pointwise(DoubleNear(kSmallTolerance), {p_k, p_k_minus_1, p_k_minus_2}));
}

// A simple sign function to be used in signal generators below.
template <typename T>
int sgn(T val) {
  return (T(0) < val) - (val < T(0));
}

// Hyperparameters defining a 1 Dof period signal with additive noise.
struct PeriodicSignalHyperparameters {
  double amplitude = 1.0;
  double omega_rad_per_sec = 2.0 * M_PI;
  double phase_shift_rad = 0.0;
  double additive_noise_stddev = 0.0;
  double y_offset = 0.0;
};

// Evaluates a discrete-time sinusoid signal with additive noise generated from
// `params` at index `step` with time-step duration `time_step_sec`.
double SinusoidTestSignal(int step, double time_step_sec,
                          const PeriodicSignalHyperparameters& params) {
  // Define random number generator with Gaussian distribution.
  absl::BitGen gen = GetRandomBitGenerator();
  intrinsic::GaussianGenerator dist(gen, /*mean=*/0.0,
                                    /*stddev=*/params.additive_noise_stddev);
  const double phi =
      step * time_step_sec * params.omega_rad_per_sec + params.phase_shift_rad;
  return params.y_offset + params.amplitude * std::sin(phi) + dist.Generate();
}

// Returns an alternating signal with the given amplitude and offset generated
// from `params` at index `step` with time-step duration `time_step_sec`.
double SquareWaveTestSignal(int step, double time_step_sec,
                            const PeriodicSignalHyperparameters& params) {
  absl::BitGen gen = GetRandomBitGenerator();
  intrinsic::GaussianGenerator dist(gen, /*mean=*/0.0,
                                    /*stddev=*/params.additive_noise_stddev);
  return params.y_offset +
         params.amplitude *
             sgn(std::sin(step * time_step_sec * params.omega_rad_per_sec +
                          params.phase_shift_rad)) +
         dist.Generate();
}

// A helper struct to insert time-varying input changes to the test controller
// rollout.
struct TimeVaryingControllerInput {
  std::string name;
  std::optional<double> p_ref;
  double v_ref = 0;
  double f_measured = 0;
  double f_ref = 0;
};

// Definitions of sinusoid test input signals that can be applied as additional
// inputs to the constrained harmonic oscillator.
TimeVaryingControllerInput GetSinusoidMeasuredForce(
    int step, double time_step_sec,
    const PeriodicSignalHyperparameters& params) {
  return {
      .name = "GetSinusoidMeasuredForce",
      .f_measured = SinusoidTestSignal(step, time_step_sec, params),
  };
}
TimeVaryingControllerInput GetSinusoidReferenceForce(
    int step, double time_step_sec,
    const PeriodicSignalHyperparameters& params) {
  return {
      .name = "GetSinusoidReferenceForce",
      .f_ref = SinusoidTestSignal(step, time_step_sec, params),
  };
}
TimeVaryingControllerInput GetSinusoidReferenceVelocity(
    int step, double time_step_sec,
    const PeriodicSignalHyperparameters& params) {
  return {
      .name = "GetSinusoidReferenceVelocity",
      .v_ref = SinusoidTestSignal(step, time_step_sec, params),
  };
}
TimeVaryingControllerInput GetSinusoidReferencePosition(
    int step, double time_step_sec,
    const PeriodicSignalHyperparameters& params) {
  return {
      .name = "GetSinusoidReferencePosition",
      .p_ref = SinusoidTestSignal(step, time_step_sec, params),
  };
}

// Definitions of Square Wave test input signals that can be applied as
// additional inputs to the constrained harmonic oscillator.
TimeVaryingControllerInput GetSquareWaveMeasuredForce(
    int step, double time_step_sec,
    const PeriodicSignalHyperparameters& params) {
  return {
      .name = "GetSquareWaveMeasuredForce",
      .f_measured = SquareWaveTestSignal(step, time_step_sec, params),
  };
}
TimeVaryingControllerInput GetSquareWaveReferenceForce(
    int step, double time_step_sec,
    const PeriodicSignalHyperparameters& params) {
  return {
      .name = "GetSinusoidReferenceForce",
      .f_ref = SquareWaveTestSignal(step, time_step_sec, params),
  };
}
TimeVaryingControllerInput GetSquareWaveReferenceVelocity(
    int step, double time_step_sec,
    const PeriodicSignalHyperparameters& params) {
  return {
      .name = "GetSquareWaveReferenceVelocity",
      .v_ref = SquareWaveTestSignal(step, time_step_sec, params),
  };
}
TimeVaryingControllerInput GetSquareWaveReferencePosition(
    int step, double time_step_sec,
    const PeriodicSignalHyperparameters& params) {
  return {
      .name = "GetSquareWaveReferencePosition",
      .p_ref = SquareWaveTestSignal(step, time_step_sec, params),
  };
}

// Returns an input for the constrained harmonic oscillator for `num_steps`
// using the given `input_generator` and `params`. Below we use std::function
// because we genuinely need copies of the callable object. See also
// go/totw/191#when-is-stdfunction-appropriate
using TimeVaryingControllerInputGenerator =
    std::function<TimeVaryingControllerInput(
        int idx, double time_step_sec,
        const PeriodicSignalHyperparameters& params)>;

absl::StatusOr<std::vector<double>> RollOutConstrainedOscillator(
    int num_steps, const ConstrainedHarmonicOscillatorInputs& initial_inputs,
    TimeVaryingControllerInputGenerator time_varying_input_generator_function,
    const PeriodicSignalHyperparameters& params,
    absl::Span<const double> vel_limits, absl::Span<const double> acc_limits,
    absl::Span<const double> jerk_limits) {
  if (vel_limits.size() != num_steps) {
    return absl::InvalidArgumentError(
        "Velocity limits size does not match number of steps.");
  }
  if (acc_limits.size() != num_steps) {
    return absl::InvalidArgumentError(
        "Acceleration limits size does not match number of steps.");
  }
  if (jerk_limits.size() != num_steps) {
    return absl::InvalidArgumentError(
        "Jerk limits size does not match number of steps.");
  }

  // Output log vectors.
  eigenmath::VectorXd log_output_positions;
  log_output_positions.resize(num_steps + 1);
  log_output_positions(0) = initial_inputs.q_k_minus_1;  // start position.
  eigenmath::VectorXd log_output_accelerations;
  log_output_accelerations.resize(num_steps + 1);
  log_output_accelerations(0) =
      (initial_inputs.q_k_minus_1 - 2.0 * initial_inputs.q_k_minus_2 +
       initial_inputs.q_k_minus_3) /
      ::intrinsic::IPow(initial_inputs.T, 2);
  eigenmath::VectorXd log_output_jerks;
  log_output_jerks.resize(num_steps + 1);
  log_output_jerks(0) = 0.0;
  eigenmath::VectorXd log_output_timestamps;
  log_output_timestamps.resize(num_steps + 1);
  log_output_timestamps(0) = 0.0;

  // Input log vectors.
  eigenmath::VectorXd log_input_generalized_force;
  log_input_generalized_force.resize(num_steps);
  eigenmath::VectorXd log_input_acceleration_limit;
  log_input_acceleration_limit.resize(num_steps);
  eigenmath::VectorXd log_input_jerk_limit;
  log_input_jerk_limit.resize(num_steps);
  eigenmath::VectorXd log_input_p_d_k;
  log_input_p_d_k.resize(num_steps);

  ConstrainedHarmonicOscillatorInputs iteration_inputs = initial_inputs;
  for (int i = 0; i < num_steps; ++i) {
    // Generate selected time-varying input signals and add to the controller
    // inputs.
    TimeVaryingControllerInput time_varying_input =
        time_varying_input_generator_function(i, iteration_inputs.T, params);
    // The input f_k needs to combine the measured force, the reference force
    // and the reference velocity term.
    iteration_inputs.f_k = time_varying_input.f_measured -
                           time_varying_input.f_ref +
                           iteration_inputs.B * time_varying_input.v_ref;
    iteration_inputs.p_d_k =
        time_varying_input.p_ref.value_or(initial_inputs.p_d_k);
    iteration_inputs.v_max = vel_limits[i];
    iteration_inputs.a_max = acc_limits[i];
    iteration_inputs.j_max = jerk_limits[i];

    IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
    INTRINSIC_RT_ASSIGN_OR_RETURN(ConstrainedHarmonicOscillatorOutput output,
                                  Step(iteration_inputs));
    IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();

    // Log inputs.
    log_input_generalized_force(i) = iteration_inputs.f_k;
    log_input_acceleration_limit(i) = iteration_inputs.a_max;
    log_input_jerk_limit(i) = iteration_inputs.j_max;
    log_input_p_d_k(i) = iteration_inputs.p_d_k;

    // Log outputs.
    log_output_positions(i + 1) = output.qk;
    log_output_accelerations(i + 1) = output.a_k;
    log_output_jerks(i + 1) = output.j_k;
    log_output_timestamps(i + 1) =
        static_cast<double>(i + 1) * iteration_inputs.T;

    // Transcribe internal state for the next iteration.
    iteration_inputs.q_k_minus_3 = iteration_inputs.q_k_minus_2;
    iteration_inputs.q_k_minus_2 = iteration_inputs.q_k_minus_1;
    iteration_inputs.q_k_minus_1 = output.qk;
    iteration_inputs.p_d_k_minus_2 = iteration_inputs.p_d_k_minus_1;
    iteration_inputs.p_d_k_minus_1 = iteration_inputs.p_d_k;
  }

  if (absl::GetFlag(FLAGS_write_to_csv)) {
    const auto test_info =
        ::testing::UnitTest::GetInstance()->current_test_info();
    if (test_info == nullptr) {
      return absl::InternalError("No test info available.");
    }
    // Create a directory for the test case logs.
    const std::string log_path =
        absl::StrCat(absl::GetFlag(FLAGS_log_root_dir),
                     test_info->test_case_name(), "/", test_info->name(), "/",
                     time_varying_input_generator_function(0, 0, params).name);
    LOG(INFO) << "Writing CSV to " << log_path;
    std::filesystem::create_directories(log_path);

    // Define helper to export a vector to a CSV file.
    auto VectorXdToFile = [log_path](std::string_view filename,
                                     eigenmath::VectorXd& data) {
      eigenmath::MatrixToFile(
          eigenmath::EigenFileExport::CSVFormat(Eigen::FullPrecision),
          absl::StrCat(log_path, "/", filename, ".csv"), data);
    };
    VectorXdToFile("output_positions", log_output_positions);
    VectorXdToFile("output_accelerations", log_output_accelerations);
    VectorXdToFile("output_jerks", log_output_jerks);
    VectorXdToFile("output_timestamps", log_output_timestamps);
    VectorXdToFile("input_generalized_force", log_input_generalized_force);
    VectorXdToFile("input_acceleration_limit", log_input_acceleration_limit);
    VectorXdToFile("input_jerk_limit", log_input_jerk_limit);
    VectorXdToFile("input_p_d_k", log_input_p_d_k);
  }

  return std::vector<double>(log_output_positions.begin(),
                             log_output_positions.end());
}

// Implements classical harmonic oscillator with reference state and symplectic
// integration, ignores constraints. Returns new position.
double StepUnconstrainedHarmonicOscillator(double T, double M, double D,
                                           double K, double F, double F_ref,
                                           double p_ref, double v_ref, double p,
                                           double v) {
  const double acc = 1. / M * (F - F_ref - K * (p - p_ref) - D * (v - v_ref));
  return p + T * (v + T * acc);
}

// Returns the position output of the unconstrained harmonic oscillator for
// `num_steps` using the given `input_generator` and `params`.
absl::StatusOr<std::vector<double>> RollOutUnconstrainedOscillator(
    int num_steps, const ConstrainedHarmonicOscillatorInputs& initial_inputs,
    absl::AnyInvocable<
        TimeVaryingControllerInput(int idx, double time_step_sec,
                                   const PeriodicSignalHyperparameters& params)>
        input_generator,
    const PeriodicSignalHyperparameters& params) {
  ConstrainedHarmonicOscillatorInputs iteration_inputs = initial_inputs;
  std::vector<double> output_positions;
  output_positions.reserve(num_steps + 1);
  output_positions.push_back(initial_inputs.q_k_minus_1);  // start position.
  for (int i = 0; i < num_steps; ++i) {
    // Generate selected time-varying input signals and add to the controller
    // inputs.
    TimeVaryingControllerInput time_varying_input =
        input_generator(i, iteration_inputs.T, params);
    const double qk = StepUnconstrainedHarmonicOscillator(
        iteration_inputs.T, iteration_inputs.M, iteration_inputs.B,
        iteration_inputs.K, time_varying_input.f_measured,
        time_varying_input.f_ref,
        time_varying_input.p_ref.value_or(initial_inputs.p_d_k),
        time_varying_input.v_ref, iteration_inputs.q_k_minus_1,
        /*v=*/(iteration_inputs.q_k_minus_1 - iteration_inputs.q_k_minus_2) /
            iteration_inputs.T);
    output_positions.push_back(qk);
    // Transcribe internal state.
    iteration_inputs.q_k_minus_3 = iteration_inputs.q_k_minus_2;
    iteration_inputs.q_k_minus_2 = iteration_inputs.q_k_minus_1;
    iteration_inputs.q_k_minus_1 = qk;
  }

  return output_positions;
}

// Test that we can rollout the constrained oscillator as expected with
// different input signal generators.
TEST(ConstrainedOscillatorRolloutTest, Works) {
  auto params = GetOscillatorInputsFromTestParameters(/*initial_position=*/1.0);
  params.v_max = 1.0;
  params.a_max = 5000.0;
  params.j_max = 500000;

  const int kNumSteps = 100;
  // Test rollout with all reference signals.
  for (const auto& input_generator :
       {GetSinusoidMeasuredForce, GetSinusoidReferenceForce,
        GetSinusoidReferenceVelocity, GetSinusoidReferencePosition,
        GetSquareWaveMeasuredForce, GetSquareWaveReferenceForce,
        GetSquareWaveReferenceVelocity, GetSquareWaveReferencePosition}) {
    ASSERT_OK_AND_ASSIGN(
        const std::vector<double> output_positions,
        RollOutConstrainedOscillator(
            kNumSteps, params, input_generator,
            PeriodicSignalHyperparameters{
                .amplitude = 100.0,
                .omega_rad_per_sec = 2.0 * M_PI,  // 1 Hz.
                .phase_shift_rad = 1.0,
            },
            /*vel_limits=*/std::vector<double>(kNumSteps, params.v_max),
            /*acc_limits=*/std::vector<double>(kNumSteps, params.a_max),
            /*jerk_limits=*/std::vector<double>(kNumSteps, params.j_max)));
    ASSERT_EQ(output_positions.size(), kNumSteps + 1);
    // We expect that the final position differs from the initial position.
    EXPECT_THAT(output_positions.back(),
                Not(DoubleNear(params.q_k_minus_1, kMediumTolerance)));
  }
}

// Test that we can rollout the unconstrained oscillator as expected with
// different input signal generators.
TEST(UnconstrainedOscillatorRolloutTest, Works) {
  auto params = GetOscillatorInputsFromTestParameters(/*initial_position=*/1.0);
  const int kNumSteps = 100;
  // Test rollout with all reference signals.
  for (const auto& input_generator :
       {GetSinusoidMeasuredForce, GetSinusoidReferenceForce,
        GetSinusoidReferenceVelocity, GetSinusoidReferencePosition,
        GetSquareWaveMeasuredForce, GetSquareWaveReferenceForce,
        GetSquareWaveReferenceVelocity, GetSquareWaveReferencePosition}) {
    ASSERT_OK_AND_ASSIGN(const std::vector<double> output_positions,
                         RollOutUnconstrainedOscillator(
                             kNumSteps, params, input_generator,
                             PeriodicSignalHyperparameters{
                                 .amplitude = 100.0,
                                 .omega_rad_per_sec = 2.0 * M_PI,  // 1 Hz.
                                 .phase_shift_rad = 1.0,
                             }));
    ASSERT_EQ(output_positions.size(), kNumSteps + 1);
    // We expect that the final position differs from the initial position.
    EXPECT_THAT(output_positions.back(),
                Not(DoubleNear(params.q_k_minus_1, kMediumTolerance)));
  }
}

// This test ensures that the dynamic responses wrt measured and reference
// forces as well as reference velocities are (almost) identical to the
// unconstrained harmonic oscillator.
TEST(ConstrainedHarmonicOscillatorTest, CompareAgainstUnconstrainedController) {
  ConstrainedHarmonicOscillatorInputs params =
      GetOscillatorInputsFromTestParameters(/*initial_position=*/1.0);
  params.T = 1e-3;
  params.a_max = std::numeric_limits<double>::infinity();
  params.j_max = std::numeric_limits<double>::infinity();
  params.v_max = std::numeric_limits<double>::infinity();

  const int kNumSteps = 1000;
  // Test rollout with all reference signals except for position references.
  // This is because the constrained harmonic oscillator handles position
  // references differently from the classical controller formulation.
  for (const auto& input_generator :
       {GetSinusoidMeasuredForce, GetSinusoidReferenceForce,
        GetSinusoidReferenceVelocity, GetSquareWaveMeasuredForce,
        GetSquareWaveReferenceForce, GetSquareWaveReferenceVelocity}) {
    ASSERT_OK_AND_ASSIGN(const std::vector<double> rollout_unconstrained,
                         RollOutUnconstrainedOscillator(
                             kNumSteps, params, input_generator,
                             PeriodicSignalHyperparameters{
                                 .amplitude = 3.0,
                                 .omega_rad_per_sec = 2.0 * M_PI,  // 1 Hz.
                                 .phase_shift_rad = 1.0,
                             }));

    ASSERT_OK_AND_ASSIGN(
        const std::vector<double> rollout_constrained,
        RollOutConstrainedOscillator(
            kNumSteps, params, input_generator,
            PeriodicSignalHyperparameters{
                .amplitude = 3.0,
                .omega_rad_per_sec = 2.0 * M_PI,  // 1 Hz.
                .phase_shift_rad = 1.0,
            },
            /*vel_limits=*/std::vector<double>(kNumSteps, params.v_max),
            /*acc_limits=*/std::vector<double>(kNumSteps, params.a_max),
            /*jerk_limits=*/std::vector<double>(kNumSteps, params.j_max)));

    // Compare the rollouts
    ASSERT_EQ(rollout_unconstrained.size(), rollout_constrained.size());
    EXPECT_THAT(rollout_constrained,
                Pointwise(DoubleNear(5e-3), rollout_unconstrained))
        << " Unconstrained and constrained trajectories don't match.";
  }
}

// Check for velocity, acceleration and jerk limit violations on a time-series
// of positions. Employs a simple backward differentiation scheme to compute
// derivatives, similar to the derivative implementation within the harmonic
// oscillator.
absl::StatusOr<bool> IsWithinLimits(
    absl::Span<const double> positions, double v_start, double a_start,
    absl::Span<const double> v_max, absl::Span<const double> a_max,
    absl::Span<const double> j_max, double dt_sec,
    double limit_violation_tolerance_factor = kLimitViolationToleranceFactor,
    bool print_log = false) {
  if (positions.size() != v_max.size() + 1) {
    return absl::InvalidArgumentError(
        "Positions size incompatible with velocity limits size.");
  }
  if (positions.size() != a_max.size() + 1) {
    return absl::InvalidArgumentError(
        "Positions size incompatible with acceleration limits size.");
  }
  if (positions.size() != j_max.size() + 1) {
    return absl::InvalidArgumentError(
        "Positions size incompatible with jerk limits size.");
  }
  double v = v_start;
  double a = a_start;
  for (int i = 1; i < positions.size(); ++i) {
    double v_prev = v;
    double a_prev = a;
    v = (positions[i] - positions[i - 1]) / dt_sec;
    a = (v - v_prev) / dt_sec;
    const double j = (a - a_prev) / dt_sec;

    if (std::abs(v) > limit_violation_tolerance_factor * v_max[i - 1]) {
      if (print_log)
        LOG(INFO) << "Velocity limit violated at index " << i - 1
                  << " with value " << v << " and limit " << v_max[i - 1];
      return false;
    }
    if (std::abs(a) > limit_violation_tolerance_factor * a_max[i - 1]) {
      if (print_log)
        LOG(INFO) << "Acceleration limit violated at index " << i - 1
                  << " with value " << a << " and limit " << a_max[i - 1];
      return false;
    }
    if (std::abs(j) > limit_violation_tolerance_factor * j_max[i - 1]) {
      if (print_log)
        LOG(INFO) << "Jerk limit violated at index " << i - 1 << " with value "
                  << j << " and limit " << j_max[i - 1];
      return false;
    }
  }
  return true;
}

struct ConstrainedHarmonicOscillatorSticksToLimitsTestCase {
  std::string test_name;
  std::vector<TimeVaryingControllerInputGenerator> input_generator_selection = {
      GetSinusoidMeasuredForce};
  PeriodicSignalHyperparameters input_generator_params;
  double initial_position = 0.0;
  double initial_velocity = 0.0;
  double initial_acceleration = 0.0;
  double v_max = kDefaultMaxVelocity;
  double a_max = kDefaultMaxAcceleration;
  double j_max = kDefaultMaxJerk;
};

using ConstrainedHarmonicOscillatorSticksToLimitsTest =
    ::testing::TestWithParam<
        ConstrainedHarmonicOscillatorSticksToLimitsTestCase>;

// A test case in which the limits are held constant over the entire time
// horizon.
TEST_P(ConstrainedHarmonicOscillatorSticksToLimitsTest, ConstantLimitsTest) {
  ConstrainedHarmonicOscillatorInputs initial_inputs =
      GetOscillatorInputsFromTestParameters(
          GetParam().initial_position, GetParam().initial_velocity,
          GetParam().initial_acceleration, GetParam().v_max, GetParam().a_max,
          GetParam().j_max);

  // To ensure feasibility, we choose the kRelaxVelocityLimit strategy.
  // Consequently, we only check against kInfinity for velocity limit
  // satisfaction below.
  initial_inputs.feasibility_restoration_strategy =
      FeasibilityRestorationStrategy::kRelaxVelocityLimit;
  const int kNumSteps = 500;

  for (const auto& input_generator : GetParam().input_generator_selection) {
    // Rollout the unconstrained oscillator and check its trajectory against
    // test case limits. Input (driving) parameters are only useful if
    // unconstrained oscillator would violate limits.
    ASSERT_OK_AND_ASSIGN(const std::vector<double> rollout_unconstrained,
                         RollOutUnconstrainedOscillator(
                             kNumSteps, initial_inputs, input_generator,
                             GetParam().input_generator_params));

    EXPECT_THAT(
        IsWithinLimits(rollout_unconstrained, GetParam().initial_velocity,
                       GetParam().initial_acceleration,
                       std::vector<double>(kNumSteps, GetParam().v_max),
                       std::vector<double>(kNumSteps, GetParam().a_max),
                       std::vector<double>(kNumSteps, GetParam().j_max),
                       initial_inputs.T),
        IsOkAndHolds(Eq(false)))
        << "Test case invalid: Unconstrained oscillator is within limits. The "
           "unconstrained oscillator should violate either acceleration or "
           "jerk limit for this test to be meaningful.";

    ASSERT_OK_AND_ASSIGN(
        const std::vector<double> rollout_constrained,
        RollOutConstrainedOscillator(
            kNumSteps, initial_inputs, input_generator,
            GetParam().input_generator_params,
            /*vel_limits=*/std::vector<double>(kNumSteps, initial_inputs.v_max),
            /*acc_limits=*/std::vector<double>(kNumSteps, initial_inputs.a_max),
            /*jerk_limits=*/
            std::vector<double>(kNumSteps, initial_inputs.j_max)));
    // Constrained oscillator should be within limits.
    EXPECT_THAT(IsWithinLimits(rollout_constrained, GetParam().initial_velocity,
                               GetParam().initial_acceleration,
                               std::vector<double>(kNumSteps, kInfinity),
                               std::vector<double>(kNumSteps, GetParam().a_max),
                               std::vector<double>(kNumSteps, GetParam().j_max),
                               initial_inputs.T, kLimitViolationToleranceFactor,
                               /*print_log=*/true),
                IsOkAndHolds(Eq(true)))
        << "Constrained oscillator rollout is NOT within limits.";
  }
}

// Test case in which we randomly vary jerk limits over time. For every control
// tick, the commanded jerk must remain within the varying limits.
TEST_P(ConstrainedHarmonicOscillatorSticksToLimitsTest,
       RandomlyVaryingJerkLimitsTest) {
  ConstrainedHarmonicOscillatorInputs initial_inputs =
      GetOscillatorInputsFromTestParameters(
          GetParam().initial_position, GetParam().initial_velocity,
          GetParam().initial_acceleration, GetParam().v_max, GetParam().a_max,
          GetParam().j_max);
  // To ensure feasibility, we choose the kRelaxVelocityLimit strategy.
  // Consequently, we only check against kInfinity for velocity limit
  // satisfaction below.
  initial_inputs.feasibility_restoration_strategy =
      FeasibilityRestorationStrategy::kRelaxVelocityLimit;
  const int kNumSteps = 1000;

  // Randomly vary the jerk limit uniformly between a small number
  // and the maximum limit for this test case.
  std::vector<double> random_jerk_limits;
  absl::BitGen bitgen;
  const double kLowerLimitMultiplier = 0.001;
  const double kUpperLimitMultiplier = 1.0;
  const int kNumLimitChangesPerRollout = 20;
  double random_multiplier = 1.0;
  for (int i = 0; i < kNumSteps; ++i) {
    if (i % (kNumSteps / kNumLimitChangesPerRollout) == 0) {
      random_multiplier =
          absl::Uniform(bitgen, kLowerLimitMultiplier, kUpperLimitMultiplier);
    }
    random_jerk_limits.push_back(random_multiplier * initial_inputs.j_max);
  }

  for (const auto& input_generator : GetParam().input_generator_selection) {
    ASSERT_OK_AND_ASSIGN(
        const std::vector<double> rollout_constrained,
        RollOutConstrainedOscillator(
            kNumSteps, initial_inputs, input_generator,
            GetParam().input_generator_params,
            std::vector<double>(kNumSteps, initial_inputs.v_max),
            std::vector<double>(kNumSteps, initial_inputs.a_max),
            random_jerk_limits));

    double v = GetParam().initial_velocity;
    double a = GetParam().initial_acceleration;
    absl::BitGen bitgen;
    for (int i = 1; i < kNumSteps; ++i) {
      // Compute VAJ state and check against limits.
      double v_prev = v;
      double a_prev = a;
      v = (rollout_constrained[i] - rollout_constrained[i - 1]) /
          initial_inputs.T;
      a = (v - v_prev) / initial_inputs.T;
      const double j = (a - a_prev) / initial_inputs.T;
      EXPECT_LE(std::abs(v), kInfinity);
      EXPECT_LE(std::abs(a),
                kLimitViolationToleranceFactor * initial_inputs.a_max)
          << "Acceleration limit violated at index " << i << " with value "
          << a;
      EXPECT_LE(std::abs(j),
                kLimitViolationToleranceFactor * initial_inputs.j_max)
          << "Jerk limit violated at index " << i << " with value " << j;
    }
  }
}

// Test case in which we randomly vary acceleration limits over time. For every
// control tick, the commanded acceleration must remain within the envelope,
// either:
// (1) given by the varying acceleration limits, if the previous
// acceleration command was already within the (new) acceleration limit,
// (2) given by the previous acceleration, moving towards the new acceleration
// limit, if the previous acceleration command was not within the (new)
// acceleration limit.
TEST_P(ConstrainedHarmonicOscillatorSticksToLimitsTest,
       RandomlyVaryingAccelerationLimitsTest) {
  ConstrainedHarmonicOscillatorInputs initial_inputs =
      GetOscillatorInputsFromTestParameters(
          GetParam().initial_position, GetParam().initial_velocity,
          GetParam().initial_acceleration, GetParam().v_max, GetParam().a_max,
          GetParam().j_max);
  // To ensure feasibility, we relax velocity limits. Consequently, we check
  // against `kInfinity` for velocity limits below.
  initial_inputs.feasibility_restoration_strategy =
      FeasibilityRestorationStrategy::kRelaxVelocityLimit;
  const int kNumSteps = 1000;

  // Randomly vary the acceleration limit uniformly between a small number
  // and the maximum limit for this test case.
  std::vector<double> random_acceleration_limits;
  absl::BitGen bitgen;
  const double kLowerLimitMultiplier = 0.001;
  const double kUpperLimitMultiplier = 1.0;
  const int kNumLimitChangesPerRollout = 20;
  double random_multiplier = 1.0;
  for (int i = 0; i < kNumSteps; ++i) {
    if (i % (kNumSteps / kNumLimitChangesPerRollout) == 0) {
      random_multiplier =
          absl::Uniform(bitgen, kLowerLimitMultiplier, kUpperLimitMultiplier);
    }
    random_acceleration_limits.push_back(random_multiplier *
                                         initial_inputs.a_max);
  }

  for (const auto& input_generator : GetParam().input_generator_selection) {
    ASSERT_OK_AND_ASSIGN(
        const std::vector<double> rollout_constrained,
        RollOutConstrainedOscillator(
            kNumSteps, initial_inputs, input_generator,
            GetParam().input_generator_params,
            /*vel_limits=*/std::vector<double>(kNumSteps, initial_inputs.v_max),
            random_acceleration_limits,
            /*jerk_limits=*/
            std::vector<double>(kNumSteps, initial_inputs.j_max)));

    double v = GetParam().initial_velocity;
    double a = GetParam().initial_acceleration;
    for (int i = 1; i < kNumSteps; ++i) {
      // Compute VAJ state and check against limits.
      const double v_prev = v;
      const double a_prev = a;
      v = (rollout_constrained[i] - rollout_constrained[i - 1]) /
          initial_inputs.T;
      a = (v - v_prev) / initial_inputs.T;
      const double j = (a - a_prev) / initial_inputs.T;
      EXPECT_LE(std::abs(v), kInfinity);
      if (std::abs(a_prev) <= random_acceleration_limits[i - 1]) {
        // Index acceleration command is within the index acceleration limit,
        // too.
        EXPECT_LE(std::abs(a), kLimitViolationToleranceFactor *
                                   random_acceleration_limits[i - 1])
            << "Acceleration command is not within the acceleration "
               "limit at index "
            << i - 1 << " with acceleration value " << a
            << " and previous acceleration value " << a_prev
            << " and acceleration limit " << random_acceleration_limits[i - 1];
      } else {
        // Acceleration should be moving towards the new acceleration limit, in
        // particular its absolute value is smaller than the previous
        // acceleration command.
        EXPECT_LE(std::abs(a),
                  kLimitViolationToleranceFactor * std::abs(a_prev))
            << "Acceleration was outside soft acceleration constraint and does "
               "not seem to be moving towards limit. Acceleration limit was  "
            << random_acceleration_limits[i - 1] << " at index " << i - 1
            << " with actual accelerationvalue " << a
            << " and previous accelerationvalue " << a_prev;
      }
      EXPECT_LE(std::abs(j),
                kLimitViolationToleranceFactor * initial_inputs.j_max)
          << "Jerk limit violated at index " << i << " with value " << j;
    }
  }
}

// Test case in which we push the initial acceleration outside acceleration
// limits. The system will be controlled back to within acceleration limits.
TEST_P(ConstrainedHarmonicOscillatorSticksToLimitsTest,
       ConvergesBackToWithinAccelerationLimits) {
  // Box the initial acceleration to allow for meaningful tests even if a limit
  // is infinity.
  const double kInitialAcceleration = std::min(2.0 * GetParam().a_max, 100.0);
  // Set a minimum jerk to allow for meaningful test even if a jerk limit is
  // configured very small.
  const double kMeaningfulJerkLimit =
      std::max(GetParam().j_max, kDefaultMaxJerk);
  ConstrainedHarmonicOscillatorInputs initial_inputs =
      GetOscillatorInputsFromTestParameters(
          GetParam().initial_position, GetParam().initial_velocity,
          // Set initial acceleration beyond limits.
          kInitialAcceleration, GetParam().v_max, GetParam().a_max,
          kMeaningfulJerkLimit);
  // To ensure feasibility, we relax velocity limits. Consequently, we check
  // against kInfinity for velocity limits below.
  initial_inputs.feasibility_restoration_strategy =
      FeasibilityRestorationStrategy::kRelaxVelocityLimit;
  const int kNumSteps = 1000;

  for (const auto& input_generator : GetParam().input_generator_selection) {
    ASSERT_OK_AND_ASSIGN(
        const std::vector<double> rollout_constrained,
        RollOutConstrainedOscillator(
            kNumSteps, initial_inputs, input_generator,
            GetParam().input_generator_params,
            /*vel_limits=*/std::vector<double>(kNumSteps, initial_inputs.v_max),
            /*acc_limits=*/std::vector<double>(kNumSteps, initial_inputs.a_max),
            /*jerk_limits=*/
            std::vector<double>(kNumSteps, initial_inputs.j_max)));

    double v = GetParam().initial_velocity;
    double a = kInitialAcceleration;
    absl::BitGen bitgen;
    for (int i = 1; i < kNumSteps; ++i) {
      // Compute VAJ state and check against limits.
      const double v_prev = v;
      const double a_prev = a;
      v = (rollout_constrained[i] - rollout_constrained[i - 1]) /
          initial_inputs.T;
      a = (v - v_prev) / initial_inputs.T;
      const double j = (a - a_prev) / initial_inputs.T;
      EXPECT_LE(std::abs(v), kInfinity);
      // Acceleration should be moving towards the new acceleration limit, in
      // particular its absolute value is smaller than the previous
      // acceleration command.
      if (std::abs(a) > initial_inputs.a_max * kLimitViolationToleranceFactor) {
        EXPECT_LE(std::abs(a),
                  kLimitViolationToleranceFactor * std::abs(a_prev))
            << "Acceleration was outside soft acceleration constraint and does "
               "not seem to be moving towards limit. Acc limit was  "
            << initial_inputs.a_max << " at index " << i - 1
            << " with actual acceleration value " << a
            << " and previous acceleration value " << a_prev;
      }

      EXPECT_LE(std::abs(j),
                kLimitViolationToleranceFactor * initial_inputs.j_max)
          << "Jerk limit violated at index " << i << " with value " << j;
    }
  }
}

INSTANTIATE_TEST_SUITE_P(
    ConstrainedHarmonicOscillatorSticksToLimitsTests,
    ConstrainedHarmonicOscillatorSticksToLimitsTest,
    ::testing::Values(
        ConstrainedHarmonicOscillatorSticksToLimitsTestCase{
            .test_name = "SimpleParameterSet",
            .input_generator_selection =
                {GetSinusoidMeasuredForce, GetSinusoidReferenceForce,
                 GetSinusoidReferenceVelocity, GetSquareWaveMeasuredForce,
                 GetSquareWaveReferenceForce, GetSquareWaveReferenceVelocity,
                 GetSquareWaveReferencePosition, GetSinusoidReferencePosition},
            .input_generator_params =
                PeriodicSignalHyperparameters{
                    .amplitude = 100.0,
                    .omega_rad_per_sec = 4.0 * M_PI,  // 2 Hz.
                },
            .initial_position = 1.0,
            .initial_velocity = 0.0,
            .initial_acceleration = 0.0,
            .v_max = 1.0,
            .a_max = 5.0,
            .j_max = 20.0,
        },
        ConstrainedHarmonicOscillatorSticksToLimitsTestCase{
            .test_name = "SimpleParameterSetNonSteadyInitStateWithinLimits",
            .input_generator_selection =
                {GetSinusoidMeasuredForce, GetSinusoidReferenceForce,
                 GetSinusoidReferenceVelocity, GetSquareWaveMeasuredForce,
                 GetSquareWaveReferenceForce, GetSquareWaveReferenceVelocity,
                 GetSquareWaveReferencePosition, GetSinusoidReferencePosition},
            .input_generator_params =
                PeriodicSignalHyperparameters{
                    .amplitude = 100.0,
                    .omega_rad_per_sec = 4.0 * M_PI,  // 2 Hz.
                },
            .initial_position = 1.0,
            .initial_velocity = 0.9,
            .initial_acceleration = -4.0,
            .v_max = 1.0,
            .a_max = 5.0,
            .j_max = 20.0,
        },
        ConstrainedHarmonicOscillatorSticksToLimitsTestCase{
            .test_name = "WithVeryLowJerkLimit",
            .input_generator_selection =
                {GetSinusoidMeasuredForce, GetSinusoidReferenceForce,
                 GetSinusoidReferenceVelocity, GetSquareWaveMeasuredForce,
                 GetSquareWaveReferenceForce, GetSquareWaveReferenceVelocity,
                 GetSquareWaveReferencePosition, GetSinusoidReferencePosition},
            .input_generator_params =
                PeriodicSignalHyperparameters{
                    .amplitude = 100.0,
                    .omega_rad_per_sec = 4.0 * M_PI,  // 2 Hz.
                },
            .initial_position = 1.0,
            .v_max = 10.0,
            .a_max = 10.0,
            .j_max = 1.0,
        },
        ConstrainedHarmonicOscillatorSticksToLimitsTestCase{
            .test_name = "WithVeryLowJerkLimitNonSteadyInitStateWithinLimits",
            .input_generator_selection =
                {GetSinusoidMeasuredForce, GetSinusoidReferenceForce,
                 GetSinusoidReferenceVelocity, GetSquareWaveMeasuredForce,
                 GetSquareWaveReferenceForce, GetSquareWaveReferenceVelocity,
                 GetSquareWaveReferencePosition, GetSinusoidReferencePosition},
            .input_generator_params =
                PeriodicSignalHyperparameters{
                    .amplitude = 100.0,
                    .omega_rad_per_sec = 4.0 * M_PI,  // 2 Hz.
                },
            .initial_position = 1.0,
            .initial_velocity = 1.0,
            .initial_acceleration = -5.0,
            .v_max = 10.0,
            .a_max = 10.0,
            .j_max = 1.0,
        },
        ConstrainedHarmonicOscillatorSticksToLimitsTestCase{
            .test_name = "WithVeryLowAccelerationLimit",
            .input_generator_selection =
                {GetSinusoidMeasuredForce, GetSinusoidReferenceForce,
                 GetSinusoidReferenceVelocity, GetSquareWaveMeasuredForce,
                 GetSquareWaveReferenceForce, GetSquareWaveReferenceVelocity,
                 GetSquareWaveReferencePosition, GetSinusoidReferencePosition},
            .input_generator_params =
                PeriodicSignalHyperparameters{
                    .amplitude = 100.0,
                    .omega_rad_per_sec = 4.0 * M_PI,  // 2 Hz.
                },
            .initial_position = 1.0,
            .v_max = 10.0,
            .a_max = 1.0,
            .j_max = 100.0,
        },
        ConstrainedHarmonicOscillatorSticksToLimitsTestCase{
            .test_name =
                "WithVeryLowAccelerationLimitNonSteadyInitStateWithinLimits",
            .input_generator_selection =
                {GetSinusoidMeasuredForce, GetSinusoidReferenceForce,
                 GetSinusoidReferenceVelocity, GetSquareWaveMeasuredForce,
                 GetSquareWaveReferenceForce, GetSquareWaveReferenceVelocity,
                 GetSquareWaveReferencePosition, GetSinusoidReferencePosition},
            .input_generator_params =
                PeriodicSignalHyperparameters{
                    .amplitude = 100.0,
                    .omega_rad_per_sec = 4.0 * M_PI,  // 2 Hz.
                },
            .initial_position = 1.0,
            .initial_velocity = 5.0,
            .initial_acceleration = 0.2,
            .v_max = 10.0,
            .a_max = 1.0,
            .j_max = 100.0,
        },
        ConstrainedHarmonicOscillatorSticksToLimitsTestCase{
            .test_name = "WithAllLimitsVeryLow",
            .input_generator_selection =
                {GetSinusoidMeasuredForce, GetSinusoidReferenceForce,
                 GetSinusoidReferenceVelocity, GetSquareWaveMeasuredForce,
                 GetSquareWaveReferenceForce, GetSquareWaveReferenceVelocity,
                 GetSquareWaveReferencePosition, GetSinusoidReferencePosition},
            .input_generator_params =
                PeriodicSignalHyperparameters{
                    .amplitude = 100.0,
                    .omega_rad_per_sec = 4.0 * M_PI,  // 2 Hz.
                },
            .initial_position = 1.0,
            .v_max = .1,
            .a_max = .1,
            .j_max = 1.0,
        },
        ConstrainedHarmonicOscillatorSticksToLimitsTestCase{
            .test_name = "WithAllLimitsVeryLowInitialVelocityOutsideLimits",
            .input_generator_selection =
                {GetSinusoidMeasuredForce, GetSinusoidReferenceForce,
                 GetSinusoidReferenceVelocity, GetSquareWaveMeasuredForce,
                 GetSquareWaveReferenceForce, GetSquareWaveReferenceVelocity,
                 GetSquareWaveReferencePosition, GetSinusoidReferencePosition},
            .input_generator_params =
                PeriodicSignalHyperparameters{
                    .amplitude = 100.0,
                    .omega_rad_per_sec = 4.0 * M_PI,  // 2 Hz.
                },
            .initial_position = 1.0,
            .initial_velocity = 1.0,
            .initial_acceleration = 0.1,
            .v_max = .1,
            .a_max = .1,
            .j_max = 1.0,
        },
        ConstrainedHarmonicOscillatorSticksToLimitsTestCase{
            .test_name = "WithInfiniteVelocityLimit",
            .input_generator_selection =
                {GetSinusoidMeasuredForce, GetSinusoidReferenceForce,
                 GetSinusoidReferenceVelocity, GetSquareWaveMeasuredForce,
                 GetSquareWaveReferenceForce, GetSquareWaveReferenceVelocity,
                 GetSquareWaveReferencePosition, GetSinusoidReferencePosition},
            .input_generator_params =
                PeriodicSignalHyperparameters{
                    .amplitude = 100.0,
                    .omega_rad_per_sec = 4.0 * M_PI,  // 2 Hz.
                },
            .initial_position = 1.0,
            .v_max = kInfinity,
            .a_max = 5,
            .j_max = 20.0,
        },
        ConstrainedHarmonicOscillatorSticksToLimitsTestCase{
            .test_name = "WithInfiniteAccelerationLimit",
            .input_generator_selection =
                {GetSinusoidMeasuredForce, GetSinusoidReferenceForce,
                 GetSinusoidReferenceVelocity, GetSquareWaveMeasuredForce,
                 GetSquareWaveReferenceForce, GetSquareWaveReferenceVelocity,
                 GetSquareWaveReferencePosition, GetSinusoidReferencePosition},
            .input_generator_params =
                PeriodicSignalHyperparameters{
                    .amplitude = 100.0,
                    .omega_rad_per_sec = 4.0 * M_PI,  // 2 Hz.
                },
            .initial_position = 1.0,
            .v_max = 1.0,
            .a_max = kInfinity,
            .j_max = 20.0,
        },
        ConstrainedHarmonicOscillatorSticksToLimitsTestCase{
            .test_name = "WithInfiniteJerkLimit",
            .input_generator_selection =
                {GetSinusoidMeasuredForce, GetSinusoidReferenceForce,
                 GetSinusoidReferenceVelocity, GetSquareWaveMeasuredForce,
                 GetSquareWaveReferenceForce, GetSquareWaveReferenceVelocity,
                 GetSquareWaveReferencePosition, GetSinusoidReferencePosition},
            .input_generator_params =
                PeriodicSignalHyperparameters{
                    .amplitude = 100.0,
                    .omega_rad_per_sec = 4.0 * M_PI,  // 2 Hz.
                },
            .initial_position = 1.0,
            .v_max = 1.0,
            .a_max = 2.0,
            .j_max = kInfinity,
        },
        ConstrainedHarmonicOscillatorSticksToLimitsTestCase{
            .test_name = "WithInfiniteJerkAndAccelerationLimit",
            .input_generator_selection =
                {GetSinusoidMeasuredForce, GetSinusoidReferenceForce,
                 GetSinusoidReferenceVelocity, GetSquareWaveMeasuredForce,
                 GetSquareWaveReferenceForce, GetSquareWaveReferenceVelocity,
                 GetSquareWaveReferencePosition, GetSinusoidReferencePosition},
            .input_generator_params =
                PeriodicSignalHyperparameters{
                    .amplitude = 100.0,
                    .omega_rad_per_sec = 4.0 * M_PI,  // 2 Hz.
                },
            .initial_position = 1.0,
            .v_max = 1.0,
            .a_max = kInfinity,
            .j_max = kInfinity,
        }),
    [](const ::testing::TestParamInfo<
        ConstrainedHarmonicOscillatorSticksToLimitsTest::ParamType>& info) {
      return info.param.test_name;
    });

TEST(ConstrainedOscillatorRolloutTest, WorksWithZeroStiffness) {
  auto params = GetOscillatorInputsFromTestParameters(/*initial_position=*/1.0);
  params.v_max = 1.0;
  params.a_max = 5.0;
  params.j_max = 50.0;
  params.K = 0.0;  // Zero stiffness.
  params.M = 2.0;
  params.B = 2.0 * std::sqrt(params.M);  // critical damping w/ fake stiffness.

  const int kNumSteps = 260;
  for (const auto& input_generator :
       {GetSinusoidMeasuredForce, GetSinusoidReferenceForce,
        GetSinusoidReferenceVelocity, GetSinusoidReferencePosition,
        GetSquareWaveMeasuredForce, GetSquareWaveReferenceForce,
        GetSquareWaveReferenceVelocity, GetSquareWaveReferencePosition}) {
    ASSERT_OK_AND_ASSIGN(const std::vector<double> output_positions,
                         RollOutConstrainedOscillator(
                             kNumSteps, params, input_generator,
                             PeriodicSignalHyperparameters{
                                 .amplitude = 100.0,
                                 .omega_rad_per_sec = 2.0 * M_PI,  // 1 Hz.
                             },
                             std::vector<double>(kNumSteps, params.v_max),
                             std::vector<double>(kNumSteps, params.a_max),
                             std::vector<double>(kNumSteps, params.j_max)));
    ASSERT_EQ(output_positions.size(), kNumSteps + 1);
    // We expect that the final position differs from the initial position.
    EXPECT_THAT(output_positions.back(),
                Not(DoubleNear(params.q_k_minus_1, kMediumTolerance)));

    EXPECT_THAT(IsWithinLimits(output_positions, /*v_start=*/0.0,
                               /*a_start=*/0.0, /*v_max=*/
                               std::vector<double>(kNumSteps, kInfinity),
                               std::vector<double>(kNumSteps, params.a_max),
                               std::vector<double>(kNumSteps, params.j_max),
                               params.T, kLimitViolationToleranceFactor,
                               /*print_log=*/true),
                IsOkAndHolds(Eq(true)))
        << "Constrained oscillator rollout is NOT within limits.";
  }
}

// TODO(b/376006879): this is a repro case for b/376006879. Fix algorithm and
// enable.
TEST(ConstrainedOscillatorTest,
     DISABLED_ReachesConstantDisplacementWithExternalForce) {
  auto params = GetOscillatorInputsFromTestParameters(
      /*initial_position=*/0.0,
      /*initial_velocity=*/0.0, /*initial_acceleration=*/0.0, /*v_max=*/1.0,
      /*a_max=*/2.8, /*j_max=*/28.0);
  params.f_k = 50.0;
  params.feasibility_restoration_strategy =
      FeasibilityRestorationStrategy::kRelaxVelocityLimit;
  params.M = 1.0;
  params.K = 1000.0;
  params.B = 2.0 * std::sqrt(params.M * params.K);  // critical damping.
  params.T = 0.005;

  const int kNumSteps = 2000;
  ASSERT_OK_AND_ASSIGN(const std::vector<double> output_positions,
                       RollOutConstrainedOscillator(
                           kNumSteps, params, GetSinusoidReferenceForce,
                           PeriodicSignalHyperparameters{
                               .amplitude = 0.0,  // Constant force.
                               .y_offset = 50.0,
                           },
                           std::vector<double>(kNumSteps, params.v_max),
                           std::vector<double>(kNumSteps, params.a_max),
                           std::vector<double>(kNumSteps, params.j_max)));
  ASSERT_EQ(output_positions.size(), kNumSteps + 1);

  // We expect the system to reach a constant displacement.
  const double expected_displacement = params.f_k / params.K;
  EXPECT_NEAR(output_positions.back(), expected_displacement, 1e-3);

  EXPECT_THAT(
      IsWithinLimits(output_positions, /*v_start=*/0.0,
                     /*a_start=*/0.0,
                     /*v_max=*/std::vector<double>(kNumSteps, kInfinity),
                     std::vector<double>(kNumSteps, params.a_max),
                     std::vector<double>(kNumSteps, params.j_max), params.T,
                     kLimitViolationToleranceFactor,
                     /*print_log=*/true),
      IsOkAndHolds(Eq(true)))
      << "Constrained oscillator rollout is NOT within limits.";
}

}  // namespace

}  // namespace intrinsic::icon
