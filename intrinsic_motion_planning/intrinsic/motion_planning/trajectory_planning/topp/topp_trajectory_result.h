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

#ifndef INTRINSIC_MOTION_PLANNING_TRAJECTORY_PLANNING_TOPP_TOPP_TRAJECTORY_RESULT_H_
#define INTRINSIC_MOTION_PLANNING_TRAJECTORY_PLANNING_TOPP_TOPP_TRAJECTORY_RESULT_H_

#include <memory>
#include <vector>

#include "absl/status/statusor.h"
#include "intrinsic/kinematics/types/joint_trajectories.h"

#include "intrinsic/motion_planning/proto/v1/motion_events.pb.h"

#include "intrinsic/motion_planning/trajectory_planning/topp/reachability_node.h"
#include "intrinsic/motion_planning/trajectory_planning/topp/squared_path_velocity_interface.h"
#include "intrinsic/motion_planning/trajectory_planning/topp/topp_trajectory_result.pb.h"

namespace intrinsic::topp {

// Represents the comprehensive output of a Time-Optimal Path Parameterization
// (TOPP) solver.
//
// This structure encapsulates both the discrete time-parameterized states and
// the continuous reachability profile used to generate them, alongside any
// event metadata associated with the trajectory.
struct ToppTrajectoryResult {
  // A discrete version of the solution. This trajectory contains the
  // sequence of joint states (position, velocity, acceleration) and the
  // corresponding optimal timestamps indicating when the robot should pass
  // through each waypoint to minimize time while strictly satisfying all
  // system limits.
  JointTrajectoryPVA trajectory;

  // A continuous representation of the parameterization. Specifically, this
  // holds a squared path velocity profile interface. This represents the
  // reachable phase-space velocity along the path and can be used for
  // fine-grained interpolation or further path adjustments.
  std::unique_ptr<SquaredPathVelocityInterface> squared_path_velocity;


  // A collection of motion events associated with the trajectory. These events
  // are mapped to specific keypoint times and Cartesian arc-lengths along the
  // parameterized path.
  std::vector<intrinsic_proto::motion_planning::v1::MotionEvent> motion_events;


  // Reachability corridor at each keyframe of the active path: the states
  // that are both reachable from the start and from which the goal remains
  // reachable, i.e. the intersection of the forward and backward reachable
  // sets. Every point of `trajectory` lies inside the region for its keyframe.
  //
  // Only the intersection is exposed. The two sets it is built from are solver
  // intermediates and are unsound as corridors on their own: the backward set
  // admits states that cannot be reached from the start, and the forward set
  // admits states from which the goal cannot be reached.
  std::vector<ReachabilityNode> intersected_reachability_nodes;
};

// Serializes a C++ `ToppTrajectoryResult` struct into its corresponding
// Protocol Buffer representation.
//
// Parameters:
//  * `topp_trajectory_result`: The C++ object to serialize.
//
// Returns:
//  The populated `intrinsic_proto::topp::ToppTrajectoryResult` proto message,
//  or an error status if the internal trajectory or continuous parameterization
//  fails to serialize.
absl::StatusOr<intrinsic_proto::topp::ToppTrajectoryResult> ToProto(
    const ToppTrajectoryResult& topp_trajectory_result);

// Deserializes a Protocol Buffer `ToppTrajectoryResult` message into its
// corresponding C++ representation.
//
// Parameters:
//  * `topp_trajectory_result_proto`: The proto message to deserialize.
//
// Returns:
//  The reconstructed `ToppTrajectoryResult` C++ struct. Returns an error status
//  if the proto contains invalid data, missing required fields, or if the
//  internal trajectory and B-spline representations fail to parse.
absl::StatusOr<ToppTrajectoryResult> FromProto(
    const intrinsic_proto::topp::ToppTrajectoryResult&
        topp_trajectory_result_proto);

}  // namespace intrinsic::topp

#endif  // INTRINSIC_MOTION_PLANNING_TRAJECTORY_PLANNING_TOPP_TOPP_TRAJECTORY_RESULT_H_
