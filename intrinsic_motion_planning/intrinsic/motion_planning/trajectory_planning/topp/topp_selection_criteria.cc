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

#include "intrinsic/motion_planning/trajectory_planning/topp/topp_selection_criteria.h"

#include <algorithm>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "intrinsic/motion_planning/path_planning/path_segment.h"
#include "intrinsic/motion_planning/trajectory_planning/topp/joint_optimization_options.pb.h"

namespace intrinsic::topp {

namespace {

double GetBoundedJerk(const double max_acceleration, const double max_jerk) {
  return std::min(
      std::min(max_jerk, kMaxJerkToAccelerationLimitRatio * max_acceleration),
      kMaxAbsoluteJerkLimitRadSec3);
}

}  // namespace

absl::StatusOr<bool> UseJerkLimitedTopp(
    const OptimizationOptions& options,
    absl::Span<const PathSegment> path_segments) {
  // Check joint jerk constraints for all `path_segments`. Currently,
  // torque-rate constraints are treated exactly as joint jerk constraints,
  // because jerk constraints can be formulated analytically. This is a
  // specification in contrast to torque-rate constraints which would require
  // time derivatives of the rigid-body dynamics model and an specification of
  // torque-rate limits.
  if (options.kinematic_constraints().activate_joint_jerk_constraints() ||
      options.dynamic_constraints().activate_joint_torque_rate_constraints()) {
    for (const PathSegment& path_segment : path_segments) {
      if (((path_segment.joint_limits.max_jerk.array() /
            path_segment.joint_limits.max_acceleration.array()) <
           kJerkToAccelerationLimitRatioForUnlimitedJerk)
              .any()) {
        return true;
      }
    }
  }

  return false;
}

absl::Status AdaptJerkLimitsForBetterFeasibility(
    const OptimizationOptions& options, absl::Span<PathSegment> path_segments) {
  // In torque-limited planning, jerk limits are unchanged.
  if (options.has_dynamic_constraints()) {
    return absl::OkStatus();
  }

  // Check joint jerk constraints for all `path_segments`.
  if (options.kinematic_constraints().activate_joint_jerk_constraints()) {
    for (PathSegment& path_segment : path_segments) {
      for (int dof = 0; dof < path_segment.joint_limits.max_jerk.size();
           ++dof) {
        path_segment.joint_limits.max_jerk[dof] =
            GetBoundedJerk(path_segment.joint_limits.max_acceleration[dof],
                           path_segment.joint_limits.max_jerk[dof]);
      }
    }
  }

  return absl::OkStatus();
}

}  // namespace intrinsic::topp
