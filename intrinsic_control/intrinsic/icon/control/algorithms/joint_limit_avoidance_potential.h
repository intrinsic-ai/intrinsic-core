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

#ifndef INTRINSIC_ICON_CONTROL_ALGORITHMS_JOINT_LIMIT_AVOIDANCE_POTENTIAL_H_
#define INTRINSIC_ICON_CONTROL_ALGORITHMS_JOINT_LIMIT_AVOIDANCE_POTENTIAL_H_

#include <cmath>

#include "intrinsic/eigenmath/types.h"
#include "intrinsic/kinematics/types/joint_limits.h"
#include "intrinsic/kinematics/types/joint_state.h"

namespace intrinsic {
namespace control {

// Repels joints from joint limits using a potential field.
// inactive in the range (lower_activation_threshold,
// upper_activation_threshold). Single dof implementation of joint limit
// avoidance using a quadratic potential field which kicks in when the joint
// state reaches the activation_threshold, and reaches a
// maximum state (given by maximum_absolute_torque) when the
// joint state reaches the joint limit. The output torque
// has the effect of repelling the joint from the joint limit.
double SingleJointAvoidanceTorque(double lower_joint_state_limit,
                                  double lower_activation_threshold,
                                  double upper_joint_state_limit,
                                  double upper_activation_threshold,
                                  double maximum_absolute_torque,
                                  double joint_state);

// Repels joints from joint limits using a potential field.
// inactive in the range (lower_activation_threshold,
// upper_activation_threshold). Single dof implementation of joint limit
// avoidance using a quadratic potential field which kicks in when the joint
// state reaches the activation_threshold, and reaches a
// maximum state (given by maximum_absolute_torque) when the
// joint state reaches the upper joint limit and minimum state (given by -
// minimum_absolute_torque) when the joint state reaches the lower joint
// limit and . The output torque has the effect of repelling the joint from the
// joint limit.
double SingleJointAvoidanceTorque(double lower_joint_state_limit,
                                  double lower_activation_threshold,
                                  double upper_joint_state_limit,
                                  double upper_activation_threshold,
                                  double minimum_absolute_torque,
                                  double maximum_absolute_torque,
                                  double joint_state);

// Multi-dof joint limit avoidance. Repels joints from state limits using a
// potential field. Inactive in the range
// (activation_thresholds.min_state(k),
// activation_thresholds.max_state(k)).
// for joint k
// Implements per-joint joint limit avoidance using SingleJointAvoidanceTorque.
//
// Example usage:

// //get joint limits, torque limits, activation thresholds, and state limits
// from robot model.
// // in control loop:
//
// while(should_live){
//   // fancy control loop
//   output_torque += jlp->ComputeJointLimitAvoidanceTorque(ndof,
//   joint_state, state_limits, activation_thresholds, max_torque);
//   // add joint_torque to whatever other joint torque control
//   // you are doing.
// }

eigenmath::VectorNd ComputeJointPositionLimitAvoidanceTorque(
    int ndof, const eigenmath::VectorNd& joint_state,
    const JointLimits& joint_limits, const JointLimits& activation_thresholds,
    const eigenmath::VectorNd& max_intervention_torque);

}  // namespace control
}  // namespace intrinsic

#endif  // INTRINSIC_ICON_CONTROL_ALGORITHMS_JOINT_LIMIT_AVOIDANCE_POTENTIAL_H_
