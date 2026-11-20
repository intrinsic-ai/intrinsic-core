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

#include "intrinsic/icon/control/actions/trajectory_tracking_utils.h"

#include <cstddef>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "intrinsic/icon/utils/realtime_guard.h"
#include "intrinsic/icon/utils/realtime_status_macro.h"
#include "intrinsic/kinematics/types/check_joint_limits.h"
#include "intrinsic/kinematics/types/dynamic_limits_check_mode.h"
#include "intrinsic/kinematics/types/joint_limits.h"
#include "intrinsic/kinematics/types/joint_state.h"
#include "intrinsic/kinematics/types/joint_trajectories.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic::icon {

namespace {

// Tolerance for sample timestamp comparisons across trajectories to accommodate
// floating-point seconds-to-duration conversion rounding.
constexpr absl::Duration kTimestampMatchTolerance = absl::Nanoseconds(100);

absl::StatusOr<size_t> ValidateTrajectoriesAndLimitsConsistency(
    const absl::Span<const TrajectoryAndLimits> trajectories_and_limits) {
  if (trajectories_and_limits.empty()) {
    return absl::InvalidArgumentError(
        "MergeTrajectoriesAndLimits requires at least one trajectory.");
  }

  const JointTrajectoryPVA& ref_trajectory =
      trajectories_and_limits[0].trajectory;

  // Pass 1: O(1) per-trajectory header, limits, and metadata checks.
  size_t total_dofs = 0;
  for (size_t i = 0; i < trajectories_and_limits.size(); ++i) {
    const JointTrajectoryPVA& trajectory =
        trajectories_and_limits[i].trajectory;
    const JointLimits& limits = trajectories_and_limits[i].limits;

    if (trajectory.data().empty()) {
      return absl::InvalidArgumentError(absl::StrCat(
          "TrajectoryTracker requires a non-empty trajectory (No data point "
          "given in trajectory ",
          i, ")."));
    }
    const size_t ndof = limits.size();
    if (ndof == 0) {
      return absl::InvalidArgumentError(
          absl::StrCat("Trajectory ", i,
                       " must have at least one degree of freedom (got 0)."));
    }
    if (!limits.IsSizeConsistent()) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Joint limits for trajectory ", i, " are inconsistent in size."));
    }
    // +/-inf is a legitimate "unlimited" value (JointLimits::SetUnlimited), so
    // NaN is checked explicitly before IsValid().
    if (limits.min_position.hasNaN() || limits.max_position.hasNaN() ||
        limits.max_velocity.hasNaN() || limits.max_acceleration.hasNaN() ||
        limits.max_jerk.hasNaN() || limits.max_torque.hasNaN()) {
      return absl::InvalidArgumentError(
          absl::StrCat("Joint limits for trajectory ", i, " contain NaN."));
    }
    if (!limits.IsValid()) {
      return absl::InvalidArgumentError(
          absl::StrCat("Joint limits for trajectory ", i, " are invalid."));
    }
    total_dofs += ndof;

    if (i > 0) {
      if (trajectory.interpolation_type() !=
          ref_trajectory.interpolation_type()) {
        return absl::InvalidArgumentError(absl::StrCat(
            "All trajectories must have the same interpolation type (mismatch "
            "between trajectory 0 and trajectory ",
            i, ")."));
      }
      if (trajectory.joint_dynamic_limits_check_mode() !=
          ref_trajectory.joint_dynamic_limits_check_mode()) {
        return absl::InvalidArgumentError(absl::StrCat(
            "All trajectories must have the same joint dynamic limits check "
            "mode (mismatch between trajectory 0 and trajectory ",
            i, ")."));
      }
      if (trajectory.time_stamps().size() !=
          ref_trajectory.time_stamps().size()) {
        return absl::InvalidArgumentError(absl::StrCat(
            "All trajectories must have matching timestamps. Trajectory 0 has ",
            ref_trajectory.time_stamps().size(), " timestamps, but trajectory ",
            i, " has ", trajectory.time_stamps().size(), " timestamps."));
      }
    }
  }

  if (total_dofs > static_cast<size_t>(JointLimits::kMaxSize)) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Total degrees of freedom across all trajectories (", total_dofs,
        ") exceeds JointLimits::kMaxSize (", JointLimits::kMaxSize, ")."));
  }

  // Pass 2: Per-sample timestamp, dimension, finiteness, and limit checks.
  const absl::Span<const absl::Duration> ref_timestamps =
      ref_trajectory.time_stamps();
  for (size_t i = 0; i < trajectories_and_limits.size(); ++i) {
    const JointTrajectoryPVA& trajectory =
        trajectories_and_limits[i].trajectory;
    const JointLimits& limits = trajectories_and_limits[i].limits;
    const size_t ndof = limits.size();

    if (i > 0) {
      const absl::Span<const absl::Duration> timestamps =
          trajectory.time_stamps();
      for (size_t j = 0; j < timestamps.size(); ++j) {
        if (absl::AbsDuration(timestamps[j] - ref_timestamps[j]) >
            kTimestampMatchTolerance) {
          return absl::InvalidArgumentError(absl::StrCat(
              "All trajectories must have matching timestamps. Mismatch at "
              "index ",
              j, " between trajectory 0 (",
              absl::FormatDuration(ref_timestamps[j]), ") and trajectory ", i,
              " (", absl::FormatDuration(timestamps[j]), ")."));
        }
      }
    }

    // JointTrajectoryPVA::Create guarantees data().size() ==
    // time_stamps().size(), but checks neither per-sample sizes, finiteness,
    // nor adherence to limits.
    for (size_t j = 0; j < trajectory.size(); ++j) {
      const JointStatePVA& sample = trajectory.data()[j];
      if (sample.position.size() != ndof || sample.velocity.size() != ndof ||
          sample.acceleration.size() != ndof) {
        return absl::InvalidArgumentError(
            absl::StrCat("Sample ", j, " of trajectory ", i, " does not have ",
                         ndof, " DOF in position, velocity and acceleration."));
      }
      if (!sample.position.allFinite() || !sample.velocity.allFinite() ||
          !sample.acceleration.allFinite()) {
        return absl::InvalidArgumentError(absl::StrCat(
            "Sample ", j, " of trajectory ", i, " has a non-finite value."));
      }
      INTRINSIC_RT_ASSIGN_OR_RETURN(const LimitCheckResult limit_check_result,
                                    IsWithinLimits(sample, limits));
      if (!limit_check_result.p_ok) {
        return absl::InvalidArgumentError(
            absl::StrCat("Provided joint trajectory violates joint position "
                         "limits at index ",
                         j, " for trajectory ", i, "."));
      } else if (!limit_check_result.v_ok) {
        return absl::InvalidArgumentError(
            absl::StrCat("Provided joint trajectory violates joint velocity "
                         "limits at index ",
                         j, " for trajectory ", i, "."));
      } else if (!limit_check_result.a_ok &&
                 trajectory.joint_dynamic_limits_check_mode() ==
                     DynamicLimitsCheckMode::kCheckJointAcceleration) {
        return absl::InvalidArgumentError(absl::StrCat(
            "Provided joint trajectory violates joint acceleration "
            "limits at index ",
            j, " for trajectory ", i, "."));
      }
    }
  }

  return total_dofs;
}

}  // namespace

absl::StatusOr<TrajectoryAndLimits> MergeTrajectoriesAndLimits(
    std::vector<TrajectoryAndLimits> trajectories_and_limits) {
  INTRINSIC_ASSERT_NON_REALTIME();
  INTR_ASSIGN_OR_RETURN(
      const size_t total_dofs,
      ValidateTrajectoriesAndLimitsConsistency(trajectories_and_limits));

  // Nothing to merge for a single input; return it unchanged (this also keeps
  // its Cartesian arc lengths, which a merge drops).
  if (trajectories_and_limits.size() == 1) {
    return std::move(trajectories_and_limits[0]);
  }

  JointLimits merged_limits;
  INTR_RETURN_IF_ERROR(merged_limits.SetSize(total_dofs));
  size_t dof_offset = 0;
  for (const TrajectoryAndLimits& trajectory_and_limits :
       trajectories_and_limits) {
    const JointLimits& limits = trajectory_and_limits.limits;
    const size_t ndof = limits.size();
    merged_limits.min_position.segment(dof_offset, ndof) = limits.min_position;
    merged_limits.max_position.segment(dof_offset, ndof) = limits.max_position;
    merged_limits.max_velocity.segment(dof_offset, ndof) = limits.max_velocity;
    merged_limits.max_acceleration.segment(dof_offset, ndof) =
        limits.max_acceleration;
    merged_limits.max_jerk.segment(dof_offset, ndof) = limits.max_jerk;
    merged_limits.max_torque.segment(dof_offset, ndof) = limits.max_torque;
    dof_offset += ndof;
  }

  const JointTrajectoryPVA& ref_trajectory =
      trajectories_and_limits[0].trajectory;
  const absl::Span<const absl::Duration> ref_timestamps =
      ref_trajectory.time_stamps();

  std::vector<JointStatePVA> merged_states;
  merged_states.reserve(ref_timestamps.size());
  for (size_t j = 0; j < ref_timestamps.size(); ++j) {
    JointStatePVA combined_state;
    INTR_RETURN_IF_ERROR(combined_state.SetSize(total_dofs));
    size_t state_dof_offset = 0;
    for (const TrajectoryAndLimits& trajectory_and_limits :
         trajectories_and_limits) {
      const size_t ndof = trajectory_and_limits.limits.size();
      const JointStatePVA& sample = trajectory_and_limits.trajectory.data()[j];
      combined_state.position.segment(state_dof_offset, ndof) = sample.position;
      combined_state.velocity.segment(state_dof_offset, ndof) = sample.velocity;
      combined_state.acceleration.segment(state_dof_offset, ndof) =
          sample.acceleration;
      state_dof_offset += ndof;
    }
    merged_states.push_back(std::move(combined_state));
  }

  INTR_ASSIGN_OR_RETURN(JointTrajectoryPVA merged_trajectory,
                        JointTrajectoryPVA::Create(
                            std::move(merged_states),
                            std::vector<absl::Duration>(ref_timestamps.begin(),
                                                        ref_timestamps.end()),
                            ref_trajectory.joint_dynamic_limits_check_mode(),
                            ref_trajectory.interpolation_type()));

  return TrajectoryAndLimits{
      .trajectory = std::move(merged_trajectory),
      .limits = std::move(merged_limits),
  };
}

}  // namespace intrinsic::icon
