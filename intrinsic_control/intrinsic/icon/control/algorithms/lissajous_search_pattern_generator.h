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

#ifndef INTRINSIC_ICON_CONTROL_ALGORITHMS_LISSAJOUS_SEARCH_PATTERN_GENERATOR_H_
#define INTRINSIC_ICON_CONTROL_ALGORITHMS_LISSAJOUS_SEARCH_PATTERN_GENERATOR_H_

#include <memory>
#include <optional>

#include "absl/status/statusor.h"
#include "intrinsic/icon/actions/impedance_trajectory_generators.pb.h"
#include "intrinsic/icon/control/algorithms/cartesian_impedance_commons.h"
#include "intrinsic/icon/control/algorithms/cartesian_impedance_trajectory.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/icon/utils/realtime_status_or.h"
#include "intrinsic/kinematics/types/cart_state.h"
#include "intrinsic/kinematics/types/state_rn.h"
#include "intrinsic/math/lissajous_pattern_utils.h"
#include "intrinsic/math/pose3.h"

namespace intrinsic::icon {

// Generates a Lissajous search pattern on a 2d task plane.
// For example, this enables robot motion following a planar search pattern as
// described in https://en.wikipedia.org/wiki/Lissajous_curve.
//
// Generally speaking, a simple desired pattern looks like below,
//  ..   ..
// .  . .  .    ^
// .   .   .    | search direction y
// .  . .  .    |
//  ..   ..
// --> search direction x

// The following functions/conventions are used in this generator:
// x = x_amplitude * sin(x_frequency * t) = x_amplitude * sin(2*pi / T_x * t)
// y = y_amplitude * sin(y_frequency * t) = y_amplitude * sin(2*pi / T_y * t)
//
// as it is required to have both of them to be sine functions without any phase
// difference to have the start point of the pattern at the center of the
// pattern. (Note: x_amplitude is x_size / 2, and y_amplitude is y_size / 2.)

// This generator finds the most optimum Lissajous pattern that
//   1. has the start point at the center of the pattern,
//   2. has the pattern symmetric both about the x-axis and y-axis,
//   3. has the pattern not walking repeated paths within the same pattern
//   cycle.
class LissajousSearchPattern2D {
 public:
  static constexpr double kDefaultSearchPatternWidth = 0.001;
  static constexpr double kDefaultSearchPatternMaxVelocity = 0.01;
  static constexpr int kDefaultSearchPatternMaxLissajousInteger = 5;

  struct InputParameters {
    // Bounds for a rectangular area within which to generate a Lissajous
    // pattern.
    double x_size = kDefaultSearchPatternWidth;
    double y_size = kDefaultSearchPatternWidth;
    double max_velocity = kDefaultSearchPatternMaxVelocity;
    int max_lissajous_integer = kDefaultSearchPatternMaxLissajousInteger;
  };

  static InputParameters FromProto(
      const intrinsic_proto::icon::actions::proto::LissajousSearchPattern2D&
          params);

  static intrinsic_proto::icon::actions::proto::LissajousSearchPattern2D
  ToProto(const InputParameters& params);

  // Creates a LissajousSearchPattern2D from associated `params` and the control
  // `frequency_hz`.
  static absl::StatusOr<LissajousSearchPattern2D> Create(
      const InputParameters& params, double frequency_hz);

  // Returns the nominal period of the Lissajous figure in seconds, assumed
  // speed override 100%.
  double GetPeriodSeconds() const {
    return geometrical_lissajous_pattern_.period_seconds;
  }

  // Returns execution phase variable, which lies in [0,1].
  double GetPhase() const {
    return time_since_pattern_execution_start_ / GetPeriodSeconds();
  }

  // Steps the 2d Lissajous pattern generator subject to
  // `speed_override_factor`. Returns an error status if the speed override is
  // outside of [0,1].
  icon::RealtimeStatusOr<StateRnPV> Step(double speed_override_factor);

 private:
  explicit LissajousSearchPattern2D(
      const GeometricalLissajousPattern& geometrical_lissajous_pattern,
      double frequency_hz)
      : geometrical_lissajous_pattern_(geometrical_lissajous_pattern),
        frequency_hz_(frequency_hz) {}

  GeometricalLissajousPattern geometrical_lissajous_pattern_;

  double frequency_hz_;

  // Internal time representation, used to modulate speed override. Gets
  // increased with every call to Step(). The speed override is multiplied with
  // the time increment, so that the total internal time passed accounts for the
  // speed override history.
  double time_since_pattern_execution_start_ = 0.0;
};

// Generates realtime Cartesian impedance references alongside a Lissajous
// pattern which can be used for the Cartesian admittance or impedance actions.
// Provides a RealTimeCartesianTarget for each time step to insert a target
// along the z axis of the task frame. In particular, the reference position,
// twist and optionally reference wrench are generated on each step, which
// command the robot to execute the following motions:
//   1) motion to track a Lissajous curve in the X-Y plane of the task frame;
//   2) motion to oscillate around z-axis of the task frame;
//   3) motion to keep pushing along the insertion direction while searching.
// 1) and 2) are achieved by the reference_twist generated and 3) is achieved by
// the tool_reference_wrench generated.
// Fixed parameters, e.g., stiffness, damping, inertia, and optionally wrench,
// are the ones whose values remain the same throughout all steps and are to be
// provided at construction time.
// The orientation of the tool relative to the task frame at initialization is
// kept constant over the whole trajectory.
class LissajousImpedanceSearchPattern
    : public CartesianImpedanceTrajectoryInterface {
 public:
  struct InputParameters {
    // Defines task frame in robot base frame. The z-axis of the task frame is
    // the direction of the hole's axis and insertion direction (the direction
    // in which which the robot should apply a search force is positive z). If
    // unset, base_t_task will be set from the previously commanded Cartesian
    // state in `Initialize()`.
    // If set only the orientation of the task frame is kept constant, but the
    // translation is set to the last commanded position. This ensures that the
    // pattern is always centered around the initial tool position.
    std::optional<Pose3d> base_t_task;

    // Parameters of the 2d Lissajous search pattern in the task plane,
    // expressed w.r.t base_t_task
    LissajousSearchPattern2D::InputParameters search_pattern_2d_parameters;

    // Parameters for rotational oscillation around search z axis.
    double yaw_oscillation_amplitude_rad = 0.0;
    double yaw_oscillation_frequency_hz = 0.0;

    // Feedforward force (in N) to exert along the motion direction, which is
    // aligned with the z-axis of the task frame.
    double search_force_newton;

    // Combined stiffness of arm and environment during contact. Used to compute
    // an overdamping coefficient for stable contact.
    double contact_stiffness;

    // Virtual translational stiffness of end-effector in the 2d search plane,
    // in x-y direction of the task frame.
    // TODO(giftthaler): defaults to zero for compatibility with legacy
    // implementation.
    double translational_virtual_stiffness_x_y = 0.0;

    // Virtual rotational stiffness of end-effector in uncontrolled dimensions
    // (roll, pitch). For a peg-in-hole task, this is rotational stiffness about
    // the hole's (task frame's) x and y axis.
    double rotational_virtual_stiffness_roll_pitch = 0.0;

    // Virtual rotational stiffness of end-effector in controlled dimension
    // (yaw). For a peg-in-hole task, this is the rotational stiffness about the
    // hole's z-axis.
    // TODO(giftthaler): defaults to zero for compatibility with legacy
    // implementation.
    double rotational_virtual_stiffness_yaw = 0.0;

    // Desired virtual task-space inertia.
    double translational_virtual_inertia;
    double rotational_virtual_inertia;

    // Transform from the robot's flange to a target frame where motions and
    // forces are being commanded.
    Pose3d robot_tip_t_robot_tool = Pose3d::Identity();
  };

  static absl::StatusOr<InputParameters> FromProto(
      const intrinsic_proto::icon::actions::proto::
          LissajousImpedanceSearchPattern& params);

  static intrinsic_proto::icon::actions::proto::LissajousImpedanceSearchPattern
  ToProto(const InputParameters& params);

  // Precomputes fixed parameters which do not change in every control cycle.
  static absl::StatusOr<std::unique_ptr<CartesianImpedanceTrajectoryInterface>>
  Create(const InputParameters& params, double frequency_hz);

  const Pose3d& GetInitialRobotTipTRobotTool() const override {
    return params_.robot_tip_t_robot_tool;
  }

  RealtimeStatus Initialize(
      const CartStatePVA& base_t_tip_last_commanded) override;

  // Compute a realtime-increment for RealTimeCartesianTarget.
  RealtimeStatusOr<cartesian_impedance::RealTimeCartesianTarget> Step(
      double speed_override) override;

  double GetPhase() const override { return search_pattern_2d_.GetPhase(); }

 private:
  LissajousImpedanceSearchPattern(
      const InputParameters& params,
      const LissajousSearchPattern2D& search_pattern_2d, double frequency_hz)
      : params_(params),
        search_pattern_2d_(search_pattern_2d),
        frequency_hz_(frequency_hz) {}

  // The input parameters.
  InputParameters params_;
  // The pose of the tool in the task frame at initialization time. Translation
  // is always zero and the task to tool orientation is kept constant over the
  // whole trajectory.
  Pose3d task_t_tool_start_;

  // The basic set of Cartesian impedance parameters, for which position and
  // velocity in the task plane are modified in each RT-control cycle to realize
  // the search pattern. Remaining parameters stay constant. Is nullopt until
  // initialization in Initialize().
  std::optional<cartesian_impedance::RealTimeCartesianTarget>
      fixed_cartesian_impedance_target_ = std::nullopt;

  LissajousSearchPattern2D search_pattern_2d_;
  const double frequency_hz_;
  double time_since_trajectory_start_internal_ = 0.0;
};

}  // namespace intrinsic::icon

#endif  // INTRINSIC_ICON_CONTROL_ALGORITHMS_LISSAJOUS_SEARCH_PATTERN_GENERATOR_H_
