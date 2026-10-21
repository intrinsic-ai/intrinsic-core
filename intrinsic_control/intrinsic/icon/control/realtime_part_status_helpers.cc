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

#include "intrinsic/icon/control/realtime_part_status_helpers.h"

#include "absl/log/check.h"
#include "absl/time/time.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/control/parts/feature_interface_registry.h"
#include "intrinsic/icon/control/parts/feature_interfaces.h"
#include "intrinsic/icon/control/parts/realtime_part_status.h"
#include "intrinsic/icon/control/realtime_operational_status.h"
#include "intrinsic/icon/utils/log.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/icon/utils/realtime_status_macro.h"
#include "intrinsic/icon/utils/realtime_status_or.h"
#include "intrinsic/kinematics/types/cart_state.h"
#include "intrinsic/kinematics/types/joint_state.h"

namespace intrinsic::icon {
namespace {

RealtimeStatusOr<intrinsic::CartStatePVA> ComputeFK(
    const intrinsic::JointStatePVA& joint_state,
    const ManipulatorKinematics& kinematics) {
  intrinsic::CartStatePVA base_state_tip;
  INTRINSIC_RT_ASSIGN_OR_RETURN(base_state_tip.pose,
                                kinematics.ComputeChainFK(joint_state));

  INTRINSIC_RT_ASSIGN_OR_RETURN(eigenmath::Matrix6Nd tip_frame_in_base_jacobian,
                                kinematics.ComputeChainJacobian(joint_state));

  base_state_tip.velocity = tip_frame_in_base_jacobian * joint_state.velocity;
  base_state_tip.acceleration =
      tip_frame_in_base_jacobian * joint_state.acceleration;

  return base_state_tip;
}

}  // namespace

RealtimePartStatus ExtractRealtimePartStatus(
    const FeatureInterfaceRegistry& feature_interface_registry,
    absl::Duration time_since_server_start,
    const RealtimeOperationalStatus& operational_status) {
  RealtimePartStatus part_status;
  // Set commanded position from last cycle.
  part_status.time_since_timeslicer_epoch = time_since_server_start;
  part_status.operational_status = operational_status;
  if (const auto* position_command_interface =
          feature_interface_registry.GetInterface<JointPosition>();
      position_command_interface != nullptr) {
    part_status.position_commanded_last_cycle =
        position_command_interface->PreviousPositionSetpoints().position();
    part_status.velocity_commanded_last_cycle =
        position_command_interface->PreviousPositionSetpoints()
            .velocity_feedforward();
    part_status.acceleration_commanded_last_cycle =
        position_command_interface->PreviousPositionSetpoints()
            .acceleration_feedforward();
  }

  if (const auto* acc_command_interface =
          feature_interface_registry.GetInterface<JointAcceleration>();
      acc_command_interface != nullptr) {
    part_status.acceleration_commanded_last_cycle =
        acc_command_interface->PreviousAccelerationSetpoints().acceleration();
    part_status.torque_commanded_last_cycle =
        acc_command_interface->PreviousAccelerationSetpoints().torque();
  }

  if (const auto* torque_command_interface =
          feature_interface_registry.GetInterface<JointTorque>();
      torque_command_interface != nullptr) {
    part_status.torque_commanded_last_cycle =
        torque_command_interface->PreviousTorqueSetpoints();
  }

  // Set velocity and acceleration early so it can be employed in the forward
  // kinematics.
  if (const auto* joint_velocity_estimator =
          feature_interface_registry.GetInterface<JointVelocityEstimator>();
      joint_velocity_estimator != nullptr) {
    part_status.sensed_velocity =
        joint_velocity_estimator->GetVelocityEstimate();
  }
  if (const auto* joint_acceleration_estimator =
          feature_interface_registry.GetInterface<JointAccelerationEstimator>();
      joint_acceleration_estimator != nullptr) {
    part_status.sensed_acceleration =
        joint_acceleration_estimator->GetAccelerationEstimate();
  }
  if (const auto* position_sensor =
          feature_interface_registry.GetInterface<JointPositionSensor>();
      position_sensor != nullptr) {
    part_status.sensed_position = position_sensor->GetSensedPosition();
    if (auto* kinematics =
            feature_interface_registry.GetInterface<ManipulatorKinematics>();
        kinematics != nullptr) {
      JointStatePVA joint_state_sensed;
      CHECK_EQ(joint_state_sensed.SetSize(
                   part_status.sensed_position.value().size()),
               icon::OkStatus());
      joint_state_sensed.position =
          part_status.sensed_position.value().position;
      // Use sensed_velocity if present.
      if (part_status.sensed_velocity.has_value() &&
          part_status.sensed_velocity->size() ==
              joint_state_sensed.position.size()) {
        joint_state_sensed.velocity = part_status.sensed_velocity->velocity;
      }
      // Use sensed_acceleration if present.
      if (part_status.sensed_acceleration.has_value() &&
          part_status.sensed_acceleration->size() ==
              joint_state_sensed.position.size()) {
        joint_state_sensed.acceleration =
            part_status.sensed_acceleration->acceleration;
      }

      if (auto base_tip_sensed_or = ComputeFK(joint_state_sensed, *kinematics);
          base_tip_sensed_or.ok()) {
        part_status.base_t_tip_sensed = base_tip_sensed_or.value().pose;
        part_status.base_twist_tip_sensed = base_tip_sensed_or.value().velocity;
      } else {
        INTRINSIC_RT_LOG_THROTTLED(ERROR)
            << "Failed to compute FK in PartStatus: "
            << base_tip_sensed_or.status().message();
      }
    }
  }
  if (const auto* torque_sensor =
          feature_interface_registry.GetInterface<JointTorqueSensor>();
      torque_sensor != nullptr) {
    part_status.sensed_torque = torque_sensor->GetSensedTorque();
  }
  if (const auto* gripper_feedback =
          feature_interface_registry.GetInterface<SimpleGripper>();
      gripper_feedback != nullptr) {
    part_status.gripper_state = gripper_feedback->GetGripperState();
  }
  if (const auto* linear_gripper_feedback =
          feature_interface_registry.GetInterface<LinearGripper>();
      linear_gripper_feedback != nullptr) {
    part_status.linear_gripper_width_sensed =
        linear_gripper_feedback->GetGripperWidth();
  }

  if (const auto* ft_sensor_interface =
          feature_interface_registry.GetInterface<ForceTorqueSensor>();
      ft_sensor_interface != nullptr) {
    part_status.wrench_at_ft_uncompensated =
        ft_sensor_interface->WrenchAtSensorUncompensated();
    part_status.wrench_at_ft = ft_sensor_interface->WrenchAtSensor();
    part_status.wrench_at_tip = ft_sensor_interface->WrenchAtTip();
    part_status.wrench_stability_index =
        ft_sensor_interface->WrenchStabilityIndex();
  }

  if (const auto* standalone_ft_sensor_interface =
          feature_interface_registry
              .GetInterface<StandaloneForceTorqueSensor>();
      standalone_ft_sensor_interface != nullptr) {
    part_status.wrench_at_ft_uncompensated =
        standalone_ft_sensor_interface->WrenchAtSensorUncompensated();
  }

  if (const auto* adio_part_interface =
          feature_interface_registry.GetInterface<ADIO>();
      adio_part_interface != nullptr) {
    part_status.adio_state = adio_part_interface->GetADIOState();
  }

  if (const auto* control_mode_exporter =
          feature_interface_registry.GetInterface<ControlModeExporter>();
      control_mode_exporter != nullptr) {
    part_status.current_control_mode =
        control_mode_exporter->GetCurrentControlMode();
  }

  if (const auto* rangefinder_interface =
          feature_interface_registry.GetInterface<RangeFinder>();
      rangefinder_interface != nullptr) {
    if (rangefinder_interface->IsMeasurementValid()) {
      part_status.rangefinder_distance =
          rangefinder_interface->GetSensedDistance();
    }
  }

  if (const auto* imu_interface =
          feature_interface_registry.GetInterface<InertialMeasurementUnit>();
      imu_interface != nullptr) {
    part_status.imu_sensed_orientation = imu_interface->GetSensedOrientation();
    part_status.imu_sensed_angular_velocity =
        imu_interface->GetSensedAngularVelocity();
    part_status.imu_sensed_linear_acceleration =
        imu_interface->GetSensedLinearAcceleration();
  }

  if (const auto* cartesian_position_interface =
          feature_interface_registry.GetInterface<CartesianPositionState>();
      cartesian_position_interface != nullptr) {
    part_status.sensed_pose = cartesian_position_interface->GetSensedPose();
  }

  return part_status;
}

}  // namespace intrinsic::icon
