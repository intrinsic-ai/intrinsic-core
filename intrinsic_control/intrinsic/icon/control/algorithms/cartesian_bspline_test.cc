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

#include "intrinsic/icon/control/algorithms/cartesian_bspline.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <vector>

#include "intrinsic/eigenmath/matchers.h"
#include "intrinsic/eigenmath/so3.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/testing/malloc_test.h"
#include "intrinsic/kinematics/types/cart_state.h"
#include "intrinsic/kinematics/types/cartesian_limits.h"
#include "intrinsic/math/pose3.h"

namespace intrinsic {
namespace control {
namespace test {

using eigenmath::testing::IsApprox;

const CartesianLimits kLimits =
    CreateSimpleCartesianLimits(10, 1, 1, 10, 0.1, 1.0, 10);

static constexpr double kEpsilon = 1e-6;

TEST(CartesianBSpline, ConstructorAndGetFunctions) {
  double kFrequency = 1000;
  size_t kMaxNPoints = 100;
  CartesianBSpline spline(kFrequency, kMaxNPoints);

  EXPECT_EQ(spline.NumPoints(), 0);
  EXPECT_EQ(spline.MaxNumPoints(), kMaxNPoints);
  EXPECT_EQ(spline.GetStatus(), CartesianBSpline::ERROR);
}

TEST(CartesianBSpline, SetControlPoints) {
  double kFrequency = 1000;
  size_t kMaxNPoints = 100;
  CartesianBSpline spline(kFrequency, kMaxNPoints);

  CartStatePVA a_S_b_previous;
  std::vector<Pose3d> a_Ts_b(5);

  // Test that one can not exceed max number of points.
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  EXPECT_TRUE(spline.SetLimits(kLimits));
  EXPECT_TRUE(spline.SetControlPoints(a_Ts_b, a_S_b_previous));
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();

  // Setting new spline: current pose should be close to first point
  a_Ts_b[0] = Pose3d(eigenmath::Vector3d(1, 1, 1));
  a_S_b_previous.pose = Pose3d(eigenmath::Vector3d(1, 1, 5));
  EXPECT_FALSE(spline.SetControlPoints(a_Ts_b, a_S_b_previous));
}

// Test case A-A-B-B which should result in linear and slerp interpolation
// for translations and rotations respectively.
TEST(CartesianBSpline, Interpolation) {
  double kFrequency = 100;
  size_t kMaxNPoints = 100;
  CartesianBSpline spline(kFrequency, kMaxNPoints);

  // Create list of control points (A-A-B-B)
  eigenmath::SO3d so3_a(0.1, 0.2, 0.3);
  eigenmath::SO3d so3_b(0.4, 0.5, 0.6);

  eigenmath::Vector3d pos_a(1, 2, 3);
  eigenmath::Vector3d pos_b(4, 5, 6);

  std::vector<Pose3d> a_Ts_b;
  a_Ts_b.emplace_back(so3_a, pos_a);
  a_Ts_b.emplace_back(so3_a, pos_a);
  a_Ts_b.emplace_back(so3_b, pos_b);
  a_Ts_b.emplace_back(so3_b, pos_b);

  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  ASSERT_EQ(spline.GetStatus(), CartesianBSpline::ERROR);

  // Initialize spline
  ASSERT_TRUE(spline.SetLimits(kLimits));
  CartStatePVA a_S_b_previous;
  a_S_b_previous.pose = a_Ts_b[0];
  ASSERT_TRUE(spline.SetControlPoints(a_Ts_b, a_S_b_previous));

  // Verify status update
  ASSERT_EQ(spline.GetStatus(), CartesianBSpline::WORKING);

  // Calculate axis/angle offset between first and last point
  Eigen::AngleAxis<double> rot_diff_overall(
      (a_Ts_b[0].inverse() * a_Ts_b[3]).quaternion());
  double rot_angle_overall = rot_diff_overall.angle();

  // Calculate translational offset between first and last point
  eigenmath::Vector3d pos_diff_overall =
      (a_Ts_b[0].inverse() * a_Ts_b[3]).translation();

  // Spline progress starts with 0
  EXPECT_DOUBLE_EQ(spline.GetProgress(), 0.0);

  // Walk through spline
  static constexpr size_t kMaxNCalls = 1000;
  double pct = 0;  // percentage of current diff wrt overall diff
  size_t n_calls = 0;
  while (spline.GetStatus() != CartesianBSpline::FINAL_STATE_REACHED) {
    CartStatePVA a_S_b_output;
    spline.ComputeSetpoint(&a_S_b_output);

    // Calculate trans & rot diff between current pose and first pose
    Pose3d pose_diff = a_Ts_b[0].inverse() * a_S_b_output.pose;
    Eigen::AngleAxis<double> rot_diff(pose_diff.quaternion());
    eigenmath::Vector3d pos_diff = pose_diff.translation();

    // Calculate percentage of current diff wrt overal diff and verify
    // all DOFs are synchronized (same percentage value).
    double pct_new = rot_diff.angle() / rot_angle_overall;
    for (size_t i = 0; i < 3; i++) {
      ASSERT_LE(fabs(pct_new - pos_diff[i] / pos_diff_overall[i]), kEpsilon);
    }

    // Verify percentage is monotonically increasing
    ASSERT_GE(pct_new, pct);
    pct = pct_new;

    // Count number of iterations and fail if we exceed max n calls.
    n_calls += 1;
    ASSERT_LE(n_calls, kMaxNCalls);
  }

  // Spline progress reached 1
  EXPECT_DOUBLE_EQ(spline.GetProgress(), 1.0);

  ASSERT_DOUBLE_EQ(pct, 1.0);
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
}

TEST(CartesianBSpline, TranslationalVelocityComputationWorks) {
  double kFrequency = 1000;
  size_t kMaxNPoints = 100;
  CartesianBSpline spline(kFrequency, kMaxNPoints);

  // Create list of arbitrarily chosen control points
  eigenmath::SO3d so3_a(0.1, 0.2, 0.3);
  eigenmath::SO3d so3_b(0.4, 0.5, 0.6);
  eigenmath::Vector3d pos_a1(1, 2, 3);
  eigenmath::Vector3d pos_a2(0, 2, 1);
  eigenmath::Vector3d pos_b1(4, 5, 6);
  eigenmath::Vector3d pos_b2(4, 1, 6);

  std::vector<Pose3d> a_Ts_b;
  a_Ts_b.emplace_back(so3_a, pos_a1);
  a_Ts_b.emplace_back(so3_a, pos_a2);
  a_Ts_b.emplace_back(so3_b, pos_b1);
  a_Ts_b.emplace_back(so3_b, pos_b2);

  ASSERT_EQ(spline.GetStatus(), CartesianBSpline::ERROR);

  // Initialize spline
  ASSERT_TRUE(spline.SetLimits(kLimits));
  CartStatePVA a_S_b_previous;
  a_S_b_previous.pose = a_Ts_b[0];
  ASSERT_TRUE(spline.SetControlPoints(a_Ts_b, a_S_b_previous));

  // Verify status update
  ASSERT_EQ(spline.GetStatus(), CartesianBSpline::WORKING);

  // Walk through spline, but only up to 95% of the total spline length. Reason:
  // finite difference approximation of velocity does not work for the last
  // sample of the spline due to inconsistent step size.
  eigenmath::Vector3d prev_position = pos_a1;
  while (spline.GetProgress() < 0.95) {
    CartStatePVA a_S_b_output;

    IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
    spline.ComputeSetpoint(&a_S_b_output);
    IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();

    // Calculate translational velocity based on finite differences.
    eigenmath::Vector3d vel_est =
        (a_S_b_output.pose.translation() - prev_position) * kFrequency;
    prev_position = a_S_b_output.pose.translation();

    EXPECT_THAT(vel_est, IsApprox(a_S_b_output.velocity.head<3>(), 1.0e-2));
  }
}

}  // namespace test
}  // namespace control
}  // namespace intrinsic
