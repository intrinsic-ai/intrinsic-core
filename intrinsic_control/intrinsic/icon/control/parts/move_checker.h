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

#ifndef INTRINSIC_ICON_CONTROL_PARTS_MOVE_CHECKER_H_
#define INTRINSIC_ICON_CONTROL_PARTS_MOVE_CHECKER_H_

#include <stddef.h>

#include <memory>
#include <optional>

#include "absl/status/statusor.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/control/algorithms/joint_stop_trajectory.h"
#include "intrinsic/icon/control/collision/robot_collision_checker.h"
#include "intrinsic/icon/control/joint_position_command.h"
#include "intrinsic/icon/control/parts/feature_interfaces.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/icon/utils/realtime_status_or.h"
#include "intrinsic/kinematics/types/joint_limits.h"

namespace intrinsic::icon {

struct MoveCheckerExtraConfig {
  // Default tolerance that we allow velocity, acceleration and jerk to
  // overshoot the system limits before we return an error status.
  static constexpr double kDefaultLimitViolationTolerance = 1e-5;

  double max_limit_violation_tolerance = kDefaultLimitViolationTolerance;
  bool error_on_jerk_system_limit_violation = false;
};

// Implements a check for joint position commands to evaluate if they are
// moving towards safety with respect to joint position limits. Additionally
// will check if robot is moving into self collision when a
// `collision_checker` is provided.
class MoveChecker {
 public:
  // Tolerance that we allow a joint to move in the wrong direction with
  // respect to limits when a stop position is beyond the limits.
  static constexpr double kDirectionViolationTolerance = 1e-9;

  // Maximum tolerance that we allow velocity, acceleration and jerk to
  // overshoot the system limits before we return an error status.
  static constexpr double kMaxLimitViolationTolerance = 1e-2;

  // Allowable margin above the maximum velocity when
  // checking for position continuity between setpoints.
  static constexpr double kVelocityInputMargin = 0.2;

  //  Factory which creates a MoveChecker where self collision checking is
  //  only performed when the optional `collision_checker` is provided.
  static absl::StatusOr<std::unique_ptr<MoveChecker>> Create(
      size_t num_dofs, double control_frequency_hz,
      std::unique_ptr<collision::RobotCollisionChecker> collision_checker =
          nullptr);

  // Updates the setpoint from the previous timestep.
  icon::RealtimeStatus UpdatePreviousSetpoint(
      const JointPositionCommand& last_setpoint);

  // Checks whether a setpoint is ok to execute. If `joint_limits` are
  // provided these must `IsWithinLimits()` of the `maximum_limits`. A
  // `max_limit_violation_tolerance` is applied to velocity, acceleration and
  // jerk limits from `maximum_joint_limits` to allow for slight numerical
  // deviations (e.g. when a control has been optimized numerically) without
  // returning an error status.
  // Checks whether the stop position (final state of a stop trajectory)
  // computed after applying the provided setpoint remains within positional
  // limits. For acceleration-limited setpoints, this can be easily checked.
  // For torque-limited setpoints, a stop trajectory from a given state cannot
  // be computed exactly as the accelerations are state dependent, but it can
  // be approximated based on `joint_acceleration_limits_from_dynamics` (which
  // are updated each cycle according to the current state, but are considered
  // constant for the computation of the stop trajectory). If these limits
  // `joint_acceleration_limits_from_dynamics` are missing for a
  // torque-limited setpoint, to compute the stop trajectory the MoveChecker
  // uses as maximum acceleration limits the component-wise maximum between
  // joint accelerations of `joint_limits` and absolute value of accelerations
  // from `setpoint`.
  icon::RealtimeStatus CheckSetpoint(
      const JointPositionCommand& setpoint, const JointLimits& joint_limits,
      const JointLimits& maximum_joint_limits,
      std::optional<JointLimitsInterface::JointAccelerationLimitsFromDynamics>
          joint_acceleration_limits_from_dynamics = std::nullopt,
      const MoveCheckerExtraConfig& extra_config = MoveCheckerExtraConfig());

 private:
  size_t num_dofs_;
  double control_frequency_hz_;
  std::optional<JointPositionCommand> previous_setpoint_;

  // Takes num_dofs which is used to verify correct dimension for commands, the
  // control_frequency_hz is used where numerical derivatives of position
  // setpoints must be evaluated. The joint_limits_default are the user defined
  // limits and joint_limits_maximum are the maximum feedback control limits to
  // compare the command to. The stop_trajectory is used to generate the
  // stopping trajectory for evaluating if a move is safe. The optional
  // collision_checker is used to check for self collision of the robot.
  MoveChecker(size_t num_dofs, double control_frequency_hz,
              std::unique_ptr<control::JointStopTrajectory> stop_trajectory,
              std::unique_ptr<collision::RobotCollisionChecker>
                  collision_checker = nullptr);

  // Checks whether the `stop_position` results in self-collision of the
  // robot. If so, checks the direction of intended motion to evaluate if the
  // setpoint moves away or towards collision.
  icon::RealtimeStatus CheckStopPositionForCollisionViolations(
      const eigenmath::VectorNd& stop_position);

  // Queries the collision checking model to determine the minimum collision
  // distance between any two links.
  icon::RealtimeStatusOr<double> MinCollisionDistance(
      const eigenmath::VectorNd& position) const;

  // This calculates whether a stop initiated after the queried motion can
  // respect the position limits.
  std::unique_ptr<control::JointStopTrajectory> stop_trajectory_;

  // This is used to calculate whether the robot is in self collision.
  std::unique_ptr<intrinsic::collision::RobotCollisionChecker>
      collision_checker_;

  // For a torque-limited setpoint, if the computation of the stop trajectory
  // fails based on the current acceleration limits due to small numerical
  // violations, it is recomputed based on the accelerations limits of the
  // previous cycle `previous_max_joint_acceleration_` to confirm if the stop
  // trajectory will violate positional limits or if the failure is only a
  // numerical artifact of the approximation. Thus, the member variable
  // `previous_max_joint_acceleration_` caches the acceleration limits of the
  // previous cycle. In the current cycle, they are used as follows:
  // - if the stop trajectory computation with the current acceleration
  // succeeds, then we just cache these values within
  // `previous_max_joint_acceleration_`.
  // - if the stop trajectory computation with the current acceleration fails,
  // we attempt to recompute it with `previous_max_joint_acceleration_`. From
  // here, we either cache the values or raise an error because the stop
  // trajectory would violate positional limits.
  eigenmath::VectorNd previous_max_joint_acceleration_;
};

}  // namespace intrinsic::icon
#endif  // INTRINSIC_ICON_CONTROL_PARTS_MOVE_CHECKER_H_
