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

#ifndef INTRINSIC_ICON_ACTIONS_COMPLIANCE_INFO_H_
#define INTRINSIC_ICON_ACTIONS_COMPLIANCE_INFO_H_

namespace intrinsic::icon {

// Shared constants for the compliance actions like CartesianAdmittanceAction,
// TrajectoryAdmittanceAction, CartesianImpedanceAction and
// TrajectoryImpedanceAction
struct ComplianceInfo {
  static constexpr char kSlotName[] = "arm_with_ft_sensor";
  static constexpr char kSlotDescription[] =
      "A combined (position controlled) arm and Force/Torque sensor Part. In "
      "the future, this Action will accept two separate Parts instead.";
  static constexpr char kArmSlotName[] = "arm";
  static constexpr char kArmSlotDescription[] =
      "A robot arm that must support Cartesian motion (i.e. have sufficient "
      "degrees of freedom and a kinematics model).";
  static constexpr char kForceTorqueSlotName[] = "ft_sensor";
  static constexpr char kForceTorqueSlotDescription[] =
      "A Force/Torque sensor Part.";
  static constexpr char kIsDoneDescription[] = "The action is done.";
  static constexpr char kReferencePoseReached[] =
      "intrinsic.reference_pose_reached";
  static constexpr char kReferencePoseReachedDescription[] =
      "Returns true if the desired reference pose has been reached. ";
  static constexpr char kReferencePositionError[] =
      "intrinsic.reference_position_error";
  static constexpr char kReferencePositionErrorDescription[] =
      "Returns the distance between the desired reference position and the "
      "sensed position of the robot tool in meters.";
  static constexpr char kReferenceOrientationError[] =
      "intrinsic.reference_orientation_error";
  static constexpr char kReferenceOrientationErrorDescription[] =
      "Returns the angular distance between the desired reference orientation "
      "and the sensed orientation of the robot tool in radians.";
  static constexpr char kStreamingInputMessageCount[] =
      "intrinsic.streaming_input_message_count";
  static constexpr char kStreamingInputMessageCountDescription[] =
      "Returns the number of streaming input messages received since the "
      "action started.";
  static constexpr char kSettledForSeconds[] = "intrinsic.settled_for_seconds";
  static constexpr char kSettledForSecondsDescription[] =
      "Returns for how many seconds the robot state has been settled and all "
      "velocities have been close to zero. ";
  static constexpr char kSensedForce[] = "intrinsic.sensed_force";
  static constexpr char kSensedForceDescription[] =
      "Euclidean norm of the measured force at the end effector in [N]. ";
  static constexpr char kSensedTorque[] = "sensed_torque";
  static constexpr char kSensedTorqueDescription[] =
      "Euclidean norm of the measured torque at the end effector in [Nm]. ";

  // `intrinsic.sensed_force_in_direction_1` to 3 output the force in one
  // direction. This direction can be configured in the
  // StateVariableConfiguration. Default are the x, y and z axis. The state
  // variable reports the projected force onto the direction.
  //
  // Example:
  //
  // StateVariableConfiguration {
  //   force_direction_1_in_task { x: 1.0 y: 1.0 z: 0.0 }
  // }
  //
  // The state variable `intrinsic.sensed_force_in_direction_1` reports the
  // force projected onto the vector (1, 1, 0).
  //
  // In most cases the three direction vectors are linearly independent, but
  // this is not a requirement.

  static constexpr char kSensedForceInDirection1[] =
      "intrinsic.sensed_force_in_direction_1";
  static constexpr char kSensedForceInDirection1Description[] =
      "Euclidean norm of the measured force in the direction_1 in [N]. "
      "Configure the direction in the StateVariableConfiguration. Default is "
      "the x-axis.";
  static constexpr char kSensedForceInDirection2[] =
      "intrinsic.sensed_force_in_direction_2";
  static constexpr char kSensedForceInDirection2Description[] =
      "Euclidean norm of the measured force in the direction_2 in [N]. "
      "Configure the direction in the StateVariableConfiguration. Default is "
      "the y-axis.";
  static constexpr char kSensedForceInDirection3[] =
      "intrinsic.sensed_force_in_direction_3";
  static constexpr char kSensedForceInDirection3Description[] =
      "Euclidean norm of the measured force in the direction_3 in [N]. "
      "Configure the direction in the StateVariableConfiguration. Default is "
      "the z-axis.";

  static constexpr char kSensedTorqueAboutAxis1[] =
      "intrinsic.sensed_torque_about_axis_1";
  static constexpr char kSensedTorqueAboutAxis1Description[] =
      "Euclidean norm of the measured torque about the axis_1 in [Nm]. "
      "Configure the axis in the StateVariableConfiguration. Default is "
      "the x-axis.";
  static constexpr char kSensedTorqueAboutAxis2[] =
      "intrinsic.sensed_torque_about_axis_2";
  static constexpr char kSensedTorqueAboutAxis2Description[] =
      "Euclidean norm of the measured torque about the axis_2 in [Nm]. "
      "Configure the axis in the StateVariableConfiguration. Default is "
      "the y-axis.";
  static constexpr char kSensedTorqueAboutAxis3[] =
      "intrinsic.sensed_torque_about_axis_3";
  static constexpr char kSensedTorqueAboutAxis3Description[] =
      "Euclidean norm of the measured torque about the axis_3 in [Nm]. "
      "Configure the axis in the StateVariableConfiguration. Default is "
      "the z-axis.";

  static constexpr char kManipulabilityIndex[] = "manipulability";
  static constexpr char kManipulabilityIndexDescription[] =
      "Manipulability of the current joint configuration of the end-effector. ";
  static constexpr char kActionElapsedTimeSeconds[] = "elapsed_time_seconds";
  static constexpr char kActionElapsedTimeSecondsDescription[] =
      "Time elapsed running this action in [s]. ";
  static constexpr char kTranslationalDistanceTraveled[] =
      "intrinsic.translational_distance_traveled";
  static constexpr char kTranslationalDistanceTraveledDescription[] =
      "Euclidean norm of the translational distance traveled by the "
      "end-effector from the start of this action in [m].";
  static constexpr char kMaximumTranslationalDisplacementInWindow[] =
      "maximum_translational_displacement_in_time_window";
  static constexpr char kMaximumTranslationalDisplacementInWindowDescription[] =
      "Euclidean norm of the translational distance between the farthest two "
      "points of the end-effector within a given time window, in [m].";
  static constexpr char kIsSettled[] = "intrinsic.is_settled";
  static constexpr char kIsSettledDescription[] =
      "This Action reports 'settled' as soon as the robot has reached a "
      "settled state. It settles if the force stays in a band and joint "
      "velocities are small and near the desired joint velocity.";

  static constexpr char kStreamingInputName[] = "cartesian-compliance-command";
  static constexpr char kStreamingInputDescription[] =
      "Changes the command of the controller. Allows changing the target or "
      "the controller configuration.";

  static constexpr char kStreamingOutputName[] = "cartesian-compliance-status";
  static constexpr char kStreamingOutputDescription[] =
      "Reports the current internal status of the controller.";
};

}  // namespace intrinsic::icon

#endif  // INTRINSIC_ICON_ACTIONS_COMPLIANCE_INFO_H_
