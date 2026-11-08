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

#include "intrinsic/icon/control/c_api/convert_c_types.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstddef>

#include "absl/status/status_matchers.h"
#include "absl/strings/str_cat.h"
#include "internal/testing.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/control/c_api/c_types.h"
#include "intrinsic/icon/control/joint_position_command.h"
#include "intrinsic/icon/utils/realtime_status_matchers.h"
#include "intrinsic/kinematics/types/joint_limits.h"
#include "intrinsic/kinematics/types/joint_state.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/twist.h"

namespace intrinsic::icon {
namespace {

TEST(ConvertJointPositionCommand, PositionOnlyRoundtrip) {
  eigenmath::VectorNd position(3);
  position << 1, 2, 3;

  JointPositionCommand position_only_command = JointPositionCommand(position);
  IntrinsicIconJointPositionCommand c_position_only_command =
      Convert(position_only_command);
  EXPECT_EQ(c_position_only_command.size, position_only_command.Size());
  EXPECT_FALSE(c_position_only_command.has_velocity_feedforwards);
  EXPECT_FALSE(c_position_only_command.has_acceleration_feedforwards);
  JointPositionCommand position_only_command_roundtrip =
      Convert(c_position_only_command);
  EXPECT_EQ(position_only_command.Size(),
            position_only_command_roundtrip.Size());
  EXPECT_EQ(position_only_command.position(),
            position_only_command_roundtrip.position());
  EXPECT_EQ(position_only_command.velocity_feedforward(),
            position_only_command_roundtrip.velocity_feedforward());
  EXPECT_EQ(position_only_command.acceleration_feedforward(),
            position_only_command_roundtrip.acceleration_feedforward());
}

TEST(ConvertJointPositionCommand, FullJointPositionCommandRoundtrip) {
  eigenmath::VectorNd position(3);
  position << 1, 2, 3;
  eigenmath::VectorNd velocity(3);
  velocity << 4, 5, 6;
  eigenmath::VectorNd acceleration(3);
  acceleration << 7, 8, 9;

  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
      JointPositionCommand command,
      JointPositionCommand::Create(position, velocity, acceleration));
  IntrinsicIconJointPositionCommand c_command = Convert(command);
  EXPECT_EQ(c_command.size, command.Size());
  EXPECT_TRUE(c_command.has_velocity_feedforwards);
  EXPECT_TRUE(c_command.has_acceleration_feedforwards);
  JointPositionCommand command_roundtrip = Convert(c_command);
  EXPECT_EQ(command.Size(), command_roundtrip.Size());
  EXPECT_EQ(command.position(), command_roundtrip.position());
  EXPECT_EQ(command.velocity_feedforward(),
            command_roundtrip.velocity_feedforward());
  EXPECT_EQ(command.acceleration_feedforward(),
            command_roundtrip.acceleration_feedforward());
}

TEST(ConvertJointPositionCommandDeathTest, DiesOnInvalidSize) {
  IntrinsicIconJointPositionCommand command;
  command.size = 2 * kIntrinsicIconMaxNumberOfJoints;
  ASSERT_DEATH(Convert(command),
               absl::StrCat("more than the maximum of ",
                            kIntrinsicIconMaxNumberOfJoints, " joints"));
}

TEST(ConvertJointLimits, Roundtrip) {
  JointLimits limits;
  ASSERT_OK(limits.SetSize(3));
  limits.min_position << -1, -2, -3;
  limits.max_velocity << 1, 2, 3;
  limits.max_acceleration << 4, 5, 6;
  limits.max_jerk << 7, 8, 9;
  limits.max_torque << 10, 11, 12;

  JointLimits limits_roundtrip = Convert(Convert(limits));
  EXPECT_EQ(limits.size(), limits_roundtrip.size());
  EXPECT_EQ(limits.min_position, limits_roundtrip.min_position);
  EXPECT_EQ(limits.max_position, limits_roundtrip.max_position);
  EXPECT_EQ(limits.max_velocity, limits_roundtrip.max_velocity);
  EXPECT_EQ(limits.max_acceleration, limits_roundtrip.max_acceleration);
  EXPECT_EQ(limits.max_jerk, limits_roundtrip.max_jerk);
  EXPECT_EQ(limits.max_torque, limits_roundtrip.max_torque);
}

TEST(ConvertJointLimitsDeathTest, DiesOnInvalidSize) {
  IntrinsicIconJointLimits limits;
  limits.size = 2 * kIntrinsicIconMaxNumberOfJoints;
  ASSERT_DEATH(Convert(limits),
               absl::StrCat("more than the maximum of ",
                            kIntrinsicIconMaxNumberOfJoints, " joints"));
}

TEST(ConvertJointStateP, Roundtrip) {
  JointStateP joint_state_p;
  ASSERT_OK(joint_state_p.SetSize(3));
  joint_state_p.position << 1, 2, 3;

  JointStateP joint_state_p_roundtrip = Convert(Convert(joint_state_p));
  EXPECT_EQ(joint_state_p.size(), joint_state_p_roundtrip.size());
  EXPECT_EQ(joint_state_p.position, joint_state_p_roundtrip.position);
}

TEST(ConvertJointStatePDeathTest, DiesOnInvalidSize) {
  IntrinsicIconJointStateP joint_state;
  joint_state.size = 2 * kIntrinsicIconMaxNumberOfJoints;
  ASSERT_DEATH(Convert(joint_state),
               absl::StrCat("more than the maximum of ",
                            kIntrinsicIconMaxNumberOfJoints, " joints"));
}

TEST(ConvertJointStateV, Roundtrip) {
  JointStateV joint_state_v;
  ASSERT_OK(joint_state_v.SetSize(3));
  joint_state_v.velocity << 1, 2, 3;

  JointStateV joint_state_v_roundtrip = Convert(Convert(joint_state_v));
  EXPECT_EQ(joint_state_v.size(), joint_state_v_roundtrip.size());
  EXPECT_EQ(joint_state_v.velocity, joint_state_v_roundtrip.velocity);
}

TEST(ConvertJointStateVDeathTest, DiesOnInvalidSize) {
  IntrinsicIconJointStateV joint_state;
  joint_state.size = 2 * kIntrinsicIconMaxNumberOfJoints;
  ASSERT_DEATH(Convert(joint_state),
               absl::StrCat("more than the maximum of ",
                            kIntrinsicIconMaxNumberOfJoints, " joints"));
}

TEST(ConvertJointStateA, Roundtrip) {
  JointStateA joint_state_a;
  ASSERT_OK(joint_state_a.SetSize(3));
  joint_state_a.acceleration << 1, 2, 3;

  JointStateA joint_state_a_roundtrip = Convert(Convert(joint_state_a));
  EXPECT_EQ(joint_state_a.size(), joint_state_a_roundtrip.size());
  EXPECT_EQ(joint_state_a.acceleration, joint_state_a_roundtrip.acceleration);
}

TEST(ConvertJointStateADeathTest, DiesOnInvalidSize) {
  IntrinsicIconJointStateA joint_state;
  joint_state.size = 2 * kIntrinsicIconMaxNumberOfJoints;
  ASSERT_DEATH(Convert(joint_state),
               absl::StrCat("more than the maximum of ",
                            kIntrinsicIconMaxNumberOfJoints, " joints"));
}

TEST(ConvertQuaternion, Roundtrip) {
  eigenmath::Quaterniond quaternion(-0.510, -0.002, -0.816, 0.272);
  quaternion.normalize();

  eigenmath::Quaterniond quaternion_roundtrip = Convert(Convert(quaternion));
  EXPECT_EQ(quaternion, quaternion_roundtrip);
}

TEST(ConvertVector3, Roundtrip) {
  eigenmath::Vector3d vector(1, 2, 3);

  eigenmath::Vector3d vector_roundtrip = Convert(Convert(vector));
  EXPECT_EQ(vector, vector_roundtrip);
}

TEST(ConvertPose, Roundtrip) {
  Pose3d pose(
      eigenmath::Quaterniond(-0.510, -0.002, -0.816, 0.272).normalized(),
      eigenmath::Vector3d(1, 2, 3));

  Pose3d pose_roundtrip = Convert(Convert(pose));
  EXPECT_EQ(pose.translation(), pose_roundtrip.translation());
  EXPECT_EQ(pose.quaternion(), pose_roundtrip.quaternion());
}

TEST(ConvertWrench, Roundtrip) {
  Wrench wrench(1, 2, 3, 4, 5, 5);

  Wrench wrench_roundtrip = Convert(Convert(wrench));
  EXPECT_EQ(wrench, wrench_roundtrip);
}

TEST(ConvertMatrix6Nd, Roundtrip) {
  eigenmath::Matrix6Nd matrix(6, kIntrinsicIconMaxNumberOfJoints);
  for (size_t row = 0; row < 6; ++row) {
    for (size_t col = 0; col < kIntrinsicIconMaxNumberOfJoints; ++col) {
      matrix(row, col) = row + (col * 6);
    }
  }

  eigenmath::Matrix6Nd matrix_roundtrip = Convert(Convert(matrix));
  EXPECT_EQ(matrix, matrix_roundtrip);
}

}  // namespace
}  // namespace intrinsic::icon
