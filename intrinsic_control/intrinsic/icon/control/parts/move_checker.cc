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

#include "intrinsic/icon/control/parts/move_checker.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <utility>

#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/control/algorithms/joint_stop_trajectory.h"
#include "intrinsic/icon/control/collision/robot_collision_checker.h"
#include "intrinsic/icon/control/joint_position_command.h"
#include "intrinsic/icon/control/parts/feature_interfaces.h"
#include "intrinsic/icon/control/realtime_bridge_types.h"
#include "intrinsic/icon/utils/fixed_str_cat.h"
#include "intrinsic/icon/utils/log.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/icon/utils/realtime_status_macro.h"
#include "intrinsic/icon/utils/realtime_status_or.h"
#include "intrinsic/kinematics/types/check_joint_limits.h"
#include "intrinsic/kinematics/types/dynamic_limits_check_mode.h"
#include "intrinsic/kinematics/types/joint_limits.h"
#include "intrinsic/kinematics/types/joint_state.h"
#include "intrinsic/kinematics/types/to_fixed_string.h"

namespace intrinsic::icon {
namespace {

// Checks if velocity and/or acceleration feedforward are missing from the
// setpoint and if so calculates them based on a first order backward Euler.
// TODO(b/261967353) numerical derivatives may be different from the ones
// computed by ArmPart, therefore setpoints accepted by the MoveChecker may be
// rejected by the ArmPart
icon::RealtimeStatusOr<JointStatePVAJ> AddMissingDerivativesToCommand(
    const JointPositionCommand& setpoint,
    const JointPositionCommand& previous_setpoint,
    double control_frequency_hz) {
  JointStatePVAJ setpoint_state;
  INTRINSIC_RT_RETURN_IF_ERROR(setpoint_state.SetSize(setpoint.Size()));
  setpoint_state.position = setpoint.position();

  // Use a first order backward Euler to infer the velocities and
  // accelerations
  setpoint_state.velocity = setpoint.velocity_feedforward().value_or(
      (setpoint.position() - previous_setpoint.position()) *
      control_frequency_hz);
  if (!previous_setpoint.velocity_feedforward().has_value()) {
    return FailedPreconditionError(
        "Cannot compute acceleration since previous JointPositionCommand "
        "lacks a velocity_feedforward");
  }
  setpoint_state.acceleration = setpoint.acceleration_feedforward().value_or(
      (setpoint_state.velocity -
       previous_setpoint.velocity_feedforward().value()) *
      control_frequency_hz);

  // Compute a jerk estimate, but only if the previous setpoint had acceleration
  // (avoid too much noise through cascaded numerical differentiation).
  if (previous_setpoint.acceleration_feedforward().has_value()) {
    setpoint_state.jerk =
        (setpoint_state.acceleration -
         previous_setpoint.acceleration_feedforward().value()) *
        control_frequency_hz;
  }

  return setpoint_state;
}

// Checks that the `setpoint_state` satisfies `joint_limits`. If the
// `joint_dynamic_limits_check_mode` is set to kJointAccelerationDefault, then
// it checks that the setpoint acceleration satisfies joint acceleration
// limits. Otherwise, it skips that check.
icon::RealtimeStatus ValidateSetpoint(
    const JointStatePVAJ& setpoint_state, const JointLimits& joint_limits,
    const DynamicLimitsCheckMode& joint_dynamic_limits_check_mode,
    const MoveCheckerExtraConfig& extra_config) {
  INTRINSIC_RT_ASSIGN_OR_RETURN(auto limit_check_result,
                                IsWithinLimits(setpoint_state, joint_limits));
  if (!limit_check_result.v_ok) {
    INTRINSIC_RT_LOG(ERROR)
        << "Queried setpoint violated the velocity joint limits. The setpoint "
           "velocity was: ["
        << eigenmath::ToFixedString(setpoint_state.velocity)
        << "] and the limits are: ["
        << eigenmath::ToFixedString(joint_limits.max_velocity) << "] ";
    return InvalidArgumentError(
        "Queried setpoint violated the velocity joint limits.");
  }
  if (!limit_check_result.a_ok &&
      joint_dynamic_limits_check_mode ==
          DynamicLimitsCheckMode::kCheckJointAcceleration) {
    INTRINSIC_RT_LOG(ERROR)
        << "Queried setpoint violated the acceleration joint limits. The "
           "setpoint acceleration was: ["
        << eigenmath::ToFixedString(setpoint_state.acceleration)
        << "] and the limits are: ["
        << eigenmath::ToFixedString(joint_limits.max_acceleration) << "] ";
    return InvalidArgumentError(
        "Queried setpoint violated the acceleration joint limits.");
  }

  if (!limit_check_result.j_ok &&
      extra_config.error_on_jerk_system_limit_violation) {
    INTRINSIC_RT_LOG(ERROR)
        << "Queried setpoint violated the jerk joint limits. The setpoint jerk "
           "was: ["
        << eigenmath::ToFixedString(setpoint_state.jerk)
        << "] but the limits to check against were: ["
        << eigenmath::ToFixedString(joint_limits.max_jerk) << "]";
    return InvalidArgumentError(
        "Queried setpoint violated the jerk joint limits.");
  }

  return OkStatus();
}

// Checks whether the queried position setpoints are continuous with the
// previous setpoints considering the `joint_limits_maximum` velocity limits.
// TODO(b/262233709) There is duplicate logic here and in the l1_controller
// which should be merged.
RealtimeStatus CheckInputContinuity(
    const eigenmath::VectorNd& target_position,
    const JointPositionCommand& previous_setpoint,
    const JointLimits& maximum_joint_limits, double velocity_input_margin,
    double control_frequency_hz) {
  if (target_position.size() != previous_setpoint.Size()) {
    return InvalidArgumentError(
        "The size of the target_position does not match the size of the "
        "previous setpoint.");
  }
  if (target_position.size() != maximum_joint_limits.size()) {
    return InvalidArgumentError(
        "The size of the target_position does not match the size of the "
        "joint limits.");
  }
  eigenmath::VectorNd velocity_fd =
      (target_position - previous_setpoint.position()) * control_frequency_hz;
  if (!velocity_fd.allFinite()) {
    return InvalidArgumentError(
        "Queried command produced non-finite position continuity velocity.");
  }
  if ((velocity_fd.array() > (1.0 + velocity_input_margin) *
                                 maximum_joint_limits.max_velocity.array())
          .any() ||
      (velocity_fd.array() < -(1.0 + velocity_input_margin) *
                                 maximum_joint_limits.max_velocity.array())
          .any()) {
    INTRINSIC_RT_LOG(ERROR)
        << "Queried target position violated the continuity in position. "
           "The target position was: ["
        << eigenmath::ToFixedString(target_position)
        << "] and the previous setpoint was: ["
        << eigenmath::ToFixedString(previous_setpoint.position())
        << "] which violated the maximum velocity joint limits: ["
        << eigenmath::ToFixedString(maximum_joint_limits.max_velocity) << "]";
    return InvalidArgumentError(
        "Queried command violated continuity in position setpoint.");
  }
  return OkStatus();
}

// Checks whether the `stop_position` violates the position limits, and if so,
// whether it is moving towards safety. The stop position is compared to the
// target position to determine the direction of motion of the stop trajectory
// with respect to the limits.
icon::RealtimeStatusOr<bool> IsStopPositionWithinLimits(
    const eigenmath::VectorNd& stop_position,
    const eigenmath::VectorNd& previous_position,
    const JointLimits& joint_limits) {
  if (stop_position.size() != previous_position.size()) {
    return InvalidArgumentError(
        "The size of the stop_position does not match the size of the "
        "previous_position.");
  }
  if (stop_position.size() != joint_limits.size()) {
    return InvalidArgumentError(
        "The size of the stop_position does not match the size of the "
        "joint_limits.");
  }
  if (!stop_position.allFinite()) {
    return false;
  }
  eigenmath::VectorNd position_max_limit_overshoot =
      (stop_position - joint_limits.max_position).cwiseMax(0);

  if (((position_max_limit_overshoot.array() > 0) &&
       (stop_position.array() >
        previous_position.array() + MoveChecker::kDirectionViolationTolerance))
          .any()) {
    return false;
  }
  eigenmath::VectorNd position_min_limit_undershoot =
      (stop_position - joint_limits.min_position).cwiseMin(0);
  if (((position_min_limit_undershoot.array() < 0) &&
       (stop_position.array() <
        previous_position.array() - MoveChecker::kDirectionViolationTolerance))
          .any()) {
    return false;
  }
  return true;
}

// Returns the maximum joint acceleration limits to compute a stop trajectory.
// For an acceleration-limited `setpoint`, it returns
// `joint_limits.max_acceleration`. For a torque-limited setpoint it computes
// the maximum acceleration limits as follows: if
// `joint_acceleration_limits_from_dynamics` holds a value, then this is used.
// If not, the maximum acceleration limits come from the component-wise maximum
// between `joint_limits.max_acceleration` and the absolute value of the current
// acceleration within `setpoint_state_with_derivatives`.
eigenmath::VectorNd ComputeStoppingMaxJointAcceleration(
    const JointLimits& joint_limits, const JointPositionCommand& setpoint,
    const JointStatePVA& setpoint_state_with_derivatives,
    const std::optional<
        JointLimitsInterface::JointAccelerationLimitsFromDynamics>
        joint_acceleration_limits_from_dynamics) {
  if (setpoint.joint_dynamic_limits_check_mode() ==
      DynamicLimitsCheckMode::kCheckJointAcceleration) {
    return eigenmath::VectorNd(joint_limits.max_acceleration);
  }

  // For torque-limited setpoints, max joint acceleration limits are computed
  // with this priority:
  // 1) `joint_acceleration_limits_from_dynamics`.
  // 2) Component-wise maximum between `joint_limits.max_acceleration` and
  //    'setpoint_state_with_derivatives.acceleration'.
  // If option 1 is not available for a torque-limited setpoint, we still want
  // to be able to stop in an appropriate time and motion distance, thus we
  // use the maximum current valid acceleration values available. Note that
  // option 2 assumes that 'setpoint_state_with_derivatives.acceleration' is
  // dynamically valid. Given that a torque-limited setpoint can violate
  // `joint_limits.max_acceleration`, we disable acceleration limit checking in
  // ICON. This means whoever provides torque-limited setpoints assumes
  // responsibility for sending setpoints that won't harm the robot.

  // We add a safety margin to the max joint acceleration limits used to
  // predict the stop trajectory for torque-limited motions, because the
  // `stop_trajectory_` used within the MoveChecker is acceleration-limited
  // only, while the actual computation of the stop commands is jerk limited.
  // The safety percentage added to the acceleration-limited computation of the
  // stop trajectory was selected to make it more conservative, such that it
  // matches more closely the actual stop trajectory coming from the stop
  // action, which is jerk-limited.
  const double kSafetyPercentage = 0.98;

  eigenmath::VectorNd max_joint_acceleration;
  if (joint_acceleration_limits_from_dynamics.has_value()) {
    const eigenmath::VectorNd& acc_at_min_trq =
        joint_acceleration_limits_from_dynamics
            ->joint_acceleration_limits_at_min_torque;
    const eigenmath::VectorNd& acc_at_max_trq =
        joint_acceleration_limits_from_dynamics
            ->joint_acceleration_limits_at_max_torque;
    // As accelerations at min and max torques will not be symmetrical in
    // general, we use the maximum valid deceleration based on the velocity
    // sign. In other words, we select the maximum acceleration value in the
    // opposite sense of the velocity. Furthermore, we only take a percentage of
    // it given by kSafetyPercentage to have a safety margin.
    max_joint_acceleration =
        kSafetyPercentage *
        (setpoint_state_with_derivatives.velocity.array() < 0.0)
            .select(acc_at_min_trq.cwiseMax(acc_at_max_trq),
                    acc_at_min_trq.cwiseMin(acc_at_max_trq))
            .cwiseAbs();
  } else {
    // If any joint is currently accelerating faster than its limit, assume that
    // it is allowed to keep doing that (in both directions!) for the
    // computation of the current maximum deceleration used to compute the stop
    // trajectory. This is a way to stop as fast as possible, given that
    // `joint_limits.max_acceleration` are very conservative for torque-limited
    // setpoints.
    max_joint_acceleration = joint_limits.max_acceleration.cwiseMax(
        kSafetyPercentage *
        setpoint_state_with_derivatives.acceleration.cwiseAbs());
  }

  return max_joint_acceleration;
}

icon::RealtimeStatusOr<eigenmath::VectorNd> ComputeStopPosition(
    const JointPositionCommand& setpoint_state_with_derivatives,
    const eigenmath::VectorNd max_joint_acceleration,
    control::JointStopTrajectory& stop_trajectory) {
  // Use `max_joint_acceleration` to determine the fastest stopping trajectory.
  if (!stop_trajectory.SetAcceleration(max_joint_acceleration)) {
    return InternalError("Failed to set acceleration for stop trajectory.");
  }

  const int num_dofs = stop_trajectory.NumDoFs();
  if (!stop_trajectory.SetInitialConditions(
          /*position=*/setpoint_state_with_derivatives.position(),
          /*velocity=*/
          setpoint_state_with_derivatives.velocity_feedforward().value_or(
              eigenmath::VectorNd::Zero(num_dofs)),
          /*time+*/ 0.0)) {
    return InternalError(
        "Failed to set initial conditions for stop trajectory while checking "
        "move.");
  }

  eigenmath::VectorNd stop_position(num_dofs);
  if (!stop_trajectory.GetStopPositionAndTime(/*position=*/&stop_position,
                                              /*time=*/nullptr)) {
    return InternalError(
        "Failed to get the stop position for the stop trajectory while "
        "checking move.");
  }
  return stop_position;
}
}  // namespace

MoveChecker::MoveChecker(
    size_t num_dofs, double control_frequency_hz,
    std::unique_ptr<control::JointStopTrajectory> stop_trajectory,
    std::unique_ptr<collision::RobotCollisionChecker> collision_checker)
    : num_dofs_(num_dofs),
      control_frequency_hz_(control_frequency_hz),
      stop_trajectory_(std::move(stop_trajectory)),
      collision_checker_(std::move(collision_checker)) {
  previous_max_joint_acceleration_ = eigenmath::VectorNd::Zero(num_dofs);
}

absl::StatusOr<std::unique_ptr<MoveChecker>> MoveChecker::Create(
    size_t num_dofs, double control_frequency_hz,
    std::unique_ptr<collision::RobotCollisionChecker> collision_checker) {
  auto stop_trajectory = std::make_unique<control::JointStopTrajectory>();
  if (!stop_trajectory->Init(num_dofs)) {
    return InternalError("Failed to initialize stop trajectory.");
  }
  if (collision_checker != nullptr && !collision_checker->IsValid()) {
    return FailedPreconditionError("The collision_checker is not valid.");
  }
  // Using `new` to access a non-public constructor.
  return absl::WrapUnique(new MoveChecker(num_dofs, control_frequency_hz,
                                          std::move(stop_trajectory),
                                          std::move(collision_checker)));
}

icon::RealtimeStatus MoveChecker::UpdatePreviousSetpoint(
    const JointPositionCommand& last_setpoint) {
  if (last_setpoint.Size() != num_dofs_) {
    return InvalidArgumentError(
        "The size of the previous setpoint does not match the num_dofs_ for "
        "the MoveChecker.");
  }
  previous_setpoint_ = last_setpoint;
  return OkStatus();
}

icon::RealtimeStatusOr<double> MoveChecker::MinCollisionDistance(
    const eigenmath::VectorNd& position) const {
  if (!collision_checker_) {
    return FailedPreconditionError(
        "Tried to check collision but no RobotCollisionChecker found.");
  }
  // Compute collision data
  INTRINSIC_RT_RETURN_IF_ERROR(
      collision_checker_->UpdateCollisionState(position));

  // Return min collision distance among all links, if valid
  return collision_checker_->GetMinCollisionDistance();
}

icon::RealtimeStatus MoveChecker::CheckStopPositionForCollisionViolations(
    const eigenmath::VectorNd& stop_position) {
  if (stop_position.size() != previous_setpoint_->position().size()) {
    return InvalidArgumentError(
        "The size of the stop_position does not match the size of the "
        "previous_position.");
  }
  INTRINSIC_RT_ASSIGN_OR_RETURN(double stop_position_min_collision_distance,
                                MinCollisionDistance(stop_position));
  if (stop_position_min_collision_distance < 0.0) {
    INTRINSIC_RT_ASSIGN_OR_RETURN(
        double previous_pos_min_collision_distance,
        MinCollisionDistance(previous_setpoint_->position()));
    // Check whether it is moving away from collision
    if (stop_position_min_collision_distance <
        previous_pos_min_collision_distance) {
      return InvalidArgumentError(
          "The provided setpoint is in collision and is not moving towards "
          "safety.");
    }
  }
  return OkStatus();
}

icon::RealtimeStatus MoveChecker::CheckSetpoint(
    const JointPositionCommand& setpoint, const JointLimits& joint_limits,
    const JointLimits& maximum_joint_limits,
    const std::optional<
        JointLimitsInterface::JointAccelerationLimitsFromDynamics>
        joint_acceleration_limits_from_dynamics,
    const MoveCheckerExtraConfig& extra_config) {
  if (!(extra_config.max_limit_violation_tolerance >= 0.0 &&
        extra_config.max_limit_violation_tolerance <=
            kMaxLimitViolationTolerance)) {
    return icon::InvalidArgumentError(RealtimeStatus::StrCat(
        "'max_limit_violation_tolerance' out of bound, got ",
        extra_config.max_limit_violation_tolerance,
        " but expected it to be in the range [0, ", kMaxLimitViolationTolerance,
        "]"));
  }

  INTRINSIC_RT_ASSIGN_OR_RETURN(
      auto limit_check_result,
      IsWithinLimits(joint_limits, maximum_joint_limits));
  if (!limit_check_result) {
    return InvalidArgumentError(
        "The provided joint limits are inconsistent since the `joint_limits` "
        "violate the `maximum_joint_limits`");
  }

  if (!previous_setpoint_.has_value()) {
    return FailedPreconditionError(
        "Tried to check a setpoint but no saved previous setpoint is present.");
  }

  if (setpoint.Size() != num_dofs_) {
    return InvalidArgumentError(
        "The size of the setpoint does not match the num_dofs_ for the "
        "MoveChecker.");
  }
  if (!setpoint.position().allFinite()) {
    return InvalidArgumentError("Setpoint position must be finite.");
  }
  if (setpoint.velocity_feedforward().has_value() &&
      !setpoint.velocity_feedforward()->allFinite()) {
    return InvalidArgumentError(
        "Setpoint velocity_feedforward must be finite.");
  }
  if (setpoint.acceleration_feedforward().has_value() &&
      !setpoint.acceleration_feedforward()->allFinite()) {
    return InvalidArgumentError(
        "Setpoint acceleration_feedforward must be finite.");
  }

  if (joint_acceleration_limits_from_dynamics.has_value()) {
    if (joint_acceleration_limits_from_dynamics
            ->joint_acceleration_limits_at_min_torque.size() != num_dofs_) {
      return InvalidArgumentError(
          "The size of `joint_acceleration_limits_at_min_torque` does not "
          "match the num_dofs_ for the MoveChecker.");
    }
    if (joint_acceleration_limits_from_dynamics
            ->joint_acceleration_limits_at_max_torque.size() != num_dofs_) {
      return InvalidArgumentError(
          "The size of `joint_acceleration_limits_at_max_torque` does not "
          "match the num_dofs_ for the MoveChecker.");
    }
  }

  INTRINSIC_RT_ASSIGN_OR_RETURN(
      const JointStatePVAJ setpoint_state,
      AddMissingDerivativesToCommand(setpoint, previous_setpoint_.value(),
                                     control_frequency_hz_));
  if (!setpoint_state.position.allFinite() ||
      !setpoint_state.velocity.allFinite() ||
      !setpoint_state.acceleration.allFinite() ||
      (extra_config.error_on_jerk_system_limit_violation &&
       !setpoint_state.jerk.allFinite())) {
    return InvalidArgumentError(
        "Computed setpoint state contains non-finite values.");
  }

  // We check against a slightly relaxed variant of maximum_joint_limits to
  // allow for small numerical deviations from the system limits.
  JointLimits relaxed_maximum_joint_limits = maximum_joint_limits;
  relaxed_maximum_joint_limits.max_velocity *=
      (1.0 + extra_config.max_limit_violation_tolerance);
  relaxed_maximum_joint_limits.max_acceleration *=
      (1.0 + extra_config.max_limit_violation_tolerance);
  relaxed_maximum_joint_limits.max_jerk *=
      (1.0 + extra_config.max_limit_violation_tolerance);

  // The `dynamics_limits_type` specifies whether joint acceleration limits
  // should be checked or not.
  INTRINSIC_RT_RETURN_IF_ERROR(ValidateSetpoint(
      setpoint_state, relaxed_maximum_joint_limits,
      setpoint.joint_dynamic_limits_check_mode(), extra_config));

  INTRINSIC_RT_ASSIGN_OR_RETURN(
      auto new_setpoint, JointPositionCommand::Create(
                             setpoint_state.position, setpoint_state.velocity,
                             setpoint_state.acceleration));
  INTRINSIC_RT_RETURN_IF_ERROR(
      CheckInputContinuity(new_setpoint.position(), previous_setpoint_.value(),
                           relaxed_maximum_joint_limits, kVelocityInputMargin,
                           control_frequency_hz_));

  eigenmath::VectorNd max_joint_acceleration =
      ComputeStoppingMaxJointAcceleration(
          joint_limits, setpoint, setpoint_state,
          joint_acceleration_limits_from_dynamics);

  INTRINSIC_RT_ASSIGN_OR_RETURN(
      eigenmath::VectorNd stop_position,
      ComputeStopPosition(new_setpoint, max_joint_acceleration,
                          *stop_trajectory_));

  INTRINSIC_RT_ASSIGN_OR_RETURN(
      bool satisfies_limits,
      IsStopPositionWithinLimits(stop_position, previous_setpoint_->position(),
                                 joint_limits));

  // For torque-limited setpoints, if the stop position does not satisfy
  // positional limits, we attempt once more to perform the validation with the
  // component-wise maximum between `previous_max_joint_acceleration_` and
  // `max_joint_acceleration`. Because the violation can be minor and only due
  // to the nature of the numerical approximation of the stop trajectory, this
  // improves recursive feasibility checks of the stop position for
  // torque-limited setpoints.
  if (setpoint.joint_dynamic_limits_check_mode() ==
          DynamicLimitsCheckMode::kCheckNone &&
      !satisfies_limits) {
    max_joint_acceleration =
        max_joint_acceleration.cwiseMax(previous_max_joint_acceleration_);
    INTRINSIC_RT_ASSIGN_OR_RETURN(
        stop_position, ComputeStopPosition(new_setpoint, max_joint_acceleration,
                                           *stop_trajectory_));

    INTRINSIC_RT_ASSIGN_OR_RETURN(
        satisfies_limits,
        IsStopPositionWithinLimits(
            stop_position, previous_setpoint_->position(), joint_limits));
  }

  if (!satisfies_limits) {
    return InvalidArgumentError(
        "Stop trajectory ends outside of position limits and does not move "
        "towards safety.");
  }
  previous_max_joint_acceleration_ = max_joint_acceleration;

  if (collision_checker_ != nullptr) {
    INTRINSIC_RT_RETURN_IF_ERROR(
        CheckStopPositionForCollisionViolations(stop_position));
  }

  return OkStatus();
}
}  // namespace intrinsic::icon
