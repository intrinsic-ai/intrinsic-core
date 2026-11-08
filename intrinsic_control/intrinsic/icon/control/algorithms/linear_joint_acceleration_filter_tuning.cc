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

#include <cstddef>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/random/random.h"
#include "absl/status/status.h"
#include "intrinsic/eigenmath/eigen_file_export.h"
#include "intrinsic/eigenmath/random_number_utils.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/control/algorithms/chirp_signal.h"
#include "intrinsic/icon/control/algorithms/linear_joint_acceleration_filter.h"
#include "intrinsic/icon/release/portable/init_intrinsic.h"
#include "intrinsic/icon/utils/realtime_status_macro.h"
#include "intrinsic/math/gaussian_noise.h"
#include "intrinsic/math/linear_systems/discrete_algebraic_riccati_equation.h"
#include "intrinsic/math/symmetric_positive_definite_matrix_squareroot.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic::icon {

using SingleJointState =
    ::intrinsic::icon::LinearJointAccelerationFilter::SingleJointState;

const double kTimeStep = 0.001;
const double kNSteps = 6000;
const size_t kStateDim = LinearJointAccelerationFilter::kStateDim;
const size_t kOutputDim = LinearJointAccelerationFilter::kOutputDim;

// Evaluates a system from an initial state using a hard-coded default motion,
// hard-coded process noise and measurement noise with covariance `R_sim`.
// Returns a vector of observations including position, velocity and
// acceleration. Jerk trajectories are not evaluated because they are irrelevant
// to estimator tuning.
std::vector<SingleJointState> RollOutWithDefaultPolicy(
    const size_t N, const eigenmath::MatrixXd& R_sim) {
  // Define a random number generator with Gaussian distribution.
  absl::BitGen gen = GetRandomBitGenerator();
  intrinsic::GaussianGenerator dist_position(gen, 0.0, R_sim(0, 0));
  intrinsic::GaussianGenerator dist_velocity(gen, 0.0, R_sim(1, 1));

  std::vector<SingleJointState> output_trajectory;

  // Define and evaluate a chirp signal, also add some noise to position and
  // velocity signals.
  ChirpSignal chirp(/*amplitude=*/1.0, /*time_horizon=*/kTimeStep * kNSteps,
                    /*starting_frequency=*/0.0,
                    /*final_frequency=*/10);

  for (size_t i = 0; i < N; i++) {
    ChirpSignal::Result chirp_result = chirp.Evaluate(i * kTimeStep);

    output_trajectory.push_back(SingleJointState{
        .position = chirp_result.position + dist_position.Generate(),
        .velocity = chirp_result.velocity + dist_velocity.Generate(),
        .acceleration = chirp_result.acceleration});
  }

  // Append a phase of zero velocity, constant position, no noise.
  for (size_t i = 0; i < 500; i++) {
    SingleJointState y;
    y.position = output_trajectory.back().position;
    y.velocity = dist_velocity.Generate();
    y.acceleration = 0;
    y.jerk = 0;
    output_trajectory.push_back(y);
  }

  // Append a phase of constant accel, inf jerk.
  for (size_t i = 0; i < 500; i++) {
    SingleJointState y;
    y.acceleration = 50;
    y.velocity = output_trajectory.back().velocity +
                 kTimeStep * y.acceleration + dist_velocity.Generate();
    y.position = output_trajectory.back().position + kTimeStep * y.velocity +
                 dist_position.Generate();
    y.jerk = 0;
    output_trajectory.push_back(y);
  }

  // Append a phase of constant decel, inf jerk, no noise.
  for (size_t i = 0; i < 501; i++) {
    SingleJointState y;
    y.acceleration = -50;
    y.velocity = output_trajectory.back().velocity + kTimeStep * y.acceleration;
    y.position = output_trajectory.back().position + kTimeStep * y.velocity;
    y.jerk = 0;
    output_trajectory.push_back(y);
  }

  // Append a phase of zero velocity, constant position.
  for (size_t i = 0; i < 500; i++) {
    SingleJointState y;
    y.position = output_trajectory.back().position;
    y.velocity = 0;
    y.acceleration = 0;
    y.jerk = 0;
    output_trajectory.push_back(y);
  }
  return output_trajectory;
}

// Runs a joint acceleration filter on synthetic data and stores the resulting
// estimates in CSV-files which can be used for fruther analysis, e.g. in Octave
// or Matlab.
absl::Status RunFilterAndStoreData() {
  // Create synthetic data.
  eigenmath::VectorXd r_sim(kOutputDim);
  r_sim << 1e-4, 1e-3;

  eigenmath::MatrixXd R_sim = r_sim.asDiagonal();
  std::vector<SingleJointState> rollout =
      RollOutWithDefaultPolicy(kNSteps, R_sim);

  // Process noise covariance.
  eigenmath::VectorXd q(kStateDim);
  q << 1e-3, 1e-3, 50, 1250;
  eigenmath::MatrixXd Q = q.asDiagonal();

  // Measurement uncertainty covariance.
  eigenmath::VectorXd r(kOutputDim);
  r << 1e-4, 1e-3;
  eigenmath::MatrixXd R = r.asDiagonal();

  LOG(INFO) << "Q-matrix: \n" << Q;
  LOG(INFO) << "R-matrix: \n" << R;

  DiscreteAlgebraicRiccatiSolverSettings dare_settings_;
  dare_settings_.max_iterations = 5000;
  dare_settings_.regularizer_epsilon = 1e-7;
  dare_settings_.accuracy = 1e-7;
  dare_settings_.strategy = DiscreteAlgebraicRiccatiSolverSettings::kRobust;
  INTR_ASSIGN_OR_RETURN(auto filter, LinearJointAccelerationFilter::Create(
                                         kTimeStep, Q, R, dare_settings_));

  LOG(INFO) << "Converged in " << filter.GetDareSolution().iteration_count
            << " iterations.";

  INTRINSIC_RT_ASSIGN_OR_RETURN(
      const eigenmath::MatrixNd P_sqrt,
      SymmetricPositiveDefiniteMatrixSquareRoot(
          eigenmath::MatrixNd(filter.GetCovariance())));

  LOG(INFO) << "P-matrix sqrt: \n" << P_sqrt;
  LOG(INFO) << "P-matrix: \n" << filter.GetCovariance();

  SingleJointState x0;
  x0.position = 0;
  x0.velocity = 0;
  x0.acceleration = 0;
  x0.jerk = 0;

  filter.SetInitialState(x0);

  // Run estimator over simulated output data and record estimates.
  eigenmath::VectorXd pos_ground_truth(rollout.size());
  eigenmath::VectorXd vel_ground_truth(rollout.size());
  eigenmath::VectorXd acc_ground_truth(rollout.size());
  eigenmath::VectorXd jerk_ground_truth(rollout.size());
  eigenmath::VectorXd pos_est(rollout.size());
  eigenmath::VectorXd vel_est(rollout.size());
  eigenmath::VectorXd acc_est(rollout.size());
  eigenmath::VectorXd jerk_est(rollout.size());

  for (size_t i = 0; i < rollout.size(); i++) {
    const auto& y = rollout[i];
    // Filter iteration.
    filter.Predict(x0);
    SingleJointState x_est = filter.Correct(y);

    // Append data.
    pos_ground_truth(i) = y.position;
    vel_ground_truth(i) = y.velocity;
    acc_ground_truth(i) = y.acceleration;
    jerk_ground_truth(i) = y.jerk;
    pos_est(i) = x_est.position;
    vel_est(i) = x_est.velocity;
    acc_est(i) = x_est.acceleration;
    jerk_est(i) = x_est.jerk;
  }

  eigenmath::MatrixToFile(eigenmath::EigenFileExport::CSVFormat(),
                          "/tmp/pos_ground_truth.csv", pos_ground_truth);
  eigenmath::MatrixToFile(eigenmath::EigenFileExport::CSVFormat(),
                          "/tmp/vel_ground_truth.csv", vel_ground_truth);
  eigenmath::MatrixToFile(eigenmath::EigenFileExport::CSVFormat(),
                          "/tmp/acc_ground_truth.csv", acc_ground_truth);
  eigenmath::MatrixToFile(eigenmath::EigenFileExport::CSVFormat(),
                          "/tmp/jerk_ground_truth.csv", jerk_ground_truth);
  eigenmath::MatrixToFile(eigenmath::EigenFileExport::CSVFormat(),
                          "/tmp/pos_est.csv", pos_est);
  eigenmath::MatrixToFile(eigenmath::EigenFileExport::CSVFormat(),
                          "/tmp/vel_est.csv", vel_est);
  eigenmath::MatrixToFile(eigenmath::EigenFileExport::CSVFormat(),
                          "/tmp/acc_est.csv", acc_est);
  eigenmath::MatrixToFile(eigenmath::EigenFileExport::CSVFormat(),
                          "/tmp/jerk_est.csv", jerk_est);

  return absl::OkStatus();
}

}  // namespace intrinsic::icon

int main(int argc, char** argv) {
  InitIntrinsic(argv[0], argc, argv);
  CHECK_OK(intrinsic::icon::RunFilterAndStoreData());
  return 0;
}
