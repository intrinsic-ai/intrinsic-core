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

#include "intrinsic/icon/control/c_api/external_action_api/icon_realtime_slot_map.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstddef>
#include <functional>

#include "absl/status/status_matchers.h"
#include "internal/testing.h"
#include "intrinsic/icon/control/c_api/c_realtime_slot_map.h"
#include "intrinsic/icon/control/c_api/external_action_api/icon_feature_interfaces.h"
#include "intrinsic/icon/control/c_api/wrappers/feature_interface_wrappers.h"
#include "intrinsic/icon/control/c_api/wrappers/realtime_slot_map_wrapper.h"
#include "intrinsic/icon/control/fake_realtime_slot_map_impl.h"
#include "intrinsic/icon/control/parts/fake_feature_interfaces.h"
#include "intrinsic/icon/control/parts/feature_interface_registry.h"
#include "intrinsic/icon/control/realtime_slot_map.h"
#include "intrinsic/icon/control/slot_types.h"
#include "intrinsic/icon/testing/malloc_test.h"
#include "intrinsic/kinematics/types/joint_limits.h"
#include "intrinsic/kinematics/types/joint_state.h"
#include "intrinsic/math/pose3.h"

namespace intrinsic::icon {
namespace {

constexpr size_t kNdof = 3;
constexpr RealtimeSlotId kSlotId(0);

class IconRealtimeSlotMapTest : public ::testing::Test {
 public:
  IconRealtimeSlotMapTest()
      : jpos_(kNdof),
        jsense_(kNdof),
        jvel_(kNdof),
        jacc_(kNdof),
        jlimits_(JointLimits::Unlimited(kNdof).value()),
        range_finder_(0, Pose3d()),
        manipulator_kinematics_(kNdof),
        fake_slot_map_impl_({{kSlotId, std::ref(registry_)}}),
        realtime_slot_map_(fake_slot_map_impl_),
        wrapped_realtime_slot_map_(Wrap(&realtime_slot_map_)),
        slot_map_(wrapped_realtime_slot_map_, GetRealtimeSlotMapVtable(),
                  GetFeatureInterfaceVtable()),
        const_slot_map_(wrapped_realtime_slot_map_, GetRealtimeSlotMapVtable(),
                        GetFeatureInterfaceVtable()) {}

  void SetUp() override {
    EXPECT_OK(registry_.RegisterAsCompatibleInterfaces(&jpos_));
    EXPECT_OK(registry_.RegisterAsCompatibleInterfaces(&jsense_));
    EXPECT_OK(registry_.RegisterAsCompatibleInterfaces(&jvel_));
    EXPECT_OK(registry_.RegisterAsCompatibleInterfaces(&jacc_));
    EXPECT_OK(registry_.RegisterAsCompatibleInterfaces(&jlimits_));
    EXPECT_OK(registry_.RegisterAsCompatibleInterfaces(&range_finder_));
    EXPECT_OK(
        registry_.RegisterAsCompatibleInterfaces(&manipulator_kinematics_));
  }

 protected:
  FeatureInterfaceRegistry registry_;

 private:
  FakeJointPosition jpos_;
  FakeJointPositionSensor jsense_;
  FakeJointVelocityEstimator jvel_;
  FakeJointAccelerationEstimator jacc_;
  FakeJointLimits jlimits_;
  FakeRangeFinder range_finder_;
  FakeManipulatorKinematics manipulator_kinematics_;

  // We set this up so it returns a FeatureInterfaceRegistry *only* for kSlotId.
  FakeRealtimeSlotMapImpl fake_slot_map_impl_;
  // Holds a reference to the mock above.
  RealtimeSlotMap realtime_slot_map_;

 protected:
  IntrinsicIconRealtimeSlotMap* const wrapped_realtime_slot_map_;
  IconRealtimeSlotMap slot_map_;
  IconConstRealtimeSlotMap const_slot_map_;
};

TEST_F(IconRealtimeSlotMapTest, Create) {}

TEST_F(IconRealtimeSlotMapTest, CreateDoesNotMalloc) {
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  IconRealtimeSlotMap slot_map(wrapped_realtime_slot_map_,
                               GetRealtimeSlotMapVtable(),
                               GetFeatureInterfaceVtable());
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
}

TEST_F(IconRealtimeSlotMapTest, CreateConstDoesNotMalloc) {
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  IconConstRealtimeSlotMap slot_map(wrapped_realtime_slot_map_,
                                    GetRealtimeSlotMapVtable(),
                                    GetFeatureInterfaceVtable());
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
}

TEST_F(IconRealtimeSlotMapTest, NoConstFeatureInterfacesForUnknownSlot) {
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  const intrinsic::icon::IconConstFeatureInterfaces const_feature_interfaces =
      slot_map_.FeatureInterfacesForSlot(RealtimeSlotId(kSlotId.value() + 1));
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();

  EXPECT_FALSE(const_feature_interfaces.joint_position.has_value());
  EXPECT_FALSE(const_feature_interfaces.joint_position_sensor.has_value());
  EXPECT_FALSE(const_feature_interfaces.joint_velocity_estimator.has_value());
  EXPECT_FALSE(const_feature_interfaces.joint_limits.has_value());
  EXPECT_FALSE(const_feature_interfaces.manipulator_kinematics.has_value());
}

TEST_F(IconRealtimeSlotMapTest,
       NoConstFeatureInterfacesForUnknownSlotFromConstMap) {
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  const intrinsic::icon::IconConstFeatureInterfaces const_feature_interfaces =
      const_slot_map_.FeatureInterfacesForSlot(
          RealtimeSlotId(kSlotId.value() + 1));
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();

  EXPECT_FALSE(const_feature_interfaces.joint_position.has_value());
  EXPECT_FALSE(const_feature_interfaces.joint_position_sensor.has_value());
  EXPECT_FALSE(const_feature_interfaces.joint_velocity_estimator.has_value());
  EXPECT_FALSE(const_feature_interfaces.joint_limits.has_value());
  EXPECT_FALSE(const_feature_interfaces.manipulator_kinematics.has_value());
}

TEST_F(IconRealtimeSlotMapTest, NoMutableFeatureInterfacesForUnknownSlot) {
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  intrinsic::icon::IconFeatureInterfaces feature_interfaces =
      slot_map_.MutableFeatureInterfacesForSlot(
          RealtimeSlotId(kSlotId.value() + 1));
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();

  EXPECT_FALSE(feature_interfaces.joint_position.has_value());
  EXPECT_FALSE(feature_interfaces.joint_position_sensor.has_value());
  EXPECT_FALSE(feature_interfaces.joint_velocity_estimator.has_value());
  EXPECT_FALSE(feature_interfaces.joint_limits.has_value());
  EXPECT_FALSE(feature_interfaces.manipulator_kinematics.has_value());
}

TEST_F(IconRealtimeSlotMapTest, FeatureInterfacesForSlotSucceeds) {
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  const intrinsic::icon::IconConstFeatureInterfaces const_feature_interfaces =
      slot_map_.FeatureInterfacesForSlot(kSlotId);
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();

  // Ensure that all expected FeatureInterfaces are present and we can call
  // them.
  //
  // No malloc checks here – we assert that property of the FeatureInterface
  // wrappers in *their* test.
  ASSERT_TRUE(const_feature_interfaces.joint_position.has_value());
  EXPECT_EQ(const_feature_interfaces.joint_position->PreviousPositionSetpoints()
                .position()
                .size(),
            kNdof);
  ASSERT_TRUE(const_feature_interfaces.joint_position_sensor.has_value());
  EXPECT_EQ(const_feature_interfaces.joint_position_sensor->GetSensedPosition()
                .position.size(),
            kNdof);
  ASSERT_TRUE(const_feature_interfaces.joint_velocity_estimator.has_value());
  EXPECT_EQ(
      const_feature_interfaces.joint_velocity_estimator->GetVelocityEstimate()
          .velocity.size(),
      kNdof);
  ASSERT_TRUE(const_feature_interfaces.joint_limits.has_value());
  EXPECT_EQ(
      const_feature_interfaces.joint_limits->GetApplicationLimits().size(),
      kNdof);
  ASSERT_TRUE(const_feature_interfaces.manipulator_kinematics.has_value());
  EXPECT_OK(const_feature_interfaces.manipulator_kinematics
                ->ComputeChainJacobian(JointStateP::Zero(kNdof))
                .status());
}

TEST_F(IconRealtimeSlotMapTest, MutableFeatureInterfacesForSlotSucceeds) {
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  intrinsic::icon::IconFeatureInterfaces feature_interfaces =
      slot_map_.MutableFeatureInterfacesForSlot(kSlotId);
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();

  EXPECT_TRUE(feature_interfaces.joint_position.has_value());
  EXPECT_EQ(feature_interfaces.joint_position->PreviousPositionSetpoints()
                .position()
                .size(),
            kNdof);

  // Ensure that all expected FeatureInterfaces are present and we can call
  // them.
  //
  // No malloc checks here – we assert that property of the FeatureInterface
  // wrappers in *their* test.
  ASSERT_TRUE(feature_interfaces.joint_position.has_value());
  EXPECT_EQ(feature_interfaces.joint_position->PreviousPositionSetpoints()
                .position()
                .size(),
            kNdof);
  ASSERT_TRUE(feature_interfaces.joint_position_sensor.has_value());
  EXPECT_EQ(feature_interfaces.joint_position_sensor->GetSensedPosition()
                .position.size(),
            kNdof);
  ASSERT_TRUE(feature_interfaces.joint_velocity_estimator.has_value());
  EXPECT_EQ(feature_interfaces.joint_velocity_estimator->GetVelocityEstimate()
                .velocity.size(),
            kNdof);
  ASSERT_TRUE(feature_interfaces.joint_limits.has_value());
  EXPECT_EQ(feature_interfaces.joint_limits->GetApplicationLimits().size(),
            kNdof);
  ASSERT_TRUE(feature_interfaces.manipulator_kinematics.has_value());
  EXPECT_OK(feature_interfaces.manipulator_kinematics
                ->ComputeChainJacobian(JointStateP::Zero(kNdof))
                .status());
}

}  // namespace
}  // namespace intrinsic::icon
