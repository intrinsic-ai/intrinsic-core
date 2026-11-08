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

#ifndef INTRINSIC_ICON_CONTROL_ALGORITHMS_CARTESIAN_BSPLINE_H_
#define INTRINSIC_ICON_CONTROL_ALGORITHMS_CARTESIAN_BSPLINE_H_

#include <cstddef>
#include <vector>

#include "intrinsic/eigenmath/types.h"
#include "intrinsic/kinematics/types/cart_state.h"
#include "intrinsic/kinematics/types/cartesian_limits.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/spline/bspline.h"
#include "intrinsic/math/spline/bsplineq.h"

namespace intrinsic {
namespace control {

// Cartesian B-spline following trajectory generator
//  Generates and executes a B-spline for the given set of control points.
//  In general, the spline will only go exactly through the first and last
//    control point. If close approximation of control points is desired,
//    control points can be pre processed using PolyLineToBspline3Waypoints()
// Known limitations:
// - does not honour Cartesian acceleration or jerk limits
// - does not support appending new control points while executing
// - computed setpoints have zero velocities & accelerations
class CartesianBSpline {
 public:
  enum {
    ERROR,
    WORKING,
    FINAL_STATE_REACHED,
  };

  // Constructor, may allocate.
  // frequency: The rate in Hz that the set point generator will be
  // called.
  // max_n_points: maximum number of control points
  CartesianBSpline(double frequency, size_t max_n_points);

  size_t NumPoints() const { return points_.size(); }
  size_t MaxNumPoints() const { return points_.capacity(); }

  int GetStatus() const { return status_; }

  // SetLimits for trajectory calculation
  // limits: the limits to set
  // returns true if inputs were successfully set.
  bool SetLimits(const CartesianLimits& limits);

  // Set the control point poses to define the b-spline
  // a_Ts_b_goal: control points defining the b-spline
  // a_S_b_previous_target: previous target state
  bool SetControlPoints(const std::vector<Pose3d>& a_Ts_b_goal,
                        const CartStatePVA& a_S_b_previous_target);

  // Compute next pose on b-spline
  // vel_trans: desired translational velocity dX/dt [m/s]
  // vel_rot desired: angular velocity dq/dt [rad/s]
  // a_S_b_output: variable to store new a_S_b
  //               setpoint in.
  // return true for success, false if an error occured
  bool ComputeSetpoint(CartStatePVA* a_S_b_output);

  // Remove all control points and set status to ERROR.
  void ClearTrajectory();

  // Return progress from 0.0 (not started) to 1.0 (goal reached).
  double GetProgress() const;

 private:
  double dt_;       // controller update period
  double s_ = 1.0;  // path parameter used to evaluate bspline [0, 1]

  Pose3d a_t_b_previous_commanded_;
  std::vector<Pose3d> points_;  // a_T_b control points
  size_t max_n_points_;
  std::vector<double> knots_;
  double smin_, smax_;
  std::vector<eigenmath::Vector3d> spline_pos_derivs_;

  CartesianLimits limits_;

  BSpline3d spline_pos_;
  BSplineQ spline_rot_;

  std::vector<eigenmath::Vector3d> points_pos_;
  std::vector<eigenmath::Quaterniond> points_rot_;

  int status_ = ERROR;
  bool previous_valid_ = false;
  bool points_valid_ = false;

  // Holds position, velocity w.r.t. path variable when evaluating bspline.
  std::vector<eigenmath::Vector3d> translation_dof_buffer_;
};

}  // namespace control
}  // namespace intrinsic
#endif  // INTRINSIC_ICON_CONTROL_ALGORITHMS_CARTESIAN_BSPLINE_H_
