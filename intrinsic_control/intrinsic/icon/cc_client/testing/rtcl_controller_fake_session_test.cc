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

#include "intrinsic/icon/cc_client/testing/rtcl_controller_fake_session.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <any>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "google/protobuf/wrappers.pb.h"
#include "gtest/gtest-spi.h"
#include "internal/testing.h"
#include "intrinsic/icon/actions/action_utils.h"
#include "intrinsic/icon/cc_client/condition.h"
#include "intrinsic/icon/cc_client/operational_status.h"
#include "intrinsic/icon/cc_client/session.h"
#include "intrinsic/icon/cc_client/testing/channel_fake.h"
#include "intrinsic/icon/common/id_types.h"
#include "intrinsic/icon/common/part_group.h"
#include "intrinsic/icon/common/slot_part_map.h"
#include "intrinsic/icon/control/action_factory_context.h"
#include "intrinsic/icon/control/actions/test_helpers.h"
#include "intrinsic/icon/control/mock_rtcl_action.h"
#include "intrinsic/icon/control/parts/testing/position_loopback_fake_part.h"
#include "intrinsic/icon/control/rtcl_action.h"
#include "intrinsic/icon/control/rtcl_action_factory_registry.h"
#include "intrinsic/icon/proto/joint_space.pb.h"
#include "intrinsic/icon/proto/joint_trajectory_conversion.h"
#include "intrinsic/icon/proto/v1/service.pb.h"
#include "intrinsic/icon/proto/v1/types.pb.h"
#include "intrinsic/icon/server/session_interface.h"
#include "intrinsic/icon/server/testing/fake_operational_state.h"
#include "intrinsic/kinematics/types/dynamic_limits_check_mode.h"
#include "intrinsic/kinematics/types/joint_state.h"
#include "intrinsic/kinematics/types/joint_trajectories.h"
#include "intrinsic/util/proto/parse_text_proto.h"
#include "intrinsic/util/status/status_macros.h"
#include "protobuf-matchers/protocol-buffer-matchers.h"

namespace intrinsic::icon {
namespace {

using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;
using ::intrinsic::ParseTextProtoOrDie;
using ::protobuf_matchers::EqualsProto;
using ::testing::AllOf;
using ::testing::Contains;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::MockFunction;
using ::testing::Ne;
using ::testing::Return;
using ::testing::UnorderedElementsAre;

constexpr char kCompatibleActionType[] = "compatible_mock_action";
constexpr char kBrokenFactoryActionType[] = "broken_factory_mock_action";
constexpr char kIncompatibleActionType[] = "incompatible_mock_action";
constexpr char kStateVariableName[] = "trigger";
constexpr char kStateVariableDescription[] =
    "A boolean we use in this test to directly trigger reactions.";
constexpr char kStreamingInputName[] = "streaming_input";
constexpr char kStreamingInputDescription[] =
    "A double to test streaming inputs.";
constexpr char kStreamingOutputName[] = "streaming_output";
constexpr char kStreamingOutputDescription[] =
    "A int to test streaming outputs.";
constexpr char kDefaultSlotName[] = "default_slot";
constexpr char kPart1Name[] = "part_1";
constexpr char kPart2Name[] = "part_2";
constexpr size_t kNdof = 6;
constexpr absl::Duration kTrajectoryDuration = absl::Seconds(1);

// A signature that describes a slot that's compatible with our fake parts.
intrinsic_proto::icon::v1::ActionSignature CompatibleActionSignature() {
  ActionSignatureBuilder builder(kCompatibleActionType, "");
  CHECK_OK(builder.SetFixedParametersType<google::protobuf::BoolValue>());
  CHECK_OK(
      builder.AddPartSlot(kDefaultSlotName, "You know, a slot.",
                          /*required_feature_interfaces=*/
                          {
                              intrinsic_proto::icon::v1::FeatureInterfaceTypes::
                                  FEATURE_INTERFACE_JOINT_POSITION,
                              intrinsic_proto::icon::v1::FeatureInterfaceTypes::
                                  FEATURE_INTERFACE_JOINT_LIMITS,
                          }));
  CHECK_OK(builder.AddStateVariable<intrinsic_proto::icon::v1::ActionSignature::
                                        StateVariableInfo::TYPE_BOOL>(
      kStateVariableName, kStateVariableDescription));
  CHECK_OK(builder.AddStreamingInput<google::protobuf::DoubleValue>(
      kStreamingInputName, kStreamingInputDescription));
  CHECK_OK(builder.AddStreamingOutput<google::protobuf::Int64Value>(
      kStreamingOutputName, kStreamingOutputDescription));
  return builder.Finish();
}

// A signature that describes a slot that is not compatible with our fake parts
// (they do not have a Force/Torque sensor).
intrinsic_proto::icon::v1::ActionSignature IncompatibleActionSignature() {
  ActionSignatureBuilder builder(kIncompatibleActionType, "");
  CHECK_OK(builder.SetFixedParametersType<google::protobuf::BoolValue>());
  CHECK_OK(
      builder.AddPartSlot(kDefaultSlotName, "You know, a slot.",
                          /*required_feature_interfaces=*/
                          {
                              intrinsic_proto::icon::v1::FeatureInterfaceTypes::
                                  FEATURE_INTERFACE_FORCE_TORQUE_SENSOR,
                          }));
  CHECK_OK(builder.AddStateVariable<intrinsic_proto::icon::v1::ActionSignature::
                                        StateVariableInfo::TYPE_BOOL>(
      kStateVariableName, kStateVariableDescription));
  return builder.Finish();
}

// A factory that returns InternalError
absl::StatusOr<std::unique_ptr<RtclActionInterface>> AlwaysFailingFactory(
    const ::google::protobuf::BoolValue& params, ActionFactoryContext& ctx) {
  return absl::InternalError("oh no.");
}

// A factory that always succeeds
absl::StatusOr<std::unique_ptr<RtclActionInterface>> AlwaysSucceedingFactory(
    const ::google::protobuf::BoolValue& params, ActionFactoryContext& ctx) {
  JointStatePVA joint_state;
  INTR_RETURN_IF_ERROR(joint_state.SetSize(kNdof));
  joint_state.position.setConstant(1);
  joint_state.velocity.setConstant(2);
  joint_state.acceleration.setConstant(3);
  INTR_ASSIGN_OR_RETURN(JointTrajectoryPVA trajectory,
                        JointTrajectoryPVA::Create(
                            {joint_state, joint_state}, kTrajectoryDuration,
                            DynamicLimitsCheckMode::kCheckNone,
                            JointTrajectoryInterpolationType::kUnspecified));
  ctx.StoreTrajectory(trajectory);
  auto parser =
      [](const google::protobuf::DoubleValue& proto) -> absl::StatusOr<double> {
    return proto.value();
  };
  auto converter =
      [](const int in) -> absl::StatusOr<google::protobuf::Int64Value> {
    google::protobuf::Int64Value proto;
    proto.set_value(in);
    return proto;
  };
  INTR_ASSIGN_OR_RETURN(
      auto id,
      (ctx.AddStreamingInputParser<google::protobuf::DoubleValue, double>(
          kStreamingInputName, parser)));
  (void)id;
  INTR_RETURN_IF_ERROR(
      (ctx.AddStreamingOutputConverter<int, google::protobuf::Int64Value>(
          converter)));

  return std::make_unique<MockRtclAction>();
}

ActionDescription MakeValidActionDescription(ActionInstanceId id,
                                             bool param_bool_value = false) {
  ActionDescription valid_action_description;
  valid_action_description.id = id;
  valid_action_description.action_type_name = kCompatibleActionType;
  valid_action_description.slot_part_map = {{/*slot_name=*/kDefaultSlotName,
                                             /*part_name=*/kPart1Name}};
  google::protobuf::BoolValue params;
  params.set_value(param_bool_value);
  CHECK(valid_action_description.params.PackFrom(params));
  return valid_action_description;
}

class RtclControllerFakeSessionTest : public ::testing::Test {
 public:
  RtclControllerFakeSessionTest() : part_1_(kNdof), part_2_(kNdof) {
    GetGlobalRtclActionFactoryRegistry().ClearForTestingOnly();

    part_configs_by_name_[kPart1Name] = PartConfigFromPart(kPart1Name, part_1_);
    part_configs_by_name_[kPart2Name] = PartConfigFromPart(kPart2Name, part_2_);
  }

  const absl::flat_hash_map<std::string, intrinsic_proto::icon::v1::PartConfig>&
  part_configs_by_name() {
    return part_configs_by_name_;
  }

  void SetUp() override {
    ASSERT_TRUE(GetGlobalRtclActionFactoryRegistry()
                    .RegisterTyped<google::protobuf::BoolValue>(
                        kCompatibleActionType, &AlwaysSucceedingFactory,
                        CompatibleActionSignature()));
    ASSERT_TRUE(GetGlobalRtclActionFactoryRegistry()
                    .RegisterTyped<google::protobuf::BoolValue>(
                        kIncompatibleActionType, &AlwaysSucceedingFactory,
                        IncompatibleActionSignature()));
    ASSERT_TRUE(GetGlobalRtclActionFactoryRegistry()
                    .RegisterTyped<google::protobuf::BoolValue>(
                        kBrokenFactoryActionType, &AlwaysFailingFactory,
                        CompatibleActionSignature()));
    ASSERT_OK(operational_state_.Enable());
  }

  FakeOperationalState operational_state_;
  MockFunction<void(
      const intrinsic_proto::icon::v1::GetStatusResponse& new_status)>
      update_status_;
  MockFunction<intrinsic_proto::icon::v1::GetStatusResponse()> get_status_;

 private:
  PositionLoopbackFakePart part_1_;
  PositionLoopbackFakePart part_2_;
  absl::flat_hash_map<std::string, intrinsic_proto::icon::v1::PartConfig>
      part_configs_by_name_;
};

TEST_F(RtclControllerFakeSessionTest, GetPartGroup) {
  RtclControllerFakeSession fake_session(
      SessionWill(), part_configs_by_name(),
      RtclControllerFakeSession::ValidationMode::kNice, operational_state_,
      update_status_.AsStdFunction(), get_status_.AsStdFunction(), {});
  PartGroup parts = fake_session.GetPartGroup();
  for (const auto& [part_name, _] : part_configs_by_name()) {
    EXPECT_THAT(parts, Contains(part_name));
  }
}

TEST_F(RtclControllerFakeSessionTest,
       CannotAddActionWithoutExpectationIfStrict) {
  RtclControllerFakeSession fake_session(
      SessionWill(), part_configs_by_name(),
      RtclControllerFakeSession::ValidationMode::kStrict, operational_state_,
      update_status_.AsStdFunction(), get_status_.AsStdFunction());

  EXPECT_THAT(fake_session.AddActionsAndReactions(ActionsAndReactions{
                  .action_descriptions = {MakeValidActionDescription(
                      ActionInstanceId(1))}}),
              StatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST_F(RtclControllerFakeSessionTest, CanAddActionWithExpectationIfStrict) {
  ActionInstanceId id(1);
  RtclControllerFakeSession fake_session(
      SessionWill().OnAction(id, ActionWill()), part_configs_by_name(),
      RtclControllerFakeSession::ValidationMode::kStrict, operational_state_,
      update_status_.AsStdFunction(), get_status_.AsStdFunction(), {});
  EXPECT_OK(fake_session.AddActionsAndReactions(
      ActionsAndReactions{.action_descriptions = {MakeValidActionDescription(
                              ActionInstanceId(1))}}));
  // Start the action to satisfy the strict requirement that all expectations
  // must be met.
  EXPECT_OK(fake_session.StartActions({ActionInstanceId(1)},
                                      /*stop_active_actions=*/true));
}

TEST_F(RtclControllerFakeSessionTest, CanAddMultipleActionsOutOfOrder) {
  ActionInstanceId id1(1);
  ActionInstanceId id2(2);
  RtclControllerFakeSession fake_session(
      SessionWill().OnAction(id1, ActionWill()).OnAction(id2, ActionWill()),
      part_configs_by_name(),
      RtclControllerFakeSession::ValidationMode::kStrict, operational_state_,
      update_status_.AsStdFunction(), get_status_.AsStdFunction(), {});
  // The expectations on ChannelFakes are in order of action *activation*, but
  // actions may be *added* in a different order.
  EXPECT_OK(fake_session.AddActionsAndReactions(
      ActionsAndReactions{.action_descriptions = {MakeValidActionDescription(
                              ActionInstanceId(2))}}));
  EXPECT_OK(fake_session.AddActionsAndReactions(
      ActionsAndReactions{.action_descriptions = {MakeValidActionDescription(
                              ActionInstanceId(1))}}));

  // Start the actions in the expected order
  EXPECT_OK(fake_session.StartActions({ActionInstanceId(1)},
                                      /*stop_active_actions=*/true));
  // Process the action will.
  EXPECT_OK(fake_session.PollReactions().status());
  EXPECT_OK(fake_session.StartActions({ActionInstanceId(2)},
                                      /*stop_active_actions=*/true));
}

TEST_F(RtclControllerFakeSessionTest, CannotAddActionWithWrongIdIfStrict) {
  ActionInstanceId expected_id(1);
  RtclControllerFakeSession fake_session(
      SessionWill().OnAction(expected_id, ActionWill()), part_configs_by_name(),
      RtclControllerFakeSession::ValidationMode::kStrict, operational_state_,
      update_status_.AsStdFunction(), get_status_.AsStdFunction(), {});

  EXPECT_THAT(fake_session.AddActionsAndReactions(ActionsAndReactions{
                  .action_descriptions = {MakeValidActionDescription(
                      ActionInstanceId(expected_id.value() + 1))}}),
              StatusIs(absl::StatusCode::kFailedPrecondition));

  // Add and start the correct action to satisfy the strict fake
  EXPECT_OK(fake_session.AddActionsAndReactions(ActionsAndReactions{
      .action_descriptions = {MakeValidActionDescription(expected_id)}}));
  EXPECT_OK(
      fake_session.StartActions({expected_id}, /*stop_active_actions=*/true));
}

TEST_F(RtclControllerFakeSessionTest, CanAddActionWithWrongIdIfNice) {
  ActionInstanceId expected_id(1);
  RtclControllerFakeSession fake_session(
      SessionWill().OnAction(expected_id, ActionWill()), part_configs_by_name(),
      RtclControllerFakeSession::ValidationMode::kNice, operational_state_,
      update_status_.AsStdFunction(), get_status_.AsStdFunction(), {});
  EXPECT_OK(fake_session.AddActionsAndReactions(
      ActionsAndReactions{.action_descriptions = {MakeValidActionDescription(
                              ActionInstanceId(expected_id.value() + 1))}}));
}

TEST_F(RtclControllerFakeSessionTest, CannotAddActionWithUnexpectedParameters) {
  ActionInstanceId id(1);
  RtclControllerFakeSession fake_session(
      SessionWill().OnAction(
          id, ActionWill().ExpectParameter<google::protobuf::BoolValue>(
                  [](const google::protobuf::BoolValue& param) {
                    if (param.value()) {
                      return absl::InternalError("oh no.");
                    }
                    return absl::OkStatus();
                  })),
      part_configs_by_name(), RtclControllerFakeSession::ValidationMode::kNice,
      operational_state_, update_status_.AsStdFunction(),
      get_status_.AsStdFunction(), {});
  // Set the parameter value to true, so that the ExpectParameter matcher above
  // returns an error (even in nice mode!).
  EXPECT_OK(fake_session.AddActionsAndReactions(ActionsAndReactions{
      .action_descriptions = {MakeValidActionDescription(
          ActionInstanceId(1), /*param_bool_value=*/true)}}));
  EXPECT_THAT(fake_session.StartActions({id}, /*stop_active_actions=*/true),
              StatusIs(absl::StatusCode::kInternal, "oh no."));
}

TEST_F(RtclControllerFakeSessionTest,
       StreamingInputChecksParameterMatchersInOrder) {
  constexpr double kExpectedValue = 1.23;
  auto matcher = [](const google::protobuf::DoubleValue& param) {
    if (param.value() != kExpectedValue) {
      return absl::InternalError("oh no, the streaming input is wrong.");
    }
    return absl::OkStatus();
  };
  ActionInstanceId id(1);
  RtclControllerFakeSession fake_session(
      SessionWill().OnAction(
          id,
          ActionWill()
              .ExpectStreamingInput<google::protobuf::DoubleValue>(matcher)
              .ExpectStreamingInput<google::protobuf::DoubleValue>(matcher)),
      part_configs_by_name(), RtclControllerFakeSession::ValidationMode::kNice,
      operational_state_, update_status_.AsStdFunction(),
      get_status_.AsStdFunction(), {});
  // Set the parameter value to true, so that the ExpectParameter matcher above
  // returns an error (even in nice mode!).
  EXPECT_OK(fake_session.AddActionsAndReactions(ActionsAndReactions{
      .action_descriptions = {MakeValidActionDescription(
          ActionInstanceId(1), /*param_bool_value=*/false)}}));
  EXPECT_OK(fake_session.StartActions({id}, /*stop_active_actions=*/true));

  google::protobuf::DoubleValue value;
  value.set_value(kExpectedValue);
  google::protobuf::Any any;
  ASSERT_TRUE(any.PackFrom(value));

  EXPECT_OK(fake_session.WriteToStreamingInput(id, kStreamingInputName, any));

  value.set_value(kExpectedValue + 1);
  ASSERT_TRUE(any.PackFrom(value));
  EXPECT_THAT(fake_session.WriteToStreamingInput(id, kStreamingInputName, any),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("the streaming input is wrong")));
}

TEST_F(RtclControllerFakeSessionTest,
       StreamingInputFailsIfStrictAndMoreStreamingInputsThanMatchers) {
  constexpr double kExpectedValue = 1.23;
  auto matcher = [](const google::protobuf::DoubleValue& param) {
    if (param.value() != kExpectedValue) {
      return absl::InternalError("oh no, the streaming input is wrong.");
    }
    return absl::OkStatus();
  };
  ActionInstanceId id(1);
  RtclControllerFakeSession fake_session(
      SessionWill().OnAction(
          id,
          ActionWill()
              .ExpectStreamingInput<google::protobuf::DoubleValue>(matcher)
              .ExpectStreamingInput<google::protobuf::DoubleValue>(matcher)),
      part_configs_by_name(),
      RtclControllerFakeSession::ValidationMode::kStrict, operational_state_,
      update_status_.AsStdFunction(), get_status_.AsStdFunction(), {});
  // Set the parameter value to true, so that the ExpectParameter matcher above
  // returns an error (even in nice mode!).
  EXPECT_OK(fake_session.AddActionsAndReactions(ActionsAndReactions{
      .action_descriptions = {MakeValidActionDescription(
          ActionInstanceId(1), /*param_bool_value=*/false)}}));
  EXPECT_OK(fake_session.StartActions({id}, /*stop_active_actions=*/true));

  google::protobuf::DoubleValue value;
  value.set_value(kExpectedValue);
  google::protobuf::Any any;
  ASSERT_TRUE(any.PackFrom(value));

  EXPECT_OK(fake_session.WriteToStreamingInput(id, kStreamingInputName, any));
  EXPECT_OK(fake_session.WriteToStreamingInput(id, kStreamingInputName, any));
  EXPECT_THAT(fake_session.WriteToStreamingInput(id, kStreamingInputName, any),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       HasSubstr("no streaming input parameter matcher was "
                                 "found for the streaming input")));
}

TEST_F(RtclControllerFakeSessionTest, StreamingInputFailsForWrongType) {
  constexpr double kExpectedValue = 1.23;
  auto matcher = [](const google::protobuf::DoubleValue& param) {
    if (param.value() != kExpectedValue) {
      return absl::InternalError("oh no, the streaming input is wrong.");
    }
    return absl::OkStatus();
  };
  ActionInstanceId id(1);
  RtclControllerFakeSession fake_session(
      SessionWill().OnAction(
          id, ActionWill().ExpectStreamingInput<google::protobuf::DoubleValue>(
                  matcher)),
      part_configs_by_name(), RtclControllerFakeSession::ValidationMode::kNice,
      operational_state_, update_status_.AsStdFunction(),
      get_status_.AsStdFunction(), {});
  // Set the parameter value to true, so that the ExpectParameter matcher above
  // returns an error (even in nice mode!).
  EXPECT_OK(fake_session.AddActionsAndReactions(ActionsAndReactions{
      .action_descriptions = {MakeValidActionDescription(
          ActionInstanceId(1), /*param_bool_value=*/false)}}));
  EXPECT_OK(fake_session.StartActions({id}, /*stop_active_actions=*/true));

  google::protobuf::Int64Value value;
  value.set_value(123);
  google::protobuf::Any any;
  ASSERT_TRUE(any.PackFrom(value));

  EXPECT_THAT(fake_session.WriteToStreamingInput(id, kStreamingInputName, any),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Streaming parameters cannot be unpacked")));
}

TEST_F(RtclControllerFakeSessionTest, StreamingOutputsArePublished) {
  constexpr int kExpectedValue1 = 456;
  std::any any1 = kExpectedValue1;
  constexpr int kExpectedValue2 = 789;
  std::any any2 = kExpectedValue2;

  ActionInstanceId id(1);
  RtclControllerFakeSession fake_session(
      SessionWill().OnAction(id,
                             ActionWill().PublishStreamingOutput({any1, any2})),
      part_configs_by_name(), RtclControllerFakeSession::ValidationMode::kNice,
      operational_state_, update_status_.AsStdFunction(),
      get_status_.AsStdFunction(), {});
  // Set the parameter value to true, so that the ExpectParameter matcher above
  // returns an error (even in nice mode!).
  EXPECT_OK(fake_session.AddActionsAndReactions(ActionsAndReactions{
      .action_descriptions = {MakeValidActionDescription(
          ActionInstanceId(1), /*param_bool_value=*/false)}}));
  EXPECT_OK(fake_session.StartActions({id}, /*stop_active_actions=*/true));
  EXPECT_OK(fake_session.PollReactions().status());
  ASSERT_OK_AND_ASSIGN(intrinsic_proto::icon::StreamingOutput output,
                       fake_session.GetLatestStreamingOutput(
                           id, absl::Now() + absl::Seconds(1)));
  google::protobuf::Int64Value output_value;
  ASSERT_TRUE(output.payload().UnpackTo(&output_value));
  EXPECT_EQ(output_value.value(), kExpectedValue1);

  EXPECT_OK(fake_session.PollReactions().status());
  ASSERT_OK_AND_ASSIGN(output, fake_session.GetLatestStreamingOutput(
                                   id, absl::Now() + absl::Seconds(1)));
  ASSERT_TRUE(output.payload().UnpackTo(&output_value));
  EXPECT_EQ(output_value.value(), kExpectedValue2);
}

TEST_F(RtclControllerFakeSessionTest,
       StreamingOutputsFailToGetLatestOutputForMismatchedTypes) {
  constexpr double kExpectedValue = 45.6;
  std::any any = kExpectedValue;
  ActionInstanceId id(1);
  RtclControllerFakeSession fake_session(
      SessionWill().OnAction(id, ActionWill().PublishStreamingOutput({any})),
      part_configs_by_name(), RtclControllerFakeSession::ValidationMode::kNice,
      operational_state_, update_status_.AsStdFunction(),
      get_status_.AsStdFunction(), {});
  // Set the parameter value to true, so that the ExpectParameter matcher above
  // returns an error (even in nice mode!).
  EXPECT_OK(fake_session.AddActionsAndReactions(ActionsAndReactions{
      .action_descriptions = {MakeValidActionDescription(
          ActionInstanceId(1), /*param_bool_value=*/false)}}));
  EXPECT_OK(fake_session.StartActions({id}, /*stop_active_actions=*/true));
  EXPECT_OK(fake_session.PollReactions().status());
  EXPECT_THAT(
      fake_session.GetLatestStreamingOutput(id, absl::Now() + absl::Seconds(1)),
      StatusIs(absl::StatusCode::kInternal,
               HasSubstr("Failed to cast streaming output")));
}

TEST_F(RtclControllerFakeSessionTest, CanAddActionWithoutExpectationIfNice) {
  RtclControllerFakeSession fake_session(
      SessionWill(), part_configs_by_name(),
      RtclControllerFakeSession::ValidationMode::kNice, operational_state_,
      update_status_.AsStdFunction(), get_status_.AsStdFunction(), {});
  EXPECT_OK(fake_session.AddActionsAndReactions(
      ActionsAndReactions{.action_descriptions = {MakeValidActionDescription(
                              ActionInstanceId(1))}}));
}

TEST_F(RtclControllerFakeSessionTest, AddActionFailsWithIncompatiblePart) {
  RtclControllerFakeSession fake_session(
      SessionWill(), part_configs_by_name(),
      RtclControllerFakeSession::ValidationMode::kNice, operational_state_,
      update_status_.AsStdFunction(), get_status_.AsStdFunction(), {});
  ActionDescription action_description_with_incompatible_part;
  action_description_with_incompatible_part.id = ActionInstanceId(1);
  action_description_with_incompatible_part.action_type_name =
      kIncompatibleActionType;
  action_description_with_incompatible_part.slot_part_map = {
      {/*slot_name=*/kDefaultSlotName,
       /*part_name=*/kPart1Name}};
  ASSERT_TRUE(action_description_with_incompatible_part.params.PackFrom(
      google::protobuf::BoolValue()));
  EXPECT_THAT(
      fake_session.AddActionsAndReactions(ActionsAndReactions{
          .action_descriptions = {action_description_with_incompatible_part}}),
      StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(RtclControllerFakeSessionTest, AddActionFailsWithUnknownActionTypeName) {
  RtclControllerFakeSession fake_session(
      SessionWill(), part_configs_by_name(),
      RtclControllerFakeSession::ValidationMode::kNice, operational_state_,
      update_status_.AsStdFunction(), get_status_.AsStdFunction(), {});
  ActionDescription action_description_with_unknown_action_type;
  action_description_with_unknown_action_type.id = ActionInstanceId(1);
  action_description_with_unknown_action_type.action_type_name =
      absl::StrCat("not_", kCompatibleActionType);
  action_description_with_unknown_action_type.slot_part_map = {
      {/*slot_name=*/kDefaultSlotName,
       /*part_name=*/kPart1Name}};
  ASSERT_TRUE(action_description_with_unknown_action_type.params.PackFrom(
      google::protobuf::BoolValue()));
  EXPECT_THAT(
      fake_session.AddActionsAndReactions(ActionsAndReactions{
          .action_descriptions =
              {action_description_with_unknown_action_type}}),
      StatusIs(absl::StatusCode::kNotFound, HasSubstr("No ActionSignature")));
}

TEST_F(RtclControllerFakeSessionTest, AddActionFailsWithUnknownSlotName) {
  RtclControllerFakeSession fake_session(
      SessionWill(), part_configs_by_name(),
      RtclControllerFakeSession::ValidationMode::kNice, operational_state_,
      update_status_.AsStdFunction(), get_status_.AsStdFunction(), {});
  ActionDescription action_description_with_unknown_slot_name;
  action_description_with_unknown_slot_name.id = ActionInstanceId(1);
  action_description_with_unknown_slot_name.action_type_name =
      kIncompatibleActionType;
  action_description_with_unknown_slot_name.slot_part_map = {
      {/*slot_name=*/absl::StrCat("not", kDefaultSlotName),
       /*part_name=*/kPart1Name}};
  ASSERT_TRUE(action_description_with_unknown_slot_name.params.PackFrom(
      google::protobuf::BoolValue()));
  EXPECT_THAT(
      fake_session.AddActionsAndReactions(ActionsAndReactions{
          .action_descriptions = {action_description_with_unknown_slot_name}}),
      StatusIs(absl::StatusCode::kNotFound));
}

TEST_F(RtclControllerFakeSessionTest, AddActionFailsForNonControlledPart) {
  auto part_configs_by_name = this->part_configs_by_name();
  part_configs_by_name.erase(kPart1Name);
  RtclControllerFakeSession fake_session(
      SessionWill(), part_configs_by_name,
      RtclControllerFakeSession::ValidationMode::kNice, operational_state_,
      update_status_.AsStdFunction(), get_status_.AsStdFunction(), {});
  ActionDescription action_description_with_uncontrolled_part_name;
  action_description_with_uncontrolled_part_name.id = ActionInstanceId(1);
  action_description_with_uncontrolled_part_name.action_type_name =
      kIncompatibleActionType;
  action_description_with_uncontrolled_part_name.slot_part_map = {
      {/*slot_name=*/kDefaultSlotName,
       /*part_name=*/kPart1Name}};
  ASSERT_TRUE(action_description_with_uncontrolled_part_name.params.PackFrom(
      google::protobuf::BoolValue()));
  EXPECT_THAT(fake_session.AddActionsAndReactions(ActionsAndReactions{
                  .action_descriptions =
                      {action_description_with_uncontrolled_part_name}}),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("not controlled by this session")));
}

TEST_F(RtclControllerFakeSessionTest, CannotAddActionWithDuplicateID) {
  RtclControllerFakeSession fake_session(
      SessionWill(), part_configs_by_name(),
      RtclControllerFakeSession::ValidationMode::kNice, operational_state_,
      update_status_.AsStdFunction(), get_status_.AsStdFunction(), {});
  // Add the same action description twice to trigger duplicate action ID
  // detection.
  EXPECT_THAT(fake_session.AddActionsAndReactions(
                  ActionsAndReactions{.action_descriptions{
                      MakeValidActionDescription(ActionInstanceId(1)),
                      MakeValidActionDescription(ActionInstanceId(1)),
                  }}),
              StatusIs(absl::StatusCode::kAlreadyExists));
}

TEST_F(RtclControllerFakeSessionTest, CannotAddReactionFromUnknownAction) {
  RtclControllerFakeSession fake_session(
      SessionWill(), part_configs_by_name(),
      RtclControllerFakeSession::ValidationMode::kNice, operational_state_,
      update_status_.AsStdFunction(), get_status_.AsStdFunction(), {});
  ActionInstanceId action_id(1283);
  ActionsAndReactions actions_and_reactions;
  actions_and_reactions.action_descriptions.push_back(
      MakeValidActionDescription(action_id));
  intrinsic_proto::icon::v1::Reaction invalid_reaction;
  invalid_reaction.set_reaction_instance_id(123);
  // Adding one to the ID of the only action there is is sure to give us an
  // invalid action ID.
  invalid_reaction.mutable_action_association()->set_action_instance_id(
      action_id.value() + 1);
  actions_and_reactions.reaction_descriptions.push_back(invalid_reaction);
  EXPECT_THAT(
      fake_session.AddActionsAndReactions(actions_and_reactions),
      StatusIs(
          absl::StatusCode::kNotFound,
          HasSubstr(absl::StrCat(invalid_reaction.mutable_action_association()
                                     ->action_instance_id()))));
}

TEST_F(RtclControllerFakeSessionTest,
       CannotAddReactionThatSwitchesToUnknownAction) {
  RtclControllerFakeSession fake_session(
      SessionWill(), part_configs_by_name(),
      RtclControllerFakeSession::ValidationMode::kNice, operational_state_,
      update_status_.AsStdFunction(), get_status_.AsStdFunction(), {});
  ActionInstanceId action_id(1283);
  ActionsAndReactions actions_and_reactions;
  actions_and_reactions.action_descriptions.push_back(
      MakeValidActionDescription(action_id));
  intrinsic_proto::icon::v1::Reaction invalid_reaction;
  invalid_reaction.set_reaction_instance_id(123);
  invalid_reaction.mutable_action_association()->set_action_instance_id(
      action_id.value());
  // Adding one to the ID of the only action there is is sure to give us an
  // invalid action ID.
  invalid_reaction.mutable_response()->set_start_action_instance_id(
      action_id.value() + 1);
  actions_and_reactions.reaction_descriptions.push_back(invalid_reaction);
  EXPECT_THAT(
      fake_session.AddActionsAndReactions(actions_and_reactions),
      StatusIs(absl::StatusCode::kNotFound,
               HasSubstr(absl::StrCat(
                   invalid_reaction.response().start_action_instance_id()))));
}

TEST_F(RtclControllerFakeSessionTest, CanAddValidReaction) {
  RtclControllerFakeSession fake_session(
      SessionWill(), part_configs_by_name(),
      RtclControllerFakeSession::ValidationMode::kNice, operational_state_,
      update_status_.AsStdFunction(), get_status_.AsStdFunction(), {});
  ActionInstanceId action_id(1283);
  ActionsAndReactions actions_and_reactions;
  actions_and_reactions.action_descriptions.push_back(
      MakeValidActionDescription(action_id));
  intrinsic_proto::icon::v1::Reaction valid_reaction;
  valid_reaction.set_reaction_instance_id(123);
  valid_reaction.mutable_action_association()->set_action_instance_id(
      action_id.value());
  actions_and_reactions.reaction_descriptions.push_back(valid_reaction);
  EXPECT_OK(fake_session.AddActionsAndReactions(actions_and_reactions));
}

TEST_F(RtclControllerFakeSessionTest, CannotAddDuplicateReaction) {
  RtclControllerFakeSession fake_session(
      SessionWill(), part_configs_by_name(),
      RtclControllerFakeSession::ValidationMode::kNice, operational_state_,
      update_status_.AsStdFunction(), get_status_.AsStdFunction(), {});
  ActionInstanceId action_id(1283);
  ActionsAndReactions actions_and_reactions;
  actions_and_reactions.action_descriptions.push_back(
      MakeValidActionDescription(action_id));
  intrinsic_proto::icon::v1::Reaction valid_reaction;
  valid_reaction.set_reaction_instance_id(123);
  valid_reaction.mutable_action_association()->set_action_instance_id(
      action_id.value());
  // Add the reaction twice to trigger duplicate reaction ID detection.
  actions_and_reactions.reaction_descriptions.push_back(valid_reaction);
  actions_and_reactions.reaction_descriptions.push_back(valid_reaction);
  EXPECT_THAT(fake_session.AddActionsAndReactions(actions_and_reactions),
              StatusIs(absl::StatusCode::kAlreadyExists));
}

TEST_F(RtclControllerFakeSessionTest, GetActionInstanceIds) {
  RtclControllerFakeSession fake_session(
      SessionWill(), part_configs_by_name(),
      RtclControllerFakeSession::ValidationMode::kNice, operational_state_,
      update_status_.AsStdFunction(), get_status_.AsStdFunction(), {});
  EXPECT_THAT(fake_session.GetActionInstanceIds(), IsEmpty());

  ActionsAndReactions actions_and_reactions;
  actions_and_reactions.action_descriptions.push_back(
      MakeValidActionDescription(ActionInstanceId(128)));
  EXPECT_OK(fake_session.AddActionsAndReactions(actions_and_reactions));

  EXPECT_THAT(fake_session.GetActionInstanceIds(),
              ElementsAre(ActionInstanceId(128)));
}

TEST_F(RtclControllerFakeSessionTest,
       RemoveActionsAndReactionsFailsForUnknownAction) {
  RtclControllerFakeSession fake_session(
      SessionWill(), part_configs_by_name(),
      RtclControllerFakeSession::ValidationMode::kNice, operational_state_,
      update_status_.AsStdFunction(), get_status_.AsStdFunction(), {});
  EXPECT_THAT(fake_session.RemoveActionsAndReactions(
                  ActionAndReactionIds{.action_ids = {ActionInstanceId(19)}}),
              StatusIs(absl::StatusCode::kNotFound,
                       AllOf(HasSubstr("19"), HasSubstr("Action"))));
}

TEST_F(RtclControllerFakeSessionTest,
       RemoveActionsAndReactionsFailsForUnknownReaction) {
  RtclControllerFakeSession fake_session(
      SessionWill(), part_configs_by_name(),
      RtclControllerFakeSession::ValidationMode::kNice, operational_state_,
      update_status_.AsStdFunction(), get_status_.AsStdFunction(), {});
  EXPECT_THAT(fake_session.RemoveActionsAndReactions(
                  ActionAndReactionIds{.reaction_ids = {ReactionId(19)}}),
              StatusIs(absl::StatusCode::kNotFound,
                       AllOf(HasSubstr("19"), HasSubstr("Reaction"))));
}

TEST_F(RtclControllerFakeSessionTest,
       RemoveActionsAndReactionsSucceedsWithNoRemovals) {
  RtclControllerFakeSession fake_session(
      SessionWill(), part_configs_by_name(),
      RtclControllerFakeSession::ValidationMode::kNice, operational_state_,
      update_status_.AsStdFunction(), get_status_.AsStdFunction(), {});
  EXPECT_OK(fake_session.RemoveActionsAndReactions(ActionAndReactionIds()));
}

TEST_F(RtclControllerFakeSessionTest, RemoveAction) {
  RtclControllerFakeSession fake_session(
      SessionWill(), part_configs_by_name(),
      RtclControllerFakeSession::ValidationMode::kNice, operational_state_,
      update_status_.AsStdFunction(), get_status_.AsStdFunction(), {});
  ActionsAndReactions actions_and_reactions;
  actions_and_reactions.action_descriptions.push_back(
      MakeValidActionDescription(ActionInstanceId(1)));
  actions_and_reactions.action_descriptions.push_back(
      MakeValidActionDescription(ActionInstanceId(2)));
  EXPECT_OK(fake_session.AddActionsAndReactions(actions_and_reactions));
  EXPECT_OK(fake_session.StartActions({ActionInstanceId(1)},
                                      /*stop_active_actions=*/true));

  EXPECT_OK(fake_session.RemoveActionsAndReactions(
      ActionAndReactionIds{.action_ids = {ActionInstanceId(1)}}));

  // The action we did not remove should still be there.
  EXPECT_THAT(fake_session.GetActionInstanceIds(),
              ElementsAre(ActionInstanceId(2)));
  // Since we deleted the active action, now there isn't an active action any
  // more.
  EXPECT_THAT(fake_session.GetActiveActionIds(), IsEmpty());
}

TEST_F(RtclControllerFakeSessionTest, RemoveReaction) {
  RtclControllerFakeSession fake_session(
      SessionWill(), part_configs_by_name(),
      RtclControllerFakeSession::ValidationMode::kNice, operational_state_,
      update_status_.AsStdFunction(), get_status_.AsStdFunction(), {});
  ActionsAndReactions actions_and_reactions;
  actions_and_reactions.action_descriptions.push_back(
      MakeValidActionDescription(ActionInstanceId(1)));
  actions_and_reactions.action_descriptions.push_back(
      MakeValidActionDescription(ActionInstanceId(2)));

  intrinsic_proto::icon::v1::Reaction reaction;
  reaction.set_reaction_instance_id(123);
  reaction.mutable_action_association()->set_action_instance_id(1);
  actions_and_reactions.reaction_descriptions.push_back(reaction);
  EXPECT_OK(fake_session.AddActionsAndReactions(actions_and_reactions));

  EXPECT_OK(fake_session.RemoveActionsAndReactions(
      ActionAndReactionIds{.reaction_ids = {ReactionId(123)}}));

  // Removing the reaction a second time should fail
  EXPECT_THAT(fake_session.RemoveActionsAndReactions(
                  ActionAndReactionIds{.reaction_ids = {ReactionId(123)}}),
              StatusIs(absl::StatusCode::kNotFound));
}

TEST_F(RtclControllerFakeSessionTest, RemoveActionRemovesDependentReactions) {
  RtclControllerFakeSession fake_session(
      SessionWill(), part_configs_by_name(),
      RtclControllerFakeSession::ValidationMode::kNice, operational_state_,
      update_status_.AsStdFunction(), get_status_.AsStdFunction(), {});
  ActionsAndReactions actions_and_reactions;
  actions_and_reactions.action_descriptions.push_back(
      MakeValidActionDescription(ActionInstanceId(1)));
  actions_and_reactions.action_descriptions.push_back(
      MakeValidActionDescription(ActionInstanceId(2)));

  intrinsic_proto::icon::v1::Reaction reaction_from_deleted_action;
  reaction_from_deleted_action.set_reaction_instance_id(123);
  reaction_from_deleted_action.mutable_action_association()
      ->set_action_instance_id(1);
  actions_and_reactions.reaction_descriptions.push_back(
      reaction_from_deleted_action);

  intrinsic_proto::icon::v1::Reaction reaction_to_deleted_action;
  reaction_to_deleted_action.set_reaction_instance_id(456);
  reaction_to_deleted_action.mutable_action_association()
      ->set_action_instance_id(2);
  reaction_to_deleted_action.mutable_response()->set_start_action_instance_id(
      1);
  actions_and_reactions.reaction_descriptions.push_back(
      reaction_to_deleted_action);

  EXPECT_OK(fake_session.AddActionsAndReactions(actions_and_reactions));

  // Removing the first action should also remove both reactions, since they
  // both refer to it.
  EXPECT_OK(fake_session.RemoveActionsAndReactions(
      ActionAndReactionIds{.action_ids = {ActionInstanceId(1)}}));

  // Verify that both reactions were removed by trying to remove them again. If
  // they were removed by the call above, then these following calls must return
  // NotFound.
  EXPECT_THAT(fake_session.RemoveActionsAndReactions(ActionAndReactionIds{
                  .reaction_ids = {ReactionId(
                      reaction_from_deleted_action.reaction_instance_id())}}),
              StatusIs(absl::StatusCode::kNotFound));
  EXPECT_THAT(fake_session.RemoveActionsAndReactions(ActionAndReactionIds{
                  .reaction_ids = {ReactionId(
                      reaction_to_deleted_action.reaction_instance_id())}}),
              StatusIs(absl::StatusCode::kNotFound));
}

TEST_F(RtclControllerFakeSessionTest, RemoveAllActionsAndReactions) {
  RtclControllerFakeSession fake_session(
      SessionWill(), part_configs_by_name(),
      RtclControllerFakeSession::ValidationMode::kNice, operational_state_,
      update_status_.AsStdFunction(), get_status_.AsStdFunction(), {});
  ActionsAndReactions actions_and_reactions;
  actions_and_reactions.action_descriptions.push_back(
      MakeValidActionDescription(ActionInstanceId(1)));
  actions_and_reactions.action_descriptions.push_back(
      MakeValidActionDescription(ActionInstanceId(2)));

  intrinsic_proto::icon::v1::Reaction reaction_1;
  reaction_1.set_reaction_instance_id(123);
  reaction_1.mutable_action_association()->set_action_instance_id(1);
  actions_and_reactions.reaction_descriptions.push_back(reaction_1);

  intrinsic_proto::icon::v1::Reaction reaction_2;
  reaction_2.set_reaction_instance_id(456);
  reaction_2.mutable_action_association()->set_action_instance_id(2);
  actions_and_reactions.reaction_descriptions.push_back(reaction_2);
  EXPECT_OK(fake_session.AddActionsAndReactions(actions_and_reactions));
  EXPECT_OK(fake_session.StartActions({ActionInstanceId(1)},
                                      /*stop_active_actions=*/true));
  EXPECT_OK(fake_session.RemoveAllActionsAndReactions());

  // Check that all Actions have been deleted.
  EXPECT_THAT(fake_session.GetActionInstanceIds(), IsEmpty());
  // Check that both Reactions have been deleted.
  EXPECT_THAT(
      fake_session.RemoveActionsAndReactions(ActionAndReactionIds{
          .reaction_ids = {ReactionId(reaction_1.reaction_instance_id())}}),
      StatusIs(absl::StatusCode::kNotFound));
  EXPECT_THAT(
      fake_session.RemoveActionsAndReactions(ActionAndReactionIds{
          .reaction_ids = {ReactionId(reaction_2.reaction_instance_id())}}),
      StatusIs(absl::StatusCode::kNotFound));
  // Since we deleted the active action, now there isn't an active action any
  // more.
  EXPECT_THAT(fake_session.GetActiveActionIds(), IsEmpty());
}

TEST_F(RtclControllerFakeSessionTest, StartActionWithUnknownActionFails) {
  RtclControllerFakeSession fake_session(
      SessionWill(), part_configs_by_name(),
      RtclControllerFakeSession::ValidationMode::kNice, operational_state_,
      update_status_.AsStdFunction(), get_status_.AsStdFunction(), {});
  EXPECT_THAT(fake_session.StartActions({ActionInstanceId(2837)},
                                        /*stop_active_actions=*/true),
              StatusIs(absl::StatusCode::kNotFound));
}

TEST_F(RtclControllerFakeSessionTest, StartActionWithWrongIdFailsIfStrict) {
  // We expect a failure since not all ActionWills will be used.

  auto fake_session = std::make_unique<RtclControllerFakeSession>(
      SessionWill()
          .OnAction(ActionInstanceId(1), ActionWill())
          .OnAction(ActionInstanceId(2), ActionWill()),
      part_configs_by_name(),
      RtclControllerFakeSession::ValidationMode::kStrict, operational_state_,
      update_status_.AsStdFunction(), get_status_.AsStdFunction());

  EXPECT_OK(fake_session->AddActionsAndReactions(ActionsAndReactions{
      .action_descriptions = {
          MakeValidActionDescription(ActionInstanceId(1)),
          MakeValidActionDescription(ActionInstanceId(2))}}));
  EXPECT_THAT(fake_session->StartActions({ActionInstanceId(2)},
                                         /*stop_active_actions=*/true),
              StatusIs(absl::StatusCode::kFailedPrecondition));
  EXPECT_NONFATAL_FAILURE({ fake_session.reset(); }, "unmet");
}

TEST_F(RtclControllerFakeSessionTest, StartActionWithWrongIdSucceedsIfNice) {
  RtclControllerFakeSession fake_session(
      SessionWill()
          .OnAction(ActionInstanceId(1), ActionWill())
          .OnAction(ActionInstanceId(2), ActionWill()),
      part_configs_by_name(), RtclControllerFakeSession::ValidationMode::kNice,
      operational_state_, update_status_.AsStdFunction(),
      get_status_.AsStdFunction(), {});

  EXPECT_OK(fake_session.AddActionsAndReactions(ActionsAndReactions{
      .action_descriptions = {
          MakeValidActionDescription(ActionInstanceId(1)),
          MakeValidActionDescription(ActionInstanceId(2))}}));
  EXPECT_OK(fake_session.StartActions({ActionInstanceId(2)},
                                      /*stop_active_actions=*/true));
}

TEST_F(RtclControllerFakeSessionTest,
       StartActionInNiceModeSkipsUnvisitedExpectations) {
  bool action_1_started = false;
  bool action_2_started = false;
  RtclControllerFakeSession fake_session(
      SessionWill()
          .OnAction(
              ActionInstanceId(1),
              ActionWill()
                  .ExpectParameter<google::protobuf::BoolValue>(
                      [](const google::protobuf::BoolValue& param) {
                        return absl::InvalidArgumentError(
                            "Action 1 matcher should not be called.");
                      })
                  .OnStart([&action_1_started]() { action_1_started = true; }))
          .OnAction(
              ActionInstanceId(2),
              ActionWill()
                  .ExpectParameter<google::protobuf::BoolValue>(
                      [](const google::protobuf::BoolValue& param) {
                        if (!param.value()) {
                          return absl::InvalidArgumentError("Expected true.");
                        }
                        return absl::OkStatus();
                      })
                  .OnStart([&action_2_started]() { action_2_started = true; })
                  .ReportStateVariables({{kStateVariableName, true}})),
      part_configs_by_name(), RtclControllerFakeSession::ValidationMode::kNice,
      operational_state_, update_status_.AsStdFunction(),
      get_status_.AsStdFunction(), {});

  EXPECT_OK(fake_session.AddActionsAndReactions(ActionsAndReactions{
      .action_descriptions =
          {MakeValidActionDescription(ActionInstanceId(1),
                                      /*param_bool_value=*/false),
           MakeValidActionDescription(ActionInstanceId(2),
                                      /*param_bool_value=*/true)},
      .reaction_descriptions = {ReactionDescriptor::ToProto(
          ReactionDescriptor(IsTrue(kStateVariableName)),
          /*reaction_id=*/ReactionId(10),
          /*action_id=*/ActionInstanceId(2))}}));
  EXPECT_OK(fake_session.StartActions({ActionInstanceId(2)},
                                      /*stop_active_actions=*/true));
  EXPECT_FALSE(action_1_started);
  EXPECT_TRUE(action_2_started);
  EXPECT_THAT(fake_session.PollReactions(), IsOkAndHolds(Ne(std::nullopt)));
}

TEST_F(
    RtclControllerFakeSessionTest,
    StartActionWithoutExpectationInNiceModeDoesNotRunOtherActionExpectation) {
  RtclControllerFakeSession fake_session(
      SessionWill().OnAction(
          ActionInstanceId(1),
          ActionWill().ExpectParameter<google::protobuf::BoolValue>(
              [](const google::protobuf::BoolValue& param) {
                return absl::InvalidArgumentError(
                    "Action 1 matcher should not be called.");
              })),
      part_configs_by_name(), RtclControllerFakeSession::ValidationMode::kNice,
      operational_state_, update_status_.AsStdFunction(),
      get_status_.AsStdFunction(), {});

  EXPECT_OK(fake_session.AddActionsAndReactions(ActionsAndReactions{
      .action_descriptions = {
          MakeValidActionDescription(ActionInstanceId(1),
                                     /*param_bool_value=*/false),
          MakeValidActionDescription(ActionInstanceId(2),
                                     /*param_bool_value=*/true)}}));
  EXPECT_OK(fake_session.StartActions({ActionInstanceId(2)},
                                      /*stop_active_actions=*/true));
}

TEST_F(RtclControllerFakeSessionTest,
       StartActionWithNoMoreExpectationsFailsIfStrict) {
  RtclControllerFakeSession fake_session(
      SessionWill().OnAction(ActionInstanceId(1), ActionWill()),
      part_configs_by_name(),
      RtclControllerFakeSession::ValidationMode::kStrict, operational_state_,
      update_status_.AsStdFunction(), get_status_.AsStdFunction(), {});

  EXPECT_OK(fake_session.AddActionsAndReactions(
      ActionsAndReactions{.action_descriptions = {MakeValidActionDescription(
                              ActionInstanceId(1))}}));
  // This matches the expectation above.
  EXPECT_OK(fake_session.StartActions({ActionInstanceId(1)},
                                      /*stop_active_actions=*/true));
  // Process the action will.
  EXPECT_OK(fake_session.PollReactions().status());
  // There are no more expectations, and the fake is strict.
  EXPECT_THAT(fake_session.StartActions({ActionInstanceId(1)},
                                        /*stop_active_actions=*/true),
              StatusIs(absl::StatusCode::kOutOfRange));
}

TEST_F(RtclControllerFakeSessionTest, StartActionSucceeds) {
  RtclControllerFakeSession fake_session(
      SessionWill(), part_configs_by_name(),
      RtclControllerFakeSession::ValidationMode::kNice, operational_state_,
      update_status_.AsStdFunction(), get_status_.AsStdFunction(), {});

  EXPECT_OK(fake_session.AddActionsAndReactions(
      ActionsAndReactions{.action_descriptions = {MakeValidActionDescription(
                              ActionInstanceId(1))}}));
  EXPECT_OK(fake_session.StartActions({ActionInstanceId(1)},
                                      /*stop_active_actions=*/true));

  EXPECT_TRUE(fake_session.GetActiveActionIds().contains(ActionInstanceId(1)));
}

TEST_F(RtclControllerFakeSessionTest,
       StartMultipleActionSucceedsAndChecksParams) {
  RtclControllerFakeSession fake_session(
      SessionWill()
          .OnAction(ActionInstanceId(1),
                    ActionWill().ExpectParameter<google::protobuf::BoolValue>(
                        [](const google::protobuf::BoolValue& param) {
                          if (param.value()) {
                            return absl::InternalError("oh no.");
                          }
                          return absl::OkStatus();
                        }))
          .OnAction(ActionInstanceId(2),
                    ActionWill().ExpectParameter<google::protobuf::BoolValue>(
                        [](const google::protobuf::BoolValue& param) {
                          if (!param.value()) {
                            return absl::InternalError("oh no.");
                          }
                          return absl::OkStatus();
                        })),
      part_configs_by_name(), RtclControllerFakeSession::ValidationMode::kNice,
      operational_state_, update_status_.AsStdFunction(),
      get_status_.AsStdFunction(), {});

  EXPECT_OK(fake_session.AddActionsAndReactions(ActionsAndReactions{
      .action_descriptions = {
          MakeValidActionDescription(ActionInstanceId(1), false),
          MakeValidActionDescription(ActionInstanceId(2), true)}}));
  EXPECT_OK(
      fake_session.StartActions({ActionInstanceId(1), ActionInstanceId(2)},
                                /*stop_active_actions=*/true));
  EXPECT_THAT(fake_session.GetActiveActionIds(),
              UnorderedElementsAre(ActionInstanceId(1), ActionInstanceId(2)));
}

TEST_F(RtclControllerFakeSessionTest, StartParallelActionFails) {
  RtclControllerFakeSession fake_session(
      SessionWill()
          .OnAction(ActionInstanceId(1),
                    ActionWill().ExpectParameter<google::protobuf::BoolValue>(
                        [](const google::protobuf::BoolValue& param) {
                          if (param.value()) {
                            return absl::InternalError("oh no.");
                          }
                          return absl::OkStatus();
                        }))
          .OnAction(ActionInstanceId(2),
                    ActionWill().ExpectParameter<google::protobuf::BoolValue>(
                        [](const google::protobuf::BoolValue& param) {
                          if (!param.value()) {
                            return absl::InternalError("oh no.");
                          }
                          return absl::OkStatus();
                        })),
      part_configs_by_name(), RtclControllerFakeSession::ValidationMode::kNice,
      operational_state_, update_status_.AsStdFunction(),
      get_status_.AsStdFunction(), {});

  EXPECT_OK(fake_session.AddActionsAndReactions(ActionsAndReactions{
      .action_descriptions = {
          MakeValidActionDescription(ActionInstanceId(1), false),
          MakeValidActionDescription(ActionInstanceId(2), true)}}));
  EXPECT_OK(fake_session.StartActions({ActionInstanceId(1)},
                                      /*stop_active_actions=*/true));
  EXPECT_THAT(fake_session.GetActiveActionIds(),
              UnorderedElementsAre(ActionInstanceId(1)));
  EXPECT_OK(fake_session.StartActions({ActionInstanceId(2)},
                                      /*stop_active_actions=*/false));
  EXPECT_THAT(fake_session.GetActiveActionIds(),
              UnorderedElementsAre(ActionInstanceId(1), ActionInstanceId(2)));
}

TEST_F(RtclControllerFakeSessionTest, PollReactionsFailsIfFaulted) {
  ASSERT_OK(operational_state_.Enable());
  operational_state_.Fault("fell over");
  {
    ASSERT_OK_AND_ASSIGN(auto operational_status,
                         operational_state_.GetStatus());
    ASSERT_EQ(operational_status.state(), OperationalState::kFaulted);
  }
  RtclControllerFakeSession fake_session(
      SessionWill(), part_configs_by_name(),
      RtclControllerFakeSession::ValidationMode::kNice, operational_state_,
      update_status_.AsStdFunction(), get_status_.AsStdFunction(), {});

  EXPECT_THAT(fake_session.PollReactions(),
              StatusIs(absl::StatusCode::kAborted, HasSubstr("fell over")));
}

TEST_F(RtclControllerFakeSessionTest, PollReactionsFailsIfDisabled) {
  EXPECT_OK(operational_state_.SetDisabled());
  {
    ASSERT_OK_AND_ASSIGN(auto operational_status,
                         operational_state_.GetStatus());
    ASSERT_EQ(operational_status.state(), OperationalState::kDisabled);
  }
  RtclControllerFakeSession fake_session(
      SessionWill(), part_configs_by_name(),
      RtclControllerFakeSession::ValidationMode::kNice, operational_state_,
      update_status_.AsStdFunction(), get_status_.AsStdFunction(), {});

  EXPECT_THAT(fake_session.PollReactions(),
              StatusIs(absl::StatusCode::kAborted, HasSubstr("disabled")));
}

TEST_F(RtclControllerFakeSessionTest,
       PollReactionsReturnsNulloptWithoutActiveAction) {
  RtclControllerFakeSession fake_session(
      SessionWill(), part_configs_by_name(),
      RtclControllerFakeSession::ValidationMode::kNice, operational_state_,
      update_status_.AsStdFunction(), get_status_.AsStdFunction(), {});
  EXPECT_THAT(fake_session.PollReactions(), IsOkAndHolds(Eq(std::nullopt)));
}

TEST_F(RtclControllerFakeSessionTest, PollReactionsReturnsReactionEvents) {
  ActionInstanceId action_id(123);
  ActionInstanceId action_id_2(456);
  RtclControllerFakeSession fake_session(
      SessionWill().OnAction(
          action_id, ActionWill()
                         .ReportStateVariables({{kStateVariableName, false}})
                         .ReportStateVariables({{kStateVariableName, true}})),
      part_configs_by_name(), RtclControllerFakeSession::ValidationMode::kNice,
      operational_state_, update_status_.AsStdFunction(),
      get_status_.AsStdFunction(), {});

  ReactionId reaction_id_1(123);
  ReactionId reaction_id_2(456);
  EXPECT_OK(fake_session.AddActionsAndReactions(ActionsAndReactions{
      .action_descriptions = {MakeValidActionDescription(action_id),
                              MakeValidActionDescription(action_id_2)},
      .reaction_descriptions = {
          // This reaction won't fire at all.
          ReactionDescriptor::ToProto(
              ReactionDescriptor(
                  IsTrue(absl::StrCat("not_", kStateVariableName))),
              /*reaction_id=*/ReactionId(789), /*action_id=*/action_id_2),
          // This reaction will fire once the second state variable map becomes
          // active.
          ReactionDescriptor::ToProto(
              ReactionDescriptor(IsTrue(kStateVariableName))
                  .WithRealtimeActionOnCondition(action_id_2),
              /*reaction_id=*/reaction_id_1, /*action_id=*/action_id),
          // This reaction fires at the same time as the previous one, but does
          // not change the active action.
          ReactionDescriptor::ToProto(
              ReactionDescriptor(IsTrue(kStateVariableName)),
              /*reaction_id=*/reaction_id_2, /*action_id=*/action_id)}}));

  // Start the first action (which the two reactions we expect to fire are
  // attached to).
  EXPECT_OK(
      fake_session.StartActions({action_id}, /*stop_active_actions=*/true));
  EXPECT_TRUE(fake_session.GetActiveActionIds().contains(action_id));
  // First call should return no event, since the reaction does not trigger.
  EXPECT_THAT(fake_session.PollReactions(), IsOkAndHolds(Eq(std::nullopt)));
  // For this call, the second state variable map should be in effect and
  // trigger both reactions.
  ASSERT_OK_AND_ASSIGN(std::optional<ReactionEvent> reaction_event_1,
                       fake_session.PollReactions());
  ASSERT_TRUE(reaction_event_1.has_value());
  ASSERT_OK_AND_ASSIGN(std::optional<ReactionEvent> reaction_event_2,
                       fake_session.PollReactions());
  ASSERT_TRUE(reaction_event_2.has_value());
  // No more reactions after the two we expect.
  EXPECT_THAT(fake_session.PollReactions(), IsOkAndHolds(Eq(std::nullopt)));

  // We don't know which order the two events are going to arrive in, so "sort"
  // them.
  const ReactionEvent& min_id_event =
      reaction_event_1->id.value() < reaction_event_2->id.value()
          ? *reaction_event_1
          : *reaction_event_2;
  const ReactionEvent& max_id_event =
      reaction_event_1->id.value() < reaction_event_2->id.value()
          ? *reaction_event_2
          : *reaction_event_1;

  EXPECT_EQ(min_id_event.id, reaction_id_1);
  EXPECT_EQ(min_id_event.previous_action_id, action_id);
  EXPECT_EQ(min_id_event.current_action_id, action_id_2);
  EXPECT_EQ(max_id_event.id, reaction_id_2);
  EXPECT_EQ(max_id_event.previous_action_id, action_id);
  EXPECT_EQ(max_id_event.current_action_id, action_id);

  EXPECT_TRUE(fake_session.GetActiveActionIds().contains(action_id_2));
}

TEST_F(RtclControllerFakeSessionTest, PollReactionsCanStartParallelActions) {
  ActionInstanceId action_id(123);
  ActionInstanceId action_id_2(456);
  RtclControllerFakeSession fake_session(
      SessionWill().OnAction(
          action_id,
          ActionWill()
              .ReportStateVariables(
                  {{kStateVariableName, false}})  // Won't fire reaction.
              .ReportStateVariables({{kStateVariableName, true}})),
      part_configs_by_name(), RtclControllerFakeSession::ValidationMode::kNice,
      operational_state_, update_status_.AsStdFunction(),
      get_status_.AsStdFunction(), {});

  ReactionId reaction_id_1(123);
  EXPECT_OK(fake_session.AddActionsAndReactions(ActionsAndReactions{
      .action_descriptions = {MakeValidActionDescription(action_id),
                              MakeValidActionDescription(action_id_2)},
      .reaction_descriptions = {
          // This reaction will fire once the second state variable map becomes
          // active.
          ReactionDescriptor::ToProto(
              ReactionDescriptor(IsTrue(kStateVariableName))
                  .WithParallelRealtimeActionOnCondition(action_id_2),
              /*reaction_id=*/reaction_id_1, /*action_id=*/action_id)}}));

  // Start the first action (which the reaction we expect to fire is attached
  // to).
  EXPECT_OK(
      fake_session.StartActions({action_id}, /*stop_active_actions=*/true));
  EXPECT_TRUE(fake_session.GetActiveActionIds().contains(action_id));
  // First call should return no event, since the reaction does not trigger.
  EXPECT_THAT(fake_session.PollReactions(), IsOkAndHolds(Eq(std::nullopt)));
  // For this call, the second state variable map should be in effect and
  // trigger both reactions.
  ASSERT_OK_AND_ASSIGN(std::optional<ReactionEvent> reaction_event,
                       fake_session.PollReactions());
  ASSERT_TRUE(reaction_event.has_value());
  // No more reactions after the two we expect.
  EXPECT_THAT(fake_session.PollReactions(), IsOkAndHolds(Eq(std::nullopt)));

  EXPECT_EQ(reaction_event->id, reaction_id_1);
  EXPECT_EQ(reaction_event->previous_action_id, action_id);
  EXPECT_EQ(reaction_event->current_action_id, action_id_2);
  EXPECT_THAT(fake_session.GetActiveActionIds(),
              UnorderedElementsAre(action_id, action_id_2));
}

TEST_F(RtclControllerFakeSessionTest,
       PollReactionsFailsIfReactionsStartActionsOnSamePartInSameCycle) {
  const ActionInstanceId action_id(1);
  const ActionInstanceId parallel_action_id_1(2);
  const ActionInstanceId parallel_action_id_2(3);
  RtclControllerFakeSession fake_session(
      SessionWill().OnAction(
          action_id, ActionWill()
                         .ReportStateVariables({{kStateVariableName, false}})
                         .ReportStateVariables({{kStateVariableName, true}})),
      part_configs_by_name(), RtclControllerFakeSession::ValidationMode::kNice,
      operational_state_, update_status_.AsStdFunction(),
      get_status_.AsStdFunction(), {});
  // Both parallel actions use kPart1Name (see MakeValidActionDescription).
  ASSERT_OK(fake_session.AddActionsAndReactions(ActionsAndReactions{
      .action_descriptions = {MakeValidActionDescription(action_id),
                              MakeValidActionDescription(parallel_action_id_1),
                              MakeValidActionDescription(parallel_action_id_2)},
      .reaction_descriptions = {
          ReactionDescriptor::ToProto(
              ReactionDescriptor(IsTrue(kStateVariableName))
                  .WithParallelRealtimeActionOnCondition(parallel_action_id_1),
              /*reaction_id=*/ReactionId(10), /*action_id=*/action_id),
          ReactionDescriptor::ToProto(
              ReactionDescriptor(IsTrue(kStateVariableName))
                  .WithParallelRealtimeActionOnCondition(parallel_action_id_2),
              /*reaction_id=*/ReactionId(11), /*action_id=*/action_id)}}));
  ASSERT_OK(
      fake_session.StartActions({action_id}, /*stop_active_actions=*/true));
  EXPECT_THAT(fake_session.PollReactions(), IsOkAndHolds(Eq(std::nullopt)));
  EXPECT_THAT(fake_session.PollReactions(),
              StatusIs(absl::StatusCode::kResourceExhausted));
}

TEST_F(RtclControllerFakeSessionTest,
       PollReactionsStartsActionsOnDifferentPartsInSameCycle) {
  const ActionInstanceId action_id(1);
  const ActionInstanceId parallel_action_id_1(2);
  const ActionInstanceId parallel_action_id_2(3);
  RtclControllerFakeSession fake_session(
      SessionWill().OnAction(
          action_id, ActionWill()
                         .ReportStateVariables({{kStateVariableName, false}})
                         .ReportStateVariables({{kStateVariableName, true}})),
      part_configs_by_name(), RtclControllerFakeSession::ValidationMode::kNice,
      operational_state_, update_status_.AsStdFunction(),
      get_status_.AsStdFunction(), {});
  ActionDescription parallel_action_2_desc =
      MakeValidActionDescription(parallel_action_id_2);
  parallel_action_2_desc.slot_part_map = {{kDefaultSlotName, kPart2Name}};
  ASSERT_OK(fake_session.AddActionsAndReactions(ActionsAndReactions{
      .action_descriptions = {MakeValidActionDescription(action_id),
                              MakeValidActionDescription(parallel_action_id_1),
                              parallel_action_2_desc},
      .reaction_descriptions = {
          ReactionDescriptor::ToProto(
              ReactionDescriptor(IsTrue(kStateVariableName))
                  .WithParallelRealtimeActionOnCondition(parallel_action_id_1),
              /*reaction_id=*/ReactionId(10), /*action_id=*/action_id),
          ReactionDescriptor::ToProto(
              ReactionDescriptor(IsTrue(kStateVariableName))
                  .WithParallelRealtimeActionOnCondition(parallel_action_id_2),
              /*reaction_id=*/ReactionId(11), /*action_id=*/action_id)}}));
  ASSERT_OK(
      fake_session.StartActions({action_id}, /*stop_active_actions=*/true));
  EXPECT_THAT(fake_session.PollReactions(), IsOkAndHolds(Eq(std::nullopt)));
  EXPECT_OK(fake_session.PollReactions().status());
  EXPECT_THAT(fake_session.GetActiveActionIds(),
              UnorderedElementsAre(action_id, parallel_action_id_1,
                                   parallel_action_id_2));
}

TEST_F(RtclControllerFakeSessionTest, OperationalStatusStartsEnabled) {
  RtclControllerFakeSession fake_session(
      SessionWill(), part_configs_by_name(),
      RtclControllerFakeSession::ValidationMode::kNice, operational_state_,
      update_status_.AsStdFunction(), get_status_.AsStdFunction(), {});
  {
    ASSERT_OK_AND_ASSIGN(OperationalStatus status,
                         operational_state_.GetStatus());
    EXPECT_EQ(status.state(), OperationalState::kEnabled);
  }
  EXPECT_THAT(fake_session.PollReactions(), IsOkAndHolds(Eq(std::nullopt)));
}

TEST_F(RtclControllerFakeSessionTest,
       ExpectationUpdatesOperationalStatusToFaulted) {
  ActionInstanceId action_id(123);
  RtclControllerFakeSession fake_session(
      SessionWill().OnAction(
          action_id, ActionWill().SetOperationalStatus(ParseTextProtoOrDie(R"pb(
            operational_status: { state: FAULTED fault_reason: "oh no." }
          )pb"))),
      part_configs_by_name(), RtclControllerFakeSession::ValidationMode::kNice,
      operational_state_, update_status_.AsStdFunction(),
      get_status_.AsStdFunction(), {});
  EXPECT_OK(fake_session.AddActionsAndReactions(ActionsAndReactions{
      .action_descriptions = {MakeValidActionDescription(action_id)}}));
  EXPECT_OK(
      fake_session.StartActions({action_id}, /*stop_active_actions=*/true));
  EXPECT_THAT(fake_session.PollReactions(), IsOkAndHolds(Eq(std::nullopt)));
  // Should be faulted with the chosen message.
  {
    ASSERT_OK_AND_ASSIGN(OperationalStatus status,
                         operational_state_.GetStatus());
    EXPECT_EQ(status.state(), OperationalState::kFaulted);
    EXPECT_EQ(status.fault_reason(), "oh no.");
  }
}

TEST_F(RtclControllerFakeSessionTest,
       ExpectationUpdatesOperationalStatusToEnabled) {
  ActionInstanceId action_id(123);
  RtclControllerFakeSession fake_session(
      SessionWill().OnAction(
          action_id, ActionWill().SetOperationalStatus(ParseTextProtoOrDie(R"pb(
            operational_status: { state: ENABLED }
          )pb"))),
      part_configs_by_name(), RtclControllerFakeSession::ValidationMode::kNice,
      operational_state_, update_status_.AsStdFunction(),
      get_status_.AsStdFunction(), {});
  EXPECT_OK(fake_session.AddActionsAndReactions(ActionsAndReactions{
      .action_descriptions = {MakeValidActionDescription(action_id)}}));
  EXPECT_OK(
      fake_session.StartActions({action_id}, /*stop_active_actions=*/true));
  EXPECT_THAT(fake_session.PollReactions(), IsOkAndHolds(Eq(std::nullopt)));
  // Should be enabled.
  ASSERT_OK_AND_ASSIGN(OperationalStatus status,
                       operational_state_.GetStatus());
  EXPECT_EQ(status.state(), OperationalState::kEnabled);
}

TEST_F(RtclControllerFakeSessionTest,
       EmptyExpectationDoesNotOverwriteOperationalStatus) {
  ActionInstanceId action_id(123);
  ActionInstanceId action_id_2(456);
  ReactionId reaction_id(123);
  RtclControllerFakeSession fake_session(
      SessionWill()
          .OnAction(action_id,
                    ActionWill()
                        .ReportStateVariables({{kStateVariableName, true}})
                        .SetOperationalStatus(ParseTextProtoOrDie(R"pb(
                          operational_status: { state: ENABLED }
                        )pb")))
          .OnAction(action_id_2, ActionWill()),
      part_configs_by_name(), RtclControllerFakeSession::ValidationMode::kNice,
      operational_state_, update_status_.AsStdFunction(),
      get_status_.AsStdFunction(), {});

  EXPECT_OK(fake_session.AddActionsAndReactions(ActionsAndReactions{
      .action_descriptions = {MakeValidActionDescription(action_id),
                              MakeValidActionDescription(action_id_2)},
      .reaction_descriptions = {
          ReactionDescriptor::ToProto(
              ReactionDescriptor(IsTrue(kStateVariableName))
                  .WithRealtimeActionOnCondition(action_id_2),
              /*reaction_id=*/reaction_id, /*action_id=*/action_id),
      }}));
  EXPECT_OK(
      fake_session.StartActions({action_id}, /*stop_active_actions=*/true));
  // Poll once to trigger the reaction, and once more to ensure that the second
  // action does not trigger a reaction of its own.
  EXPECT_THAT(fake_session.PollReactions(), IsOkAndHolds(Ne(std::nullopt)));
  EXPECT_THAT(fake_session.PollReactions(), IsOkAndHolds(Eq(std::nullopt)));
  // Should be enabled, even after switching to the second action.
  ASSERT_OK_AND_ASSIGN(OperationalStatus status,
                       operational_state_.GetStatus());
  EXPECT_EQ(status.state(), OperationalState::kEnabled);
}

TEST_F(RtclControllerFakeSessionTest, ExpectationUpdatesRobotStatus) {
  ActionInstanceId action_id(123);
  const intrinsic_proto::icon::v1::GetStatusResponse expected_status =
      ParseTextProtoOrDie(R"pb(
        part_status {
          key: "part_2"
          value { timestamp_ns: 12345 }
        }
        part_status {
          key: "part_1"
          value { timestamp_ns: 12345 }
        }
        sessions {
          key: 1
          value {
            part_group { parts: "part_1" parts: "part_2" }
            action_ids: 123
          }
        }
      )pb");
  RtclControllerFakeSession fake_session(
      SessionWill().OnAction(action_id,
                             ActionWill().SetRobotStatus(expected_status)),
      part_configs_by_name(), RtclControllerFakeSession::ValidationMode::kNice,
      operational_state_, update_status_.AsStdFunction(),
      get_status_.AsStdFunction(), {});
  EXPECT_OK(fake_session.AddActionsAndReactions(ActionsAndReactions{
      .action_descriptions = {MakeValidActionDescription(action_id)}}));
  EXPECT_OK(
      fake_session.StartActions({action_id}, /*stop_active_actions=*/true));
  EXPECT_CALL(update_status_, Call(EqualsProto(expected_status)));
  EXPECT_THAT(fake_session.PollReactions(), IsOkAndHolds(Eq(std::nullopt)));
}

TEST_F(RtclControllerFakeSessionTest,
       EmptyExpectationDoesNotOverwriteRobotStatus) {
  ActionInstanceId action_id(123);
  ActionInstanceId action_id_2(456);
  ReactionId reaction_id(123);
  const intrinsic_proto::icon::v1::GetStatusResponse expected_status =
      ParseTextProtoOrDie(R"pb(
        part_status {
          key: "part_2"
          value { timestamp_ns: 12345 }
        }
        part_status {
          key: "part_1"
          value { timestamp_ns: 12345 }
        }
        sessions {
          key: 1
          value {
            part_group { parts: "part_1" parts: "part_2" }
            action_ids: 123
          }
        }
      )pb");
  RtclControllerFakeSession fake_session(
      SessionWill()
          .OnAction(action_id,
                    ActionWill()
                        .ReportStateVariables({{kStateVariableName, true}})
                        .SetRobotStatus(expected_status))
          .OnAction(action_id_2, ActionWill()),
      part_configs_by_name(), RtclControllerFakeSession::ValidationMode::kNice,
      operational_state_, update_status_.AsStdFunction(),
      get_status_.AsStdFunction(), {});
  EXPECT_OK(fake_session.AddActionsAndReactions(ActionsAndReactions{
      .action_descriptions = {MakeValidActionDescription(action_id),
                              MakeValidActionDescription(action_id_2)},
      .reaction_descriptions = {
          ReactionDescriptor::ToProto(
              ReactionDescriptor(IsTrue(kStateVariableName))
                  .WithRealtimeActionOnCondition(action_id_2),
              /*reaction_id=*/reaction_id, /*action_id=*/action_id),
      }}));
  EXPECT_OK(
      fake_session.StartActions({action_id}, /*stop_active_actions=*/true));
  EXPECT_CALL(update_status_, Call(EqualsProto(expected_status)));
  EXPECT_THAT(fake_session.PollReactions(), IsOkAndHolds(Ne(std::nullopt)));
  // No further updates, the second action does not have a RobotStatus.
  EXPECT_CALL(update_status_, Call).Times(0);
  EXPECT_THAT(fake_session.PollReactions(), IsOkAndHolds(Eq(std::nullopt)));
}

TEST_F(RtclControllerFakeSessionTest, CallsGetStatusWhenExecutingCycle) {
  ActionInstanceId action_id(123);
  ActionInstanceId action_id_2(456);
  const intrinsic_proto::icon::v1::GetStatusResponse expected_status =
      ParseTextProtoOrDie(R"pb(
        part_status {
          key: "part_2"
          value { timestamp_ns: 12345 }
        }
        part_status {
          key: "part_1"
          value { timestamp_ns: 12345 }
        }
        sessions {
          key: 1
          value {
            part_group { parts: "part_1" parts: "part_2" }
            action_ids: 123
          }
        }
      )pb");
  RtclControllerFakeSession fake_session(
      SessionWill(), part_configs_by_name(),
      RtclControllerFakeSession::ValidationMode::kNice, operational_state_,
      update_status_.AsStdFunction(), get_status_.AsStdFunction());
  EXPECT_OK(fake_session.AddActionsAndReactions(ActionsAndReactions{
      .action_descriptions = {MakeValidActionDescription(action_id),
                              MakeValidActionDescription(action_id_2)},
      .reaction_descriptions = {}}));

  // Without an action there should be a call to get_status_ since there can be
  // free-standing reactions.
  EXPECT_CALL(get_status_, Call()).WillOnce(Return(expected_status));
  EXPECT_THAT(fake_session.PollReactions(), IsOkAndHolds(Eq(std::nullopt)));
  // With an action there should be a call to get_status_ for evaluating all
  // reactions that are bound to actions.
  EXPECT_OK(
      fake_session.StartActions({action_id}, /*stop_active_actions=*/true));
  EXPECT_CALL(get_status_, Call()).WillOnce(Return(expected_status));
  EXPECT_THAT(fake_session.PollReactions(), IsOkAndHolds(Eq(std::nullopt)));
}

TEST_F(RtclControllerFakeSessionTest, SetRobotStatus) {
  ActionInstanceId action_id(123);
  const intrinsic_proto::icon::v1::GetStatusResponse expected_status =
      ParseTextProtoOrDie(R"pb(
        part_status {
          key: "part_2"
          value { timestamp_ns: 12345 }
        }
        part_status {
          key: "part_1"
          value { timestamp_ns: 12345 }
        }
        sessions {
          key: 1
          value {
            part_group { parts: "part_1" parts: "part_2" }
            action_ids: 123
          }
        }
      )pb");
  RtclControllerFakeSession fake_session(
      SessionWill().SetRobotStatus(expected_status), part_configs_by_name(),
      RtclControllerFakeSession::ValidationMode::kNice, operational_state_,
      update_status_.AsStdFunction(), get_status_.AsStdFunction());
  EXPECT_OK(fake_session.AddActionsAndReactions(ActionsAndReactions{
      .action_descriptions = {MakeValidActionDescription(action_id)},
      .reaction_descriptions = {}}));

  EXPECT_CALL(update_status_, Call(EqualsProto(expected_status))).Times(1);
  EXPECT_THAT(fake_session.PollReactions(), IsOkAndHolds(Eq(std::nullopt)));
}

TEST_F(RtclControllerFakeSessionTest, SetRobotStatusAfterActionBehavior) {
  ActionInstanceId action_id(123);
  ReactionId reaction_id(789);
  const intrinsic_proto::icon::v1::GetStatusResponse expected_status =
      ParseTextProtoOrDie(R"pb(
        part_status {
          key: "part_2"
          value { timestamp_ns: 12345 }
        }
        part_status {
          key: "part_1"
          value { timestamp_ns: 12345 }
        }
        sessions {
          key: 1
          value {
            part_group { parts: "part_1" parts: "part_2" }
            action_ids: 123
          }
        }
      )pb");
  RtclControllerFakeSession fake_session(
      SessionWill()
          .OnAction(action_id, ActionWill().ReportStateVariables(
                                   {{kStateVariableName, true}}))
          .SetRobotStatus(expected_status),
      part_configs_by_name(),
      RtclControllerFakeSession::ValidationMode::kStrict, operational_state_,
      update_status_.AsStdFunction(), get_status_.AsStdFunction(), {});
  EXPECT_OK(fake_session.AddActionsAndReactions(ActionsAndReactions{
      .action_descriptions = {MakeValidActionDescription(action_id)},
      .reaction_descriptions = {ReactionDescriptor::ToProto(
          ReactionDescriptor(IsTrue(kStateVariableName)),
          /*reaction_id=*/reaction_id, /*action_id=*/action_id)}}));
  EXPECT_OK(
      fake_session.StartActions({action_id}, /*stop_active_actions=*/true));

  EXPECT_CALL(update_status_, Call(EqualsProto(expected_status))).Times(1);
  EXPECT_OK(fake_session.PollReactions().status());
  EXPECT_OK(fake_session.PollReactions().status());
}

TEST_F(RtclControllerFakeSessionTest,
       StrictValidationModeActionStateVariableMapExpectationViolation) {
  ActionInstanceId action_id(123);
  ReactionId reaction_id(789);
  RtclControllerFakeSession fake_session(
      SessionWill().OnAction(
          action_id,
          ActionWill().ReportStateVariablesInOrder(
              {{{kStateVariableName, true}}, {{kStateVariableName, false}}})),
      part_configs_by_name(),
      RtclControllerFakeSession::ValidationMode::kStrict, operational_state_,
      update_status_.AsStdFunction(), get_status_.AsStdFunction(), {});
  EXPECT_OK(fake_session.AddActionsAndReactions(ActionsAndReactions{
      .action_descriptions = {MakeValidActionDescription(action_id)},
      .reaction_descriptions = {ReactionDescriptor::ToProto(
          ReactionDescriptor(IsTrue(kStateVariableName))
              .WithRealtimeActionOnCondition(action_id),
          /*reaction_id=*/reaction_id, /*action_id=*/action_id)}}));
  EXPECT_OK(
      fake_session.StartActions({action_id}, /*stop_active_actions=*/true));
  EXPECT_THAT(fake_session.PollReactions(),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       HasSubstr("not all Action StateVariableMaps")));
}

TEST_F(RtclControllerFakeSessionTest,
       StrictValidationModeActionStreamingInputExpectationViolation) {
  constexpr double kExpectedValue = 1.23;
  auto matcher = [](const google::protobuf::DoubleValue& param) {
    if (param.value() != kExpectedValue) {
      return absl::InternalError("oh no, the streaming input is wrong.");
    }
    return absl::OkStatus();
  };
  ActionInstanceId id(1);
  RtclControllerFakeSession fake_session(
      SessionWill().OnAction(
          id, ActionWill().ExpectStreamingInput<google::protobuf::DoubleValue>(
                  matcher)),
      part_configs_by_name(),
      RtclControllerFakeSession::ValidationMode::kStrict, operational_state_,
      update_status_.AsStdFunction(), get_status_.AsStdFunction(), {});
  // Set the parameter value to true, so that the ExpectParameter matcher above
  // returns an error (even in nice mode!).
  EXPECT_OK(fake_session.AddActionsAndReactions(ActionsAndReactions{
      .action_descriptions = {MakeValidActionDescription(
          ActionInstanceId(1), /*param_bool_value=*/false)}}));
  EXPECT_OK(fake_session.StartActions({id}, /*stop_active_actions=*/true));

  google::protobuf::DoubleValue value;
  value.set_value(kExpectedValue);
  google::protobuf::Any any;
  ASSERT_TRUE(any.PackFrom(value));

  EXPECT_THAT(
      fake_session.PollReactions(),
      StatusIs(absl::StatusCode::kFailedPrecondition,
               HasSubstr("not all expected streaming inputs were detected")));
}

TEST_F(RtclControllerFakeSessionTest,
       StrictValidationModeActionExpectationViolation) {
  ActionInstanceId action_id(123);
  ActionInstanceId action_id2(456);
  ReactionId reaction_id(789);

  auto fake_session = std::make_unique<RtclControllerFakeSession>(
      SessionWill()
          .OnAction(action_id, ActionWill().ReportStateVariables(
                                   {{kStateVariableName, true}}))
          .OnAction(action_id2, ActionWill())
          .OnAction(action_id2, ActionWill()),
      part_configs_by_name(),
      RtclControllerFakeSession::ValidationMode::kStrict, operational_state_,
      update_status_.AsStdFunction(), get_status_.AsStdFunction());
  EXPECT_OK(fake_session->AddActionsAndReactions(ActionsAndReactions{
      .action_descriptions = {MakeValidActionDescription(action_id),
                              MakeValidActionDescription(action_id2)},
      .reaction_descriptions = {ReactionDescriptor::ToProto(
          ReactionDescriptor(IsTrue(kStateVariableName))
              .WithRealtimeActionOnCondition(action_id2),
          /*reaction_id=*/reaction_id, /*action_id=*/action_id)}}));
  EXPECT_OK(
      fake_session->StartActions({action_id}, /*stop_active_actions=*/true));

  EXPECT_OK(fake_session->PollReactions().status());
  EXPECT_NONFATAL_FAILURE({ fake_session.reset(); }, "unmet");
}

TEST_F(RtclControllerFakeSessionTest,
       StrictValidationModeWithWrongActionExpectationsFails) {
  ActionInstanceId action_id(123);
  ActionInstanceId action_id2(456);
  ReactionId reaction_id(789);
  auto fake_session = std::make_unique<RtclControllerFakeSession>(
      SessionWill()
          .OnAction(action_id, ActionWill().ReportStateVariables(
                                   {{kStateVariableName, true}}))
          .OnAction(action_id, ActionWill())
          .OnAction(action_id2, ActionWill()),
      part_configs_by_name(),
      RtclControllerFakeSession::ValidationMode::kStrict, operational_state_,
      update_status_.AsStdFunction(), get_status_.AsStdFunction());
  EXPECT_OK(fake_session->AddActionsAndReactions(ActionsAndReactions{
      .action_descriptions = {MakeValidActionDescription(action_id),
                              MakeValidActionDescription(action_id2)},
      .reaction_descriptions = {ReactionDescriptor::ToProto(
          ReactionDescriptor(IsTrue(kStateVariableName))
              .WithRealtimeActionOnCondition(action_id2),
          /*reaction_id=*/reaction_id, /*action_id=*/action_id)}}));
  EXPECT_OK(
      fake_session->StartActions({action_id}, /*stop_active_actions=*/true));
  EXPECT_THAT(fake_session->PollReactions(),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       HasSubstr("ChannelFake expected action 123 to become "
                                 "active, but instead action 456 became "
                                 "active")));
  EXPECT_NONFATAL_FAILURE({ fake_session.reset(); }, "unmet");
}

TEST_F(RtclControllerFakeSessionTest, FactoryRegistersPlannedTrajectory) {
  ActionInstanceId expected_id(1);
  RtclControllerFakeSession fake_session(
      SessionWill(), part_configs_by_name(),
      RtclControllerFakeSession::ValidationMode::kNice, operational_state_,
      update_status_.AsStdFunction(), get_status_.AsStdFunction(), {});
  EXPECT_THAT(fake_session.GetPlannedTrajectory(expected_id),
              StatusIs(absl::StatusCode::kNotFound));
  EXPECT_OK(fake_session.AddActionsAndReactions(ActionsAndReactions{
      .action_descriptions = {MakeValidActionDescription(expected_id)}}));
  ASSERT_OK_AND_ASSIGN(
      ::intrinsic_proto::icon::JointTrajectoryPVA trajectory_proto,
      fake_session.GetPlannedTrajectory(expected_id));
  // We don't care too much about the exact content here, just check the
  // duration.
  ASSERT_OK_AND_ASSIGN(JointTrajectoryPVA trajectory,
                       ::intrinsic::FromProto(trajectory_proto));
  EXPECT_EQ(trajectory.Duration(), kTrajectoryDuration);
}

}  // namespace
}  // namespace intrinsic::icon
