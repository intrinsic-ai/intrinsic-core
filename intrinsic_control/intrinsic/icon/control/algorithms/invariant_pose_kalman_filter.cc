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

#include "intrinsic/icon/control/algorithms/invariant_pose_kalman_filter.h"

#include <Eigen/Eigenvalues>
#include <cmath>
#include <limits>

#include "intrinsic/eigenmath/manifolds.h"
#include "intrinsic/eigenmath/pose3_utils.h"
#include "intrinsic/icon/utils/log.h"
#include "intrinsic/icon/utils/realtime_status_macro.h"
#include "intrinsic/icon/utils/realtime_status_or.h"
#include "intrinsic/math/ipow.h"
#include "intrinsic/math/pose3.h"

namespace intrinsic::icon {

namespace {
constexpr double kDtEpsilonQCacheSeconds = 1.0e-5;
constexpr double kSymmetryTolerance = 1.0e-9;
constexpr double kEpsilon = 1.0e-12;

// Returns the chi-square distribution threshold for the confidence level in
// the gating `mode`. Thresholds are already given for 6 DoF.
RealtimeStatusOr<double> ChiSquareThreshold(
    InvariantPoseKalmanFilter::GatingMode mode) {
  switch (mode) {
    case InvariantPoseKalmanFilter::GatingMode::kDisabled:
      return std::numeric_limits<double>::infinity();
    case InvariantPoseKalmanFilter::GatingMode::kConfidence95:
      return 12.59;
    case InvariantPoseKalmanFilter::GatingMode::kConfidence99:
      return 16.81;
    case InvariantPoseKalmanFilter::GatingMode::kConfidence99_9:
      return 22.46;
    default:
      return InvalidArgumentError("Unknown gating mode!");
  }
}

}  // namespace

RealtimeStatus InvariantPoseKalmanFilter::Initialize(
    const Pose3d& pose, const Twist& twist, const NoiseConfig& noise_config,
    const MatrixState& P_initial) {
  if (noise_config.process_noise_sigma_acc <= 0.0 ||
      noise_config.process_noise_sigma_ang_acc <= 0.0 ||
      noise_config.measurement_noise_sigma_pos <= 0.0 ||
      noise_config.measurement_noise_sigma_ang <= 0.0) {
    return icon::InvalidArgumentError(
        "All noise standard deviation values must be positive.");
  }

  // Check that P_initial is symmetric.
  if (!P_initial.isApprox(P_initial.transpose(), kSymmetryTolerance)) {
    return icon::InvalidArgumentError("P_initial is not symmetric.");
  }

  // Check that P_initial is positive semi-definite.
  const Eigen::SelfAdjointEigenSolver<MatrixState> solver(P_initial);
  if (solver.eigenvalues().minCoeff() <= kEpsilon) {
    return icon::InvalidArgumentError("P_initial is not positive definite.");
  }

  pose_ = pose;
  twist_ = twist;
  noise_config_ = noise_config;

  R_.setZero();
  R_.block<3, 3>(0, 0) =
      eigenmath::Matrix3d::Identity() *
      intrinsic::IPow(noise_config_.measurement_noise_sigma_pos, 2);
  R_.block<3, 3>(3, 3) =
      eigenmath::Matrix3d::Identity() *
      intrinsic::IPow(noise_config_.measurement_noise_sigma_ang, 2);

  P_ = P_initial;

  initialized_ = true;
  return icon::OkStatus();
}

InvariantPoseKalmanFilter::MatrixState InvariantPoseKalmanFilter::CalculateQ(
    double dt) {
  if (last_dt_.has_value() && cached_Q_.has_value() &&
      std::abs(dt - *last_dt_) < kDtEpsilonQCacheSeconds) {
    return *cached_Q_;
  }

  MatrixState Q = MatrixState::Zero();

  const double dt2 = dt * dt;
  const double dt3 = dt2 * dt;
  const double dt4 = dt2 * dt2;
  const double var_acc =
      intrinsic::IPow(noise_config_.process_noise_sigma_acc, 2);
  const double var_ang_acc =
      intrinsic::IPow(noise_config_.process_noise_sigma_ang_acc, 2);

  const eigenmath::Matrix3d kIdentity = eigenmath::Matrix3d::Identity();

  // Discrete-time process noise for random constant acceleration.
  Q.block<3, 3>(0, 0) = (1.0 / 4.0) * dt4 * var_acc * kIdentity;
  Q.block<3, 3>(3, 3) = (1.0 / 4.0) * dt4 * var_ang_acc * kIdentity;
  Q.block<3, 3>(6, 6) = dt2 * var_acc * kIdentity;
  Q.block<3, 3>(9, 9) = dt2 * var_ang_acc * kIdentity;

  Q.block<3, 3>(0, 6) = (1.0 / 2.0) * dt3 * var_acc * kIdentity;
  Q.block<3, 3>(3, 9) = (1.0 / 2.0) * dt3 * var_ang_acc * kIdentity;
  Q.block<6, 6>(6, 0) = Q.block<6, 6>(0, 6).transpose();

  last_dt_ = dt;
  cached_Q_ = Q;

  return Q;
}

RealtimeStatus InvariantPoseKalmanFilter::Predict(double dt) {
  if (!initialized_) {
    return icon::FailedPreconditionError("Filter not initialized");
  }
  if (dt <= 0.0) {
    return icon::InvalidArgumentError("dt must be positive");
  }

  // Nominal State Prediction: T_next = T * exp(twist * dt)
  const Pose3d U = eigenmath::expRiemann(eigenmath::Vector6d(twist_ * dt));
  pose_ *= U;

  // Covariance propagation: P = F * P * F' + Q
  // zeta_{k+1} = Ad_{U^-1} * zeta_k + delta_xi_k * dt
  MatrixState F = MatrixState::Identity();
  F.block<6, 6>(0, 0) = eigenmath::AdjointSE3(U.inverse());
  F.block<6, 6>(0, 6) = eigenmath::Matrix6d::Identity() * dt;

  const MatrixState Q = CalculateQ(dt);
  P_ = F * P_ * F.transpose() + Q;

  return icon::OkStatus();
}

RealtimeStatus InvariantPoseKalmanFilter::Update(
    const Pose3d& pose_measured, const GatingMode& gating_mode) {
  if (!initialized_) {
    return icon::FailedPreconditionError("Filter not initialized");
  }

  // Innovation vector y. Residual is in tangent space:
  // y = log(pose.inverse() * pose_measured).
  // This corresponds to zeta if pose_measured = pose_nominal * exp(zeta)
  const eigenmath::Vector6d y =
      eigenmath::logRiemann(pose_.inverse() * pose_measured);

  // Innovation covariance S, which requires inversion. We use a linear solver
  // for better numerical stability and efficiency.
  // Jacobian H = [ I_6  0_6 ].
  // S = H * P_ * H^T + R_ = P_block<6, 6>(0, 0) + R_
  const eigenmath::Matrix6d S = P_.block<6, 6>(0, 0) + R_;
  const Eigen::LDLT<eigenmath::Matrix6d::PlainObject> S_ldlt = S.ldlt();

  // Gating: reject measurement outliers based on Mahalanobis distance.
  const double mahalanobis_distance_squared = S_ldlt.solve(y).dot(y);
  INTRINSIC_RT_ASSIGN_OR_RETURN(const double chi_squared_threshold,
                                ChiSquareThreshold(gating_mode));
  const bool is_outlier = mahalanobis_distance_squared > chi_squared_threshold;
  gating_result_.mahalanobis_distance_squared = mahalanobis_distance_squared;
  gating_result_.was_outlier = is_outlier;
  if (gating_result_.was_outlier) {
    INTRINSIC_RT_LOG(WARNING) << "Outlier rejected.";
    return icon::OkStatus();
  }

  // Kalman Update.
  // Kalmain Gain K = P_ * H^T * S^-1.
  const eigenmath::Matrix<double, 12, 6> K =
      (S.ldlt().solve(P_.topRows<6>())).transpose();
  const VectorState delta_x = K * y;

  // Covariance update using the Joseph form for numerical stability.
  // I - KH = I - [K 0]
  MatrixState I_KH = MatrixState::Identity();
  I_KH.leftCols<6>() -= K;
  P_ = I_KH * P_ * I_KH.transpose() + K * R_ * K.transpose();

  // Inject error into nominal state.
  // zeta = delta_x.head<6>()
  // delta_xi = delta_x.tail<6>()
  // T_hat_new = T_hat * exp(zeta)
  pose_ *= eigenmath::expRiemann(eigenmath::Vector6d(delta_x.head<6>()));
  twist_ += Twist(delta_x.tail<6>());

  return icon::OkStatus();
}

}  // namespace intrinsic::icon
