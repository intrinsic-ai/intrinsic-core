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

#include "intrinsic/icon/control/algorithms/joint_limit_avoidance_potential.h"

#include <cstdlib>

#include "absl/log/check.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/utils/log.h"
#include "intrinsic/kinematics/types/joint_limits.h"

namespace intrinsic {

namespace control {

double SingleJointAvoidanceTorque(double lower_joint_state_limit,
                                  double lower_activation_threshold,
                                  double upper_joint_state_limit,
                                  double upper_activation_threshold,
                                  double maximum_absolute_torque,
                                  double joint_state) {
  return SingleJointAvoidanceTorque(
      lower_joint_state_limit, lower_activation_threshold,
      upper_joint_state_limit, upper_activation_threshold,
      maximum_absolute_torque, maximum_absolute_torque, joint_state);
}

double SingleJointAvoidanceTorque(double lower_joint_state_limit,
                                  double lower_activation_threshold,
                                  double upper_joint_state_limit,
                                  double upper_activation_threshold,
                                  double minimum_absolute_torque,
                                  double maximum_absolute_torque,
                                  double joint_state) {
  CHECK_NE(lower_joint_state_limit, lower_activation_threshold)
      << absl::StrFormat(
             "Joint limit [%f] equals activation threshold [%f], divide by 0",
             lower_joint_state_limit, lower_activation_threshold);
  CHECK_NE(upper_joint_state_limit, upper_activation_threshold)
      << absl::StrFormat(
             "Joint limit [%f] equals activation threshold [%f], divide by 0",
             upper_joint_state_limit, upper_activation_threshold);
  CHECK_GT(upper_joint_state_limit, upper_activation_threshold)
      << absl::StrFormat(
             "Upper joint limit [%f] less than upper activation "
             "threshold [%f], invalid",
             upper_joint_state_limit, upper_activation_threshold);
  CHECK_LT(lower_joint_state_limit, lower_activation_threshold)
      << absl::StrFormat(
             "Lower joint limit [%f] greater than lower activation "
             "threshold [%f], invalid",
             upper_joint_state_limit, upper_activation_threshold);
  double torque = 0.0;
  // the gain that should be used to reach the maximum_absolute_torque
  // at the joint limit
  double upper_gain =
      std::abs(minimum_absolute_torque /
               std::abs(upper_joint_state_limit - upper_activation_threshold));
  double lower_gain =
      maximum_absolute_torque /
      std::abs(lower_joint_state_limit - lower_activation_threshold);

  // we are past the joint limit, saturate torque output
  if (joint_state > upper_joint_state_limit) {
    torque =
        upper_gain * (upper_activation_threshold - upper_joint_state_limit);
    INTRINSIC_RT_LOG_THROTTLED(ERROR)
        << "Joint limit avoidance potential saturated at upper limit!";
  } else if (joint_state > upper_activation_threshold) {
    // activation_threshold < joint_state < joint_limit
    // We are in the active zone. Compute torque according to
    // joint_state.
    INTRINSIC_RT_LOG_THROTTLED(ERROR)
        << "Joint limit avoidance potential is active at upper limit!";
    torque = upper_gain * (upper_activation_threshold - joint_state);
  } else if (joint_state < lower_joint_state_limit) {
    torque =
        lower_gain * (lower_activation_threshold - lower_joint_state_limit);
    INTRINSIC_RT_LOG_THROTTLED(ERROR)
        << "Joint limit avoidance potential saturated at lower limit!";
  } else if (joint_state < lower_activation_threshold) {
    // activation_threshold < joint_state < joint_limit
    // We are in the active zone. Compute torque according to
    // joint_state.
    INTRINSIC_RT_LOG_THROTTLED(ERROR)
        << "Joint limit avoidance potential active at lower limit!";
    torque = lower_gain * (lower_activation_threshold - joint_state);
  } else {
    // joint_state < activation_threshold
    // We are outside the active zone. No torque output.
    torque = 0.0;
  }
  return torque;
}

eigenmath::VectorNd ComputeJointPositionLimitAvoidanceTorque(
    int ndof, const eigenmath::VectorNd& joint_state,
    const JointLimits& joint_limits, const JointLimits& activation_thresholds,
    const eigenmath::VectorNd& max_intervention_torque) {
  CHECK_EQ(activation_thresholds.min_position.size(), ndof) << absl::StrFormat(
      "wrong dimension for lower activation thresholds, need %d,  have %ld",
      ndof, activation_thresholds.min_position.size());
  CHECK_EQ(activation_thresholds.max_position.size(), ndof) << absl::StrFormat(
      "wrong dimension for upper activation thresholds, need %d,  have %ld",
      ndof, activation_thresholds.max_position.size());
  CHECK_EQ(joint_limits.min_position.size(), ndof) << absl::StrFormat(
      "wrong dimension for lower state limits, need %d,  have %ld", ndof,
      joint_limits.min_position.size());
  CHECK_EQ(joint_limits.max_position.size(), ndof) << absl::StrFormat(
      "wrong dimension for upper state limits, need %d,  have %ld", ndof,
      joint_limits.max_position.size());
  CHECK_EQ(max_intervention_torque.size(), ndof) << absl::StrFormat(
      "wrong dimension for max intervention torque, need %d,  have %ld", ndof,
      max_intervention_torque.size());

  eigenmath::VectorNd joint_torque(ndof);
  CHECK_EQ(joint_state.size(), ndof) << absl::StrFormat(
      "wrong dimension for joint_state vector [%ld], expected [%d]",
      joint_state.size(), ndof);
  for (int k = 0; k < ndof; ++k) {
    joint_torque(k) = SingleJointAvoidanceTorque(
        joint_limits.min_position(k), activation_thresholds.min_position(k),
        joint_limits.max_position(k), activation_thresholds.max_position(k),
        max_intervention_torque(k), joint_state(k));
  }
  return joint_torque;
}

}  // namespace control

}  // namespace intrinsic
