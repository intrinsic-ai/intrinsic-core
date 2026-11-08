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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "internal/testing.h"
#include "intrinsic/icon/utils/realtime_status_matchers.h"

constexpr double kTimeStep = 0.1;
constexpr int kNSteps = 100;

using ::absl_testing::StatusIs;
using ::testing::HasSubstr;

namespace intrinsic::icon {
namespace {

// A known reference chirp signal.
double EvaluateReferenceChirp(double amplitude, double starting_f,
                              double final_f, double t) {
  return amplitude * (1 - t / (kTimeStep * kNSteps)) *
         std::sin(
             2 * M_PI *
             (starting_f * t +
              (((final_f - starting_f) / (kTimeStep * kNSteps)) * t * t) / 2));
}

TEST(ChirpSignalTest, CompareAgainstKnownValues) {
  double amplitude = 1.0;
  double starting_f = 1.0;
  double final_f = 3.0;

  ChirpSignal chirp(amplitude, /*time_horizon=*/kTimeStep * kNSteps, starting_f,
                    final_f);

  // Compare against reference solution.
  for (size_t i = 0; i < kNSteps; i++) {
    double t = i * kTimeStep;
    EXPECT_NEAR(chirp.Evaluate(t).position,
                EvaluateReferenceChirp(amplitude, starting_f, final_f, t),
                1e-6);
  }
}

TEST(MultiJointChirpSignalTest, FactoryFailsForEmptyAmplitudes) {
  double starting_f = 1.0;
  double final_f = 3.0;
  EXPECT_THAT(
      MultiJointChirpSignal::Create({}, /*time_horizon=*/kTimeStep * kNSteps,
                                    starting_f, final_f),
      StatusIs(absl::StatusCode::kFailedPrecondition,
               HasSubstr("must not be empty")));
}

TEST(MultiJointChirpSignalTest, CompareAgainstKnownValues) {
  double starting_f = 1.0;
  double final_f = 3.0;
  const std::vector<double> amplitudes{1.0, 2.0, 3.0};
  ASSERT_OK_AND_ASSIGN(auto multi_dof_chirp,
                       MultiJointChirpSignal::Create(
                           amplitudes, /*time_horizon=*/kTimeStep * kNSteps,
                           starting_f, final_f));

  // Compare against reference solution.
  for (size_t i = 0; i < kNSteps; i++) {
    double t = i * kTimeStep;
    INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(auto joint_values,
                                      multi_dof_chirp.Evaluate(t));

    // Loop through 3 DoF and assert equality to reference solution.
    for (size_t dof = 0; dof < 3; dof++) {
      EXPECT_NEAR(joint_values.position(0),
                  EvaluateReferenceChirp(amplitudes[0], starting_f, final_f, t),
                  1e-6);
    }
  }
}

}  // namespace
}  // namespace intrinsic::icon
