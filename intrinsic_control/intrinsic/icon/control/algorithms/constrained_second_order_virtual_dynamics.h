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

#ifndef INTRINSIC_ICON_CONTROL_ALGORITHMS_CONSTRAINED_SECOND_ORDER_VIRTUAL_DYNAMICS_H_
#define INTRINSIC_ICON_CONTROL_ALGORITHMS_CONSTRAINED_SECOND_ORDER_VIRTUAL_DYNAMICS_H_

#include <optional>

#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/control/algorithms/cartesian_impedance_commons.h"
#include "intrinsic/icon/control/algorithms/virtual_impedance_dynamics_bidiagonalization.h"
#include "intrinsic/icon/utils/realtime_status_or.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/twist.h"

namespace intrinsic::icon {

// Implements an acceleration- and jerk-constrained Cartesian (6-DoF) harmonic
// oscillator with driving wrench.
//
// This is an extension of the paper
//
// [1] "An Admittance Controller with a Jerk Limiter for Position-Controlled
// Robots" by Mae Ryusei and Kikuuwe Ryo, Hiroshima University, Japan.
// https://doi.org/10.51094/jxiv.444
//
// which originally is for 1-DoF systems only. We implement a generalization to
// SE(3) using
// * Riemannian geometry and logarithmic maps of pose histories into local
// tangent spaces.
// * the concept of "bidiagonalization" to transform fully coupled 6 DoF system
// of equations into decoupled diagonal systems, for which the 1 DoF constrained
// oscillator can be applied.
// * velocity constraints are respected only at "best effort" and might be
// actively relaxed to prevent infeasibility, for example when Cartesian limits
// are time-varying.
//
// See inline comments for more details.
class ConstrainedCartesianVirtualDynamics {
 public:
  explicit ConstrainedCartesianVirtualDynamics(double control_frequency_hz)
      : control_frequency_hz_(control_frequency_hz),
        impedance_bidiagonalization_(6) {}

  // Initializes the ConstrainedCartesianVirtualDynamics internal state from
  // `initial_base_t_tool_frame`, which must be set to the current tool frame
  // pose (or previously commanded pose, e.g. for admittance control), the
  // `initial_twist_in_tool_frame` which is the current system twist expressed
  // in the local frame (tool_frame), and the
  // `initial_acceleration_in_tool_frame` which is the current system
  // acceleration expressed in the local frame (tool_frame). Uses a constant
  // acceleration model (linear velocities) for the initialization of past
  // poses. Optionally, one can also provide the previous reference pose and
  // twist, which were inputs to the last control cycle. `Initialize()` must be
  // called before `Step()`.
  void Initialize(
      const Pose3d& initial_base_t_tool_frame,
      const Twist& initial_twist_in_tool_frame,
      const Acceleration& initial_acceleration_in_tool_frame,
      std::optional<Pose3d> previous_base_t_tool_desired_pose = std::nullopt,
      std::optional<Twist> previous_reference_twist_in_tool_frame =
          std::nullopt);

  // Returns a command Pose3d, which realizes a constrained 2nd-order virtual
  // dynamics model subject to the realtime impedance target `cartesian_target`,
  // the provided Cartesian limits and the input wrench
  // `sensed_wrench_in_tool_frame`. The sensed wrench and the deadband
  // are expressed wrt the "current pose", which typically means the "previously
  // commanded" pose returned by this algorithm by the previous call to
  // `Step()`. Jerk and acceleration limits are treated as hard constraints but
  // jerk limits are prioritized over acceleration limits. Velocity constraints
  // are respected only at "best effort". Velocity constraints might be actively
  // relaxed (up to arbitrary magnitudes)to prevent infeasibility, for example
  // when Cartesian limits are time-varying. This is one of the main assumptions
  // of the algorithm, and the reason why we should only consider it
  // acceleration and jerk constrained, not strictly velocity constrained. If
  // Cartesian limits are infinite, the realized dynamics will be equivalent to
  // an unconstrained 2nd order virtual dynamics model (like in classical
  // admittance and impedance control).
  //
  // Usage hints:
  // * when used in an admittance controller, for example,
  // `sensed_wrench_in_tool_frame` needs to be chosen as the
  // `sensed_wrench_at_tip` transformed to the tool frame and the
  // `wrench_deadband_in_tool_frame` needs to be chosen as the
  // `wrench_deadband_at_tip` transformed to the tool frame.
  // * different from the classical harmonic oscillator, position
  // references do not enter the control law through feedback on a position
  // error term. Instead, position references are directly used as feedforwards
  // on the position signal. This algorithm will in turn produce a rate limited
  // output trajectory creating a limit-aware transition to the position
  // setpoint.
  // * thanks to the properties of the underlying 1-Dof constrained harmonic
  // oscillator, this implementation can handle aribitrarily time-varying jerk
  // and acceleration limits.
  icon::RealtimeStatusOr<Pose3d> Step(
      const cartesian_impedance::RealTimeCartesianTarget& cartesian_target,
      const Wrench& sensed_wrench_in_tool_frame,
      const Wrench& wrench_deadband_in_tool_frame,
      double max_cartesian_velocity, double max_cartesian_acceleration,
      double max_cartesian_jerk);

  const Pose3d& q_k_minus_1() const { return q_k_minus_1_; }
  const Pose3d& q_k_minus_2() const { return q_k_minus_2_; }
  const Pose3d& q_k_minus_3() const { return q_k_minus_3_; }
  const Pose3d& p_d_k_minus_1() const { return p_d_k_minus_1_; }
  const Pose3d& p_d_k_minus_2() const { return p_d_k_minus_2_; }

 protected:
  const double control_frequency_hz_;

  VirtualImpedanceDynamicsBiDiagonalization impedance_bidiagonalization_;

  bool initialized_ = false;

  // Internal state tracking the history of commanded poses and reference poses,
  // all expressed wrt the robot base frame. We track a "pose" history instead
  // of differential states (like velocities and accelerations) for consistency
  // with the original paper [1] and to avoid complicated transformations of
  // angular velocities and accelerations on Riemannian manifolds. For
  // consistency of notation, we also adopt the naming convention as in [1].
  // Indices for the main equation are q_k = f(q_k_minus_1, q_k_minus_2,
  // q_k_minus_3, ... ,
  //         ... p_d_k, p_d_k_minus_1, p_d_k_minus_2, ...);
  // where q_k is the new pose command for this cycle computed in Step(),
  // p_d_k is the reference pose for this cycle, and with the following other
  // definitions:
  Pose3d q_k_minus_1_;    // Cartesian pose commanded in last control cycle.
  Pose3d q_k_minus_2_;    // Cartesian pose commanded two control cycles ago.
  Pose3d q_k_minus_3_;    // Cartesian pose commanded three control cycles ago.
  Pose3d p_d_k_minus_1_;  // Cartesian reference pose in last control cycle.
  Pose3d p_d_k_minus_2_;  // Cartesian reference pose two control cycles ago.

  std::optional<double> max_cartesian_acceleration_last_cycle_ = 0.0;
};

// Implements an acceleration- and jerk-constrained n-dimensional driven
// harmonic oscillator.
//
// This is an extension of the paper
//
// [1] "An Admittance Controller with a Jerk Limiter for Position-Controlled
// Robots" by Mae Ryusei and Kikuuwe Ryo, Hiroshima University, Japan.
// https://doi.org/10.51094/jxiv.444
//
// which originally is for 1-DoF systems only. We implement a generalization to
// N dimensions using
// * the concept of "bidiagonalization" to transform fully coupled N DoF system
// of equations into decoupled diagonal systems, for which the 1 DoF constrained
// oscillator can be applied.
// * velocity constraints are respected only at "best effort" and might be
// actively relaxed to prevent infeasibility, for example when Cartesian limits
// are time-varying.
//
// See inline comments for more details.
class ConstrainedVirtualDynamicsNd {
 public:
  struct Parameters {
    eigenmath::VectorNd reference_position;
    eigenmath::VectorNd reference_velocity;
    eigenmath::VectorNd feedforward_acceleration;
    eigenmath::VectorNd reference_force;
    eigenmath::MatrixNd virtual_inertia_inverse;
    eigenmath::MatrixNd stiffness;
    eigenmath::MatrixNd damping;
    eigenmath::MatrixNd force_selection_matrix;
  };

  explicit ConstrainedVirtualDynamicsNd(int ndof, double control_frequency_hz)
      : ndof_(ndof),
        control_frequency_hz_(control_frequency_hz),
        impedance_bidiagonalization_(ndof) {}

  // Initializes the ConstrainedVirtualDynamicsNd internal state from
  // `initial_position`, `initial_velocity`, and the `initial_acceleration`.
  // Uses a constant acceleration model (linear velocities) for the
  // initialization of past positions. Optionally, one can also provide the
  // previous reference positions and/or velocities, which were inputs to the
  // last control cycle. `Initialize()` must be called before `Step()`.
  void Initialize(
      const eigenmath::VectorNd& initial_position,
      const eigenmath::VectorNd& initial_velocity,
      const eigenmath::VectorNd& initial_acceleration,
      std::optional<eigenmath::VectorNd> previous_reference_position =
          std::nullopt,
      std::optional<eigenmath::VectorNd> previous_reference_velocity =
          std::nullopt);

  // Returns a position command, which realizes a constrained 2nd-order virtual
  // dynamics model subject to the realtime impedance `input_parameters`,
  // the provided `max_velocity`, `max_acceleration`, and `max_jerk` and the
  // `sensed_force`. Jerk and acceleration limits are treated as hard
  // constraints but jerk limits are prioritized over acceleration limits.
  // Velocity constraints are respected only at "best effort". Velocity
  // constraints might be actively relaxed (up to arbitrary magnitudes)to
  // prevent infeasibility, for example when other limits are time-varying.
  // This is one of the main assumptions of the algorithm, and the reason why we
  // should only consider it acceleration and jerk constrained, not strictly
  // velocity constrained. If limits are infinite, the realized dynamics will be
  // equivalent to an unconstrained 2nd order virtual dynamics model.
  icon::RealtimeStatusOr<eigenmath::VectorNd> Step(
      const Parameters& input_parameters,
      const eigenmath::VectorNd& sensed_force,
      const eigenmath::VectorNd& deadband_force, double max_velocity,
      double max_acceleration, double max_jerk);

  const eigenmath::VectorNd& q_k_minus_1() const { return q_k_minus_1_; }
  const eigenmath::VectorNd& q_k_minus_2() const { return q_k_minus_2_; }
  const eigenmath::VectorNd& q_k_minus_3() const { return q_k_minus_3_; }
  const eigenmath::VectorNd& p_d_k_minus_1() const { return p_d_k_minus_1_; }
  const eigenmath::VectorNd& p_d_k_minus_2() const { return p_d_k_minus_2_; }

 protected:
  const int ndof_;
  const double control_frequency_hz_;

  VirtualImpedanceDynamicsBiDiagonalization impedance_bidiagonalization_;

  bool initialized_ = false;

  eigenmath::VectorNd q_k_minus_1_;    // Position commanded in last cycle.
  eigenmath::VectorNd q_k_minus_2_;    // Position commanded two cycles ago.
  eigenmath::VectorNd q_k_minus_3_;    // Position commanded three cycles ago.
  eigenmath::VectorNd p_d_k_minus_1_;  // Reference position in last cycle.
  eigenmath::VectorNd p_d_k_minus_2_;  // Reference position two cycles ago.

  std::optional<double> max_acceleration_last_cycle_ = 0.0;

  int iteration_count_ = 0;
};

}  // namespace intrinsic::icon

#endif  // INTRINSIC_ICON_CONTROL_ALGORITHMS_CONSTRAINED_SECOND_ORDER_VIRTUAL_DYNAMICS_H_
