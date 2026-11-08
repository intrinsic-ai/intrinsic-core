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

#include "intrinsic/icon/cc_client/testing/rtcl_controller_fake_robot_connection.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstddef>
#include <memory>
#include <string>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "internal/testing.h"
#include "intrinsic/icon/actions/action_utils.h"
#include "intrinsic/icon/cc_client/condition.h"
#include "intrinsic/icon/cc_client/operational_status.h"
#include "intrinsic/icon/cc_client/testing/channel_fake.h"
#include "intrinsic/icon/cc_client/testing/rtcl_controller_channel_fake.h"
#include "intrinsic/icon/cc_client/testing/rtcl_controller_fake_session.h"
#include "intrinsic/icon/cc_client/testing/rtcl_readonly_parts.h"
#include "intrinsic/icon/common/builtins.h"
#include "intrinsic/icon/common/id_types.h"
#include "intrinsic/icon/common/part_properties.h"
#include "intrinsic/icon/common/slot_part_map.h"
#include "intrinsic/icon/control/action_factory_context.h"
#include "intrinsic/icon/control/actions/rtcl_joint_stop_action.h"
#include "intrinsic/icon/control/actions/test_helpers.h"
#include "intrinsic/icon/control/logging_mode.h"
#include "intrinsic/icon/control/mock_rtcl_action.h"
#include "intrinsic/icon/control/parts/testing/fake_realtime_part.h"
#include "intrinsic/icon/control/parts/testing/position_loopback_fake_part.h"
#include "intrinsic/icon/control/rtcl_action_factory_registry.h"
#include "intrinsic/icon/proto/part_status.pb.h"
#include "intrinsic/icon/proto/v1/service.pb.h"
#include "intrinsic/icon/proto/v1/types.pb.h"
#include "intrinsic/icon/server/session_interface.h"
#include "intrinsic/util/proto/parse_text_proto.h"
#include "protobuf-matchers/protocol-buffer-matchers.h"

namespace intrinsic::icon {
namespace {

using ::absl_testing::StatusIs;
using ::intrinsic::ParseTextProtoOrDie;
using ::protobuf_matchers::EqualsProto;
using ::protobuf_matchers::proto::IgnoringRepeatedFieldOrdering;
using ::testing::AllOf;
using ::testing::ElementsAre;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::Pair;
using ::testing::UnorderedElementsAre;
using ::testing::UnorderedElementsAreArray;

constexpr char kPartName[] = "part";
constexpr char kOptionalPartName[] = "optional_part";
constexpr char kIncompatiblePartName[] = "incompatible_part";
constexpr char kSlotName[] = "slot";
constexpr char kOptionalSlotName[] = "optional_slot";
constexpr char kMockActionTypeName[] = "mock_action";
constexpr size_t kNdof = 6;
constexpr ActionInstanceId kActionInstanceId(34);

TEST(RtclControllerFakeRobotConnection, Create) {
  EXPECT_OK(RtclControllerFakeRobotConnection::Create(
      RtclControllerChannelFake::kDefaultServerName,
      RtclControllerFakeSession::ValidationMode::kNice,
      intrinsic_proto::icon::v1::GetStatusResponse(),
      /*part_configs=*/
      {PartConfigFromPart(kPartName, PositionLoopbackFakePart(kNdof))},
      {SessionWill().OnAction(
          kActionInstanceId,
          ActionWill().ReportStateVariables(
              absl::flat_hash_map<std::string, StateVariableValue>{
                  {"henlo", true}}))}));
}

TEST(RtclControllerFakeRobotConnection, SpeedOverride) {
  ASSERT_OK_AND_ASSIGN(
      auto fake_connection,
      RtclControllerFakeRobotConnection::Create(
          RtclControllerChannelFake::kDefaultServerName,
          RtclControllerFakeSession::ValidationMode::kNice,
          intrinsic_proto::icon::v1::GetStatusResponse(),
          /*part_configs=*/
          {PartConfigFromPart(kPartName, PositionLoopbackFakePart(kNdof))},
          {}));
  EXPECT_EQ(fake_connection->GetSpeedOverride(), 1.0);
  fake_connection->SetSpeedOverride(0.75);
  EXPECT_EQ(fake_connection->GetSpeedOverride(), 0.75);
}

TEST(RtclControllerFakeRobotConnection, SetLoggingMode) {
  ASSERT_OK_AND_ASSIGN(
      auto fake_connection,
      RtclControllerFakeRobotConnection::Create(
          RtclControllerChannelFake::kDefaultServerName,
          RtclControllerFakeSession::ValidationMode::kNice,
          intrinsic_proto::icon::v1::GetStatusResponse(),
          /*part_configs=*/
          {PartConfigFromPart(kPartName, PositionLoopbackFakePart(kNdof))},
          {}));
  EXPECT_EQ(fake_connection->GetLoggingMode(), LoggingMode::kThrottled);
  fake_connection->SetLoggingMode(LoggingMode::kFullRate);
  EXPECT_EQ(fake_connection->GetLoggingMode(), LoggingMode::kFullRate);
}

TEST(RtclControllerFakeRobotConnection, CannotStartUnexpectedSessionIfStrict) {
  ASSERT_OK_AND_ASSIGN(
      auto fake_connection,
      RtclControllerFakeRobotConnection::Create(
          RtclControllerChannelFake::kDefaultServerName,
          RtclControllerFakeSession::ValidationMode::kStrict,
          intrinsic_proto::icon::v1::GetStatusResponse(),
          /*part_configs=*/
          {PartConfigFromPart(kPartName, PositionLoopbackFakePart(kNdof))},
          /*session_behaviors=*/{}));

  EXPECT_THAT(fake_connection->CreateSession({kPartName}, SessionId(9238), {}),
              StatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST(RtclControllerFakeRobotConnection, CanStartUnexpectedSessionIfNice) {
  ASSERT_OK_AND_ASSIGN(
      auto fake_connection,
      RtclControllerFakeRobotConnection::Create(
          RtclControllerChannelFake::kDefaultServerName,
          RtclControllerFakeSession::ValidationMode::kNice,
          intrinsic_proto::icon::v1::GetStatusResponse(),
          /*part_configs=*/
          {PartConfigFromPart(kPartName, PositionLoopbackFakePart(kNdof))},
          /*session_behaviors=*/{}));

  EXPECT_OK(fake_connection->CreateSession({kPartName}, SessionId(9238), {}));
}

TEST(RtclControllerFakeRobotConnection,
     ExpectedSessionExecutesActionBehaviors) {
  GetGlobalRtclActionFactoryRegistry().ClearForTestingOnly();
  ASSERT_TRUE(GetGlobalRtclActionFactoryRegistry().RegisterNoParameters(
      kStopAction,
      [](ActionFactoryContext& ctx) {
        return std::make_unique<MockRtclAction>();
      },
      JointStopAction::GetStopSignature()));

  const intrinsic_proto::icon::v1::GetStatusResponse initial_robot_status =
      ParseTextProtoOrDie(R"pb(
        part_status {
          key: "part"
          value { timestamp_ns: 1 }
        }
      )pb");

  const intrinsic_proto::icon::v1::GetStatusResponse expected_status =
      ParseTextProtoOrDie(R"pb(
        part_status {
          key: "part"
          value { timestamp_ns: 100 }
        }
      )pb");

  ASSERT_OK_AND_ASSIGN(
      auto fake_connection,
      RtclControllerFakeRobotConnection::Create(
          RtclControllerChannelFake::kDefaultServerName,
          RtclControllerFakeSession::ValidationMode::kNice,
          initial_robot_status,
          /*part_configs=*/
          {PartConfigFromPart(kPartName, PositionLoopbackFakePart(kNdof))},
          {SessionWill().OnAction(
              kActionInstanceId,
              ActionWill().SetRobotStatus(expected_status))}));
  {
    ASSERT_OK_AND_ASSIGN(
        OperationalStatus operational_status,
        fake_connection->MutableOperationalStateInterface().GetStatus());
    EXPECT_TRUE(IsEnabled(operational_status));
  }
  {
    ASSERT_OK_AND_ASSIGN(
        (absl::flat_hash_map<std::string, intrinsic_proto::icon::PartStatus>
             part_status_map),
        fake_connection->GetPartCollection().GetPartStatuses(
            absl::InfiniteFuture()));
    // Expect the initial status we passed to the fake RobotConnection.
    EXPECT_THAT(part_status_map.at(kPartName),
                EqualsProto(initial_robot_status.part_status().at(kPartName)));
  }

  ASSERT_OK_AND_ASSIGN(auto session, fake_connection->CreateSession(
                                         {kPartName}, SessionId(9238), {}));
  // Add a stop action, since that action type does not need parameters.
  EXPECT_OK(session->AddActionsAndReactions(
      ActionsAndReactions{.action_descriptions = {ActionDescription{
                              .id = kActionInstanceId,
                              .slot_part_map = {{kStopPartSlot, kPartName}},
                              .action_type_name = kStopAction,
                          }}}));

  EXPECT_OK(session->StartActions({kActionInstanceId},
                                  /*stop_active_actions=*/true));
  // Poll reactions once to trigger the ActionWill() behavior.
  EXPECT_OK(session->PollReactions());

  {
    ASSERT_OK_AND_ASSIGN(
        (absl::flat_hash_map<std::string, intrinsic_proto::icon::PartStatus>
             part_status_map),
        fake_connection->GetPartCollection().GetPartStatuses(
            absl::InfiniteFuture()));
    // Now we expect the updated status from the ActionWill behavior.
    EXPECT_THAT(part_status_map.at(kPartName),
                EqualsProto(expected_status.part_status().at(kPartName)));
  }
}

TEST(RtclControllerFakeRobotConnection,
     CannotAddActionForPartNotControlledBySession) {
  GetGlobalRtclActionFactoryRegistry().ClearForTestingOnly();
  ASSERT_TRUE(GetGlobalRtclActionFactoryRegistry().RegisterNoParameters(
      kStopAction,
      [](ActionFactoryContext& ctx) {
        return std::make_unique<MockRtclAction>();
      },
      JointStopAction::GetStopSignature()));

  const intrinsic_proto::icon::v1::GetStatusResponse initial_robot_status =
      ParseTextProtoOrDie(R"pb(
        part_status {
          key: "part"
          value { timestamp_ns: 1 }
        }
        part_status {
          key: "part2"
          value { timestamp_ns: 1 }
        }
      )pb");

  const std::string kPartName2 = "part2";

  ASSERT_OK_AND_ASSIGN(
      auto fake_connection,
      RtclControllerFakeRobotConnection::Create(
          RtclControllerChannelFake::kDefaultServerName,
          RtclControllerFakeSession::ValidationMode::kNice,
          initial_robot_status,
          /*part_configs=*/
          {PartConfigFromPart(kPartName, PositionLoopbackFakePart(kNdof)),
           PartConfigFromPart(kPartName2, PositionLoopbackFakePart(kNdof))},
          {}));
  {
    ASSERT_OK_AND_ASSIGN(
        OperationalStatus operational_status,
        fake_connection->MutableOperationalStateInterface().GetStatus());
    EXPECT_TRUE(IsEnabled(operational_status));
  }
  {
    ASSERT_OK_AND_ASSIGN(
        (absl::flat_hash_map<std::string, intrinsic_proto::icon::PartStatus>
             part_status_map),
        fake_connection->GetPartCollection().GetPartStatuses(
            absl::InfiniteFuture()));
    // Expect the initial status we passed to the fake RobotConnection.
    EXPECT_THAT(part_status_map.at(kPartName),
                EqualsProto(initial_robot_status.part_status().at(kPartName)));
    EXPECT_THAT(part_status_map.at(kPartName2),
                EqualsProto(initial_robot_status.part_status().at(kPartName2)));
  }
  // The action and session have different parts and so cannot be added.
  ASSERT_OK_AND_ASSIGN(auto session, fake_connection->CreateSession(
                                         {kPartName2}, SessionId(9238), {}));
  // Add a stop action, since that action type does not need parameters.
  EXPECT_THAT(session->AddActionsAndReactions(ActionsAndReactions{
                  .action_descriptions = {ActionDescription{
                      .id = kActionInstanceId,
                      .slot_part_map = {{kStopPartSlot, kPartName}},
                      .action_type_name = kStopAction,
                  }}}),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("is not controlled by this session.")));
}

TEST(RtclControllerFakeRobotConnection, GetActionSignatures) {
  GetGlobalRtclActionFactoryRegistry().ClearForTestingOnly();
  ASSERT_TRUE(GetGlobalRtclActionFactoryRegistry().RegisterNoParameters(
      kStopAction,
      [](ActionFactoryContext& ctx) {
        return std::make_unique<MockRtclAction>();
      },
      JointStopAction::GetStopSignature()));

  const intrinsic_proto::icon::v1::GetStatusResponse initial_robot_status =
      ParseTextProtoOrDie(R"pb(
        part_status {
          key: "part"
          value { timestamp_ns: 1 }
        }
      )pb");
  ASSERT_OK_AND_ASSIGN(
      auto fake_connection,
      RtclControllerFakeRobotConnection::Create(
          RtclControllerChannelFake::kDefaultServerName,
          RtclControllerFakeSession::ValidationMode::kNice,
          initial_robot_status,
          /*part_configs=*/
          {PartConfigFromPart(kPartName, PositionLoopbackFakePart(kNdof))},
          /*session_behaviors=*/{}));

  EXPECT_THAT(fake_connection->ActionTypeToSignature(),
              ElementsAre(Pair(kStopAction,
                               IgnoringRepeatedFieldOrdering(EqualsProto(
                                   JointStopAction::GetStopSignature())))));
}

TEST(RtclControllerFakeRobotConnection, ActionCompatibility) {
  GetGlobalRtclActionFactoryRegistry().ClearForTestingOnly();

  ActionSignatureBuilder b(kMockActionTypeName,
                           "mock action with an optional part slot");
  ASSERT_OK(b.AddPartSlot(
      kSlotName, "This slot is required!",
      /*required_feature_interfaces=*/
      {intrinsic_proto::icon::v1::FEATURE_INTERFACE_JOINT_POSITION}));
  ASSERT_OK(b.AddOptionalPartSlot(
      kOptionalSlotName, "This slot is optional!",
      /*required_feature_interfaces=*/
      {intrinsic_proto::icon::v1::FEATURE_INTERFACE_SIMPLE_GRIPPER}));
  auto signature = b.Finish();

  ASSERT_TRUE(GetGlobalRtclActionFactoryRegistry().RegisterNoParameters(
      kMockActionTypeName,
      [](ActionFactoryContext& ctx) {
        // Access both slots so that the factory is valid.
        (void)ctx.GetSlotInfo(kSlotName);
        (void)ctx.GetOptionalSlotInfo(kOptionalSlotName);
        return std::make_unique<MockRtclAction>();
      },
      signature));

  const intrinsic_proto::icon::v1::GetStatusResponse initial_robot_status =
      ParseTextProtoOrDie(R"pb(
        part_status {
          key: "part"
          value { timestamp_ns: 1 }
        }
      )pb");
  ASSERT_OK_AND_ASSIGN(
      auto fake_connection,
      RtclControllerFakeRobotConnection::Create(
          RtclControllerChannelFake::kDefaultServerName,
          RtclControllerFakeSession::ValidationMode::kNice,
          initial_robot_status,
          /*part_configs=*/
          {
              // This has the JointPosition feature interface, so it is
              // compatible with the required slot.
              PartConfigFromPart(kPartName, PositionLoopbackFakePart(kNdof)),
              // This has the SimpleGripper feature interface, so it is
              // compatible with the optional slot.
              PartConfigFromPart(kOptionalPartName, *FakeReadOnlyGripper()),
              // Does not have any feature interfaces, so it's not compatible
              // with either the required or optional slot.
              PartConfigFromPart(kIncompatiblePartName, FakeRealtimePart<>()),
          },
          /*session_behaviors=*/{}));

  EXPECT_OK(fake_connection->ActionCompatibleWithPart(kMockActionTypeName,
                                                      kPartName));
  EXPECT_OK(fake_connection->ActionCompatibleWithPart(kMockActionTypeName,
                                                      kOptionalPartName));
  EXPECT_OK(fake_connection->ActionCompatibleWithSlotPartMap(
      kMockActionTypeName, {{kSlotName, kPartName}}));
  EXPECT_OK(fake_connection->ActionCompatibleWithSlotPartMap(
      kMockActionTypeName, {
                               {kSlotName, kPartName},
                               {kOptionalSlotName, kOptionalPartName},
                           }));

  // All of the following should return errors - for ActionCompatibleWithPart:
  // * Unknown action type
  // * Unknown part name
  // * Incompatible part
  EXPECT_THAT(fake_connection->ActionCompatibleWithPart(
                  absl::StrCat("not_", kMockActionTypeName), kPartName),
              StatusIs(absl::StatusCode::kNotFound));
  EXPECT_THAT(fake_connection->ActionCompatibleWithPart(
                  kMockActionTypeName, absl::StrCat("not_", kPartName)),
              StatusIs(absl::StatusCode::kNotFound));
  EXPECT_THAT(fake_connection->ActionCompatibleWithPart(kMockActionTypeName,
                                                        kIncompatiblePartName),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       AllOf(HasSubstr(kSlotName), HasSubstr(kOptionalSlotName),
                             HasSubstr("required interfaces"))));

  // For ActionCompatibleWithSlotPartMap:
  // * Unknown action type
  // * Unknown slot name
  // * Unknown part name
  // * Incompatible part
  // * Missing required part
  EXPECT_THAT(
      fake_connection->ActionCompatibleWithSlotPartMap(
          absl::StrCat("not_", kMockActionTypeName), {{kSlotName, kPartName}}),
      StatusIs(absl::StatusCode::kNotFound));
  {
    SlotPartMap unknown_slot_name_slot_part_map{
        {kSlotName, kPartName},
        {absl::StrCat("not_", kOptionalSlotName), kOptionalPartName},
    };
    EXPECT_THAT(fake_connection->ActionCompatibleWithSlotPartMap(
                    kMockActionTypeName, unknown_slot_name_slot_part_map),
                StatusIs(absl::StatusCode::kNotFound));
  }
  EXPECT_THAT(
      fake_connection->ActionCompatibleWithSlotPartMap(
          kMockActionTypeName, {{kSlotName, absl::StrCat("not_", kPartName)}}),
      StatusIs(absl::StatusCode::kNotFound));
  {
    SlotPartMap incompatible_part_slot_part_map{
        {kSlotName, kIncompatiblePartName},
        {kOptionalSlotName, kOptionalPartName},
    };
    EXPECT_THAT(fake_connection->ActionCompatibleWithSlotPartMap(
                    kMockActionTypeName, incompatible_part_slot_part_map),
                StatusIs(absl::StatusCode::kInvalidArgument));
  }
  EXPECT_THAT(
      fake_connection->ActionCompatibleWithSlotPartMap(
          kMockActionTypeName, {{kOptionalSlotName, kOptionalPartName}}),
      StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(RtclControllerFakeRobotConnection, PartPropertyMapStartsEmpty) {
  ASSERT_OK_AND_ASSIGN(
      auto fake_connection,
      RtclControllerFakeRobotConnection::Create(
          RtclControllerChannelFake::kDefaultServerName,
          RtclControllerFakeSession::ValidationMode::kNice,
          intrinsic_proto::icon::v1::GetStatusResponse(),
          /*part_configs=*/
          {PartConfigFromPart(kPartName, PositionLoopbackFakePart(kNdof))},
          {}));
  // Get the part property map directly from the connection
  EXPECT_THAT(fake_connection->GetPartPropertiesTestOnly().properties,
              IsEmpty());

  // Get the part property map via the PartCollection (like an ICON client
  // would)
  ASSERT_OK_AND_ASSIGN(
      auto properties_from_part_collection,
      fake_connection->MutablePartCollection().GetPartProperties());
  EXPECT_THAT(properties_from_part_collection.properties, IsEmpty());

  // Try to set a property on `kPartName` and expect NotFound
  EXPECT_THAT(fake_connection->MutablePartCollection().SetPartProperties(
                  {.properties = {{kPartName, {{"my_property", 1.23}}}}}),
              StatusIs(absl::StatusCode::kNotFound));
}

TEST(RtclControllerFakeRobotConnection,
     SetPartPropertiesIsReflectedInPartCollection) {
  ASSERT_OK_AND_ASSIGN(
      auto fake_connection,
      RtclControllerFakeRobotConnection::Create(
          RtclControllerChannelFake::kDefaultServerName,
          RtclControllerFakeSession::ValidationMode::kNice,
          intrinsic_proto::icon::v1::GetStatusResponse(),
          /*part_configs=*/
          {PartConfigFromPart(kPartName, PositionLoopbackFakePart(kNdof))},
          {}));
  PartPropertyMap kExpectedProperties = {
      .properties = {{kPartName, {{"my_property", 1.23}}}}};
  fake_connection->SetPartPropertiesTestOnly(kExpectedProperties);

  // Check that the connection reports the same map we just set.
  EXPECT_THAT(
      fake_connection->GetPartPropertiesTestOnly().properties,
      UnorderedElementsAre(
          Pair(kPartName, UnorderedElementsAreArray(
                              kExpectedProperties.properties.at(kPartName)))));
  // Try to set "not_my_property" (which doesn't exist) on `kPartName` and
  // expect NotFound
  EXPECT_THAT(fake_connection->MutablePartCollection().SetPartProperties(
                  {.properties = {{kPartName, {{"not_my_property", false}}}}}),
              StatusIs(absl::StatusCode::kNotFound));
  // Try to set "my_property" on `kPartName` to bool (wrong type) and expect
  // InvalidArgument
  EXPECT_THAT(fake_connection->MutablePartCollection().SetPartProperties(
                  {.properties = {{kPartName, {{"my_property", false}}}}}),
              StatusIs(absl::StatusCode::kInvalidArgument));
  // Try to set "my_property" on `kPartName` and expect success
  EXPECT_OK(fake_connection->MutablePartCollection().SetPartProperties(
      {.properties = {{kPartName, {{"my_property", 4.56}}}}}));

  // Check that we can read the new value, both via the PartCollection and the
  // connection.
  ASSERT_OK_AND_ASSIGN(
      TimestampedPartProperties property_map_from_part_collection,
      fake_connection->MutablePartCollection().GetPartProperties());
  EXPECT_THAT(property_map_from_part_collection.properties,
              UnorderedElementsAre(Pair(
                  kPartName, UnorderedElementsAre(Pair("my_property", 4.56)))));
  EXPECT_THAT(fake_connection->GetPartPropertiesTestOnly().properties,
              UnorderedElementsAre(Pair(
                  kPartName, UnorderedElementsAre(Pair("my_property", 4.56)))));
}

}  // namespace
}  // namespace intrinsic::icon
