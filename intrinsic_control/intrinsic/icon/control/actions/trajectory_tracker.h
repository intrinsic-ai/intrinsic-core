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

#include <cstddef>
#include <limits>
#include <memory>
#include <optional>
#include <vector>

#include "absl/status/statusor.h"
#include "intrinsic/icon/control/algorithms/is_settled_criterion.h"
#include "intrinsic/icon/control/algorithms/trajectory_player.h"
#include "intrinsic/icon/control/algorithms/trajectory_residual_controller.h"
#include "intrinsic/icon/control/joint_position_command.h"
#include "intrinsic/icon/control/parts/feature_interfaces.h"
#include "intrinsic/icon/control/realtime_slot_map.h"
#include "intrinsic/icon/control/slot_types.h"
#include "intrinsic/icon/proto/generic_part_config.pb.h"
#include "intrinsic/icon/testing/realtime_annotations.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/icon/utils/realtime_status_or.h"
#include "intrinsic/kinematics/types/joint_limits.h"
#include "intrinsic/kinematics/types/joint_state.h"
#include "intrinsic/kinematics/types/joint_trajectories.h"
#include "intrinsic/util/fixed_vector.h"

namespace intrinsic::icon {

// Action-agnostic trajectory tracking controller that encapsulates trajectory
// playback (`TrajectoryPlayer`), initial/final residual compensation
// (`TrajectoryResidualController`), and settlement detection
// (`IsSettledCriterion`) for 1..`kMaxNumParts` robot arm part slots. All parts
// share a single `TrajectoryPlayer` over the merged trajectory and limits so
// they remain locked in trajectory phase and speed override factor.
class TrajectoryTracker {
 public:
  // Maximum number of arm parts supported by a single TrajectoryTracker.
  static constexpr size_t kMaxNumParts = 3;

  struct PartParams {
    RealtimeSlotId slot_id;
    ::intrinsic_proto::icon::GenericPartConfig generic_config;
    JointLimits planning_limits;
    JointLimits system_limits;
    intrinsic::JointTrajectoryPVA command_trajectory;
  };

  // Factory method to construct and validate a TrajectoryTracker from one or
  // more PartParams configurations (at most `kMaxNumParts`, with distinct
  // `slot_id`s and matching trajectory timing). Multi-part torque-mode tracking
  // (`DynamicLimitsCheckMode::kCheckNone`) returns UnimplementedError.
  static absl::StatusOr<std::unique_ptr<TrajectoryTracker>> Create(
      std::vector<PartParams> parts,
      double frequency_hz) INTRINSIC_NON_REALTIME_ONLY;

  // Initializes the trajectory player and computes initial state residuals.
  // Resets timers and settlement flags.
  RealtimeStatus OnEnter(double global_speed_override,
                         const RealtimeSlotMap& slot_map)
      INTRINSIC_CHECK_REALTIME_SAFE;

  // Updates tracking metrics (distance to final setpoint, elapsed timers) and
  // evaluates the settlement criterion.
  RealtimeStatus Sense(const RealtimeSlotMap& slot_map)
      INTRINSIC_CHECK_REALTIME_SAFE;

  // Advances the trajectory player and computes/applies the next joint
  // position command (including initial or final residual compensation).
  RealtimeStatus Control(double combined_speed_override_factor,
                         RealtimeSlotMap& slot_map)
      INTRINSIC_CHECK_REALTIME_SAFE;

  // Latches a path-accurate stop request. Once called, cannot be unset.
  void SetPathAccurateStopRequested() INTRINSIC_CHECK_REALTIME_SAFE;

  // Returns the underlying trajectory played by this tracker. For multiple
  // parts this is the merged trajectory over all parts' DOFs, in `parts` order.
  const JointTrajectoryPVA& GetTrajectory() const
      INTRINSIC_CHECK_REALTIME_SAFE {
    return trajectory_player_->GetTrajectory();
  }

  // State variable queries. Queries that take a `part_index` return
  // OutOfRangeError if `part_index >= GetNumParts()`.
  size_t GetNumParts() const INTRINSIC_CHECK_REALTIME_SAFE {
    return arm_part_states_.size();
  }
  bool IsDone() const INTRINSIC_CHECK_REALTIME_SAFE;
  RealtimeStatusOr<bool> IsSettled(size_t part_index) const
      INTRINSIC_CHECK_REALTIME_SAFE;
  RealtimeStatusOr<double> GetIsSettledUncertainty(size_t part_index) const
      INTRINSIC_CHECK_REALTIME_SAFE;
  double GetTrajectoryProgress() const INTRINSIC_CHECK_REALTIME_SAFE {
    return trajectory_player_->GetPhase();
  }
  double GetTrajectoryDoneForSeconds() const INTRINSIC_CHECK_REALTIME_SAFE {
    return trajectory_done_for_seconds_;
  }
  RealtimeStatusOr<double> GetDistanceToFinalSetpoint(size_t part_index) const
      INTRINSIC_CHECK_REALTIME_SAFE;
  double GetWallTimePassedSinceTrajectoryStart() const
      INTRINSIC_CHECK_REALTIME_SAFE {
    return wall_time_passed_since_trajectory_start_;
  }
  RealtimeStatusOr<double> GetCartesianArcLength(size_t part_index) const
      INTRINSIC_CHECK_REALTIME_SAFE;

 private:
  struct ArmPartState {
    RealtimeSlotId slot_id;
    size_t joint_offset = 0;
    size_t ndof = 0;
    // Cartesian arc length samples of this part's trajectory, if any (the
    // merged trajectory in `trajectory_player_` does not carry per-part arc
    // lengths).
    std::optional<std::vector<double>> cartesian_arc_lengths;
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
                    FixedVector<ArmPartState, kMaxNumParts> arm_part_states);

  std::unique_ptr<TrajectoryPlayer> trajectory_player_;
  FixedVector<ArmPartState, kMaxNumParts> arm_part_states_;

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
