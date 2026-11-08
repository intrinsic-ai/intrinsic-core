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

#ifndef INTRINSIC_ICON_CONTROL_PARTS_HAL_FORCE_TORQUE_SENSOR_PART_HAL_FORCE_TORQUE_SENSOR_PART_TEST_FIXTURE_H_
#define INTRINSIC_ICON_CONTROL_PARTS_HAL_FORCE_TORQUE_SENSOR_PART_HAL_FORCE_TORQUE_SENSOR_PART_TEST_FIXTURE_H_

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "grpcpp/server_builder.h"
#include "intrinsic/eigenmath/matchers.h"
#include "intrinsic/eigenmath/pose3_utils.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/control/context.h"
#include "intrinsic/icon/control/parts/feature_interfaces.h"
#include "intrinsic/icon/control/parts/hal/force_torque_sensor_part/hal_force_torque_sensor_part.h"
#include "intrinsic/icon/control/parts/hal/force_torque_sensor_part/hal_force_torque_sensor_part_config.pb.h"
#include "intrinsic/icon/control/parts/hal/v1/hal_part_config.pb.h"
#include "intrinsic/icon/control/parts/new_manipulator_kinematics.h"
#include "intrinsic/icon/control/parts/part_test_helper.h"
#include "intrinsic/icon/control/realtime_operational_status.h"
#include "intrinsic/icon/control/services/kinematics_from_world_service.h"
#include "intrinsic/icon/control/services/kinematics_service.h"
#include "intrinsic/icon/control/services/service_collection.h"
#include "intrinsic/icon/control/services/world_from_grpc_service.h"
#include "intrinsic/icon/control/services/world_service.h"
#include "intrinsic/icon/hal/default_hardware_interfaces.h"
#include "intrinsic/icon/hal/hardware_interface_handle.h"
#include "intrinsic/icon/hal/hardware_interface_registry.h"
#include "intrinsic/icon/hal/hardware_module_init_context.h"
#include "intrinsic/icon/hal/hardware_module_interface.h"
#include "intrinsic/icon/hal/hardware_module_manager.h"
#include "intrinsic/icon/hal/hardware_module_proxy.h"
#include "intrinsic/icon/hal/hardware_module_runtime.h"
#include "intrinsic/icon/hal/interfaces/force_sensor.fbs.h"
#include "intrinsic/icon/hal/interfaces/force_torque.fbs.h"
#include "intrinsic/icon/hal/interfaces/force_torque_utils.h"
#include "intrinsic/icon/hal/interfaces/joint_command.fbs.h"
#include "intrinsic/icon/hal/interfaces/joint_limits.fbs.h"
#include "intrinsic/icon/hal/interfaces/joint_state.fbs.h"
#include "intrinsic/icon/hal/module_config.h"
#include "intrinsic/icon/hal/proto/hardware_module_config.pb.h"
#include "intrinsic/icon/hardware_modules/fake/fake_module_config.pb.h"
#include "intrinsic/icon/interprocess/shared_memory_manager/shared_memory_manager.h"
#include "intrinsic/icon/interprocess/shared_memory_manager/testing/unique_segment_name.h"
#include "intrinsic/icon/proto/linear_joint_acceleration_filter_config.pb.h"
#include "intrinsic/icon/server/config/icon_main_config.pb.h"
#include "intrinsic/icon/utils/constants.h"
#include "intrinsic/icon/utils/realtime_guard.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/icon/utils/realtime_status_matchers.h"
#include "intrinsic/kinematics/ik/inverse_kinematics_interface.h"
#include "intrinsic/kinematics/skeleton.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/twist.h"
#include "intrinsic/util/proto/parse_text_proto.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/thread/rt_thread.h"
#include "intrinsic/util/thread/thread.h"
#include "intrinsic/util/thread/thread_options.h"
#include "intrinsic/world/component/ppr_component.h"
#include "intrinsic/world/entity.h"
#include "intrinsic/world/hashing/hashing.h"
#include "intrinsic/world/objects/object_world_ids.h"
#include "intrinsic/world/objects/test/object_world_test_utils.h"
#include "intrinsic/world/proto/kinematics_component.pb.h"
#include "intrinsic/world/service/test/world_service_fake.h"
#include "intrinsic/world/test/world_test_utils.h"
#include "intrinsic/world/world.h"

namespace intrinsic::icon {

using JointPositionCommandHardwareInterface =
    ::intrinsic_fbs::JointPositionCommand;
using JointTorqueCommandHardwareInterface = ::intrinsic_fbs::JointTorqueCommand;
using JointPositionStateHardwareInterface = ::intrinsic_fbs::JointPositionState;
using JointVelocityStateHardwareInterface = ::intrinsic_fbs::JointVelocityState;
using JointAccelerationStateHardwareInterface =
    ::intrinsic_fbs::JointAccelerationState;
using JointLimitsHardwareInterface = ::intrinsic_fbs::JointLimits;
using ForceTorqueStatusHardwareInterface = intrinsic_fbs::ForceTorqueStatus;
using ForceTorqueCommandHardwareInterface = intrinsic_fbs::ForceTorqueCommand;

constexpr double kSmallTolerance = 1e-5;
constexpr char kRobotCollectionName[] = "robot";
constexpr char kTargetLinkName[] = "link_wrist";
constexpr char kForceTorqueSensorLinkName[] = "ft_sensor_link";
constexpr char kFTModuleName[] = "ft_module";
constexpr char kArmModuleName[] = "arm_module";
constexpr char kHalForceTorqueSensorPartName[] = "hal_force_torque_sensor_part";
constexpr double kControlFrequencyHz = 250.0;
constexpr double kDefaultMountedPayloadMass = 1.5;
const eigenmath::Vector3d kDefaultMountedPayloadCoG = {1.0, 0.0, 0.0};

constexpr absl::string_view k3DofRobotNoClockConfig = R"pb(
  name: "fake_module"
  drives_realtime_clock: false
  module_config {
    [type.googleapis.com/intrinsic_proto.icon.FakeModuleConfig] {
      # 250Hz
      cycle_time { seconds: 0 nanos: 4000000 }
      arm_interfaces {
        joint_position_state_name: "joint_position_state"
        joint_velocity_state_name: "joint_velocity_state"
        joint_acceleration_state_name: "joint_acceleration_state"
        initial_joint_state {
          position_sensed: 0.0
          velocity_sensed: 0.0
          acceleration_sensed: 0.0
        }
        initial_joint_state {
          position_sensed: 0.0
          velocity_sensed: 0.0
          acceleration_sensed: 0.0
        }
        initial_joint_state {
          position_sensed: 0.0
          velocity_sensed: 0.0
          acceleration_sensed: 0.0
        }
      }
    }
  }
)pb";

// Creates default linear joint acceleration filter config for tests.
intrinsic_proto::icon::LinearJointAccelerationFilterConfig
CreateDefaultLinearJointAccelerationFilterConfig();

// Creates a simple test world with 3 DoF robot (elbow-wrist configuration).
absl::StatusOr<World> MakeDefaultRobotWorld(
    bool moveable_sensor_link = false,
    const Pose3d& tip_t_ft = Pose3d{eigenmath::Vector3d(0.1, 0, 0)});

// Mock hardware module interface simulating arm joint states for testing.
class ArmModule final : public intrinsic::icon::HardwareModuleInterface {
 public:
  static constexpr char kPositionStateInterfaceName[] = "joint_position_state";
  static constexpr char kVelocityStateInterfaceName[] = "joint_velocity_state";
  static constexpr char kAccelerationStateInterfaceName[] =
      "joint_acceleration_state";
  static constexpr char kExternalJointTorqueInterfaceName[] =
      "external_joint_torque";

  // Initializes arm hardware module interface and registers shared memory
  // interfaces.
  absl::Status Init(
      intrinsic::icon::HardwareModuleInitContext& init_context) override;

  RealtimeStatus Activate() override { return OkStatus(); }
  RealtimeStatus Deactivate() override { return OkStatus(); }
  absl::Status EnableMotion() override { return absl::OkStatus(); }
  absl::Status DisableMotion() override { return absl::OkStatus(); }
  absl::Status ClearFaults() override { return absl::OkStatus(); }
  absl::Status Shutdown() override { return absl::OkStatus(); }
  RealtimeStatus ApplyCommand() override { return OkStatus(); }
  RealtimeStatus ReadStatus() override { return OkStatus(); }

  RealtimeStatus SetJointPositionState(eigenmath::VectorNd position);
  RealtimeStatus SetJointVelocityState(eigenmath::VectorNd velocity);
  RealtimeStatus SetJointAccelerationState(eigenmath::VectorNd acceleration);
  RealtimeStatus SetExternalJointTorque(eigenmath::VectorNd torque);

 private:
  MutableHardwareInterfaceHandle<JointPositionStateHardwareInterface>
      joint_position_state_;
  MutableHardwareInterfaceHandle<JointVelocityStateHardwareInterface>
      joint_velocity_state_;
  MutableHardwareInterfaceHandle<JointAccelerationStateHardwareInterface>
      joint_acceleration_state_;
  MutableHardwareInterfaceHandle<intrinsic_fbs::JointTorqueState>
      external_joint_torque_state_;
};

// Mock hardware module interface simulating force torque sensor readings for
// testing.
class ForceTorqueModule final
    : public intrinsic::icon::HardwareModuleInterface {
 public:
  static constexpr char kForceTorqueCommand[] = "force_torque_sensor_command";
  static constexpr char kForceTorqueStatus[] = "force_torque_sensor_status";

  enum class State : int8_t {
    kDeactivated = 0,
    kActivated = 1,
    kMotionEnabled = 2,
    kFaulted = 3,
  };

  // Initializes force torque hardware module interface and registers shared
  // memory interfaces.
  absl::Status Init(
      intrinsic::icon::HardwareModuleInitContext& init_context) override;

  State GetCurrentState() { return current_state_; }

  RealtimeStatus Activate() override {
    current_state_ = State::kActivated;
    return OkStatus();
  }

  RealtimeStatus Deactivate() override {
    current_state_ = State::kDeactivated;
    return OkStatus();
  }

  absl::Status EnableMotion() override {
    if (fail_transitions_) {
      return InternalError("Failed to enable");
    }
    current_state_ = State::kMotionEnabled;
    return absl::OkStatus();
  }

  absl::Status DisableMotion() override {
    if (fail_transitions_) {
      return InternalError("Failed to disable");
    }
    current_state_ = State::kActivated;
    return absl::OkStatus();
  }

  absl::Status ClearFaults() override {
    if (fail_transitions_) {
      return InternalError("Failed to clear faults");
    }
    if (current_state_ == State::kFaulted) {
      current_state_ = State::kActivated;
    }
    return absl::OkStatus();
  }

  absl::Status TriggerFault() {
    current_state_ = State::kFaulted;
    return absl::OkStatus();
  }

  absl::Status Shutdown() override { return absl::OkStatus(); }
  RealtimeStatus ApplyCommand() override;
  RealtimeStatus ReadStatus() override;

  void SetForceTorque(const Wrench& wrench);
  void SetEnabled(const bool enabled);
  void SetFailingTransitions(bool fail = true) { fail_transitions_ = fail; }
  bool IsTaring() { return taring_; }
  void SetError(intrinsic_fbs::ForceSensorStatusCode status_code);

  MutableHardwareInterfaceHandle<ForceTorqueStatusHardwareInterface>
      force_torque_status_handle_;
  MutableHardwareInterfaceHandle<ForceTorqueCommandHardwareInterface>
      force_torque_command_handle_;

 private:
  bool taring_ = false;
  int tare_cycle_counter_ = 0;
  int tare_for_ = 1;
  bool fail_transitions_ = false;
  State current_state_;
};

// Creates default part config for HalForceTorqueSensorPart tests.
intrinsic_proto::icon::HalForceTorqueSensorPartConfig
CreateDefaultHalFTSensorPartConfig();

// Test fixture for HalForceTorqueSensorPart unit tests.
class HalForceTorqueSensorPartTest : public ::testing::Test {
 public:
  // Initializes mock modules, world services, and hardware module runtime for
  // test world.
  void Init(World world,
            absl::string_view arm_hwm_config = k3DofRobotNoClockConfig);
  // Constructs control Context reference for testing part creation.
  Context GetContext();

  ForceTorqueModule* ft_module_ptr_;
  ArmModule* arm_module_ptr_;
  std::unique_ptr<HardwareModuleRuntime> ft_module_runtime_;
  std::unique_ptr<HardwareModuleRuntime> arm_module_runtime_;
  ServiceCollection service_collection_;
  std::unique_ptr<PartTestHelper> helper_with_part_ = nullptr;
  HardwareModuleManager hwm_manager_;
  std::unique_ptr<NewManipulatorKinematicsImpl> kinematics_;

 private:
  std::unique_ptr<FakeWorldService> world_service_;
};

}  // namespace intrinsic::icon

#endif  // INTRINSIC_ICON_CONTROL_PARTS_HAL_FORCE_TORQUE_SENSOR_PART_HAL_FORCE_TORQUE_SENSOR_PART_TEST_FIXTURE_H_
