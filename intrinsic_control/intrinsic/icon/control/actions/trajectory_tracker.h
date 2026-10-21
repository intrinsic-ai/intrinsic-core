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

#ifndef INTRINSIC_ICON_CONTROL_ACTIONS_TRAJECTORY_TRACKER_H_
#define INTRINSIC_ICON_CONTROL_ACTIONS_TRAJECTORY_TRACKER_H_

#include <limits>
#include <memory>
#include <optional>

#include "absl/status/statusor.h"
#include "intrinsic/icon/control/algorithms/is_settled_criterion.h"
#include "intrinsic/icon/control/algorithms/trajectory_player.h"
#include "intrinsic/icon/control/algorithms/trajectory_residual_controller.h"
#include "intrinsic/icon/control/joint_position_command.h"
#include "intrinsic/icon/control/parts/feature_interfaces.h"
#include "intrinsic/icon/control/realtime_slot_map.h"
#include "intrinsic/icon/control/slot_types.h"
#include "intrinsic/icon/proto/generic_part_config.pb.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/icon/utils/realtime_status_or.h"
#include "intrinsic/kinematics/types/joint_limits.h"
#include "intrinsic/kinematics/types/joint_state.h"
#include "intrinsic/kinematics/types/joint_trajectories.h"

namespace intrinsic::icon {

// Encapsulates the real-time trajectory tracking state machine, residual
// compensation, and settlement logic for an ICON arm part.
class TrajectoryTracker {
 public:
  // Non-realtime configuration parameters for a controlled arm part.
  struct PartParams {
    RealtimeSlotId slot_id;
    ::intrinsic_proto::icon::GenericPartConfig generic_config;
    JointLimits planning_limits;
    JointLimits system_limits;
    intrinsic::JointTrajectoryPVA command_trajectory;
  };

  // Factory method to construct and validate a TrajectoryTracker
  // from a PartParams configuration.
  static absl::StatusOr<std::unique_ptr<TrajectoryTracker>> Create(
      PartParams part, double frequency_hz);

  // Initializes the trajectory player and computes initial state residuals.
  // Resets timers and settlement flags.
  RealtimeStatus OnEnter(double global_speed_override,
                         const RealtimeSlotMap& slot_map);

  // Updates tracking metrics (distance to final setpoint, elapsed timers) and
  // evaluates the settlement criterion.
  RealtimeStatus Sense(const RealtimeSlotMap& slot_map);

  // Advances the trajectory player and computes/applies the next joint
  // position command (including initial or final residual compensation).
  RealtimeStatus Control(double combined_speed_override_factor,
                         RealtimeSlotMap& slot_map);

  // Latches a path-accurate stop request. Once called, cannot be unset.
  void SetPathAccurateStopRequested();

  // Returns the underlying trajectory played by this tracker.
  const JointTrajectoryPVA& GetTrajectory() const {
    return trajectory_player_->GetTrajectory();
  }

  // State variable queries.
  bool IsDone() const;
  bool IsSettled() const { return arm_.is_settled; }
  double GetIsSettledUncertainty() const {
    return arm_.is_settled_criterion->GetUncertainty();
  }
  double GetTrajectoryProgress() const {
    return trajectory_player_->GetPhase();
  }
  double GetTrajectoryDoneForSeconds() const {
    return trajectory_done_for_seconds_;
  }
  double GetDistanceToFinalSetpoint() const {
    return arm_.distance_to_final_setpoint;
  }
  double GetWallTimePassedSinceTrajectoryStart() const {
    return wall_time_passed_since_trajectory_start_;
  }
  RealtimeStatusOr<double> GetCartesianArcLength() const;

 private:
  struct ArmPartState {
    RealtimeSlotId slot_id;
    std::unique_ptr<TrajectoryResidualController> residual_controller;
    std::unique_ptr<IsSettledCriterion> is_settled_criterion;
    JointLimits planning_limits;
    JointLimits system_limits;

    JointStatePVA initial_state_residual;
    std::optional<JointStatePVA> final_state_residual = std::nullopt;
    double distance_to_final_setpoint = std::numeric_limits<double>::max();
    bool is_settled = false;
  };

  TrajectoryTracker(std::unique_ptr<TrajectoryPlayer> trajectory_player,
                    ArmPartState arm);

  std::unique_ptr<TrajectoryPlayer> trajectory_player_;
  ArmPartState arm_;

  double trajectory_done_for_seconds_ = 0.0;
  // The wall time passed since the trajectory was started. This is does not
  // correspond to the time parametrization of the trajectory.
  // wall_time_passed_since_trajectory_start_ increases by the time_step every
  // Sense cycle and is not affected by speed override which mean the
  // trajectory's time parametrization is advancing more slowly than the wall
  // time.
  double wall_time_passed_since_trajectory_start_ = 0.0;
  bool path_accurate_stop_requested_ = false;
};

}  // namespace intrinsic::icon

#endif  // INTRINSIC_ICON_CONTROL_ACTIONS_TRAJECTORY_TRACKER_H_
