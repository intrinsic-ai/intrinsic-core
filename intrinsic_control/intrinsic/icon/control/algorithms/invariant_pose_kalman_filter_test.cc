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

#include <gmock/gmock.h>

#include <Eigen/Eigenvalues>
#include <cmath>
#include <limits>
#include <string>

#include "absl/status/status_matchers.h"
#include "gmock/gmock.h"
#include "internal/testing.h"
#include "intrinsic/eigenmath/manifolds.h"
#include "intrinsic/eigenmath/matchers.h"
#include "intrinsic/icon/testing/malloc_test.h"
#include "intrinsic/icon/utils/realtime_status_macro.h"
#include "intrinsic/icon/utils/realtime_status_matchers.h"

namespace intrinsic::icon {
namespace {

using ::intrinsic::eigenmath::testing::IsApprox;
using ::testing::Gt;
using ::testing::HasSubstr;
using ::testing::Not;

constexpr double kNumericTolerance = 1.0e-12;

TEST(InvariantPoseKalmanFilterTest, InitializeAndPredict) {
  InvariantPoseKalmanFilter filter;
  EXPECT_FALSE(filter.IsInitialized());

  Pose3d initial_pose = Pose3d::Identity();
  // v_x = 1, omega_z = 0.1
  Twist initial_twist(1.0, 0.0, 0.0, 0.0, 0.0, 0.1);

  InvariantPoseKalmanFilter::NoiseConfig noise_config;
  noise_config.process_noise_sigma_acc = 0.01;
  noise_config.process_noise_sigma_ang_acc = 0.01;
  noise_config.measurement_noise_sigma_pos = 0.01;
  noise_config.measurement_noise_sigma_ang = 0.01;

  InvariantPoseKalmanFilter::MatrixState P_initial =
      InvariantPoseKalmanFilter::MatrixState::Identity() * 1e-3;

  INTRINSIC_RT_EXPECT_OK(
      filter.Initialize(initial_pose, initial_twist, noise_config, P_initial));
  EXPECT_TRUE(filter.IsInitialized());

  EXPECT_THAT(filter.pose(), IsApprox(initial_pose));
  EXPECT_THAT(filter.twist(), IsApprox(initial_twist));
  EXPECT_THAT(filter.covariance(), IsApprox(P_initial));

  double dt = 0.1;
  INTRINSIC_RT_EXPECT_OK(filter.Predict(dt));

  // T_next = T * exp(xi * dt)
  Pose3d expected_pose =
      initial_pose * eigenmath::expRiemann((initial_twist * dt).eval());
  EXPECT_THAT(filter.pose(), IsApprox(expected_pose));

  // Re-initialization should succeed and reset the state
  INTRINSIC_RT_EXPECT_OK(
      filter.Initialize(initial_pose, initial_twist, noise_config, P_initial));
  EXPECT_THAT(filter.pose(), IsApprox(initial_pose));
  EXPECT_THAT(filter.twist(), IsApprox(initial_twist));
  EXPECT_THAT(filter.covariance(), IsApprox(P_initial));
}

TEST(InvariantPoseKalmanFilterTest, InvalidPInitial) {
  InvariantPoseKalmanFilter filter;
  InvariantPoseKalmanFilter::NoiseConfig noise_config;
  noise_config.process_noise_sigma_acc = 0.01;
  noise_config.process_noise_sigma_ang_acc = 0.01;
  noise_config.measurement_noise_sigma_pos = 0.01;
  noise_config.measurement_noise_sigma_ang = 0.01;

  // Not symmetric
  InvariantPoseKalmanFilter::MatrixState P_not_symmetric =
      InvariantPoseKalmanFilter::MatrixState::Identity() * 1e-3;
  P_not_symmetric(0, 1) = 1.0;
  EXPECT_THAT(filter.Initialize(Pose3d::Identity(), Twist::ZERO, noise_config,
                                P_not_symmetric),
              icon::RealtimeStatusIs(absl::StatusCode::kInvalidArgument,
                                     HasSubstr("not symmetric")));

  // Not positive semi-definite
  InvariantPoseKalmanFilter::MatrixState P_not_psd =
      InvariantPoseKalmanFilter::MatrixState::Identity() * 1e-3;
  P_not_psd(0, 0) = -1.0;
  EXPECT_THAT(filter.Initialize(Pose3d::Identity(), Twist::ZERO, noise_config,
                                P_not_psd),
              icon::RealtimeStatusIs(absl::StatusCode::kInvalidArgument,
                                     HasSubstr("not positive definite")));
}

TEST(InvariantPoseKalmanFilterTest, InvalidNoiseConfig) {
  InvariantPoseKalmanFilter filter;
  InvariantPoseKalmanFilter::NoiseConfig noise_config;
  noise_config.process_noise_sigma_acc = 0.1;
  noise_config.process_noise_sigma_ang_acc = 0.1;
  noise_config.measurement_noise_sigma_pos = 0.01;
  noise_config.measurement_noise_sigma_ang = 0.01;

  InvariantPoseKalmanFilter::MatrixState P_initial =
      InvariantPoseKalmanFilter::MatrixState::Identity() * 1e-3;

  EXPECT_TRUE(
      filter
          .Initialize(Pose3d::Identity(), Twist::ZERO, noise_config, P_initial)
          .ok());

  noise_config.measurement_noise_sigma_ang = 0.0;
  EXPECT_FALSE(
      filter
          .Initialize(Pose3d::Identity(), Twist::ZERO, noise_config, P_initial)
          .ok());

  noise_config.measurement_noise_sigma_ang = -0.1;
  EXPECT_FALSE(
      filter
          .Initialize(Pose3d::Identity(), Twist::ZERO, noise_config, P_initial)
          .ok());
}

TEST(InvariantPoseKalmanFilterTest, InvalidDtInPredict) {
  InvariantPoseKalmanFilter filter;
  InvariantPoseKalmanFilter::NoiseConfig noise_config;
  noise_config.process_noise_sigma_acc = 0.1;
  noise_config.process_noise_sigma_ang_acc = 0.1;
  noise_config.measurement_noise_sigma_pos = 0.01;
  noise_config.measurement_noise_sigma_ang = 0.01;

  // Uninitialized calls should fail
  InvariantPoseKalmanFilter::MatrixState P_initial =
      InvariantPoseKalmanFilter::MatrixState::Identity() * 1e-3;
  INTRINSIC_RT_EXPECT_OK(filter.Initialize(Pose3d::Identity(), Twist::ZERO,
                                           noise_config, P_initial));

  INTRINSIC_RT_EXPECT_OK(filter.Predict(0.1));

  EXPECT_THAT(filter.Predict(0.0),
              icon::RealtimeStatusIs(absl::StatusCode::kInvalidArgument,
                                     HasSubstr("dt must be positive")));
  EXPECT_THAT(filter.Predict(-0.1),
              icon::RealtimeStatusIs(absl::StatusCode::kInvalidArgument,
                                     HasSubstr("dt must be positive")));
}

TEST(InvariantPoseKalmanFilterTest, LargeRotationConvergence) {
  InvariantPoseKalmanFilter filter;
  // Start at -170 degrees z-rotation
  Pose3d initial_pose(eigenmath::Quaterniond(eigenmath::AngleAxisd(
      -170.0 * M_PI / 180.0, eigenmath::Vector3d::UnitZ())));
  InvariantPoseKalmanFilter::NoiseConfig noise_config;
  noise_config.process_noise_sigma_acc = 0.1;
  noise_config.process_noise_sigma_ang_acc = 0.1;
  noise_config.measurement_noise_sigma_pos = 0.01;
  noise_config.measurement_noise_sigma_ang = 0.01;

  InvariantPoseKalmanFilter::MatrixState P_initial =
      InvariantPoseKalmanFilter::MatrixState::Identity() * 1e-3;
  INTRINSIC_RT_EXPECT_OK(
      filter.Initialize(initial_pose, Twist::ZERO, noise_config, P_initial));

  // Measure +170 degrees (total 340 deg or -20 deg difference)
  Pose3d measured_pose(eigenmath::Quaterniond(eigenmath::AngleAxisd(
      170.0 * M_PI / 180.0, eigenmath::Vector3d::UnitZ())));

  for (int i = 0; i < 50; ++i) {
    INTRINSIC_RT_EXPECT_OK(filter.Predict(0.01));
    INTRINSIC_RT_EXPECT_OK(filter.Update(measured_pose));
  }

  EXPECT_THAT(filter.pose(), IsApprox(measured_pose, 1e-2));
}

TEST(InvariantPoseKalmanFilterTest, UpdateCorrectsState) {
  InvariantPoseKalmanFilter filter;
  Pose3d initial_pose = Pose3d::Identity();
  Twist twist = Twist::ZERO;

  InvariantPoseKalmanFilter::NoiseConfig noise_config;
  noise_config.process_noise_sigma_acc = 0.1;
  noise_config.process_noise_sigma_ang_acc = 0.1;
  noise_config.measurement_noise_sigma_pos = 0.01;
  noise_config.measurement_noise_sigma_ang = 0.01;

  InvariantPoseKalmanFilter::MatrixState P_initial =
      InvariantPoseKalmanFilter::MatrixState::Identity() * 1e-3;
  INTRINSIC_RT_EXPECT_OK(
      filter.Initialize(initial_pose, twist, noise_config, P_initial));

  // Offset measurement (position only for simplicity)
  Pose3d measured_pose(eigenmath::Vector3d(0.1, 0.2, 0.3));
  const double dt = 0.1;

  // Multiple updates to converge (since measurement noise is non-zero)
  for (int i = 0; i < 20; ++i) {
    INTRINSIC_RT_EXPECT_OK(filter.Predict(dt));
    INTRINSIC_RT_EXPECT_OK(filter.Update(measured_pose));
  }

  EXPECT_THAT(filter.pose(), IsApprox(measured_pose, 1e-2));
}

TEST(InvariantPoseKalmanFilterTest, OrientationUpdateCorrectsState) {
  InvariantPoseKalmanFilter filter;
  Pose3d initial_pose = Pose3d::Identity();
  Twist twist = Twist::ZERO;

  InvariantPoseKalmanFilter::NoiseConfig noise_config;
  noise_config.process_noise_sigma_acc = 0.1;
  noise_config.process_noise_sigma_ang_acc = 0.1;
  noise_config.measurement_noise_sigma_pos = 0.01;
  noise_config.measurement_noise_sigma_ang = 0.01;

  InvariantPoseKalmanFilter::MatrixState P_initial =
      InvariantPoseKalmanFilter::MatrixState::Identity() * 1e-3;
  INTRINSIC_RT_EXPECT_OK(
      filter.Initialize(initial_pose, twist, noise_config, P_initial));

  // Offset measurement (orientation only)
  Pose3d measured_pose(eigenmath::Quaterniond(0.707, 0.0, 0.0, 0.707),
                       eigenmath::Vector3d::Zero());
  const double dt = 0.1;

  // Multiple updates to converge
  for (int i = 0; i < 20; ++i) {
    INTRINSIC_RT_EXPECT_OK(filter.Predict(dt));
    INTRINSIC_RT_EXPECT_OK(filter.Update(measured_pose));
  }

  EXPECT_THAT(filter.pose(), IsApprox(measured_pose, 1e-2));
}

TEST(InvariantPoseKalmanFilterTest, ConstantAngularVelocityTracking) {
  InvariantPoseKalmanFilter filter;
  Pose3d initial_pose = Pose3d::Identity();
  Twist initial_twist = Twist::ZERO;

  InvariantPoseKalmanFilter::NoiseConfig noise_config;
  noise_config.process_noise_sigma_acc = 0.1;
  noise_config.process_noise_sigma_ang_acc = 0.1;
  noise_config.measurement_noise_sigma_pos = 0.01;
  noise_config.measurement_noise_sigma_ang = 0.01;

  InvariantPoseKalmanFilter::MatrixState P_initial =
      InvariantPoseKalmanFilter::MatrixState::Identity() * 1e-3;
  INTRINSIC_RT_EXPECT_OK(
      filter.Initialize(initial_pose, initial_twist, noise_config, P_initial));

  Twist true_twist(0.0, 0.0, 0.0, 0.0, 0.0, 1.0);  // 1 rad/s in z
  Pose3d true_pose = initial_pose;
  double dt = 0.01;

  for (int i = 0; i < 200; ++i) {
    true_pose = true_pose * eigenmath::expRiemann((true_twist * dt).eval());
    INTRINSIC_RT_EXPECT_OK(filter.Predict(dt));
    INTRINSIC_RT_EXPECT_OK(filter.Update(true_pose));
  }

  // Filter should have estimated the velocity correctly
  EXPECT_THAT(filter.twist(), IsApprox(true_twist, 1e-1));
  EXPECT_THAT(filter.pose(), IsApprox(true_pose, 1e-2));

  // Validate covariance properties
  EXPECT_THAT(filter.covariance(), IsApprox(filter.covariance().transpose()));
  EXPECT_TRUE((filter.covariance().diagonal().array() > 0.0).all());
}

TEST(InvariantPoseKalmanFilterTest, HelicalTrajectoryTracking) {
  InvariantPoseKalmanFilter filter;
  Pose3d initial_pose = Pose3d::Identity();
  Twist initial_twist = Twist::ZERO;

  InvariantPoseKalmanFilter::NoiseConfig noise_config;
  noise_config.process_noise_sigma_acc = 0.1;
  noise_config.process_noise_sigma_ang_acc = 0.1;
  noise_config.measurement_noise_sigma_pos = 0.01;
  noise_config.measurement_noise_sigma_ang = 0.01;

  InvariantPoseKalmanFilter::MatrixState P_initial =
      InvariantPoseKalmanFilter::MatrixState::Identity() * 1e-3;
  INTRINSIC_RT_EXPECT_OK(
      filter.Initialize(initial_pose, initial_twist, noise_config, P_initial));

  // v_x = 1.0, omega_z = 0.5
  Twist true_twist(1.0, 0.0, 0.0, 0.0, 0.0, 0.5);
  Pose3d true_pose = initial_pose;
  double dt = 0.01;

  for (int i = 0; i < 500; ++i) {
    true_pose = true_pose * eigenmath::expRiemann((true_twist * dt).eval());
    INTRINSIC_RT_EXPECT_OK(filter.Predict(dt));
    INTRINSIC_RT_EXPECT_OK(filter.Update(true_pose));
  }

  // Filter should have estimated the velocity correctly
  EXPECT_THAT(filter.twist(), IsApprox(true_twist, 1e-1));
  EXPECT_THAT(filter.pose(), IsApprox(true_pose, 1e-2));

  // Validate covariance properties
  EXPECT_THAT(filter.covariance(), IsApprox(filter.covariance().transpose()));
  EXPECT_TRUE((filter.covariance().diagonal().array() > 0.0).all());
}

TEST(InvariantPoseKalmanFilterTest, AcceleratingRotationTracking) {
  InvariantPoseKalmanFilter filter;
  Pose3d initial_pose = Pose3d::Identity();
  Twist initial_twist = Twist::ZERO;

  InvariantPoseKalmanFilter::NoiseConfig noise_config;
  noise_config.process_noise_sigma_acc = 0.1;
  noise_config.process_noise_sigma_ang_acc = 1.0;  // High angular acc noise
  noise_config.measurement_noise_sigma_pos = 0.01;
  noise_config.measurement_noise_sigma_ang = 0.01;

  InvariantPoseKalmanFilter::MatrixState P_initial =
      InvariantPoseKalmanFilter::MatrixState::Identity() * 1e-3;
  INTRINSIC_RT_EXPECT_OK(
      filter.Initialize(initial_pose, initial_twist, noise_config, P_initial));

  double angular_acc = 0.5;  // rad/s^2
  Twist current_twist = initial_twist;
  Pose3d true_pose = initial_pose;
  double dt = 0.01;

  for (int i = 0; i < 200; ++i) {
    current_twist.RZ() += angular_acc * dt;
    true_pose = true_pose * eigenmath::expRiemann((current_twist * dt).eval());
    INTRINSIC_RT_EXPECT_OK(filter.Predict(dt));
    INTRINSIC_RT_EXPECT_OK(filter.Update(true_pose));
  }

  // Filter should be able to track the changing velocity
  EXPECT_THAT(filter.twist(), IsApprox(current_twist, 0.2));
  EXPECT_THAT(filter.pose(), IsApprox(true_pose, 0.05));

  // Validate covariance properties
  EXPECT_THAT(filter.covariance(), IsApprox(filter.covariance().transpose()));
  EXPECT_TRUE((filter.covariance().diagonal().array() > 0.0).all());
}

TEST(InvariantPoseKalmanFilterTest, OrientationMeasurementNoiseImpact) {
  auto run_test = [](double measurement_noise) {
    InvariantPoseKalmanFilter filter;
    Pose3d initial_pose = Pose3d::Identity();
    Twist twist = Twist::ZERO;

    InvariantPoseKalmanFilter::NoiseConfig noise_config;
    noise_config.process_noise_sigma_acc = 0.1;
    noise_config.process_noise_sigma_ang_acc = 0.1;
    noise_config.measurement_noise_sigma_pos = 0.01;
    noise_config.measurement_noise_sigma_ang = measurement_noise;

    InvariantPoseKalmanFilter::MatrixState P_initial =
        InvariantPoseKalmanFilter::MatrixState::Identity() * 1e-3;
    INTRINSIC_RT_EXPECT_OK(
        filter.Initialize(initial_pose, twist, noise_config, P_initial));

    Pose3d measured_pose(eigenmath::Quaterniond(0.707, 0.0, 0.0, 0.707),
                         eigenmath::Vector3d::Zero());
    const double dt = 0.1;

    for (int i = 0; i < 10; ++i) {
      INTRINSIC_RT_EXPECT_OK(filter.Predict(dt));
      INTRINSIC_RT_EXPECT_OK(filter.Update(measured_pose));
    }
    return filter.pose();
  };

  Pose3d low_noise_pose = run_test(0.01);
  Pose3d high_noise_pose = run_test(1.0);

  Pose3d target_pose(eigenmath::Quaterniond(0.707, 0.0, 0.0, 0.707),
                     eigenmath::Vector3d::Zero());

  double low_noise_err =
      eigenmath::logRiemann(low_noise_pose.inverse() * target_pose).norm();
  double high_noise_err =
      eigenmath::logRiemann(high_noise_pose.inverse() * target_pose).norm();

  // Low noise should converge much faster
  EXPECT_LT(low_noise_err, high_noise_err);
}

TEST(InvariantPoseKalmanFilterTest, ConstantVelocityTracking) {
  InvariantPoseKalmanFilter filter;
  Pose3d initial_pose = Pose3d::Identity();
  Twist initial_twist = Twist::ZERO;

  InvariantPoseKalmanFilter::NoiseConfig noise_config;
  noise_config.process_noise_sigma_acc = 0.1;
  noise_config.process_noise_sigma_ang_acc = 0.1;
  noise_config.measurement_noise_sigma_pos = 0.01;
  noise_config.measurement_noise_sigma_ang = 0.01;

  InvariantPoseKalmanFilter::MatrixState P_initial =
      InvariantPoseKalmanFilter::MatrixState::Identity() * 1e-3;
  INTRINSIC_RT_EXPECT_OK(
      filter.Initialize(initial_pose, initial_twist, noise_config, P_initial));

  Twist true_twist(1.0, 0.0, 0.0, 0.0, 0.0, 0.0);  // 1 m/s in x
  Pose3d true_pose = initial_pose;
  double dt = 0.01;

  for (int i = 0; i < 100; ++i) {
    true_pose = true_pose * eigenmath::expRiemann((true_twist * dt).eval());
    INTRINSIC_RT_EXPECT_OK(filter.Predict(dt));
    INTRINSIC_RT_EXPECT_OK(filter.Update(true_pose));
  }

  // Filter should have estimated the velocity correctly
  EXPECT_THAT(filter.twist(), IsApprox(true_twist, 1e-1));
  EXPECT_THAT(filter.pose(), IsApprox(true_pose, 1e-2));

  // Validate covariance properties (symmetric and positive definite)
  EXPECT_THAT(filter.covariance(), IsApprox(filter.covariance().transpose()));
  const Eigen::SelfAdjointEigenSolver<InvariantPoseKalmanFilter::MatrixState>
      solver(P_initial);
  EXPECT_THAT(solver.eigenvalues().minCoeff(), Gt(0));
}

TEST(InvariantPoseKalmanFilterMallocTest, NoAllocationsInPredictAndUpdate) {
  InvariantPoseKalmanFilter filter;
  Pose3d initial_pose = Pose3d::Identity();
  Twist initial_twist = Twist::ZERO;

  InvariantPoseKalmanFilter::NoiseConfig noise_config;
  noise_config.process_noise_sigma_acc = 0.1;
  noise_config.process_noise_sigma_ang_acc = 0.1;
  noise_config.measurement_noise_sigma_pos = 0.01;
  noise_config.measurement_noise_sigma_ang = 0.01;

  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  InvariantPoseKalmanFilter::MatrixState P_initial =
      InvariantPoseKalmanFilter::MatrixState::Identity() * 1e-3;
  INTRINSIC_RT_EXPECT_OK(
      filter.Initialize(initial_pose, initial_twist, noise_config, P_initial));
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();

  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  INTRINSIC_RT_EXPECT_OK(filter.Predict(0.01));
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();

  Pose3d measurement = Pose3d::Identity();
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  INTRINSIC_RT_EXPECT_OK(filter.Update(measurement));
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
}

struct GatingTestParams {
  std::string test_name;
  InvariantPoseKalmanFilter::GatingMode gating_mode;
  double expected_chi_squared_threshold;
};

class InvariantPoseKalmanFilterGatingTest
    : public ::testing::TestWithParam<GatingTestParams> {
 protected:
  void SetUp() override {
    initial_pose_ = Pose3d::Identity();
    initial_twist_ = Twist::ZERO;

    noise_config_.process_noise_sigma_acc = 0.1;
    noise_config_.process_noise_sigma_ang_acc = 0.1;
    noise_config_.measurement_noise_sigma_pos = 0.01;
    noise_config_.measurement_noise_sigma_ang = 0.01;
    P_initial_ = InvariantPoseKalmanFilter::MatrixState::Identity() * 1e-3;
    ASSERT_OK(filter_.Initialize(initial_pose_, initial_twist_, noise_config_,
                                 P_initial_));
  }

  // Computes the Mahalanobis distance squared of a given measurement pose
  // relative to the filter's current state and covariance.
  icon::RealtimeStatusOr<double> GetMahalanobisDistanceSquared(
      const Pose3d& pose_measured) {
    // Use a second filter to get the Mahalanobis squared distance for
    // `pose_measured`, without updating the internal `filter_`.
    InvariantPoseKalmanFilter filter;
    INTRINSIC_RT_RETURN_IF_ERROR(filter.Initialize(
        filter_.pose(), filter_.twist(), noise_config_, filter_.covariance()));
    INTRINSIC_RT_RETURN_IF_ERROR(filter.Update(pose_measured));

    return filter.gating_result().mahalanobis_distance_squared;
  }

  // Generates a measurement pose that has a specific target Mahalanobis
  // distance squared relative to the filter's current state and covariance.
  RealtimeStatusOr<Pose3d> GenerateMeasurementForMahalanobisDistance(
      double target_mahalanobis_dist_sq) {
    // Create a temporary unscaled measurement along some direction.
    eigenmath::Vector6d dir = eigenmath::Vector6d::Ones();
    const Pose3d unscaled_measurement =
        filter_.pose() * eigenmath::expRiemann(dir);
    INTRINSIC_RT_ASSIGN_OR_RETURN(
        const double mahalanobis_dist_sq,
        GetMahalanobisDistanceSquared(unscaled_measurement));

    // Scale the direction to achieve the target distance.
    double alpha = std::sqrt(target_mahalanobis_dist_sq / mahalanobis_dist_sq);
    eigenmath::Vector6d y = eigenmath::Vector6d::Constant(alpha);
    return Pose3d(filter_.pose() * eigenmath::expRiemann(y));
  }

  InvariantPoseKalmanFilter filter_;
  Pose3d initial_pose_;
  Twist initial_twist_;
  InvariantPoseKalmanFilter::NoiseConfig noise_config_;
  InvariantPoseKalmanFilter::MatrixState P_initial_;
};

TEST_F(InvariantPoseKalmanFilterGatingTest, InitializesGatingResult) {
  EXPECT_FALSE(filter_.gating_result().was_outlier);
  EXPECT_NEAR(filter_.gating_result().mahalanobis_distance_squared, 0.0,
              kNumericTolerance);
}

TEST_P(InvariantPoseKalmanFilterGatingTest,
       ComputesCorrectMahalanobisDistance) {
  const Pose3d measured_pose =
      Pose3d(eigenmath::Vector3d(1.0, 0.0, 0.0)) * initial_pose_;
  const double expected_mahalanobis_dist_squared = 10000.0 / 11.0;

  INTRINSIC_RT_EXPECT_OK(filter_.Update(measured_pose, GetParam().gating_mode));

  EXPECT_NEAR(filter_.gating_result().mahalanobis_distance_squared,
              expected_mahalanobis_dist_squared, kNumericTolerance);
}

TEST_P(InvariantPoseKalmanFilterGatingTest, AcceptsMeasurementEqualToState) {
  // A measurement that is identical to the current state estimate should have a
  // Mahalanobis distance of 0, which is within any gate.
  const Pose3d measured_pose = filter_.pose();

  INTRINSIC_RT_EXPECT_OK(filter_.Update(measured_pose, GetParam().gating_mode));
  EXPECT_FALSE(filter_.gating_result().was_outlier);
  EXPECT_NEAR(filter_.gating_result().mahalanobis_distance_squared, 0.0,
              kNumericTolerance);

  // The state should be updated (or in this case, remain close to the
  // measurement).
  EXPECT_THAT(filter_.pose(), IsApprox(measured_pose, kNumericTolerance));
}

TEST_P(InvariantPoseKalmanFilterGatingTest,
       AcceptsMeasurementJustBelowTolerance) {
  const GatingTestParams& params = GetParam();

  // A measurement that is just within the chi-distribution tolerance.
  const double target_chi_squared =
      params.gating_mode == InvariantPoseKalmanFilter::GatingMode::kDisabled
          ? 100.0  // kDisabled mode has an infinite chi-squared threshold.
          : params.expected_chi_squared_threshold - 0.01;
  ASSERT_OK_AND_ASSIGN(
      const Pose3d measured_pose,
      GenerateMeasurementForMahalanobisDistance(target_chi_squared));

  const Pose3d pose_before = filter_.pose();
  INTRINSIC_RT_EXPECT_OK(filter_.Update(measured_pose, params.gating_mode));

  // The measurement should be rejected as an outlier.
  EXPECT_FALSE(filter_.gating_result().was_outlier);
  EXPECT_NEAR(filter_.gating_result().mahalanobis_distance_squared,
              target_chi_squared, kNumericTolerance);

  // Check that the filter pose have been updated.
  EXPECT_THAT(filter_.pose(), Not(IsApprox(pose_before, 1e-2)));
}

TEST_P(InvariantPoseKalmanFilterGatingTest,
       RejectsMeasurementJustAboveTolerance) {
  const GatingTestParams& params = GetParam();
  if (params.gating_mode == InvariantPoseKalmanFilter::GatingMode::kDisabled) {
    // For GatingMode::kDisabled, no measurement is ever rejected.
    return;
  }

  // A measurement that just exceeds the chi-distribution tolerance.
  const double target_chi_squared =
      params.expected_chi_squared_threshold + 0.01;
  ASSERT_OK_AND_ASSIGN(
      const Pose3d measured_pose,
      GenerateMeasurementForMahalanobisDistance(target_chi_squared));

  const Pose3d pose_before = filter_.pose();
  const Twist twist_before = filter_.twist();
  const InvariantPoseKalmanFilter::MatrixState covariance_before =
      filter_.covariance();
  INTRINSIC_RT_EXPECT_OK(filter_.Update(measured_pose, params.gating_mode));

  // The measurement should be rejected as an outlier.
  EXPECT_TRUE(filter_.gating_result().was_outlier);
  EXPECT_NEAR(filter_.gating_result().mahalanobis_distance_squared,
              target_chi_squared, kNumericTolerance);

  // Check that the rejection actually happened: pose_ and twist_ are untouched.
  EXPECT_THAT(filter_.pose(), IsApprox(pose_before, kNumericTolerance));
  EXPECT_THAT(filter_.twist(), IsApprox(twist_before, kNumericTolerance));
  EXPECT_THAT(filter_.covariance(),
              IsApprox(covariance_before, kNumericTolerance));
}

INSTANTIATE_TEST_SUITE_P(
    GatingModes, InvariantPoseKalmanFilterGatingTest,
    ::testing::Values(
        GatingTestParams{
            .test_name = "Disabled",
            .gating_mode = InvariantPoseKalmanFilter::GatingMode::kDisabled,
            .expected_chi_squared_threshold =
                std::numeric_limits<double>::infinity()},
        GatingTestParams{
            .test_name = "Confidence95",
            .gating_mode = InvariantPoseKalmanFilter::GatingMode::kConfidence95,
            .expected_chi_squared_threshold = 12.59},
        GatingTestParams{
            .test_name = "Confidence99",
            .gating_mode = InvariantPoseKalmanFilter::GatingMode::kConfidence99,
            .expected_chi_squared_threshold = 16.81},
        GatingTestParams{
            .test_name = "Confidence99_9",
            .gating_mode =
                InvariantPoseKalmanFilter::GatingMode::kConfidence99_9,
            .expected_chi_squared_threshold = 22.46}),
    [](const ::testing::TestParamInfo<GatingTestParams>& info) {
      return info.param.test_name;
    });

TEST_F(InvariantPoseKalmanFilterGatingTest,
       AcceptsAnyMeasurementWithDisabledGating) {
  // A measurement very far away from the initial state.
  Pose3d measured_pose{eigenmath::Vector3d(1.0e10, 0.0, 0.0)};

  const Pose3d pose_before = filter_.pose();
  INTRINSIC_RT_EXPECT_OK(filter_.Update(
      measured_pose, InvariantPoseKalmanFilter::GatingMode::kDisabled));

  // The measurement should be accepted.
  EXPECT_FALSE(filter_.gating_result().was_outlier);

  // Check that the filter significantly updated the state pose towards the
  // measurement.
  EXPECT_THAT(filter_.pose(), Not(IsApprox(pose_before, 1.0e8)));
}

}  // namespace
}  // namespace intrinsic::icon
