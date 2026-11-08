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


#include <gtest/gtest.h>

#include "flatbuffers/buffer.h"
#include "flatbuffers/flatbuffers.h"  // IWYU pragma: keep
#include "intrinsic/icon/flatbuffers/state_types.fbs.h"
#include "intrinsic/icon/flatbuffers/state_types.h"

namespace intrinsic {
namespace {

using ::testing::Test;

// Tests CartStateP
TEST(RoboticsTypesTest, ChecksCartStateP) {
  intrinsic_fbs::CartStateP cart_state;

  EXPECT_EQ(cart_state.pose().position().x(), 0);
  EXPECT_EQ(cart_state.pose().position().y(), 0);
  EXPECT_EQ(cart_state.pose().position().z(), 0);

  EXPECT_EQ(cart_state.pose().rotation().qx(), 0);
  EXPECT_EQ(cart_state.pose().rotation().qy(), 0);
  EXPECT_EQ(cart_state.pose().rotation().qz(), 0);
  EXPECT_EQ(cart_state.pose().rotation().qw(), 0);

  cart_state.mutable_pose().mutable_position().mutate_x(10);
  EXPECT_EQ(cart_state.pose().position().x(), 10);

  cart_state.mutable_pose().mutable_rotation().mutate_qw(10);
  EXPECT_EQ(cart_state.pose().rotation().qw(), 10);
}

// Tests CartStatePVA
TEST(RoboticsTypesTest, ChecksCartStatePVA) {
  intrinsic_fbs::CartStatePVA cart_state;

  EXPECT_EQ(cart_state.pose().position().x(), 0);
  EXPECT_EQ(cart_state.pose().position().y(), 0);
  EXPECT_EQ(cart_state.pose().position().z(), 0);

  EXPECT_EQ(cart_state.pose().rotation().qx(), 0);
  EXPECT_EQ(cart_state.pose().rotation().qy(), 0);
  EXPECT_EQ(cart_state.pose().rotation().qz(), 0);
  EXPECT_EQ(cart_state.pose().rotation().qw(), 0);

  EXPECT_EQ(cart_state.velocity().x(), 0);
  EXPECT_EQ(cart_state.velocity().y(), 0);
  EXPECT_EQ(cart_state.velocity().z(), 0);
  EXPECT_EQ(cart_state.velocity().rx(), 0);
  EXPECT_EQ(cart_state.velocity().ry(), 0);
  EXPECT_EQ(cart_state.velocity().rz(), 0);

  EXPECT_EQ(cart_state.acceleration().x(), 0);
  EXPECT_EQ(cart_state.acceleration().y(), 0);
  EXPECT_EQ(cart_state.acceleration().z(), 0);
  EXPECT_EQ(cart_state.acceleration().rx(), 0);
  EXPECT_EQ(cart_state.acceleration().ry(), 0);
  EXPECT_EQ(cart_state.acceleration().rz(), 0);

  cart_state.mutable_pose().mutable_position().mutate_x(10);
  EXPECT_EQ(cart_state.pose().position().x(), 10);

  cart_state.mutable_pose().mutable_rotation().mutate_qw(10);
  EXPECT_EQ(cart_state.pose().rotation().qw(), 10);

  cart_state.mutable_velocity().mutate_z(10);
  EXPECT_EQ(cart_state.velocity().z(), 10);

  cart_state.mutable_acceleration().mutate_rx(10);
  EXPECT_EQ(cart_state.acceleration().rx(), 10);
}

// Tests CartStateTPVA
TEST(RoboticsTypesTest, ChecksCartStateTPVA) {
  intrinsic_fbs::CartStateTPVA cart_state;

  EXPECT_EQ(cart_state.timestamp_ns(), 0);

  EXPECT_EQ(cart_state.pose().position().x(), 0);
  EXPECT_EQ(cart_state.pose().position().y(), 0);
  EXPECT_EQ(cart_state.pose().position().z(), 0);

  EXPECT_EQ(cart_state.pose().rotation().qx(), 0);
  EXPECT_EQ(cart_state.pose().rotation().qy(), 0);
  EXPECT_EQ(cart_state.pose().rotation().qz(), 0);
  EXPECT_EQ(cart_state.pose().rotation().qw(), 0);

  EXPECT_EQ(cart_state.velocity().x(), 0);
  EXPECT_EQ(cart_state.velocity().y(), 0);
  EXPECT_EQ(cart_state.velocity().z(), 0);
  EXPECT_EQ(cart_state.velocity().rx(), 0);
  EXPECT_EQ(cart_state.velocity().ry(), 0);
  EXPECT_EQ(cart_state.velocity().rz(), 0);

  EXPECT_EQ(cart_state.acceleration().x(), 0);
  EXPECT_EQ(cart_state.acceleration().y(), 0);
  EXPECT_EQ(cart_state.acceleration().z(), 0);
  EXPECT_EQ(cart_state.acceleration().rx(), 0);
  EXPECT_EQ(cart_state.acceleration().ry(), 0);
  EXPECT_EQ(cart_state.acceleration().rz(), 0);

  cart_state.mutable_pose().mutable_position().mutate_x(10);
  EXPECT_EQ(cart_state.pose().position().x(), 10);

  cart_state.mutable_pose().mutable_rotation().mutate_qw(10);
  EXPECT_EQ(cart_state.pose().rotation().qw(), 10);

  cart_state.mutable_velocity().mutate_z(10);
  EXPECT_EQ(cart_state.velocity().z(), 10);

  cart_state.mutable_acceleration().mutate_rx(10);
  EXPECT_EQ(cart_state.acceleration().rx(), 10);

  cart_state.mutate_timestamp_ns(10);
  EXPECT_EQ(cart_state.timestamp_ns(), 10);
}

// Test JointStatePVTorque
TEST(RoboticsTypesTest, ChecksJointStatePVTorque) {
  auto joint_state_buffer = intrinsic_fbs::CreateJointStatePVTBuffer(4);
  auto state = flatbuffers::GetMutableRoot<intrinsic_fbs::JointStatePVTorque>(
      joint_state_buffer.data());
  ASSERT_NE(nullptr, state);

  EXPECT_EQ(state->position()->data()->size(), 4);
  EXPECT_EQ(state->position()->data()->Get(0), 0);
  EXPECT_EQ(state->position()->data()->Get(1), 0);
  EXPECT_EQ(state->position()->data()->Get(2), 0);
  EXPECT_EQ(state->position()->data()->Get(3), 0);

  EXPECT_EQ(state->velocity()->data()->size(), 4);
  EXPECT_EQ(state->velocity()->data()->Get(0), 0);
  EXPECT_EQ(state->velocity()->data()->Get(1), 0);
  EXPECT_EQ(state->velocity()->data()->Get(2), 0);
  EXPECT_EQ(state->velocity()->data()->Get(3), 0);

  EXPECT_EQ(state->torque()->data()->size(), 4);
  EXPECT_EQ(state->torque()->data()->Get(0), 0);
  EXPECT_EQ(state->torque()->data()->Get(1), 0);
  EXPECT_EQ(state->torque()->data()->Get(2), 0);
  EXPECT_EQ(state->torque()->data()->Get(3), 0);
}

// Test JointStateVAJ
TEST(RoboticsTypesTest, ChecksJointStateVAJ) {
  auto joint_state_buffer = intrinsic_fbs::CreateJointStateVAJBuffer(4);
  auto state = flatbuffers::GetMutableRoot<intrinsic_fbs::JointStateVAJ>(
      joint_state_buffer.data());
  ASSERT_NE(nullptr, state);

  EXPECT_EQ(state->velocity()->data()->size(), 4);
  EXPECT_EQ(state->velocity()->data()->Get(0), 0);
  EXPECT_EQ(state->velocity()->data()->Get(1), 0);
  EXPECT_EQ(state->velocity()->data()->Get(2), 0);
  EXPECT_EQ(state->velocity()->data()->Get(3), 0);

  EXPECT_EQ(state->acceleration()->data()->size(), 4);
  EXPECT_EQ(state->acceleration()->data()->Get(0), 0);
  EXPECT_EQ(state->acceleration()->data()->Get(1), 0);
  EXPECT_EQ(state->acceleration()->data()->Get(2), 0);
  EXPECT_EQ(state->acceleration()->data()->Get(3), 0);

  EXPECT_EQ(state->jerk()->data()->size(), 4);
  EXPECT_EQ(state->jerk()->data()->Get(0), 0);
  EXPECT_EQ(state->jerk()->data()->Get(1), 0);
  EXPECT_EQ(state->jerk()->data()->Get(2), 0);
  EXPECT_EQ(state->jerk()->data()->Get(3), 0);
}

// Test JointStatePVAJ
TEST(RoboticsTypesTest, ChecksJointStatePVAJ) {
  auto joint_state_buffer = intrinsic_fbs::CreateJointStatePVAJBuffer(4);
  auto state = flatbuffers::GetMutableRoot<intrinsic_fbs::JointStatePVAJ>(
      joint_state_buffer.data());
  ASSERT_NE(nullptr, state);

  EXPECT_EQ(state->position()->data()->size(), 4);
  EXPECT_EQ(state->position()->data()->Get(0), 0);
  EXPECT_EQ(state->position()->data()->Get(1), 0);
  EXPECT_EQ(state->position()->data()->Get(2), 0);
  EXPECT_EQ(state->position()->data()->Get(3), 0);

  EXPECT_EQ(state->velocity()->data()->size(), 4);
  EXPECT_EQ(state->velocity()->data()->Get(0), 0);
  EXPECT_EQ(state->velocity()->data()->Get(1), 0);
  EXPECT_EQ(state->velocity()->data()->Get(2), 0);
  EXPECT_EQ(state->velocity()->data()->Get(3), 0);

  EXPECT_EQ(state->acceleration()->data()->size(), 4);
  EXPECT_EQ(state->acceleration()->data()->Get(0), 0);
  EXPECT_EQ(state->acceleration()->data()->Get(1), 0);
  EXPECT_EQ(state->acceleration()->data()->Get(2), 0);
  EXPECT_EQ(state->acceleration()->data()->Get(3), 0);

  EXPECT_EQ(state->jerk()->data()->size(), 4);
  EXPECT_EQ(state->jerk()->data()->Get(0), 0);
  EXPECT_EQ(state->jerk()->data()->Get(1), 0);
  EXPECT_EQ(state->jerk()->data()->Get(2), 0);
  EXPECT_EQ(state->jerk()->data()->Get(3), 0);
}

// Test JointStatePVA
TEST(RoboticsTypesTest, ChecksJointStatePVA) {
  auto joint_state_buffer = intrinsic_fbs::CreateJointStatePVABuffer(4);
  auto state = flatbuffers::GetMutableRoot<intrinsic_fbs::JointStatePVA>(
      joint_state_buffer.data());
  ASSERT_NE(nullptr, state);

  EXPECT_EQ(state->position()->data()->size(), 4);
  EXPECT_EQ(state->position()->data()->Get(0), 0);
  EXPECT_EQ(state->position()->data()->Get(1), 0);
  EXPECT_EQ(state->position()->data()->Get(2), 0);
  EXPECT_EQ(state->position()->data()->Get(3), 0);

  EXPECT_EQ(state->velocity()->data()->size(), 4);
  EXPECT_EQ(state->velocity()->data()->Get(0), 0);
  EXPECT_EQ(state->velocity()->data()->Get(1), 0);
  EXPECT_EQ(state->velocity()->data()->Get(2), 0);
  EXPECT_EQ(state->velocity()->data()->Get(3), 0);

  EXPECT_EQ(state->acceleration()->data()->size(), 4);
  EXPECT_EQ(state->acceleration()->data()->Get(0), 0);
  EXPECT_EQ(state->acceleration()->data()->Get(1), 0);
  EXPECT_EQ(state->acceleration()->data()->Get(2), 0);
  EXPECT_EQ(state->acceleration()->data()->Get(3), 0);
}

// Test JointStateP
TEST(RoboticsTypesTest, ChecksJointStateP) {
  auto joint_state_buffer = intrinsic_fbs::CreateJointStatePBuffer(4);
  auto state = flatbuffers::GetMutableRoot<intrinsic_fbs::JointStateP>(
      joint_state_buffer.data());
  ASSERT_NE(nullptr, state);

  EXPECT_EQ(state->position()->data()->size(), 4);
  EXPECT_EQ(state->position()->data()->Get(0), 0);
  EXPECT_EQ(state->position()->data()->Get(1), 0);
  EXPECT_EQ(state->position()->data()->Get(2), 0);
  EXPECT_EQ(state->position()->data()->Get(3), 0);
}

// Test JointStatePV
TEST(RoboticsTypesTest, ChecksJointStatePV) {
  auto joint_state_buffer = intrinsic_fbs::CreateJointStatePVBuffer(4);
  auto state = flatbuffers::GetMutableRoot<intrinsic_fbs::JointStatePV>(
      joint_state_buffer.data());
  ASSERT_NE(nullptr, state);

  EXPECT_EQ(state->position()->data()->size(), 4);
  EXPECT_EQ(state->position()->data()->Get(0), 0);
  EXPECT_EQ(state->position()->data()->Get(1), 0);
  EXPECT_EQ(state->position()->data()->Get(2), 0);
  EXPECT_EQ(state->position()->data()->Get(3), 0);

  EXPECT_EQ(state->velocity()->data()->size(), 4);
  EXPECT_EQ(state->velocity()->data()->Get(0), 0);
  EXPECT_EQ(state->velocity()->data()->Get(1), 0);
  EXPECT_EQ(state->velocity()->data()->Get(2), 0);
  EXPECT_EQ(state->velocity()->data()->Get(3), 0);
}

// Test JointStateV
TEST(RoboticsTypesTest, ChecksJointStateV) {
  auto joint_state_buffer = intrinsic_fbs::CreateJointStateVBuffer(4);
  auto state = flatbuffers::GetMutableRoot<intrinsic_fbs::JointStateV>(
      joint_state_buffer.data());
  ASSERT_NE(nullptr, state);

  EXPECT_EQ(state->velocity()->data()->size(), 4);
  EXPECT_EQ(state->velocity()->data()->Get(0), 0);
  EXPECT_EQ(state->velocity()->data()->Get(1), 0);
  EXPECT_EQ(state->velocity()->data()->Get(2), 0);
  EXPECT_EQ(state->velocity()->data()->Get(3), 0);
}

// Test JointStateVA
TEST(RoboticsTypesTest, ChecksJointStateVA) {
  auto joint_state_buffer = intrinsic_fbs::CreateJointStateVABuffer(4);
  auto state = flatbuffers::GetMutableRoot<intrinsic_fbs::JointStateVA>(
      joint_state_buffer.data());
  ASSERT_NE(nullptr, state);

  EXPECT_EQ(state->velocity()->data()->size(), 4);
  EXPECT_EQ(state->velocity()->data()->Get(0), 0);
  EXPECT_EQ(state->velocity()->data()->Get(1), 0);
  EXPECT_EQ(state->velocity()->data()->Get(2), 0);
  EXPECT_EQ(state->velocity()->data()->Get(3), 0);

  EXPECT_EQ(state->acceleration()->data()->size(), 4);
  EXPECT_EQ(state->acceleration()->data()->Get(0), 0);
  EXPECT_EQ(state->acceleration()->data()->Get(1), 0);
  EXPECT_EQ(state->acceleration()->data()->Get(2), 0);
  EXPECT_EQ(state->acceleration()->data()->Get(3), 0);
}

}  // namespace
}  // namespace intrinsic
