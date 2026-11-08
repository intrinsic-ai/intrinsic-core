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

#include "intrinsic/icon/cc_client/state_variable_path.h"

#include <gtest/gtest.h>

namespace intrinsic::icon {

TEST(StateVariablePathTest, HumanReadableArmPaths) {
  EXPECT_EQ(ArmSensedPositionStateVariablePath("left_arm", 1),
            "@left_arm.ArmPart.sensed_position[1]");
  EXPECT_EQ(ArmSensedVelocityStateVariablePath("left_arm", 1),
            "@left_arm.ArmPart.sensed_velocity[1]");
  EXPECT_EQ(ArmSensedAccelerationStateVariablePath("left_arm", 1),
            "@left_arm.ArmPart.sensed_acceleration[1]");
  EXPECT_EQ(ArmSensedTorqueStateVariablePath("left_arm", 1),
            "@left_arm.ArmPart.sensed_torque[1]");
  EXPECT_EQ(
      ArmBaseTwistTipSensedStateVariablePath("left_arm", TwistDimension::Y),
      "@left_arm.ArmPart.base_twist_tip_sensed[1]");
  EXPECT_EQ(ArmBaseLinearVelocityTipSensedStateVariablePath("left_arm"),
            "@left_arm.ArmPart.base_linear_velocity_tip_sensed");
  EXPECT_EQ(ArmBaseAngularVelocityTipSensedStateVariablePath("left_arm"),
            "@left_arm.ArmPart.base_angular_velocity_tip_sensed");
  EXPECT_EQ(ArmCurrentControlModeStateVariablePath("left_arm"),
            "@left_arm.ArmPart.current_control_mode");
}

TEST(StateVariablePathTest, HumanReadableFTPaths) {
  EXPECT_EQ(FTForceMagnitudeAtTipStateVariablePath("ft"),
            "@ft.ForceTorqueSensorPart.force_magnitude_at_tip");
  EXPECT_EQ(FTTorqueMagnitudeAtTipStateVariablePath("ft"),
            "@ft.ForceTorqueSensorPart.torque_magnitude_at_tip");
  EXPECT_EQ(FTWrenchAtTipStateVariablePath("ft", WrenchDimension::Z),
            "@ft.ForceTorqueSensorPart.wrench_at_tip[2]");
  EXPECT_EQ(FTWrenchStabilityIndexStateVariablePath("ft"),
            "@ft.ForceTorqueSensorPart.wrench_stability_index");
}

TEST(StateVariablePathTest, HumanReadableGripperPaths) {
  EXPECT_EQ(GripperSensedStateStateVariablePath("gripper"),
            "@gripper.GripperPart.sensed_state");
  EXPECT_EQ(GripperOpeningWidthStateVariablePath("gripper"),
            "@gripper.GripperPart.opening_width");
}

TEST(StateVariablePathTest, HumanReadableADIOPaths) {
  EXPECT_EQ(ADIODigitalInputStateVariablePath("end_of_arm", "hmi", 123),
            "@end_of_arm.ADIOPart.di.hmi[123]");
  EXPECT_EQ(ADIOAnalogInputStateVariablePath("end_of_arm", "hmi", 123),
            "@end_of_arm.ADIOPart.ai.hmi[123]");
  EXPECT_EQ(ADIOAnalogOutputStateVariablePath("end_of_arm", "hmi", 123),
            "@end_of_arm.ADIOPart.ao.hmi[123]");
  EXPECT_EQ(ADIODigitalOutputStateVariablePath("end_of_arm", "hmi", 123),
            "@end_of_arm.ADIOPart.do.hmi[123]");
}

TEST(StateVariablePathTest, HumanReadableRangefinderPaths) {
  EXPECT_EQ(RangefinderDistanceStateVariablePath("rf"),
            "@rf.RangefinderPart.distance");
}

TEST(StateVariablePathTest, HumanReadableSafetyPaths) {
  EXPECT_EQ(SafetyEnableButtonStatusStateVariablePath(),
            "@Safety.enable_button_status");
}

}  // namespace intrinsic::icon
