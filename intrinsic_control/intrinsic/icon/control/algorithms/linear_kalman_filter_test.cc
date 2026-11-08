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

#include "intrinsic/icon/control/algorithms/linear_kalman_filter.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

#include "absl/random/random.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "internal/testing.h"
#include "intrinsic/eigenmath/matchers.h"
#include "intrinsic/eigenmath/random_number_utils.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/testing/malloc_test.h"
#include "intrinsic/kinematics/types/joint_state.h"
#include "intrinsic/math/gaussian_noise.h"
#include "intrinsic/math/linear_systems/discrete_algebraic_riccati_equation.h"
#include "intrinsic/math/linear_systems/kalman_criterions.h"
#include "intrinsic/math/linear_systems/testing/harmonic_oscillator_test_system.h"
#include "intrinsic/math/linear_systems/testing/linear_test_problem.h"

namespace intrinsic::icon {
namespace {

using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;
using ::intrinsic::eigenmath::testing::IsApprox;

const double kTimeStep = 0.01;
// TODO(b/283910442): lower tolerances fail for cpu=haswell
const double kDefaultEpsilon = 2e-1;

class LinearKalmanFilterTest : public ::testing::Test {
 public:
  struct Rollout {
    std::vector<eigenmath::VectorXd> state_trajectory;
    std::vector<eigenmath::VectorXd> input_trajectory;
    std::vector<eigenmath::VectorXd> output_trajectory;
  };

  LinearKalmanFilterTest() : system_(kTimeStep) {
    // Default values for process and measurement noise.
    Q_ = eigenmath::MatrixXd::Zero(HarmonicOscillatorTestSystem::kStateDim,
                                   HarmonicOscillatorTestSystem::kStateDim);
    Q_.diagonal() << 0.01, 0.01;
    R_ = eigenmath::MatrixXd::Zero(HarmonicOscillatorTestSystem::kControlDim,
                                   HarmonicOscillatorTestSystem::kControlDim);
    R_.diagonal().setConstant(0.01);
  }

  // Forward integrate the system from x0 using a given default policy, process
  // and measurement noise. Record states, inputs and system observations.
  Rollout RollOutWithDefaultPolicy(const eigenmath::VectorXd& x0,
                                   const size_t N,
                                   const eigenmath::MatrixXd& Q_sim,
                                   const eigenmath::MatrixXd& R_sim) {
    absl::BitGen gen = GetRandomBitGenerator();
    intrinsic::GaussianGenerator noise_generator(gen, 0.0, 1.0);

    Rollout rollout;
    rollout.state_trajectory.push_back(x0);
    rollout.output_trajectory.emplace_back(system_.C * x0);

    for (size_t i = 1; i < N; i++) {
      eigenmath::VectorXd u(HarmonicOscillatorTestSystem::kControlDim);
      u << sin(0.1 * i * kTimeStep);
      rollout.input_trajectory.push_back(u);

      // Propagate dynamics and observations, noisify.
      // TODO(giftthaler): strictly speaking, this is not optimal since we add
      // uniform and not Gaussian noise. Change to Gaussian noise.
      rollout.state_trajectory.emplace_back(
          system_.A * rollout.state_trajectory[i - 1] + system_.B * u +
          Q_sim * noise_generator.Generate(
                      HarmonicOscillatorTestSystem::kStateDim));
      rollout.output_trajectory.emplace_back(
          system_.C * rollout.state_trajectory.back() +
          R_sim * noise_generator.Generate(
                      HarmonicOscillatorTestSystem::kOutputDim));
    }
    return rollout;
  }

 protected:
  HarmonicOscillatorTestSystem system_;
  // Process noise covariance.
  eigenmath::MatrixXd Q_;  // NOLINT(google3-readability-class-member-naming)
  // Measurement uncertainty covariance.
  eigenmath::MatrixXd R_;  // NOLINT(google3-readability-class-member-naming)
  DiscreteAlgebraicRiccatiSolverSettings dare_settings_;
  std::unique_ptr<LinearKalmanFilter> filter_ = nullptr;
};

TEST_F(LinearKalmanFilterTest, RolloutWorks) {
  // System initial state.
  eigenmath::VectorXd x0 =
      eigenmath::VectorXd::Ones(HarmonicOscillatorTestSystem::kStateDim);

  // Roll out system and verify that dimensions are correct.
  size_t nsteps = 1000;
  auto rollout = RollOutWithDefaultPolicy(x0, nsteps, Q_, R_);

  EXPECT_EQ(rollout.state_trajectory.size(), nsteps);
  EXPECT_EQ(rollout.output_trajectory.size(), nsteps);
  EXPECT_EQ(rollout.input_trajectory.size(), nsteps - 1);
  EXPECT_TRUE(rollout.input_trajectory.back().allFinite());
  EXPECT_TRUE(rollout.state_trajectory.back().allFinite());
  EXPECT_TRUE(rollout.output_trajectory.back().allFinite());
}

TEST_F(LinearKalmanFilterTest, FilterCreateWorksForNaiveIteration) {
  dare_settings_.strategy = DiscreteAlgebraicRiccatiSolverSettings::kNaive;

  ASSERT_OK(LinearKalmanFilter::Create(Q_, R_, system_.A, system_.B, system_.C,
                                       dare_settings_));
}

TEST_F(LinearKalmanFilterTest, FilterCreateWorksForRobustIteration) {
  dare_settings_.strategy = DiscreteAlgebraicRiccatiSolverSettings::kRobust;
  ASSERT_OK(LinearKalmanFilter::Create(Q_, R_, system_.A, system_.B, system_.C,
                                       dare_settings_));
}

TEST_F(LinearKalmanFilterTest, FilterCreateWorksWithInitialPMatrix) {
  eigenmath::MatrixXd P_initial =
      eigenmath::MatrixXd::Identity(HarmonicOscillatorTestSystem::kStateDim,
                                    HarmonicOscillatorTestSystem::kStateDim);
  ASSERT_OK(LinearKalmanFilter::Create(Q_, R_, system_.A, system_.B, system_.C,
                                       P_initial, dare_settings_));
}

TEST_F(LinearKalmanFilterTest,
       FilterCreateFailsForInitialPMatrixWithInconsistentDimensions) {
  eigenmath::MatrixXd P_initial;
  EXPECT_THAT(LinearKalmanFilter::Create(Q_, R_, system_.A, system_.B,
                                         system_.C, P_initial, dare_settings_),
              StatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST_F(LinearKalmanFilterTest, FilterAchievesPerfectTrackingWithoutNoise) {
  ASSERT_OK_AND_ASSIGN(auto kf,
                       LinearKalmanFilter::Create(Q_, R_, system_.A, system_.B,
                                                  system_.C, dare_settings_));
  filter_ = std::make_unique<LinearKalmanFilter>(std::move(kf));

  // System initial state.
  eigenmath::VectorXd x0 =
      eigenmath::VectorXd::Ones(HarmonicOscillatorTestSystem::kStateDim);

  // Roll out system to get ground truth state trajectory, employ zero process
  // and measurement noise.
  eigenmath::MatrixXd Q_sim =
      eigenmath::MatrixXd::Zero(HarmonicOscillatorTestSystem::kStateDim,
                                HarmonicOscillatorTestSystem::kStateDim);
  eigenmath::MatrixXd R_sim =
      eigenmath::MatrixXd::Zero(HarmonicOscillatorTestSystem::kControlDim,
                                HarmonicOscillatorTestSystem::kControlDim);

  auto rollout = RollOutWithDefaultPolicy(x0, 1000, Q_sim, R_sim);

  filter_->SetInitialState(x0);
  EXPECT_THAT(filter_->GetCurrentState(), IsApprox(x0));

  // Run estimator over simulated process data and validate state estimates.
  for (size_t i = 0; i < rollout.input_trajectory.size(); i++) {
    // Filter iteration.
    filter_->Predict(rollout.input_trajectory[i]);
    eigenmath::VectorXd x_est =
        filter_->Correct(rollout.output_trajectory[i + 1]);

    // Due to lack of noise, the state estimate must be perfect (within
    // numerical bounds).
    EXPECT_THAT(x_est,
                IsApprox(rollout.state_trajectory[i + 1], kDefaultEpsilon));
  }
}

TEST_F(LinearKalmanFilterTest, FilterWorksWithNoisyProcess) {
  ASSERT_OK_AND_ASSIGN(auto kf,
                       LinearKalmanFilter::Create(Q_, R_, system_.A, system_.B,
                                                  system_.C, dare_settings_));
  filter_ = std::make_unique<LinearKalmanFilter>(std::move(kf));

  // Retrieve covariane for result validation.
  eigenmath::MatrixXd P_steady = filter_->GetCovariance();

  // System initial state.
  eigenmath::VectorXd x0 =
      eigenmath::VectorXd::Ones(HarmonicOscillatorTestSystem::kStateDim);

  // Roll out system to get ground truth state trajectory, employ default
  // process and measurement noise.
  auto rollout = RollOutWithDefaultPolicy(x0, 1000, Q_, R_);

  // Run estimator over simulated process data and validate state estimates.
  filter_->SetInitialState(x0);
  for (size_t i = 0; i < rollout.input_trajectory.size(); i++) {
    // Filter iteration.
    filter_->Predict(rollout.input_trajectory[i]);
    eigenmath::VectorXd x_est =
        filter_->Correct(rollout.output_trajectory[i + 1]);

    // The state estimate must be within loose bounds of the steady-state
    // filter covariance.
    EXPECT_NEAR(x_est(0), rollout.state_trajectory[i + 1](0),
                std::sqrt(P_steady(0, 0)));
    EXPECT_NEAR(x_est(1), rollout.state_trajectory[i + 1](1),
                std::sqrt(P_steady(1, 1)));
  }
}

TEST(LinearKalmanFilterMallocTest, PredictPerformsNoAllocations) {
  LinearTestProblem problem = LinearTestProblem::Create();

  ASSERT_OK_AND_ASSIGN(
      auto filter, LinearKalmanFilter::Create(problem.Q, problem.V, problem.A,
                                              problem.B, problem.C));

  const eigenmath::VectorXd u =
      eigenmath::VectorXd::Zero(LinearTestProblem::kControlDim);

  eigenmath::VectorXd x_est =
      eigenmath::VectorXd::Zero(LinearTestProblem::kStateDim);
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  x_est = filter.Predict(u);
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
}

TEST(LinearKalmanFilterMallocTest, CorrectionPerformsNoAllocations) {
  LinearTestProblem problem = LinearTestProblem::Create();

  ASSERT_OK_AND_ASSIGN(
      auto filter, LinearKalmanFilter::Create(problem.Q, problem.V, problem.A,
                                              problem.B, problem.C));

  const eigenmath::VectorXd y =
      eigenmath::VectorXd::Zero(LinearTestProblem::kOutputDim);
  eigenmath::VectorXd x_est(LinearTestProblem::kStateDim);
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  x_est = filter.Correct(y);
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
}

TEST(LinearKalmanFilterMallocTest, LargeScaleSystemsPerformNoAllocation) {
  const size_t state_dim = 28;
  const size_t control_dim = 18;
  const size_t output_dim = 19;

  // A meaningless large-scale system dynamics matrix with direct feedthrough
  // and small dynamic coupling on the top right off-diagonal.
  // NOLINT(google3-readability-class-member-naming)
  eigenmath::MatrixXd A = eigenmath::MatrixXd::Identity(state_dim, state_dim);
  A.topRightCorner(state_dim - 1, state_dim - 1).diagonal().setConstant(1);

  // A control matrix with simply direct action in the lower block.
  // NOLINT(google3-readability-class-member-naming)
  eigenmath::MatrixXd B = eigenmath::MatrixXd::Zero(state_dim, control_dim);
  B.bottomRows(control_dim).setIdentity();

  // An output matrix which simply captures the first "output_dim" states.
  // NOLINT(google3-readability-class-member-naming)
  eigenmath::MatrixXd C = eigenmath::MatrixXd::Zero(output_dim, state_dim);
  C.leftCols(output_dim).setIdentity();

  // We set the covariances to simple Identities.
  // NOLINT(google3-readability-class-member-naming)
  eigenmath::MatrixXd Q = eigenmath::MatrixXd::Identity(state_dim, state_dim);
  // NOLINT(google3-readability-class-member-naming)
  eigenmath::MatrixXd R = eigenmath::MatrixXd::Identity(output_dim, output_dim);

  // Ensure the system is observable.
  EXPECT_THAT(IsObservable(A, C), IsOkAndHolds(true));

  ASSERT_OK_AND_ASSIGN(auto filter, LinearKalmanFilter::Create(Q, R, A, B, C));

  // Prediction needs to work with VectorXd types.
  {
    eigenmath::VectorXd x_est(state_dim);
    const eigenmath::VectorXd u = eigenmath::VectorXd::Zero(control_dim);
    IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
    x_est = filter.Predict(u);
    IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
  }

  // Correction needs to work with VectorXd types.
  {
    eigenmath::VectorXd x_est(state_dim);
    const eigenmath::VectorXd y = eigenmath::VectorXd::Zero(output_dim);
    IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
    x_est = filter.Correct(y);
    IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
  }

  // Prediction needs to work with VectorXd types constructed from JointState
  // types without malloc.
  {
    JointStatePV u_realtime;
    ASSERT_OK(u_realtime.SetSize(state_dim / 4));
    u_realtime.position.setConstant(1.0);
    u_realtime.velocity.setConstant(1.0);
    eigenmath::VectorXd x_est(state_dim);
    eigenmath::VectorXd u_est = eigenmath::VectorXd::Zero(control_dim);
    IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
    u_est.segment(0, state_dim / 4) = u_realtime.position;
    u_est.segment(state_dim / 4, state_dim / 4) = u_realtime.velocity;
    x_est = filter.Predict(u_est);
    IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
  }

  // Correction needs to work with VectorXd types constructed from VectorNd
  // types without malloc.
  {
    eigenmath::VectorNd measurement_realtime_1 = eigenmath::VectorNd::Ones(6);
    eigenmath::VectorNd measurement_realtime_2 = eigenmath::VectorNd::Zero(6);
    eigenmath::VectorXd x_est(state_dim);
    eigenmath::VectorXd y_est = eigenmath::VectorXd::Zero(output_dim);
    IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
    y_est.segment(0, 6) = measurement_realtime_1;
    y_est.segment(6, 6) = measurement_realtime_2;
    x_est = filter.Correct(y_est);
    IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
  }
}

}  // namespace
}  // namespace intrinsic::icon
