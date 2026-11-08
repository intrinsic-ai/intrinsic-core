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

#include "intrinsic/icon/control/algorithms/linear_joint_acceleration_filter.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cmath>
#include <cstdlib>
#include <vector>

#include "absl/random/random.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "internal/testing.h"
#include "intrinsic/eigenmath/random_number_utils.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/proto/linear_joint_acceleration_filter_config.pb.h"
#include "intrinsic/icon/utils/realtime_status_matchers.h"
#include "intrinsic/math/gaussian_noise.h"
#include "intrinsic/math/linear_systems/discrete_algebraic_riccati_equation.h"
#include "intrinsic/math/symmetric_positive_definite_matrix_squareroot.h"

namespace intrinsic::icon {
namespace {

using ::absl_testing::StatusIs;
using SingleJointState =
    ::intrinsic::icon::LinearJointAccelerationFilter::SingleJointState;

const double kTimeStep = 0.001;
const size_t kNSteps = 1000;
const double kOmega = 2 * M_PI;
const size_t kStateDim = LinearJointAccelerationFilter::kStateDim;
const size_t kOutputDim = LinearJointAccelerationFilter::kOutputDim;

class LinearJointAccelerationFilterTest : public ::testing::Test {
 public:
  LinearJointAccelerationFilterTest() {
    // Default values for process and measurement noise.
    eigenmath::VectorXd q(kStateDim);  // Define diagonal process noise.
    q << 1e-3, 1e-3, 1e-2, 150;
    Q_ = q.asDiagonal();

    eigenmath::VectorXd r(kOutputDim);  // Define diagonal observation noise.
    r << 1e-4, 1e-3;
    R_ = r.asDiagonal();
  }

  // Evaluates a system from x0 using a given sinusoidal default motion, process
  // and measurement noise. Returns a vector of observations.
  static std::vector<SingleJointState> RollOutWithDefaultPolicy(
      const size_t N, const eigenmath::MatrixXd& R_sim) {
    // Define random number generator with Gaussian distribution.
    absl::BitGen gen = GetRandomBitGenerator();
    intrinsic::GaussianGenerator dist_position(gen, 0.0, R_sim(0, 0));
    intrinsic::GaussianGenerator dist_velocity(gen, 0.0, R_sim(1, 1));

    std::vector<SingleJointState> output_trajectory;

    for (size_t i = 0; i < N; i++) {
      SingleJointState y;
      // Evaluate synthetic data for joint state - noisify position and
      // velocity.
      const double phi = i * kTimeStep * kOmega;
      y.position = std::sin(phi) + dist_position.Generate();
      y.velocity = kOmega * std::cos(phi) + dist_velocity.Generate();
      y.acceleration = -std::pow(kOmega, 2) * std::sin(phi);
      y.jerk = -std::pow(kOmega, 3) * std::cos(phi);
      output_trajectory.push_back(y);
    }
    return output_trajectory;
  }

 protected:
  // Process noise covariance.
  eigenmath::MatrixXd Q_;  // NOLINT(google3-readability-class-member-naming)
  // Measurement uncertainty covariance.
  eigenmath::MatrixXd R_;  // NOLINT(google3-readability-class-member-naming)
};

TEST_F(LinearJointAccelerationFilterTest, RolloutWorks) {
  // Roll out system and verify that dimensions are correct.
  auto rollout = RollOutWithDefaultPolicy(kNSteps, R_);
  EXPECT_EQ(rollout.size(), kNSteps);
}

TEST_F(LinearJointAccelerationFilterTest, FilterCreateWorksForNaiveIteration) {
  DiscreteAlgebraicRiccatiSolverSettings dare_settings;
  dare_settings.strategy = DiscreteAlgebraicRiccatiSolverSettings::kNaive;
  dare_settings.max_iterations = 1000;
  ASSERT_OK(
      LinearJointAccelerationFilter::Create(kTimeStep, Q_, R_, dare_settings));
}

TEST_F(LinearJointAccelerationFilterTest, FilterCreateWorksForRobustIteration) {
  DiscreteAlgebraicRiccatiSolverSettings dare_settings;
  dare_settings.strategy = DiscreteAlgebraicRiccatiSolverSettings::kRobust;
  dare_settings.max_iterations = 1000;
  ASSERT_OK(
      LinearJointAccelerationFilter::Create(kTimeStep, Q_, R_, dare_settings));
}

TEST_F(LinearJointAccelerationFilterTest,
       FilterCreateWorksWithInitialCovarianceMatrix) {
  DiscreteAlgebraicRiccatiSolverSettings dare_settings;
  dare_settings.max_iterations = 1000;
  eigenmath::MatrixXd P_initial =
      eigenmath::MatrixXd::Identity(kStateDim, kStateDim);
  ASSERT_OK(LinearJointAccelerationFilter::Create(kTimeStep, Q_, R_,
                                                  dare_settings, P_initial));
}

TEST_F(LinearJointAccelerationFilterTest, CreateWorksWithProto) {
  intrinsic_proto::icon::LinearJointAccelerationFilterConfig config;
  intrinsic_proto::icon::DiscreteAlgebraicRicattiEquationSolutionConfig
      dare_config;
  // Discrete Ricatti Equation  settings.
  dare_config.set_max_iterations(5000);
  dare_config.set_regularizer_epsilon(1e-7);
  dare_config.set_accuracy(1e-7);
  dare_config.set_dare_iteration_strategy(
      intrinsic_proto::icon::DiscreteAlgebraicRicattiEquationSolutionConfig::
          DARE_ITERATION_STRATEGY_ROBUST);
  *config.mutable_dare_settings() = dare_config;
  config.set_joint_position_process_noise(1e-3);
  config.set_joint_velocity_process_noise(1e-3);
  config.set_joint_acceleration_process_noise(50);
  config.set_joint_jerk_process_noise(1250);
  config.set_joint_position_measurement_noise(1e-4);
  config.set_joint_velocity_measurement_noise(1e-3);
  ASSERT_OK_AND_ASSIGN(auto filter, LinearJointAccelerationFilter::Create(
                                        config, 1 / kTimeStep));
}

TEST_F(LinearJointAccelerationFilterTest,
       FilterCreateFailsForWrongQDimensions) {
  Q_.resize(kStateDim, kStateDim + 1);  // Wrong dimensions.
  ASSERT_THAT(LinearJointAccelerationFilter::Create(kTimeStep, Q_, R_),
              StatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST_F(LinearJointAccelerationFilterTest,
       FilterCreateFailsForWrongRDimensions) {
  R_.resize(kStateDim, kStateDim + 1);  // Wrong dimensions.
  ASSERT_THAT(LinearJointAccelerationFilter::Create(kTimeStep, Q_, R_),
              StatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST_F(LinearJointAccelerationFilterTest,
       FilterCreateFailsForWrongPDimensions) {
  DiscreteAlgebraicRiccatiSolverSettings dare_settings;
  eigenmath::MatrixXd P_initial(kStateDim, kStateDim + 1);  // Wrong dimensions.
  ASSERT_THAT(LinearJointAccelerationFilter::Create(kTimeStep, Q_, R_,
                                                    dare_settings, P_initial),
              StatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST_F(LinearJointAccelerationFilterTest, AccelerationEstimationWorks) {
  DiscreteAlgebraicRiccatiSolverSettings dare_settings;
  dare_settings.strategy = DiscreteAlgebraicRiccatiSolverSettings::kRobust;
  dare_settings.max_iterations = 5000;
  dare_settings.regularizer_epsilon = 1e-5;
  dare_settings.strategy = DiscreteAlgebraicRiccatiSolverSettings::kNaive;
  ASSERT_OK_AND_ASSIGN(
      auto joint_filter,
      LinearJointAccelerationFilter::Create(kTimeStep, Q_, R_, dare_settings));

  // Retrieve the converged covariance from the filter, used for testing
  // acceleration estimate later.
  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
      const auto kalman_matrix_stddev,
      SymmetricPositiveDefiniteMatrixSquareRoot(
          eigenmath::MatrixNd(joint_filter.GetCovariance())));

  // We choose the simulation noise different from the estimator model noise.
  eigenmath::VectorXd r(kOutputDim);
  r << 1e-4, 1e-1;
  eigenmath::MatrixXd R_sim = r.asDiagonal();

  // Create synthetic data based on a sinusoid motion.
  auto output_trajectory = RollOutWithDefaultPolicy(kNSteps, R_sim);

  SingleJointState x0;
  joint_filter.SetInitialState(x0);

  std::vector<SingleJointState> estimates;

  // Run estimator over simulated output data and validate state estimates.
  for (auto& joint_state : output_trajectory) {
    // Filter iteration.
    joint_filter.Predict(
        x0);  // The input does actually not matter here, supply a dummy.
    estimates.push_back(joint_filter.Correct(joint_state));
  }

  // The filter takes time to converge, so we only check the estimates of the
  // 500th datapoint onwards. Since this is a probabilistic check, we allow for
  // errors to happen, but limit the number of admissible errors.
  size_t error_violation_count = 0;
  for (size_t i = 500; i < estimates.size(); i++) {
    // The acceleration estimation error should be bounded by the filter
    // covariance for that degree of freedom.
    if (std::abs(estimates[i].acceleration -
                 output_trajectory[i].acceleration) >
        2 * kalman_matrix_stddev(3, 3)) {
      error_violation_count++;
    }
  }
  EXPECT_LT(error_violation_count, 10);
}

}  // namespace
}  // namespace intrinsic::icon
