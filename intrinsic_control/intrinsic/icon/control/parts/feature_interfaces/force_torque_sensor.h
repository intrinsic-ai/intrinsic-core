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

#ifndef INTRINSIC_ICON_CONTROL_PARTS_FEATURE_INTERFACES_FORCE_TORQUE_SENSOR_H_
#define INTRINSIC_ICON_CONTROL_PARTS_FEATURE_INTERFACES_FORCE_TORQUE_SENSOR_H_

#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/control/algorithms/linear_joint_acceleration_filter.h"
#include "intrinsic/icon/control/algorithms/wrench_stability_monitor.h"
#include "intrinsic/icon/control/parts/feature_interfaces.h"
#include "intrinsic/icon/control/parts/feature_interfaces/hal_feature_interface_base.h"
#include "intrinsic/icon/control/parts/hal/force_torque_sensor_part/force_sensor_controller_migration_utils.h"
#include "intrinsic/icon/control/parts/hal/force_torque_sensor_part/hal_force_torque_sensor_part_config.pb.h"
#include "intrinsic/icon/control/parts/realtime_part_interface.h"
#include "intrinsic/icon/control/services/world_service.h"
#include "intrinsic/icon/hal/hardware_interface_handle.h"
#include "intrinsic/icon/hal/interfaces/force_torque.fbs.h"
#include "intrinsic/icon/hal/interfaces/joint_state.fbs.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/icon/utils/realtime_status_or.h"
#include "intrinsic/icon/utils/sensor_utils.h"
#include "intrinsic/kinematics/elements.h"
#include "intrinsic/kinematics/skeleton.h"
#include "intrinsic/kinematics/state.h"
#include "intrinsic/kinematics/types/joint_state.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/twist.h"

namespace intrinsic::icon {

// Implementation of the ForceTorqueSensor feature interface.
class ForceTorqueSensorFeature : public HalFeatureInterfaceBase,
                                 public ForceTorqueSensor {
  using ForceTorqueStatusHardwareInterface =
      HardwareInterfaceHandle<intrinsic_fbs::ForceTorqueStatus>;
  using JointTorqueStateHardwareInterface =
      HardwareInterfaceHandle<intrinsic_fbs::JointTorqueState>;
  using ForceTorqueCommandHardwareInterface =
      MutableHardwareInterfaceHandle<intrinsic_fbs::ForceTorqueCommand>;
  using JointPositionStateHardwareInterface =
      HardwareInterfaceHandle<intrinsic_fbs::JointPositionState>;
  using JointVelocityStateHardwareInterface =
      HardwareInterfaceHandle<intrinsic_fbs::JointVelocityState>;
  using JointAccelerationStateHardwareInterface =
      HardwareInterfaceHandle<intrinsic_fbs::JointAccelerationState>;

 public:
  static absl::StatusOr<std::unique_ptr<ForceTorqueSensorFeature>> Create(
      const intrinsic_proto::icon::HalForceTorqueSensorPartConfig& config,
      const WorldService* world_service,
      std::variant<ForceTorqueStatusHardwareInterface,
                   JointTorqueStateHardwareInterface>
          input_status_handle,
      std::optional<ForceTorqueCommandHardwareInterface>
          force_torque_command_handle,
      JointPositionStateHardwareInterface joint_position_handle,
      absl::string_view robot_world_object_name, double control_frequency_hz,
      std::optional<JointVelocityStateHardwareInterface> joint_velocity_handle =
          std::nullopt,
      std::optional<JointAccelerationStateHardwareInterface>
          joint_acceleration_handle = std::nullopt);

  ForceTorqueSensorFeature(const ForceTorqueSensorFeature&) = delete;
  ForceTorqueSensorFeature& operator=(const ForceTorqueSensorFeature&) = delete;
  ForceTorqueSensorFeature(ForceTorqueSensorFeature&& other) = delete;
  ForceTorqueSensorFeature& operator=(ForceTorqueSensorFeature&& other) =
      delete;
  ~ForceTorqueSensorFeature() override = default;

  RealtimeStatus ReadStatus(
      RealtimePartInterface::ReadStatusParameters params) override;

  RealtimeStatus ApplyCommand(
      RealtimePartInterface::ApplyCommandParameters params) override;

  Wrench WrenchAtSensorUncompensated() const override {
    return force_sensor_variables_.wrench_at_ft_uncompensated;
  }

  Wrench WrenchAtSensor() const override {
    return force_sensor_variables_.wrench_at_ft;
  }

  Wrench WrenchAtTip() const override {
    return force_sensor_variables_.wrench_at_target;
  }

  double WrenchStabilityIndex() const override {
    return force_sensor_variables_.wrench_stability_index;
  }

  Wrench PostSensorDynamicLoadAtSensor() const override {
    return force_sensor_variables_.post_sensor_dynamic_load_at_ft;
  }

  Wrench PostSensorDynamicLoadAtTip() const override {
    return force_sensor_variables_.post_sensor_dynamic_load_at_tip;
  }

  RealtimeStatus Tare(int num_taring_cycles) override;
  RealtimeStatusOr<bool> TareIsDone() const override;

  // These methods are used by a part exposing this feature interface
  // implementation to get and set the mounted and grasped payloads. The part
  // handles the payload part properties and these methods are used to pipe
  // through those values.
  PostSensorPayload GetMountedPayload() const;
  PostSensorPayload GetGraspedPayload() const;
  void SetMountedPayload(const PostSensorPayload& payload);
  void SetGraspedPayload(const PostSensorPayload& payload);

 private:
  struct TaringState {
    bool requested = false;

    // Initialize with true, since taring will never be requested if
    // completed=false.
    bool completed = true;

    // Number of taring cycles that is sent to the FT module when requesting a
    // taring operation.
    int requested_num_taring_cycles = 1;
  };

  // These are constant variables configuring the behaviour of the feature
  // interface.
  struct ForceSensorConfig {
    // Name of the World RobotCollection for the robot that the F/T sensor is
    // attached to. We use this to retrieve a Skeleton from the World that
    // includes the kinematic chain from the robot's base to the F/T sensor (see
    // `ft_sensor_link_name`).
    std::string robot_collection_name;

    // Name of the target link - this is translated to an ID at initialization
    // time.
    std::string target_link_name;

    // Element ID of the target link (=tip).
    kinematics::ElementId target_link_element_id;

    // Name of the force-torque sensor link/frame - this is translated to an ID
    // at initialization time.
    std::optional<std::string> ft_sensor_link_name;

    // Element ID of the force-torque sensor link/frame.
    kinematics::ElementId ft_sensor_link_element_id;

    // The pose of the ft sensor wrt the target frame.
    Pose3d target_t_ft;

    // Number of constant readings to accept, this defaults to 10.
    int num_acceptable_constant_readings;

    // Use estimated joint acceleration and rigid body dynamics to obtain an
    // estimate of the dynamic load exerted by the support_mass on the
    // FT-sensor.
    bool estimate_post_sensor_dynamic_load;
  };

  // These are variables used by the feature interface that are updated with
  // each cycle.
  struct ForceSensorVariables {
    // Payload that is mounted after the force-torque sensor, e.g. the gripper.
    // This payload is combined with the grasped_payload during compensation.
    icon::PostSensorPayload mounted_payload;

    // Payload that is changed by actions of the robot itself, e.g. a grasped
    // object (without the end-effector itself). This payload is combined with
    // the mounted_payload during compensation.
    icon::PostSensorPayload grasped_payload;

    Wrench wrench_at_ft_uncompensated;
    Wrench wrench_at_ft;
    Wrench wrench_at_target;
    double wrench_stability_index = 0.0;
    Wrench post_sensor_dynamic_load_at_ft;
    Wrench post_sensor_dynamic_load_at_tip;

    // Wrench sensed at F/T sensor without postprocessing. Cached for detecting
    // constant readings, etc.
    eigenmath::Vector6d wrench_at_ft_unprocessed = eigenmath::Vector6d::Zero();

    // Sensor bias: Wrench at F/T sensor at time of taring, compensated for
    // mass.
    eigenmath::Vector6d wrench_at_ft_bias = eigenmath::Vector6d::Zero();

    // Estimated wrench at F/T sensor as a result of post-sensor mass.
    eigenmath::Vector6d wrench_at_ft_support_mass = eigenmath::Vector6d::Zero();
  };

  ForceTorqueSensorFeature(
      ForceSensorConfig force_sensor_config,
      ForceSensorVariables force_sensor_variables,
      std::unique_ptr<intrinsic::kinematics::Skeleton> skeleton,
      std::unique_ptr<intrinsic::kinematics::State> kinematics_state,
      std::unique_ptr<WrenchStabilityMonitor> wrench_stability_monitor,
      std::variant<ForceTorqueStatusHardwareInterface,
                   JointTorqueStateHardwareInterface>
          input_status_handle,
      std::optional<ForceTorqueCommandHardwareInterface> force_torque_command,
      JointPositionStateHardwareInterface&& joint_position_handle,
      std::optional<JointVelocityStateHardwareInterface> joint_velocity_handle =
          std::nullopt,
      std::optional<std::variant<
          HardwareInterfaceHandle<intrinsic_fbs::JointAccelerationState>,
          std::vector<intrinsic::icon::LinearJointAccelerationFilter>>>
          joint_acceleration = std::nullopt);

  RealtimeStatusOr<JointStatePVA> ReadJointState();
  // Kinematic state must be updated prior to this function if we are using
  // external_joint_torque_state_handle_ to compute a ft sensor reading.
  RealtimeStatusOr<eigenmath::Vector6d> ReadWrenchFromDevice(
      const JointStatePVA& current_full_joint_state);

  PostSensorPayload GetTotalPayload() const;

  TaringState taring_state_;
  TaringData taring_data_joint_torque_interface_only_;

  const ForceSensorConfig config_;

  ForceSensorVariables force_sensor_variables_;

  std::unique_ptr<intrinsic::kinematics::Skeleton> skeleton_;
  // Kinematics3 instance. Must not outlive `skeleton_`.
  std::unique_ptr<intrinsic::kinematics::State> kinematics_state_;

  // A monitor for FT high frequencies used to detect control instability.
  std::unique_ptr<WrenchStabilityMonitor> wrench_stability_monitor_;

  // One of these two fields will be populated, depending on how the force
  // reading is being computed.
  std::variant<ForceTorqueStatusHardwareInterface,
               JointTorqueStateHardwareInterface>
      input_status_handle_;

  // This will be provided if input_status_handle_ holds a
  // ForceTorqueStatusHardwareInterface.
  std::optional<ForceTorqueCommandHardwareInterface>
      force_torque_command_handle_;

  // connected arm related hw interfaces
  JointPositionStateHardwareInterface joint_position_handle_;
  std::optional<JointVelocityStateHardwareInterface> joint_velocity_handle_ =
      std::nullopt;

  ConstSensorReadingsCounter<eigenmath::Vector6d> constant_readings_counter_;

  std::optional<std::variant<
      HardwareInterfaceHandle<intrinsic_fbs::JointAccelerationState>,
      std::vector<intrinsic::icon::LinearJointAccelerationFilter>>>
      joint_acceleration_;

  const int ndof_;
};

}  // namespace intrinsic::icon
#endif  // INTRINSIC_ICON_CONTROL_PARTS_FEATURE_INTERFACES_FORCE_TORQUE_SENSOR_H_
