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

#include "intrinsic/icon/control/actions/trajectory_tracker.h"

#include <cstddef>
#include <limits>
#include <memory>
#include <optional>
#include <utility>

#include "absl/log/check.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "intrinsic/eigenmath/clamp.h"
#include "intrinsic/icon/actions/trajectory_tracking_action_info.h"
#include "intrinsic/icon/dynamics/rigid_body_interface.h"
#include "intrinsic/icon/dynamics/robotics_library_dynamics_creator.h"
#include "intrinsic/icon/proto/cart_space_conversion.h"
#include "intrinsic/icon/utils/log.h"
#include "intrinsic/icon/utils/realtime_guard.h"
#include "intrinsic/icon/utils/realtime_status_macro.h"
#include "intrinsic/kinematics/chain.h"
#include "intrinsic/kinematics/proto/kinematics_conversion.h"
#include "intrinsic/kinematics/skeleton.h"
#include "intrinsic/kinematics/types/check_joint_limits.h"
#include "intrinsic/kinematics/types/dynamic_limits_check_mode.h"
#include "intrinsic/motion_planning/trajectory_planning/trajectory_utils.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic::icon {

namespace {

// Returns `true` if the trajectory is jerk-limited and false otherwise. The
// interpolation type is used to identify if the trajectory is
// acceleration-limited (kCubicPolynomial) or jerk-limited (kQuinticPolynomial).
bool IsTrajectoryJerkLimited(const JointTrajectoryPVA& trajectory) {
  return trajectory.interpolation_type() ==
         JointTrajectoryInterpolationType::kQuinticPolynomial;
}

// Designs the initial state residual for the `trajectory` and
// `previous_joint_setpoint` such that it guarantees continuity of the state. In
// general, this is computed as the difference between the
// `previous_joint_setpoint` and the `trajectory` initial state.
// - If the trajectory is jerk-limited, then the initial state residual is set
//   to the difference between the previous setpoint and the initial state
//   of the trajectory such that the acceleration is continuous.
// - If the trajectory is acceleration-limited, we consider continuity of the PV
// state only. The initial acceleration residual is set to zero.
RealtimeStatusOr<JointStatePVA> DesignInitialStateResidual(
    const JointTrajectoryPVA& trajectory,
    const JointStatePVA& previous_joint_setpoint) {
  if (trajectory.size() == 0) {
    return icon::InvalidArgumentError(
        "TrajectoryTracker requires a non-empty trajectory.");
  }
  if (previous_joint_setpoint.size() != trajectory.data().front().size()) {
    return icon::InvalidArgumentError(absl::StrCat(
        "Dimension mismatch for trajectory (", trajectory.data().front().size(),
        ") and setpoint (", previous_joint_setpoint.size(), ")."));
  }

  JointStatePVA initial_state_residual;
  INTRINSIC_RT_RETURN_IF_ERROR(
      initial_state_residual.SetSize(previous_joint_setpoint.size()));
  INTRINSIC_RT_ASSIGN_OR_RETURN(JointStatePVA trajectory_initial_state,
                                trajectory.DataAt(0));

  initial_state_residual.position =
      previous_joint_setpoint.position - trajectory_initial_state.position;
  initial_state_residual.velocity =
      previous_joint_setpoint.velocity - trajectory_initial_state.velocity;
  if (IsTrajectoryJerkLimited(trajectory)) {
    initial_state_residual.acceleration = previous_joint_setpoint.acceleration -
                                          trajectory_initial_state.acceleration;
  }
  return initial_state_residual;
}

}  // namespace

absl::StatusOr<std::unique_ptr<TrajectoryTracker>> TrajectoryTracker::Create(
    PartParams part, double frequency_hz) {
  INTRINSIC_ASSERT_NON_REALTIME();
  if (part.command_trajectory.size() == 0) {
    return absl::InvalidArgumentError(
        "TrajectoryTracker requires a non-empty trajectory.");
  }
  if (part.command_trajectory.data().front().position.size() !=
      part.system_limits.size()) {
    return absl::InvalidArgumentError(
        absl::StrCat("Dimension mismatch between trajectory, which is ",
                     part.command_trajectory.data().front().position.size(),
                     " and limits, which is ", part.system_limits.size()));
  }

  // Loop through discretized trajectory and check it against limits.
  for (size_t k = 0; k < part.command_trajectory.size(); ++k) {
    const JointStatePVA& state = part.command_trajectory.data().at(k);
    INTRINSIC_RT_ASSIGN_OR_RETURN(auto limit_check_result,
                                  IsWithinLimits(state, part.system_limits));
    if (!limit_check_result.p_ok) {
      return absl::InvalidArgumentError(
          absl::StrCat("Provided joint trajectory violates joint position "
                       "limits at index ",
                       k));
    } else if (!limit_check_result.v_ok) {
      return absl::InvalidArgumentError(
          absl::StrCat("Provided joint trajectory violates joint velocity "
                       "limits at index ",
                       k));
    } else if (!limit_check_result.a_ok &&
               part.command_trajectory.joint_dynamic_limits_check_mode() ==
                   DynamicLimitsCheckMode::kCheckJointAcceleration) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Provided joint trajectory violates joint acceleration limits at "
          "index ",
          k));
    }
  }

  // Check the trajectory against Cartesian limits if possible.
  if (part.generic_config.has_cartesian_limits_config() &&
      part.generic_config.has_manipulator_kinematics_config()) {
    INTR_ASSIGN_OR_RETURN(
        CartesianLimits cartesian_limits,
        icon::FromProto(part.generic_config.cartesian_limits_config()
                            .default_cartesian_limits()));
    INTR_ASSIGN_OR_RETURN(
        std::unique_ptr<intrinsic::kinematics::Skeleton> skeleton,
        intrinsic::kinematics::FromProto(
            part.generic_config.manipulator_kinematics_config().skeleton()));
    INTR_ASSIGN_OR_RETURN(kinematics::Chain chain,
                          kinematics::ExtractNonBranchingChain(*skeleton));
    INTR_RETURN_IF_ERROR(intrinsic::IsTrajectoryWithinCartesianPositionLimits(
        chain, cartesian_limits, part.command_trajectory));
  }

  // If the commanded trajectory is torque-limited, we construct a rigid body
  // interface for the trajectory player to be able to play it while
  // respecting torque-limits.
  std::unique_ptr<icon::RigidBodyInterface> rigid_body_interface = nullptr;
  if (part.command_trajectory.joint_dynamic_limits_check_mode() ==
          DynamicLimitsCheckMode::kCheckNone &&
      part.generic_config.has_manipulator_kinematics_config()) {
    INTR_ASSIGN_OR_RETURN(
        std::unique_ptr<intrinsic::kinematics::Skeleton> skeleton,
        intrinsic::kinematics::FromProto(
            part.generic_config.manipulator_kinematics_config().skeleton()));
    INTR_ASSIGN_OR_RETURN(
        rigid_body_interface,
        icon::CreateRoboticsLibraryDynamics(std::move(skeleton)));
  }

  INTR_ASSIGN_OR_RETURN(
      std::unique_ptr<TrajectoryResidualController> residual_controller,
      TrajectoryResidualController::Create(frequency_hz, part.planning_limits,
                                           part.system_limits));
  INTR_ASSIGN_OR_RETURN(
      std::unique_ptr<TrajectoryPlayer> trajectory_player,
      TrajectoryPlayer::Create(
          std::move(part.command_trajectory), absl::Seconds(1. / frequency_hz),
          part.system_limits, std::move(rigid_body_interface)));
  INTR_ASSIGN_OR_RETURN(std::unique_ptr<IsSettledCriterion> settled_criterion,
                        IsSettledCriterion::Create(frequency_hz));

  ArmPartState arm{
      .slot_id = part.slot_id,
      .residual_controller = std::move(residual_controller),
      .is_settled_criterion = std::move(settled_criterion),
      .planning_limits = part.planning_limits,
      .system_limits = part.system_limits,
  };

  return absl::WrapUnique(
      new TrajectoryTracker(std::move(trajectory_player), std::move(arm)));
}

TrajectoryTracker::TrajectoryTracker(
    std::unique_ptr<TrajectoryPlayer> trajectory_player, ArmPartState arm)
    : trajectory_player_(std::move(trajectory_player)), arm_(std::move(arm)) {}

void TrajectoryTracker::SetPathAccurateStopRequested() {
  // This is a one-way latch; once requested, path_accurate_stop_requested_
  // cannot be cleared.
  path_accurate_stop_requested_ = true;
  (void)trajectory_player_->SetDesiredSpeedOverrideFactor(0.0);
}

RealtimeStatus TrajectoryTracker::OnEnter(double global_speed_override,
                                          const RealtimeSlotMap& slot_map) {
  // The final state residual will be computed and used after the trajectory
  // has been finished.
  arm_.final_state_residual = std::nullopt;

  trajectory_player_->ResetPhase();
  // Initialize the trajectory player to currently active speed override
  // factors. This allows consistent Action switching despite speed overriding
  // being active.
  INTRINSIC_RT_RETURN_IF_ERROR(
      trajectory_player_->ForceSpeedOverrideFactor(global_speed_override));

  const JointPosition* const joint_position_interface =
      slot_map.GetInterfaceForSlot<JointPosition>(arm_.slot_id);
  if (joint_position_interface == nullptr) {
    return icon::InternalError("Slot doesn't have JointPosition.");
  }

  const JointPositionCommand& previous_position_setpoints =
      joint_position_interface->PreviousPositionSetpoints();
  const size_t ndof = previous_position_setpoints.position().size();

  JointStatePVA previous_joint_setpoint;
  INTRINSIC_RT_RETURN_IF_ERROR(previous_joint_setpoint.SetSize(ndof));
  previous_joint_setpoint.position = previous_position_setpoints.position();

  // If there is no velocity feedforward available in the previous command,
  // we set the starting velocity to the sensed velocity. This should only
  // happen once, in the very first control cycle of this action.
  const auto* sensed_velocity =
      slot_map.GetInterfaceForSlot<JointVelocityEstimator>(arm_.slot_id);
  if (sensed_velocity == nullptr) {
    return icon::InternalError("Slot does not have JointVelocityEstimator");
  }
  previous_joint_setpoint.velocity =
      previous_position_setpoints.velocity_feedforward().value_or(
          sensed_velocity->GetVelocityEstimate().velocity);

  // If there is no acceleration feedforward available in the previous
  // command, we set the starting acceleration to zero. This should only
  // happen once, in the very first control cycle of this action.
  // TODO(b/370698109): this can lead to very high jerk values and
  // potentially jerk limit violation.
  previous_joint_setpoint.acceleration =
      previous_position_setpoints.acceleration_feedforward().value_or(
          eigenmath::VectorNd::Zero(ndof));

  // Set the initial state residual.
  INTRINSIC_RT_ASSIGN_OR_RETURN(
      arm_.initial_state_residual,
      DesignInitialStateResidual(trajectory_player_->GetTrajectory(),
                                 previous_joint_setpoint));

  if (arm_.initial_state_residual.position.cwiseAbs().maxCoeff() >
      TrajectoryTrackingActionInfo::kMaxInitialJointPositionDeviation) {
    return icon::FailedPreconditionError(icon::RealtimeStatus::StrCat(
        "Difference between previous setpoint and position command (",
        arm_.initial_state_residual.position.cwiseAbs().maxCoeff(),
        ") exceeds ",
        TrajectoryTrackingActionInfo::kMaxInitialJointPositionDeviation));
  }

  if (arm_.initial_state_residual.velocity.cwiseAbs().maxCoeff() >
      TrajectoryTrackingActionInfo::kMaxInitialJointVelocityDeviation) {
    return icon::FailedPreconditionError(icon::RealtimeStatus::StrCat(
        "Difference between previous setpoint and velocity command (",
        arm_.initial_state_residual.velocity.cwiseAbs().maxCoeff(),
        ") exceeds ",
        TrajectoryTrackingActionInfo::kMaxInitialJointVelocityDeviation));
  }

  arm_.distance_to_final_setpoint = std::numeric_limits<double>::max();
  trajectory_done_for_seconds_ = 0.0;
  wall_time_passed_since_trajectory_start_ = 0.0;
  arm_.is_settled = false;
  INTRINSIC_RT_RETURN_IF_ERROR(arm_.is_settled_criterion->Initialize());

  return icon::OkStatus();
}

RealtimeStatus TrajectoryTracker::Sense(const RealtimeSlotMap& slot_map) {
  // Update distance to final setpoint.
  const auto* sensed_joint_position =
      slot_map.GetInterfaceForSlot<JointPositionSensor>(arm_.slot_id);
  if (sensed_joint_position == nullptr) {
    return icon::InternalError("Slot does not have JointPositionSensor");
  }
  arm_.distance_to_final_setpoint =
      (sensed_joint_position->GetSensedPosition().position -
       trajectory_player_->GetTrajectory().data().back().position)
          .norm();

  const bool is_done = IsDone();
  if (is_done) {
    INTRINSIC_RT_LOG_THROTTLED(INFO)
        << "Trajectory done for " << trajectory_done_for_seconds_
        << " seconds.";
    trajectory_done_for_seconds_ +=
        absl::ToDoubleSeconds(trajectory_player_->GetControlSamplingTime());
  }

  wall_time_passed_since_trajectory_start_ +=
      absl::ToDoubleSeconds(trajectory_player_->GetControlSamplingTime());

  // Update the settled state estimator with new measurements.
  const JointPosition* position_command =
      slot_map.GetInterfaceForSlot<JointPosition>(arm_.slot_id);
  if (position_command == nullptr) {
    return icon::FailedPreconditionError(
        "Slot does not have JointPosition feature interface");
  }
  CHECK(position_command->PreviousPositionSetpoints()
            .velocity_feedforward()
            .has_value());

  const auto* sensed_velocity =
      slot_map.GetInterfaceForSlot<JointVelocityEstimator>(arm_.slot_id);
  if (sensed_velocity == nullptr) {
    return icon::InternalError("Slot does not have JointVelocityEstimator");
  }

  INTRINSIC_RT_ASSIGN_OR_RETURN(
      arm_.is_settled, arm_.is_settled_criterion->Update(
                           sensed_velocity->GetVelocityEstimate().velocity,
                           position_command->PreviousPositionSetpoints()
                               .velocity_feedforward()
                               .value(),
                           /*has_trajectory_ended=*/is_done));
  if (arm_.is_settled) {
    INTRINSIC_RT_LOG_THROTTLED(INFO)
        << "Trajectory settled (slot_id: " << arm_.slot_id.value() << ").";
  }

  return icon::OkStatus();
}

RealtimeStatus TrajectoryTracker::Control(double combined_speed_override_factor,
                                          RealtimeSlotMap& slot_map) {
  JointPosition* const joint_position_interface =
      slot_map.GetMutableInterfaceForSlot<JointPosition>(arm_.slot_id);
  if (joint_position_interface == nullptr) {
    return icon::InternalError("Slot doesn't have JointPosition.");
  }

  // If a path accurate stop is requested, the speed override is overwritten
  // to be zero.
  const double effective_speed_override_factor =
      combined_speed_override_factor *
      static_cast<double>(!path_accurate_stop_requested_);
  INTRINSIC_RT_RETURN_IF_ERROR(
      trajectory_player_->SetDesiredSpeedOverrideFactor(
          effective_speed_override_factor));

  // TODO(b/370891053): kIsDone should also account for the internal state of
  // the residual controller.
  const bool is_done = IsDone();

  // Evaluate the new reference state according to the trajectory.
  INTRINSIC_RT_ASSIGN_OR_RETURN(JointStatePVA new_trajectory_reference_state,
                                trajectory_player_->GetNextState());

  const JointTrajectoryPVA& trajectory = trajectory_player_->GetTrajectory();

  if (is_done) {
    // We need to account for the fact that the commanded trajectory may not
    // exactly end with zero velocity or acceleration. Since we should
    // effectively reach a steady-state if there is no follow-up action, we
    // need to actively enforce zero velocity and acceleration once the full
    // trajectory has been played back and is therefore "done". The residual
    // controller is then used to eliminate a "final" residual, e.g. a
    // non-zero terminal velocity/acceleration in the trajectory. We
    // initialize the final residual controller once.
    if (!arm_.final_state_residual.has_value()) {
      arm_.final_state_residual = JointStatePVA();
      INTRINSIC_RT_RETURN_IF_ERROR(arm_.final_state_residual->SetSize(
          new_trajectory_reference_state.size()));
      // We would like to converge to the position of the last trajectory
      // datapoint, so the final residual position just needs to take over
      // the remaining "initial" position residual.
      arm_.final_state_residual->position =
          arm_.initial_state_residual.position;
      // Start from the final velocity and acceleration setpoint
      // interpolated from the trajectory, and add the remaining residual
      // from the initial state. This total residual will subsequently be
      // brought to zero using the full planning limits.
      arm_.final_state_residual->velocity =
          new_trajectory_reference_state.velocity +
          arm_.initial_state_residual.velocity;
      if (IsTrajectoryJerkLimited(trajectory)) {
        arm_.final_state_residual->acceleration =
            new_trajectory_reference_state.acceleration +
            arm_.initial_state_residual.acceleration;
      } else {
        arm_.final_state_residual->acceleration.setZero();
      }
    }
    // New reference velocity and acceleration get set to zero. This needs
    // to happen after the initialization of the final_state_residual_.
    new_trajectory_reference_state.velocity.setZero();
    new_trajectory_reference_state.acceleration.setZero();
  }

  JointStatePVA filtered_reference = new_trajectory_reference_state;
  // The actual command is a superposition of reference state and residual.
  // Update and include initial state residual as long as the trajectory has
  // not been played back to its end.
  if (!arm_.final_state_residual.has_value()) {
    INTRINSIC_RT_ASSIGN_OR_RETURN(
        arm_.initial_state_residual,
        arm_.residual_controller->ComputeResidualUsingLimitMargin(
            arm_.initial_state_residual));
    filtered_reference.position += arm_.initial_state_residual.position;
    filtered_reference.velocity += arm_.initial_state_residual.velocity;
    filtered_reference.acceleration += arm_.initial_state_residual.acceleration;
  } else {
    // Final state of the trajectory has been reached, update and include
    // final state residual. Since the residual compensation remains the
    // only motion, use planning limits.
    INTRINSIC_RT_ASSIGN_OR_RETURN(
        arm_.final_state_residual,
        arm_.residual_controller->ComputeResidualUsingPlanningLimits(
            *arm_.final_state_residual));
    filtered_reference.position += arm_.final_state_residual->position;
    filtered_reference.velocity += arm_.final_state_residual->velocity;
    filtered_reference.acceleration += arm_.final_state_residual->acceleration;
  }

  // Polynomial splining (as used by the trajectory player) may lead to
  // slight oscillations at the acceleration level, which might sometimes
  // surpass the maximum acceleration limits. This is not a critical
  // failure, can be tolerated, and trajectory execution may proceed as
  // intended. To avoid a failure at the HAL level (which strictly checks
  // the acceleration), we saturate the joint acceleration command ONLY for
  // acceleration-limited trajectories. In jerk-limited trajectories, this
  // is not enforced as it could induce an actual jerk-limit violation.
  // TODO(b/371952663): Remove once fine interpolation for acceleration
  // limited trajectories is limit-aware at the acceleration level.
  if (!IsTrajectoryJerkLimited(trajectory)) {
    if (!eigenmath::ClampVector(-arm_.system_limits.max_acceleration,
                                arm_.system_limits.max_acceleration,
                                filtered_reference.acceleration)) {
      return icon::InternalError(
          "Clamping joint accelerations to limits failed.");
    }
  }

  INTRINSIC_RT_ASSIGN_OR_RETURN(
      JointPositionCommand new_joint_position_command,
      JointPositionCommand::Create(
          filtered_reference.position, filtered_reference.velocity,
          filtered_reference.acceleration,
          trajectory.joint_dynamic_limits_check_mode()));
  return joint_position_interface->SetPositionSetpoints(
      new_joint_position_command);
}

bool TrajectoryTracker::IsDone() const {
  if (path_accurate_stop_requested_ &&
      trajectory_player_->SpeedOverrideStateHasConverged()) {
    return true;
  }

  constexpr double kDoubleEqThreshold = 1.0e-14;
  const double phase = trajectory_player_->GetPhase();
  return 1.0 - phase < kDoubleEqThreshold;
}

RealtimeStatusOr<double> TrajectoryTracker::GetCartesianArcLength() const {
  return trajectory_player_->GetCartesianArcLength();
}

}  // namespace intrinsic::icon
