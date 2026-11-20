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
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "intrinsic/eigenmath/clamp.h"
#include "intrinsic/icon/actions/trajectory_tracking_action_info.h"
#include "intrinsic/icon/control/actions/trajectory_tracking_utils.h"
#include "intrinsic/icon/dynamics/rigid_body_interface.h"
#include "intrinsic/icon/dynamics/robotics_library_dynamics_creator.h"
#include "intrinsic/icon/proto/cart_space_conversion.h"
#include "intrinsic/icon/utils/log.h"
#include "intrinsic/icon/utils/realtime_guard.h"
#include "intrinsic/icon/utils/realtime_status_macro.h"
#include "intrinsic/kinematics/chain.h"
#include "intrinsic/kinematics/proto/kinematics_conversion.h"
#include "intrinsic/kinematics/skeleton.h"
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

// Designs the initial state residual for the `[joint_offset, joint_offset +
// ndof)` slice of `trajectory` and `previous_joint_setpoint` such that it
// guarantees continuity of the state. In general, this is computed as the
// difference between the `previous_joint_setpoint` and the `trajectory` initial
// state.
// - If the trajectory is jerk-limited, then the initial state residual is set
//   to the difference between the previous setpoint and the initial state
//   of the trajectory such that the acceleration is continuous.
// - If the trajectory is acceleration-limited, we consider continuity of the PV
// state only. The initial acceleration residual is set to zero.
RealtimeStatusOr<JointStatePVA> DesignInitialStateResidual(
    const JointTrajectoryPVA& trajectory, const size_t joint_offset,
    const size_t ndof, const JointStatePVA& previous_joint_setpoint) {
  if (trajectory.size() == 0) {
    return InvalidArgumentError(
        "TrajectoryTracker requires a non-empty trajectory.");
  }
  if (previous_joint_setpoint.size() != ndof) {
    return InvalidArgumentError(RealtimeStatus::StrCat(
        "Dimension mismatch for trajectory (", ndof, ") and setpoint (",
        previous_joint_setpoint.size(), ")."));
  }

  JointStatePVA initial_state_residual;
  INTRINSIC_RT_RETURN_IF_ERROR(
      initial_state_residual.SetSize(previous_joint_setpoint.size()));
  INTRINSIC_RT_ASSIGN_OR_RETURN(JointStatePVA trajectory_initial_state,
                                trajectory.DataAt(0));
  if (joint_offset + ndof > trajectory_initial_state.size()) {
    return InvalidArgumentError(RealtimeStatus::StrCat(
        "Slice [", joint_offset, ", ", joint_offset + ndof,
        ") exceeds trajectory dimension (", trajectory_initial_state.size(),
        ")."));
  }

  initial_state_residual.position =
      previous_joint_setpoint.position -
      trajectory_initial_state.position.segment(joint_offset, ndof);
  initial_state_residual.velocity =
      previous_joint_setpoint.velocity -
      trajectory_initial_state.velocity.segment(joint_offset, ndof);
  if (IsTrajectoryJerkLimited(trajectory)) {
    initial_state_residual.acceleration =
        previous_joint_setpoint.acceleration -
        trajectory_initial_state.acceleration.segment(joint_offset, ndof);
  }
  return initial_state_residual;
}

}  // namespace

absl::StatusOr<std::unique_ptr<TrajectoryTracker>> TrajectoryTracker::Create(
    std::vector<PartParams> parts, const double frequency_hz) {
  INTRINSIC_ASSERT_NON_REALTIME();
  if (parts.empty()) {
    return absl::InvalidArgumentError(
        "TrajectoryTracker requires at least one part.");
  }
  if (parts.size() > kMaxNumParts) {
    return absl::InvalidArgumentError(
        absl::StrCat("TrajectoryTracker supports at most ", kMaxNumParts,
                     " parts (got ", parts.size(), ")."));
  }

  absl::flat_hash_map<RealtimeSlotId, size_t> part_index_by_slot_id;
  FixedVector<ArmPartState, kMaxNumParts> arm_part_states;
  std::vector<TrajectoryAndLimits> part_trajectories_and_limits;
  part_trajectories_and_limits.reserve(parts.size());
  size_t current_joint_offset = 0;

  for (size_t i = 0; i < parts.size(); ++i) {
    PartParams& part = parts[i];
    if (parts.size() > 1 &&
        part.command_trajectory.joint_dynamic_limits_check_mode() ==
            DynamicLimitsCheckMode::kCheckNone) {
      return absl::UnimplementedError(absl::StrCat(
          "Multi-part trajectory tracking with "
          "DynamicLimitsCheckMode::kCheckNone (torque-limit checking) is not "
          "supported (part ",
          i, ", slot ", part.slot_id.value(), ")."));
    }
    const auto [existing, inserted] =
        part_index_by_slot_id.try_emplace(part.slot_id, i);
    if (!inserted) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Duplicate slot_id ", part.slot_id.value(), " found between part ",
          existing->second, " and part ", i, "."));
    }
    if (!part.planning_limits.IsSizeConsistent()) {
      return absl::InvalidArgumentError(
          absl::StrCat("Planning limits are inconsistent in size (part ", i,
                       ", slot ", part.slot_id.value(), ")."));
    }
    if (part.planning_limits.size() != part.system_limits.size()) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Dimension mismatch between planning limits, which is ",
          part.planning_limits.size(), " and system limits, which is ",
          part.system_limits.size(), " (part ", i, ", slot ",
          part.slot_id.value(), ")."));
    }
    if (part.command_trajectory.size() == 0) {
      return absl::InvalidArgumentError(absl::StrCat(
          "TrajectoryTracker requires a non-empty trajectory (part ", i,
          ", slot ", part.slot_id.value(), ")."));
    }
    if (part.command_trajectory.data().front().position.size() !=
        part.system_limits.size()) {
      return absl::InvalidArgumentError(
          absl::StrCat("Dimension mismatch between trajectory, which is ",
                       part.command_trajectory.data().front().position.size(),
                       " and limits, which is ", part.system_limits.size(),
                       " (part ", i, ", slot ", part.slot_id.value(), ")."));
    }

    // Check the trajectory against Cartesian limits if possible.
    if (part.generic_config.has_cartesian_limits_config() &&
        part.generic_config.has_manipulator_kinematics_config()) {
      INTR_ASSIGN_OR_RETURN(
          CartesianLimits cartesian_limits,
          FromProto(part.generic_config.cartesian_limits_config()
                        .default_cartesian_limits()),
          _ << " (part " << i << ", slot " << part.slot_id.value() << ")");
      INTR_ASSIGN_OR_RETURN(
          std::unique_ptr<intrinsic::kinematics::Skeleton> skeleton,
          intrinsic::kinematics::FromProto(
              part.generic_config.manipulator_kinematics_config().skeleton()),
          _ << " (part " << i << ", slot " << part.slot_id.value() << ")");
      INTR_ASSIGN_OR_RETURN(
          kinematics::Chain chain,
          kinematics::ExtractNonBranchingChain(*skeleton),
          _ << " (part " << i << ", slot " << part.slot_id.value() << ")");
      INTR_RETURN_IF_ERROR(intrinsic::IsTrajectoryWithinCartesianPositionLimits(
          chain, cartesian_limits, part.command_trajectory))
          << " (part " << i << ", slot " << part.slot_id.value() << ")";
    }

    std::optional<std::vector<double>> cartesian_arc_lengths;
    if (part.command_trajectory.HasCartesianArcLength()) {
      cartesian_arc_lengths.emplace(
          part.command_trajectory.cartesian_arc_lengths()->begin(),
          part.command_trajectory.cartesian_arc_lengths()->end());
    }

    const size_t ndof = part.system_limits.size();
    arm_part_states.push_back(ArmPartState{
        .slot_id = part.slot_id,
        .joint_offset = current_joint_offset,
        .ndof = ndof,
        .cartesian_arc_lengths = std::move(cartesian_arc_lengths),
        .planning_limits = std::move(part.planning_limits),
        .system_limits = part.system_limits,
    });
    current_joint_offset += ndof;
    part_trajectories_and_limits.push_back(TrajectoryAndLimits{
        .trajectory = std::move(part.command_trajectory),
        .limits = std::move(part.system_limits),
    });
  }

  // Merge all parts' trajectories and check discretized samples against each
  // part's system limits.
  INTR_ASSIGN_OR_RETURN(
      TrajectoryAndLimits merged,
      MergeTrajectoriesAndLimits(std::move(part_trajectories_and_limits)));

  // If the commanded trajectory is torque-limited, we construct a rigid body
  // interface for the trajectory player to be able to play it while
  // respecting torque-limits (`kCheckNone` is rejected above for multiple
  // parts).
  std::unique_ptr<RigidBodyInterface> rigid_body_interface = nullptr;
  if (merged.trajectory.joint_dynamic_limits_check_mode() ==
          DynamicLimitsCheckMode::kCheckNone &&
      parts[0].generic_config.has_manipulator_kinematics_config()) {
    INTR_ASSIGN_OR_RETURN(
        std::unique_ptr<intrinsic::kinematics::Skeleton> skeleton,
        intrinsic::kinematics::FromProto(
            parts[0]
                .generic_config.manipulator_kinematics_config()
                .skeleton()));
    INTR_ASSIGN_OR_RETURN(rigid_body_interface,
                          CreateRoboticsLibraryDynamics(std::move(skeleton)));
  }

  for (size_t i = 0; i < arm_part_states.size(); ++i) {
    ArmPartState& arm = arm_part_states[i];
    INTR_ASSIGN_OR_RETURN(
        arm.residual_controller,
        TrajectoryResidualController::Create(frequency_hz, arm.planning_limits,
                                             arm.system_limits),
        _ << " (part " << i << ", slot " << arm.slot_id.value() << ")");
    INTR_ASSIGN_OR_RETURN(
        arm.is_settled_criterion, IsSettledCriterion::Create(frequency_hz),
        _ << " (part " << i << ", slot " << arm.slot_id.value() << ")");
  }
  INTR_ASSIGN_OR_RETURN(
      std::unique_ptr<TrajectoryPlayer> trajectory_player,
      TrajectoryPlayer::Create(
          std::move(merged.trajectory), absl::Seconds(1. / frequency_hz),
          std::move(merged.limits), std::move(rigid_body_interface)));

  return absl::WrapUnique(new TrajectoryTracker(std::move(trajectory_player),
                                                std::move(arm_part_states)));
}

TrajectoryTracker::TrajectoryTracker(
    std::unique_ptr<TrajectoryPlayer> trajectory_player,
    FixedVector<ArmPartState, kMaxNumParts> arm_part_states)
    : trajectory_player_(std::move(trajectory_player)),
      arm_part_states_(std::move(arm_part_states)) {}

void TrajectoryTracker::SetPathAccurateStopRequested() {
  // This is a one-way latch; once requested, path_accurate_stop_requested_
  // cannot be cleared.
  path_accurate_stop_requested_ = true;
  (void)trajectory_player_->SetDesiredSpeedOverrideFactor(0.0);
}

RealtimeStatus TrajectoryTracker::OnEnter(const double global_speed_override,
                                          const RealtimeSlotMap& slot_map) {
  for (ArmPartState& arm : arm_part_states_) {
    // The final state residual will be computed and used after the trajectory
    // has been finished.
    arm.final_state_residual = std::nullopt;
  }

  trajectory_player_->ResetPhase();
  // Initialize the trajectory player to currently active speed override
  // factors. This allows consistent Action switching despite speed overriding
  // being active.
  INTRINSIC_RT_RETURN_IF_ERROR(
      trajectory_player_->ForceSpeedOverrideFactor(global_speed_override));

  for (size_t i = 0; i < arm_part_states_.size(); ++i) {
    ArmPartState& arm = arm_part_states_[i];
    const JointPosition* const joint_position_interface =
        slot_map.GetInterfaceForSlot<JointPosition>(arm.slot_id);
    if (joint_position_interface == nullptr) {
      return InternalError(
          RealtimeStatus::StrCat("Slot doesn't have JointPosition (part ", i,
                                 ", slot ", arm.slot_id.value(), ")."));
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
    const auto* const sensed_velocity =
        slot_map.GetInterfaceForSlot<JointVelocityEstimator>(arm.slot_id);
    if (sensed_velocity == nullptr) {
      return InternalError(RealtimeStatus::StrCat(
          "Slot does not have JointVelocityEstimator (part ", i, ", slot ",
          arm.slot_id.value(), ")"));
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
        arm.initial_state_residual,
        DesignInitialStateResidual(trajectory_player_->GetTrajectory(),
                                   arm.joint_offset, arm.ndof,
                                   previous_joint_setpoint),
        "Part ", i, " (slot ", arm.slot_id.value(), ")");

    const double max_position_deviation =
        arm.initial_state_residual.position.cwiseAbs().maxCoeff();
    if (max_position_deviation >
        TrajectoryTrackingActionInfo::kMaxInitialJointPositionDeviation) {
      return FailedPreconditionError(RealtimeStatus::StrCat(
          "Difference between previous setpoint and position command (",
          max_position_deviation, ") exceeds ",
          TrajectoryTrackingActionInfo::kMaxInitialJointPositionDeviation,
          " (part ", i, ", slot ", arm.slot_id.value(), ")"));
    }

    const double max_velocity_deviation =
        arm.initial_state_residual.velocity.cwiseAbs().maxCoeff();
    if (max_velocity_deviation >
        TrajectoryTrackingActionInfo::kMaxInitialJointVelocityDeviation) {
      return FailedPreconditionError(RealtimeStatus::StrCat(
          "Difference between previous setpoint and velocity command (",
          max_velocity_deviation, ") exceeds ",
          TrajectoryTrackingActionInfo::kMaxInitialJointVelocityDeviation,
          " (part ", i, ", slot ", arm.slot_id.value(), ")"));
    }

    arm.distance_to_final_setpoint = std::numeric_limits<double>::max();
    arm.is_settled = false;
    INTRINSIC_RT_RETURN_IF_ERROR(arm.is_settled_criterion->Initialize());
  }

  trajectory_done_for_seconds_ = 0.0;
  wall_time_passed_since_trajectory_start_ = 0.0;

  return OkStatus();
}

RealtimeStatus TrajectoryTracker::Sense(const RealtimeSlotMap& slot_map) {
  // Update distance to final setpoint.
  const JointStatePVA& final_state =
      trajectory_player_->GetTrajectory().data().back();
  for (size_t i = 0; i < arm_part_states_.size(); ++i) {
    ArmPartState& arm = arm_part_states_[i];
    const auto* const sensed_joint_position =
        slot_map.GetInterfaceForSlot<JointPositionSensor>(arm.slot_id);
    if (sensed_joint_position == nullptr) {
      return InternalError(RealtimeStatus::StrCat(
          "Slot does not have JointPositionSensor (part ", i, ", slot ",
          arm.slot_id.value(), ")"));
    }
    // Create() + MergeTrajectoriesAndLimits() guarantee that
    // [joint_offset, joint_offset + ndof) partitions every merged sample.
    arm.distance_to_final_setpoint =
        (sensed_joint_position->GetSensedPosition().position -
         final_state.position.segment(arm.joint_offset, arm.ndof))
            .norm();
  }

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
  for (size_t i = 0; i < arm_part_states_.size(); ++i) {
    ArmPartState& arm = arm_part_states_[i];
    const JointPosition* const position_command =
        slot_map.GetInterfaceForSlot<JointPosition>(arm.slot_id);
    if (position_command == nullptr) {
      return FailedPreconditionError(RealtimeStatus::StrCat(
          "Slot does not have JointPosition feature interface (part ", i,
          ", slot ", arm.slot_id.value(), ")"));
    }
    CHECK(position_command->PreviousPositionSetpoints()
              .velocity_feedforward()
              .has_value());

    const auto* const sensed_velocity =
        slot_map.GetInterfaceForSlot<JointVelocityEstimator>(arm.slot_id);
    if (sensed_velocity == nullptr) {
      return InternalError(RealtimeStatus::StrCat(
          "Slot does not have JointVelocityEstimator (part ", i, ", slot ",
          arm.slot_id.value(), ")"));
    }

    const bool was_settled = arm.is_settled;
    INTRINSIC_RT_ASSIGN_OR_RETURN(
        arm.is_settled,
        arm.is_settled_criterion->Update(
            sensed_velocity->GetVelocityEstimate().velocity,
            position_command->PreviousPositionSetpoints()
                .velocity_feedforward()
                .value(),
            /*has_trajectory_ended=*/is_done),
        "Part ", i, " (slot ", arm.slot_id.value(), ")");
    if (!was_settled && arm.is_settled) {
      INTRINSIC_RT_LOG(INFO) << "Trajectory settled for part " << i
                             << " (slot_id: " << arm.slot_id.value() << ").";
    }
  }

  return OkStatus();
}

RealtimeStatus TrajectoryTracker::Control(
    const double combined_speed_override_factor, RealtimeSlotMap& slot_map) {
  FixedVector<JointPosition*, kMaxNumParts> joint_position_interfaces;
  for (size_t i = 0; i < arm_part_states_.size(); ++i) {
    const ArmPartState& arm = arm_part_states_[i];
    JointPosition* const joint_position_interface =
        slot_map.GetMutableInterfaceForSlot<JointPosition>(arm.slot_id);
    if (joint_position_interface == nullptr) {
      return InternalError(
          RealtimeStatus::StrCat("Slot doesn't have JointPosition (part ", i,
                                 ", slot ", arm.slot_id.value(), ")."));
    }
    joint_position_interfaces.push_back(joint_position_interface);
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
  INTRINSIC_RT_ASSIGN_OR_RETURN(JointStatePVA merged_reference_state,
                                trajectory_player_->GetNextState());

  const JointTrajectoryPVA& trajectory = trajectory_player_->GetTrajectory();

  FixedVector<JointPositionCommand, kMaxNumParts> new_joint_position_commands;
  for (size_t i = 0; i < arm_part_states_.size(); ++i) {
    ArmPartState& arm = arm_part_states_[i];
    JointStatePVA new_trajectory_reference_state;
    INTRINSIC_RT_RETURN_IF_ERROR(
        new_trajectory_reference_state.SetSize(arm.ndof));
    // Create() + MergeTrajectoriesAndLimits() guarantee that
    // [joint_offset, joint_offset + ndof) partitions the merged trajectory,
    // and TrajectoryPlayer::GetNextState() preserves that dimension.
    new_trajectory_reference_state.position =
        merged_reference_state.position.segment(arm.joint_offset, arm.ndof);
    new_trajectory_reference_state.velocity =
        merged_reference_state.velocity.segment(arm.joint_offset, arm.ndof);
    new_trajectory_reference_state.acceleration =
        merged_reference_state.acceleration.segment(arm.joint_offset, arm.ndof);

    if (is_done) {
      // We need to account for the fact that the commanded trajectory may not
      // exactly end with zero velocity or acceleration. Since we should
      // effectively reach a steady-state if there is no follow-up action, we
      // need to actively enforce zero velocity and acceleration once the full
      // trajectory has been played back and is therefore "done". The residual
      // controller is then used to eliminate a "final" residual, e.g. a
      // non-zero terminal velocity/acceleration in the trajectory. We
      // initialize the final residual controller once.
      if (!arm.final_state_residual.has_value()) {
        arm.final_state_residual = JointStatePVA();
        INTRINSIC_RT_RETURN_IF_ERROR(arm.final_state_residual->SetSize(
            new_trajectory_reference_state.size()));
        // We would like to converge to the position of the last trajectory
        // datapoint, so the final residual position just needs to take over
        // the remaining "initial" position residual.
        arm.final_state_residual->position =
            arm.initial_state_residual.position;
        // Start from the final velocity and acceleration setpoint
        // interpolated from the trajectory, and add the remaining residual
        // from the initial state. This total residual will subsequently be
        // brought to zero using the full planning limits.
        arm.final_state_residual->velocity =
            new_trajectory_reference_state.velocity +
            arm.initial_state_residual.velocity;
        if (IsTrajectoryJerkLimited(trajectory)) {
          arm.final_state_residual->acceleration =
              new_trajectory_reference_state.acceleration +
              arm.initial_state_residual.acceleration;
        } else {
          arm.final_state_residual->acceleration.setZero();
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
    if (!arm.final_state_residual.has_value()) {
      INTRINSIC_RT_ASSIGN_OR_RETURN(
          arm.initial_state_residual,
          arm.residual_controller->ComputeResidualUsingLimitMargin(
              arm.initial_state_residual),
          "Part ", i, " (slot ", arm.slot_id.value(), ")");
      filtered_reference.position += arm.initial_state_residual.position;
      filtered_reference.velocity += arm.initial_state_residual.velocity;
      filtered_reference.acceleration +=
          arm.initial_state_residual.acceleration;
    } else {
      // Final state of the trajectory has been reached, update and include
      // final state residual. Since the residual compensation remains the
      // only motion, use planning limits.
      INTRINSIC_RT_ASSIGN_OR_RETURN(
          arm.final_state_residual,
          arm.residual_controller->ComputeResidualUsingPlanningLimits(
              *arm.final_state_residual),
          "Part ", i, " (slot ", arm.slot_id.value(), ")");
      filtered_reference.position += arm.final_state_residual->position;
      filtered_reference.velocity += arm.final_state_residual->velocity;
      filtered_reference.acceleration += arm.final_state_residual->acceleration;
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
      if (!eigenmath::ClampVector(-arm.system_limits.max_acceleration,
                                  arm.system_limits.max_acceleration,
                                  filtered_reference.acceleration)) {
        return InternalError(RealtimeStatus::StrCat(
            "Clamping joint accelerations to limits failed (part ", i,
            ", slot ", arm.slot_id.value(), ")."));
      }
    }

    INTRINSIC_RT_ASSIGN_OR_RETURN(
        JointPositionCommand new_joint_position_command,
        JointPositionCommand::Create(
            filtered_reference.position, filtered_reference.velocity,
            filtered_reference.acceleration,
            trajectory.joint_dynamic_limits_check_mode()),
        "Part ", i, " (slot ", arm.slot_id.value(), ")");
    new_joint_position_commands.push_back(
        std::move(new_joint_position_command));
  }

  for (size_t i = 0; i < arm_part_states_.size(); ++i) {
    INTRINSIC_RT_RETURN_IF_ERROR(
        joint_position_interfaces[i]->SetPositionSetpoints(
            new_joint_position_commands[i]));
  }
  return OkStatus();
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

namespace {

RealtimeStatus CheckPartIndex(const size_t part_index, const size_t num_parts) {
  if (part_index >= num_parts) {
    return OutOfRangeError(RealtimeStatus::StrCat(
        "Part index ", part_index, " is out of range [0, ", num_parts, ")"));
  }
  return OkStatus();
}

}  // namespace

RealtimeStatusOr<bool> TrajectoryTracker::IsSettled(
    const size_t part_index) const {
  INTRINSIC_RT_RETURN_IF_ERROR(
      CheckPartIndex(part_index, arm_part_states_.size()));
  return arm_part_states_[part_index].is_settled;
}

RealtimeStatusOr<double> TrajectoryTracker::GetIsSettledUncertainty(
    const size_t part_index) const {
  INTRINSIC_RT_RETURN_IF_ERROR(
      CheckPartIndex(part_index, arm_part_states_.size()));
  return arm_part_states_[part_index].is_settled_criterion->GetUncertainty();
}

RealtimeStatusOr<double> TrajectoryTracker::GetDistanceToFinalSetpoint(
    const size_t part_index) const {
  INTRINSIC_RT_RETURN_IF_ERROR(
      CheckPartIndex(part_index, arm_part_states_.size()));
  return arm_part_states_[part_index].distance_to_final_setpoint;
}

RealtimeStatusOr<double> TrajectoryTracker::GetCartesianArcLength(
    const size_t part_index) const {
  INTRINSIC_RT_RETURN_IF_ERROR(
      CheckPartIndex(part_index, arm_part_states_.size()));
  const std::optional<std::vector<double>>& arc_lengths =
      arm_part_states_[part_index].cartesian_arc_lengths;
  if (!arc_lengths.has_value()) {
    return FailedPreconditionError(RealtimeStatus::StrCat(
        "Trajectory does not have Cartesian arc length (part ", part_index,
        ", slot ", arm_part_states_[part_index].slot_id.value(), ")."));
  }
  const JointTrajectoryPVA& merged_trajectory =
      trajectory_player_->GetTrajectory();
  const double phase = trajectory_player_->GetPhase();
  if (phase < 0.0 || phase > 1.0) {
    return InvalidArgumentError(RealtimeStatus::StrCat(
        "phase should be in value range [0, 1], but instead has value ", phase,
        "."));
  }
  const absl::Duration time_since_start = phase * merged_trajectory.Duration();
  const size_t lower_index = static_cast<size_t>(
      merged_trajectory.GetLowerIndexForTime(time_since_start));
  if (lower_index + 1 >= arc_lengths->size()) {
    return arc_lengths->back();
  }
  INTRINSIC_RT_ASSIGN_OR_RETURN(const absl::Duration t_lower,
                                merged_trajectory.TimeAt(lower_index));
  INTRINSIC_RT_ASSIGN_OR_RETURN(const absl::Duration t_upper,
                                merged_trajectory.TimeAt(lower_index + 1));
  const double interpolation_fraction =
      absl::FDivDuration(time_since_start - t_lower, t_upper - t_lower);
  return (1.0 - interpolation_fraction) * (*arc_lengths)[lower_index] +
         interpolation_fraction * (*arc_lengths)[lower_index + 1];
}

}  // namespace intrinsic::icon
