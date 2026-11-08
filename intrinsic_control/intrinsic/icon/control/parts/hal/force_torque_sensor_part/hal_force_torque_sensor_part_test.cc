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

#include "intrinsic/icon/control/parts/hal/force_torque_sensor_part/hal_force_torque_sensor_part.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "internal/testing.h"
#include "intrinsic/eigenmath/matchers.h"
#include "intrinsic/eigenmath/pose3_utils.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/control/context.h"
#include "intrinsic/icon/control/parts/feature_interfaces.h"
#include "intrinsic/icon/control/parts/hal/force_torque_sensor_part/hal_force_torque_sensor_part_config.pb.h"
#include "intrinsic/icon/control/parts/hal/force_torque_sensor_part/hal_force_torque_sensor_part_test_fixture.h"
#include "intrinsic/icon/control/parts/part_test_helper.h"
#include "intrinsic/icon/control/realtime_operational_status.h"
#include "intrinsic/icon/hal/interfaces/force_sensor.fbs.h"
#include "intrinsic/icon/hal/interfaces/force_torque.fbs.h"
#include "intrinsic/icon/utils/constants.h"
#include "intrinsic/icon/utils/realtime_status_matchers.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/util/thread/rt_thread.h"
#include "intrinsic/util/thread/thread.h"
#include "intrinsic/util/thread/thread_options.h"

namespace intrinsic::icon {
namespace {

using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;
using intrinsic::eigenmath::testing::IsApprox;
using ::testing::Field;
using ::testing::HasSubstr;

constexpr char kSecondRobotCollectionName[] = "robot_2";

constexpr absl::string_view k2DofRobotNoClockConfig = R"pb(
  name: "fake_module"
  drives_realtime_clock: false
  module_config {
    [type.googleapis.com/intrinsic_proto.icon.FakeModuleConfig] {
      # 250Hz
      cycle_time { seconds: 0 nanos: 4000000 }
      arm_interfaces {
        joint_position_state_name: "joint_position_state"
        joint_velocity_state_name: "joint_velocity_state"
        joint_acceleration_state_name: "joint_acceleration_state"
        initial_joint_state {
          position_sensed: 0.0
          velocity_sensed: 0.0
          acceleration_sensed: 0.0
        }
        initial_joint_state {
          position_sensed: 0.0
          velocity_sensed: 0.0
          acceleration_sensed: 0.0
        }
      }
    }
  }
)pb";

TEST_F(HalForceTorqueSensorPartTest, CreateWorks) {
  ASSERT_OK_AND_ASSIGN(auto default_world, MakeDefaultRobotWorld());
  Init(std::move(default_world));

  EXPECT_OK(PartTestHelper::CreateFromProtoFactory(
      {.part_name = kHalForceTorqueSensorPartName,
       .context = GetContext(),
       .control_frequency_hz = kControlFrequencyHz},
      HalForceTorqueSensorPart::kPartTypeName,
      CreateDefaultHalFTSensorPartConfig()));
}

TEST_F(HalForceTorqueSensorPartTest, OperationalStateFromHwmState) {
  ASSERT_OK_AND_ASSIGN(auto default_world, MakeDefaultRobotWorld());
  Init(std::move(default_world));
  ASSERT_OK_AND_ASSIGN(helper_with_part_,
                       PartTestHelper::CreateFromProtoFactory(
                           {.part_name = kHalForceTorqueSensorPartName,
                            .context = GetContext(),
                            .control_frequency_hz = kControlFrequencyHz},
                           HalForceTorqueSensorPart::kPartTypeName,
                           CreateDefaultHalFTSensorPartConfig()));

  EXPECT_THAT(
      helper_with_part_->part().GetOperationalStatus(),
      RealtimeIsOkAndHolds(Field("state", &RealtimeOperationalStatus::state,
                                 RealtimeOperationalState::kDisabled)));

  auto deadline = absl::Now() + absl::Seconds(15);
  EXPECT_OK(hwm_manager_.Prepare(deadline));
  EXPECT_OK(hwm_manager_.Activate(deadline));
  EXPECT_THAT(
      helper_with_part_->part().GetOperationalStatus(),
      RealtimeIsOkAndHolds(Field("state", &RealtimeOperationalStatus::state,
                                 RealtimeOperationalState::kDisabled)));
  {
    ft_module_ptr_->SetFailingTransitions(true);
    absl::Notification enabled_notification;
    Thread thread;
    ASSERT_OK_AND_ASSIGN(
        thread,
        CreateRealtimeCapableThread(ThreadOptions().SetName("HwmLoop"), [&] {
          while (!enabled_notification.HasBeenNotified()) {
            EXPECT_OK(hwm_manager_.Read(deadline));
            EXPECT_OK(hwm_manager_.UpdateState());
          }
        }));
    // EnableMotion should fail because we inserted a failing transition.
    EXPECT_THAT(hwm_manager_.EnableMotionTestOnly(deadline),
                StatusIs(absl::StatusCode::kUnavailable));
    enabled_notification.Notify();
    thread.join();
    EXPECT_OK(hwm_manager_.UpdateState());
    EXPECT_THAT(helper_with_part_->part().GetOperationalStatus(),
                RealtimeIsOkAndHolds(
                    Field("state", &RealtimeOperationalStatus::state,
                          RealtimeOperationalState::kFaultedConnected)));
  }
  ft_module_ptr_->SetFailingTransitions(false);
  absl::Notification enabled_notification;
  Thread thread;
  ASSERT_OK_AND_ASSIGN(
      thread,
      CreateRealtimeCapableThread(ThreadOptions().SetName("HwmLoop"), [&] {
        while (!enabled_notification.HasBeenNotified()) {
          EXPECT_OK(hwm_manager_.Read(deadline));
          EXPECT_OK(hwm_manager_.UpdateState());
        }
      }));
  EXPECT_OK(hwm_manager_.ClearFaultsTestOnly(deadline));
  EXPECT_OK(hwm_manager_.EnableMotionTestOnly(deadline));
  enabled_notification.Notify();
  thread.join();
  EXPECT_OK(hwm_manager_.UpdateState());

  EXPECT_THAT(
      helper_with_part_->part().GetOperationalStatus(),
      RealtimeIsOkAndHolds(Field("state", &RealtimeOperationalStatus::state,
                                 RealtimeOperationalState::kEnabled)));
}

TEST_F(HalForceTorqueSensorPartTest, FailsIfDofOfHWMDontMatchKinematics) {
  World test_world = World::CreateEmptyWorld();
  {
    ASSERT_OK_AND_ASSIGN(
        auto robot_data,
        CreateLinearChainRobot(
            &test_world,
            LinearChainRobotParams{
                .robot_name = kRobotCollectionName,
                .robot_alias = kRobotCollectionName,
                .link_params =
                    {
                        {.name = "base_link"},
                        {.name = "link_mast_pan"},
                        {.name = kTargetLinkName},
                        {.name = kForceTorqueSensorLinkName},
                    },
                .joint_params =
                    {
                        {
                            .name = "jaco_joint_mast_pan",
                            .parent_t_inboard =
                                Pose3d(eigenmath::Vector3d{0, 0, 1.0}),
                            .axis = eigenmath::Vector3d::UnitZ(),
                            .lower_value_limit = -3.7,
                            .upper_value_limit = 2.15,
                        },
                        {
                            .name = "jaco_joint_mast_wrist",
                            .parent_t_inboard =
                                Pose3d(eigenmath::Vector3d{1.0, 0, 0}),
                            .axis = eigenmath::Vector3d::UnitX(),
                        },
                        {
                            .name = "ft_sensor_fixed_joint",
                            .parent_t_inboard =
                                Pose3d(eigenmath::Vector3d{0.1, 0, 0}),
                            .motion_type = intrinsic_proto::world::
                                KinematicsComponent::MOTION_TYPE_FIXED,
                        },
                    },
                .add_solver_key = true,
                .resource_name = kRobotCollectionName,
            }));
    ASSERT_OK_AND_ASSIGN(WorldEntity * robot_entity,
                         test_world.GetEntityById(robot_data.robot_id));
    ASSERT_OK_AND_ASSIGN(PPRComponent * ppr_component,
                         robot_entity->GetOrCreateComponent<PPRComponent>());
    ppr_component->SetResourceName(kRobotCollectionName);
  }
  Init(std::move(test_world));

  EXPECT_THAT(PartTestHelper::CreateFromProtoFactory(
                  {.part_name = kHalForceTorqueSensorPartName,
                   .context = GetContext(),
                   .control_frequency_hz = kControlFrequencyHz},
                  HalForceTorqueSensorPart::kPartTypeName,
                  CreateDefaultHalFTSensorPartConfig()),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       HasSubstr("does not have enough DoFs")));
}

TEST_F(HalForceTorqueSensorPartTest,
       FailsIfMoveableLinkBetweenSensorAndTarget) {
  ASSERT_OK_AND_ASSIGN(auto default_world,
                       MakeDefaultRobotWorld(/*moveable_sensor_link=*/true));
  Init(std::move(default_world), k2DofRobotNoClockConfig);

  intrinsic_proto::icon::HalForceTorqueSensorPartConfig ft_sensor_part_config =
      CreateDefaultHalFTSensorPartConfig();
  ft_sensor_part_config.set_target_link_name("link_mast_tilt");
  EXPECT_THAT(
      PartTestHelper::CreateFromProtoFactory(
          {.part_name = kHalForceTorqueSensorPartName,
           .context = GetContext(),
           .control_frequency_hz = kControlFrequencyHz},
          HalForceTorqueSensorPart::kPartTypeName, ft_sensor_part_config),
      StatusIs(absl::StatusCode::kFailedPrecondition,
               HasSubstr("movable joint")));
}

TEST_F(HalForceTorqueSensorPartTest, FailsForInexistentTargetName) {
  intrinsic_proto::icon::HalForceTorqueSensorPartConfig ft_sensor_part_config =
      CreateDefaultHalFTSensorPartConfig();
  ft_sensor_part_config.set_target_link_name("the_wrong_target");

  ASSERT_OK_AND_ASSIGN(auto default_world, MakeDefaultRobotWorld());
  Init(std::move(default_world));

  EXPECT_THAT(
      PartTestHelper::CreateFromProtoFactory(
          {.part_name = kHalForceTorqueSensorPartName,
           .context = GetContext(),
           .control_frequency_hz = kControlFrequencyHz},
          HalForceTorqueSensorPart::kPartTypeName, ft_sensor_part_config),
      StatusIs(absl::StatusCode::kFailedPrecondition,
               HasSubstr("target link")));
}

TEST_F(HalForceTorqueSensorPartTest,
       SucceedsWithNoRobotCollectionNameIfSingleRobot) {
  intrinsic_proto::icon::HalForceTorqueSensorPartConfig ft_sensor_part_config =
      CreateDefaultHalFTSensorPartConfig();
  ft_sensor_part_config.clear_world_robot_collection_name();

  ASSERT_OK_AND_ASSIGN(auto default_world, MakeDefaultRobotWorld());
  Init(std::move(default_world));

  EXPECT_OK(PartTestHelper::CreateFromProtoFactory(
      {.part_name = kHalForceTorqueSensorPartName,
       .context = GetContext(),
       .control_frequency_hz = kControlFrequencyHz},
      HalForceTorqueSensorPart::kPartTypeName, ft_sensor_part_config));
}

TEST_F(HalForceTorqueSensorPartTest,
       FailsIfMultipleRobotsAndNoRobotCollectionName) {
  intrinsic_proto::icon::HalForceTorqueSensorPartConfig ft_sensor_part_config =
      CreateDefaultHalFTSensorPartConfig();
  ft_sensor_part_config.clear_world_robot_collection_name();
  ASSERT_OK_AND_ASSIGN(auto world, MakeDefaultRobotWorld());
  ASSERT_OK_AND_ASSIGN(
      auto robot_data,
      object_world::Create3DofRobotObject(
          &world, {
                      .name = WorldObjectName(kSecondRobotCollectionName),
                      .resource_name = kSecondRobotCollectionName,
                      .create_flange_frame = true,
                  }));
  Init(std::move(world));

  EXPECT_THAT(
      PartTestHelper::CreateFromProtoFactory(
          {.part_name = kHalForceTorqueSensorPartName,
           .context = GetContext(),
           .control_frequency_hz = kControlFrequencyHz},
          HalForceTorqueSensorPart::kPartTypeName, ft_sensor_part_config),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("multiple robots in the World")));
}

TEST_F(HalForceTorqueSensorPartTest,
       SucceedsIfMultipleRobotsAndCorrectRobotCollectionName) {
  ASSERT_OK_AND_ASSIGN(auto world, MakeDefaultRobotWorld());
  ASSERT_OK_AND_ASSIGN(
      auto robot_data,
      object_world::Create3DofRobotObject(
          &world, {
                      .name = WorldObjectName(kSecondRobotCollectionName),
                      .resource_name = kSecondRobotCollectionName,
                      .create_flange_frame = true,
                  }));
  Init(std::move(world));

  EXPECT_OK(PartTestHelper::CreateFromProtoFactory(
      {.part_name = kHalForceTorqueSensorPartName,
       .context = GetContext(),
       .control_frequency_hz = kControlFrequencyHz},
      HalForceTorqueSensorPart::kPartTypeName,
      CreateDefaultHalFTSensorPartConfig()));
}

TEST_F(HalForceTorqueSensorPartTest,
       SucceedsWithCorrectRobotCollectionNameInProtoWithUnrelatedResourceId) {
  intrinsic_proto::icon::HalForceTorqueSensorPartConfig ft_sensor_part_config =
      CreateDefaultHalFTSensorPartConfig();

  ASSERT_OK_AND_ASSIGN(auto default_world, MakeDefaultRobotWorld());
  Init(std::move(default_world));

  EXPECT_OK(PartTestHelper::CreateFromProtoFactory(
      {.part_name = kHalForceTorqueSensorPartName,
       .context = GetContext(),
       .control_frequency_hz = kControlFrequencyHz,
       .hardware_resource_name = "unrelated_resource_name"},
      HalForceTorqueSensorPart::kPartTypeName, ft_sensor_part_config));
}

TEST_F(HalForceTorqueSensorPartTest, FailsForWrongRobotCollection) {
  intrinsic_proto::icon::HalForceTorqueSensorPartConfig ft_sensor_part_config =
      CreateDefaultHalFTSensorPartConfig();
  ft_sensor_part_config.set_world_robot_collection_name(
      "wrong_robot_collection");

  ASSERT_OK_AND_ASSIGN(auto default_world, MakeDefaultRobotWorld());
  Init(std::move(default_world));

  EXPECT_THAT(
      PartTestHelper::CreateFromProtoFactory(
          {.part_name = kHalForceTorqueSensorPartName,
           .context = GetContext(),
           .control_frequency_hz = kControlFrequencyHz},
          HalForceTorqueSensorPart::kPartTypeName, ft_sensor_part_config),
      StatusIs(absl::StatusCode::kNotFound,
               HasSubstr("wrong_robot_collection")));
}

TEST_F(HalForceTorqueSensorPartTest, FailsForMissingInterface) {
  intrinsic_proto::icon::HalForceTorqueSensorPartConfig ft_sensor_part_config =
      CreateDefaultHalFTSensorPartConfig();
  ft_sensor_part_config.mutable_force_torque_state()->set_interface_name(
      "completely_irrelevant_name");

  ASSERT_OK_AND_ASSIGN(auto default_world, MakeDefaultRobotWorld());
  Init(std::move(default_world));

  EXPECT_THAT(
      PartTestHelper::CreateFromProtoFactory(
          {.part_name = kHalForceTorqueSensorPartName,
           .context = GetContext(),
           .control_frequency_hz = kControlFrequencyHz},
          HalForceTorqueSensorPart::kPartTypeName, ft_sensor_part_config),
      StatusIs(absl::StatusCode::kNotFound,
               HasSubstr("does not export interface")));
}

TEST_F(HalForceTorqueSensorPartTest, FailsForWrongInterface) {
  intrinsic_proto::icon::HalForceTorqueSensorPartConfig ft_sensor_part_config =
      CreateDefaultHalFTSensorPartConfig();
  // state interface is configured to read from command interface.
  ft_sensor_part_config.mutable_force_torque_state()->set_interface_name(
      "force_torque_sensor_command");
  ASSERT_OK_AND_ASSIGN(auto default_world, MakeDefaultRobotWorld());
  Init(std::move(default_world));

  EXPECT_THAT(
      PartTestHelper::CreateFromProtoFactory(
          {.part_name = kHalForceTorqueSensorPartName,
           .context = GetContext(),
           .control_frequency_hz = kControlFrequencyHz},
          HalForceTorqueSensorPart::kPartTypeName, ft_sensor_part_config),
      StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("Type mismatch")));
}

TEST_F(HalForceTorqueSensorPartTest, CreateWorksWithAccelerationEstimation) {
  intrinsic_proto::icon::HalForceTorqueSensorPartConfig ft_sensor_part_config =
      CreateDefaultHalFTSensorPartConfig();
  // state interface is configured to read from command interface.
  ft_sensor_part_config.clear_joint_acceleration_state();
  *ft_sensor_part_config.mutable_linear_joint_acceleration_filter_config() =
      CreateDefaultLinearJointAccelerationFilterConfig();
  ASSERT_OK_AND_ASSIGN(auto default_world, MakeDefaultRobotWorld());
  Init(std::move(default_world));
  EXPECT_OK(PartTestHelper::CreateFromProtoFactory(
      {.part_name = kHalForceTorqueSensorPartName,
       .context = GetContext(),
       .control_frequency_hz = kControlFrequencyHz},
      HalForceTorqueSensorPart::kPartTypeName, ft_sensor_part_config));
}

TEST_F(HalForceTorqueSensorPartTest,
       FailsWithNoAccelerationAndPostSensorDynamicsEstimationTrue) {
  intrinsic_proto::icon::HalForceTorqueSensorPartConfig ft_sensor_part_config =
      CreateDefaultHalFTSensorPartConfig();
  ft_sensor_part_config.clear_joint_acceleration_state();
  ft_sensor_part_config.set_estimate_post_sensor_dynamic_load(true);
  ASSERT_OK_AND_ASSIGN(auto default_world, MakeDefaultRobotWorld());
  Init(std::move(default_world));
  EXPECT_THAT(
      PartTestHelper::CreateFromProtoFactory(
          {.part_name = kHalForceTorqueSensorPartName,
           .context = GetContext(),
           .control_frequency_hz = kControlFrequencyHz},
          HalForceTorqueSensorPart::kPartTypeName, ft_sensor_part_config),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("joint acceleration")));
}

// Test that mounted payload property is initialized to the default value set
// in the config.
TEST_F(HalForceTorqueSensorPartTest, DefaultMountedPayloadTest) {
  ASSERT_OK_AND_ASSIGN(auto default_world, MakeDefaultRobotWorld());
  Init(std::move(default_world));
  ASSERT_OK_AND_ASSIGN(helper_with_part_,
                       PartTestHelper::CreateFromProtoFactory(
                           {.part_name = kHalForceTorqueSensorPartName,
                            .context = GetContext(),
                            .control_frequency_hz = kControlFrequencyHz},
                           HalForceTorqueSensorPart::kPartTypeName,
                           CreateDefaultHalFTSensorPartConfig()));

  EXPECT_THAT(helper_with_part_->GetPartPropertyAccess().GetDoubleProperty(
                  "mounted_payload_mass"),
              IsOkAndHolds(kDefaultMountedPayloadMass));
  EXPECT_THAT(helper_with_part_->GetPartPropertyAccess().GetDoubleProperty(
                  "mounted_payload_tip_t_cog_x"),
              IsOkAndHolds(kDefaultMountedPayloadCoG.x()));
  EXPECT_THAT(helper_with_part_->GetPartPropertyAccess().GetDoubleProperty(
                  "mounted_payload_tip_t_cog_y"),
              IsOkAndHolds(kDefaultMountedPayloadCoG.y()));
  EXPECT_THAT(helper_with_part_->GetPartPropertyAccess().GetDoubleProperty(
                  "mounted_payload_tip_t_cog_z"),
              IsOkAndHolds(kDefaultMountedPayloadCoG.z()));
}

// Test that the payload part properties for the mounted payload arrive in the
// controller.
TEST_F(HalForceTorqueSensorPartTest, ForceTorquePropagates) {
  const Pose3d tip_t_ft = Pose3d{
      eigenmath::Quaterniond(0.707106781186547, 0.707106781186547, 0.0, 0.0),
      eigenmath::Vector3d(0.1, 0, 0)};
  ASSERT_OK_AND_ASSIGN(
      auto default_world,
      MakeDefaultRobotWorld(/*moveable_sensor_link=*/false, tip_t_ft));
  Init(std::move(default_world));
  ASSERT_OK_AND_ASSIGN(helper_with_part_,
                       PartTestHelper::CreateFromProtoFactory(
                           {.part_name = kHalForceTorqueSensorPartName,
                            .context = GetContext(),
                            .control_frequency_hz = kControlFrequencyHz},
                           HalForceTorqueSensorPart::kPartTypeName,
                           CreateDefaultHalFTSensorPartConfig()));

  eigenmath::Vector3d joint_positions({0.1, 0.3, 0.6});
  ForceTorqueSensor* ft_sensor_part =
      helper_with_part_->GetPartFeatureInterfaces()
          .GetInterface<ForceTorqueSensor>();
  INTRINSIC_RT_EXPECT_OK(
      arm_module_ptr_->SetJointPositionState(joint_positions));

  Wrench measured_wrench(eigenmath::Vector6d({0.3, 0.6, 0.9, 1.3, 2.6, 3.9}));
  ft_module_ptr_->SetForceTorque(measured_wrench);
  ft_module_ptr_->SetError(intrinsic_fbs::ForceSensorStatusCode::Ok);

  INTRINSIC_RT_EXPECT_OK(helper_with_part_->ReadStatus());

  // ApplyCommand updates time. Timestamp of command should be newer.
  const auto last_updated_time =
      ft_module_ptr_->force_torque_command_handle_.LastUpdatedTime();
  INTRINSIC_RT_EXPECT_OK(helper_with_part_->ApplyCommand());
  EXPECT_GT(ft_module_ptr_->force_torque_command_handle_.LastUpdatedTime(),
            last_updated_time);

  const Wrench expected_wrench_at_ft{4.04857984104,  -12.2023835603,
                                     7.0376176581,   -1.3,
                                     -10.5376176581, -15.5023835603};
  const Wrench expected_wrench_at_tip{4.04857984104,  -7.0376176581,
                                      -12.2023835603, -1.3,
                                      16.7226219163,  -11.2413794239};
  EXPECT_THAT(ft_sensor_part->WrenchAtSensor(),
              IsApprox(expected_wrench_at_ft, kSmallTolerance));
  EXPECT_THAT(ft_sensor_part->WrenchAtTip(),
              IsApprox(expected_wrench_at_tip, kSmallTolerance));
}

TEST_F(HalForceTorqueSensorPartTest, ErrorIfTooManyConstantReadings) {
  const int max_num_constant_readings = 50;
  intrinsic_proto::icon::HalForceTorqueSensorPartConfig ft_sensor_part_config =
      CreateDefaultHalFTSensorPartConfig();
  // state interface is configured to read from command interface.
  ft_sensor_part_config.set_num_acceptable_constant_readings(
      max_num_constant_readings);
  ASSERT_OK_AND_ASSIGN(auto default_world, MakeDefaultRobotWorld());
  Init(std::move(default_world));
  ASSERT_OK_AND_ASSIGN(
      helper_with_part_,
      PartTestHelper::CreateFromProtoFactory(
          {.part_name = kHalForceTorqueSensorPartName,
           .context = GetContext(),
           .control_frequency_hz = kControlFrequencyHz},
          HalForceTorqueSensorPart::kPartTypeName, ft_sensor_part_config));

  Wrench measured_wrench(eigenmath::Vector6d({0.3, 0.6, 0.9, 1.3, 2.6, 3.9}));
  ft_module_ptr_->SetForceTorque(measured_wrench);

  for (int i = 0; i <= max_num_constant_readings; ++i) {
    INTRINSIC_RT_EXPECT_OK(helper_with_part_->ReadStatus());
    INTRINSIC_RT_EXPECT_OK(helper_with_part_->ApplyCommand());
  }
  EXPECT_THAT(helper_with_part_->ReadStatus(),
              StatusIs(absl::StatusCode::kInternal));
}

TEST_F(HalForceTorqueSensorPartTest, MountedPayloadChangingWorks) {
  ASSERT_OK_AND_ASSIGN(auto default_world, MakeDefaultRobotWorld());
  Init(std::move(default_world));
  ASSERT_OK_AND_ASSIGN(helper_with_part_,
                       PartTestHelper::CreateFromProtoFactory(
                           {.part_name = kHalForceTorqueSensorPartName,
                            .context = GetContext(),
                            .control_frequency_hz = kControlFrequencyHz},
                           HalForceTorqueSensorPart::kPartTypeName,
                           CreateDefaultHalFTSensorPartConfig()));
  // Set the new part property
  const double new_mass = 1.337;
  const eigenmath::Vector3d new_cog({0.1, 0.2, 0.3});
  EXPECT_OK(helper_with_part_->GetPartPropertyAccess().SetDoubleProperty(
      "mounted_payload_mass", new_mass));
  EXPECT_OK(helper_with_part_->GetPartPropertyAccess().SetDoubleProperty(
      "mounted_payload_tip_t_cog_x", new_cog[0]));
  EXPECT_OK(helper_with_part_->GetPartPropertyAccess().SetDoubleProperty(
      "mounted_payload_tip_t_cog_y", new_cog[1]));
  EXPECT_OK(helper_with_part_->GetPartPropertyAccess().SetDoubleProperty(
      "mounted_payload_tip_t_cog_z", new_cog[2]));

  ft_module_ptr_->SetError(intrinsic_fbs::ForceSensorStatusCode::Ok);

  INTRINSIC_RT_EXPECT_OK(helper_with_part_->ReadStatus());
  INTRINSIC_RT_EXPECT_OK(helper_with_part_->ApplyCommand());
  INTRINSIC_RT_EXPECT_OK(helper_with_part_->ReadStatus());

  EXPECT_THAT(helper_with_part_->GetPartPropertyAccess().GetDoubleProperty(
                  "mounted_payload_mass"),
              IsOkAndHolds(new_mass));
  EXPECT_THAT(helper_with_part_->GetPartPropertyAccess().GetDoubleProperty(
                  "mounted_payload_tip_t_cog_x"),
              IsOkAndHolds(new_cog[0]));
  EXPECT_THAT(helper_with_part_->GetPartPropertyAccess().GetDoubleProperty(
                  "mounted_payload_tip_t_cog_y"),
              IsOkAndHolds(new_cog[1]));
  EXPECT_THAT(helper_with_part_->GetPartPropertyAccess().GetDoubleProperty(
                  "mounted_payload_tip_t_cog_z"),
              IsOkAndHolds(new_cog[2]));
}

TEST_F(HalForceTorqueSensorPartTest, Tare) {
  ASSERT_OK_AND_ASSIGN(auto default_world, MakeDefaultRobotWorld());
  Init(std::move(default_world));
  ASSERT_OK_AND_ASSIGN(helper_with_part_,
                       PartTestHelper::CreateFromProtoFactory(
                           {.part_name = kHalForceTorqueSensorPartName,
                            .context = GetContext(),
                            .control_frequency_hz = kControlFrequencyHz},
                           HalForceTorqueSensorPart::kPartTypeName,
                           CreateDefaultHalFTSensorPartConfig()));

  // Make sure the hwm is starting off while not taring.
  EXPECT_FALSE(ft_module_ptr_->IsTaring());
  ft_module_ptr_->SetError(intrinsic_fbs::ForceSensorStatusCode::Ok);

  ForceTorqueSensor* ft_sensor_part =
      helper_with_part_->GetPartFeatureInterfaces()
          .GetInterface<ForceTorqueSensor>();
  EXPECT_OK(hwm_manager_.Prepare(absl::InfiniteFuture()));
  EXPECT_OK(hwm_manager_.Activate(absl::InfiniteFuture()));
  absl::Notification enable_motion_notification;
  Thread thread;
  ASSERT_OK_AND_ASSIGN(
      thread,
      CreateRealtimeCapableThread(ThreadOptions().SetName("Enable"), [&]() {
        EXPECT_OK(hwm_manager_.EnableMotionTestOnly(
            /*deadline=*/absl::InfiniteFuture()));
        enable_motion_notification.Notify();
      }));
  while (!enable_motion_notification.HasBeenNotified()) {
    EXPECT_OK(hwm_manager_.Read(
        /*deadline=*/absl::InfiniteFuture()));  // Trigger processing of HWM
                                                // state.
    absl::SleepFor(absl::Milliseconds(100));
  }
  EXPECT_OK(hwm_manager_.UpdateState());
  INTRINSIC_RT_EXPECT_OK(helper_with_part_->ReadStatus());
  int num_taring_cycles = 5;
  INTRINSIC_RT_EXPECT_OK(ft_sensor_part->Tare(num_taring_cycles));
  for (int i = 0; i < num_taring_cycles + 1; ++i) {
    INTRINSIC_RT_EXPECT_OK(helper_with_part_->ApplyCommand());
    EXPECT_FALSE(ft_sensor_part->TareIsDone().value());
    INTRINSIC_RT_EXPECT_OK(hwm_manager_.Write(absl::InfiniteFuture()));
    EXPECT_TRUE(ft_module_ptr_->IsTaring());
    INTRINSIC_RT_EXPECT_OK(hwm_manager_.Read(absl::InfiniteFuture()));
    INTRINSIC_RT_EXPECT_OK(helper_with_part_->ReadStatus());
  }
  EXPECT_FALSE(ft_module_ptr_->IsTaring());
  EXPECT_THAT(ft_sensor_part->TareIsDone().value(), true);
  thread.join();
}

TEST_F(HalForceTorqueSensorPartTest, DynamicLoadPropagates) {
  intrinsic_proto::icon::HalForceTorqueSensorPartConfig ft_sensor_part_config =
      CreateDefaultHalFTSensorPartConfig();
  // state interface is configured to read from command interface.
  ft_sensor_part_config.set_estimate_post_sensor_dynamic_load(true);
  ft_sensor_part_config.set_target_link_name(kTargetLinkName);

  const Pose3d tip_t_ft = Pose3d{
      eigenmath::Quaterniond(0.707106781186547, 0.707106781186547, 0.0, 0.0),
      eigenmath::Vector3d(0.1, 0, 0)};
  ASSERT_OK_AND_ASSIGN(
      auto default_world,
      MakeDefaultRobotWorld(/*moveable_sensor_link=*/false, tip_t_ft));
  Init(std::move(default_world));
  ASSERT_OK_AND_ASSIGN(
      helper_with_part_,
      PartTestHelper::CreateFromProtoFactory(
          {.part_name = kHalForceTorqueSensorPartName,
           .context = GetContext(),
           .control_frequency_hz = kControlFrequencyHz},
          HalForceTorqueSensorPart::kPartTypeName, ft_sensor_part_config));

  // Oscillating motion, sinusoid on first joint, zero on all others. Move until
  // one full sinusoid cycle has been completed. This allows all estimator
  // transients to decay.
  const double kAmplitudeRad = 0.5;
  const double kOmegaRadSec = 2.0;
  const double kTimeFullCycleSec = 2.0 * M_PI / kOmegaRadSec;
  const int num_steps_to_exceed_first_cycle =
      static_cast<int>(kTimeFullCycleSec * kControlFrequencyHz) + 2;
  eigenmath::VectorNd joint_position = eigenmath::VectorNd::Zero(3);
  eigenmath::VectorNd joint_velocity = eigenmath::VectorNd::Zero(3);
  eigenmath::VectorNd joint_acceleration = eigenmath::VectorNd::Zero(3);
  for (size_t i = 0; i < num_steps_to_exceed_first_cycle; ++i) {
    const double phi = i * kOmegaRadSec / kControlFrequencyHz;
    joint_position[0] = kAmplitudeRad * cos(phi);
    joint_velocity[0] = -kAmplitudeRad * kOmegaRadSec * sin(phi);
    joint_acceleration[0] =
        -kAmplitudeRad * kOmegaRadSec * kOmegaRadSec * cos(phi);
    INTRINSIC_RT_EXPECT_OK(
        arm_module_ptr_->SetJointPositionState(joint_position));
    INTRINSIC_RT_EXPECT_OK(
        arm_module_ptr_->SetJointVelocityState(joint_velocity));
    INTRINSIC_RT_EXPECT_OK(
        arm_module_ptr_->SetJointAccelerationState(joint_acceleration));

    INTRINSIC_RT_EXPECT_OK(helper_with_part_->ReadStatus());
    INTRINSIC_RT_EXPECT_OK(helper_with_part_->ApplyCommand());
  }

  ForceTorqueSensor* ft_sensor_part =
      helper_with_part_->GetPartFeatureInterfaces()
          .GetInterface<ForceTorqueSensor>();
  const Wrench expected_post_sensor_dynamic_load_at_ft{
      -7.30204266701e-05, 1.39886479714e-15, 6.29992697915, 0,
      -9.29989220732,     2.06499089102e-15};
  const Wrench expected_post_sensor_dynamic_load_at_tip{
      -7.30204266701e-05, -6.29992697915, 0.0, 0.0,
      3.94430452611e-31,  -9.92988490523};
  EXPECT_THAT(
      ft_sensor_part->PostSensorDynamicLoadAtSensor(),
      IsApprox(expected_post_sensor_dynamic_load_at_ft, kSmallTolerance));
  EXPECT_THAT(
      ft_sensor_part->PostSensorDynamicLoadAtTip(),
      IsApprox(expected_post_sensor_dynamic_load_at_tip, kSmallTolerance));
}

TEST_F(HalForceTorqueSensorPartTest,
       StandaloneForceTorqueSensorCreationSucceeds) {
  intrinsic_proto::icon::HalForceTorqueSensorPartConfig ft_sensor_part_config;

  // FT Hardware interfaces
  ft_sensor_part_config.mutable_force_torque_state()->set_interface_name(
      ForceTorqueModule::kForceTorqueStatus);
  ft_sensor_part_config.mutable_force_torque_state()->set_module_name(
      kFTModuleName);
  ft_sensor_part_config.mutable_force_torque_command()->set_interface_name(
      ForceTorqueModule::kForceTorqueCommand);
  ft_sensor_part_config.mutable_force_torque_command()->set_module_name(
      kFTModuleName);

  ASSERT_OK_AND_ASSIGN(auto default_world, MakeDefaultRobotWorld());
  Init(std::move(default_world));

  EXPECT_OK(PartTestHelper::CreateFromProtoFactory(
      {.part_name = kHalForceTorqueSensorPartName,
       .context = GetContext(),
       .control_frequency_hz = kControlFrequencyHz},
      HalForceTorqueSensorPart::kPartTypeName, ft_sensor_part_config));
}

TEST_F(HalForceTorqueSensorPartTest,
       StandaloneForceTorqueSensorPropagatesForce) {
  intrinsic_proto::icon::HalForceTorqueSensorPartConfig ft_sensor_part_config;

  // FT Hardware interfaces
  ft_sensor_part_config.mutable_force_torque_state()->set_interface_name(
      ForceTorqueModule::kForceTorqueStatus);
  ft_sensor_part_config.mutable_force_torque_state()->set_module_name(
      kFTModuleName);
  ft_sensor_part_config.mutable_force_torque_command()->set_interface_name(
      ForceTorqueModule::kForceTorqueCommand);
  ft_sensor_part_config.mutable_force_torque_command()->set_module_name(
      kFTModuleName);

  ASSERT_OK_AND_ASSIGN(auto default_world, MakeDefaultRobotWorld());
  Init(std::move(default_world));

  ASSERT_OK_AND_ASSIGN(
      helper_with_part_,
      PartTestHelper::CreateFromProtoFactory(
          {.part_name = kHalForceTorqueSensorPartName,
           .context = GetContext(),
           .control_frequency_hz = kControlFrequencyHz},
          HalForceTorqueSensorPart::kPartTypeName, ft_sensor_part_config));

  StandaloneForceTorqueSensor* standalone_ft_sensor_part =
      helper_with_part_->GetPartFeatureInterfaces()
          .GetInterface<StandaloneForceTorqueSensor>();

  ForceTorqueSensor* ft_sensor_part =
      helper_with_part_->GetPartFeatureInterfaces()
          .GetInterface<ForceTorqueSensor>();

  EXPECT_EQ(ft_sensor_part, nullptr);

  Wrench measured_wrench(eigenmath::Vector6d({0.3, 0.6, 0.9, 1.3, 2.6, 3.9}));
  ft_module_ptr_->SetForceTorque(measured_wrench);
  ft_module_ptr_->SetError(intrinsic_fbs::ForceSensorStatusCode::Ok);

  INTRINSIC_RT_EXPECT_OK(helper_with_part_->ReadStatus());
  INTRINSIC_RT_EXPECT_OK(helper_with_part_->ApplyCommand());

  Wrench expected_total_wrench;
  expected_total_wrench.head<6>() = -measured_wrench.head<6>();

  EXPECT_THAT(standalone_ft_sensor_part->WrenchAtSensorUncompensated(),
              IsApprox(expected_total_wrench));
}

TEST_F(HalForceTorqueSensorPartTest, StandaloneForceTorqueSensorSensorTare) {
  intrinsic_proto::icon::HalForceTorqueSensorPartConfig ft_sensor_part_config;

  // FT Hardware interfaces
  ft_sensor_part_config.mutable_force_torque_state()->set_interface_name(
      ForceTorqueModule::kForceTorqueStatus);
  ft_sensor_part_config.mutable_force_torque_state()->set_module_name(
      kFTModuleName);
  ft_sensor_part_config.mutable_force_torque_command()->set_interface_name(
      ForceTorqueModule::kForceTorqueCommand);
  ft_sensor_part_config.mutable_force_torque_command()->set_module_name(
      kFTModuleName);

  ASSERT_OK_AND_ASSIGN(auto default_world, MakeDefaultRobotWorld());
  Init(std::move(default_world));
  ASSERT_OK_AND_ASSIGN(
      helper_with_part_,
      PartTestHelper::CreateFromProtoFactory(
          {.part_name = kHalForceTorqueSensorPartName,
           .context = GetContext(),
           .control_frequency_hz = kControlFrequencyHz},
          HalForceTorqueSensorPart::kPartTypeName, ft_sensor_part_config));

  // Make sure the hwm is starting off while not taring.
  EXPECT_FALSE(ft_module_ptr_->IsTaring());
  ft_module_ptr_->SetError(intrinsic_fbs::ForceSensorStatusCode::Ok);

  StandaloneForceTorqueSensor* ft_sensor_part =
      helper_with_part_->GetPartFeatureInterfaces()
          .GetInterface<StandaloneForceTorqueSensor>();
  EXPECT_OK(hwm_manager_.Prepare(absl::InfiniteFuture()));
  EXPECT_OK(hwm_manager_.Activate(absl::InfiniteFuture()));

  absl::Notification enable_motion_notification;
  Thread thread;
  ASSERT_OK_AND_ASSIGN(
      thread,
      CreateRealtimeCapableThread(ThreadOptions().SetName("Enable"), [&]() {
        EXPECT_OK(hwm_manager_.EnableMotionTestOnly(
            /*deadline=*/absl::InfiniteFuture()));
        enable_motion_notification.Notify();
      }));
  while (!enable_motion_notification.HasBeenNotified()) {
    EXPECT_OK(hwm_manager_.Read(
        /*deadline=*/absl::InfiniteFuture()));  // Trigger processing of HWM
                                                // state.
    absl::SleepFor(absl::Milliseconds(100));
  }
  thread.join();
  EXPECT_OK(hwm_manager_.UpdateState());
  INTRINSIC_RT_EXPECT_OK(helper_with_part_->ReadStatus());
  int num_taring_cycles = 5;
  INTRINSIC_RT_EXPECT_OK(ft_sensor_part->Tare(num_taring_cycles));
  for (int i = 0; i < num_taring_cycles + 1; ++i) {
    INTRINSIC_RT_EXPECT_OK(helper_with_part_->ApplyCommand());
    EXPECT_FALSE(ft_sensor_part->TareIsDone().value());
    INTRINSIC_RT_EXPECT_OK(hwm_manager_.Write(absl::InfiniteFuture()));
    EXPECT_TRUE(ft_module_ptr_->IsTaring());
    INTRINSIC_RT_EXPECT_OK(hwm_manager_.Read(absl::InfiniteFuture()));
    INTRINSIC_RT_EXPECT_OK(helper_with_part_->ReadStatus());
  }
  EXPECT_FALSE(ft_module_ptr_->IsTaring());
  EXPECT_THAT(ft_sensor_part->TareIsDone().value(), true);
}

TEST_F(HalForceTorqueSensorPartTest, ExternalJointTorqueModeCreate) {
  ASSERT_OK_AND_ASSIGN(auto default_world, MakeDefaultRobotWorld());
  Init(std::move(default_world));

  auto config = CreateDefaultHalFTSensorPartConfig();
  config.clear_force_torque_state();
  config.clear_force_torque_command();
  config.mutable_external_joint_torque_state()->set_module_name(kArmModuleName);
  config.mutable_external_joint_torque_state()->set_interface_name(
      ArmModule::kExternalJointTorqueInterfaceName);

  EXPECT_OK(PartTestHelper::CreateFromProtoFactory(
      {.part_name = kHalForceTorqueSensorPartName,
       .context = GetContext(),
       .control_frequency_hz = kControlFrequencyHz},
      HalForceTorqueSensorPart::kPartTypeName, config));
}

TEST_F(HalForceTorqueSensorPartTest, ExternalJointTorqueModeComputeValues) {
  ASSERT_OK_AND_ASSIGN(auto default_world, MakeDefaultRobotWorld());
  Init(std::move(default_world));

  auto config = CreateDefaultHalFTSensorPartConfig();
  config.clear_force_torque_state();
  config.clear_force_torque_command();
  config.mutable_external_joint_torque_state()->set_module_name(kArmModuleName);
  config.mutable_external_joint_torque_state()->set_interface_name(
      ArmModule::kExternalJointTorqueInterfaceName);

  ASSERT_OK_AND_ASSIGN(helper_with_part_,
                       PartTestHelper::CreateFromProtoFactory(
                           {.part_name = kHalForceTorqueSensorPartName,
                            .context = GetContext(),
                            .control_frequency_hz = kControlFrequencyHz},
                           HalForceTorqueSensorPart::kPartTypeName, config));

  ForceTorqueSensor* ft_sensor = helper_with_part_->GetPartFeatureInterfaces()
                                     .GetInterface<ForceTorqueSensor>();

  // Set external joint torques to zero. This results in a non-zero torque
  // because the code compensates for the mass specified.
  INTRINSIC_RT_ASSERT_OK(
      arm_module_ptr_->SetExternalJointTorque(eigenmath::Vector3d(0, 0, 0)));
  INTRINSIC_RT_EXPECT_OK(helper_with_part_->ReadStatus());
  INTRINSIC_RT_EXPECT_OK(helper_with_part_->ApplyCommand());
  const eigenmath::Vector6d kExpectedWrenchForZeroTorque =
      kDefaultMountedPayloadMass *
      eigenmath::Vector6d{0, 0, -kDefaultGravity, 0, kDefaultGravity, 0};
  ASSERT_THAT(ft_sensor->WrenchAtSensor(),
              IsApprox(kExpectedWrenchForZeroTorque));

  // Now set some non-zero values for the external joint torques. The result
  // depends on the jacobian mapping, so we just check changed from before.
  INTRINSIC_RT_ASSERT_OK(
      arm_module_ptr_->SetExternalJointTorque(eigenmath::Vector3d(1, 1, 1)));
  INTRINSIC_RT_EXPECT_OK(helper_with_part_->ReadStatus());
  INTRINSIC_RT_EXPECT_OK(helper_with_part_->ApplyCommand());
  eigenmath::Vector6d delta_wrench =
      kExpectedWrenchForZeroTorque - ft_sensor->WrenchAtSensor();
  ASSERT_GT(delta_wrench.norm(), 1.0);
}

TEST_F(HalForceTorqueSensorPartTest, ExternalJointTorqueModeTare) {
  ASSERT_OK_AND_ASSIGN(auto default_world, MakeDefaultRobotWorld());
  Init(std::move(default_world));

  auto config = CreateDefaultHalFTSensorPartConfig();
  config.clear_force_torque_state();
  config.clear_force_torque_command();
  config.mutable_external_joint_torque_state()->set_module_name(kArmModuleName);
  config.mutable_external_joint_torque_state()->set_interface_name(
      ArmModule::kExternalJointTorqueInterfaceName);

  ASSERT_OK_AND_ASSIGN(helper_with_part_,
                       PartTestHelper::CreateFromProtoFactory(
                           {.part_name = kHalForceTorqueSensorPartName,
                            .context = GetContext(),
                            .control_frequency_hz = kControlFrequencyHz},
                           HalForceTorqueSensorPart::kPartTypeName, config));

  ForceTorqueSensor* ft_sensor = helper_with_part_->GetPartFeatureInterfaces()
                                     .GetInterface<ForceTorqueSensor>();

  // Set external joint torques to zero. This results in a non-zero torque
  // because the code compensates for the mass specified.
  INTRINSIC_RT_ASSERT_OK(
      arm_module_ptr_->SetExternalJointTorque(eigenmath::Vector3d(0, 0, 0)));
  INTRINSIC_RT_EXPECT_OK(helper_with_part_->ReadStatus());
  INTRINSIC_RT_EXPECT_OK(helper_with_part_->ApplyCommand());

  // Check that we start with a non-zero torque.
  const eigenmath::Vector6d kExpectedWrenchForZeroTorque =
      kDefaultMountedPayloadMass *
      eigenmath::Vector6d{0, 0, -kDefaultGravity, 0, kDefaultGravity, 0};
  ASSERT_THAT(ft_sensor->WrenchAtSensor(),
              IsApprox(kExpectedWrenchForZeroTorque));

  // After taring we should read zeros.
  constexpr int kTareCycles = 10;
  INTRINSIC_RT_ASSERT_OK(ft_sensor->Tare(kTareCycles));

  for (int i = 0; i < kTareCycles; i++) {
    INTRINSIC_RT_EXPECT_OK(helper_with_part_->ReadStatus());
    INTRINSIC_RT_EXPECT_OK(helper_with_part_->ApplyCommand());
    INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(bool tare_done, ft_sensor->TareIsDone());
    // It takes one cycle start the taring since it happens in ApplyCommand, so
    // we should see kTareCycles of taring, and in the following one we should
    // be finished.
    ASSERT_FALSE(tare_done);
  }

  // One more cycle.
  INTRINSIC_RT_EXPECT_OK(helper_with_part_->ReadStatus());
  INTRINSIC_RT_EXPECT_OK(helper_with_part_->ApplyCommand());
  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(bool tare_done, ft_sensor->TareIsDone());
  ASSERT_TRUE(tare_done);
  EXPECT_THAT(ft_sensor->WrenchAtSensor(),
              IsApprox(eigenmath::Vector6d::Zero()));
}

TEST_F(HalForceTorqueSensorPartTest,
       ExternalJointTorqueModeCorrectFrameForFtSensor) {
  ASSERT_OK_AND_ASSIGN(auto default_world, MakeDefaultRobotWorld());
  Init(std::move(default_world));

  auto config = CreateDefaultHalFTSensorPartConfig();
  config.clear_force_torque_state();
  config.clear_force_torque_command();
  config.mutable_external_joint_torque_state()->set_module_name(kArmModuleName);
  config.mutable_external_joint_torque_state()->set_interface_name(
      ArmModule::kExternalJointTorqueInterfaceName);
  // We specifically set the payload to zero here, because we just want to
  // observe the computation of the wrench from the external torque.
  config.set_support_mass(0);
  config.mutable_ft_t_cog()->Clear();
  config.mutable_ft_t_cog()->Add(0);
  config.mutable_ft_t_cog()->Add(0);
  config.mutable_ft_t_cog()->Add(0);

  ASSERT_OK_AND_ASSIGN(helper_with_part_,
                       PartTestHelper::CreateFromProtoFactory(
                           {.part_name = kHalForceTorqueSensorPartName,
                            .context = GetContext(),
                            .control_frequency_hz = kControlFrequencyHz},
                           HalForceTorqueSensorPart::kPartTypeName, config));

  // Set a specific joint position. It's important that this results in a pose
  // for the ft sensor which is not identity so that we can confirm the frame is
  // transformed appropriately.
  //
  // We specifically select a pi rotation here so that the ft sensor's frame
  // differs by this amount. This will flip around x and y reading so we can
  // just check the sign to confirm the right behavior.
  eigenmath::Vector3d joint_positions({M_PI, 0, 0});
  INTRINSIC_RT_EXPECT_OK(
      arm_module_ptr_->SetJointPositionState(joint_positions));

  ForceTorqueSensor* ft_sensor = helper_with_part_->GetPartFeatureInterfaces()
                                     .GetInterface<ForceTorqueSensor>();

  // Set external joint torques some value.
  INTRINSIC_RT_ASSERT_OK(
      arm_module_ptr_->SetExternalJointTorque(eigenmath::Vector3d(1, 1, 1)));
  INTRINSIC_RT_EXPECT_OK(helper_with_part_->ReadStatus());
  INTRINSIC_RT_EXPECT_OK(helper_with_part_->ApplyCommand());

  eigenmath::Vector6d wrench = ft_sensor->WrenchAtSensor();
  // The signs here were just observed from at a known correct state.
  EXPECT_LT(wrench(1), 0);
  EXPECT_LT(wrench(3), 0);
  EXPECT_LT(wrench(4), 0);
}

class HalForceTorqueSensorPartMultiModuleTest : public ::testing::Test {
 public:
  static constexpr char kModuleName1[] = "ft_module_1";
  static constexpr char kModuleName2[] = "ft_module_2";

  void SetUp() override {
    intrinsic_proto::icon::HardwareModuleConfig ft_hw_module_config_1;
    ft_hw_module_config_1.set_name(kModuleName1);

    std::string shared_memory_namespace =
        intrinsic::icon::UniqueMemoryNamespace();

    auto module_config_1 =
        ModuleConfig(ft_hw_module_config_1, shared_memory_namespace,
                     /*realtime_clock=*/nullptr);
    auto ft_module_1 = std::make_unique<ForceTorqueModule>();
    ft_module_ptr_1_ = ft_module_1.get();
    {
      ASSERT_OK_AND_ASSIGN(auto shm_manager,
                           intrinsic::icon::SharedMemoryManager::Create(
                               shared_memory_namespace, kModuleName1));
      ASSERT_OK_AND_ASSIGN(
          auto module_runtime_1,
          HardwareModuleRuntime::Create(std::move(shm_manager),
                                        {
                                            /*realtime_clock=*/nullptr,
                                            std::move(ft_module_1),
                                            module_config_1,
                                        }));
      module_runtime_1_ = std::move(module_runtime_1);
    }
    grpc::ServerBuilder server_builder1;
    EXPECT_OK(module_runtime_1_->Run(server_builder1));

    intrinsic_proto::icon::HardwareModuleConfig ft_hw_module_config_2;
    ft_hw_module_config_2.set_name(kModuleName2);
    auto module_config_2 =
        ModuleConfig(ft_hw_module_config_2, shared_memory_namespace,
                     /*realtime_clock=*/nullptr);
    auto ft_module_2 = std::make_unique<ForceTorqueModule>();
    ft_module_ptr_2_ = ft_module_2.get();

    {
      ASSERT_OK_AND_ASSIGN(auto shm_manager,
                           intrinsic::icon::SharedMemoryManager::Create(
                               shared_memory_namespace, kModuleName2));

      ASSERT_OK_AND_ASSIGN(
          auto module_runtime_2,
          HardwareModuleRuntime::Create(
              std::move(shm_manager),
              {
                  /*realtime_clock=*/nullptr,
                  std::move(ft_module_2),
                  ModuleConfig(ft_hw_module_config_2, shared_memory_namespace,
                               /*realtime_clock=*/nullptr),
              }));
      module_runtime_2_ = std::move(module_runtime_2);
    }
    grpc::ServerBuilder server_builder2;
    EXPECT_OK(module_runtime_2_->Run(server_builder2));

    ASSERT_OK_AND_ASSIGN(
        auto hardware_module_proxy_1,
        HardwareModuleProxy::Attach(module_config_1, absl::ZeroDuration()));
    ASSERT_OK_AND_ASSIGN(
        auto hardware_module_proxy_2,
        HardwareModuleProxy::Attach(module_config_2, absl::ZeroDuration()));
    EXPECT_TRUE(hardware_module_proxy_1.IsConnected());
    EXPECT_TRUE(hardware_module_proxy_2.IsConnected());
    ASSERT_OK(manager_.Add(std::move(hardware_module_proxy_1),
                           /*proxy_drives_clock=*/false,
                           intrinsic_proto::icon::HardwareConfig{}));
    ASSERT_OK(manager_.Add(std::move(hardware_module_proxy_2),
                           /*proxy_drives_clock=*/false,
                           intrinsic_proto::icon::HardwareConfig{}));
    module_proxy_1_ = manager_.GetHardwareModuleProxy(kModuleName1);
    module_proxy_2_ = manager_.GetHardwareModuleProxy(kModuleName2);
  }

  ~HalForceTorqueSensorPartMultiModuleTest() override {
    EXPECT_OK(module_runtime_1_->Stop());
    EXPECT_OK(module_runtime_2_->Stop());
  }

  Context GetContext() {
    return Context(
        /*service_collection=*/&service_collection_,
        /*hardware_module_manager=*/&manager_,
        /*runtime_options=*/{});
  }

  ForceTorqueModule *ft_module_ptr_1_, *ft_module_ptr_2_;
  std::unique_ptr<HardwareModuleRuntime> module_runtime_1_, module_runtime_2_;
  ServiceCollection service_collection_;
  const HardwareModuleProxy *module_proxy_1_, *module_proxy_2_;
  HardwareModuleManager manager_;
  std::unique_ptr<PartTestHelper> helper_with_part_ = nullptr;
};

}  // namespace
}  // namespace intrinsic::icon
