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

#ifndef INTRINSIC_ICON_CONTROL_ACTIONS_TRAJECTORY_TRACKING_UTILS_H_
#define INTRINSIC_ICON_CONTROL_ACTIONS_TRAJECTORY_TRACKING_UTILS_H_

#include <vector>

#include "absl/status/statusor.h"
#include "intrinsic/icon/testing/realtime_annotations.h"
#include "intrinsic/kinematics/types/joint_limits.h"
#include "intrinsic/kinematics/types/joint_trajectories.h"

namespace intrinsic::icon {

// A joint trajectory together with the joint limits that apply to its DOF.
struct TrajectoryAndLimits {
  JointTrajectoryPVA trajectory;
  JointLimits limits;
};

// Merges trajectories and their limits into one trajectory and one JointLimits
// over the sum of the inputs' DOF. Not real-time safe.
//
// Returns InvalidArgumentError unless:
// * `trajectories_and_limits` is non-empty.
// * Every `trajectory` is non-empty, and its `limits` has at least one DOF, is
//   size-consistent, contains no NaN values, and is valid
//   (`JointLimits::IsValid()`).
// * The sum of DOF across all inputs does not exceed `JointLimits::kMaxSize`
//   (`eigenmath::MAX_EIGEN_VECTOR_SIZE = 25`).
// * Every sample's position, velocity, and acceleration have `limits.size()`
//   finite entries and satisfy `limits` (position, velocity, and acceleration
//   when `kCheckJointAcceleration` is configured).
// * All trajectories have the same sample count, interpolation type, dynamic
//   limits check mode, and sample timestamps (within 100 ns).
//
// On success:
// * DOF are ordered as in `trajectories_and_limits` (index 0 first).
// * The merged trajectory uses trajectory 0's timestamps.
// * Cartesian arc lengths are left empty.
// * A single entry is returned unchanged (including its Cartesian arc lengths)
//   after validation.
absl::StatusOr<TrajectoryAndLimits> MergeTrajectoriesAndLimits(
    std::vector<TrajectoryAndLimits> trajectories_and_limits)
    INTRINSIC_NON_REALTIME_ONLY;

}  // namespace intrinsic::icon

#endif  // INTRINSIC_ICON_CONTROL_ACTIONS_TRAJECTORY_TRACKING_UTILS_H_
