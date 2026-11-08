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

#include "intrinsic/icon/control/c_api/external_action_api/testing/icon_slot_map_fake.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <optional>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "internal/testing.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/control/c_api/external_action_api/icon_feature_interfaces.h"
#include "intrinsic/icon/control/c_api/external_action_api/icon_realtime_slot_map.h"
#include "intrinsic/icon/control/slot_types.h"
#include "intrinsic/icon/utils/realtime_status_matchers.h"
#include "intrinsic/kinematics/types/joint_limits.h"
#include "intrinsic/kinematics/types/joint_state.h"
#include "intrinsic/math/twist.h"
#include "protobuf-matchers/protocol-buffer-matchers.h"

namespace intrinsic::icon {
namespace {

using ::absl_testing::StatusIs;
using ::protobuf_matchers::EqualsProto;
using ::testing::IsNull;
using ::testing::NotNull;

TEST(IconSlotMapFake, ReturnsNullForInvalidPart) {
  IconSlotMapFake slot_map;
  EXPECT_THAT(slot_map.GetFakeArmForSlot("void_arm"), IsNull());
  EXPECT_THAT(((const IconSlotMapFake&)slot_map).GetFakeArmForSlot("void_arm"),
              IsNull());
  EXPECT_THAT(slot_map.GetSlotInfoForSlot("void_arm"),
              StatusIs(absl::StatusCode::kNotFound));
  EXPECT_THAT(slot_map.GetIdForSlot("void_arm"),
              StatusIs(absl::StatusCode::kNotFound));
}

TEST(IconSlotMapFake, AddPartSucceeds) {
  IconSlotMapFake slot_map;
  ASSERT_OK(slot_map.AddLoopbackFakeArmSlot("arm_slot"));
  EXPECT_THAT(slot_map.GetFakeArmForSlot("arm_slot"), NotNull());
  EXPECT_THAT(((const IconSlotMapFake&)slot_map).GetFakeArmForSlot("arm_slot"),
              NotNull());
  EXPECT_OK(slot_map.GetSlotInfoForSlot("arm_slot"));

  // Can call methods on the fake arm
  EXPECT_TRUE(slot_map.GetFakeArmForSlot("arm_slot")
                  ->PreviousPositionSetpoints()
                  .position()
                  .isZero());
}

TEST(IconSlotMapFake, AddDuplicatePartFails) {
  IconSlotMapFake slot_map;
  EXPECT_OK(slot_map.AddLoopbackFakeArmSlot("arm_slot"));
  EXPECT_THAT(slot_map.AddLoopbackFakeArmSlot("arm_slot"),
              StatusIs(absl::StatusCode::kAlreadyExists));
}

TEST(IconSlotMapFake, MakeIconConstRealtimeSlotMap) {
  IconSlotMapFake fake;
  EXPECT_OK(fake.AddLoopbackFakeArmSlot("arm_slot"));
  ASSERT_OK_AND_ASSIGN(RealtimeSlotId id, fake.GetIdForSlot("arm_slot"));
  IconConstRealtimeSlotMap slot_map = fake.MakeIconConstRealtimeSlotMap();
  EXPECT_EQ(slot_map.FeatureInterfacesForSlot(RealtimeSlotId(id.value() + 1))
                .joint_position,
            std::nullopt);
  IconConstFeatureInterfaces feature_interfaces =
      slot_map.FeatureInterfacesForSlot(id);
  // All expected feature interfaces are present.
  ASSERT_TRUE(feature_interfaces.joint_position.has_value());
  ASSERT_TRUE(feature_interfaces.joint_position_sensor.has_value());
  ASSERT_TRUE(feature_interfaces.joint_velocity_estimator.has_value());
  ASSERT_TRUE(feature_interfaces.joint_limits.has_value());
  ASSERT_TRUE(feature_interfaces.manipulator_kinematics.has_value());
  ASSERT_TRUE(feature_interfaces.force_torque_sensor.has_value());
  // Can call methods on the FeatureInterfaces
  EXPECT_TRUE(feature_interfaces.joint_position->PreviousPositionSetpoints()
                  .position()
                  .isZero());
  EXPECT_TRUE(feature_interfaces.joint_position_sensor->GetSensedPosition()
                  .position.isZero());
  EXPECT_TRUE(feature_interfaces.joint_velocity_estimator->GetVelocityEstimate()
                  .velocity.isZero());
  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(JointLimits unlimited,
                                    JointLimits::Unlimited(6));
  EXPECT_THAT(ToProto(feature_interfaces.joint_limits->GetApplicationLimits()),
              EqualsProto(ToProto(unlimited)));
  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
      eigenmath::Matrix6Nd jacobian,
      feature_interfaces.manipulator_kinematics->ComputeChainJacobian(
          JointStateP::Zero(6)));
  EXPECT_TRUE(jacobian.isIdentity());
  EXPECT_EQ(feature_interfaces.force_torque_sensor->WrenchAtTip(),
            Wrench::ZERO);
}

TEST(IconSlotMapFake, MakeIconRealtimeSlotMap) {
  IconSlotMapFake fake;
  EXPECT_OK(fake.AddLoopbackFakeArmSlot("arm_slot"));
  ASSERT_OK_AND_ASSIGN(RealtimeSlotId id, fake.GetIdForSlot("arm_slot"));
  IconRealtimeSlotMap slot_map = fake.MakeIconRealtimeSlotMap();
  EXPECT_EQ(
      slot_map.MutableFeatureInterfacesForSlot(RealtimeSlotId(id.value() + 1))
          .joint_position,
      std::nullopt);
  IconFeatureInterfaces feature_interfaces =
      slot_map.MutableFeatureInterfacesForSlot(id);
  // All expected feature interfaces are present.
  ASSERT_TRUE(feature_interfaces.joint_position.has_value());
  ASSERT_TRUE(feature_interfaces.joint_position_sensor.has_value());
  ASSERT_TRUE(feature_interfaces.joint_velocity_estimator.has_value());
  ASSERT_TRUE(feature_interfaces.joint_limits.has_value());
  ASSERT_TRUE(feature_interfaces.manipulator_kinematics.has_value());
  ASSERT_TRUE(feature_interfaces.force_torque_sensor.has_value());
  // Can call methods on the FeatureInterfaces
  EXPECT_TRUE(feature_interfaces.joint_position->PreviousPositionSetpoints()
                  .position()
                  .isZero());
  EXPECT_TRUE(feature_interfaces.joint_position_sensor->GetSensedPosition()
                  .position.isZero());
  EXPECT_TRUE(feature_interfaces.joint_velocity_estimator->GetVelocityEstimate()
                  .velocity.isZero());
  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(JointLimits unlimited,
                                    JointLimits::Unlimited(6));
  EXPECT_THAT(ToProto(feature_interfaces.joint_limits->GetApplicationLimits()),
              EqualsProto(ToProto(unlimited)));
  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
      eigenmath::Matrix6Nd jacobian,
      feature_interfaces.manipulator_kinematics->ComputeChainJacobian(
          JointStateP::Zero(6)));
  EXPECT_TRUE(jacobian.isIdentity());
  EXPECT_EQ(feature_interfaces.force_torque_sensor->WrenchAtTip(),
            Wrench::ZERO);
  EXPECT_OK(feature_interfaces.force_torque_sensor->Tare());
}

}  // namespace
}  // namespace intrinsic::icon
