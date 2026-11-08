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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstddef>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "internal/testing.h"
#include "intrinsic/eigenmath/manifolds.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/control/c_api/c_feature_interfaces.h"
#include "intrinsic/icon/control/c_api/wrappers/feature_interface_wrappers.h"
#include "intrinsic/icon/control/c_api/wrappers/force_torque_sensor_wrapper.h"
#include "intrinsic/icon/control/c_api/wrappers/joint_limits_wrapper.h"
#include "intrinsic/icon/control/c_api/wrappers/joint_position_sensor_wrapper.h"
#include "intrinsic/icon/control/c_api/wrappers/joint_position_wrapper.h"
#include "intrinsic/icon/control/c_api/wrappers/joint_velocity_estimator_wrapper.h"
#include "intrinsic/icon/control/c_api/wrappers/manipulator_kinematics_wrapper.h"
#include "intrinsic/icon/control/joint_position_command.h"
#include "intrinsic/icon/control/parts/fake_feature_interfaces.h"
#include "intrinsic/icon/testing/malloc_test.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/icon/utils/realtime_status_matchers.h"
#include "intrinsic/icon/utils/realtime_status_or.h"
#include "intrinsic/kinematics/types/joint_limits.h"
#include "intrinsic/kinematics/types/joint_state.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/twist.h"
#include "protobuf-matchers/protocol-buffer-matchers.h"

namespace intrinsic::icon {
namespace {

using ::absl_testing::StatusIs;
using ::protobuf_matchers::EqualsProto;
using ::testing::Each;
using ::testing::Optional;

constexpr size_t kNdof = 6;

// This fixture holds fakes and wrapped C API pointers to all supported feature
// interfaces, plus a copy of the vtable for those wrapped pointers for
// convenience.
class IconFeatureInterfacesTest : public ::testing::Test {
 public:
  IconFeatureInterfacesTest()
      : joint_position_fake_(kNdof),
        joint_position_sensor_fake_(kNdof),
        joint_velocity_estimator_fake_(kNdof),
        expected_application_limits_(CreateSimpleJointLimits(
            /*ndof=*/kNdof, /*max_position=*/1, /*max_velocity=*/2,
            /*max_acceleration=*/3, /*max_jerk=*/4)),
        expected_system_limits_(CreateSimpleJointLimits(
            /*ndof=*/kNdof, /*max_position=*/10, /*max_velocity=*/20,
            /*max_acceleration=*/30, /*max_jerk=*/40)),
        joint_limits_fake_(/*application_limits=*/expected_application_limits_,
                           /*system_limits=*/expected_system_limits_),
        manipulator_kinematics_fake_(kNdof),
        force_torque_sensor_fake_(),
        feature_interfaces_wrapped_({
            .joint_position = Wrap(&joint_position_fake_),
            .joint_position_sensor = Wrap(&joint_position_sensor_fake_),
            .joint_velocity_estimator = Wrap(&joint_velocity_estimator_fake_),
            .joint_limits = Wrap(&joint_limits_fake_),
            .manipulator_kinematics = Wrap(&manipulator_kinematics_fake_),
            .force_torque_sensor = Wrap(&force_torque_sensor_fake_),
        }),
        const_feature_interfaces_wrapped_({
            .joint_position = Wrap(&joint_position_fake_),
            .joint_position_sensor = Wrap(&joint_position_sensor_fake_),
            .joint_velocity_estimator = Wrap(&joint_velocity_estimator_fake_),
            .joint_limits = Wrap(&joint_limits_fake_),
            .manipulator_kinematics = Wrap(&manipulator_kinematics_fake_),
            .force_torque_sensor = Wrap(&force_torque_sensor_fake_),
        }),
        feature_interface_vtable_(GetFeatureInterfaceVtable()) {}

 protected:
  FakeJointPosition joint_position_fake_;
  FakeJointPositionSensor joint_position_sensor_fake_;
  FakeJointVelocityEstimator joint_velocity_estimator_fake_;
  JointLimits expected_application_limits_;
  JointLimits expected_system_limits_;
  FakeJointLimits joint_limits_fake_;
  FakeManipulatorKinematics manipulator_kinematics_fake_;
  FakeForceTorqueSensor force_torque_sensor_fake_;

  IntrinsicIconFeatureInterfacesForSlot feature_interfaces_wrapped_;
  IntrinsicIconConstFeatureInterfacesForSlot const_feature_interfaces_wrapped_;
  IntrinsicIconFeatureInterfaceVtable feature_interface_vtable_;
};

TEST_F(IconFeatureInterfacesTest, Init) {}

TEST_F(IconFeatureInterfacesTest,
       MutableJointPositionCommandCtorDoesNotMalloc) {
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  IconJointPositionCommandInterface joint_position_command(
      feature_interfaces_wrapped_.joint_position,
      feature_interface_vtable_.joint_position);
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
}

TEST_F(IconFeatureInterfacesTest, JointPositionCommandConstCtorDoesNotMalloc) {
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  IconConstJointPositionCommandInterface joint_position_command(
      const_feature_interfaces_wrapped_.joint_position,
      feature_interface_vtable_.joint_position);
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
}

TEST_F(IconFeatureInterfacesTest, SetPositionSetpoints) {
  IconJointPositionCommandInterface joint_position_command(
      feature_interfaces_wrapped_.joint_position,
      feature_interface_vtable_.joint_position);
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  INTRINSIC_RT_EXPECT_OK(joint_position_command.SetPositionSetpoints(
      JointPositionCommand(eigenmath::VectorNd::Constant(kNdof, 0.42))));
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
  EXPECT_THAT(joint_position_fake_.PreviousPositionSetpoints().position(),
              Each(0.42));
}

TEST_F(IconFeatureInterfacesTest,
       JointPositionCommandPreviousPositionSetpoints) {
  IconJointPositionCommandInterface joint_position_command(
      feature_interfaces_wrapped_.joint_position,
      feature_interface_vtable_.joint_position);
  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
      JointPositionCommand expected_command,
      JointPositionCommand::Create(
          /*position=*/eigenmath::VectorNd::Constant(kNdof, 0.42),
          /*velocity_feedforward=*/eigenmath::VectorNd::Constant(kNdof, 0.23),
          /*acceleration_feedforward=*/
          eigenmath::VectorNd::Constant(kNdof, 256)));
  EXPECT_OK(joint_position_fake_.SetPositionSetpoints(expected_command));
  {
    IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
    JointPositionCommand previous_command =
        joint_position_command.PreviousPositionSetpoints();
    IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
    EXPECT_THAT(previous_command.position(), Each(0.42));
    EXPECT_THAT(previous_command.velocity_feedforward(), Optional(Each(0.23)));
    EXPECT_THAT(previous_command.acceleration_feedforward(),
                Optional(Each(256)));
  }
}

TEST_F(IconFeatureInterfacesTest,
       JointPositionCommandPreviousPositionSetpointsConst) {
  IconConstJointPositionCommandInterface joint_position_command(
      const_feature_interfaces_wrapped_.joint_position,
      feature_interface_vtable_.joint_position);
  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
      JointPositionCommand expected_command,
      JointPositionCommand::Create(
          /*position=*/eigenmath::VectorNd::Constant(kNdof, 0.42),
          /*velocity_feedforward=*/eigenmath::VectorNd::Constant(kNdof, 0.23),
          /*acceleration_feedforward=*/
          eigenmath::VectorNd::Constant(kNdof, 2.56)));
  EXPECT_OK(joint_position_fake_.SetPositionSetpoints(expected_command));
  {
    IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
    JointPositionCommand previous_command =
        joint_position_command.PreviousPositionSetpoints();
    IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
    EXPECT_THAT(previous_command.position(), Each(0.42));
    EXPECT_THAT(previous_command.velocity_feedforward(), Optional(Each(0.23)));
    EXPECT_THAT(previous_command.acceleration_feedforward(),
                Optional(Each(2.56)));
  }
}

TEST_F(IconFeatureInterfacesTest, JointPositionSensorCtorDoesNotMalloc) {
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  IconJointPositionSensor joint_position_sensor(
      feature_interfaces_wrapped_.joint_position_sensor,
      feature_interface_vtable_.joint_position_sensor);
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
}

TEST_F(IconFeatureInterfacesTest, JointPositionSensorGetSensedPosition) {
  IconJointPositionSensor joint_position_sensor(
      feature_interfaces_wrapped_.joint_position_sensor,
      feature_interface_vtable_.joint_position_sensor);
  JointStateP expected_sensed_position;
  EXPECT_OK(expected_sensed_position.SetSize(kNdof));
  expected_sensed_position.position.setConstant(123);
  joint_position_sensor_fake_.SetSensedPosition(expected_sensed_position);
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  JointStateP sensed_position = joint_position_sensor.GetSensedPosition();
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
  EXPECT_THAT(sensed_position.position, Each(123));
}

TEST_F(IconFeatureInterfacesTest, JointVelocityEstimatorCtorDoesNotMalloc) {
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  IconJointVelocityEstimator joint_velocity_estimator(
      feature_interfaces_wrapped_.joint_velocity_estimator,
      feature_interface_vtable_.joint_velocity_estimator);
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
}

TEST_F(IconFeatureInterfacesTest, JointVelocityEstimatorGetVelocityEstimate) {
  IconJointVelocityEstimator joint_velocity_estimator(
      feature_interfaces_wrapped_.joint_velocity_estimator,
      feature_interface_vtable_.joint_velocity_estimator);
  JointStateV expected_velocity_estimate;
  EXPECT_OK(expected_velocity_estimate.SetSize(kNdof));
  expected_velocity_estimate.velocity.setConstant(123);
  joint_velocity_estimator_fake_.SetVelocityEstimate(
      expected_velocity_estimate);
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  JointStateV velocity_estimate =
      joint_velocity_estimator.GetVelocityEstimate();
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
  EXPECT_THAT(velocity_estimate.velocity, Each(123));
}

TEST_F(IconFeatureInterfacesTest, JointLimitsCtorDoesNotMalloc) {
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  IconJointLimits joint_limits(feature_interfaces_wrapped_.joint_limits,
                               feature_interface_vtable_.joint_limits);
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
}

TEST_F(IconFeatureInterfacesTest, JointLimitsReturnsExpectedApplicationLimits) {
  IconJointLimits joint_limits(feature_interfaces_wrapped_.joint_limits,
                               feature_interface_vtable_.joint_limits);
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  JointLimits application_limits = joint_limits.GetApplicationLimits();
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
  EXPECT_THAT(ToProto(application_limits),
              EqualsProto(ToProto(expected_application_limits_)));
}

TEST_F(IconFeatureInterfacesTest, JointLimitsReturnsExpectedSystemLimits) {
  IconJointLimits joint_limits(feature_interfaces_wrapped_.joint_limits,
                               feature_interface_vtable_.joint_limits);
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  JointLimits system_limits = joint_limits.GetSystemLimits();
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
  EXPECT_THAT(ToProto(system_limits),
              EqualsProto(ToProto(expected_system_limits_)));
}

TEST_F(IconFeatureInterfacesTest, ManipulatorKinematicsCtorDoesNotMalloc) {
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  IconManipulatorKinematics manipulator_kinematics(
      feature_interfaces_wrapped_.manipulator_kinematics,
      feature_interface_vtable_.manipulator_kinematics);
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
}

TEST_F(IconFeatureInterfacesTest,
       ManipulatorKinematicsComputeChainJacobianReturnsError) {
  IconManipulatorKinematics manipulator_kinematics(
      feature_interfaces_wrapped_.manipulator_kinematics,
      feature_interface_vtable_.manipulator_kinematics);
  manipulator_kinematics_fake_.SetJacobianReturnStatus(InternalError("oh no."));
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  RealtimeStatusOr<eigenmath::Matrix6Nd> jacobian =
      manipulator_kinematics.ComputeChainJacobian(JointStateP::Zero(kNdof));
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
  EXPECT_THAT(jacobian.status(),
              StatusIs(absl::StatusCode::kInternal, "oh no."));
}

TEST_F(IconFeatureInterfacesTest,
       ManipulatorKinematicsComputeChainJacobianSucceeds) {
  IconManipulatorKinematics manipulator_kinematics(
      feature_interfaces_wrapped_.manipulator_kinematics,
      feature_interface_vtable_.manipulator_kinematics);
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  RealtimeStatusOr<eigenmath::Matrix6Nd> jacobian =
      manipulator_kinematics.ComputeChainJacobian(JointStateP::Zero(kNdof));
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
  EXPECT_OK(jacobian.status());
  EXPECT_TRUE(jacobian.value().isIdentity());
}

TEST_F(IconFeatureInterfacesTest,
       ManipulatorKinematicsComputeChainFKReturnsError) {
  IconManipulatorKinematics manipulator_kinematics(
      feature_interfaces_wrapped_.manipulator_kinematics,
      feature_interface_vtable_.manipulator_kinematics);
  manipulator_kinematics_fake_.SetFKReturnStatus(InternalError("oh no."));
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  RealtimeStatusOr<Pose3d> fk_pose =
      manipulator_kinematics.ComputeChainFK(JointStateP::Zero(kNdof));
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
  EXPECT_THAT(fk_pose.status(),
              StatusIs(absl::StatusCode::kInternal, "oh no."));
}

TEST_F(IconFeatureInterfacesTest, ManipulatorKinematicsComputeChainFKSucceeds) {
  IconManipulatorKinematics manipulator_kinematics(
      feature_interfaces_wrapped_.manipulator_kinematics,
      feature_interface_vtable_.manipulator_kinematics);
  JointStateP input_position;
  ASSERT_OK(input_position.SetSize(kNdof));
  input_position.position << 1, 2, 3, 4, 5, 6;

  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  RealtimeStatusOr<Pose3d> fk_pose =
      manipulator_kinematics.ComputeChainFK(input_position);
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();

  EXPECT_OK(fk_pose.status());
  // See documentation of FakeManipulatorKinematics
  // (intrinsic/icon/control/parts/fake_feature_interfaces.h) for the
  // definition of this expected output.
  eigenmath::Quaterniond expected_quaternion(
      intrinsic::eigenmath::expSO3(
          eigenmath::Vector3d(input_position.position.tail<3>()))
          .quaternion());

  EXPECT_EQ(fk_pose.value().translation().x(), 1);
  EXPECT_EQ(fk_pose.value().translation().y(), 2);
  EXPECT_EQ(fk_pose.value().translation().z(), 3);
  EXPECT_EQ(fk_pose.value().quaternion().x(), expected_quaternion.x());
  EXPECT_EQ(fk_pose.value().quaternion().y(), expected_quaternion.y());
  EXPECT_EQ(fk_pose.value().quaternion().z(), expected_quaternion.z());
  EXPECT_EQ(fk_pose.value().quaternion().w(), expected_quaternion.w());
}

TEST_F(IconFeatureInterfacesTest, ForcetorqueSensorCtorDoesNotMalloc) {
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  IconForceTorqueSensor force_torque_sensor(
      feature_interfaces_wrapped_.force_torque_sensor,
      feature_interface_vtable_.force_torque_sensor);
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
}

TEST_F(IconFeatureInterfacesTest, ForcetorqueSensorConstCtorDoesNotMalloc) {
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  IconConstForceTorqueSensor force_torque_sensor(
      const_feature_interfaces_wrapped_.force_torque_sensor,
      feature_interface_vtable_.force_torque_sensor);
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
}

TEST_F(IconFeatureInterfacesTest, ForcetorqueSensorWrenchAtTip) {
  IconForceTorqueSensor force_torque_sensor(
      feature_interfaces_wrapped_.force_torque_sensor,
      feature_interface_vtable_.force_torque_sensor);
  Wrench expected_wrench(1, 2, 3, 4, 5, 6);
  force_torque_sensor_fake_.SetWrenchAtTip(expected_wrench);
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  Wrench actual_wrench = force_torque_sensor.WrenchAtTip();
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
  EXPECT_EQ(actual_wrench, expected_wrench);
}

TEST_F(IconFeatureInterfacesTest, ForcetorqueSensorWrenchAtTipConst) {
  IconConstForceTorqueSensor force_torque_sensor(
      const_feature_interfaces_wrapped_.force_torque_sensor,
      feature_interface_vtable_.force_torque_sensor);
  Wrench expected_wrench(1, 2, 3, 4, 5, 6);
  force_torque_sensor_fake_.SetWrenchAtTip(expected_wrench);
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  Wrench actual_wrench = force_torque_sensor.WrenchAtTip();
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
  EXPECT_EQ(actual_wrench, expected_wrench);
}

TEST_F(IconFeatureInterfacesTest, ForcetorqueSensorTareForwardsError) {
  IconForceTorqueSensor force_torque_sensor(
      feature_interfaces_wrapped_.force_torque_sensor,
      feature_interface_vtable_.force_torque_sensor);
  force_torque_sensor_fake_.SetReturnStatus(InternalError("oh no."));
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  RealtimeStatus tare_status = force_torque_sensor.Tare();
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
  EXPECT_THAT(tare_status, StatusIs(absl::StatusCode::kInternal, "oh no."));
}

TEST_F(IconFeatureInterfacesTest, FromCApiFeatureInterfaces) {
  IconFeatureInterfaces all_feature_interfaces = FromCApiFeatureInterfaces(
      feature_interfaces_wrapped_, feature_interface_vtable_);
  EXPECT_TRUE(all_feature_interfaces.joint_position.has_value());
  EXPECT_TRUE(all_feature_interfaces.joint_position_sensor.has_value());
  EXPECT_TRUE(all_feature_interfaces.joint_velocity_estimator.has_value());
  EXPECT_TRUE(all_feature_interfaces.joint_limits.has_value());
  EXPECT_TRUE(all_feature_interfaces.manipulator_kinematics.has_value());
  EXPECT_TRUE(all_feature_interfaces.force_torque_sensor.has_value());
}

TEST_F(IconFeatureInterfacesTest, FromCApiFeatureInterfacesConst) {
  IconConstFeatureInterfaces all_feature_interfaces = FromCApiFeatureInterfaces(
      const_feature_interfaces_wrapped_, feature_interface_vtable_);
  EXPECT_TRUE(all_feature_interfaces.joint_position.has_value());
  EXPECT_TRUE(all_feature_interfaces.joint_position_sensor.has_value());
  EXPECT_TRUE(all_feature_interfaces.joint_velocity_estimator.has_value());
  EXPECT_TRUE(all_feature_interfaces.joint_limits.has_value());
  EXPECT_TRUE(all_feature_interfaces.manipulator_kinematics.has_value());
  EXPECT_TRUE(all_feature_interfaces.force_torque_sensor.has_value());
}

TEST_F(IconFeatureInterfacesTest,
       FromCApiFeatureInterfacesWithNullptrValuesContainsNullopt) {
  IconFeatureInterfaces all_feature_interfaces = FromCApiFeatureInterfaces(
      IntrinsicIconFeatureInterfacesForSlot{
          .joint_position = nullptr,
          .joint_position_sensor = nullptr,
          .joint_velocity_estimator = nullptr,
          .manipulator_kinematics = nullptr,
          .force_torque_sensor = nullptr,
      },
      feature_interface_vtable_);
  EXPECT_FALSE(all_feature_interfaces.joint_position.has_value());
  EXPECT_FALSE(all_feature_interfaces.joint_position_sensor.has_value());
  EXPECT_FALSE(all_feature_interfaces.joint_velocity_estimator.has_value());
  EXPECT_FALSE(all_feature_interfaces.joint_limits.has_value());
  EXPECT_FALSE(all_feature_interfaces.manipulator_kinematics.has_value());
  EXPECT_FALSE(all_feature_interfaces.force_torque_sensor.has_value());
}

TEST_F(IconFeatureInterfacesTest,
       FromCApiFeatureInterfacesConstWithNullptrValuesContainsNullopt) {
  IconConstFeatureInterfaces all_feature_interfaces = FromCApiFeatureInterfaces(
      IntrinsicIconConstFeatureInterfacesForSlot{
          .joint_position = nullptr,
          .joint_position_sensor = nullptr,
          .joint_velocity_estimator = nullptr,
          .manipulator_kinematics = nullptr,
          .force_torque_sensor = nullptr,
      },
      feature_interface_vtable_);
  EXPECT_FALSE(all_feature_interfaces.joint_position.has_value());
  EXPECT_FALSE(all_feature_interfaces.joint_position_sensor.has_value());
  EXPECT_FALSE(all_feature_interfaces.joint_velocity_estimator.has_value());
  EXPECT_FALSE(all_feature_interfaces.joint_limits.has_value());
  EXPECT_FALSE(all_feature_interfaces.manipulator_kinematics.has_value());
  EXPECT_FALSE(all_feature_interfaces.force_torque_sensor.has_value());
}

}  // namespace
}  // namespace intrinsic::icon
