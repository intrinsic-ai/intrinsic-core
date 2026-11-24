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

#include "intrinsic/icon/equipment/resolved_dependency_utils.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "grpcpp/support/status.h"
#include "internal/testing.h"
#include "intrinsic/assets/interface_utils.h"
#include "intrinsic/assets/proto/v1/resolved_dependency.pb.h"
#include "intrinsic/icon/cc_client/client.h"
#include "intrinsic/icon/control/parts/hal/force_torque_sensor_part/hal_force_torque_sensor_part_config.pb.h"
#include "intrinsic/icon/equipment/force_control_settings.pb.h"
#include "intrinsic/icon/proto/v1/service.grpc.pb.h"
#include "intrinsic/icon/proto/v1/types.pb.h"
#include "intrinsic/icon/server/fake_robot_connection.h"
#include "intrinsic/icon/server/testing/fake_part_collection.h"
#include "intrinsic/icon/server/testing/in_process_server.h"
#include "intrinsic/production/external/tsl/net.h"
#include "intrinsic/util/proto/parse_text_proto.h"
#include "intrinsic/world/objects/object_world_client.h"
#include "intrinsic/world/objects/object_world_ids.h"
#include "intrinsic/world/proto/object_world_refs.pb.h"
#include "intrinsic/world/proto/object_world_service.pb.h"
#include "intrinsic/world/proto/object_world_service_mock.grpc.pb.h"

namespace intrinsic {
namespace icon {
namespace {

constexpr char kIconServerName[] = "icon_server";
constexpr double kControlFrequencyHz = 1000;

using ::absl_testing::StatusIs;
using ::testing::_;
using ::testing::DoAll;
using ::testing::ElementsAre;
using ::testing::HasSubstr;
using ::testing::Key;
using ::testing::Pair;
using ::testing::Return;
using ::testing::SetArgPointee;
using ::testing::UnorderedElementsAre;

intrinsic_proto::icon::v1::ServerConfig ServerConfig() {
  intrinsic_proto::icon::v1::ServerConfig server_config;
  server_config.set_name(kIconServerName);
  server_config.set_frequency_hz(kControlFrequencyHz);
  return server_config;
}

// Returns a "HalForceTorqueSensorPart" PartConfig named `name` whose config
// contains force control settings with `excessive_force_threshold`.
intrinsic_proto::icon::v1::PartConfig MakeForceTorqueSensorPartConfig(
    absl::string_view name, double excessive_force_threshold) {
  intrinsic_proto::icon::v1::PartConfig part_config;
  part_config.set_name(name);
  part_config.set_part_type_name(kHalForceTorqueSensorPartTypeName);
  part_config.set_hardware_resource_name(name);
  intrinsic_proto::icon::HalForceTorqueSensorPartConfig ft_config;
  ft_config.mutable_force_control_settings()->set_excessive_force_threshold(
      excessive_force_threshold);
  part_config.mutable_config()->PackFrom(ft_config);
  return part_config;
}

class ResolvedDependencyUtilsTestFixture : public ::testing::Test {
 public:
  ResolvedDependencyUtilsTestFixture()
      : port_(tsl::net::PickUnusedPortOrDie()) {
    StartServerWithConfigs(ServerConfig(), {});
  }

  ~ResolvedDependencyUtilsTestFixture() override {
    server_ = nullptr;
    // RecycleUnusedPort is not available on insrc.
  }

  void StartServerWithConfigs(
      const intrinsic_proto::icon::v1::ServerConfig& config,
      std::vector<intrinsic_proto::icon::v1::PartConfig> part_configs) {
    // Shut down the existing server first so it releases `port_` and stops
    // referencing the old `robot_connection_` before we replace both.
    server_ = nullptr;
    robot_connection_ = std::make_unique<FakeRobotConnection>(
        config, FakePartCollectionWithConfigs(std::move(part_configs)).value());
    server_ = std::make_unique<InProcessApplicationLayerServer>(
        *robot_connection_,
        InProcessApplicationLayerServer::Options{
            .use_in_process_channel = false, .port = port_});
  }

  intrinsic_proto::assets::v1::ResolvedDependency MakeIconResolvedDependency()
      const {
    intrinsic_proto::assets::v1::ResolvedDependency resolved_dependency;
    intrinsic_proto::assets::v1::ResolvedDependency::Interface interface;
    interface.mutable_grpc()->mutable_connection()->set_address(
        absl::StrCat("localhost:", port_));
    auto* metadata = interface.mutable_grpc()
                         ->mutable_connection()
                         ->mutable_metadata()
                         ->Add();
    metadata->set_key("x-resource-instance-name");
    metadata->set_value(kIconServerName);
    resolved_dependency.mutable_interfaces()->insert(
        {absl::StrCat(assets::kGrpcUriPrefix,
                      intrinsic_proto::icon::v1::IconApi::service_full_name()),
         interface});
    return resolved_dependency;
  }

  int port_;
  std::unique_ptr<FakeRobotConnection> robot_connection_;
  std::unique_ptr<InProcessApplicationLayerServer> server_;
};

TEST_F(ResolvedDependencyUtilsTestFixture,
       ConnectToIconResolvedDependencyErrorsNoIconInterface) {
  intrinsic_proto::assets::v1::ResolvedDependency resolved_dependency;
  EXPECT_TRUE(absl::IsNotFound(
      ConnectToIconResolvedDependency(resolved_dependency, absl::Seconds(1))
          .status()));
}

TEST_F(ResolvedDependencyUtilsTestFixture, ConnectToIconResolvedDependency) {
  const auto resolved_dependency = MakeIconResolvedDependency();

  std::vector<intrinsic_proto::icon::v1::PartConfig> part_configs = {
      ParseTextProtoOrDie(R"pb(
        name: "robot_arm_2"
        part_type_name: "HalArmPart"
        hardware_resource_name: "robot_arm_2"
      )pb"),
      ParseTextProtoOrDie(R"pb(
        name: "robot_arm_1"
        part_type_name: "HalArmPart"
        hardware_resource_name: "robot_arm_1"
      )pb"),
      ParseTextProtoOrDie(R"pb(
        name: "gripper_1"
        part_type_name: "HalLinearGripperPart"
        hardware_resource_name: "gripper_1"
      )pb"),
      ParseTextProtoOrDie(R"pb(
        name: "adio_1"
        part_type_name: "HalADIOPart"
        hardware_resource_name: "adio_1"
      )pb"),
      MakeForceTorqueSensorPartConfig("ft_sensor_1",
                                      /*excessive_force_threshold=*/42.0),
      ParseTextProtoOrDie(R"pb(
        name: "rangefinder_1"
        part_type_name: "HalRangefinderPart"
        hardware_resource_name: "rangefinder_1"
      )pb"),
      ParseTextProtoOrDie(R"pb(
        name: "custom_part_1"
        part_type_name: "SomeOtherPart"
        hardware_resource_name: "custom_part_1"
      )pb"),
  };
  StartServerWithConfigs(ServerConfig(), std::move(part_configs));

  ASSERT_OK_AND_ASSIGN(
      IconResolvedDependency icon_resolved_dependency,
      ConnectToIconResolvedDependency(resolved_dependency, absl::Seconds(1)));

  EXPECT_THAT(
      icon_resolved_dependency.parts,
      UnorderedElementsAre(
          Pair("HalArmPart",
               UnorderedElementsAre(Pair("robot_arm_1", "robot_arm_1"),
                                    Pair("robot_arm_2", "robot_arm_2"))),
          Pair("HalLinearGripperPart",
               UnorderedElementsAre(Pair("gripper_1", "gripper_1"))),
          Pair("HalADIOPart", UnorderedElementsAre(Pair("adio_1", "adio_1"))),
          Pair("HalForceTorqueSensorPart",
               UnorderedElementsAre(Pair("ft_sensor_1", "ft_sensor_1"))),
          Pair("HalRangefinderPart",
               UnorderedElementsAre(Pair("rangefinder_1", "rangefinder_1"))),
          Pair("SomeOtherPart",
               UnorderedElementsAre(Pair("custom_part_1", "custom_part_1")))));
  EXPECT_THAT(
      icon_resolved_dependency.part_configs,
      UnorderedElementsAre(Key("robot_arm_1"), Key("robot_arm_2"),
                           Key("gripper_1"), Key("adio_1"), Key("ft_sensor_1"),
                           Key("rangefinder_1"), Key("custom_part_1")));
  ASSERT_OK_AND_ASSIGN(
      intrinsic_proto::icon::ForceControlSettings force_control_settings,
      icon_resolved_dependency.GetForceControlSettings("ft_sensor_1"));
  EXPECT_EQ(force_control_settings.excessive_force_threshold(), 42.0);

  // Verify that we can use the returned connection.
  Client icon_client(icon_resolved_dependency.channel);
  EXPECT_OK(icon_client.GetConfig());
}

TEST_F(ResolvedDependencyUtilsTestFixture,
       ConnectToIconResolvedDependencyErrorsEmptyPartTypeName) {
  const auto resolved_dependency = MakeIconResolvedDependency();

  std::vector<intrinsic_proto::icon::v1::PartConfig> part_configs = {
      ParseTextProtoOrDie(R"pb(
        name: "robot_arm_1"
        hardware_resource_name: "robot_arm_1"
      )pb"),
  };
  StartServerWithConfigs(ServerConfig(), std::move(part_configs));

  EXPECT_TRUE(absl::IsInternal(
      ConnectToIconResolvedDependency(resolved_dependency, absl::Seconds(1))
          .status()));
}

TEST_F(ResolvedDependencyUtilsTestFixture,
       ConnectToIconResolvedDependencyErrorsEmptyHardwareResourceName) {
  const auto resolved_dependency = MakeIconResolvedDependency();

  std::vector<intrinsic_proto::icon::v1::PartConfig> part_configs = {
      ParseTextProtoOrDie(R"pb(
        name: "robot_arm_1" part_type_name: "HalArmPart"
      )pb"),
  };
  StartServerWithConfigs(ServerConfig(), std::move(part_configs));

  EXPECT_TRUE(absl::IsInternal(
      ConnectToIconResolvedDependency(resolved_dependency, absl::Seconds(1))
          .status()));
}

intrinsic_proto::world::Object MakeKinematicObjectProto(
    absl::string_view name) {
  intrinsic_proto::world::Object obj;
  obj.set_name(name);
  obj.set_type(intrinsic_proto::world::ObjectType::KINEMATIC_OBJECT);
  obj.mutable_object_component()
      ->mutable_parent_t_this()
      ->mutable_orientation()
      ->set_w(1.0);
  obj.mutable_kinematic_object_component();
  return obj;
}

TEST(GetPositionPartAndKinematicObjectTest, SinglePartFallback) {
  auto mock_world_stub =
      std::make_shared<intrinsic_proto::world::MockObjectWorldServiceStub>();
  EXPECT_CALL(*mock_world_stub, GetObject(_, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(MakeKinematicObjectProto("my_robot_1")),
                      Return(grpc::Status::OK)));
  world::ObjectWorldClient world("world_id", mock_world_stub);

  IconResolvedDependency icon_resolved_dependency;
  icon_resolved_dependency.parts = {
      {"HalArmPart", {{"robot_arm_1", "my_robot_1"}}}};

  ASSERT_OK_AND_ASSIGN(
      PositionPartWithObject result,
      icon_resolved_dependency.GetPositionPartAndKinematicObject(world));
  EXPECT_EQ(result.part_name, "robot_arm_1");
  EXPECT_EQ(result.kinematic_object.Name(), WorldObjectName("my_robot_1"));
}

TEST(GetPositionPartAndKinematicObjectTest, SelectsMatchingArmPart) {
  auto mock_world_stub =
      std::make_shared<intrinsic_proto::world::MockObjectWorldServiceStub>();
  EXPECT_CALL(*mock_world_stub, GetObject(_, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(MakeKinematicObjectProto("my_robot_2")),
                      Return(grpc::Status::OK)));
  world::ObjectWorldClient world("world_id", mock_world_stub);

  IconResolvedDependency icon_resolved_dependency;
  icon_resolved_dependency.parts = {{"HalArmPart",
                                     {
                                         {"robot_arm_1", "my_robot_1"},
                                         {"robot_arm_2", "my_robot_2"},
                                     }}};

  intrinsic_proto::world::ObjectReference arm_part;
  arm_part.mutable_by_name()->set_object_name("my_robot_2");

  ASSERT_OK_AND_ASSIGN(
      PositionPartWithObject result,
      icon_resolved_dependency.GetPositionPartAndKinematicObject(world,
                                                                 arm_part));
  EXPECT_EQ(result.part_name, "robot_arm_2");
  EXPECT_EQ(result.kinematic_object.Name(), WorldObjectName("my_robot_2"));
}

TEST(GetPositionPartAndKinematicObjectTest, ErrorsWhenEmptyPositionParts) {
  auto mock_world_stub =
      std::make_shared<intrinsic_proto::world::MockObjectWorldServiceStub>();
  world::ObjectWorldClient world("world_id", mock_world_stub);

  IconResolvedDependency icon_resolved_dependency;

  EXPECT_THAT(
      icon_resolved_dependency.GetPositionPartAndKinematicObject(world),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("The given ICON instance does not have any position "
                         "parts")));
}

TEST(GetPositionPartAndKinematicObjectTest,
     ErrorsWhenMultiplePositionPartsAndNoArmPart) {
  auto mock_world_stub =
      std::make_shared<intrinsic_proto::world::MockObjectWorldServiceStub>();
  world::ObjectWorldClient world("world_id", mock_world_stub);

  IconResolvedDependency icon_resolved_dependency;
  icon_resolved_dependency.parts = {{"HalArmPart",
                                     {
                                         {"robot_arm_1", "my_robot_1"},
                                         {"robot_arm_2", "my_robot_2"},
                                     }}};

  EXPECT_THAT(
      icon_resolved_dependency.GetPositionPartAndKinematicObject(world),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Available (part_name, object_name) pairs for the "
                         "selected ICON instance are: (robot_arm_1, "
                         "my_robot_1), (robot_arm_2, my_robot_2).")));
}

TEST(GetPositionPartAndKinematicObjectTest,
     ErrorsWhenArmPartNotInPositionParts) {
  auto mock_world_stub =
      std::make_shared<intrinsic_proto::world::MockObjectWorldServiceStub>();
  EXPECT_CALL(*mock_world_stub, GetObject(_, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(MakeKinematicObjectProto("other_robot")),
                      Return(grpc::Status::OK)));
  world::ObjectWorldClient world("world_id", mock_world_stub);

  IconResolvedDependency icon_resolved_dependency;
  icon_resolved_dependency.parts = {{"HalArmPart",
                                     {
                                         {"robot_arm_1", "my_robot_1"},
                                         {"robot_arm_2", "my_robot_2"},
                                     }}};

  intrinsic_proto::world::ObjectReference arm_part;
  arm_part.mutable_by_name()->set_object_name("other_robot");

  EXPECT_THAT(
      icon_resolved_dependency.GetPositionPartAndKinematicObject(world,
                                                                 arm_part),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Could not find a position part corresponding to "
                         "object with name 'other_robot'. Please check you "
                         "have the right object and ICON instance selected. "
                         "Available (part_name, object_name) pairs for the "
                         "selected ICON instance are: (robot_arm_1, "
                         "my_robot_1), (robot_arm_2, my_robot_2).")));
}

TEST(GetAdioPartNamesTest, ReturnsSortedAdioPartNames) {
  IconResolvedDependency icon_resolved_dependency;
  icon_resolved_dependency.parts = {
      {"HalADIOPart",
       {
           {"adio_2", "adio_2"},
           {"adio_1", "adio_1"},
       }},
      // Parts of other types must not be returned.
      {"HalArmPart", {{"robot_arm", "my_robot"}}},
  };

  ASSERT_OK_AND_ASSIGN(std::vector<std::string> adio_parts,
                       icon_resolved_dependency.GetAdioPartNames());
  EXPECT_THAT(adio_parts, ElementsAre("adio_1", "adio_2"));
}

TEST(GetAdioPartNamesTest, ErrorsWhenEmptyAdioParts) {
  IconResolvedDependency icon_resolved_dependency;

  EXPECT_THAT(icon_resolved_dependency.GetAdioPartNames(),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("The given ICON instance does not have any "
                                 "ADIO parts")));
}

TEST(GetGripperPartNameTest, ReturnsGripperPartName) {
  IconResolvedDependency icon_resolved_dependency;
  icon_resolved_dependency.parts = {
      {"HalLinearGripperPart", {{"gripper_1", "my_gripper_1"}}}};

  ASSERT_OK_AND_ASSIGN(std::string part_name,
                       icon_resolved_dependency.GetGripperPartName());
  EXPECT_EQ(part_name, "gripper_1");
}

TEST(GetGripperPartNameTest, ErrorsWhenEmptyGripperParts) {
  IconResolvedDependency icon_resolved_dependency;

  EXPECT_THAT(icon_resolved_dependency.GetGripperPartName(),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("The given ICON instance does not have any "
                                 "gripper parts.")));
}

TEST(GetGripperPartNameTest, ErrorsWhenMultipleGripperParts) {
  IconResolvedDependency icon_resolved_dependency;
  icon_resolved_dependency.parts = {{"HalLinearGripperPart",
                                     {
                                         {"gripper_1", "my_gripper_1"},
                                         {"gripper_2", "my_gripper_2"},
                                     }}};

  EXPECT_THAT(
      icon_resolved_dependency.GetGripperPartName(),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("The given ICON instance has multiple gripper parts. "
                         "Please specify which gripper part to use. Available "
                         "gripper parts for the selected ICON instance are: "
                         "gripper_1, gripper_2.")));
}

TEST(GetAdioPartNameTest, ReturnsAdioPartName) {
  IconResolvedDependency icon_resolved_dependency;
  icon_resolved_dependency.parts = {{"HalADIOPart", {{"adio_1", "my_adio_1"}}}};

  ASSERT_OK_AND_ASSIGN(std::string part_name,
                       icon_resolved_dependency.GetAdioPartName());
  EXPECT_EQ(part_name, "adio_1");
}

TEST(GetAdioPartNameTest, ErrorsWhenEmptyAdioParts) {
  IconResolvedDependency icon_resolved_dependency;

  EXPECT_THAT(icon_resolved_dependency.GetAdioPartName(),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("The given ICON instance does not have any "
                                 "ADIO parts.")));
}

TEST(GetAdioPartNameTest, ErrorsWhenMultipleAdioParts) {
  IconResolvedDependency icon_resolved_dependency;
  icon_resolved_dependency.parts = {{"HalADIOPart",
                                     {
                                         {"adio_1", "my_adio_1"},
                                         {"adio_2", "my_adio_2"},
                                     }}};

  EXPECT_THAT(
      icon_resolved_dependency.GetAdioPartName(),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("The given ICON instance has multiple ADIO parts. "
                         "Please specify which ADIO part to use. Available "
                         "ADIO parts for the selected ICON instance are: "
                         "adio_1, adio_2.")));
}

TEST(GetForceTorqueSensorPartNameTest, ReturnsForceTorqueSensorPartName) {
  IconResolvedDependency icon_resolved_dependency;
  icon_resolved_dependency.parts = {
      {"HalForceTorqueSensorPart", {{"ft_sensor_1", "my_ft_sensor_1"}}}};

  ASSERT_OK_AND_ASSIGN(std::string part_name,
                       icon_resolved_dependency.GetForceTorqueSensorPartName());
  EXPECT_EQ(part_name, "ft_sensor_1");
}

TEST(GetForceTorqueSensorPartNameTest, ErrorsWhenEmptyForceTorqueSensorParts) {
  IconResolvedDependency icon_resolved_dependency;

  EXPECT_THAT(icon_resolved_dependency.GetForceTorqueSensorPartName(),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("The given ICON instance does not have any "
                                 "force-torque sensor parts.")));
}

TEST(GetForceTorqueSensorPartNameTest,
     ErrorsWhenMultipleForceTorqueSensorParts) {
  IconResolvedDependency icon_resolved_dependency;
  icon_resolved_dependency.parts = {{"HalForceTorqueSensorPart",
                                     {
                                         {"ft_sensor_1", "my_ft_sensor_1"},
                                         {"ft_sensor_2", "my_ft_sensor_2"},
                                     }}};

  EXPECT_THAT(
      icon_resolved_dependency.GetForceTorqueSensorPartName(),
      StatusIs(
          absl::StatusCode::kInvalidArgument,
          HasSubstr("The given ICON instance has multiple force-torque sensor "
                    "parts. Please specify which force-torque sensor part to "
                    "use. Available force-torque sensor parts for the selected "
                    "ICON instance are: ft_sensor_1, ft_sensor_2.")));
}

TEST(GetRangefinderPartNameTest, ReturnsRangefinderPartName) {
  IconResolvedDependency icon_resolved_dependency;
  icon_resolved_dependency.parts = {
      {"HalRangefinderPart", {{"rangefinder_1", "my_rangefinder_1"}}}};

  ASSERT_OK_AND_ASSIGN(std::string part_name,
                       icon_resolved_dependency.GetRangefinderPartName());
  EXPECT_EQ(part_name, "rangefinder_1");
}

TEST(GetRangefinderPartNameTest, ErrorsWhenEmptyRangefinderParts) {
  IconResolvedDependency icon_resolved_dependency;

  EXPECT_THAT(icon_resolved_dependency.GetRangefinderPartName(),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("The given ICON instance does not have any "
                                 "rangefinder parts.")));
}

TEST(GetRangefinderPartNameTest, ErrorsWhenMultipleRangefinderParts) {
  IconResolvedDependency icon_resolved_dependency;
  icon_resolved_dependency.parts = {{"HalRangefinderPart",
                                     {
                                         {"rangefinder_1", "my_rangefinder_1"},
                                         {"rangefinder_2", "my_rangefinder_2"},
                                     }}};

  EXPECT_THAT(
      icon_resolved_dependency.GetRangefinderPartName(),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("The given ICON instance has multiple rangefinder "
                         "parts. Please specify which rangefinder part to use. "
                         "Available rangefinder parts for the selected ICON "
                         "instance are: rangefinder_1, rangefinder_2.")));
}

TEST(GetForceControlSettingsTest, ReturnsForceControlSettings) {
  IconResolvedDependency icon_resolved_dependency;
  icon_resolved_dependency.part_configs = {
      {"ft_sensor_1",
       MakeForceTorqueSensorPartConfig("ft_sensor_1",
                                       /*excessive_force_threshold=*/10.0)},
      {"ft_sensor_2",
       MakeForceTorqueSensorPartConfig("ft_sensor_2",
                                       /*excessive_force_threshold=*/20.0)},
  };

  ASSERT_OK_AND_ASSIGN(
      intrinsic_proto::icon::ForceControlSettings force_control_settings,
      icon_resolved_dependency.GetForceControlSettings("ft_sensor_2"));
  EXPECT_EQ(force_control_settings.excessive_force_threshold(), 20.0);
}

TEST(GetForceControlSettingsTest, ErrorsWhenPartNotFound) {
  IconResolvedDependency icon_resolved_dependency;

  EXPECT_THAT(
      icon_resolved_dependency.GetForceControlSettings("other_part"),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Could not find a part with name 'other_part'. The "
                         "selected ICON instance does not have any "
                         "force-torque sensor parts.")));
}

TEST(GetForceControlSettingsTest,
     ErrorsWhenPartNotFoundListsOnlyCompatibleParts) {
  IconResolvedDependency icon_resolved_dependency;
  icon_resolved_dependency.part_configs = {
      {"ft_sensor_2",
       MakeForceTorqueSensorPartConfig("ft_sensor_2",
                                       /*excessive_force_threshold=*/20.0)},
      {"ft_sensor_1",
       MakeForceTorqueSensorPartConfig("ft_sensor_1",
                                       /*excessive_force_threshold=*/10.0)},
      {"robot_arm", ParseTextProtoOrDie(R"pb(name: "robot_arm")pb")},
  };

  EXPECT_THAT(
      icon_resolved_dependency.GetForceControlSettings("other_part"),
      StatusIs(
          absl::StatusCode::kInvalidArgument,
          HasSubstr("Could not find a part with name 'other_part'. Please "
                    "check you have the right part name and ICON instance "
                    "selected. Available force-torque sensor parts for the "
                    "selected ICON instance are: ft_sensor_1, ft_sensor_2.")));
}

TEST(GetForceControlSettingsTest,
     ErrorsWhenConfigIsNotForceTorqueSensorPartConfig) {
  intrinsic_proto::icon::v1::PartConfig arm_config;
  arm_config.set_name("robot_arm");
  arm_config.mutable_config()->PackFrom(
      intrinsic_proto::icon::ForceControlSettings());
  IconResolvedDependency icon_resolved_dependency;
  icon_resolved_dependency.part_configs = {{"robot_arm", arm_config}};

  EXPECT_THAT(
      icon_resolved_dependency.GetForceControlSettings("robot_arm"),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Part 'robot_arm' does not have a "
                         "HalForceTorqueSensorPartConfig (config type URL: "
                         "'type.googleapis.com/"
                         "intrinsic_proto.icon.ForceControlSettings'). Please "
                         "check that you selected a force-torque sensor "
                         "part.")));
}

}  // namespace
}  // namespace icon
}  // namespace intrinsic
