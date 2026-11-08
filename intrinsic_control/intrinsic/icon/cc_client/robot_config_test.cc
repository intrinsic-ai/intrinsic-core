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

#include "intrinsic/icon/cc_client/robot_config.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <string>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "google/protobuf/any.pb.h"
#include "google/protobuf/wrappers.pb.h"
#include "internal/testing.h"
#include "intrinsic/icon/proto/v1/service.pb.h"
#include "intrinsic/icon/proto/v1/types.pb.h"
#include "protobuf-matchers/protocol-buffer-matchers.h"

namespace intrinsic {
namespace icon {
namespace {

using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;
using ::protobuf_matchers::EqualsProto;
using ::testing::UnorderedElementsAre;

TEST(RobotConfig, GetGenericPartConfigFailsPartNotFound) {
  intrinsic_proto::icon::v1::GetConfigResponse config_proto;
  EXPECT_THAT(RobotConfig(config_proto).GetGenericPartConfig("not_found"),
              StatusIs(absl::StatusCode::kNotFound));
}

TEST(RobotConfig, GetPartConfigFailsPartNotFound) {
  intrinsic_proto::icon::v1::GetConfigResponse config_proto;
  EXPECT_THAT(RobotConfig(config_proto)
                  .GetPartConfig<google::protobuf::FloatValue>("not_found"),
              StatusIs(absl::StatusCode::kNotFound));
}

TEST(RobotConfig, GetPartConfigFailsWrongAnyType) {
  intrinsic_proto::icon::v1::GetConfigResponse config_proto;
  intrinsic_proto::icon::v1::PartConfig* part_config =
      config_proto.add_part_configs();
  part_config->set_name("robot_arm");
  ASSERT_TRUE(
      part_config->mutable_config()->PackFrom(google::protobuf::FloatValue()));
  EXPECT_THAT(RobotConfig(config_proto)
                  .GetPartConfig<google::protobuf::Int32Value>("robot_arm"),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(RobotConfig, GetGenericPartConfigSucceeds) {
  intrinsic_proto::icon::v1::GetConfigResponse config_proto;
  intrinsic_proto::icon::v1::PartConfig* part_config =
      config_proto.add_part_configs();
  part_config->set_name("robot_arm");
  part_config->mutable_generic_config()
      ->mutable_joint_position_config()
      ->set_num_joints(32);
  ASSERT_OK_AND_ASSIGN(
      intrinsic_proto::icon::GenericPartConfig generic_config,
      RobotConfig(config_proto).GetGenericPartConfig("robot_arm"));
  EXPECT_THAT(generic_config, EqualsProto(part_config->generic_config()));
}

TEST(RobotConfig, GetPartConfigSucceeds) {
  intrinsic_proto::icon::v1::GetConfigResponse config_proto;
  intrinsic_proto::icon::v1::PartConfig* part_config =
      config_proto.add_part_configs();
  part_config->set_name("robot_arm");
  google::protobuf::Int32Value value;
  value.set_value(123);
  ASSERT_TRUE(part_config->mutable_config()->PackFrom(value));
  ASSERT_OK_AND_ASSIGN(
      google::protobuf::Int32Value result_proto,
      RobotConfig(config_proto)
          .GetPartConfig<google::protobuf::Int32Value>("robot_arm"));
  EXPECT_THAT(value, EqualsProto(result_proto));
}

TEST(RobotConfig, GetHardwareResourceNameSucceeds) {
  intrinsic_proto::icon::v1::GetConfigResponse config_proto;
  intrinsic_proto::icon::v1::PartConfig* part_config =
      config_proto.add_part_configs();
  part_config->set_name("robot_arm");
  part_config->set_hardware_resource_name("robot_object");
  ASSERT_OK_AND_ASSIGN(
      std::string hardware_resource_name,
      RobotConfig(config_proto).GetHardwareResourceName("robot_arm"));
  EXPECT_EQ(hardware_resource_name, "robot_object");
}

TEST(RobotConfig, GetControlFrequency) {
  intrinsic_proto::icon::v1::GetConfigResponse config_proto;
  config_proto.set_control_frequency_hz(100);
  EXPECT_EQ(RobotConfig(config_proto).GetControlFrequency(),
            config_proto.control_frequency_hz());
}

TEST(RobotConfig, GetPartFeatureInterfacesFailsPartNotFound) {
  intrinsic_proto::icon::v1::GetConfigResponse config_proto;
  EXPECT_THAT(RobotConfig(config_proto).GetPartFeatureInterfaces("not_found"),
              StatusIs(absl::StatusCode::kNotFound));
}

TEST(RobotConfig, GetPartFeatureInterfacesSucceeds) {
  intrinsic_proto::icon::v1::GetConfigResponse config_proto;
  intrinsic_proto::icon::v1::PartConfig* part_config =
      config_proto.add_part_configs();
  part_config->set_name("robot_arm");
  part_config->add_feature_interfaces(
      intrinsic_proto::icon::v1::FEATURE_INTERFACE_JOINT_POSITION);
  part_config->add_feature_interfaces(
      intrinsic_proto::icon::v1::FEATURE_INTERFACE_MOVE_OK);
  EXPECT_THAT(RobotConfig(config_proto).GetPartFeatureInterfaces("robot_arm"),
              IsOkAndHolds(UnorderedElementsAre(
                  intrinsic_proto::icon::v1::FEATURE_INTERFACE_JOINT_POSITION,
                  intrinsic_proto::icon::v1::FEATURE_INTERFACE_MOVE_OK)));
}

TEST(RobotConfig, GetServerName) {
  intrinsic_proto::icon::v1::GetConfigResponse config_proto;
  config_proto.mutable_server_config()->set_name("calculon");
  EXPECT_EQ(RobotConfig(config_proto).GetServerName(),
            config_proto.server_config().name());
}

}  // namespace
}  // namespace icon
}  // namespace intrinsic
