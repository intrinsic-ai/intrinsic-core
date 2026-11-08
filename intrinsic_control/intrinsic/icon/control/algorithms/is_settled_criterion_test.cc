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

#include "intrinsic/icon/control/algorithms/is_settled_criterion.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <memory>
#include <vector>

#include "absl/random/random.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "internal/testing.h"
#include "intrinsic/eigenmath/random_number_utils.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/testing/malloc_test.h"
#include "intrinsic/icon/utils/realtime_status_macro.h"
#include "intrinsic/icon/utils/realtime_status_matchers.h"
#include "intrinsic/kinematics/types/state_rn.h"
#include "intrinsic/math/gaussian_noise.h"
#include "intrinsic/math/spline/polynomial_spline.h"

namespace intrinsic::icon {
namespace {

using ::absl_testing::StatusIs;
using ::testing::AllOf;
using ::testing::HasSubstr;

static const size_t kNDofs = 6;
static constexpr double kDtSeconds = 1.0e-3;

class IsSettledCriterionTest : public ::testing::Test {
 public:
  void SetUp() override {
    // Zero initial state on position, velocity and acceleration.
    start_state_.position = eigenmath::VectorNd(kNDofs).setZero();
    start_state_.velocity = eigenmath::VectorNd(kNDofs).setZero();
    start_state_.acceleration = eigenmath::VectorNd(kNDofs).setZero();

    // Zero final state on position, velocity and acceleration.
    final_state_.position = eigenmath::VectorNd(kNDofs).setOnes();
    final_state_.velocity = eigenmath::VectorNd(kNDofs).setZero();
    final_state_.acceleration = eigenmath::VectorNd(kNDofs).setZero();

    // Initialization of noise vector.
    noise_vector_ = eigenmath::VectorNd::Zero(kNDofs);

    // Trajectory duration time.
    trajectory_time_seconds_ = 2.0;
  }

  // Replay the trajectory from the `current_state`, with `gaussian_noise`, and
  // until the `time_to_simulate` is reached. Simultaneously update the
  // `is_settled_criterion`.
  void RollOutTrajectoryWithNoise(
      const double time_to_simulate, StateRnPVA& current_state,
      GaussianGenerator& gaussian_noise,
      std::unique_ptr<IsSettledCriterion>& is_settled_criterion) {
    double time_to_go_seconds = trajectory_time_seconds_;
    while (time_to_go_seconds >
           trajectory_time_seconds_ - time_to_simulate + kDtSeconds) {
      INTRINSIC_RT_ASSIGN_OR_DIE(
          StateRnPVA next_state,
          GetNextStateJointQuinticSpline(current_state, final_state_,
                                         time_to_go_seconds, kDtSeconds));
      time_to_go_seconds -= kDtSeconds;
      current_state = next_state;

      INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
          bool is_settled,
          is_settled_criterion->Update(
              /*measured_joint_velocities=*/current_state.velocity +
                  gaussian_noise.Generate(kNDofs),
              /*commanded_joint_velocities=*/current_state.velocity,
              /*has_trajectory_ended=*/false));
      EXPECT_FALSE(is_settled);
    }
  }

  // Update the `is_settled_criterion` for `timesteps` using the `current_state`
  // with `gaussian_noise`. Return the `settling_index_output`. Returns an
  // InternalError if the robot has not settled.
  absl::StatusOr<int> LoopForTimestepsToReachSettledState(
      size_t timesteps, bool has_trajectory_ended, StateRnPVA& current_state,
      GaussianGenerator& gaussian_noise,
      std::unique_ptr<IsSettledCriterion>& is_settled_criterion) {
    for (int i = 0; i < timesteps; ++i) {
      auto noise_sample = gaussian_noise.Generate(kNDofs);
      IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
      INTRINSIC_RT_ASSIGN_OR_RETURN(
          bool is_settled,
          is_settled_criterion->Update(
              /*measured_joint_velocities=*/current_state.velocity +
                  noise_sample,
              /*commanded_joint_velocities=*/current_state.velocity,
              /*has_trajectory_ended=*/has_trajectory_ended));
      IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
      if (is_settled) {
        return i;
      }
    }
    return absl::InternalError("The system has not settled.");
  }

  // Stays in the `is_settled_criterion` for `timesteps` using the
  // `current_state` with `gaussian_noise`.
  void LoopForTimestepsInSettledState(
      size_t timesteps, StateRnPVA& current_state,
      GaussianGenerator& gaussian_noise,
      std::unique_ptr<IsSettledCriterion>& is_settled_criterion) {
    for (size_t i = 0; i < timesteps; ++i) {
      auto noise_sample = gaussian_noise.Generate(kNDofs);
      IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
      INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
          bool is_settled,
          is_settled_criterion->Update(
              /*measured_joint_velocities=*/current_state.velocity +
                  noise_sample,
              /*commanded_joint_velocities=*/current_state.velocity,
              /*has_trajectory_ended=*/true));
      IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
      EXPECT_TRUE(is_settled);
    }
  }

  // Update the `is_settled_criterion` for `timesteps` using the `current_state`
  // with `gaussian_noise`. Returns the `unsettling_index_output`, which
  // indicates the index at which the system passed from a settled to an
  // unsettled state (for instance if the robot starts moving again). In case,
  // the robot never unsettles, we return an internal error.
  absl::StatusOr<int> LoopForTimestepsToReachUnsettledState(
      size_t timesteps, StateRnPVA& current_state,
      GaussianGenerator& gaussian_noise,
      IsSettledCriterion& is_settled_criterion, double extra_robot_velocity) {
    for (size_t i = 0; i < timesteps; ++i) {
      auto new_robot_velocity = current_state.velocity;
      new_robot_velocity.array() += extra_robot_velocity;
      auto noise_sample = gaussian_noise.Generate(kNDofs);
      IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
      INTRINSIC_RT_ASSIGN_OR_RETURN(
          bool is_settled,
          is_settled_criterion.Update(
              /*measured_joint_velocities=*/new_robot_velocity + noise_sample,
              /*commanded_joint_velocities=*/current_state.velocity,
              /*has_trajectory_ended=*/true));
      IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
      if (!is_settled) {
        return i;
      }
    }
    return absl::InternalError("The system has not unsettled.");
  }

 protected:
  StateRnPVA start_state_;
  StateRnPVA final_state_;
  eigenmath::VectorNd noise_vector_;
  double trajectory_time_seconds_;
};

TEST(IsSettledCriterionFactoryTest, IncorrectInitializationOfFrequency) {
  EXPECT_THAT(IsSettledCriterion::Create(
                  /*frequency_hz=*/0.0),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("The frequency should be larger than zero.")));

  EXPECT_THAT(IsSettledCriterion::Create(
                  /*frequency_hz=*/-1.0),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("The frequency should be larger than zero.")));
}

TEST_F(IsSettledCriterionTest, CorrectInitializationOfCriterion) {
  ASSERT_OK_AND_ASSIGN(
      auto is_settled_criterion,
      IsSettledCriterion::Create(/*frequency_hz=*/1.0 / kDtSeconds));

  EXPECT_OK(is_settled_criterion->Initialize());
}

TEST_F(IsSettledCriterionTest, IncorrectSetMaxUncertaintyThreshold) {
  ASSERT_OK_AND_ASSIGN(
      auto is_settled_criterion,
      IsSettledCriterion::Create(/*frequency_hz=*/1.0 / kDtSeconds));

  EXPECT_THAT(
      is_settled_criterion->SetMaxUncertaintyThreshold(
          /*max_uncertainty_threshold=*/0.0),
      StatusIs(absl::StatusCode::kOutOfRange,
               HasSubstr("The max uncertainty threshold should be within")));

  EXPECT_THAT(
      is_settled_criterion->SetMaxUncertaintyThreshold(
          /*max_uncertainty_threshold=*/1.00),
      StatusIs(absl::StatusCode::kOutOfRange,
               HasSubstr("The max uncertainty threshold should be within")));
}

TEST_F(IsSettledCriterionTest, CorrectGetAndSetMaxUncertaintyThreshold) {
  ASSERT_OK_AND_ASSIGN(
      auto is_settled_criterion,
      IsSettledCriterion::Create(/*frequency_hz=*/1.0 / kDtSeconds));

  std::vector<double> values{0.1, 0.5, 0.99};
  for (const auto& threshold : values) {
    SCOPED_TRACE(absl::StrCat("Setting Max Uncertainty Threshold ", threshold));
    INTRINSIC_RT_ASSERT_OK(is_settled_criterion->SetMaxUncertaintyThreshold(
        /*max_uncertainty_threshold=*/threshold));
    double read_threshold = is_settled_criterion->GetMaxUncertaintyThreshold();
    EXPECT_EQ(read_threshold, threshold);
  }
}

TEST_F(IsSettledCriterionTest, IncorrectUpdateDueToSizesMismatch) {
  ASSERT_OK_AND_ASSIGN(
      auto is_settled_criterion,
      IsSettledCriterion::Create(/*frequency_hz=*/1.0 / kDtSeconds));

  ASSERT_OK(is_settled_criterion->Initialize());

  EXPECT_THAT(
      is_settled_criterion->Update(
          /*measured_joint_velocities=*/eigenmath::VectorNd::Zero(kNDofs + 1),
          /*commanded_joint_velocities=*/eigenmath::VectorNd::Zero(kNDofs),
          /*has_trajectory_ended=*/false),
      RealtimeStatusIs(absl::StatusCode::kInvalidArgument,
                       AllOf(HasSubstr("The size of measured_joint_velocities"),
                             HasSubstr("and commanded_joint_velocities"),
                             HasSubstr("should be equal."))));
}

TEST_F(IsSettledCriterionTest, CheckUncertaintyValueIsCorrectlyInitialized) {
  ASSERT_OK_AND_ASSIGN(
      auto is_settled_criterion,
      IsSettledCriterion::Create(/*frequency_hz=*/1.0 / kDtSeconds));
  EXPECT_EQ(is_settled_criterion->GetUncertainty(), 1.0);

  INTRINSIC_RT_ASSERT_OK(is_settled_criterion->Update(
      /*measured_joint_velocities=*/final_state_.velocity,
      /*commanded_joint_velocities=*/final_state_.velocity,
      /*has_trajectory_ended=*/true));
  EXPECT_TRUE(is_settled_criterion->GetUncertainty() < 1.0);

  INTRINSIC_RT_ASSERT_OK(is_settled_criterion->Initialize());
  EXPECT_EQ(is_settled_criterion->GetUncertainty(), 1.0);
}

TEST_F(IsSettledCriterionTest, HalfTrajectoriesNotSettled) {
  // Noise generator.
  const double kNoiseBias = 0.0;
  const std::vector<double> kNoiseStdDevs{0.000, 0.001, 0.002, 0.004, 0.006,
                                          0.008, 0.012, 0.016, 0.020, 0.040};
  absl::BitGen bit_generator = GetRandomBitGenerator();

  // Create instance of IsSettledCriterion.
  ASSERT_OK_AND_ASSIGN(
      auto is_settled_criterion,
      IsSettledCriterion::Create(/*frequency_hz=*/1.0 / kDtSeconds));

  for (size_t id = 0; id < kNoiseStdDevs.size(); ++id) {
    SCOPED_TRACE(absl::StrCat("Noise std dev: ", kNoiseStdDevs[id]));
    GaussianGenerator gaussian_noise(bit_generator, kNoiseBias,
                                     kNoiseStdDevs[id]);
    GaussianGenerator measurement_noise(bit_generator, kNoiseBias, 0.01);

    const size_t kNumSamples = 10;
    for (size_t t = 0; t < kNumSamples; ++t) {
      SCOPED_TRACE(absl::StrCat("  TimeSample: ", t, " out of ", kNumSamples));
      INTRINSIC_RT_ASSERT_OK(is_settled_criterion->Initialize());

      // Loop until the trajectory is completely replayed.
      StateRnPVA current_state = start_state_;
      RollOutTrajectoryWithNoise(0.5 * trajectory_time_seconds_, current_state,
                                 gaussian_noise, is_settled_criterion);

      // Continue looping further and expect it settles.
      size_t timesteps =
          5 * static_cast<size_t>(trajectory_time_seconds_ / kDtSeconds);
      EXPECT_THAT(LoopForTimestepsToReachSettledState(
                      timesteps, /*has_trajectory_ended=*/false, current_state,
                      measurement_noise, is_settled_criterion),
                  StatusIs(absl::StatusCode::kInternal,
                           HasSubstr("The system has not settled.")));
    }
  }
}

TEST_F(IsSettledCriterionTest, LargerThresholdResultsInShorterSettlingIndices) {
  // Noise generator.
  const double kNoiseBias = 0.0;
  const std::vector<double> kNoiseStdDevs{0.001, 0.020};
  const std::vector<double> max_uncertainty_thresholds{
      0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 0.99};
  absl::BitGen bit_generator = GetRandomBitGenerator();

  // Create instance of IsSettledCriterion.
  ASSERT_OK_AND_ASSIGN(
      auto is_settled_criterion,
      IsSettledCriterion::Create(/*frequency_hz=*/1.0 / kDtSeconds));

  for (size_t id = 0; id < kNoiseStdDevs.size(); ++id) {
    SCOPED_TRACE(absl::StrCat("Noise std dev: ", kNoiseStdDevs[id]));
    GaussianGenerator gaussian_noise(bit_generator, kNoiseBias,
                                     kNoiseStdDevs[id]);
    GaussianGenerator measurement_noise(bit_generator, kNoiseBias, 0.01);

    std::vector<double> settling_indices;
    for (const auto threshold : max_uncertainty_thresholds) {
      SCOPED_TRACE(absl::StrCat("Max Uncertainty Threshold ", threshold));
      INTRINSIC_RT_ASSERT_OK(
          is_settled_criterion->SetMaxUncertaintyThreshold(threshold));

      double average_settling_index = 0.0;
      const size_t kNumSamples = 10;
      for (size_t t = 0; t < kNumSamples; ++t) {
        SCOPED_TRACE(
            absl::StrCat("  TimeSample: ", t, " out of ", kNumSamples));

        INTRINSIC_RT_ASSERT_OK(is_settled_criterion->Initialize());

        // Loop until the trajectory is completely replayed.
        StateRnPVA current_state = start_state_;
        RollOutTrajectoryWithNoise(trajectory_time_seconds_, current_state,
                                   gaussian_noise, is_settled_criterion);

        // Continue looping further and expect it settles.
        size_t timesteps =
            5 * static_cast<size_t>(trajectory_time_seconds_ / kDtSeconds);
        ASSERT_OK_AND_ASSIGN(
            int settling_index_output,
            LoopForTimestepsToReachSettledState(
                timesteps, /*has_trajectory_ended=*/true, current_state,
                measurement_noise, is_settled_criterion));
        EXPECT_GT(settling_index_output, -1);

        // Accumulate the timing indices.
        average_settling_index += static_cast<double>(settling_index_output);
      }
      average_settling_index /= kNumSamples;
      settling_indices.push_back(average_settling_index);
    }
    for (int i = 0; i < settling_indices.size() - 1; ++i) {
      EXPECT_GE(settling_indices[i], settling_indices[i + 1]);
    }
  }
}

TEST_F(IsSettledCriterionTest,
       SettlingToLowConfidenceOrNoSettlingDueToOverlyLargeNoise) {
  // Noise generator.
  const double kNoiseBias = 0.0;
  const std::vector<double> kNoiseStdDevs{0.001, 0.100};
  const std::vector<bool> kExpectedSettledState{true, false};
  absl::BitGen bit_generator = GetRandomBitGenerator();

  // Create instance of IsSettledCriterion.
  ASSERT_OK_AND_ASSIGN(
      auto is_settled_criterion,
      IsSettledCriterion::Create(/*frequency_hz=*/1.0 / kDtSeconds));

  for (size_t id = 0; id < kNoiseStdDevs.size(); ++id) {
    SCOPED_TRACE(absl::StrCat("Noise std dev: ", kNoiseStdDevs[id]));
    GaussianGenerator gaussian_noise(bit_generator, kNoiseBias,
                                     kNoiseStdDevs[id]);

    const size_t kNumSamples = 1;
    for (size_t t = 0; t < kNumSamples; ++t) {
      SCOPED_TRACE(absl::StrCat("  TimeSample: ", t, " out of ", kNumSamples));
      INTRINSIC_RT_ASSERT_OK(is_settled_criterion->Initialize());

      // Loop until the trajectory is completely replayed.
      StateRnPVA current_state = start_state_;
      RollOutTrajectoryWithNoise(trajectory_time_seconds_, current_state,
                                 gaussian_noise, is_settled_criterion);

      // Continue looping further and expect it settles.
      size_t timesteps =
          5 * static_cast<size_t>(trajectory_time_seconds_ / kDtSeconds);
      auto has_settled_status = LoopForTimestepsToReachSettledState(
          timesteps, /*has_trajectory_ended=*/true, current_state,
          gaussian_noise, is_settled_criterion);
      if (kExpectedSettledState[id]) {
        ASSERT_OK(has_settled_status);
      } else {
        EXPECT_THAT(has_settled_status,
                    StatusIs(absl::StatusCode::kInternal,
                             HasSubstr("The system has not settled.")));
      }
    }
  }
}

TEST_F(IsSettledCriterionTest, RobotSettlesInitiallyThenUnsettles) {
  const double kNoiseBias = 0.0;
  const std::vector<double> kNoiseStdDevs{0.000, 0.001, 0.002, 0.004, 0.006,
                                          0.008, 0.012, 0.016, 0.020, 0.040};
  absl::BitGen bit_generator = GetRandomBitGenerator();

  // Create instance of IsSettledCriterion.
  ASSERT_OK_AND_ASSIGN(
      auto is_settled_criterion,
      IsSettledCriterion::Create(/*frequency_hz=*/1.0 / kDtSeconds));

  for (size_t id = 0; id < kNoiseStdDevs.size(); ++id) {
    SCOPED_TRACE(absl::StrCat("Noise std dev: ", kNoiseStdDevs[id]));
    GaussianGenerator gaussian_noise(bit_generator, kNoiseBias,
                                     kNoiseStdDevs[id]);
    GaussianGenerator measurement_noise(bit_generator, kNoiseBias, 0.01);
    GaussianGenerator zero_gaussian_noise(bit_generator, kNoiseBias, 0.0);

    const size_t kNumSamples = 5;
    for (size_t t = 0; t < kNumSamples; ++t) {
      SCOPED_TRACE(absl::StrCat("  TimeSample: ", t, " out of ", kNumSamples));
      INTRINSIC_RT_ASSERT_OK(is_settled_criterion->Initialize());

      // Loop until the trajectory is completely replayed.
      StateRnPVA current_state = start_state_;
      RollOutTrajectoryWithNoise(trajectory_time_seconds_, current_state,
                                 gaussian_noise, is_settled_criterion);

      // The robot settles.
      size_t timesteps =
          10 * static_cast<size_t>(trajectory_time_seconds_ / kDtSeconds);
      ASSERT_OK_AND_ASSIGN(
          int settling_index_output,
          LoopForTimestepsToReachSettledState(
              timesteps, /*has_trajectory_ended=*/true, current_state,
              measurement_noise, is_settled_criterion));
      EXPECT_GT(settling_index_output, 0);

      // Remain in a settled state for 100ms.
      LoopForTimestepsInSettledState(
          /*timesteps=*/100, current_state, zero_gaussian_noise,
          is_settled_criterion);

      // The robot starts moving again, thus the `is_settled_criterion` reports
      // accordingly that the state is unsettled.
      ASSERT_OK(LoopForTimestepsToReachUnsettledState(
          timesteps, current_state, gaussian_noise, *is_settled_criterion,
          /*extra_robot_velocity=*/1.0));
    }
  }
}

TEST_F(IsSettledCriterionTest, NoisyTrajectoriesSettledInNominalTimings) {
  // Noise generator.
  const double kNoiseBias = 0.0;
  const double kAllowedPercentageError = 10;
  const std::vector<double> kNoiseStdDevs{0.020, 0.070};
  const std::vector<double> kNominalTimings{250.0, 400.0};
  absl::BitGen bit_generator = GetRandomBitGenerator();

  // Create instance of IsSettledCriterion.
  ASSERT_OK_AND_ASSIGN(
      auto is_settled_criterion,
      IsSettledCriterion::Create(/*frequency_hz=*/1.0 / kDtSeconds));

  for (size_t id = 0; id < kNoiseStdDevs.size(); ++id) {
    SCOPED_TRACE(absl::StrCat("Noise std dev: ", kNoiseStdDevs[id]));
    GaussianGenerator gaussian_noise(bit_generator, kNoiseBias,
                                     kNoiseStdDevs[id]);
    GaussianGenerator zero_gaussian_noise(bit_generator, kNoiseBias, 0.0);

    double average_settling_index = 0.0;
    const size_t kNumSamples = 10;
    for (size_t t = 0; t < kNumSamples; ++t) {
      SCOPED_TRACE(absl::StrCat("  TimeSample: ", t, " out of ", kNumSamples));
      INTRINSIC_RT_ASSERT_OK(is_settled_criterion->Initialize());

      // Loop until the trajectory is completely replayed.
      StateRnPVA current_state = start_state_;
      RollOutTrajectoryWithNoise(trajectory_time_seconds_, current_state,
                                 gaussian_noise, is_settled_criterion);

      // Continue looping further and expect it settles.
      size_t timesteps =
          5 * static_cast<size_t>(trajectory_time_seconds_ / kDtSeconds);
      ASSERT_OK_AND_ASSIGN(
          int settling_index_output,
          LoopForTimestepsToReachSettledState(
              timesteps, /*has_trajectory_ended=*/true, current_state,
              zero_gaussian_noise, is_settled_criterion));
      EXPECT_GT(settling_index_output, -1);

      // Accumulate the timing indices.
      average_settling_index += static_cast<double>(settling_index_output);
    }
    average_settling_index /= kNumSamples;
    EXPECT_LE(std::abs(average_settling_index - kNominalTimings[id]) /
                  kNominalTimings[id],
              kAllowedPercentageError / 100.0);
  }
}

TEST_F(IsSettledCriterionTest, NoisyTrajectoriesDoNotSettleIfNoisePersist) {
  // Noise generator.
  const double kNoiseBias = 0.0;
  const double kNonZeroStdDev = 0.020;
  const std::vector<double> kNoiseStdDevs{0.020, 0.070};
  const std::vector<double> kNominalTimings{250.0, 400.0};
  absl::BitGen bit_generator = GetRandomBitGenerator();

  // Create instance of IsSettledCriterion.
  ASSERT_OK_AND_ASSIGN(
      auto is_settled_criterion,
      IsSettledCriterion::Create(/*frequency_hz=*/1.0 / kDtSeconds));

  for (size_t id = 0; id < kNoiseStdDevs.size(); ++id) {
    SCOPED_TRACE(absl::StrCat("Noise std dev: ", kNoiseStdDevs[id]));
    GaussianGenerator gaussian_noise(bit_generator, kNoiseBias,
                                     kNoiseStdDevs[id]);
    GaussianGenerator measurement_noise(bit_generator, kNoiseBias,
                                        kNonZeroStdDev);

    // double average_settling_index = 0.0;
    const size_t kNumSamples = 10;
    for (size_t t = 0; t < kNumSamples; ++t) {
      SCOPED_TRACE(absl::StrCat("  TimeSample: ", t, " out of ", kNumSamples));
      INTRINSIC_RT_ASSERT_OK(is_settled_criterion->Initialize());

      // Loop until the trajectory is completely replayed.
      StateRnPVA current_state = start_state_;
      RollOutTrajectoryWithNoise(trajectory_time_seconds_, current_state,
                                 gaussian_noise, is_settled_criterion);

      // Continue looping further and expect it settles.
      size_t timesteps =
          5 * static_cast<size_t>(trajectory_time_seconds_ / kDtSeconds);
      EXPECT_THAT(LoopForTimestepsToReachSettledState(
                      timesteps, /*has_trajectory_ended=*/true, current_state,
                      measurement_noise, is_settled_criterion),
                  StatusIs(absl::StatusCode::kInternal,
                           HasSubstr("The system has not settled.")));
    }
  }
}

TEST_F(IsSettledCriterionTest, VaryingNoiseTrajectoriesSettled) {
  // Noise generator.
  const double kNoiseBias = 0.0;
  const std::vector<double> kNoiseStdDevs{0.000, 0.001, 0.002, 0.004, 0.006,
                                          0.008, 0.012, 0.016, 0.020, 0.040};
  absl::BitGen bit_generator = GetRandomBitGenerator();

  // Create instance of IsSettledCriterion.
  ASSERT_OK_AND_ASSIGN(
      auto is_settled_criterion,
      IsSettledCriterion::Create(/*frequency_hz=*/1.0 / kDtSeconds));

  std::vector<double> average_settling_indices;
  for (size_t id = 0; id < kNoiseStdDevs.size(); ++id) {
    SCOPED_TRACE(absl::StrCat("Noise std dev: ", kNoiseStdDevs[id]));
    GaussianGenerator gaussian_noise(bit_generator, kNoiseBias,
                                     kNoiseStdDevs[id]);
    GaussianGenerator zero_gaussian_noise(bit_generator, kNoiseBias, 0.0);

    double average_settling_index = 0.0;
    const size_t kNumSamples = 10;
    for (size_t t = 0; t < kNumSamples; ++t) {
      SCOPED_TRACE(absl::StrCat("  TimeSample: ", t, " out of ", kNumSamples));
      INTRINSIC_RT_ASSERT_OK(is_settled_criterion->Initialize());

      // Loop until the trajectory is completely replayed.
      StateRnPVA current_state = start_state_;
      RollOutTrajectoryWithNoise(trajectory_time_seconds_, current_state,
                                 gaussian_noise, is_settled_criterion);

      // Continue looping further and expect it settles.
      size_t timesteps =
          static_cast<size_t>(trajectory_time_seconds_ / kDtSeconds);
      ASSERT_OK_AND_ASSIGN(
          int settling_index_output,
          LoopForTimestepsToReachSettledState(
              timesteps, /*has_trajectory_ended=*/true, current_state,
              zero_gaussian_noise, is_settled_criterion));
      EXPECT_GT(settling_index_output, -1);

      // Accumulate the timing indices.
      average_settling_index += static_cast<double>(settling_index_output);
    }
    average_settling_index /= kNumSamples;
    average_settling_indices.push_back(average_settling_index);
  }
  for (size_t i = 0; i < average_settling_indices.size() - 1; ++i) {
    EXPECT_LE(average_settling_indices[i], average_settling_indices[i + 1]);
  }
}

}  // namespace
}  // namespace intrinsic::icon
