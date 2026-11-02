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

#ifndef INTRINSIC_MOTION_PLANNING_TRAJECTORY_PLANNING_TOPP_TOPP_SELECTION_CRITERIA_H_
#define INTRINSIC_MOTION_PLANNING_TRAJECTORY_PLANNING_TOPP_TOPP_SELECTION_CRITERIA_H_

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "intrinsic/motion_planning/path_planning/path_segment.h"
#include "intrinsic/motion_planning/trajectory_planning/topp/joint_optimization_options.pb.h"

namespace intrinsic::topp {

// Refer to go/intrinsic-switch-mechanism-topp-algorithm for more details about
// the considerations taken to design this criteria.

// The maximum absolute value of jerk limits that can be used. It will be used
// within `UpdateJerkLimits` to bound the absolute value of jerk-limit values
// for all degrees of freedom. This value has been discovered to be the upper
// bound for what TOPP-SOCP can handle robustly.
constexpr double kMaxAbsoluteJerkLimitRadSec3 = 5000.0;

// The maximum relative value of jerk limit to acceleration limit for a degree
// of freedom. It will be used in `UpdateJerkLimits` to bound the relative value
// of the jerk-limit for a degree of freedom based on its acceleration-limit and
// this ratio.
constexpr double kMaxJerkToAccelerationLimitRatio = 80.0;

// The value of jerk to acceleration limit ratio for a given degree of freedom,
// upon which a degree of freedom is considered to be only acceleration-limited.
// It will be used in `UseJerkLimitedTopp` to decide on the algorithm to be
// used. If the ratios of all degrees of freedom is above this limit, then the
// acceleration-limited algorithm will be suggested.
constexpr double kJerkToAccelerationLimitRatioForUnlimitedJerk = 800.0;

// Determines whether to use the jerk-limited TOPP algorithm based on the
// provided options and path segments. Returns `true` if the jerk-limited (or
// torque-rate limited) algorithm is suggested, and `false` if the
// acceleration-limited (or torque-limited) algorithm is sufficient.
// Logic:
// 1. If `joint_jerk` or `torque_rate` constraints are disabled in `options`,
//    returns `false` (acceleration-limited).
// 2. If enabled, analyzes `path_segments` to determine if jerk constraints
//    are actually required for the trajectory generation.
// Note: Torque-rate constraints are treated equivalently to jerk constraints.
absl::StatusOr<bool> UseJerkLimitedTopp(
    const OptimizationOptions& options,
    absl::Span<const PathSegment> path_segments);

// Adjusts the jerk limits across all `path_segments` to appropriate upper
// bounds to improve feasibility. This adjustment is applied only to kinematic
// models (joint jerk and Cartesian jerk constraints). If a dynamic model
// (torque-limited planning) is selected in `options`, jerk limits are not
// modified, ensuring the robot's dynamic capabilities are exploited to the
// maximum extent.
absl::Status AdaptJerkLimitsForBetterFeasibility(
    const OptimizationOptions& options, absl::Span<PathSegment> path_segments);

}  // namespace intrinsic::topp

#endif  // INTRINSIC_MOTION_PLANNING_TRAJECTORY_PLANNING_TOPP_TOPP_SELECTION_CRITERIA_H_
