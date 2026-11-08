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

#include "intrinsic/icon/control/c_api/external_action_api/icon_feature_interfaces.h"

#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/control/c_api/c_feature_interfaces.h"
#include "intrinsic/icon/control/c_api/c_types.h"
#include "intrinsic/icon/control/c_api/convert_c_realtime_status.h"
#include "intrinsic/icon/control/c_api/convert_c_types.h"
#include "intrinsic/icon/control/joint_position_command.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/icon/utils/realtime_status_or.h"
#include "intrinsic/kinematics/types/joint_limits.h"
#include "intrinsic/kinematics/types/joint_state.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/twist.h"

namespace intrinsic::icon {

JointPositionCommand
IconConstJointPositionCommandInterface::PreviousPositionSetpoints() const {
  return Convert(
      joint_position_vtable_.previous_position_setpoints(joint_position_c_));
}

RealtimeStatus IconJointPositionCommandInterface::SetPositionSetpoints(
    const JointPositionCommand& setpoints) {
  IntrinsicIconJointPositionCommand cmd = Convert(setpoints);
  return ToRealtimeStatus(
      joint_position_vtable_.set_position_setpoints(joint_position_c_, &cmd));
}

JointPositionCommand
IconJointPositionCommandInterface::PreviousPositionSetpoints() const {
  return Convert(
      joint_position_vtable_.previous_position_setpoints(joint_position_c_));
}

JointStateP IconJointPositionSensor::GetSensedPosition() const {
  return Convert(joint_position_sensor_vtable_.get_sensed_position(
      joint_position_sensor_c_));
}

JointStateV IconJointVelocityEstimator::GetVelocityEstimate() const {
  return Convert(joint_velocity_estimator_vtable_.get_velocity_estimate(
      joint_velocity_estimator_c_));
}

JointLimits IconJointLimits::GetApplicationLimits() const {
  return Convert(joint_limits_vtable_.get_application_limits(joint_limits_c_));
}

JointLimits IconJointLimits::GetSystemLimits() const {
  return Convert(joint_limits_vtable_.get_system_limits(joint_limits_c_));
}

Wrench IconConstForceTorqueSensor::WrenchAtTip() const {
  return Convert(
      force_torque_sensor_vtable_.wrench_at_tip(force_torque_sensor_c_));
}

Wrench IconForceTorqueSensor::WrenchAtTip() const {
  return Convert(
      force_torque_sensor_vtable_.wrench_at_tip(force_torque_sensor_c_));
}

RealtimeStatus IconForceTorqueSensor::Tare() {
  return ToRealtimeStatus(
      force_torque_sensor_vtable_.tare(force_torque_sensor_c_));
}

RealtimeStatusOr<Pose3d> IconManipulatorKinematics::ComputeChainFK(
    const JointStateP dof_positions) const {
  IntrinsicIconPose3d out;
  IntrinsicIconJointStateP state_c = Convert(dof_positions);
  RealtimeStatus status =
      ToRealtimeStatus(manipulator_kinematics_vtable_.compute_chain_fk(
          manipulator_kinematics_c_, &state_c, &out));
  if (!status.ok()) {
    return status;
  }
  return Convert(out);
}

RealtimeStatusOr<eigenmath::Matrix6Nd>
IconManipulatorKinematics::ComputeChainJacobian(
    const JointStateP dof_positions) const {
  IntrinsicIconMatrix6Nd out;
  IntrinsicIconJointStateP state_c = Convert(dof_positions);
  RealtimeStatus status =
      ToRealtimeStatus(manipulator_kinematics_vtable_.compute_chain_jacobian(
          manipulator_kinematics_c_, &state_c, &out));
  if (!status.ok()) {
    return status;
  }
  return Convert(out);
}

IconConstFeatureInterfaces FromCApiFeatureInterfaces(
    IntrinsicIconConstFeatureInterfacesForSlot const_feature_interfaces,
    const IntrinsicIconFeatureInterfaceVtable feature_interface_vtable) {
  IconConstFeatureInterfaces out;
  if (const_feature_interfaces.joint_limits != nullptr) {
    out.joint_limits = IconJointLimits(const_feature_interfaces.joint_limits,
                                       feature_interface_vtable.joint_limits);
  }
  if (const_feature_interfaces.joint_position_sensor != nullptr) {
    out.joint_position_sensor =
        IconJointPositionSensor(const_feature_interfaces.joint_position_sensor,
                                feature_interface_vtable.joint_position_sensor);
  }
  if (const_feature_interfaces.joint_velocity_estimator != nullptr) {
    out.joint_velocity_estimator = IconJointVelocityEstimator(
        const_feature_interfaces.joint_velocity_estimator,
        feature_interface_vtable.joint_velocity_estimator);
  }
  if (const_feature_interfaces.joint_position != nullptr) {
    out.joint_position = IconConstJointPositionCommandInterface(
        const_feature_interfaces.joint_position,
        feature_interface_vtable.joint_position);
  }
  if (const_feature_interfaces.manipulator_kinematics != nullptr) {
    out.manipulator_kinematics = IconManipulatorKinematics(
        const_feature_interfaces.manipulator_kinematics,
        feature_interface_vtable.manipulator_kinematics);
  }
  if (const_feature_interfaces.force_torque_sensor != nullptr) {
    out.force_torque_sensor = IconConstForceTorqueSensor(
        const_feature_interfaces.force_torque_sensor,
        feature_interface_vtable.force_torque_sensor);
  }
  return out;
}

IconFeatureInterfaces FromCApiFeatureInterfaces(
    IntrinsicIconFeatureInterfacesForSlot feature_interfaces,
    IntrinsicIconFeatureInterfaceVtable feature_interface_vtable) {
  IconFeatureInterfaces out;
  if (feature_interfaces.joint_limits != nullptr) {
    out.joint_limits = IconJointLimits(feature_interfaces.joint_limits,
                                       feature_interface_vtable.joint_limits);
  }
  if (feature_interfaces.joint_position_sensor != nullptr) {
    out.joint_position_sensor =
        IconJointPositionSensor(feature_interfaces.joint_position_sensor,
                                feature_interface_vtable.joint_position_sensor);
  }
  if (feature_interfaces.joint_velocity_estimator != nullptr) {
    out.joint_velocity_estimator = IconJointVelocityEstimator(
        feature_interfaces.joint_velocity_estimator,
        feature_interface_vtable.joint_velocity_estimator);
  }
  if (feature_interfaces.joint_position != nullptr) {
    out.joint_position = IconJointPositionCommandInterface(
        feature_interfaces.joint_position,
        feature_interface_vtable.joint_position);
  }
  if (feature_interfaces.manipulator_kinematics != nullptr) {
    out.manipulator_kinematics = IconManipulatorKinematics(
        feature_interfaces.manipulator_kinematics,
        feature_interface_vtable.manipulator_kinematics);
  }
  if (feature_interfaces.force_torque_sensor != nullptr) {
    out.force_torque_sensor =
        IconForceTorqueSensor(feature_interfaces.force_torque_sensor,
                              feature_interface_vtable.force_torque_sensor);
  }
  return out;
}

}  // namespace intrinsic::icon
