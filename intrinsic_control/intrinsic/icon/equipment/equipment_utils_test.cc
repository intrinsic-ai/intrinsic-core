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

#include "intrinsic/icon/equipment/equipment_utils.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/strings/str_format.h"
#include "absl/time/time.h"
#include "google/protobuf/any.pb.h"
#include "internal/testing.h"
#include "intrinsic/icon/cc_client/client.h"
#include "intrinsic/icon/equipment/channel_factory.h"
#include "intrinsic/icon/equipment/icon_equipment.pb.h"
#include "intrinsic/icon/equipment/testing/fake_channel_factory.h"
#include "intrinsic/icon/proto/v1/types.pb.h"
#include "intrinsic/icon/server/fake_robot_connection.h"
#include "intrinsic/icon/server/testing/in_process_server.h"
#include "intrinsic/production/external/tsl/net.h"
#include "intrinsic/resources/proto/resource_handle.pb.h"
#include "intrinsic/skills/cc/equipment_pack.h"
#include "intrinsic/skills/proto/equipment.pb.h"
#include "intrinsic/util/proto/parse_text_proto.h"
#include "protobuf-matchers/protocol-buffer-matchers.h"

namespace intrinsic {
namespace icon {
namespace {

constexpr char kIconServerName[] = "icon_server";
constexpr double kControlFrequencyHz = 1000;

using ::protobuf_matchers::EqualsProto;
using ::protobuf_matchers::proto::IgnoringRepeatedFieldOrdering;

intrinsic_proto::icon::v1::ServerConfig ServerConfig() {
  intrinsic_proto::icon::v1::ServerConfig server_config;
  server_config.set_name(kIconServerName);
  server_config.set_frequency_hz(kControlFrequencyHz);
  return server_config;
}

class EquipmentUtilsTestFixture : public ::testing::Test {
 public:
  EquipmentUtilsTestFixture()
      : port_(tsl::net::PickUnusedPortOrDie()),
        robot_connection_(ServerConfig(),
                          /*part_names=*/{}),
        // Start server on that port.
        server_(std::make_unique<InProcessApplicationLayerServer>(
            robot_connection_,
            InProcessApplicationLayerServer::Options{
                .use_in_process_channel = false, .port = port_})) {}

  ~EquipmentUtilsTestFixture() override {
    server_ = nullptr;
    // RecycleUnusedPort is not available on insrc.
  }

  int port_;
  FakeRobotConnection robot_connection_;
  std::unique_ptr<InProcessApplicationLayerServer> server_;
};

TEST_F(EquipmentUtilsTestFixture,
       ConnectToIconEquipmentErrorsNoConnectionInfo) {
  intrinsic_proto::resources::ResourceHandle resource_handle;
  intrinsic_proto::icon::Icon2PositionPart position_part_config;
  position_part_config.set_part_name("robot_arm");

  ASSERT_TRUE((*resource_handle.mutable_resource_data())[kIcon2PositionPartKey]
                  .mutable_contents()
                  ->PackFrom(position_part_config));

  constexpr char kEquipmentSlot[] = "robot";
  google::protobuf::Map<std::string, intrinsic_proto::resources::ResourceHandle>
      resource_handles;
  resource_handles.insert({kEquipmentSlot, resource_handle});

  EXPECT_TRUE(absl::IsInvalidArgument(
      ConnectToIconEquipment(skills::EquipmentPack(resource_handles),
                             kEquipmentSlot, DefaultChannelFactory(),
                             absl::Seconds(1))
          .status()));
}

TEST_F(EquipmentUtilsTestFixture, ConnectToIconEquipmentErrorsNoGrpc) {
  intrinsic_proto::resources::ResourceHandle resource_handle;
  intrinsic_proto::icon::Icon2PositionPart position_part_config;
  position_part_config.set_part_name("robot_arm");

  *resource_handle.mutable_connection_info() = {};

  ASSERT_TRUE((*resource_handle.mutable_resource_data())[kIcon2PositionPartKey]
                  .mutable_contents()
                  ->PackFrom(position_part_config));

  constexpr char kEquipmentSlot[] = "robot";
  google::protobuf::Map<std::string, intrinsic_proto::resources::ResourceHandle>
      resource_handles;
  resource_handles.insert({kEquipmentSlot, resource_handle});

  EXPECT_TRUE(absl::IsInvalidArgument(
      ConnectToIconEquipment(skills::EquipmentPack(resource_handles),
                             kEquipmentSlot, DefaultChannelFactory(),
                             absl::Seconds(1))
          .status()));
}

TEST_F(EquipmentUtilsTestFixture, ConnectToIconPositionEquipment) {
  intrinsic_proto::resources::ResourceHandle resource_handle;
  intrinsic_proto::icon::Icon2PositionPart position_part_config;
  position_part_config.set_part_name("robot_arm");

  resource_handle.mutable_connection_info()->mutable_grpc()->set_address(
      absl::StrFormat("localhost:%d", port_));

  ASSERT_TRUE((*resource_handle.mutable_resource_data())[kIcon2PositionPartKey]
                  .mutable_contents()
                  ->PackFrom(position_part_config));

  constexpr char kEquipmentSlot[] = "robot";
  google::protobuf::Map<std::string, intrinsic_proto::resources::ResourceHandle>
      resource_handles;
  resource_handles.insert({kEquipmentSlot, resource_handle});

  ASSERT_OK_AND_ASSIGN(
      IconEquipment icon_equipment,
      ConnectToIconEquipment(skills::EquipmentPack(resource_handles),
                             kEquipmentSlot, DefaultChannelFactory(),
                             absl::Seconds(1)));

  EXPECT_EQ(icon_equipment.position_part_name, "robot_arm");

  // Verify that we can use the returned connection.
  Client icon_client(icon_equipment.channel);
  EXPECT_OK(icon_client.GetConfig());
}

TEST_F(EquipmentUtilsTestFixture, ConnectToIconTorqueEquipment) {
  intrinsic_proto::resources::ResourceHandle resource_handle;
  intrinsic_proto::icon::Icon2PositionPart part_config;
  part_config.set_part_name("robot_arm");

  resource_handle.mutable_connection_info()->mutable_grpc()->set_address(
      absl::StrFormat("localhost:%d", port_));

  ASSERT_TRUE((*resource_handle.mutable_resource_data())[kIcon2PositionPartKey]
                  .mutable_contents()
                  ->PackFrom(part_config));

  constexpr char kEquipmentSlot[] = "robot";
  google::protobuf::Map<std::string, intrinsic_proto::resources::ResourceHandle>
      resource_handles;
  resource_handles.insert({kEquipmentSlot, resource_handle});

  ASSERT_OK_AND_ASSIGN(
      IconEquipment icon_equipment,
      ConnectToIconEquipment(skills::EquipmentPack(resource_handles),
                             kEquipmentSlot, DefaultChannelFactory(),
                             absl::Seconds(1)));

  EXPECT_EQ(icon_equipment.position_part_name, "robot_arm");

  // Verify that we can use the returned connection.
  Client icon_client(icon_equipment.channel);
  EXPECT_OK(icon_client.GetConfig());
}

TEST_F(EquipmentUtilsTestFixture, ConnectToIconGripperEquipment) {
  intrinsic_proto::resources::ResourceHandle resource_handle;
  intrinsic_proto::icon::Icon2GripperPart gripper_part_config;
  gripper_part_config.set_part_name("gripper");
  resource_handle.mutable_connection_info()->mutable_grpc()->set_address(
      absl::StrFormat("localhost:%d", port_));

  ASSERT_TRUE((*resource_handle.mutable_resource_data())[kIcon2GripperPartKey]
                  .mutable_contents()
                  ->PackFrom(gripper_part_config));

  constexpr char kEquipmentSlot[] = "robot";
  google::protobuf::Map<std::string, intrinsic_proto::resources::ResourceHandle>
      resource_handles;
  resource_handles.insert({kEquipmentSlot, resource_handle});

  ASSERT_OK_AND_ASSIGN(
      IconEquipment icon_equipment,
      ConnectToIconEquipment(skills::EquipmentPack(resource_handles),
                             kEquipmentSlot, DefaultChannelFactory(),
                             absl::Seconds(1)));

  EXPECT_EQ(icon_equipment.gripper_part_name, "gripper");

  // Verify that we can use the returned connection.
  Client icon_client(icon_equipment.channel);
  EXPECT_OK(icon_client.GetConfig());
}

TEST_F(EquipmentUtilsTestFixture, ConnectToIconAdioEquipment) {
  intrinsic_proto::resources::ResourceHandle resource_handle;
  intrinsic_proto::icon::Icon2AdioPart adio_part_config;
  adio_part_config.mutable_icon_target()->set_part_name("adio");
  resource_handle.mutable_connection_info()->mutable_grpc()->set_address(
      absl::StrFormat("localhost:%d", port_));

  ASSERT_TRUE((*resource_handle.mutable_resource_data())[kIcon2AdioPartKey]
                  .mutable_contents()
                  ->PackFrom(adio_part_config));

  constexpr char kEquipmentSlot[] = "robot";
  google::protobuf::Map<std::string, intrinsic_proto::resources::ResourceHandle>
      resource_handles;
  resource_handles.insert({kEquipmentSlot, resource_handle});

  ASSERT_OK_AND_ASSIGN(
      IconEquipment icon_equipment,
      ConnectToIconEquipment(skills::EquipmentPack(resource_handles),
                             kEquipmentSlot, DefaultChannelFactory(),
                             absl::Seconds(1)));

  EXPECT_EQ(icon_equipment.adio_part_name, "adio");

  // Verify that we can use the returned connection.
  Client icon_client(icon_equipment.channel);
  EXPECT_OK(icon_client.GetConfig());
}

TEST_F(EquipmentUtilsTestFixture, ConnectToIconForceTorqueSensorEquipment) {
  intrinsic_proto::resources::ResourceHandle resource_handle;
  intrinsic_proto::icon::Icon2ForceTorqueSensorPart
      force_torque_sensor_part_config;
  force_torque_sensor_part_config.set_part_name("ft_sensor");
  resource_handle.mutable_connection_info()->mutable_grpc()->set_address(
      absl::StrFormat("localhost:%d", port_));

  ASSERT_TRUE(
      (*resource_handle.mutable_resource_data())[kIcon2ForceTorqueSensorPartKey]
          .mutable_contents()
          ->PackFrom(force_torque_sensor_part_config));

  constexpr char kEquipmentSlot[] = "robot";
  google::protobuf::Map<std::string, intrinsic_proto::resources::ResourceHandle>
      resource_handles;
  resource_handles.insert({kEquipmentSlot, resource_handle});

  ASSERT_OK_AND_ASSIGN(
      IconEquipment icon_equipment,
      ConnectToIconEquipment(skills::EquipmentPack(resource_handles),
                             kEquipmentSlot, DefaultChannelFactory(),
                             absl::Seconds(1)));

  EXPECT_EQ(icon_equipment.force_torque_sensor_part_name, "ft_sensor");

  // Verify that we can use the returned connection.
  Client icon_client(icon_equipment.channel);
  EXPECT_OK(icon_client.GetConfig());
}

TEST_F(EquipmentUtilsTestFixture, ConnectToIconRangefinderEquipment) {
  intrinsic_proto::resources::ResourceHandle resource_handle;
  intrinsic_proto::icon::Icon2RangefinderPart rangefinder_part_config;
  rangefinder_part_config.set_part_name("rangefinder");
  resource_handle.mutable_connection_info()->mutable_grpc()->set_address(
      absl::StrFormat("localhost:%d", port_));

  ASSERT_TRUE(
      (*resource_handle.mutable_resource_data())[kIcon2RangefinderPartKey]
          .mutable_contents()
          ->PackFrom(rangefinder_part_config));

  constexpr char kEquipmentSlot[] = "robot";
  google::protobuf::Map<std::string, intrinsic_proto::resources::ResourceHandle>
      resource_handles;
  resource_handles.insert({kEquipmentSlot, resource_handle});

  ASSERT_OK_AND_ASSIGN(
      IconEquipment icon_equipment,
      ConnectToIconEquipment(skills::EquipmentPack(resource_handles),
                             kEquipmentSlot, DefaultChannelFactory(),
                             absl::Seconds(1)));

  EXPECT_EQ(icon_equipment.rangefinder_part_name, "rangefinder");

  // Verify that we can use the returned connection.
  Client icon_client(icon_equipment.channel);
  EXPECT_OK(icon_client.GetConfig());
}

TEST(EquipmentChannelFakeTest, ConnectToFakeIconEquipment) {
  intrinsic_proto::resources::ResourceHandle resource_handle;
  intrinsic_proto::icon::Icon2PositionPart position_part_config;
  position_part_config.set_part_name("robot_arm");

  resource_handle.mutable_connection_info()->mutable_grpc()->set_address(
      "localhost:1234");  // Ignored by FakeChannelFactory.

  ASSERT_TRUE((*resource_handle.mutable_resource_data())[kIcon2PositionPartKey]
                  .mutable_contents()
                  ->PackFrom(position_part_config));

  constexpr char kEquipmentSlot[] = "robot";
  google::protobuf::Map<std::string, intrinsic_proto::resources::ResourceHandle>
      resource_handles;
  resource_handles.insert({kEquipmentSlot, resource_handle});

  ASSERT_OK_AND_ASSIGN(
      IconEquipment icon_equipment,
      ConnectToIconEquipment(skills::EquipmentPack(resource_handles),
                             kEquipmentSlot, FakeChannelFactory(),
                             absl::Seconds(1)));

  EXPECT_EQ(icon_equipment.position_part_name, "robot_arm");

  // Verify that we can use the returned connection.
  Client icon_client(icon_equipment.channel);
  EXPECT_OK(icon_client.GetConfig());
}

struct ResourceSelectorBuilderTestCase {
  std::string test_name;
  intrinsic_proto::skills::ResourceSelector selector;
  std::string expected_capability_name;
};

using ResourceSelectorBuilderTest =
    ::testing::TestWithParam<ResourceSelectorBuilderTestCase>;

TEST_P(ResourceSelectorBuilderTest, TestContainsEquipment) {
  const ResourceSelectorBuilderTestCase& test_case = GetParam();

  intrinsic_proto::skills::ResourceSelector expected = ParseTextProtoOrDie(
      R"pb(
        capability_names: "intrinsic_proto.icon.v1.IconApi"
      )pb");
  expected.add_capability_names(test_case.expected_capability_name);
  EXPECT_THAT(test_case.selector,
              IgnoringRepeatedFieldOrdering(EqualsProto(expected)));
}

INSTANTIATE_TEST_SUITE_P(
    ResourceSelectorBuilderTests, ResourceSelectorBuilderTest,
    ::testing::ValuesIn<ResourceSelectorBuilderTestCase>(
        {{
             .test_name = "PositionControlledPart",
             .selector = icon::Icon2ResourceSelectorBuilder()
                             .WithPositionControlledPart()
                             .Build(),
             .expected_capability_name = "Icon2PositionPart",
         },
         {
             .test_name = "GripperPart",
             .selector =
                 icon::Icon2ResourceSelectorBuilder().WithGripperPart().Build(),
             .expected_capability_name = "Icon2GripperPart",
         },
         {
             .test_name = "AdioPart",
             .selector =
                 icon::Icon2ResourceSelectorBuilder().WithAdioPart().Build(),
             .expected_capability_name = "Icon2AdioPart",
         },
         {
             .test_name = "ForceTorqueSensorPart",
             .selector = icon::Icon2ResourceSelectorBuilder()
                             .WithForceTorqueSensorPart()
                             .Build(),
             .expected_capability_name = "Icon2ForceTorqueSensorPart",
         },
         {
             .test_name = "ObservationStream",
             .selector = icon::Icon2ResourceSelectorBuilder()
                             .WithObservationStream()
                             .Build(),
             .expected_capability_name = "IconRobotObservationStreamParams",
         },
         {
             .test_name = "RangefinderPart",
             .selector = icon::Icon2ResourceSelectorBuilder()
                             .WithRangefinderPart()
                             .Build(),
             .expected_capability_name = "Icon2RangefinderPart",
         }}),
    [](const ::testing::TestParamInfo<ResourceSelectorBuilderTest::ParamType>&
           info) { return info.param.test_name; });

}  // namespace
}  // namespace icon
}  // namespace intrinsic
