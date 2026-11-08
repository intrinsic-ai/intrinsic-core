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

#include "intrinsic/icon/cc_client/testing/rtcl_controller_channel_fake.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <atomic>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "gtest/gtest-spi.h"
#include "internal/testing.h"
#include "intrinsic/icon/cc_client/client.h"
#include "intrinsic/icon/cc_client/client_utils.h"
#include "intrinsic/icon/cc_client/condition.h"
#include "intrinsic/icon/cc_client/operational_status.h"
#include "intrinsic/icon/cc_client/session.h"
#include "intrinsic/icon/cc_client/state_variable_path.h"
#include "intrinsic/icon/cc_client/testing/channel_fake.h"
#include "intrinsic/icon/cc_client/testing/rtcl_controller_fake_session.h"
#include "intrinsic/icon/common/builtins.h"
#include "intrinsic/icon/common/id_types.h"
#include "intrinsic/icon/control/action_factory_context.h"
#include "intrinsic/icon/control/mock_rtcl_action.h"
#include "intrinsic/icon/control/parts/realtime_part_factory_common.h"
#include "intrinsic/icon/control/parts/testing/position_loopback_fake_part.h"
#include "intrinsic/icon/control/rtcl_action.h"
#include "intrinsic/icon/control/rtcl_action_factory_registry.h"
#include "intrinsic/icon/proto/cart_space.pb.h"
#include "intrinsic/icon/proto/part_status.pb.h"
#include "intrinsic/icon/proto/safety_status.pb.h"
#include "intrinsic/icon/proto/v1/service.pb.h"
#include "intrinsic/icon/proto/v1/types.pb.h"
#include "intrinsic/icon/server/testing/in_process_server.h"
#include "intrinsic/icon/server/testing/test_with_status_spec.h"
#include "intrinsic/production/external/tsl/net.h"
#include "intrinsic/util/proto/parse_text_proto.h"
#include "protobuf-matchers/protocol-buffer-matchers.h"

namespace intrinsic::icon {
namespace {

using ::absl_testing::StatusIs;
using ::protobuf_matchers::EqualsProto;
using ::testing::Contains;
using ::testing::HasSubstr;
using ::testing::MockFunction;
using ::testing::Pair;
using ::testing::Property;
using ::testing::Sequence;
using ::testing::UnorderedElementsAre;

constexpr char kActionType[] = "zombulator";
constexpr char kDefaultSlotName[] = "default_slot";
constexpr size_t kNdof = 6;
constexpr char kPart1Name[] = "part_1";
constexpr char kPart2Name[] = "part_2";

intrinsic_proto::icon::v1::ActionSignature CreateSignature(
    absl::string_view action_name) {
  intrinsic_proto::icon::v1::ActionSignature signature;
  signature.set_action_type_name(action_name);
  intrinsic_proto::icon::v1::ActionSignature::PartSlotInfo slot_info;
  slot_info.set_description("Just a fake slot.");
  signature.mutable_part_slot_infos()->insert({kDefaultSlotName, slot_info});
  return signature;
}

class RtclControllerChannelFakeBuilderTest : public TestWithStatusSpec {
 public:
  RtclControllerChannelFakeBuilderTest() {
    PositionLoopbackFakePart part_1(kNdof);
    PositionLoopbackFakePart part_2(kNdof);

    intrinsic_proto::icon::v1::PartConfig& part_1_config =
        part_configs_.emplace_back();
    part_1_config.set_name(kPart1Name);
    part_1_config.set_part_type_name("PositionLoopbackFakePart");
    for (const auto& feature_interface :
         part_1.GetFeatureInterfaces().SupportedFeatureInterfaceTypes()) {
      part_1_config.add_feature_interfaces(feature_interface);
    }
    *part_1_config.mutable_generic_config() =
        ExtractGenericConfig(part_1.GetFeatureInterfaces());

    intrinsic_proto::icon::v1::PartConfig& part_2_config =
        part_configs_.emplace_back();
    part_2_config.set_name(kPart2Name);
    part_2_config.set_part_type_name("PositionLoopbackFakePart");
    for (const auto& feature_interface :
         part_2.GetFeatureInterfaces().SupportedFeatureInterfaceTypes()) {
      part_2_config.add_feature_interfaces(feature_interface);
    }
    *part_2_config.mutable_generic_config() =
        ExtractGenericConfig(part_2.GetFeatureInterfaces());
  }

  void SetUp() override {
    TestWithStatusSpec::SetUp();
    // Clear Action type registry
    GetGlobalRtclActionFactoryRegistry().ClearForTestingOnly();
    ASSERT_TRUE(GetGlobalRtclActionFactoryRegistry().RegisterNoParameters(
        kActionType,
        [](ActionFactoryContext& ctx) {
          return std::make_unique<MockRtclAction>();
        },
        CreateSignature(kActionType)));
  }

  absl::Span<const intrinsic_proto::icon::v1::PartConfig> part_configs() const {
    return part_configs_;
  }

 private:
  std::vector<intrinsic_proto::icon::v1::PartConfig> part_configs_;
};

TEST_F(RtclControllerChannelFakeBuilderTest, ClientFromEmptyChannel) {
  RtclControllerChannelFake::Builder b(
      RtclControllerFakeSession::ValidationMode::kNice);
  ASSERT_OK_AND_ASSIGN(auto channel, b.Build(part_configs()));
  Client client(channel);
  ASSERT_OK_AND_ASSIGN(OperationalStatus operational_status,
                       client.GetOperationalStatus());
  EXPECT_TRUE(IsEnabled(operational_status));
}

TEST_F(RtclControllerChannelFakeBuilderTest, StartsDisabledIfRequested) {
  RtclControllerChannelFake::Builder b(
      RtclControllerFakeSession::ValidationMode::kNice);
  ASSERT_OK_AND_ASSIGN(auto channel, b.StartDisabled().Build(part_configs()));
  Client client(channel);
  ASSERT_OK_AND_ASSIGN(OperationalStatus operational_status,
                       client.GetOperationalStatus());
  EXPECT_TRUE(IsDisabled(operational_status));
}

TEST_F(RtclControllerChannelFakeBuilderTest, StartsFaultedIfRequested) {
  RtclControllerChannelFake::Builder b(
      RtclControllerFakeSession::ValidationMode::kNice);
  ASSERT_OK_AND_ASSIGN(auto channel, b.StartFaulted().Build(part_configs()));
  Client client(channel);
  ASSERT_OK_AND_ASSIGN(OperationalStatus operational_status,
                       client.GetOperationalStatus());
  EXPECT_TRUE(IsFaulted(operational_status));
}

TEST_F(RtclControllerChannelFakeBuilderTest, CanClearFault) {
  RtclControllerChannelFake::Builder b(
      RtclControllerFakeSession::ValidationMode::kNice);
  ASSERT_OK_AND_ASSIGN(auto channel, b.StartFaulted().Build(part_configs()));
  Client client(channel);
  EXPECT_OK(client.ClearFaults());
}

TEST_F(RtclControllerChannelFakeBuilderTest, ErrorsFromFactoryAreForwarded) {
  GetGlobalRtclActionFactoryRegistry().ClearForTestingOnly();
  ASSERT_TRUE(GetGlobalRtclActionFactoryRegistry().RegisterNoParameters(
      kActionType,
      [](ActionFactoryContext& ctx) { return absl::InternalError("oh no."); },
      CreateSignature(kActionType)));
  constexpr ActionInstanceId kId1(1);
  ASSERT_OK_AND_ASSIGN(auto channel_fake,
                       RtclControllerChannelFake::Builder(
                           RtclControllerFakeSession::ValidationMode::kNice)
                           .Build(part_configs()));

  ASSERT_OK(Client(channel_fake).Enable());
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Session> session,
                       Session::Start(channel_fake, {kPart1Name}));

  EXPECT_THAT(
      session->AddAction(ActionDescriptor(kActionType, kId1, kPart1Name)),
      StatusIs(absl::StatusCode::kInternal));
}

TEST_F(RtclControllerChannelFakeBuilderTest, ListPartsHasExpectedPart) {
  ASSERT_OK_AND_ASSIGN(auto channel_fake,
                       RtclControllerChannelFake::Builder(
                           RtclControllerFakeSession::ValidationMode::kNice)
                           .Build(part_configs()));

  Client client(channel_fake);
  ASSERT_OK_AND_ASSIGN(std::vector<std::string> parts, client.ListParts());
  EXPECT_THAT(parts, UnorderedElementsAre(kPart1Name, kPart2Name));
}

TEST_F(RtclControllerChannelFakeBuilderTest,
       ListPartsHasExpectedPartViaLoopbackPort) {
  ASSERT_OK_AND_ASSIGN(auto channel_fake,
                       RtclControllerChannelFake::Builder(
                           RtclControllerFakeSession::ValidationMode::kNice,
                           /*control_frequency_hz=*/1000,
                           InProcessApplicationLayerServer::Options{
                               .use_in_process_channel = false,
                               .port = tsl::net::PickUnusedPortOrDie(),
                           })
                           .Build(part_configs()));

  Client client(channel_fake);
  ASSERT_OK_AND_ASSIGN(std::vector<std::string> parts, client.ListParts());
  EXPECT_THAT(parts, UnorderedElementsAre(kPart1Name, kPart2Name));
}

TEST_F(RtclControllerChannelFakeBuilderTest, WithInitialRobotStatus) {
  ::intrinsic_proto::icon::PartStatus part_status;
  part_status.mutable_wrench_at_ft()->set_x(123);
  ::intrinsic_proto::icon::v1::GetStatusResponse initial_status;
  initial_status.mutable_part_status()->insert({kPart1Name, part_status});
  initial_status.mutable_part_status()->insert(
      {kPart2Name, ::intrinsic_proto::icon::PartStatus()});
  intrinsic_proto::icon::SafetyStatus safety_status;
  safety_status.set_enable_button_status(
      intrinsic_proto::icon::BUTTON_STATUS_DISENGAGED);
  safety_status.set_estop_button_status(
      intrinsic_proto::icon::BUTTON_STATUS_ENGAGED);
  safety_status.set_mode_of_safe_operation(
      intrinsic_proto::icon::MODE_OF_SAFE_OPERATION_AUTOMATIC);
  safety_status.set_requested_behavior(
      intrinsic_proto::icon::REQUESTED_BEHAVIOR_NORMAL_OPERATION);
  *initial_status.mutable_safety_status() = safety_status;

  ASSERT_OK_AND_ASSIGN(auto channel_fake,
                       RtclControllerChannelFake::Builder(
                           RtclControllerFakeSession::ValidationMode::kNice)
                           .WithInitialRobotStatus(initial_status)
                           .Build(part_configs()));

  Client client(channel_fake);
  ASSERT_OK_AND_ASSIGN(
      intrinsic_proto::icon::v1::GetStatusResponse robot_status,
      client.GetStatus());
  EXPECT_THAT(
      robot_status.part_status(),
      Contains(Pair(kPart1Name,
                    Property(&intrinsic_proto::icon::PartStatus::wrench_at_ft,
                             EqualsProto(part_status.wrench_at_ft())))));

  EXPECT_THAT(robot_status.safety_status(), EqualsProto(safety_status));
}

TEST_F(RtclControllerChannelFakeBuilderTest, UpdatesRobotStatus) {
  constexpr ActionInstanceId kId1(1);

  ::intrinsic_proto::icon::PartStatus initial_part_status;
  initial_part_status.mutable_wrench_at_ft()->set_x(123);
  ::intrinsic_proto::icon::v1::GetStatusResponse initial_status;
  initial_status.mutable_part_status()->insert(
      {kPart1Name, initial_part_status});
  initial_status.mutable_part_status()->insert(
      {kPart2Name, ::intrinsic_proto::icon::PartStatus()});

  ::intrinsic_proto::icon::PartStatus final_part_status;
  final_part_status.mutable_wrench_at_ft()->set_x(456);
  ::intrinsic_proto::icon::v1::GetStatusResponse final_status;
  final_status.mutable_part_status()->insert({kPart1Name, final_part_status});
  final_status.mutable_part_status()->insert(
      {kPart2Name, ::intrinsic_proto::icon::PartStatus()});

  RtclControllerChannelFake::Builder b(
      RtclControllerFakeSession::ValidationMode::kNice);
  b.WithInitialRobotStatus(initial_status)
      .OnNextSession(SessionWill().OnAction(
          kId1, ActionWill().SetRobotStatus(final_status)));
  ASSERT_OK_AND_ASSIGN(std::shared_ptr<RtclControllerChannelFake> channel_fake,
                       b.Build(part_configs()));

  Client client(channel_fake);
  ASSERT_OK(client.Enable());
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Session> session,
                       Session::Start(channel_fake, {kPart1Name}));

  ASSERT_OK_AND_ASSIGN(Action action, session->AddAction(ActionDescriptor(
                                          kActionType, kId1, kPart1Name)));

  EXPECT_OK(session->StartAction(action));
  ASSERT_OK(client.GetStatus());
  // the fake – eventually, it will trigger the action behavior we set above and
  // update the status to final_part_status.
  while (true) {
    ASSERT_OK_AND_ASSIGN(
        intrinsic_proto::icon::v1::GetStatusResponse robot_status,
        client.GetStatus());
    if (auto part_1_status = robot_status.part_status().find(kPart1Name);
        part_1_status != robot_status.part_status().end() &&
        part_1_status->second.wrench_at_ft().x() ==
            final_part_status.wrench_at_ft().x()) {
      break;
    }
  }
}

TEST_F(RtclControllerChannelFakeBuilderTest, StartSessionFailsWithUnknownPart) {
  ASSERT_OK_AND_ASSIGN(std::shared_ptr<RtclControllerChannelFake> channel_fake,
                       RtclControllerChannelFake::Builder(
                           RtclControllerFakeSession::ValidationMode::kNice)
                           .Build(part_configs()));

  EXPECT_OK(Client(channel_fake).Enable());
  EXPECT_THAT(Session::Start(channel_fake, {absl::StrCat("not_", kPart1Name)}),
              StatusIs(absl::StatusCode::kNotFound, HasSubstr("Part")));
}

TEST_F(RtclControllerChannelFakeBuilderTest, StartSessionFailsIfFaulted) {
  ASSERT_OK_AND_ASSIGN(std::shared_ptr<RtclControllerChannelFake> channel_fake,
                       RtclControllerChannelFake::Builder(
                           RtclControllerFakeSession::ValidationMode::kNice)
                           .StartFaulted()
                           .Build(part_configs()));

  EXPECT_THAT(
      Session::Start(channel_fake, {kPart1Name}),
      StatusIs(absl::StatusCode::kFailedPrecondition, HasSubstr("FAULTED")));
}

TEST_F(RtclControllerChannelFakeBuilderTest, StartSessionSucceeds) {
  ASSERT_OK_AND_ASSIGN(std::shared_ptr<RtclControllerChannelFake> channel_fake,
                       RtclControllerChannelFake::Builder(
                           RtclControllerFakeSession::ValidationMode::kNice)
                           .Build(part_configs()));

  ASSERT_OK(Client(channel_fake).Enable());
  EXPECT_OK(Session::Start(channel_fake, {kPart1Name}));
}

TEST_F(RtclControllerChannelFakeBuilderTest, StartAction) {
  constexpr ActionInstanceId kId1(1);

  ASSERT_OK_AND_ASSIGN(auto channel_fake,
                       RtclControllerChannelFake::Builder(
                           RtclControllerFakeSession::ValidationMode::kNice)
                           .Build(part_configs()));

  Client client(channel_fake);
  ASSERT_OK(client.Enable());
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Session> session,
                       Session::Start(channel_fake, {kPart1Name}));

  ASSERT_OK_AND_ASSIGN(Action action, session->AddAction(ActionDescriptor(
                                          kActionType, kId1, kPart1Name)));
  ASSERT_THAT(action.id(), kId1);
  EXPECT_OK(session->StartAction(action));
}

TEST_F(RtclControllerChannelFakeBuilderTest,
       StateVariableMapTriggersCondition) {
  constexpr ActionInstanceId kId1(1);
  constexpr ActionInstanceId kId2(2);

  RtclControllerChannelFake::Builder b(
      RtclControllerFakeSession::ValidationMode::kNice);
  b.OnNextSession(
      SessionWill()
          .OnAction(kId1, ActionWill().ReportStateVariables({{kIsDone, true}}))
          .OnAction(kId2,
                    ActionWill().ReportStateVariables({{kIsDone, true}})));
  ASSERT_OK_AND_ASSIGN(auto channel_fake, b.Build(part_configs()));

  Client client(channel_fake);
  ASSERT_OK(client.Enable());
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Session> session,
                       Session::Start(channel_fake, {kPart1Name, kPart2Name}));

  constexpr ReactionHandle kDoneHandle(1);
  ::testing::MockFunction<void()> action_1_done_watcher;
  EXPECT_CALL(action_1_done_watcher, Call).Times(1);
  {
    ASSERT_OK_AND_ASSIGN(
        auto action,
        session->AddAction(
            {ActionDescriptor(kActionType, kId2, kPart1Name)
                 .WithReaction(
                     ReactionDescriptor(IsDone()).WithHandle(kDoneHandle))}));
    ASSERT_THAT(action.id(), kId2);
  }
  ASSERT_OK_AND_ASSIGN(
      auto action,
      session->AddAction(
          {ActionDescriptor(kActionType, kId1, kPart1Name)
               .WithReaction(ReactionDescriptor(IsDone())
                                 .WithWatcherOnCondition(
                                     action_1_done_watcher.AsStdFunction())
                                 .WithRealtimeActionOnCondition(kId2))}));
  ASSERT_THAT(action.id(), kId1);

  EXPECT_OK(session->StartAction(action));
  EXPECT_OK(session->RunWatcherLoopUntilReaction(kDoneHandle));
}

TEST_F(RtclControllerChannelFakeBuilderTest,
       StateVariableMapTriggersMultipleConditions) {
  constexpr ActionInstanceId kId1(1);
  constexpr ActionInstanceId kId2(2);
  const std::string kCymbalsJammed = "cymbals_jammed";

  RtclControllerChannelFake::Builder b(
      RtclControllerFakeSession::ValidationMode::kNice);
  // The state variables reported by the first Action should trigger both
  // Reactions we add to the Action below.
  b.OnNextSession(SessionWill()
                      .OnAction(kId1, ActionWill().ReportStateVariables({
                                          {kIsDone, true},
                                          {kCymbalsJammed, true},
                                      }))
                      .OnAction(kId2, ActionWill().ReportStateVariables(
                                          {{kIsDone, true}})));
  ASSERT_OK_AND_ASSIGN(auto channel_fake, b.Build(part_configs()));

  Client client(channel_fake);
  ASSERT_OK(client.Enable());
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Session> session,
                       Session::Start(channel_fake, {kPart1Name}));

  constexpr ReactionHandle kDoneHandle(1);
  ::testing::MockFunction<void()> cymbal_jammed_watcher;
  EXPECT_CALL(cymbal_jammed_watcher, Call).Times(1);

  ASSERT_OK_AND_ASSIGN(
      auto actions,
      session->AddActions(
          {ActionDescriptor(kActionType, kId1, kPart1Name)
               .WithReaction(ReactionDescriptor(IsTrue(kCymbalsJammed))
                                 .WithWatcherOnCondition(
                                     cymbal_jammed_watcher.AsStdFunction()))
               .WithReaction(ReactionDescriptor(IsDone())
                                 .WithRealtimeActionOnCondition(kId2)),
           ActionDescriptor(kActionType, kId2, kPart1Name)
               .WithReaction(
                   ReactionDescriptor(IsDone()).WithHandle(kDoneHandle))}));
  ASSERT_THAT(actions.front().id(), kId1);

  EXPECT_OK(session->StartAction(actions.front()));
  EXPECT_OK(session->RunWatcherLoopUntilReaction(kDoneHandle));
}

TEST_F(RtclControllerChannelFakeBuilderTest,
       StateVariableMapTriggersConditionsInOrder) {
  constexpr ActionInstanceId kId1(1);
  const std::string kPastaCooked = "pasta_cooked";

  RtclControllerChannelFake::Builder b(
      RtclControllerFakeSession::ValidationMode::kNice);
  b.OnNextSession(
      SessionWill().OnAction(kId1, ActionWill().ReportStateVariablesInOrder({
                                       {{kPastaCooked, true}},
                                       {{kIsDone, true}},
                                   })));
  constexpr ReactionHandle kDoneHandle(1);
  MockFunction<void()> pasta_done_watcher;
  MockFunction<void()> action_done_watcher;
  {
    // The watchers should be invoked in the same order that the state variable
    // maps are listed above.
    Sequence s;
    EXPECT_CALL(pasta_done_watcher, Call).Times(1);
    EXPECT_CALL(action_done_watcher, Call).Times(1);
  }

  ASSERT_OK_AND_ASSIGN(auto channel_fake, b.Build(part_configs()));

  Client client(channel_fake);
  ASSERT_OK(client.Enable());
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Session> session,
                       Session::Start(channel_fake, {kPart1Name}));

  ASSERT_OK_AND_ASSIGN(
      Action action,
      session->AddAction(
          ActionDescriptor(kActionType, kId1, kPart1Name)
              .WithReaction(ReactionDescriptor(IsTrue(kPastaCooked))
                                .WithWatcherOnCondition(
                                    pasta_done_watcher.AsStdFunction()))
              .WithReaction(ReactionDescriptor(IsDone())
                                .WithWatcherOnCondition(
                                    action_done_watcher.AsStdFunction())
                                .WithHandle(kDoneHandle))));
  ASSERT_THAT(action.id(), kId1);

  EXPECT_OK(session->StartAction(action));
  EXPECT_OK(session->RunWatcherLoopUntilReaction(kDoneHandle));
}

TEST_F(RtclControllerChannelFakeBuilderTest,
       StrictChannelFailsOnUnmetExpectation) {
  auto channel_with_unmet_expectation = [this]() {
    constexpr ActionInstanceId kId1(1);
    constexpr ActionInstanceId kId2(2);

    RtclControllerChannelFake::Builder b(
        RtclControllerFakeSession::ValidationMode::kStrict);
    b.OnNextSession(
        SessionWill()
            .OnAction(kId1,
                      ActionWill().ReportStateVariables({{kIsDone, true}}))
            .OnAction(kId2,
                      ActionWill().ReportStateVariables({{kIsDone, true}}))
            .OnAction(kId1,
                      ActionWill().ReportStateVariables({{"final", true}})));

    ASSERT_OK_AND_ASSIGN(
        std::shared_ptr<RtclControllerChannelFake> channel_fake,
        b.Build(part_configs()));

    Client client(channel_fake);
    ASSERT_OK(client.Enable());
    ASSERT_OK_AND_ASSIGN(std::unique_ptr<Session> session,
                         Session::Start(channel_fake, {kPart1Name}));

    constexpr ReactionHandle kDoneHandle(1);
    ASSERT_OK_AND_ASSIGN(
        auto actions,
        session->AddActions(
            {ActionDescriptor(kActionType, kId1, kPart1Name)
                 .WithReaction(ReactionDescriptor(IsDone())
                                   .WithRealtimeActionOnCondition(kId2)),
             ActionDescriptor(kActionType, kId2, kPart1Name)
                 .WithReaction(
                     ReactionDescriptor(IsDone()).WithHandle(kDoneHandle))}));
    ASSERT_THAT(actions.front().id(), kId1);
    EXPECT_OK(session->StartAction(actions.front()));
    EXPECT_OK(session->RunWatcherLoopUntilReaction(kDoneHandle));
  };

  EXPECT_NONFATAL_FAILURE_ON_ALL_THREADS(channel_with_unmet_expectation(),
                                         "unmet action expectations");
}

TEST_F(RtclControllerChannelFakeBuilderTest,
       FreeStandingReactionTriggersOnlyOnRisingEdge) {
  intrinsic_proto::icon::v1::GetStatusResponse initial_status_resp =
      ParseTextProtoOrDie(
          R"pb(part_status {
                 key: "part_1"
                 value {
                   adio_state {
                     digital_inputs {
                       key: "digital_in"
                       value {
                         signals {
                           key: 1
                           value { value: false }
                         }
                       }
                     }
                   }
                 }
               })pb");

  intrinsic_proto::icon::v1::GetStatusResponse second_status_resp =
      ParseTextProtoOrDie(
          R"pb(part_status {
                 key: "part_1"
                 value {
                   adio_state {
                     digital_inputs {
                       key: "digital_in"
                       value {
                         signals {
                           key: 1
                           value { value: true }
                         }
                       }
                     }
                   }
                 }
               })pb");

  RtclControllerChannelFake::Builder b(
      RtclControllerFakeSession::ValidationMode::kStrict);
  b.WithInitialRobotStatus(initial_status_resp)
      .OnNextSession(SessionWill().SetRobotStatus(second_status_resp));

  ASSERT_OK_AND_ASSIGN(std::shared_ptr<RtclControllerChannelFake> channel_fake,
                       b.Build(part_configs()));

  Client client(channel_fake);
  ASSERT_OK(client.Enable());
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Session> session,
                       Session::Start(channel_fake, {"part_1"}));
  std::atomic_int callback_counter = 0;
  constexpr ReactionHandle kDoneHandle(1);
  ASSERT_OK(session->AddFreestandingReaction(
      ReactionDescriptor(
          IsTrue(ADIODigitalInputStateVariablePath("part_1", "digital_in", 1)))
          .WithHandle(kDoneHandle)
          .WithWatcherOnCondition(
              [&callback_counter] { callback_counter++; })));
  EXPECT_OK(session->RunWatcherLoopUntilReaction(kDoneHandle));
  // Wait a bit to ensure that the reaction is not triggered again.
  absl::SleepFor(absl::Seconds(1));
  // Reset the session, which blocks until the session is completely done. So
  // that all possible callbacks are triggered.
  session.reset();
  EXPECT_EQ(callback_counter, 1);
}

TEST_F(RtclControllerChannelFakeBuilderTest,
       FreeStandingReactionTriggersOnlyOnce) {
  constexpr ActionInstanceId kId1(1);
  constexpr ActionInstanceId kId2(2);
  constexpr ActionInstanceId kId3(3);
  intrinsic_proto::icon::v1::GetStatusResponse initial_status_resp =
      ParseTextProtoOrDie(
          R"pb(part_status {
                 key: "part_1"
                 value {
                   adio_state {
                     digital_inputs {
                       key: "digital_in"
                       value {
                         signals {
                           key: 1
                           value { value: false }
                         }
                       }
                     }
                   }
                 }
               })pb");

  intrinsic_proto::icon::v1::GetStatusResponse second_status_resp =
      ParseTextProtoOrDie(
          R"pb(part_status {
                 key: "part_1"
                 value {
                   adio_state {
                     digital_inputs {
                       key: "digital_in"
                       value {
                         signals {
                           key: 1
                           value { value: true }
                         }
                       }
                     }
                   }
                 }
               })pb");

  RtclControllerChannelFake::Builder b(
      RtclControllerFakeSession::ValidationMode::kNice);
  b.WithInitialRobotStatus(initial_status_resp)
      .OnNextSession(
          SessionWill()
              .OnAction(kId1, ActionWill()
                                  .SetRobotStatus(second_status_resp)
                                  .ReportStateVariables({{kIsDone, true}}))
              .OnAction(kId2, ActionWill()
                                  .SetRobotStatus(initial_status_resp)
                                  .ReportStateVariables({{kIsDone, true}}))
              .OnAction(kId3, ActionWill()
                                  .SetRobotStatus(second_status_resp)
                                  .ReportStateVariables({{kIsDone, true}})));

  ASSERT_OK_AND_ASSIGN(std::shared_ptr<RtclControllerChannelFake> channel_fake,
                       b.Build(part_configs()));

  Client client(channel_fake);
  ASSERT_OK(client.Enable());
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Session> session,
                       Session::Start(channel_fake, {kPart1Name}));
  std::atomic_int callback_counter = 0;
  constexpr ReactionHandle kDoneHandle(1);
  constexpr ReactionHandle kDoneHandle2(2);
  ASSERT_OK(session->AddFreestandingReaction(
      ReactionDescriptor(IsTrue(ADIODigitalInputStateVariablePath(
                             kPart1Name, "digital_in", 1)))
          .FireOnce()
          .WithHandle(kDoneHandle)
          .WithWatcherOnCondition([&callback_counter] {
            LOG(INFO) << "Callback counter: " << ++callback_counter;
          })));
  ASSERT_OK_AND_ASSIGN(
      auto actions,
      session->AddActions(
          {ActionDescriptor(kActionType, kId1, kPart1Name)
               .WithReaction(ReactionDescriptor(IsDone())
                                 .WithRealtimeActionOnCondition(kId2)),
           ActionDescriptor(kActionType, kId2, kPart1Name)
               .WithReaction(ReactionDescriptor(IsDone())
                                 .WithRealtimeActionOnCondition(kId3)),
           ActionDescriptor(kActionType, kId3, kPart1Name)
               .WithReaction(
                   ReactionDescriptor(IsDone()).WithHandle(kDoneHandle2))}));
  EXPECT_OK(session->StartActions({actions.front()}));
  EXPECT_OK(session->RunWatcherLoopUntilReaction(kDoneHandle2));
  // Wait a bit to ensure that the reaction is not triggered again.
  absl::SleepFor(absl::Seconds(2));
  // Reset the session, which blocks until the session is completely done. So
  // that all possible callbacks are triggered.
  session.reset();
  EXPECT_EQ(callback_counter, 1);
}

}  // namespace
}  // namespace intrinsic::icon
