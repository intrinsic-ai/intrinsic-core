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

#ifndef INTRINSIC_ICON_CONTROL_ACTIONS_TRAJECTORY_TRACKING_ACTION_H_
#define INTRINSIC_ICON_CONTROL_ACTIONS_TRAJECTORY_TRACKING_ACTION_H_

#include <memory>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "intrinsic/icon/actions/trajectory_tracking_action.pb.h"
#include "intrinsic/icon/actions/trajectory_tracking_action_info.h"
#include "intrinsic/icon/cc_client/condition.h"
#include "intrinsic/icon/control/action_factory_context.h"
#include "intrinsic/icon/control/actions/trajectory_tracker.h"
#include "intrinsic/icon/control/realtime_signal_types.h"
#include "intrinsic/icon/control/rtcl_action.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/icon/utils/realtime_status_or.h"
#include "intrinsic/kinematics/types/joint_trajectories.h"

namespace intrinsic::icon {

// Follows a given intrinsic::JointTrajectoryPVA and uses quintic splining to
// interpolate between the discretized states. The previously commanded
// robot position/velocity setpoint must be within
// `kMaxInitialJointPositionDeviation`/`kMaxInitialJointVelocityDeviation` of
// the first joint state of the command. A residual controller is applied to
// compensate for initial state deviations smaller than above thresholds.
class TrajectoryTrackingAction final : public RtclActionInterface {
 public:
  struct Params {
    // The joint position-velocity-acceleration trajectory to be played back on
    // the robot. Time stamps must start at zero. Time stamps are not required
    // to increase uniformly, however the trajectory is expected to be
    // dynamically/kinematically consistent. The action does not perform
    // additional consistency checks beyond limit checking, the system may be at
    // risk if the input trajectory is inconsistent.
    JointTrajectoryPVA trajectory;

    static absl::StatusOr<Params> FromProto(
        const intrinsic_proto::icon::actions::
            TrajectoryTrackingActionFixedParams& proto_params);
  };

  // Constructs a TrajectoryTrackingAction from `slot_id`, `trajectory_player`
  // and a `trajectory_residual_controller`.
  TrajectoryTrackingAction(
      RealtimeSlotId slot_id,
      std::unique_ptr<TrajectoryTracker> trajectory_tracker,
      RealtimeSignalId path_accurate_stop_signal_id);

  static absl::StatusOr<std::unique_ptr<TrajectoryTrackingAction>> Create(
      const TrajectoryTrackingActionInfo::FixedParams& params_proto,
      ActionFactoryContext& context);

  RealtimeStatus OnEnter(OnEnterParameters params) override;

  RealtimeStatus Sense(SenseParameters params) override;

  RealtimeStatus Control(ControlParameters params) override;

  RealtimeStatusOr<StateVariableValue> GetStateVariable(
      absl::string_view name) const override;

 private:
  const RealtimeSlotId slot_id_;
  std::unique_ptr<TrajectoryTracker> trajectory_tracker_;

  RealtimeSignalId path_accurate_stop_signal_id_;
  SignalValue path_accurate_stop_signal_value_;
  bool path_accurate_stop_requested_ = false;
};

}  // namespace intrinsic::icon

#endif  // INTRINSIC_ICON_CONTROL_ACTIONS_TRAJECTORY_TRACKING_ACTION_H_
