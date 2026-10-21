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

#include "intrinsic/icon/hardware_modules/fake/fake_module.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <string>
#include <utility>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/notification.h"
#include "absl/time/time.h"
#include "grpcpp/server_builder.h"
#include "internal/testing.h"
#include "intrinsic/icon/control/realtime_clock_interface.h"
#include "intrinsic/icon/control/safety/extern/safety_status.fbs.h"
#include "intrinsic/icon/control/safety/safety_messages.fbs.h"
#include "intrinsic/icon/hal/default_hardware_interfaces.h"  // IWYU pragma: keep
#include "intrinsic/icon/hal/hardware_module_proxy.h"
#include "intrinsic/icon/hal/hardware_module_runtime.h"
#include "intrinsic/icon/hal/icon_state_register.h"
#include "intrinsic/icon/hal/interfaces/adio.fbs.h"
#include "intrinsic/icon/hal/interfaces/control_mode.fbs.h"
#include "intrinsic/icon/hal/interfaces/force_torque.fbs.h"
#include "intrinsic/icon/hal/interfaces/icon_state.fbs.h"
#include "intrinsic/icon/hal/interfaces/io_controller.fbs.h"
#include "intrinsic/icon/hal/interfaces/joint_command.fbs.h"
#include "intrinsic/icon/hal/interfaces/joint_state.fbs.h"
#include "intrinsic/icon/hal/interfaces/rangefinder.fbs.h"
#include "intrinsic/icon/hal/module_config.h"
#include "intrinsic/icon/hal/proto/hardware_module_config.pb.h"
#include "intrinsic/icon/hal/proto/hardware_module_inspection.pb.h"
#include "intrinsic/icon/hal/realtime_clock.h"
#include "intrinsic/icon/hardware_modules/fake/fake_module_config.pb.h"
#include "intrinsic/icon/interprocess/shared_memory_manager/shared_memory_manager.h"
#include "intrinsic/icon/interprocess/shared_memory_manager/testing/unique_segment_name.h"
#include "intrinsic/icon/testing/malloc_test.h"
#include "intrinsic/icon/utils/clock.h"
#include "intrinsic/icon/utils/current_cycle.h"
#include "intrinsic/icon/utils/inspection_publisher.h"
#include "intrinsic/icon/utils/realtime_status_matchers.h"
#include "intrinsic/platform/pubsub/pubsub.h"
#include "intrinsic/util/proto/any.h"
#include "intrinsic/util/proto/parse_text_proto.h"

namespace intrinsic::icon {
namespace {

using ::absl_testing::StatusIs;
using ::intrinsic::ParseTextProtoOrDie;
using ::intrinsic::timeslicer::fake_module::FakeModule;
using ::intrinsic_fbs::ButtonStatus;
using ::intrinsic_fbs::ControlMode;
using ::intrinsic_fbs::ModeOfSafeOperation;
using ::intrinsic_fbs::RequestedBehavior;
using ::testing::ElementsAre;
using ::testing::HasSubstr;
using ::testing::IsSupersetOf;

constexpr absl::string_view k6DofRobotNoClockConfig = R"pb(
  name: "fake_module"
  drives_realtime_clock: false
  control_frequency_hz: 2500
  module_config {
    [type.googleapis.com/intrinsic_proto.icon.FakeModuleConfig] {
      arm_interfaces {
        control_mode_state_name: "control_mode_state"
        hand_guiding_command_name: "hand_guiding_command"
        joint_position_command_name: "joint_position_command"
        joint_commanded_position_name: "joint_commanded_position"
        joint_velocity_command_name: "joint_velocity_command"
        joint_torque_command_name: "joint_torque_command"
        process_wrench_command_name: "process_wrench_command"
        joint_position_state_name: "joint_position_state"
        joint_velocity_state_name: "joint_velocity_state"
        joint_acceleration_state_name: "joint_acceleration_state"
        joint_torque_state_name: "joint_torque_state"
        joint_system_limits_name: "joint_system_limits"
        initial_joint_state {
          position_sensed: 1.0
          velocity_sensed: 0.0
          acceleration_sensed: 0.0
          position_commanded_last_cycle: 2.0
          velocity_commanded_last_cycle: 2.1
          acceleration_commanded_last_cycle: 2.3
        }
        initial_joint_state {
          position_sensed: 1.1
          velocity_sensed: -0.1
          acceleration_sensed: 0.1
        }
        initial_joint_state {
          position_sensed: 1.2
          velocity_sensed: -0.2
          acceleration_sensed: 0.2
        }
        initial_joint_state {
          position_sensed: 1.3
          velocity_sensed: -0.3
          acceleration_sensed: 0.3
        }
        initial_joint_state {
          position_sensed: 1.4
          velocity_sensed: -0.4
          acceleration_sensed: 0.4
        }
        initial_joint_state {}
      }

      adio_interface: {
        initial_adio_state: {
          analog_inputs: {
            key: "ai"
            value: {
              signals: {
                key: 1
                value: { unit: "Kilogram" value: 1.337 }
              }
            }
          }
          analog_outputs: {
            key: "ao"
            value: {
              signals: {
                key: 1
                value: { unit: "Candela" value: 2.558 }
              }
            }
          }
          digital_inputs: {
            key: "di"
            value: {
              signals: {
                key: 0
                value: { value: false }
              }
              signals: {
                key: 1
                value: { value: true }
              }
            }
          }
          digital_inputs: {
            key: "di2"
            value: {
              signals: {
                key: 0
                value: { value: false }
              }
            }
          }
          digital_outputs: {
            key: "do"
            value: {
              signals: {
                key: 0
                value: { value: true }
              }
              signals: {
                key: 1
                value: { value: true }
              }
              signals: {
                key: 5
                value: { value: false }
              }
            }
          }
        }
        setup_loopback_interfaces: true
      }

      force_torque_interface {
        force_torque_status_name: "force_torque_status"
        force_torque_command_name: "force_torque_command"
        initial_force_torque_state {
          x: 1.0
          y: 2.0
          z: 3.0
          rx: 4.0
          ry: 5.0
          rz: 6.0
        }
      }

      rangefinder_interface {
        rangefinder_status_name: "rangefinder_status"
        initial_rangefinder_distance: 12.3
      }

      safety_interface {
        safety_status_name: "safety_status"
        initial_safety_status {
          mode_of_safe_operation: MODE_OF_SAFE_OPERATION_CONFIGURATION
          estop_button_status: BUTTON_STATUS_ENGAGED
          enable_button_status: BUTTON_STATUS_NOT_AVAILABLE
          requested_behavior: REQUESTED_BEHAVIOR_NORMAL_OPERATION
        }
      }
    }
  }
)pb";

constexpr absl::string_view kInvalidArmConfig = R"pb(
  name: "fake_module"
  drives_realtime_clock: false
  control_frequency_hz: 2500
  module_config {
    [type.googleapis.com/intrinsic_proto.icon.FakeModuleConfig] {
      arm_interfaces {
        joint_position_command_name: "joint_position_command"
        process_wrench_command_name: "process_wrench_command"
        joint_position_state_name: "joint_position_state"
      }
    }
  }
)pb";

constexpr absl::string_view k6AdioNoClockConfig = R"pb(
  name: "fake_module"
  drives_realtime_clock: false
  control_frequency_hz: 2500
  module_config {
    [type.googleapis.com/intrinsic_proto.icon.FakeModuleConfig] {
      adio_interface: {
        initial_adio_state: {
          analog_inputs: {
            key: "ai"
            value: {
              signals: {
                key: 1
                value: { unit: "Kilogram" value: 1.337 }
              }
            }
          }
          analog_outputs: {
            key: "ao"
            value: {
              signals: {
                key: 2
                value: { unit: "Candela" value: 2.558 }
              }
            }
          }
          digital_inputs: {
            key: "di"
            value: {
              signals: {
                key: 0
                value: { value: false }
              }
              signals: {
                key: 1
                value: { value: true }
              }
            }
          }
          digital_inputs: {
            key: "di2"
            value: {
              signals: {
                key: 0
                value: { value: false }
              }
            }
          }
          digital_outputs: {
            key: "do"
            value: {
              signals: {
                key: 0
                value: { value: true }
              }
              signals: {
                key: 1
                value: { value: true }
              }
              signals: {
                key: 5
                value: { value: false }
              }
            }
          }
        }
        setup_loopback_interfaces: true
      }
    }
  }
)pb";

TEST(FakeModule, ArmRequiresInitialJointState) {
  std::string memory_namespace = UniqueMemoryNamespace();
  intrinsic_proto::icon::HardwareModuleConfig config =
      ParseTextProtoOrDie(kInvalidArmConfig);
  ModuleConfig module_config(config, memory_namespace,
                             /*realtime_clock=*/nullptr);
  auto module = std::make_unique<FakeModule>();
  ASSERT_OK_AND_ASSIGN(auto shm_manager, SharedMemoryManager::Create(
                                             memory_namespace, config.name()));
  ASSERT_OK_AND_ASSIGN(auto module_runtime, icon::HardwareModuleRuntime::Create(
                                                std::move(shm_manager),
                                                {
                                                    /*realtime_clock=*/nullptr,
                                                    std::move(module),
                                                    module_config,
                                                }));
  grpc::ServerBuilder server_builder;
  EXPECT_THAT(module_runtime->Run(server_builder),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       HasSubstr("initial_joint_state")));
}

TEST(FakeModule, InitializesWithoutClock) {
  std::string memory_namespace = UniqueMemoryNamespace();
  intrinsic_proto::icon::HardwareModuleConfig config =
      ParseTextProtoOrDie(k6DofRobotNoClockConfig);
  ModuleConfig module_config(config, memory_namespace,
                             /*realtime_clock=*/nullptr);
  auto module = std::make_unique<FakeModule>();
  ASSERT_OK_AND_ASSIGN(auto shm_manager, SharedMemoryManager::Create(
                                             memory_namespace, config.name()));
  ASSERT_OK_AND_ASSIGN(auto module_runtime, icon::HardwareModuleRuntime::Create(
                                                std::move(shm_manager),
                                                {
                                                    /*realtime_clock=*/nullptr,
                                                    std::move(module),
                                                    module_config,
                                                }));
  grpc::ServerBuilder server_builder;
  EXPECT_OK(module_runtime->Run(server_builder));
}

TEST(FakeModule, InitializesWithClock) {
  std::string memory_namespace = UniqueMemoryNamespace();
  intrinsic_proto::icon::HardwareModuleConfig config =
      ParseTextProtoOrDie(k6DofRobotNoClockConfig);
  config.set_drives_realtime_clock(true);
  ASSERT_OK_AND_ASSIGN(auto shm_manager, SharedMemoryManager::Create(
                                             memory_namespace, config.name()));
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<icon::RealtimeClockInterface> clock,
                       icon::RealtimeClock::Create(*shm_manager));

  ModuleConfig module_config(config, memory_namespace,
                             /*realtime_clock=*/clock.get());

  auto module = std::make_unique<FakeModule>();
  ASSERT_OK_AND_ASSIGN(auto module_runtime, icon::HardwareModuleRuntime::Create(
                                                std::move(shm_manager),
                                                {
                                                    /*realtime_clock=*/nullptr,
                                                    std::move(module),
                                                    module_config,
                                                }));
  grpc::ServerBuilder server_builder;
  EXPECT_OK(module_runtime->Run(server_builder));
}

TEST(FakeModule, RegistersHandGuidingInterfacesWhenNameNotSet) {
  std::string memory_namespace = UniqueMemoryNamespace();
  intrinsic_proto::icon::HardwareModuleConfig config =
      ParseTextProtoOrDie(k6DofRobotNoClockConfig);
  ASSERT_OK_AND_ASSIGN(auto fake_config,
                       UnpackAny<intrinsic_proto::icon::FakeModuleConfig>(
                           config.module_config()));
  // "control_mode_state" is the fallback name for the interfaces.
  fake_config.mutable_arm_interfaces()->clear_hand_guiding_command_name();
  ASSERT_TRUE(config.mutable_module_config()->PackFrom(fake_config));

  ModuleConfig module_config(config, memory_namespace,
                             /*realtime_clock=*/nullptr);

  auto module = std::make_unique<FakeModule>();
  ASSERT_OK_AND_ASSIGN(auto shm_manager, SharedMemoryManager::Create(
                                             memory_namespace, config.name()));
  ASSERT_OK_AND_ASSIGN(auto module_runtime, icon::HardwareModuleRuntime::Create(
                                                std::move(shm_manager),
                                                {
                                                    /*realtime_clock=*/nullptr,
                                                    std::move(module),
                                                    module_config,
                                                }));
  grpc::ServerBuilder server_builder;
  EXPECT_OK(module_runtime->Run(server_builder));
  ASSERT_OK_AND_ASSIGN(auto proxy, intrinsic::icon::HardwareModuleProxy::Attach(
                                       module_config, absl::ZeroDuration()));

  EXPECT_THAT(proxy.GetHardwareInterfaceNames(),
              IsSupersetOf({"safety_status", "joint_torque_state",
                            "joint_velocity_command", "joint_position_state",
                            "joint_position_command", "joint_velocity_state",
                            "hardware_module_state", "joint_torque_command",
                            "joint_acceleration_state", "control_mode_state",
                            "hand_guiding_command", "force_torque_status",
                            "force_torque_command", "rangefinder_status"}));
}

TEST(FakeModule, RegistersControlModeInterfacesWhenNameNotSet) {
  std::string memory_namespace = UniqueMemoryNamespace();
  intrinsic_proto::icon::HardwareModuleConfig config =
      ParseTextProtoOrDie(k6DofRobotNoClockConfig);
  ASSERT_OK_AND_ASSIGN(auto fake_config,
                       UnpackAny<intrinsic_proto::icon::FakeModuleConfig>(
                           config.module_config()));
  // "control_mode_state" is the fallback name for the interfaces.
  fake_config.mutable_arm_interfaces()->clear_control_mode_state_name();
  ASSERT_TRUE(config.mutable_module_config()->PackFrom(fake_config));

  ModuleConfig module_config(config, memory_namespace,
                             /*realtime_clock=*/nullptr);

  auto module = std::make_unique<FakeModule>();
  ASSERT_OK_AND_ASSIGN(auto shm_manager, SharedMemoryManager::Create(
                                             memory_namespace, config.name()));
  ASSERT_OK_AND_ASSIGN(auto module_runtime, icon::HardwareModuleRuntime::Create(
                                                std::move(shm_manager),
                                                {
                                                    /*realtime_clock=*/nullptr,
                                                    std::move(module),
                                                    module_config,
                                                }));
  grpc::ServerBuilder server_builder;
  EXPECT_OK(module_runtime->Run(server_builder));
  ASSERT_OK_AND_ASSIGN(auto proxy, intrinsic::icon::HardwareModuleProxy::Attach(
                                       module_config, absl::ZeroDuration()));

  EXPECT_THAT(proxy.GetHardwareInterfaceNames(),
              IsSupersetOf({"safety_status", "joint_torque_state",
                            "joint_velocity_command", "joint_position_state",
                            "joint_position_command", "joint_velocity_state",
                            "hardware_module_state", "joint_torque_command",
                            "joint_acceleration_state", "control_mode_state",
                            "hand_guiding_command", "force_torque_status",
                            "force_torque_command", "rangefinder_status",
                            "joint_system_limits"}));
}

TEST(FakeModule, InitializesInterfaces) {
  std::string memory_namespace = UniqueMemoryNamespace();
  intrinsic_proto::icon::HardwareModuleConfig config =
      ParseTextProtoOrDie(k6DofRobotNoClockConfig);
  ModuleConfig module_config(config, memory_namespace,
                             /*realtime_clock=*/nullptr);
  auto module = std::make_unique<FakeModule>();
  ASSERT_OK_AND_ASSIGN(auto shm_manager, SharedMemoryManager::Create(
                                             memory_namespace, config.name()));
  ASSERT_OK_AND_ASSIGN(auto module_runtime, icon::HardwareModuleRuntime::Create(
                                                std::move(shm_manager),
                                                {
                                                    /*realtime_clock=*/nullptr,
                                                    std::move(module),
                                                    module_config,
                                                }));
  grpc::ServerBuilder server_builder;
  EXPECT_OK(module_runtime->Run(server_builder));
  ASSERT_OK_AND_ASSIGN(auto proxy, intrinsic::icon::HardwareModuleProxy::Attach(
                                       module_config, absl::ZeroDuration()));

  EXPECT_THAT(proxy.GetHardwareInterfaceNames(),
              IsSupersetOf({"safety_status", "joint_torque_state",
                            "joint_velocity_command", "process_wrench_command",
                            "joint_position_state", "joint_position_command",
                            "joint_velocity_state", "hardware_module_state",
                            "joint_torque_command", "joint_acceleration_state",
                            "control_mode_state", "hand_guiding_command",
                            "force_torque_status", "force_torque_command",
                            "rangefinder_status"}));

  ASSERT_OK_AND_ASSIGN(
      auto jpos_command,
      proxy.GetMutableHardwareInterface<intrinsic_fbs::JointPositionCommand>(
          "joint_position_command"));
  EXPECT_EQ(jpos_command->position()->Get(0), 2.0);
  EXPECT_EQ(jpos_command->velocity_feedforward()->Get(0), 2.1);
  EXPECT_EQ(jpos_command->acceleration_feedforward()->Get(0), 2.3);
  ASSERT_OK_AND_ASSIGN(
      auto jvel_command,
      proxy.GetMutableHardwareInterface<intrinsic_fbs::JointVelocityCommand>(
          "joint_velocity_command"));
  EXPECT_EQ(jvel_command->velocity()->Get(0), 2.1);
  EXPECT_EQ(jvel_command->acceleration_feedforward()->Get(0), 2.3);
  ASSERT_OK_AND_ASSIGN(
      auto jtorque_command,
      proxy.GetMutableHardwareInterface<intrinsic_fbs::JointTorqueCommand>(
          "joint_torque_command"));
  // Initialisation of JointTorqueCommand is not supported.
  EXPECT_EQ(jtorque_command->torque()->Get(0), 0);

  ASSERT_OK_AND_ASSIGN(
      auto jpos_state,
      proxy.GetHardwareInterface<intrinsic_fbs::JointPositionState>(
          "joint_position_state"));
  EXPECT_THAT(*jpos_state->position(), ElementsAre(1.0, 1.1, 1.2, 1.3, 1.4, 0));
  ASSERT_OK_AND_ASSIGN(
      auto jvel_state,
      proxy.GetHardwareInterface<intrinsic_fbs::JointVelocityState>(
          "joint_velocity_state"));
  EXPECT_THAT(*jvel_state->velocity(),
              ElementsAre(0.0, -0.1, -0.2, -0.3, -0.4, 0));
  ASSERT_OK_AND_ASSIGN(
      auto jaccel_state,
      proxy.GetHardwareInterface<intrinsic_fbs::JointAccelerationState>(
          "joint_acceleration_state"));
  EXPECT_THAT(*jaccel_state->acceleration(),
              ElementsAre(0, 0.1, 0.2, 0.3, 0.4, 0));
  ASSERT_OK_AND_ASSIGN(
      auto jtorque_state,
      proxy.GetHardwareInterface<intrinsic_fbs::JointTorqueState>(
          "joint_torque_state"));
  EXPECT_THAT(*jtorque_state->torque(), ElementsAre(0, 0, 0, 0, 0, 0));

  ASSERT_OK_AND_ASSIGN(
      auto safety_status,
      proxy.GetHardwareInterface<intrinsic_fbs::SafetyStatusMessage>(
          "safety_status"));
  EXPECT_EQ(safety_status->mode_of_safe_operation(),
            ModeOfSafeOperation::CONFIGURATION);
  EXPECT_EQ(safety_status->estop_button_status(), ButtonStatus::ENGAGED);
  EXPECT_EQ(safety_status->enable_button_status(), ButtonStatus::NOT_AVAILABLE);
  EXPECT_EQ(safety_status->requested_behavior(),
            RequestedBehavior::NORMAL_OPERATION);

  ASSERT_OK_AND_ASSIGN(
      auto motor_status,
      proxy.GetMutableHardwareInterface<intrinsic_fbs::ControlModeStatus>(
          "control_mode_state"));
  EXPECT_EQ(motor_status->status(), ControlMode::kCyclicPosition);

  ASSERT_OK_AND_ASSIGN(
      auto force_torque_status,
      proxy.GetHardwareInterface<intrinsic_fbs::ForceTorqueStatus>(
          "force_torque_status"));
  ASSERT_OK_AND_ASSIGN(
      auto force_torque_command,
      proxy.GetHardwareInterface<intrinsic_fbs::ForceTorqueCommand>(
          "force_torque_command"));
  EXPECT_EQ(force_torque_status->wrench()->x(), 1.0);
  EXPECT_EQ(force_torque_status->wrench()->y(), 2.0);
  EXPECT_EQ(force_torque_status->wrench()->z(), 3.0);
  EXPECT_EQ(force_torque_status->wrench()->rx(), 4.0);
  EXPECT_EQ(force_torque_status->wrench()->ry(), 5.0);
  EXPECT_EQ(force_torque_status->wrench()->rz(), 6.0);

  ASSERT_OK_AND_ASSIGN(
      auto rangefinder_status,
      proxy.GetHardwareInterface<intrinsic_fbs::RangeFinderStatus>(
          "rangefinder_status"));
  EXPECT_FLOAT_EQ(rangefinder_status->distance(), 12.3);
  EXPECT_EQ(rangefinder_status->error(),
            intrinsic_fbs::RangeFinderError::NoError);
}

TEST(FakeModule, LoopsBack) {
  std::string memory_namespace = UniqueMemoryNamespace();
  intrinsic_proto::icon::HardwareModuleConfig config =
      ParseTextProtoOrDie(k6DofRobotNoClockConfig);
  ModuleConfig module_config(config, memory_namespace,
                             /*realtime_clock=*/nullptr);
  auto module = std::make_unique<FakeModule>();
  ASSERT_OK_AND_ASSIGN(auto shm_manager, SharedMemoryManager::Create(
                                             memory_namespace, config.name()));
  ASSERT_OK_AND_ASSIGN(auto module_runtime, icon::HardwareModuleRuntime::Create(
                                                std::move(shm_manager),
                                                {
                                                    /*realtime_clock=*/nullptr,
                                                    std::move(module),
                                                    module_config,
                                                }));
  grpc::ServerBuilder server_builder;
  EXPECT_OK(module_runtime->Run(server_builder));
  ASSERT_OK_AND_ASSIGN(auto proxy, intrinsic::icon::HardwareModuleProxy::Attach(
                                       module_config, absl::ZeroDuration()));

  EXPECT_THAT(proxy.GetHardwareInterfaceNames(),
              IsSupersetOf({"safety_status", "joint_torque_state",
                            "joint_velocity_command", "joint_position_state",
                            "joint_position_command",
                            "joint_commanded_position", "joint_velocity_state",
                            "hardware_module_state", "joint_torque_command",
                            "joint_acceleration_state", "control_mode_state",
                            "hand_guiding_command", "force_torque_status",
                            "force_torque_command", "rangefinder_status"}));

  ASSERT_OK_AND_ASSIGN(
      auto icon_state,
      proxy.GetMutableHardwareInterface<intrinsic_fbs::IconState>(
          kIconStateInterfaceName));

  // Sets the operational mode
  ASSERT_OK_AND_ASSIGN(
      auto control_mode_state,
      proxy.GetMutableHardwareInterface<intrinsic_fbs::ControlModeStatus>(
          "control_mode_state"));

  ASSERT_OK_AND_ASSIGN(
      auto jpos_command,
      proxy.GetMutableHardwareInterface<intrinsic_fbs::JointPositionCommand>(
          "joint_position_command"));
  ASSERT_OK_AND_ASSIGN(
      auto jvel_command,
      proxy.GetMutableHardwareInterface<intrinsic_fbs::JointVelocityCommand>(
          "joint_velocity_command"));
  ASSERT_OK_AND_ASSIGN(
      auto jtorque_command,
      proxy.GetMutableHardwareInterface<intrinsic_fbs::JointTorqueCommand>(
          "joint_torque_command"));
  ASSERT_OK_AND_ASSIGN(
      auto hand_guiding_command,
      proxy.GetMutableHardwareInterface<intrinsic_fbs::HandGuidingCommand>(
          "hand_guiding_command"));

  ASSERT_OK_AND_ASSIGN(
      auto jpos_state,
      proxy.GetHardwareInterface<intrinsic_fbs::JointPositionState>(
          "joint_position_state"));
  ASSERT_OK_AND_ASSIGN(
      auto jvel_state,
      proxy.GetHardwareInterface<intrinsic_fbs::JointVelocityState>(
          "joint_velocity_state"));
  ASSERT_OK_AND_ASSIGN(
      auto jaccel_state,
      proxy.GetHardwareInterface<intrinsic_fbs::JointAccelerationState>(
          "joint_acceleration_state"));
  ASSERT_OK_AND_ASSIGN(
      auto jtorque_state,
      proxy.GetHardwareInterface<intrinsic_fbs::JointTorqueState>(
          "joint_torque_state"));

  jpos_command->mutable_position()->Mutate(0, 5);
  jpos_command->mutable_velocity_feedforward()->Mutate(0, 4);
  jpos_command->mutable_acceleration_feedforward()->Mutate(0, 3);
  {
    // Simulates an ICON tick.
    icon::Cycle::SetCurrentCycle(42);
    icon_state->mutate_current_cycle(42);
    icon_state.UpdatedAt(intrinsic::Clock::now());
    jpos_command.UpdatedAt(intrinsic::Clock::Now());
  }
  // Ticks the module
  INTRINSIC_RT_EXPECT_OK(
      module_runtime->GetHardwareModule().instance->ReadStatus());
  INTRINSIC_RT_EXPECT_OK(
      module_runtime->GetHardwareModule().instance->ApplyCommand());
  EXPECT_EQ(jpos_state->position()->Get(0), 5);
  EXPECT_EQ(jvel_state->velocity()->Get(0), 4);
  EXPECT_EQ(jaccel_state->acceleration()->Get(0), 3);
  EXPECT_EQ(control_mode_state->status(), ControlMode::kCyclicPosition);

  // Doesn't update when not in MotorControlMode::CYCLIC_VELOCITY.
  jvel_command->mutable_velocity()->Mutate(0, 15);
  jvel_command->mutable_acceleration_feedforward()->Mutate(0, 4);
  // Ticks the module
  INTRINSIC_RT_EXPECT_OK(
      module_runtime->GetHardwareModule().instance->ReadStatus());
  INTRINSIC_RT_EXPECT_OK(
      module_runtime->GetHardwareModule().instance->ApplyCommand());
  EXPECT_NE(jvel_state->velocity()->Get(0), 15);
  EXPECT_NE(jaccel_state->acceleration()->Get(0), 4);
  jvel_command.UpdatedAt(intrinsic::Clock::Now());
  // Ticks the module
  INTRINSIC_RT_EXPECT_OK(
      module_runtime->GetHardwareModule().instance->ReadStatus());
  INTRINSIC_RT_EXPECT_OK(
      module_runtime->GetHardwareModule().instance->ApplyCommand());
  EXPECT_EQ(jvel_state->velocity()->Get(0), 15);
  EXPECT_EQ(jaccel_state->acceleration()->Get(0), 4);
  EXPECT_EQ(control_mode_state->status(), ControlMode::kCyclicVelocity);

  jtorque_command->mutable_torque()->Mutate(0, 1);
  jtorque_command.UpdatedAt(intrinsic::Clock::Now());
  // Ticks the module
  INTRINSIC_RT_EXPECT_OK(
      module_runtime->GetHardwareModule().instance->ReadStatus());
  INTRINSIC_RT_EXPECT_OK(
      module_runtime->GetHardwareModule().instance->ApplyCommand());
  EXPECT_EQ(jtorque_state->torque()->Get(0), 1);
  EXPECT_EQ(control_mode_state->status(), ControlMode::kCyclicTorque);

  hand_guiding_command.UpdatedAt(intrinsic::Clock::Now());
  // Ticks the module
  INTRINSIC_RT_EXPECT_OK(
      module_runtime->GetHardwareModule().instance->ReadStatus());
  INTRINSIC_RT_EXPECT_OK(
      module_runtime->GetHardwareModule().instance->ApplyCommand());
  EXPECT_EQ(control_mode_state->status(), ControlMode::kHandguiding);
}

TEST(FakeModule, ADIOLoopsBack) {
  std::string memory_namespace = UniqueMemoryNamespace();
  intrinsic_proto::icon::HardwareModuleConfig config =
      ParseTextProtoOrDie(k6AdioNoClockConfig);
  ModuleConfig module_config(config, memory_namespace,
                             /*realtime_clock=*/nullptr);
  auto module = std::make_unique<FakeModule>();
  ASSERT_OK_AND_ASSIGN(auto shm_manager, SharedMemoryManager::Create(
                                             memory_namespace, config.name()));
  ASSERT_OK_AND_ASSIGN(auto module_runtime, icon::HardwareModuleRuntime::Create(
                                                std::move(shm_manager),
                                                {
                                                    /*realtime_clock=*/nullptr,
                                                    std::move(module),
                                                    module_config,
                                                }));
  grpc::ServerBuilder server_builder;
  EXPECT_OK(module_runtime->Run(server_builder));
  ASSERT_OK_AND_ASSIGN(auto proxy, intrinsic::icon::HardwareModuleProxy::Attach(
                                       module_config, absl::ZeroDuration()));

  EXPECT_THAT(
      proxy.GetHardwareInterfaceNames(),
      IsSupersetOf({"di", "di2", "do", "ai", "ao", "di_loopback_command",
                    "do_loopback_status", "ai_loopback_command"}));
  ASSERT_OK_AND_ASSIGN(
      auto di_status,
      proxy.GetHardwareInterface<intrinsic_fbs::DIOStatus>("di"));
  ASSERT_OK_AND_ASSIGN(
      auto di2_status,
      proxy.GetHardwareInterface<intrinsic_fbs::DIOStatus>("di2"));
  ASSERT_OK_AND_ASSIGN(
      auto di_loopback_command,
      proxy.GetMutableHardwareInterface<intrinsic_fbs::DIOCommand>(
          "di_loopback_command"));

  ASSERT_OK_AND_ASSIGN(
      auto ai_status,
      proxy.GetHardwareInterface<intrinsic_fbs::AIOStatus>("ai"));
  ASSERT_OK_AND_ASSIGN(
      auto ai_loopback_command,
      proxy.GetMutableHardwareInterface<intrinsic_fbs::AIOStatus>(
          "ai_loopback_command"));

  ASSERT_OK_AND_ASSIGN(
      auto do_command,
      proxy.GetMutableHardwareInterface<intrinsic_fbs::DIOCommand>("do"));
  ASSERT_OK_AND_ASSIGN(auto do_loopback_status,
                       proxy.GetHardwareInterface<intrinsic_fbs::DIOStatus>(
                           "do_loopback_status"));

  ASSERT_OK_AND_ASSIGN(
      auto ao_command,
      proxy.GetMutableHardwareInterface<intrinsic_fbs::AIOCommand>("ao"));
  ASSERT_OK_AND_ASSIGN(auto ao_loopback_status,
                       proxy.GetHardwareInterface<intrinsic_fbs::AIOStatus>(
                           "ao_loopback_status"));

  EXPECT_EQ(di_status->signals()->size(), 2);
  EXPECT_EQ(di2_status->signals()->size(), 1);
  EXPECT_EQ(di_status->signals()->Get(0)->name()->string_view(), "0");
  EXPECT_EQ(di_status->signals()->Get(0)->value(), false);
  EXPECT_NEAR(ai_status->signals()->Get(1)->value(), 1.337, 1e-6);
  EXPECT_EQ(ai_status->signals()->Get(1)->unit(),
            intrinsic_fbs::AnalogInputUnit::kKilogram);
  // Command interfaces have default values.
  EXPECT_EQ(do_command->signals()->Get(5)->name()->string_view(), "5");
  EXPECT_EQ(do_command->signals()->Get(5)->value(), false);
  EXPECT_EQ(ao_command->signals()->Get(2)->value(), 0);
  EXPECT_EQ(ao_command->signals()->Get(2)->unit(),
            intrinsic_fbs::AnalogInputUnit::kUnknown);
  // Ticks the module
  INTRINSIC_RT_EXPECT_OK(
      module_runtime->GetHardwareModule().instance->ApplyCommand());
  INTRINSIC_RT_EXPECT_OK(
      module_runtime->GetHardwareModule().instance->ReadStatus());

  // The data should stay at the initial values after a tick
  EXPECT_EQ(di_status->signals()->Get(0)->name()->string_view(), "0");
  EXPECT_EQ(di_status->signals()->Get(0)->value(), false);
  EXPECT_EQ(ai_status->signals()->Get(1)->unit(),
            intrinsic_fbs::AnalogInputUnit::kKilogram);
  EXPECT_EQ(do_loopback_status->signals()->Get(5)->name()->string_view(), "5");
  EXPECT_EQ(do_loopback_status->signals()->Get(5)->value(), false);
  EXPECT_EQ(do_loopback_status->signals()->Get(4)->value(), false)
      << "Unspecified value is not false";
  EXPECT_EQ(do_command->signals()->Get(5)->name()->string_view(), "5");
  EXPECT_EQ(do_command->signals()->Get(5)->value(), false);
  EXPECT_EQ(ao_command->signals()->Get(2)->value(), 0);
  EXPECT_EQ(ao_command->signals()->Get(2)->unit(),
            intrinsic_fbs::AnalogInputUnit::kUnknown);
  EXPECT_EQ(ao_loopback_status->signals()->Get(2)->value(), 0);
  EXPECT_EQ(ao_loopback_status->signals()->Get(2)->unit(),
            intrinsic_fbs::AnalogInputUnit::kUnknown);

  const bool new_digital_value = true;
  di_loopback_command->mutable_signals()->GetMutableObject(0)->mutate_value(
      new_digital_value);
  do_command->mutable_signals()->GetMutableObject(5)->mutate_value(
      new_digital_value);
  const double new_analog_input_value = 42.0;
  ai_loopback_command->mutable_signals()->GetMutableObject(1)->mutate_value(
      new_analog_input_value);
  const double new_analog_output_value = 54.0;
  ao_command->mutable_signals()->GetMutableObject(2)->mutate_value(
      new_analog_output_value);

  // Ticks the module
  INTRINSIC_RT_EXPECT_OK(
      module_runtime->GetHardwareModule().instance->ReadStatus());
  INTRINSIC_RT_EXPECT_OK(
      module_runtime->GetHardwareModule().instance->ApplyCommand());
  EXPECT_EQ(di_status->signals()->Get(0)->value(), new_digital_value);
  EXPECT_EQ(ai_status->signals()->Get(1)->value(), new_analog_input_value);
  EXPECT_EQ(do_loopback_status->signals()->Get(5)->value(), new_digital_value);
  EXPECT_EQ(ao_command->signals()->Get(2)->value(), new_analog_output_value);
}

TEST(FakeModule, InterfaceWorks) {
  std::string memory_namespace = UniqueMemoryNamespace();
  intrinsic_proto::icon::HardwareModuleConfig config =
      ParseTextProtoOrDie(k6DofRobotNoClockConfig);
  ModuleConfig module_config(config, memory_namespace,
                             /*realtime_clock=*/nullptr);
  auto module = std::make_unique<FakeModule>();
  ASSERT_OK_AND_ASSIGN(auto shm_manager, SharedMemoryManager::Create(
                                             memory_namespace, config.name()));
  ASSERT_OK_AND_ASSIGN(auto module_runtime, icon::HardwareModuleRuntime::Create(
                                                std::move(shm_manager),
                                                {
                                                    /*realtime_clock=*/nullptr,
                                                    std::move(module),
                                                    module_config,
                                                }));
  grpc::ServerBuilder server_builder;
  EXPECT_OK(module_runtime->Run(server_builder));
  ASSERT_OK_AND_ASSIGN(auto proxy, intrinsic::icon::HardwareModuleProxy::Attach(
                                       module_config, absl::ZeroDuration()));
  ASSERT_OK_AND_ASSIGN(
      auto icon_state,
      proxy.GetMutableHardwareInterface<intrinsic_fbs::IconState>(
          kIconStateInterfaceName));
  ASSERT_OK_AND_ASSIGN(
      auto jpos_command,
      proxy.GetMutableHardwareInterface<intrinsic_fbs::JointPositionCommand>(
          "joint_position_command"));
  {
    // Simulates an ICON tick.
    icon::Cycle::SetCurrentCycle(42);
    icon_state->mutate_current_cycle(42);
    icon_state.UpdatedAt(intrinsic::Clock::now());
    jpos_command.UpdatedAt(intrinsic::Clock::Now());
  }

  EXPECT_OK(module_runtime->GetHardwareModule().instance->Activate());
  EXPECT_OK(module_runtime->GetHardwareModule().instance->EnableMotion());
  EXPECT_OK(module_runtime->GetHardwareModule().instance->DisableMotion());
  EXPECT_OK(module_runtime->GetHardwareModule().instance->ClearFaults());
  INTRINSIC_RT_EXPECT_OK(
      module_runtime->GetHardwareModule().instance->ReadStatus());
  INTRINSIC_RT_EXPECT_OK(
      module_runtime->GetHardwareModule().instance->ApplyCommand());
  EXPECT_OK(module_runtime->GetHardwareModule().instance->Deactivate());
  EXPECT_OK(module_runtime->GetHardwareModule().instance->Shutdown());
}

TEST(FakeModule, ChecksIconCycleOfJposCommand) {
  std::string memory_namespace = UniqueMemoryNamespace();
  intrinsic_proto::icon::HardwareModuleConfig config =
      ParseTextProtoOrDie(k6DofRobotNoClockConfig);
  ModuleConfig module_config(config, memory_namespace,
                             /*realtime_clock=*/nullptr);
  auto module = std::make_unique<FakeModule>();
  ASSERT_OK_AND_ASSIGN(auto shm_manager, SharedMemoryManager::Create(
                                             memory_namespace, config.name()));
  ASSERT_OK_AND_ASSIGN(auto module_runtime, icon::HardwareModuleRuntime::Create(
                                                std::move(shm_manager),
                                                {
                                                    /*realtime_clock=*/nullptr,
                                                    std::move(module),
                                                    module_config,
                                                }));
  grpc::ServerBuilder server_builder;
  EXPECT_OK(module_runtime->Run(server_builder));
  ASSERT_OK_AND_ASSIGN(auto proxy, intrinsic::icon::HardwareModuleProxy::Attach(
                                       module_config, absl::ZeroDuration()));
  ASSERT_OK_AND_ASSIGN(
      auto icon_state,
      proxy.GetMutableHardwareInterface<intrinsic_fbs::IconState>(
          kIconStateInterfaceName));
  ASSERT_OK_AND_ASSIGN(
      auto jpos_command,
      proxy.GetMutableHardwareInterface<intrinsic_fbs::JointPositionCommand>(
          "joint_position_command"));

  INTRINSIC_RT_EXPECT_OK(
      module_runtime->GetHardwareModule().instance->ReadStatus());

  EXPECT_THAT(module_runtime->GetHardwareModule().instance->ApplyCommand(),
              RealtimeStatusIs(absl::StatusCode::kFailedPrecondition));

  // Simulates an ICON tick.
  icon::Cycle::SetCurrentCycle(42);
  icon_state->mutate_current_cycle(42);
  icon_state.UpdatedAt(intrinsic::Clock::now());
  jpos_command.UpdatedAt(intrinsic::Clock::Now());

  INTRINSIC_RT_EXPECT_OK(
      module_runtime->GetHardwareModule().instance->ApplyCommand());
}

TEST(FakeModule, DoesntMalloc) {
  std::string memory_namespace = UniqueMemoryNamespace();
  intrinsic_proto::icon::HardwareModuleConfig config =
      ParseTextProtoOrDie(k6DofRobotNoClockConfig);
  ModuleConfig module_config(config, memory_namespace,
                             /*realtime_clock=*/nullptr);
  auto module = std::make_unique<FakeModule>();
  ASSERT_OK_AND_ASSIGN(auto shm_manager, SharedMemoryManager::Create(
                                             memory_namespace, config.name()));
  ASSERT_OK_AND_ASSIGN(auto module_runtime, icon::HardwareModuleRuntime::Create(
                                                std::move(shm_manager),
                                                {
                                                    /*realtime_clock=*/nullptr,
                                                    std::move(module),
                                                    module_config,
                                                }));
  grpc::ServerBuilder server_builder;
  EXPECT_OK(module_runtime->Run(server_builder));
  ASSERT_OK_AND_ASSIGN(auto proxy, intrinsic::icon::HardwareModuleProxy::Attach(
                                       module_config, absl::ZeroDuration()));
  ASSERT_OK_AND_ASSIGN(
      auto icon_state,
      proxy.GetMutableHardwareInterface<intrinsic_fbs::IconState>(
          kIconStateInterfaceName));
  ASSERT_OK_AND_ASSIGN(
      auto jpos_command,
      proxy.GetMutableHardwareInterface<intrinsic_fbs::JointPositionCommand>(
          "joint_position_command"));
  {
    // Simulates an ICON tick.
    icon::Cycle::SetCurrentCycle(42);
    icon_state->mutate_current_cycle(42);
    icon_state.UpdatedAt(intrinsic::Clock::now());
    jpos_command.UpdatedAt(intrinsic::Clock::Now());
  }

  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  INTRINSIC_RT_EXPECT_OK(
      module_runtime->GetHardwareModule().instance->ReadStatus());
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();

  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  INTRINSIC_RT_EXPECT_OK(
      module_runtime->GetHardwareModule().instance->ApplyCommand());
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
}

TEST(FakeModule, PublishesInspectionData) {
  std::string memory_namespace = UniqueMemoryNamespace();
  intrinsic_proto::icon::HardwareModuleConfig config =
      ParseTextProtoOrDie(k6DofRobotNoClockConfig);
  ModuleConfig module_config(config, memory_namespace,
                             /*realtime_clock=*/nullptr);
  auto module = std::make_unique<FakeModule>();
  ASSERT_OK_AND_ASSIGN(auto shm_manager, SharedMemoryManager::Create(
                                             memory_namespace, config.name()));
  ASSERT_OK_AND_ASSIGN(auto module_runtime, icon::HardwareModuleRuntime::Create(
                                                std::move(shm_manager),
                                                {
                                                    /*realtime_clock=*/nullptr,
                                                    std::move(module),
                                                    module_config,
                                                }));
  grpc::ServerBuilder server_builder;
  const std::string kHwmName = "fake_module";
  absl::string_view topic_name = "/service_inspection/services/fake_module";
  EXPECT_OK(module_runtime->Run(server_builder, false, {}, topic_name));

  absl::Notification inspection_data_received;
  PubSub pub_sub;
  LOG(INFO) << "Topic name: " << topic_name;
  std::function<void(absl::string_view packet, absl::Status error)> err_cb =
      [](absl::string_view packet, const absl::Status& error) { FAIL(); };
  ASSERT_OK_AND_ASSIGN(
      auto sub,
      pub_sub.CreateSubscription<
          intrinsic_proto::services::v1::ServiceInspectionData>(
          topic_name, TopicConfig(),
          [&inspection_data_received](
              const intrinsic_proto::services::v1::ServiceInspectionData&
                  message) {
            LOG(INFO) << "Received inspection data: " << message.DebugString();
            inspection_data_received.Notify();
            ASSERT_EQ(
                message.data().type_url(),
                absl::StrCat("type.googleapis.com/",
                             intrinsic_proto::icon::v1::
                                 HardwareModuleInspectionData::descriptor()
                                     ->full_name()));
            intrinsic_proto::icon::v1::HardwareModuleInspectionData
                inspection_data;
            EXPECT_TRUE(message.data().UnpackTo(&inspection_data));
            EXPECT_EQ(inspection_data.event_history().events().size(), 1);
            EXPECT_EQ(inspection_data.event_history().events(0).message(),
                      "This is a demo event");
            EXPECT_EQ(
                inspection_data.event_history().events(0).timestamp().seconds(),
                1625097600);
            EXPECT_EQ(
                inspection_data.event_history().events(0).timestamp().nanos(),
                0);
          },
          err_cb));

  EXPECT_TRUE(inspection_data_received.WaitForNotificationWithTimeout(
      absl::Seconds(10)));
}

}  // namespace
}  // namespace intrinsic::icon
