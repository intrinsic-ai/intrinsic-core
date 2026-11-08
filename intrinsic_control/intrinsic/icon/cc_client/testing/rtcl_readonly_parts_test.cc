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

#include "intrinsic/icon/cc_client/testing/rtcl_readonly_parts.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstddef>
#include <memory>

#include "absl/status/status_matchers.h"
#include "internal/testing.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/control/parts/fake_feature_interfaces.h"
#include "intrinsic/icon/control/parts/feature_interface_registry.h"
#include "intrinsic/icon/control/parts/feature_interfaces.h"
#include "intrinsic/icon/control/parts/io_block.h"
#include "intrinsic/icon/control/parts/realtime_part_interface.h"
#include "intrinsic/icon/dynamics/rigid_body_interface.h"
#include "intrinsic/kinematics/types/joint_limits.h"
#include "intrinsic/math/pose3.h"

namespace intrinsic::icon {
namespace {

TEST(FakePartBuilders, FakeNdofArm) {
  constexpr size_t kNdof = 6;
  std::unique_ptr<RealtimePartInterface> arm_part = FakeReadOnlyNDofArm(kNdof);
  // Check that all the interfaces we expect are present.
  EXPECT_OK(
      arm_part->GetFeatureInterfaces().StatusOrInterface<JointPosition>());
  EXPECT_OK(
      arm_part->GetFeatureInterfaces().StatusOrInterface<JointVelocity>());
  EXPECT_OK(arm_part->GetFeatureInterfaces().StatusOrInterface<JointTorque>());
  EXPECT_OK(arm_part->GetFeatureInterfaces()
                .StatusOrInterface<JointPositionSensor>());
  EXPECT_OK(arm_part->GetFeatureInterfaces()
                .StatusOrInterface<JointVelocityEstimator>());
  EXPECT_OK(
      arm_part->GetFeatureInterfaces().StatusOrInterface<JointTorqueSensor>());
  ASSERT_OK_AND_ASSIGN(JointLimitsInterface & jlimits,
                       arm_part->GetFeatureInterfaces()
                           .StatusOrInterface<JointLimitsInterface>());
  EXPECT_EQ(jlimits.GetApplicationLimits().size(), kNdof);
  EXPECT_EQ(jlimits.GetSystemLimits().size(), kNdof);
  EXPECT_OK(arm_part->GetFeatureInterfaces()
                .StatusOrInterface<CartesianLimitsInterface>());
  ASSERT_OK_AND_ASSIGN(ManipulatorKinematics & kinematics,
                       arm_part->GetFeatureInterfaces()
                           .StatusOrInterface<ManipulatorKinematics>());
  EXPECT_EQ(kinematics.GetKinematicsModel().GetNumberDegreesOfFreedom(), kNdof);
  ASSERT_OK_AND_ASSIGN(
      Dynamics & dynamics,
      arm_part->GetFeatureInterfaces().StatusOrInterface<Dynamics>());
  EXPECT_EQ(dynamics.GetRigidBodyInterface().GetNumDof(), kNdof);
}

TEST(FakePartBuilders, FakeGripper) {
  std::unique_ptr<RealtimePartInterface> gripper_part = FakeReadOnlyGripper();
  // Check that all the interfaces we expect are present.
  EXPECT_OK(
      gripper_part->GetFeatureInterfaces().StatusOrInterface<SimpleGripper>());
}

TEST(FakePartBuilders, FakeLinearGripper) {
  std::unique_ptr<RealtimePartInterface> gripper_part =
      FakeReadOnlyLinearGripper();
  // Check that all the interfaces we expect are present.
  EXPECT_OK(
      gripper_part->GetFeatureInterfaces().StatusOrInterface<LinearGripper>());
}

TEST(FakePartBuilders, FakeForceTorqueSensor) {
  std::unique_ptr<RealtimePartInterface> ft_sensor_part =
      FakeReadOnlyForceTorqueSensor();
  // Check that all the interfaces we expect are present.
  EXPECT_OK(ft_sensor_part->GetFeatureInterfaces()
                .StatusOrInterface<ForceTorqueSensor>());
}

TEST(FakePartBuilders, FakeNdofArmForceTorqueSensorPart) {
  constexpr size_t kNdof = 6;
  std::unique_ptr<RealtimePartInterface> arm_force_torque_sensor_part =
      FakeReadOnlyNDofArmForceTorqueSensorPart(kNdof);
  // Check that all the interfaces we expect are present.
  EXPECT_OK(arm_force_torque_sensor_part->GetFeatureInterfaces()
                .StatusOrInterface<ForceTorqueSensor>());
  EXPECT_OK(arm_force_torque_sensor_part->GetFeatureInterfaces()
                .StatusOrInterface<JointPosition>());
  EXPECT_OK(arm_force_torque_sensor_part->GetFeatureInterfaces()
                .StatusOrInterface<JointVelocity>());
  EXPECT_OK(arm_force_torque_sensor_part->GetFeatureInterfaces()
                .StatusOrInterface<JointTorque>());
  EXPECT_OK(arm_force_torque_sensor_part->GetFeatureInterfaces()
                .StatusOrInterface<JointPositionSensor>());
  EXPECT_OK(arm_force_torque_sensor_part->GetFeatureInterfaces()
                .StatusOrInterface<JointVelocityEstimator>());
  EXPECT_OK(arm_force_torque_sensor_part->GetFeatureInterfaces()
                .StatusOrInterface<JointTorqueSensor>());
  ASSERT_OK_AND_ASSIGN(JointLimitsInterface & jlimits,
                       arm_force_torque_sensor_part->GetFeatureInterfaces()
                           .StatusOrInterface<JointLimitsInterface>());
  EXPECT_EQ(jlimits.GetApplicationLimits().size(), kNdof);
  EXPECT_EQ(jlimits.GetSystemLimits().size(), kNdof);
  EXPECT_OK(arm_force_torque_sensor_part->GetFeatureInterfaces()
                .StatusOrInterface<CartesianLimitsInterface>());
  ASSERT_OK_AND_ASSIGN(ManipulatorKinematics & kinematics,
                       arm_force_torque_sensor_part->GetFeatureInterfaces()
                           .StatusOrInterface<ManipulatorKinematics>());
  EXPECT_EQ(kinematics.GetKinematicsModel().GetNumberDegreesOfFreedom(), kNdof);
  ASSERT_OK_AND_ASSIGN(Dynamics & dynamics,
                       arm_force_torque_sensor_part->GetFeatureInterfaces()
                           .StatusOrInterface<Dynamics>());
  EXPECT_EQ(dynamics.GetRigidBodyInterface().GetNumDof(), kNdof);
}

TEST(FakePartBuilders, FakeADIOPart) {
  std::unique_ptr<RealtimePartInterface> adio_part = FakeReadOnlyADIO();
  // Check that all the interfaces we expect are present.
  EXPECT_OK(adio_part->GetFeatureInterfaces().StatusOrInterface<ADIO>());
}

TEST(FakePartBuilders, FakeADIOPartWithInitialState) {
  FakeADIO::FakeADIOState state{
      .analog_inputs = {{"analog_in", AnalogBlock::Create(/*size=*/2).value()}},
      .digital_inputs = {{"digital_in", DioBlock::Create(/*size=*/2).value()}},
      .digital_outputs = {{"digital_out",
                           DioBlock::Create(/*size=*/2).value()}},
  };

  std::unique_ptr<RealtimePartInterface> adio_part = FakeReadOnlyADIO(state);
  // Check that all the interfaces we expect are present with the expected
  // blocks.
  ASSERT_OK_AND_ASSIGN(
      auto adio_interface,
      adio_part->GetFeatureInterfaces().StatusOrInterface<ADIO>());
  EXPECT_THAT(adio_interface.get().AnalogInputBlockNames(),
              ::testing::ElementsAre("analog_in"));
  EXPECT_THAT(adio_interface.get().DigitalInputBlockNames(),
              ::testing::ElementsAre("digital_in"));
  EXPECT_THAT(adio_interface.get().DigitalOutputBlockNames(),
              ::testing::ElementsAre("digital_out"));
}

TEST(FakePartBuilders, FakeRangefinder) {
  std::unique_ptr<RealtimePartInterface> rangefinder_part =
      FakeReadOnlyRangefinderPart(1.0,
                                  Pose3d(eigenmath::Vector3d(1.0, 2.0, 3.0)));

  ASSERT_OK_AND_ASSIGN(auto rangefinder_interface,
                       rangefinder_part->GetFeatureInterfaces()
                           .StatusOrInterface<RangeFinder>());
  EXPECT_EQ(rangefinder_interface.get().GetSensedDistance(), 1.0);
  EXPECT_EQ(rangefinder_interface.get().GetPoseInTCPFrame().translation(),
            eigenmath::Vector3d(1.0, 2.0, 3.0));
}

}  // namespace
}  // namespace intrinsic::icon
