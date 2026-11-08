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

package icon

import (
	"testing"
)

func TestStateVariablePaths(t *testing.T) {
	tests := []struct {
		got  string
		want string
	}{
		{
			got:  ArmSensedPosition("left_arm", 1),
			want: "@left_arm.ArmPart.sensed_position[1]",
		},
		{
			got:  ArmSensedVelocity("left_arm", 1),
			want: "@left_arm.ArmPart.sensed_velocity[1]",
		},
		{
			got:  ArmSensedAcceleration("left_arm", 1),
			want: "@left_arm.ArmPart.sensed_acceleration[1]",
		},
		{
			got:  ArmSensedTorque("left_arm", 1),
			want: "@left_arm.ArmPart.sensed_torque[1]",
		},
		{
			got:  ArmBaseTwistTipSensed("left_arm", TwistY),
			want: "@left_arm.ArmPart.base_twist_tip_sensed[1]",
		},
		{
			got:  ArmBaseLinearVelocityTipSensed("left_arm"),
			want: "@left_arm.ArmPart.base_linear_velocity_tip_sensed",
		},
		{
			got:  ArmBaseAngularVelocityTipSensed("left_arm"),
			want: "@left_arm.ArmPart.base_angular_velocity_tip_sensed",
		},
		{
			got:  ArmCurrentControlMode("left_arm"),
			want: "@left_arm.ArmPart.current_control_mode",
		},
		{
			got:  FTWrenchAtTip("ft", WrenchRZ),
			want: "@ft.ForceTorqueSensorPart.wrench_at_tip[5]",
		},
		{
			got:  FTForceMagnitudeAtTip("ft"),
			want: "@ft.ForceTorqueSensorPart.force_magnitude_at_tip",
		},
		{
			got:  FTTorqueMagnitudeAtTip("ft"),
			want: "@ft.ForceTorqueSensorPart.torque_magnitude_at_tip",
		},
		{
			got:  FTWrenchStabilityIndex("ft"),
			want: "@ft.ForceTorqueSensorPart.wrench_stability_index",
		},
		{
			got:  GripperSensedState("my_gripper"),
			want: "@my_gripper.GripperPart.sensed_state",
		},
		{
			got:  GripperOpeningWidth("my_gripper"),
			want: "@my_gripper.GripperPart.opening_width",
		},
		{
			got:  RangefinderDistance("rangefinder"),
			want: "@rangefinder.RangefinderPart.distance",
		},
		{
			got:  ADIODigitalInput("my_adio", "hmi", 3),
			want: "@my_adio.ADIOPart.di.hmi[3]",
		},
		{
			got:  ADIODigitalOutput("my_adio", "hmi", 3),
			want: "@my_adio.ADIOPart.do.hmi[3]",
		},
		{
			got:  ADIOAnalogInput("my_adio", "hmi", 3),
			want: "@my_adio.ADIOPart.ai.hmi[3]",
		},
		{
			got:  ADIOAnalogOutput("my_adio", "hmi", 3),
			want: "@my_adio.ADIOPart.ao.hmi[3]",
		},
		{
			got:  SafetyEnableButtonStatus(),
			want: "@Safety.enable_button_status",
		},
	}

	for _, tc := range tests {
		if tc.got != tc.want {
			t.Errorf("got path  %q, want %q", tc.got, tc.want)
		}
	}
}
