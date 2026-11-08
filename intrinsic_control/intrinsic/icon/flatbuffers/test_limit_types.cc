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

#include <limits>

#include "flatbuffers/buffer.h"
#include "intrinsic/icon/flatbuffers/limit_types.fbs.h"
#include "intrinsic/icon/flatbuffers/limit_types.h"

namespace intrinsic {
namespace {

using ::testing::Test;

// Tests CartLimitPVAJ
TEST(RoboticsTypesTest, ChecksCartLimitsPVAJ) {
  intrinsic_fbs::CartLimitsPVAJ cart_limit;

  EXPECT_EQ(cart_limit.max_translational_position().x(), 0);
  EXPECT_EQ(cart_limit.max_translational_position().y(), 0);
  EXPECT_EQ(cart_limit.max_translational_position().z(), 0);

  EXPECT_EQ(cart_limit.min_translational_position().x(), 0);
  EXPECT_EQ(cart_limit.min_translational_position().y(), 0);
  EXPECT_EQ(cart_limit.min_translational_position().z(), 0);

  EXPECT_EQ(cart_limit.max_translational_velocity().x(), 0);
  EXPECT_EQ(cart_limit.max_translational_velocity().y(), 0);
  EXPECT_EQ(cart_limit.max_translational_velocity().z(), 0);

  EXPECT_EQ(cart_limit.min_translational_velocity().x(), 0);
  EXPECT_EQ(cart_limit.min_translational_velocity().y(), 0);
  EXPECT_EQ(cart_limit.min_translational_velocity().z(), 0);

  EXPECT_EQ(cart_limit.max_translational_acceleration().x(), 0);
  EXPECT_EQ(cart_limit.max_translational_acceleration().y(), 0);
  EXPECT_EQ(cart_limit.max_translational_acceleration().z(), 0);

  EXPECT_EQ(cart_limit.min_translational_acceleration().x(), 0);
  EXPECT_EQ(cart_limit.min_translational_acceleration().y(), 0);
  EXPECT_EQ(cart_limit.min_translational_acceleration().z(), 0);

  EXPECT_EQ(cart_limit.max_translational_jerk().x(), 0);
  EXPECT_EQ(cart_limit.max_translational_jerk().y(), 0);
  EXPECT_EQ(cart_limit.max_translational_jerk().z(), 0);

  EXPECT_EQ(cart_limit.min_translational_jerk().x(), 0);
  EXPECT_EQ(cart_limit.min_translational_jerk().y(), 0);
  EXPECT_EQ(cart_limit.min_translational_jerk().z(), 0);

  EXPECT_EQ(cart_limit.max_rotational_velocity(), 0);
  EXPECT_EQ(cart_limit.max_rotational_acceleration(), 0.);
  EXPECT_EQ(cart_limit.max_rotational_jerk(), 0);

  cart_limit.mutate_max_rotational_velocity(10);
  EXPECT_EQ(cart_limit.max_rotational_velocity(), 10);

  cart_limit.mutate_max_rotational_acceleration(10);
  EXPECT_EQ(cart_limit.max_rotational_acceleration(), 10);

  cart_limit.mutate_max_rotational_jerk(10);
  EXPECT_EQ(cart_limit.max_rotational_jerk(), 10);
}

// Tests CartLimitPAJ
TEST(RoboticsTypesTest, ChecksCartLimitsPAJ) {
  intrinsic_fbs::CartLimitsPAJ cart_limit;

  EXPECT_EQ(cart_limit.max_translational_position().x(), 0);
  EXPECT_EQ(cart_limit.max_translational_position().y(), 0);
  EXPECT_EQ(cart_limit.max_translational_position().z(), 0);

  EXPECT_EQ(cart_limit.min_translational_position().x(), 0);
  EXPECT_EQ(cart_limit.min_translational_position().y(), 0);
  EXPECT_EQ(cart_limit.min_translational_position().z(), 0);

  EXPECT_EQ(cart_limit.max_translational_acceleration().x(), 0);
  EXPECT_EQ(cart_limit.max_translational_acceleration().y(), 0);
  EXPECT_EQ(cart_limit.max_translational_acceleration().z(), 0);

  EXPECT_EQ(cart_limit.min_translational_acceleration().x(), 0);
  EXPECT_EQ(cart_limit.min_translational_acceleration().y(), 0);
  EXPECT_EQ(cart_limit.min_translational_acceleration().z(), 0);

  EXPECT_EQ(cart_limit.max_translational_jerk().x(), 0);
  EXPECT_EQ(cart_limit.max_translational_jerk().y(), 0);
  EXPECT_EQ(cart_limit.max_translational_jerk().z(), 0);

  EXPECT_EQ(cart_limit.min_translational_jerk().x(), 0);
  EXPECT_EQ(cart_limit.min_translational_jerk().y(), 0);
  EXPECT_EQ(cart_limit.min_translational_jerk().z(), 0);

  EXPECT_EQ(cart_limit.max_rotational_acceleration(), 0.);
  EXPECT_EQ(cart_limit.max_rotational_jerk(), 0);

  cart_limit.mutate_max_rotational_acceleration(10);
  EXPECT_EQ(cart_limit.max_rotational_acceleration(), 10);

  cart_limit.mutate_max_rotational_jerk(10);
  EXPECT_EQ(cart_limit.max_rotational_jerk(), 10);
}

// Test JointLimitsPVAJ
TEST(RoboticsTypesTest, ChecksJointLimitsPVAJ) {
  auto joint_limits_buffer = intrinsic_fbs::CreateJointLimitsPVAJBuffer(4);
  auto limits = flatbuffers::GetMutableRoot<intrinsic_fbs::JointLimitsPVAJ>(
      joint_limits_buffer.data());
  ASSERT_NE(nullptr, limits);

  EXPECT_EQ(limits->max_position()->data()->size(), 4);
  EXPECT_EQ(limits->max_position()->data()->Get(0), 0);
  EXPECT_EQ(limits->max_position()->data()->Get(1), 0);
  EXPECT_EQ(limits->max_position()->data()->Get(2), 0);
  EXPECT_EQ(limits->max_position()->data()->Get(3), 0);

  EXPECT_EQ(limits->min_position()->data()->size(), 4);
  EXPECT_EQ(limits->min_position()->data()->Get(0), 0);
  EXPECT_EQ(limits->min_position()->data()->Get(1), 0);
  EXPECT_EQ(limits->min_position()->data()->Get(2), 0);
  EXPECT_EQ(limits->min_position()->data()->Get(3), 0);

  EXPECT_EQ(limits->max_velocity()->data()->size(), 4);
  EXPECT_EQ(limits->max_velocity()->data()->Get(0), 0);
  EXPECT_EQ(limits->max_velocity()->data()->Get(1), 0);
  EXPECT_EQ(limits->max_velocity()->data()->Get(2), 0);
  EXPECT_EQ(limits->max_velocity()->data()->Get(3), 0);

  EXPECT_EQ(limits->min_velocity()->data()->size(), 4);
  EXPECT_EQ(limits->min_velocity()->data()->Get(0), 0);
  EXPECT_EQ(limits->min_velocity()->data()->Get(1), 0);
  EXPECT_EQ(limits->min_velocity()->data()->Get(2), 0);
  EXPECT_EQ(limits->min_velocity()->data()->Get(3), 0);

  EXPECT_EQ(limits->max_acceleration()->data()->size(), 4);
  EXPECT_EQ(limits->max_acceleration()->data()->Get(0), 0);
  EXPECT_EQ(limits->max_acceleration()->data()->Get(1), 0);
  EXPECT_EQ(limits->max_acceleration()->data()->Get(2), 0);
  EXPECT_EQ(limits->max_acceleration()->data()->Get(3), 0);

  EXPECT_EQ(limits->min_acceleration()->data()->size(), 4);
  EXPECT_EQ(limits->min_acceleration()->data()->Get(0), 0);
  EXPECT_EQ(limits->min_acceleration()->data()->Get(1), 0);
  EXPECT_EQ(limits->min_acceleration()->data()->Get(2), 0);
  EXPECT_EQ(limits->min_acceleration()->data()->Get(3), 0);

  EXPECT_EQ(limits->max_jerk()->data()->size(), 4);
  EXPECT_EQ(limits->max_jerk()->data()->Get(0), 0);
  EXPECT_EQ(limits->max_jerk()->data()->Get(1), 0);
  EXPECT_EQ(limits->max_jerk()->data()->Get(2), 0);
  EXPECT_EQ(limits->max_jerk()->data()->Get(3), 0);

  EXPECT_EQ(limits->min_jerk()->data()->size(), 4);
  EXPECT_EQ(limits->min_jerk()->data()->Get(0), 0);
  EXPECT_EQ(limits->min_jerk()->data()->Get(1), 0);
  EXPECT_EQ(limits->min_jerk()->data()->Get(2), 0);
  EXPECT_EQ(limits->min_jerk()->data()->Get(3), 0);
}

// Test JointLimitsPVAJ unlimited
TEST(RoboticsTypesTest, ChecksJointLimitsPVAJUnlimited) {
  auto joint_limits_buffer =
      intrinsic_fbs::CreateJointLimitsPVAJBuffer(4, true);
  auto limits = flatbuffers::GetMutableRoot<intrinsic_fbs::JointLimitsPVAJ>(
      joint_limits_buffer.data());
  ASSERT_NE(nullptr, limits);

  EXPECT_EQ(limits->max_position()->data()->size(), 4);
  EXPECT_EQ(limits->max_position()->data()->Get(0),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->max_position()->data()->Get(1),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->max_position()->data()->Get(2),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->max_position()->data()->Get(3),
            std::numeric_limits<double>::infinity());

  EXPECT_EQ(limits->min_position()->data()->size(), 4);
  EXPECT_EQ(limits->min_position()->data()->Get(0),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->min_position()->data()->Get(1),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->min_position()->data()->Get(2),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->min_position()->data()->Get(3),
            std::numeric_limits<double>::infinity());

  EXPECT_EQ(limits->max_velocity()->data()->size(), 4);
  EXPECT_EQ(limits->max_velocity()->data()->Get(0),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->max_velocity()->data()->Get(1),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->max_velocity()->data()->Get(2),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->max_velocity()->data()->Get(3),
            std::numeric_limits<double>::infinity());

  EXPECT_EQ(limits->min_velocity()->data()->size(), 4);
  EXPECT_EQ(limits->min_velocity()->data()->Get(0),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->min_velocity()->data()->Get(1),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->min_velocity()->data()->Get(2),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->min_velocity()->data()->Get(3),
            std::numeric_limits<double>::infinity());

  EXPECT_EQ(limits->max_acceleration()->data()->size(), 4);
  EXPECT_EQ(limits->max_acceleration()->data()->Get(0),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->max_acceleration()->data()->Get(1),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->max_acceleration()->data()->Get(2),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->max_acceleration()->data()->Get(3),
            std::numeric_limits<double>::infinity());

  EXPECT_EQ(limits->min_acceleration()->data()->size(), 4);
  EXPECT_EQ(limits->min_acceleration()->data()->Get(0),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->min_acceleration()->data()->Get(1),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->min_acceleration()->data()->Get(2),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->min_acceleration()->data()->Get(3),
            std::numeric_limits<double>::infinity());

  EXPECT_EQ(limits->max_jerk()->data()->size(), 4);
  EXPECT_EQ(limits->max_jerk()->data()->Get(0),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->max_jerk()->data()->Get(1),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->max_jerk()->data()->Get(2),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->max_jerk()->data()->Get(3),
            std::numeric_limits<double>::infinity());

  EXPECT_EQ(limits->min_jerk()->data()->size(), 4);
  EXPECT_EQ(limits->min_jerk()->data()->Get(0),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->min_jerk()->data()->Get(1),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->min_jerk()->data()->Get(2),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->min_jerk()->data()->Get(3),
            std::numeric_limits<double>::infinity());
}

// Test JointLimitsPVA
TEST(RoboticsTypesTest, ChecksJointLimitsPVA) {
  auto joint_limits_buffer = intrinsic_fbs::CreateJointLimitsPVABuffer(4);
  auto limits = flatbuffers::GetMutableRoot<intrinsic_fbs::JointLimitsPVA>(
      joint_limits_buffer.data());
  ASSERT_NE(nullptr, limits);

  EXPECT_EQ(limits->max_position()->data()->size(), 4);
  EXPECT_EQ(limits->max_position()->data()->Get(0), 0);
  EXPECT_EQ(limits->max_position()->data()->Get(1), 0);
  EXPECT_EQ(limits->max_position()->data()->Get(2), 0);
  EXPECT_EQ(limits->max_position()->data()->Get(3), 0);

  EXPECT_EQ(limits->min_position()->data()->size(), 4);
  EXPECT_EQ(limits->min_position()->data()->Get(0), 0);
  EXPECT_EQ(limits->min_position()->data()->Get(1), 0);
  EXPECT_EQ(limits->min_position()->data()->Get(2), 0);
  EXPECT_EQ(limits->min_position()->data()->Get(3), 0);

  EXPECT_EQ(limits->max_velocity()->data()->size(), 4);
  EXPECT_EQ(limits->max_velocity()->data()->Get(0), 0);
  EXPECT_EQ(limits->max_velocity()->data()->Get(1), 0);
  EXPECT_EQ(limits->max_velocity()->data()->Get(2), 0);
  EXPECT_EQ(limits->max_velocity()->data()->Get(3), 0);

  EXPECT_EQ(limits->min_velocity()->data()->size(), 4);
  EXPECT_EQ(limits->min_velocity()->data()->Get(0), 0);
  EXPECT_EQ(limits->min_velocity()->data()->Get(1), 0);
  EXPECT_EQ(limits->min_velocity()->data()->Get(2), 0);
  EXPECT_EQ(limits->min_velocity()->data()->Get(3), 0);

  EXPECT_EQ(limits->max_acceleration()->data()->size(), 4);
  EXPECT_EQ(limits->max_acceleration()->data()->Get(0), 0);
  EXPECT_EQ(limits->max_acceleration()->data()->Get(1), 0);
  EXPECT_EQ(limits->max_acceleration()->data()->Get(2), 0);
  EXPECT_EQ(limits->max_acceleration()->data()->Get(3), 0);

  EXPECT_EQ(limits->min_acceleration()->data()->size(), 4);
  EXPECT_EQ(limits->min_acceleration()->data()->Get(0), 0);
  EXPECT_EQ(limits->min_acceleration()->data()->Get(1), 0);
  EXPECT_EQ(limits->min_acceleration()->data()->Get(2), 0);
  EXPECT_EQ(limits->min_acceleration()->data()->Get(3), 0);
}

// Test JointLimitsPVA unlimited
TEST(RoboticsTypesTest, ChecksJointLimitsPVAUnlimited) {
  auto joint_limits_buffer = intrinsic_fbs::CreateJointLimitsPVABuffer(4, true);
  auto limits = flatbuffers::GetMutableRoot<intrinsic_fbs::JointLimitsPVA>(
      joint_limits_buffer.data());
  ASSERT_NE(nullptr, limits);

  EXPECT_EQ(limits->max_position()->data()->size(), 4);
  EXPECT_EQ(limits->max_position()->data()->Get(0),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->max_position()->data()->Get(1),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->max_position()->data()->Get(2),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->max_position()->data()->Get(3),
            std::numeric_limits<double>::infinity());

  EXPECT_EQ(limits->min_position()->data()->size(), 4);
  EXPECT_EQ(limits->min_position()->data()->Get(0),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->min_position()->data()->Get(1),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->min_position()->data()->Get(2),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->min_position()->data()->Get(3),
            std::numeric_limits<double>::infinity());

  EXPECT_EQ(limits->max_velocity()->data()->size(), 4);
  EXPECT_EQ(limits->max_velocity()->data()->Get(0),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->max_velocity()->data()->Get(1),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->max_velocity()->data()->Get(2),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->max_velocity()->data()->Get(3),
            std::numeric_limits<double>::infinity());

  EXPECT_EQ(limits->min_velocity()->data()->size(), 4);
  EXPECT_EQ(limits->min_velocity()->data()->Get(0),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->min_velocity()->data()->Get(1),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->min_velocity()->data()->Get(2),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->min_velocity()->data()->Get(3),
            std::numeric_limits<double>::infinity());

  EXPECT_EQ(limits->max_acceleration()->data()->size(), 4);
  EXPECT_EQ(limits->max_acceleration()->data()->Get(0),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->max_acceleration()->data()->Get(1),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->max_acceleration()->data()->Get(2),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->max_acceleration()->data()->Get(3),
            std::numeric_limits<double>::infinity());

  EXPECT_EQ(limits->min_acceleration()->data()->size(), 4);
  EXPECT_EQ(limits->min_acceleration()->data()->Get(0),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->min_acceleration()->data()->Get(1),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->min_acceleration()->data()->Get(2),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->min_acceleration()->data()->Get(3),
            std::numeric_limits<double>::infinity());
}

// Test JointLimitsPAJ
TEST(RoboticsTypesTest, ChecksJointLimitsPAJ) {
  auto joint_limits_buffer = intrinsic_fbs::CreateJointLimitsPAJBuffer(4);
  auto limits = flatbuffers::GetMutableRoot<intrinsic_fbs::JointLimitsPAJ>(
      joint_limits_buffer.data());
  ASSERT_NE(nullptr, limits);

  EXPECT_EQ(limits->max_position()->data()->size(), 4);
  EXPECT_EQ(limits->max_position()->data()->Get(0), 0);
  EXPECT_EQ(limits->max_position()->data()->Get(1), 0);
  EXPECT_EQ(limits->max_position()->data()->Get(2), 0);
  EXPECT_EQ(limits->max_position()->data()->Get(3), 0);

  EXPECT_EQ(limits->min_position()->data()->size(), 4);
  EXPECT_EQ(limits->min_position()->data()->Get(0), 0);
  EXPECT_EQ(limits->min_position()->data()->Get(1), 0);
  EXPECT_EQ(limits->min_position()->data()->Get(2), 0);
  EXPECT_EQ(limits->min_position()->data()->Get(3), 0);

  EXPECT_EQ(limits->max_acceleration()->data()->size(), 4);
  EXPECT_EQ(limits->max_acceleration()->data()->Get(0), 0);
  EXPECT_EQ(limits->max_acceleration()->data()->Get(1), 0);
  EXPECT_EQ(limits->max_acceleration()->data()->Get(2), 0);
  EXPECT_EQ(limits->max_acceleration()->data()->Get(3), 0);

  EXPECT_EQ(limits->min_acceleration()->data()->size(), 4);
  EXPECT_EQ(limits->min_acceleration()->data()->Get(0), 0);
  EXPECT_EQ(limits->min_acceleration()->data()->Get(1), 0);
  EXPECT_EQ(limits->min_acceleration()->data()->Get(2), 0);
  EXPECT_EQ(limits->min_acceleration()->data()->Get(3), 0);

  EXPECT_EQ(limits->max_jerk()->data()->size(), 4);
  EXPECT_EQ(limits->max_jerk()->data()->Get(0), 0);
  EXPECT_EQ(limits->max_jerk()->data()->Get(1), 0);
  EXPECT_EQ(limits->max_jerk()->data()->Get(2), 0);
  EXPECT_EQ(limits->max_jerk()->data()->Get(3), 0);

  EXPECT_EQ(limits->min_jerk()->data()->size(), 4);
  EXPECT_EQ(limits->min_jerk()->data()->Get(0), 0);
  EXPECT_EQ(limits->min_jerk()->data()->Get(1), 0);
  EXPECT_EQ(limits->min_jerk()->data()->Get(2), 0);
  EXPECT_EQ(limits->min_jerk()->data()->Get(3), 0);
}

// Test JointLimitsPAJ unlimited
TEST(RoboticsTypesTest, ChecksJointLimitsPAJUnlimited) {
  auto joint_limits_buffer = intrinsic_fbs::CreateJointLimitsPAJBuffer(4, true);
  auto limits = flatbuffers::GetMutableRoot<intrinsic_fbs::JointLimitsPAJ>(
      joint_limits_buffer.data());
  ASSERT_NE(nullptr, limits);

  EXPECT_EQ(limits->max_position()->data()->size(), 4);
  EXPECT_EQ(limits->max_position()->data()->Get(0),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->max_position()->data()->Get(1),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->max_position()->data()->Get(2),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->max_position()->data()->Get(3),
            std::numeric_limits<double>::infinity());

  EXPECT_EQ(limits->min_position()->data()->size(), 4);
  EXPECT_EQ(limits->min_position()->data()->Get(0),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->min_position()->data()->Get(1),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->min_position()->data()->Get(2),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->min_position()->data()->Get(3),
            std::numeric_limits<double>::infinity());

  EXPECT_EQ(limits->max_acceleration()->data()->size(), 4);
  EXPECT_EQ(limits->max_acceleration()->data()->Get(0),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->max_acceleration()->data()->Get(1),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->max_acceleration()->data()->Get(2),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->max_acceleration()->data()->Get(3),
            std::numeric_limits<double>::infinity());

  EXPECT_EQ(limits->min_acceleration()->data()->size(), 4);
  EXPECT_EQ(limits->min_acceleration()->data()->Get(0),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->min_acceleration()->data()->Get(1),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->min_acceleration()->data()->Get(2),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->min_acceleration()->data()->Get(3),
            std::numeric_limits<double>::infinity());

  EXPECT_EQ(limits->max_jerk()->data()->size(), 4);
  EXPECT_EQ(limits->max_jerk()->data()->Get(0),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->max_jerk()->data()->Get(1),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->max_jerk()->data()->Get(2),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->max_jerk()->data()->Get(3),
            std::numeric_limits<double>::infinity());

  EXPECT_EQ(limits->min_jerk()->data()->size(), 4);
  EXPECT_EQ(limits->min_jerk()->data()->Get(0),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->min_jerk()->data()->Get(1),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->min_jerk()->data()->Get(2),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->min_jerk()->data()->Get(3),
            std::numeric_limits<double>::infinity());
}

// Test JointLimitsP
TEST(RoboticsTypesTest, ChecksJointLimitsP) {
  auto joint_limits_buffer = intrinsic_fbs::CreateJointLimitsPBuffer(4);
  auto limits = flatbuffers::GetMutableRoot<intrinsic_fbs::JointLimitsP>(
      joint_limits_buffer.data());
  ASSERT_NE(nullptr, limits);

  EXPECT_EQ(limits->max_position()->data()->size(), 4);
  EXPECT_EQ(limits->max_position()->data()->Get(0), 0);
  EXPECT_EQ(limits->max_position()->data()->Get(1), 0);
  EXPECT_EQ(limits->max_position()->data()->Get(2), 0);
  EXPECT_EQ(limits->max_position()->data()->Get(3), 0);

  EXPECT_EQ(limits->min_position()->data()->size(), 4);
  EXPECT_EQ(limits->min_position()->data()->Get(0), 0);
  EXPECT_EQ(limits->min_position()->data()->Get(1), 0);
  EXPECT_EQ(limits->min_position()->data()->Get(2), 0);
  EXPECT_EQ(limits->min_position()->data()->Get(3), 0);
}

// Test JointLimitsP unlimited
TEST(RoboticsTypesTest, ChecksJointLimitsPUnlimited) {
  auto joint_limits_buffer = intrinsic_fbs::CreateJointLimitsPBuffer(4, true);
  auto limits = flatbuffers::GetMutableRoot<intrinsic_fbs::JointLimitsP>(
      joint_limits_buffer.data());
  ASSERT_NE(nullptr, limits);

  EXPECT_EQ(limits->max_position()->data()->size(), 4);
  EXPECT_EQ(limits->max_position()->data()->Get(0),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->max_position()->data()->Get(1),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->max_position()->data()->Get(2),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->max_position()->data()->Get(3),
            std::numeric_limits<double>::infinity());

  EXPECT_EQ(limits->min_position()->data()->size(), 4);
  EXPECT_EQ(limits->min_position()->data()->Get(0),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->min_position()->data()->Get(1),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->min_position()->data()->Get(2),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->min_position()->data()->Get(3),
            std::numeric_limits<double>::infinity());
}

// Test JointLimitsPVAJBuffer
TEST(RoboticsTypesTest, ChecksJointLimitsPVAJBuffer) {
  auto buffer = intrinsic_fbs::CreateJointLimitsPVAJBuffer(4);
  auto limits = flatbuffers::GetMutableRoot<intrinsic_fbs::JointLimitsPVAJ>(
      buffer.data());

  EXPECT_EQ(limits->max_position()->data()->size(), 4);
  EXPECT_EQ(limits->max_position()->data()->Get(0), 0);
  EXPECT_EQ(limits->max_position()->data()->Get(1), 0);
  EXPECT_EQ(limits->max_position()->data()->Get(2), 0);
  EXPECT_EQ(limits->max_position()->data()->Get(3), 0);

  EXPECT_EQ(limits->min_position()->data()->size(), 4);
  EXPECT_EQ(limits->min_position()->data()->Get(0), 0);
  EXPECT_EQ(limits->min_position()->data()->Get(1), 0);
  EXPECT_EQ(limits->min_position()->data()->Get(2), 0);
  EXPECT_EQ(limits->min_position()->data()->Get(3), 0);

  EXPECT_EQ(limits->max_velocity()->data()->size(), 4);
  EXPECT_EQ(limits->max_velocity()->data()->Get(0), 0);
  EXPECT_EQ(limits->max_velocity()->data()->Get(1), 0);
  EXPECT_EQ(limits->max_velocity()->data()->Get(2), 0);
  EXPECT_EQ(limits->max_velocity()->data()->Get(3), 0);

  EXPECT_EQ(limits->min_velocity()->data()->size(), 4);
  EXPECT_EQ(limits->min_velocity()->data()->Get(0), 0);
  EXPECT_EQ(limits->min_velocity()->data()->Get(1), 0);
  EXPECT_EQ(limits->min_velocity()->data()->Get(2), 0);
  EXPECT_EQ(limits->min_velocity()->data()->Get(3), 0);

  EXPECT_EQ(limits->max_acceleration()->data()->size(), 4);
  EXPECT_EQ(limits->max_acceleration()->data()->Get(0), 0);
  EXPECT_EQ(limits->max_acceleration()->data()->Get(1), 0);
  EXPECT_EQ(limits->max_acceleration()->data()->Get(2), 0);
  EXPECT_EQ(limits->max_acceleration()->data()->Get(3), 0);

  EXPECT_EQ(limits->min_acceleration()->data()->size(), 4);
  EXPECT_EQ(limits->min_acceleration()->data()->Get(0), 0);
  EXPECT_EQ(limits->min_acceleration()->data()->Get(1), 0);
  EXPECT_EQ(limits->min_acceleration()->data()->Get(2), 0);
  EXPECT_EQ(limits->min_acceleration()->data()->Get(3), 0);

  EXPECT_EQ(limits->max_jerk()->data()->size(), 4);
  EXPECT_EQ(limits->max_jerk()->data()->Get(0), 0);
  EXPECT_EQ(limits->max_jerk()->data()->Get(1), 0);
  EXPECT_EQ(limits->max_jerk()->data()->Get(2), 0);
  EXPECT_EQ(limits->max_jerk()->data()->Get(3), 0);

  EXPECT_EQ(limits->min_jerk()->data()->size(), 4);
  EXPECT_EQ(limits->min_jerk()->data()->Get(0), 0);
  EXPECT_EQ(limits->min_jerk()->data()->Get(1), 0);
  EXPECT_EQ(limits->min_jerk()->data()->Get(2), 0);
  EXPECT_EQ(limits->min_jerk()->data()->Get(3), 0);

  limits->mutable_min_jerk()->mutable_data()->Mutate(0, 10);
  EXPECT_EQ(limits->min_jerk()->data()->Get(0), 10);
}

// Test JointLimitsPVAJ unlimited
TEST(RoboticsTypesTest, ChecksJointLimitsPVAJBufferUnlimited) {
  auto buffer = intrinsic_fbs::CreateJointLimitsPVAJBuffer(4, true);
  auto limits = flatbuffers::GetMutableRoot<intrinsic_fbs::JointLimitsPVAJ>(
      buffer.data());

  EXPECT_EQ(limits->max_position()->data()->size(), 4);
  EXPECT_EQ(limits->max_position()->data()->Get(0),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->max_position()->data()->Get(1),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->max_position()->data()->Get(2),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->max_position()->data()->Get(3),
            std::numeric_limits<double>::infinity());

  EXPECT_EQ(limits->min_position()->data()->size(), 4);
  EXPECT_EQ(limits->min_position()->data()->Get(0),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->min_position()->data()->Get(1),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->min_position()->data()->Get(2),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->min_position()->data()->Get(3),
            std::numeric_limits<double>::infinity());

  EXPECT_EQ(limits->max_velocity()->data()->size(), 4);
  EXPECT_EQ(limits->max_velocity()->data()->Get(0),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->max_velocity()->data()->Get(1),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->max_velocity()->data()->Get(2),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->max_velocity()->data()->Get(3),
            std::numeric_limits<double>::infinity());

  EXPECT_EQ(limits->min_velocity()->data()->size(), 4);
  EXPECT_EQ(limits->min_velocity()->data()->Get(0),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->min_velocity()->data()->Get(1),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->min_velocity()->data()->Get(2),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->min_velocity()->data()->Get(3),
            std::numeric_limits<double>::infinity());

  EXPECT_EQ(limits->max_acceleration()->data()->size(), 4);
  EXPECT_EQ(limits->max_acceleration()->data()->Get(0),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->max_acceleration()->data()->Get(1),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->max_acceleration()->data()->Get(2),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->max_acceleration()->data()->Get(3),
            std::numeric_limits<double>::infinity());

  EXPECT_EQ(limits->min_acceleration()->data()->size(), 4);
  EXPECT_EQ(limits->min_acceleration()->data()->Get(0),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->min_acceleration()->data()->Get(1),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->min_acceleration()->data()->Get(2),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->min_acceleration()->data()->Get(3),
            std::numeric_limits<double>::infinity());

  EXPECT_EQ(limits->max_jerk()->data()->size(), 4);
  EXPECT_EQ(limits->max_jerk()->data()->Get(0),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->max_jerk()->data()->Get(1),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->max_jerk()->data()->Get(2),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->max_jerk()->data()->Get(3),
            std::numeric_limits<double>::infinity());

  EXPECT_EQ(limits->min_jerk()->data()->size(), 4);
  EXPECT_EQ(limits->min_jerk()->data()->Get(0),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->min_jerk()->data()->Get(1),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->min_jerk()->data()->Get(2),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->min_jerk()->data()->Get(3),
            std::numeric_limits<double>::infinity());

  limits->mutable_min_jerk()->mutable_data()->Mutate(0, 10);
  EXPECT_EQ(limits->min_jerk()->data()->Get(0), 10);
}

// Test JointLimitsPVA
TEST(RoboticsTypesTest, ChecksJointLimitsPVABuffer) {
  auto buffer = intrinsic_fbs::CreateJointLimitsPVABuffer(4);
  auto limits =
      flatbuffers::GetMutableRoot<intrinsic_fbs::JointLimitsPVA>(buffer.data());

  EXPECT_EQ(limits->max_position()->data()->size(), 4);
  EXPECT_EQ(limits->max_position()->data()->Get(0), 0);
  EXPECT_EQ(limits->max_position()->data()->Get(1), 0);
  EXPECT_EQ(limits->max_position()->data()->Get(2), 0);
  EXPECT_EQ(limits->max_position()->data()->Get(3), 0);

  EXPECT_EQ(limits->min_position()->data()->size(), 4);
  EXPECT_EQ(limits->min_position()->data()->Get(0), 0);
  EXPECT_EQ(limits->min_position()->data()->Get(1), 0);
  EXPECT_EQ(limits->min_position()->data()->Get(2), 0);
  EXPECT_EQ(limits->min_position()->data()->Get(3), 0);

  EXPECT_EQ(limits->max_velocity()->data()->size(), 4);
  EXPECT_EQ(limits->max_velocity()->data()->Get(0), 0);
  EXPECT_EQ(limits->max_velocity()->data()->Get(1), 0);
  EXPECT_EQ(limits->max_velocity()->data()->Get(2), 0);
  EXPECT_EQ(limits->max_velocity()->data()->Get(3), 0);

  EXPECT_EQ(limits->min_velocity()->data()->size(), 4);
  EXPECT_EQ(limits->min_velocity()->data()->Get(0), 0);
  EXPECT_EQ(limits->min_velocity()->data()->Get(1), 0);
  EXPECT_EQ(limits->min_velocity()->data()->Get(2), 0);
  EXPECT_EQ(limits->min_velocity()->data()->Get(3), 0);

  EXPECT_EQ(limits->max_acceleration()->data()->size(), 4);
  EXPECT_EQ(limits->max_acceleration()->data()->Get(0), 0);
  EXPECT_EQ(limits->max_acceleration()->data()->Get(1), 0);
  EXPECT_EQ(limits->max_acceleration()->data()->Get(2), 0);
  EXPECT_EQ(limits->max_acceleration()->data()->Get(3), 0);

  EXPECT_EQ(limits->min_acceleration()->data()->size(), 4);
  EXPECT_EQ(limits->min_acceleration()->data()->Get(0), 0);
  EXPECT_EQ(limits->min_acceleration()->data()->Get(1), 0);
  EXPECT_EQ(limits->min_acceleration()->data()->Get(2), 0);
  EXPECT_EQ(limits->min_acceleration()->data()->Get(3), 0);
}

// Test JointLimitsPVA unlimited
TEST(RoboticsTypesTest, ChecksJointLimitsPVABufferUnlimited) {
  auto buffer = intrinsic_fbs::CreateJointLimitsPVABuffer(4, true);
  auto limits =
      flatbuffers::GetMutableRoot<intrinsic_fbs::JointLimitsPVA>(buffer.data());

  EXPECT_EQ(limits->max_position()->data()->size(), 4);
  EXPECT_EQ(limits->max_position()->data()->Get(0),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->max_position()->data()->Get(1),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->max_position()->data()->Get(2),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->max_position()->data()->Get(3),
            std::numeric_limits<double>::infinity());

  EXPECT_EQ(limits->min_position()->data()->size(), 4);
  EXPECT_EQ(limits->min_position()->data()->Get(0),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->min_position()->data()->Get(1),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->min_position()->data()->Get(2),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->min_position()->data()->Get(3),
            std::numeric_limits<double>::infinity());

  EXPECT_EQ(limits->max_velocity()->data()->size(), 4);
  EXPECT_EQ(limits->max_velocity()->data()->Get(0),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->max_velocity()->data()->Get(1),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->max_velocity()->data()->Get(2),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->max_velocity()->data()->Get(3),
            std::numeric_limits<double>::infinity());

  EXPECT_EQ(limits->min_velocity()->data()->size(), 4);
  EXPECT_EQ(limits->min_velocity()->data()->Get(0),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->min_velocity()->data()->Get(1),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->min_velocity()->data()->Get(2),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->min_velocity()->data()->Get(3),
            std::numeric_limits<double>::infinity());

  EXPECT_EQ(limits->max_acceleration()->data()->size(), 4);
  EXPECT_EQ(limits->max_acceleration()->data()->Get(0),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->max_acceleration()->data()->Get(1),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->max_acceleration()->data()->Get(2),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->max_acceleration()->data()->Get(3),
            std::numeric_limits<double>::infinity());

  EXPECT_EQ(limits->min_acceleration()->data()->size(), 4);
  EXPECT_EQ(limits->min_acceleration()->data()->Get(0),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->min_acceleration()->data()->Get(1),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->min_acceleration()->data()->Get(2),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->min_acceleration()->data()->Get(3),
            std::numeric_limits<double>::infinity());

  limits->mutable_min_acceleration()->mutable_data()->Mutate(0, 10);
  EXPECT_EQ(limits->min_acceleration()->data()->Get(0), 10);
}

// Test JointLimitsPAJ
TEST(RoboticsTypesTest, ChecksJointLimitsPAJBuffer) {
  auto buffer = intrinsic_fbs::CreateJointLimitsPAJBuffer(4);
  auto limits =
      flatbuffers::GetMutableRoot<intrinsic_fbs::JointLimitsPAJ>(buffer.data());

  EXPECT_EQ(limits->max_position()->data()->size(), 4);
  EXPECT_EQ(limits->max_position()->data()->Get(0), 0);
  EXPECT_EQ(limits->max_position()->data()->Get(1), 0);
  EXPECT_EQ(limits->max_position()->data()->Get(2), 0);
  EXPECT_EQ(limits->max_position()->data()->Get(3), 0);

  EXPECT_EQ(limits->min_position()->data()->size(), 4);
  EXPECT_EQ(limits->min_position()->data()->Get(0), 0);
  EXPECT_EQ(limits->min_position()->data()->Get(1), 0);
  EXPECT_EQ(limits->min_position()->data()->Get(2), 0);
  EXPECT_EQ(limits->min_position()->data()->Get(3), 0);

  EXPECT_EQ(limits->max_acceleration()->data()->size(), 4);
  EXPECT_EQ(limits->max_acceleration()->data()->Get(0), 0);
  EXPECT_EQ(limits->max_acceleration()->data()->Get(1), 0);
  EXPECT_EQ(limits->max_acceleration()->data()->Get(2), 0);
  EXPECT_EQ(limits->max_acceleration()->data()->Get(3), 0);

  EXPECT_EQ(limits->min_acceleration()->data()->size(), 4);
  EXPECT_EQ(limits->min_acceleration()->data()->Get(0), 0);
  EXPECT_EQ(limits->min_acceleration()->data()->Get(1), 0);
  EXPECT_EQ(limits->min_acceleration()->data()->Get(2), 0);
  EXPECT_EQ(limits->min_acceleration()->data()->Get(3), 0);

  EXPECT_EQ(limits->max_jerk()->data()->size(), 4);
  EXPECT_EQ(limits->max_jerk()->data()->Get(0), 0);
  EXPECT_EQ(limits->max_jerk()->data()->Get(1), 0);
  EXPECT_EQ(limits->max_jerk()->data()->Get(2), 0);
  EXPECT_EQ(limits->max_jerk()->data()->Get(3), 0);

  EXPECT_EQ(limits->min_jerk()->data()->size(), 4);
  EXPECT_EQ(limits->min_jerk()->data()->Get(0), 0);
  EXPECT_EQ(limits->min_jerk()->data()->Get(1), 0);
  EXPECT_EQ(limits->min_jerk()->data()->Get(2), 0);
  EXPECT_EQ(limits->min_jerk()->data()->Get(3), 0);

  limits->mutable_min_jerk()->mutable_data()->Mutate(0, 10);
  EXPECT_EQ(limits->min_jerk()->data()->Get(0), 10);
}

// Test JointLimitsPAJ unlimited
TEST(RoboticsTypesTest, ChecksJointLimitsPAJBufferUnlimited) {
  auto buffer = intrinsic_fbs::CreateJointLimitsPAJBuffer(4, true);
  auto limits =
      flatbuffers::GetMutableRoot<intrinsic_fbs::JointLimitsPAJ>(buffer.data());

  EXPECT_EQ(limits->max_position()->data()->size(), 4);
  EXPECT_EQ(limits->max_position()->data()->Get(0),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->max_position()->data()->Get(1),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->max_position()->data()->Get(2),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->max_position()->data()->Get(3),
            std::numeric_limits<double>::infinity());

  EXPECT_EQ(limits->min_position()->data()->size(), 4);
  EXPECT_EQ(limits->min_position()->data()->Get(0),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->min_position()->data()->Get(1),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->min_position()->data()->Get(2),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->min_position()->data()->Get(3),
            std::numeric_limits<double>::infinity());

  EXPECT_EQ(limits->max_acceleration()->data()->size(), 4);
  EXPECT_EQ(limits->max_acceleration()->data()->Get(0),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->max_acceleration()->data()->Get(1),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->max_acceleration()->data()->Get(2),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->max_acceleration()->data()->Get(3),
            std::numeric_limits<double>::infinity());

  EXPECT_EQ(limits->min_acceleration()->data()->size(), 4);
  EXPECT_EQ(limits->min_acceleration()->data()->Get(0),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->min_acceleration()->data()->Get(1),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->min_acceleration()->data()->Get(2),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->min_acceleration()->data()->Get(3),
            std::numeric_limits<double>::infinity());

  EXPECT_EQ(limits->max_jerk()->data()->size(), 4);
  EXPECT_EQ(limits->max_jerk()->data()->Get(0),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->max_jerk()->data()->Get(1),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->max_jerk()->data()->Get(2),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->max_jerk()->data()->Get(3),
            std::numeric_limits<double>::infinity());

  EXPECT_EQ(limits->min_jerk()->data()->size(), 4);
  EXPECT_EQ(limits->min_jerk()->data()->Get(0),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->min_jerk()->data()->Get(1),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->min_jerk()->data()->Get(2),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->min_jerk()->data()->Get(3),
            std::numeric_limits<double>::infinity());

  limits->mutable_min_jerk()->mutable_data()->Mutate(0, 10);
  EXPECT_EQ(limits->min_jerk()->data()->Get(0), 10);
}

// Test JointLimitsP
TEST(RoboticsTypesTest, ChecksJointLimitsPBuffer) {
  auto buffer = intrinsic_fbs::CreateJointLimitsPAJBuffer(4);
  auto limits =
      flatbuffers::GetMutableRoot<intrinsic_fbs::JointLimitsPAJ>(buffer.data());

  EXPECT_EQ(limits->max_position()->data()->size(), 4);
  EXPECT_EQ(limits->max_position()->data()->Get(0), 0);
  EXPECT_EQ(limits->max_position()->data()->Get(1), 0);
  EXPECT_EQ(limits->max_position()->data()->Get(2), 0);
  EXPECT_EQ(limits->max_position()->data()->Get(3), 0);

  EXPECT_EQ(limits->min_position()->data()->size(), 4);
  EXPECT_EQ(limits->min_position()->data()->Get(0), 0);
  EXPECT_EQ(limits->min_position()->data()->Get(1), 0);
  EXPECT_EQ(limits->min_position()->data()->Get(2), 0);
  EXPECT_EQ(limits->min_position()->data()->Get(3), 0);

  limits->mutable_min_position()->mutable_data()->Mutate(0, 10);
  EXPECT_EQ(limits->min_position()->data()->Get(0), 10);
}

// Test JointLimitsP unlimited
TEST(RoboticsTypesTest, ChecksJointLimitsPBufferUnlimited) {
  auto buffer = intrinsic_fbs::CreateJointLimitsPAJBuffer(4, true);
  auto limits =
      flatbuffers::GetMutableRoot<intrinsic_fbs::JointLimitsPAJ>(buffer.data());

  EXPECT_EQ(limits->max_position()->data()->size(), 4);
  EXPECT_EQ(limits->max_position()->data()->Get(0),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->max_position()->data()->Get(1),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->max_position()->data()->Get(2),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->max_position()->data()->Get(3),
            std::numeric_limits<double>::infinity());

  EXPECT_EQ(limits->min_position()->data()->size(), 4);
  EXPECT_EQ(limits->min_position()->data()->Get(0),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->min_position()->data()->Get(1),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->min_position()->data()->Get(2),
            std::numeric_limits<double>::infinity());
  EXPECT_EQ(limits->min_position()->data()->Get(3),
            std::numeric_limits<double>::infinity());

  limits->mutable_min_position()->mutable_data()->Mutate(0, 10);
  EXPECT_EQ(limits->min_position()->data()->Get(0), 10);
}

}  // namespace
}  // namespace intrinsic
