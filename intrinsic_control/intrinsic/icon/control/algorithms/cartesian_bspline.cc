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

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "absl/log/check.h"
#include "absl/strings/str_format.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/utils/log.h"
#include "intrinsic/kinematics/types/cart_state.h"
#include "intrinsic/kinematics/types/cartesian_limits.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/spline/bspline_base.h"

namespace intrinsic {
namespace control {

namespace {
constexpr double kEpsilonSmall = 1e-6;  // checking for path continuity
constexpr double kEpsilonLarge = 1e-2;  // checking for initial pose offset
constexpr size_t kMinPoints = 4;
constexpr int kDegree = 3;
}  // namespace

CartesianBSpline::CartesianBSpline(double frequency, size_t max_n_points)
    : dt_(1.0 / frequency),
      max_n_points_(max_n_points),
      translation_dof_buffer_(2, eigenmath::Vector3d::Zero()) {
  CHECK_GT(frequency, 0);
  CHECK_GE(max_n_points_, kMinPoints);

  // Reserve memory for vectors to avoid mallocs during runtime
  points_.reserve(max_n_points_);
  spline_pos_derivs_.resize(3);

  points_pos_.reserve(max_n_points_);
  points_rot_.reserve(max_n_points_);
  knots_.reserve(max_n_points_ + kDegree + 1);

  CHECK(spline_pos_.Init(kDegree, knots_.capacity(), 3));
  CHECK(spline_rot_.Init(kDegree, knots_.capacity()));
}

bool CartesianBSpline::SetLimits(const CartesianLimits& limits) {
#define CHECK_AGAINST(lhs, op, rhs, i)                                   \
  do {                                                                   \
    if (lhs op rhs) {                                                    \
      INTRINSIC_RT_LOG_THROTTLED(ERROR)                                  \
          << "did not expect " #lhs " " #op " " #rhs ", but " #lhs " = " \
          << lhs << " and " #rhs " = " << rhs << " for i = " << i;       \
      return false;                                                      \
    }                                                                    \
  } while (0)

  for (int i = 0; i < 3; i++) {
    CHECK_AGAINST(limits.max_translational_position[i], <=,
                  limits.min_translational_position[i], i);
    CHECK_AGAINST(limits.min_translational_velocity[i], >=, 0.0, i);
    CHECK_AGAINST(limits.max_translational_velocity[i], <=, 0.0, i);
    CHECK_AGAINST(limits.min_translational_acceleration[i], >=, 0.0, i);
    CHECK_AGAINST(limits.max_translational_acceleration[i], <=, 0.0, i);
    CHECK_AGAINST(limits.min_translational_jerk[i], >=, 0.0, i);
    CHECK_AGAINST(limits.max_translational_jerk[i], <=, 0.0, i);
  }

#undef CHECK_AGAINST

#define CHECK_AGAINST(lhs, op, rhs)                                      \
  do {                                                                   \
    if (lhs op rhs) {                                                    \
      INTRINSIC_RT_LOG_THROTTLED(ERROR)                                  \
          << "did not expect " #lhs " " #op " " #rhs ", but " #lhs " = " \
          << lhs << " and " #rhs " = " << rhs;                           \
      return false;                                                      \
    }                                                                    \
  } while (0)

  CHECK_AGAINST(limits.max_rotational_velocity, <=, 0.0);
  CHECK_AGAINST(limits.max_rotational_acceleration, <=, 0.0);
  CHECK_AGAINST(limits.max_rotational_jerk, <=, 0.0);

#undef CHECK_AGAINST

  limits_ = limits;

  return true;
}

// Checks whether all poses in vector A are equal to the poses in B.
bool PointsAreEqual(const std::vector<Pose3d>& A,
                    const std::vector<Pose3d>& B) {
  if (A.size() != B.size()) {
    return false;
  }

  for (size_t i = 0; i < A.size(); i++) {
    // TODO(elir): b/34820023
    if (!A[i].isApprox(B[i], kEpsilonLarge)) {
      return false;
    }
  }

  return true;
}

bool CartesianBSpline::SetControlPoints(
    const std::vector<Pose3d>& a_Ts_b_goal,
    const CartStatePVA& a_S_b_previous_target) {
  status_ = ERROR;

  // If command hasn't changed, just check continuity
  if (PointsAreEqual(a_Ts_b_goal, points_)) {
    // TODO(elir) check is not bspline specific. move to CartSelectionCtl?
    if (!a_t_b_previous_commanded_.isApprox(a_S_b_previous_target.pose,
                                            kEpsilonSmall)) {
      const eigenmath::Vector3d diff =
          (a_t_b_previous_commanded_.translation() -
           a_S_b_previous_target.pose.translation());
      INTRINSIC_RT_LOG(ERROR)
          << "Previous point must match last setpoint! Position diff xyz = "
          << diff[0] << " " << diff[1] << " " << diff[2];
      ClearTrajectory();
      return false;
    }
    previous_valid_ = true;
    return true;
  }

  INTRINSIC_RT_LOG(INFO) << "Initializing Cartesian spline";
  INTRINSIC_RT_LOG(INFO) << "There are " << a_Ts_b_goal.size()
                         << " points in a_Ts_b_goal and " << points_.size()
                         << " points in points_";

  // Command has changed.
  if (!a_Ts_b_goal[0].isApprox(a_S_b_previous_target.pose, kEpsilonLarge)) {
    const eigenmath::Vector3d diff =
        a_Ts_b_goal[0].translation() - a_S_b_previous_target.pose.translation();
    INTRINSIC_RT_LOG(ERROR)
        << "First control point should be close to current robot position. "
           "Position diff xyz = "
        << diff[0] << " " << diff[1] << " " << diff[2];
    ClearTrajectory();
    return false;
  }

  // Copy control points
  points_ = a_Ts_b_goal;
  size_t n_points = points_.size();

  CHECK_LE(n_points, max_n_points_);

  // Generate knot vector
  if (!BSplineBase::MakeUniformKnotVector(n_points, &knots_, kDegree)) {
    INTRINSIC_RT_LOG(ERROR)
        << "Error setting knot vector, points.size()= " << n_points;
    ClearTrajectory();
    return false;
  }

  // Set splines knot vector
  if (!spline_pos_.SetKnotVector(knots_) ||
      !spline_rot_.SetKnotVector(knots_)) {
    INTRINSIC_RT_LOG(ERROR) << "Error setting knot vector.";
    ClearTrajectory();
    return false;
  }

  smin_ = knots_[0];
  smax_ = knots_[knots_.size() - 1];

  CHECK_LE(n_points, max_n_points_) << absl::StrFormat(
      "n_points (%zu) exceeds capacity (%zu)", n_points, max_n_points_);

  // Set splines control points
  points_pos_.resize(n_points);
  points_rot_.resize(n_points);
  for (size_t i = 0; i < n_points; i++) {
    points_pos_[i] = points_[i].translation();
    points_rot_[i] = points_[i].quaternion();
  }
  if (!spline_pos_.SetControlPoints(points_pos_) ||
      !spline_rot_.SetControlPoints(points_rot_)) {
    INTRINSIC_RT_LOG(ERROR) << "Error setting control points.";
    ClearTrajectory();
    return false;
  }

  points_valid_ = true;
  previous_valid_ = true;

  s_ = smin_;

  status_ = WORKING;

  return true;
}

bool CartesianBSpline::ComputeSetpoint(CartStatePVA* a_S_b_output) {
  status_ = ERROR;

  if (!previous_valid_ || !points_valid_) {
    INTRINSIC_RT_LOG(ERROR)
        << "Error: limits, control points and previous value must be valid.";
    return false;
  }

  CHECK(nullptr != a_S_b_output) << "Output is null";

  spline_pos_.EvalCurveAndDerivatives(s_, &spline_pos_derivs_);

  // Conservative limit requirement to allow us to use the norm safely
  CHECK(limits_.max_translational_velocity[0] ==
            limits_.max_translational_velocity[1] &&
        limits_.max_translational_velocity[0] ==
            limits_.max_translational_velocity[2])
      << "Translational velocity limits should be all equal. ";

  double dXdt_desired = limits_.max_translational_velocity.norm();
  double omega_desired = limits_.max_rotational_velocity;

  // The following is a heuristic to walk through the spline using the desired
  // translational and angular velocity wrt time.
  //   1) Calculate Cartesian translational path velocity dX/ds at current s
  //   2) Calculate Cartesian angular path velocity dq/ds at current s
  //   3) Calculate required path parameter change ds/dt to achieve the desired
  //       Cartesian translational velocity dX/dt and angular velocity dq/dt;
  //       use the most conservative (smallest) value.
  //   4) Increment path parameter s
  //   5) Calculate positional and rotational setpoint at new s value
  // TODO(elir) implement timing algorithm to ensure higher derivative
  // limits are respected.
  double dXds = spline_pos_derivs_[1].norm();
  double omega = spline_rot_.EvalAngularVelocity(s_);
  double dsdt;
  if (std::isfinite(dXdt_desired / dXds) &&
      std::isfinite(omega_desired / omega)) {
    dsdt = std::min(dXdt_desired / dXds, omega_desired / omega);
  } else if (std::isfinite(dXdt_desired / dXds)) {
    dsdt = dXdt_desired / dXds;
  } else if (std::isfinite(omega_desired / omega)) {
    dsdt = omega_desired / omega;
  } else {
    // Both omega = 0 and dXds = 0, which can only occur for zero motion
    // (several identical poses added.) In this case we select dsdt such that
    // we'd end up at the final pose within one time step.
    dsdt = (smax_ - smin_) / dt_;
  }

  double ds = dsdt * dt_;

  // TODO(gaschler): We assume that the problem is linear for small ds, so
  // we should make sure here that ds is small or make multiple steps.
  s_ = std::min(s_ + ds, smax_);

  double knots = ds * knots_.size();
  if (knots > 0.1) {
    INTRINSIC_RT_LOG_THROTTLED(WARNING)
        << "Inaccurate spline computation. Assuming "
           "spline is linear over knots: "
        << knots << " ds: " << ds << " s: " << s_;
  }

  // Evaluate both splines at calculated s value and copy to pose
  eigenmath::Quaterniond quat;
  spline_rot_.EvalCurve(s_, &quat);
  spline_pos_.EvalCurveAndDerivatives(s_, &translation_dof_buffer_);
  a_S_b_output->pose = Pose3d(quat, translation_dof_buffer_[0]);

  // Compute translational velocities.
  const eigenmath::Vector3d& dqds = translation_dof_buffer_[1];
  a_S_b_output->velocity.head<3>() = dqds * dsdt;

  // TODO(elir) fill in angular velocity and accelerations (blocked by
  // b/34102244)

  a_t_b_previous_commanded_ = a_S_b_output->pose;

  if (s_ >= smax_) {
    status_ = FINAL_STATE_REACHED;
    s_ = smin_;
  } else {
    status_ = WORKING;
  }

  return true;
}

void CartesianBSpline::ClearTrajectory() {
  status_ = ERROR;
  points_.clear();
}

double CartesianBSpline::GetProgress() const {
  return status_ == FINAL_STATE_REACHED ? 1.0 : (s_ - smin_) / (smax_ - smin_);
}

}  // namespace control
}  // namespace intrinsic
