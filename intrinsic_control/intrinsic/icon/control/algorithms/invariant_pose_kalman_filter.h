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

#ifndef INTRINSIC_ICON_CONTROL_ALGORITHMS_INVARIANT_POSE_KALMAN_FILTER_H_
#define INTRINSIC_ICON_CONTROL_ALGORITHMS_INVARIANT_POSE_KALMAN_FILTER_H_

#include <optional>

#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/twist.h"

namespace intrinsic::icon {

// Left-Invariant Extended Kalman Filter (LI-EKF) for Pose and Twist estimation.
//
// This filter estimates the 3D pose (Position and Orientation) and the 6DOF
// twist (Linear and Angular Velocity) of a rigid body.
//
// State representation:
// - pose: T in SE(3)
// - twist: xi in se(3) (linear and angular velocity in the body frame)
//
// The model assumes constant velocity with zero-mean Gaussian noise on
// the linear and angular accelerations.
class InvariantPoseKalmanFilter {
 public:
  struct NoiseConfig {
    double process_noise_sigma_acc;      // Linear acceleration (m/s^2)
    double process_noise_sigma_ang_acc;  // Angular acceleration (rad/s^2)
    double measurement_noise_sigma_pos;  // Position (m)
    double measurement_noise_sigma_ang;  // Orientation (rad)
  };

  // Configures the rejection of measurement outliers for the `Update` step.
  // Gating protects the filter from state divergence by validating the
  // innovation vector against statistical confidence boundaries (based on
  // chi-squared distribution) using Mahalanobis distance.
  enum class GatingMode {
    kDisabled,       // Bypasses the validation gate.
    kConfidence95,   // Rejects measurements falling in the outer 5% tail of the
                     // chi-squared distribution.
    kConfidence99,   // Rejects measurements falling in the outer 1% tail of the
                     // chi-squared distribution.
    kConfidence99_9  // Rejects measurements falling in the outer 0.1% tail of
                     // the chi-squared distribution.
  };

  // Holds the status and diagnostic of the gating check for the rejection of
  // outliers.
  struct GatingResult {
    // True if the measurement failed the validation gate and was rejected.
    bool was_outlier = false;

    // The squared Mahalanobis distance of the innovation vector (residual).
    double mahalanobis_distance_squared = 0.0;
  };

  // Dimensions for the error-state (12D) and measurement (6D).
  static constexpr int DIM = 12;
  static constexpr int M_DIM = 6;

  using VectorState = eigenmath::Vector<double, DIM>;
  using MatrixState = eigenmath::Matrix<double, DIM, DIM>;
  using MatrixMeas = eigenmath::Matrix<double, M_DIM, DIM>;

  InvariantPoseKalmanFilter() = default;

  // Initializes the filter state and covariance.
  // If the filter is already initialized, it will be reset with the new values.
  // All sigma values in noise_config must be positive.
  // P_initial must be symmetric and positive semi-definite.
  RealtimeStatus Initialize(const Pose3d& pose, const Twist& twist,
                            const NoiseConfig& noise_config,
                            const MatrixState& P_initial);

  // Propagates the state and covariance forward in time by dt.
  // dt must be positive.
  RealtimeStatus Predict(double dt);

  // Updates the state using a new pose measurement.
  RealtimeStatus Update(const Pose3d& pose_measured,
                        const GatingMode& gating_mode = GatingMode::kDisabled);

  // Returns true if the filter has been initialized.
  bool IsInitialized() const { return initialized_; }

  // Accessors for the current estimate.
  const Pose3d& pose() const { return pose_; }
  const Twist& twist() const { return twist_; }
  const MatrixState& covariance() const { return P_; }

  // Accessor for the last gating result.
  const GatingResult& gating_result() const { return gating_result_; }

 private:
  // Computes the discrete process noise matrix Q for a given time step.
  MatrixState CalculateQ(double dt);

  bool initialized_ = false;

  Pose3d pose_;
  Twist twist_;
  MatrixState P_;

  NoiseConfig noise_config_;
  eigenmath::Matrix6d R_;
  GatingResult gating_result_;

  // Cached values for process noise calculation.
  std::optional<double> last_dt_ = std::nullopt;
  std::optional<MatrixState> cached_Q_ = std::nullopt;
};
}  // namespace intrinsic::icon

#endif  // INTRINSIC_ICON_CONTROL_ALGORITHMS_INVARIANT_POSE_KALMAN_FILTER_H_
