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
using ::testing::HasSubstr;
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
      ParseTextProtoOrDie(R"pb(
        name: "ft_sensor_1"
        part_type_name: "HalForceTorqueSensorPart"
        hardware_resource_name: "ft_sensor_1"
      )pb"),
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

}  // namespace
}  // namespace icon
}  // namespace intrinsic
