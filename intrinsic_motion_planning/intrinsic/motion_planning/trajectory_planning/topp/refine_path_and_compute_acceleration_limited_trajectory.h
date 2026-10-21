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

#ifndef INTRINSIC_MOTION_PLANNING_TRAJECTORY_PLANNING_TOPP_REFINE_PATH_AND_COMPUTE_ACCELERATION_LIMITED_TRAJECTORY_H_
#define INTRINSIC_MOTION_PLANNING_TRAJECTORY_PLANNING_TOPP_REFINE_PATH_AND_COMPUTE_ACCELERATION_LIMITED_TRAJECTORY_H_

#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "intrinsic/icon/dynamics/rigid_body_interface.h"
#include "intrinsic/kinematics/chain.h"
#include "intrinsic/math/spline/bspline_sampler.h"
#include "intrinsic/math/spline/uniform_between_knots_bspline_sampler.h"
#include "intrinsic/motion_planning/path_planning/path_segment.h"
#include "intrinsic/motion_planning/trajectory_planning/path_refinement/spline_based_path_refinement.h"
#include "intrinsic/motion_planning/trajectory_planning/topp/joint_optimization_options.pb.h"
#include "intrinsic/motion_planning/trajectory_planning/topp/path_and_trajectory.h"

namespace intrinsic::topp {

// Maximum joint acceleration that can be used with ToppRa, in rad/s^2. This is
// a conservative value that keeps fine interpolation feasible and avoids limit
// violations.
constexpr double kMaxJointAccelerationToppRa = 20;

// Refines raw geometric path segments into a densely sampled, continuous path
// using B-spline interpolation, using the provided custom `SplineSampler`.
//
// This method enforces zero path derivatives at the trajectory boundaries and
// dynamically calculates the sampling step to guarantee a minimum number of
// discrete samples (at least 25), ensuring stability even for very short paths.
//
// Parameters:
//  * `path_segments`: The geometric path description.
//  * `sampler`: The custom spline sampler to use for discretization.
//
// Returns:
//  A `SplineBasedPathRefinementResult`.
absl::StatusOr<topp::SplineBasedPathRefinementResult>
ComputeRefinedPathForAccelerationLimitedTrajectory(
    absl::Span<const PathSegment> path_segments,
    const BSplineSampler& sampler = UniformBetweenKnotsBSplineSampler());

// High-level convenience function that sequentially refines a raw geometric
// path and computes its time-optimal, acceleration-limited parameterization. It
// calls `ComputeRefinedPathForAccelerationLimitedTrajectory()` to refine the
// path and then computes an acceleration-limited trajectory.
//
// Parameters:
//  * `options`: Specifies the active optimization constraints.
//  * `path_segments`: The raw geometric path to be traversed.
//  * `chain`: The robot's kinematic chain (required if Cartesian constraints
//    are active or Cartesian arc lengths are needed).
//  * `dynamics`: The robot's rigid body dynamics interface (default: nullptr).
//  * `sampler`: The custom spline sampler to use for discretization.
//
// Returns:
//  A `PathAndTrajectory` containing the parameterized trajectory,
//  fully populated motion events,  
//  and refined path result.
absl::StatusOr<PathAndTrajectory>
RefinePathAndComputeAccelerationLimitedTrajectory(
    const OptimizationOptions& options,
    absl::Span<const PathSegment> path_segments,
    const kinematics::Chain* chain = nullptr,
    icon::RigidBodyInterface* dynamics = nullptr,
    const BSplineSampler& sampler = UniformBetweenKnotsBSplineSampler());

}  // namespace intrinsic::topp

#endif  // INTRINSIC_MOTION_PLANNING_TRAJECTORY_PLANNING_TOPP_REFINE_PATH_AND_COMPUTE_ACCELERATION_LIMITED_TRAJECTORY_H_
