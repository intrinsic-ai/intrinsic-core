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

#include "intrinsic/icon/hal/interfaces/electrical_motor_utils.h"

#include <gtest/gtest.h>

#include "flatbuffers/buffer.h"
#include "intrinsic/icon/flatbuffers/fixed_string.h"
#include "intrinsic/icon/hal/interfaces/electrical_motor.fbs.h"

namespace intrinsic_fbs {
namespace {

using ::testing::Test;

// The following tests verify that BuildElectricalMotorCommand adds all of the
// subcommands we specify, and none that we did not.

TEST(ElectricalMotorUtilsTest, EmptyMotorCommand) {
  auto command_buffer = BuildElectricalMotorCommand({});
  auto command_ptr = flatbuffers::GetMutableRoot<ElectricalMotorCommand>(
      command_buffer.data());
  ASSERT_NE(command_ptr, nullptr);
  EXPECT_EQ(command_ptr->position_profile(), nullptr);
  EXPECT_EQ(command_ptr->velocity(), nullptr);
  EXPECT_EQ(command_ptr->velocity_profile(), nullptr);
  EXPECT_EQ(command_ptr->torque_profile(), nullptr);
  EXPECT_EQ(command_ptr->home(), nullptr);
  EXPECT_EQ(command_ptr->cyclic_position(), nullptr);
  EXPECT_EQ(command_ptr->cyclic_velocity(), nullptr);
  EXPECT_EQ(command_ptr->cyclic_torque(), nullptr);
  EXPECT_EQ(command_ptr->cyclic_joint_impedance(), nullptr);
}

TEST(ElectricalMotorUtilsTest, AllSubcommands) {
  auto command_buffer = BuildElectricalMotorCommand(
      {MotorControlMode::PROFILE_POSITION, MotorControlMode::VELOCITY,
       MotorControlMode::VELOCITY_PROFILE, MotorControlMode::TORQUE_PROFILE,
       MotorControlMode::HOME, MotorControlMode::CYCLIC_POSITION,
       MotorControlMode::CYCLIC_VELOCITY, MotorControlMode::CYCLIC_TORQUE,
       MotorControlMode::CYCLIC_JOINT_IMPEDANCE,
       MotorControlMode::CYCLIC_POSITION_TORQUE});
  auto command_ptr = flatbuffers::GetMutableRoot<ElectricalMotorCommand>(
      command_buffer.data());
  ASSERT_NE(command_ptr, nullptr);
  EXPECT_NE(command_ptr->position_profile(), nullptr);
  EXPECT_NE(command_ptr->velocity(), nullptr);
  EXPECT_NE(command_ptr->velocity_profile(), nullptr);
  EXPECT_NE(command_ptr->torque_profile(), nullptr);
  EXPECT_NE(command_ptr->home(), nullptr);
  EXPECT_NE(command_ptr->cyclic_position(), nullptr);
  EXPECT_NE(command_ptr->cyclic_velocity(), nullptr);
  EXPECT_NE(command_ptr->cyclic_torque(), nullptr);
  EXPECT_NE(command_ptr->cyclic_joint_impedance(), nullptr);
  EXPECT_NE(command_ptr->cyclic_position_torque(), nullptr);
}

TEST(ElectricalMotorUtilsTest, SomeSubcommands) {
  // Cyclic control modes only, like most of our current device implementations.
  auto command_buffer = BuildElectricalMotorCommand({
      // MotorControlMode::PROFILE_POSITION,
      // MotorControlMode::VELOCITY,
      // MotorControlMode::VELOCITY_PROFILE,
      // MotorControlMode::TORQUE_PROFILE,
      // MotorControlMode::HOME,
      MotorControlMode::CYCLIC_POSITION,
      MotorControlMode::CYCLIC_VELOCITY,
      MotorControlMode::CYCLIC_TORQUE,
      MotorControlMode::CYCLIC_JOINT_IMPEDANCE,
      // MotorControlMode::CYCLIC_POSITION_TORQUE
  });
  auto command_ptr = flatbuffers::GetMutableRoot<ElectricalMotorCommand>(
      command_buffer.data());
  ASSERT_NE(command_ptr, nullptr);
  EXPECT_EQ(command_ptr->position_profile(), nullptr);
  EXPECT_EQ(command_ptr->velocity(), nullptr);
  EXPECT_EQ(command_ptr->velocity_profile(), nullptr);
  EXPECT_EQ(command_ptr->torque_profile(), nullptr);
  EXPECT_EQ(command_ptr->home(), nullptr);
  EXPECT_NE(command_ptr->cyclic_position(), nullptr);
  EXPECT_NE(command_ptr->cyclic_velocity(), nullptr);
  EXPECT_NE(command_ptr->cyclic_torque(), nullptr);
  EXPECT_NE(command_ptr->cyclic_joint_impedance(), nullptr);
  EXPECT_EQ(command_ptr->cyclic_position_torque(), nullptr);
}

TEST(ElectricalMotorUtilsTest, NoControlModeIsCorrect) {
  // No control modes supported.
  auto command_buffer = BuildElectricalMotorCommand({});
  auto command_ptr = flatbuffers::GetMutableRoot<ElectricalMotorCommand>(
      command_buffer.data());
  ASSERT_NE(command_ptr, nullptr);

  command_ptr->mutate_control_mode(MotorControlMode::NONE);
  // Control mode == NONE, so we can't not be good.
  EXPECT_TRUE(CommandCorrectlySet(*command_ptr));
}

TEST(ElectricalMotorUtilsTest, SupportsMode) {
  auto command_buffer = BuildElectricalMotorCommand({
      // MotorControlMode::PROFILE_POSITION,
      // MotorControlMode::VELOCITY,
      // MotorControlMode::VELOCITY_PROFILE,
      //  MotorControlMode::TORQUE_PROFILE,
      //  MotorControlMode::HOME,
      MotorControlMode::CYCLIC_POSITION,
      MotorControlMode::CYCLIC_VELOCITY,
      MotorControlMode::CYCLIC_TORQUE,
      MotorControlMode::CYCLIC_JOINT_IMPEDANCE,
  });
  auto command_ptr = flatbuffers::GetMutableRoot<ElectricalMotorCommand>(
      command_buffer.data());
  ASSERT_NE(command_ptr, nullptr);

  ASSERT_FALSE(SupportsMode(*command_ptr, MotorControlMode::PROFILE_POSITION));
  ASSERT_FALSE(SupportsMode(*command_ptr, MotorControlMode::VELOCITY));
  ASSERT_FALSE(SupportsMode(*command_ptr, MotorControlMode::VELOCITY_PROFILE));
  ASSERT_FALSE(SupportsMode(*command_ptr, MotorControlMode::TORQUE_PROFILE));
  ASSERT_FALSE(SupportsMode(*command_ptr, MotorControlMode::HOME));
  ASSERT_TRUE(SupportsMode(*command_ptr, MotorControlMode::CYCLIC_POSITION));
  ASSERT_TRUE(SupportsMode(*command_ptr, MotorControlMode::CYCLIC_VELOCITY));
  ASSERT_TRUE(SupportsMode(*command_ptr, MotorControlMode::CYCLIC_TORQUE));
  ASSERT_TRUE(
      SupportsMode(*command_ptr, MotorControlMode::CYCLIC_JOINT_IMPEDANCE));
  ASSERT_FALSE(
      SupportsMode(*command_ptr, MotorControlMode::CYCLIC_POSITION_TORQUE));
}

TEST(ElectricalMotorUtilsTest, CommandCorrectlySet) {
  auto command_buffer = BuildElectricalMotorCommand(
      {MotorControlMode::PROFILE_POSITION, MotorControlMode::HOME});
  auto command_ptr = flatbuffers::GetMutableRoot<ElectricalMotorCommand>(
      command_buffer.data());
  ASSERT_NE(command_ptr, nullptr);

  command_ptr->mutate_control_mode(MotorControlMode::NONE);
  // Control mode == NONE, so we can't not be good.
  EXPECT_TRUE(CommandCorrectlySet(*command_ptr));

  command_ptr->mutate_control_mode(MotorControlMode::PROFILE_POSITION);
  // The command supports PROFILE_POSITION, so we're good too.
  EXPECT_TRUE(CommandCorrectlySet(*command_ptr));

  command_ptr->mutate_control_mode(MotorControlMode::CYCLIC_POSITION);
  // The command does not support CYCLIC_POSITION, so we expect an error.
  EXPECT_FALSE(CommandCorrectlySet(*command_ptr));
}

TEST(ElectricalMotorUtilsTest, SetSubCommandsFailsOnEmptyMessage) {
  auto command_buffer = BuildElectricalMotorCommand({});
  auto command_ptr = flatbuffers::GetMutableRoot<ElectricalMotorCommand>(
      command_buffer.data());
  ASSERT_NE(command_ptr, nullptr);

  // None of the sub-commands are present so the setter helper methods all fail.
  EXPECT_FALSE(SetMotorCommandCyclicPosition(command_ptr, 0.0, 0.0, 0.0, 0.0));
  EXPECT_FALSE(SetMotorCommandCyclicVelocity(command_ptr, 0.0, 0.0));
  EXPECT_FALSE(SetMotorCommandCyclicTorque(command_ptr, 0.0));
  EXPECT_FALSE(SetMotorCommandCyclicJointImpedance(command_ptr, 0.0, 0.0, 0.0,
                                                   0.0, 0.0));
}

TEST(ElectricalMotorUtilsTest, SetSubCommands) {
  auto command_buffer = BuildElectricalMotorCommand(
      {MotorControlMode::CYCLIC_POSITION, MotorControlMode::CYCLIC_VELOCITY,
       MotorControlMode::CYCLIC_TORQUE,
       MotorControlMode::CYCLIC_JOINT_IMPEDANCE,
       MotorControlMode::CYCLIC_POSITION_TORQUE});
  auto command_ptr = flatbuffers::GetMutableRoot<ElectricalMotorCommand>(
      command_buffer.data());
  ASSERT_NE(command_ptr, nullptr);

  ASSERT_TRUE(SetMotorCommandCyclicPosition(command_ptr, 1.0, 2.0, 3.0, 4.0));
  ASSERT_TRUE(CommandCorrectlySet(*command_ptr));
  EXPECT_EQ(command_ptr->control_mode(), MotorControlMode::CYCLIC_POSITION);
  EXPECT_EQ(command_ptr->cyclic_position()->target_position(), 1.0);
  EXPECT_EQ(command_ptr->cyclic_position()->velocity_feedforward(), 2.0);
  EXPECT_EQ(command_ptr->cyclic_position()->acceleration_feedforward(), 3.0);
  EXPECT_EQ(command_ptr->cyclic_position()->torque_feedforward(), 4.0);

  ASSERT_TRUE(SetMotorCommandCyclicVelocity(command_ptr, 1.5, 0.3));
  ASSERT_TRUE(CommandCorrectlySet(*command_ptr));
  EXPECT_EQ(command_ptr->control_mode(), MotorControlMode::CYCLIC_VELOCITY);
  EXPECT_EQ(command_ptr->cyclic_velocity()->target_velocity(), 1.5);
  EXPECT_EQ(command_ptr->cyclic_velocity()->torque_offset(), 0.3);

  ASSERT_TRUE(SetMotorCommandCyclicTorque(command_ptr, 0.2));
  ASSERT_TRUE(CommandCorrectlySet(*command_ptr));
  EXPECT_EQ(command_ptr->control_mode(), MotorControlMode::CYCLIC_TORQUE);
  EXPECT_EQ(command_ptr->cyclic_torque()->target_torque(), 0.2);

  ASSERT_TRUE(SetMotorCommandCyclicJointImpedance(command_ptr, 3.1, 4.1, 5.9,
                                                  2.6, 5.4));
  ASSERT_TRUE(CommandCorrectlySet(*command_ptr));
  EXPECT_EQ(command_ptr->control_mode(),
            MotorControlMode::CYCLIC_JOINT_IMPEDANCE);
  EXPECT_EQ(command_ptr->cyclic_joint_impedance()->target_stiffness(), 3.1);
  EXPECT_EQ(command_ptr->cyclic_joint_impedance()->target_damping(), 4.1);
  EXPECT_EQ(command_ptr->cyclic_joint_impedance()->target_position(), 5.9);
  EXPECT_EQ(command_ptr->cyclic_joint_impedance()->target_velocity(), 2.6);
  EXPECT_EQ(command_ptr->cyclic_joint_impedance()->target_torque(), 5.4);

  ASSERT_TRUE(SetMotorCommandCyclicPositionTorque(command_ptr, 1.0, 2.0, 3.0));
  ASSERT_TRUE(CommandCorrectlySet(*command_ptr));
  EXPECT_EQ(command_ptr->control_mode(),
            MotorControlMode::CYCLIC_POSITION_TORQUE);
  EXPECT_EQ(command_ptr->cyclic_position_torque()->target_position(), 1.0);
  EXPECT_EQ(command_ptr->cyclic_position_torque()->velocity_offset(), 2.0);
  EXPECT_EQ(command_ptr->cyclic_position_torque()->target_torque(), 3.0);
}

TEST(ElectricalMotorUtilsTest, SetElectricalMotorStatus) {
  auto status_buffer = BuildElectricalMotorStatus();
  auto status_ptr =
      flatbuffers::GetMutableRoot<ElectricalMotorStatus>(status_buffer.data());
  ASSERT_NE(status_ptr, nullptr);

  SetElectricalMotorStatus(
      status_ptr, ToFlag(OperationModeStatusFlags::READY_TO_SWITCH_ON) |
                      ToFlag(OperationModeStatusFlags::SWITCHED_ON) |
                      ToFlag(OperationModeStatusFlags::OPERATION_ENABLED) |
                      ToFlag(OperationModeStatusFlags::FAULT) |
                      ToFlag(OperationModeStatusFlags::VOLTAGE_ENABLED) |
                      ToFlag(OperationModeStatusFlags::QUICK_STOP) |
                      ToFlag(OperationModeStatusFlags::TARGET_REACHED));
  EXPECT_TRUE(status_ptr->ready_to_switch_on());
  EXPECT_TRUE(status_ptr->switched_on());
  EXPECT_TRUE(status_ptr->operation_enabled());
  EXPECT_TRUE(status_ptr->in_fault());
  EXPECT_TRUE(status_ptr->voltage_enabled());
  EXPECT_TRUE(status_ptr->in_quick_stop());
  EXPECT_TRUE(status_ptr->target_reached());

  // Unset all flags
  SetElectricalMotorStatus(status_ptr, 0);
  EXPECT_FALSE(status_ptr->ready_to_switch_on());
  EXPECT_FALSE(status_ptr->switched_on());
  EXPECT_FALSE(status_ptr->operation_enabled());
  EXPECT_FALSE(status_ptr->in_fault());
  EXPECT_FALSE(status_ptr->voltage_enabled());
  EXPECT_FALSE(status_ptr->in_quick_stop());
  EXPECT_FALSE(status_ptr->target_reached());

  // Set some flags
  SetElectricalMotorStatus(
      status_ptr, ToFlag(OperationModeStatusFlags::READY_TO_SWITCH_ON) |
                      ToFlag(OperationModeStatusFlags::SWITCHED_ON) |
                      ToFlag(OperationModeStatusFlags::OPERATION_ENABLED) |
                      ToFlag(OperationModeStatusFlags::VOLTAGE_ENABLED));
  EXPECT_TRUE(status_ptr->ready_to_switch_on());
  EXPECT_TRUE(status_ptr->switched_on());
  EXPECT_TRUE(status_ptr->operation_enabled());
  EXPECT_FALSE(status_ptr->in_fault());
  EXPECT_TRUE(status_ptr->voltage_enabled());
  EXPECT_FALSE(status_ptr->in_quick_stop());
  EXPECT_FALSE(status_ptr->target_reached());
}

TEST(HomeCommandTest, BuildHomeCommand) {
  auto command_buffer = BuildHomeCommand();
  auto command_ptr =
      flatbuffers::GetMutableRoot<HomeCommand>(command_buffer.data());
  ASSERT_NE(command_ptr, nullptr);
  EXPECT_EQ(command_ptr->method(), 0);
  EXPECT_DOUBLE_EQ(command_ptr->offset(), 0.0);
  EXPECT_DOUBLE_EQ(command_ptr->search_speed(), 0.0);
  EXPECT_DOUBLE_EQ(command_ptr->creep_speed(), 0.0);
  EXPECT_DOUBLE_EQ(command_ptr->acceleration(), 0.0);

  command_buffer = BuildHomeCommand(1, 2.0, 3.0, 4.0, 5.0);
  command_ptr = flatbuffers::GetMutableRoot<HomeCommand>(command_buffer.data());
  ASSERT_NE(command_ptr, nullptr);
  EXPECT_EQ(command_ptr->method(), 1);
  EXPECT_DOUBLE_EQ(command_ptr->offset(), 2.0);
  EXPECT_DOUBLE_EQ(command_ptr->search_speed(), 3.0);
  EXPECT_DOUBLE_EQ(command_ptr->creep_speed(), 4.0);
  EXPECT_DOUBLE_EQ(command_ptr->acceleration(), 5.0);
}

TEST(HomeCommandTest, BuildHomingStatus) {
  auto status_buffer = BuildHomingStatus();
  auto status_ptr =
      flatbuffers::GetMutableRoot<HomingStatus>(status_buffer.data());
  ASSERT_NE(status_ptr, nullptr);
  EXPECT_EQ(status_ptr->state(), HomingStatusFlag::HomingUnknown);
  EXPECT_EQ(intrinsic_fbs::StringView(&status_ptr->error_message()), "");

  status_buffer = BuildHomingStatus(HomingStatusFlag::HomingError, "oh no");
  status_ptr = flatbuffers::GetMutableRoot<HomingStatus>(status_buffer.data());
  ASSERT_NE(status_ptr, nullptr);
  EXPECT_EQ(status_ptr->state(), HomingStatusFlag::HomingError);
  EXPECT_EQ(intrinsic_fbs::StringView(&status_ptr->error_message()), "oh no");
}

}  // namespace
}  // namespace intrinsic_fbs
