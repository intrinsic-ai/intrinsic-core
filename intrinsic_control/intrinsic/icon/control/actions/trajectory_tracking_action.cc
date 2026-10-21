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

#include "intrinsic/icon/control/actions/trajectory_tracking_action.h"

#include <memory>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "intrinsic/icon/actions/trajectory_tracking_action.pb.h"
#include "intrinsic/icon/actions/trajectory_tracking_action_info.h"
#include "intrinsic/icon/cc_client/condition.h"
#include "intrinsic/icon/common/builtins.h"
#include "intrinsic/icon/control/action_factory_context.h"
#include "intrinsic/icon/control/actions/trajectory_tracker.h"
#include "intrinsic/icon/control/parts/feature_interfaces.h"
#include "intrinsic/icon/control/realtime_signal_types.h"
#include "intrinsic/icon/control/slot_types.h"
#include "intrinsic/icon/proto/generic_part_config.pb.h"
#include "intrinsic/icon/proto/joint_trajectory_conversion.h"
#include "intrinsic/icon/proto/v1/types.pb.h"
#include "intrinsic/icon/utils/log.h"
#include "intrinsic/icon/utils/realtime_guard.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/icon/utils/realtime_status_macro.h"
#include "intrinsic/icon/utils/realtime_status_or.h"
#include "intrinsic/kinematics/proto/kinematics_conversion.h"
#include "intrinsic/kinematics/types/joint_limits.h"
#include "intrinsic/kinematics/types/joint_trajectories.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic::icon {

namespace {

using ::intrinsic_proto::icon::v1::BehaviorOverrideRequest;

}  // namespace

// static
absl::StatusOr<TrajectoryTrackingAction::Params>
TrajectoryTrackingAction::Params::FromProto(
    const intrinsic_proto::icon::actions::TrajectoryTrackingActionFixedParams&
        proto_params) {
  INTRINSIC_ASSERT_NON_REALTIME();
  TrajectoryTrackingAction::Params params;
  INTR_ASSIGN_OR_RETURN(params.trajectory,
                        intrinsic::FromProto(proto_params.trajectory()));

  return params;
}

TrajectoryTrackingAction::TrajectoryTrackingAction(
    RealtimeSlotId slot_id,
    std::unique_ptr<TrajectoryTracker> trajectory_tracker,
    RealtimeSignalId path_accurate_stop_signal_id)
    : slot_id_(slot_id),
      trajectory_tracker_(std::move(trajectory_tracker)),
      path_accurate_stop_signal_id_(path_accurate_stop_signal_id) {}

// static
absl::StatusOr<std::unique_ptr<TrajectoryTrackingAction>>
TrajectoryTrackingAction::Create(
    const TrajectoryTrackingActionInfo::FixedParams& params_proto,
    ActionFactoryContext& context) {
  INTRINSIC_ASSERT_NON_REALTIME();
  INTR_ASSIGN_OR_RETURN(
      SlotInfo arm_info,
      context.GetSlotInfo(TrajectoryTrackingActionInfo::kSlotName));
  const ::intrinsic_proto::icon::GenericJointLimitsConfig& joint_limits_config =
      arm_info.config.generic_config().joint_limits_config();

  if (!arm_info.config.generic_config().has_joint_limits_config()) {
    return absl::InvalidArgumentError(
        "TrajectoryTrackingAction requires joint limits.");
  }

  // TODO(b/234426024): there will be no headroom for controlling towards the
  // desired trajectory if the provided trajectory already saturates ICON system
  // limits. Decide on b/234426024 and use application limits as planning
  // limits here.
  INTR_ASSIGN_OR_RETURN(
      JointLimits planning_limits,
      ::intrinsic::FromProto(joint_limits_config.application_limits()));
  INTR_ASSIGN_OR_RETURN(
      JointLimits system_limits,
      ::intrinsic::FromProto(joint_limits_config.system_limits()));

  if (!params_proto.has_trajectory()) {
    return absl::InvalidArgumentError(
        "TrajectoryTrackingAction requires a trajectory.");
  }
  INTR_ASSIGN_OR_RETURN(Params command, Params::FromProto(params_proto));

  // Store the commanded trajectory in the ActionFactoryContext. This allows an
  // ICON client to retrieve it via the ICON client API using an
  // ActionInstanceId.
  context.StoreTrajectory(command.trajectory);

  TrajectoryTracker::PartParams part_params{
      .slot_id = arm_info.slot_id,
      .generic_config = arm_info.config.generic_config(),
      .planning_limits = std::move(planning_limits),
      .system_limits = std::move(system_limits),
      .command_trajectory = std::move(command.trajectory),
  };
  INTR_ASSIGN_OR_RETURN(
      std::unique_ptr<TrajectoryTracker> trajectory_tracker,
      TrajectoryTracker::Create(std::move(part_params),
                                context.ServerConfig().frequency_hz()));

  INTR_ASSIGN_OR_RETURN(
      RealtimeSignalId path_accurate_stop_signal_id,
      context.GetRealtimeSignalId(
          TrajectoryTrackingActionInfo::kSignalPathAccurateStop));

  return std::make_unique<TrajectoryTrackingAction>(
      arm_info.slot_id, std::move(trajectory_tracker),
      path_accurate_stop_signal_id);
}

RealtimeStatus TrajectoryTrackingAction::OnEnter(OnEnterParameters params) {
  INTRINSIC_RT_RETURN_IF_ERROR(
      trajectory_tracker_->OnEnter(params.speed_override, params.slot_map));
  return icon::OkStatus();
}

RealtimeStatus TrajectoryTrackingAction::Sense(SenseParameters params) {
  INTRINSIC_RT_RETURN_IF_ERROR(trajectory_tracker_->Sense(params.slot_map));
  // Listen for path accurate stop request and store signal. Signal will only be
  // evaluated in Control() to avoid race conditions between Sense() and the
  // evaluation of state variables.
  INTRINSIC_RT_ASSIGN_OR_RETURN(
      path_accurate_stop_signal_value_,
      params.signal_access.ReadSignal(path_accurate_stop_signal_id_));

  return icon::OkStatus();
}

RealtimeStatus TrajectoryTrackingAction::Control(ControlParameters params) {
  JointPosition* const joint_position_interface =
      params.slot_map.GetMutableInterfaceForSlot<JointPosition>(slot_id_);
  if (joint_position_interface == nullptr) {
    return InternalError("Slot doesn't have JointPosition.");
  }

  // Evaluate path accurate stop signal. This needs to happen in Control() to
  // avoid race conditions between Sense() and state variable evaluation. The
  // path accurate stop can only be triggered once, currently it is impossible
  // to recover from it (and resume a motion).
  const bool path_accurate_stop_requested =
      path_accurate_stop_requested_ ||
      path_accurate_stop_signal_value_.current_value;
  // Logs once exactly when the path accurate stop is requested.
  if (path_accurate_stop_requested_ != path_accurate_stop_requested) {
    const double trajectory_phase =
        trajectory_tracker_->GetTrajectoryProgress();
    INTRINSIC_RT_LOG(INFO)
        << "Path accurate stop requested at trajectory phase "
        << trajectory_phase;
  }
  path_accurate_stop_requested_ = path_accurate_stop_requested;

  if (path_accurate_stop_requested_) {
    trajectory_tracker_->SetPathAccurateStopRequested();
  }

  // Pause can be requested and cleared.
  bool pause_requested_ = false;
  if (params.requested_behavior_override ==
      BehaviorOverrideRequest::BEHAVIOR_OVERRIDE_REQUEST_PAUSE) {
    INTRINSIC_RT_LOG_THROTTLED(INFO)
        << "PAUSE requested at trajectory phase "
        << trajectory_tracker_->GetTrajectoryProgress();
    pause_requested_ = true;
  }

  // If a path accurate stop is requested, the speed override is overwritten to
  // be zero.
  const double combined_speed_override_factor =
      params.speed_override *
      static_cast<double>(!path_accurate_stop_requested_) *
      static_cast<double>(!pause_requested_);

  return trajectory_tracker_->Control(combined_speed_override_factor,
                                      params.slot_map);
}

RealtimeStatusOr<StateVariableValue> TrajectoryTrackingAction::GetStateVariable(
    absl::string_view name) const {
  if (name == kIsDone) {
    return StateVariableValue(trajectory_tracker_->IsDone());
  }
  if (name == TrajectoryTrackingActionInfo::kIsSettled) {
    return StateVariableValue(trajectory_tracker_->IsSettled());
  }
  if (name == TrajectoryTrackingActionInfo::kIsSettledUncertainty) {
    return StateVariableValue(trajectory_tracker_->GetIsSettledUncertainty());
  }
  if (name == TrajectoryTrackingActionInfo::kTrajectoryProgress) {
    return StateVariableValue(trajectory_tracker_->GetTrajectoryProgress());
  }
  if (name == TrajectoryTrackingActionInfo::kTrajectoryDoneForSeconds) {
    return StateVariableValue(
        trajectory_tracker_->GetTrajectoryDoneForSeconds());
  }
  if (name == TrajectoryTrackingActionInfo::kDistanceToFinalSetpoint) {
    return StateVariableValue(
        trajectory_tracker_->GetDistanceToFinalSetpoint());
  }
  if (name == TrajectoryTrackingActionInfo::kTimeSinceTrajectoryStartSeconds) {
    return StateVariableValue(
        trajectory_tracker_->GetWallTimePassedSinceTrajectoryStart());
  }
  if (name ==
      TrajectoryTrackingActionInfo::kCartesianArcLengthAlongTrajectoryMeters) {
    INTRINSIC_RT_ASSIGN_OR_RETURN(double cart_arc_length,
                                  trajectory_tracker_->GetCartesianArcLength());
    return StateVariableValue(cart_arc_length);
  }
  return NotFoundError(RealtimeStatus::StrCat(
      "TrajectoryTrackingAction, state variable not found ", name));
}

}  // namespace intrinsic::icon
