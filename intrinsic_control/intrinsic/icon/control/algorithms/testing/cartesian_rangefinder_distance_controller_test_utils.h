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

#ifndef INTRINSIC_ICON_CONTROL_ALGORITHMS_TESTING_CARTESIAN_RANGEFINDER_DISTANCE_CONTROLLER_TEST_UTILS_H_
#define INTRINSIC_ICON_CONTROL_ALGORITHMS_TESTING_CARTESIAN_RANGEFINDER_DISTANCE_CONTROLLER_TEST_UTILS_H_

#include <cstddef>
#include <optional>
#include <vector>

#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/control/algorithms/cartesian_rangefinder_distance_controller.h"
#include "intrinsic/icon/control/algorithms/compute_critical_damping.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/icon/utils/realtime_status_macro.h"
#include "intrinsic/icon/utils/realtime_status_or.h"
#include "intrinsic/kinematics/types/cartesian_limits.h"
#include "intrinsic/kinematics/types/joint_limits.h"
#include "intrinsic/math/pose3.h"

namespace intrinsic::icon {

inline JointLimits DefaultJointLimits(size_t njoints) {
  INTRINSIC_RT_ASSIGN_OR_DIE(auto limits, JointLimits::Unlimited(njoints));
  limits.min_position.setConstant(-1.0);
  limits.max_position.setConstant(1.0);
  limits.max_velocity.setConstant(1.0);
  return limits;
}

inline JointLimits MH12JointLimits() {
  INTRINSIC_RT_ASSIGN_OR_DIE(auto limits, JointLimits::Unlimited(6));
  limits.min_position << -2.96, -1.57, -3.05, -3.14, -2.35, -6.28;
  limits.max_position << 2.96, 2.70, 4.18, 3.14, 2.35, 6.28;
  limits.max_velocity << 3.83, 3.49, 3.83, 7.15, 7.15, 10.64;
  limits.max_acceleration << 12.0, 12.0, 12.0, 12.0, 12.0, 12.0;
  return limits;
}

inline CartesianLimits DefaultCartesianLimits() {
  CartesianLimits cartesian_limits;
  cartesian_limits.max_translational_position.setConstant(1.0);
  cartesian_limits.min_translational_position.setConstant(-1.0);
  cartesian_limits.max_translational_velocity.setConstant(1.0);
  cartesian_limits.min_translational_velocity.setConstant(-1.0);
  cartesian_limits.max_translational_acceleration.setConstant(1.0);
  cartesian_limits.min_translational_acceleration.setConstant(-1.0);
  cartesian_limits.max_translational_jerk.setConstant(1.0);
  cartesian_limits.min_translational_jerk.setConstant(-1.0);
  cartesian_limits.max_rotational_velocity = 1.0;
  cartesian_limits.max_rotational_acceleration = 1.0;
  cartesian_limits.max_rotational_jerk = 1.0;
  return cartesian_limits;
}

inline CartesianLimits MH12CartesianLimits() {
  CartesianLimits cartesian_limits;
  cartesian_limits.max_translational_position.setConstant(10.0);
  cartesian_limits.min_translational_position.setConstant(-10.0);
  cartesian_limits.max_translational_velocity.setConstant(0.2);
  cartesian_limits.min_translational_velocity.setConstant(-0.2);
  cartesian_limits.max_translational_acceleration.setConstant(1.0);
  cartesian_limits.min_translational_acceleration.setConstant(-1.0);
  cartesian_limits.max_translational_jerk.setConstant(1.0);
  cartesian_limits.min_translational_jerk.setConstant(-1.0);
  cartesian_limits.max_rotational_velocity = 1.0;
  cartesian_limits.max_rotational_acceleration = 1.0;
  cartesian_limits.max_rotational_jerk = 1.0;
  return cartesian_limits;
}

inline CartesianRangefinderDistanceController::InputParameters
CreateValidParameters(size_t njoints) {
  CartesianRangefinderDistanceController::InputParameters p;

  p.joint_position_nullspace = eigenmath::VectorNd::Zero(njoints);

  p.z_reference_offset_safety_threshold = 0.5;
  p.z_reference_offset_tcp_frame = 0.1;

  p.cartesian_damping_tcp_frame = 1 * eigenmath::Matrix6d::Identity();
  p.cartesian_stiffness_tcp_frame = 10 * eigenmath::Matrix6d::Identity();

  p.jacobian_pinv_damping = 1e-3;
  p.nullspace_damping = 1.0;
  p.nullspace_stiffness = 1.0;
  p.low_pass_filter_gain = 0.5;

  p.joint_limits = DefaultJointLimits(njoints);
  p.cartesian_limits = DefaultCartesianLimits();

  return p;
}

inline CartesianRangefinderDistanceController::InputParameters
CreateMH12Parameters() {
  CartesianRangefinderDistanceController::InputParameters p;

  p.joint_limits = MH12JointLimits();
  p.cartesian_limits = MH12CartesianLimits();

  // The controller is tested with a relativ small stiffnes to ensure that the
  // motion stays in the joint limits. If the limit clamping is triggered the
  // EEF is changing its orientation and the distance approximation is invalid.
  p.cartesian_stiffness_tcp_frame = 50 * eigenmath::Matrix6d::Identity();
  INTRINSIC_RT_ASSIGN_OR_DIE(
      p.cartesian_damping_tcp_frame,
      ComputeCriticalDamping(eigenmath::Matrix6d::Identity(),
                             p.cartesian_stiffness_tcp_frame));

  p.z_reference_offset_safety_threshold = 0.5;
  p.z_reference_offset_tcp_frame = 0.1;

  p.jacobian_pinv_damping = 1e-3;
  p.low_pass_filter_gain = 0.5;

  return p;
}

inline std::vector<Pose3d> CreateMH12Poses() {
  std::vector<Pose3d> poses;
  // Create a basic trajectory
  eigenmath::Quaterniond orientation =
      eigenmath::QuaternionFromRPY(0.0, 3.141, 3.141);
  poses.emplace_back(Pose3d(orientation, eigenmath::Vector3d(0.8, 0.0, 0.4)));
  poses.emplace_back(Pose3d(orientation, eigenmath::Vector3d(1.2, 0.0, 0.4)));
  poses.emplace_back(Pose3d(orientation, eigenmath::Vector3d(0.8, 0.0, 0.4)));
  poses.emplace_back(Pose3d(orientation, eigenmath::Vector3d(0.8, -0.4, 0.4)));
  poses.emplace_back(Pose3d(orientation, eigenmath::Vector3d(0.8, 0.4, 0.4)));
  poses.emplace_back(Pose3d(orientation, eigenmath::Vector3d(0.8, 0.0, 0.4)));
  return poses;
}

}  // namespace intrinsic::icon

#endif  // INTRINSIC_ICON_CONTROL_ALGORITHMS_TESTING_CARTESIAN_RANGEFINDER_DISTANCE_CONTROLLER_TEST_UTILS_H_
