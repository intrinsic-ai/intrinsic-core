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

#include "intrinsic/icon/cc_client/testing/channel_fake.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <any>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/any.pb.h"
#include "internal/testing.h"
#include "intrinsic/icon/actions/joint_jogging_info.h"
#include "intrinsic/icon/actions/point_to_point_move_info.h"
#include "intrinsic/icon/cc_client/condition.h"
#include "intrinsic/icon/common/id_types.h"
#include "intrinsic/icon/proto/joint_space.pb.h"
#include "intrinsic/icon/proto/part_status.pb.h"
#include "intrinsic/icon/proto/v1/service.pb.h"
#include "intrinsic/icon/proto/v1/types.pb.h"
#include "intrinsic/util/proto/parse_text_proto.h"
#include "protobuf-matchers/protocol-buffer-matchers.h"

namespace intrinsic {
namespace icon {
namespace {

using ::absl_testing::StatusIs;
using ::intrinsic::ParseTextProtoOrDie;
using ::protobuf_matchers::EqualsProto;
using ::testing::ElementsAre;
using ::testing::ElementsAreArray;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::IsNull;
using ::testing::MockFunction;
using ::testing::Optional;
using ::testing::Return;

TEST(ActionWill, ParameterMatcherDefaultsToNullpt) {
  EXPECT_THAT(ActionWill().GetParameterMatcher(), IsNull());
}

TEST(ActionWill, ParameterMatcher) {
  MockFunction<absl::Status(const google::protobuf::Any& parameter)>
      mock_matcher;
  EXPECT_CALL(mock_matcher, Call)
      .WillOnce(Return(absl::InternalError("oh no")));

  EXPECT_THAT(ActionWill()
                  .ExpectParameter(mock_matcher.AsStdFunction())
                  .GetParameterMatcher()(google::protobuf::Any()),
              StatusIs(absl::StatusCode::kInternal, HasSubstr("oh no")));
}

TEST(ActionWill, TemplatedParameterMatcher) {
  MockFunction<absl::Status(const PointToPointMoveInfo::FixedParams& parameter)>
      mock_matcher;
  PointToPointMoveInfo::FixedParams params;
  params.mutable_goal_position()->add_joints(123);
  EXPECT_CALL(mock_matcher, Call(EqualsProto(params)))
      .WillOnce(Return(absl::OkStatus()));

  google::protobuf::Any any_params;
  ASSERT_TRUE(any_params.PackFrom(params));
  EXPECT_OK(ActionWill()
                .ExpectParameter(mock_matcher.AsStdFunction())
                .GetParameterMatcher()(any_params));
}

TEST(ActionWill, TemplatedParameterMatcherFailsWithWrongType) {
  MockFunction<absl::Status(const PointToPointMoveInfo::FixedParams& parameter)>
      mock_matcher;
  // The matcher itself won't be called if the Any proto unpacking fails.
  EXPECT_CALL(mock_matcher, Call).Times(0);

  intrinsic_proto::icon::JointVec params;
  params.add_joints(231);
  google::protobuf::Any any_params;
  ASSERT_TRUE(any_params.PackFrom(params));
  EXPECT_THAT(
      ActionWill()
          .ExpectParameter(mock_matcher.AsStdFunction())
          .GetParameterMatcher()(any_params),
      StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("unpack")));
}

TEST(ActionWill, StreamingInputParameterMatcherDefaultsToEmpty) {
  EXPECT_THAT(ActionWill().GetOrderedStreamingInputParameterMatchers(),
              IsEmpty());
}

TEST(ActionWill, StreamingInputParameterMatcher) {
  MockFunction<absl::Status(const google::protobuf::Any& parameter)>
      mock_matcher1, mock_matcher2;
  EXPECT_CALL(mock_matcher1, Call).WillOnce(Return(absl::InternalError("hi")));
  EXPECT_CALL(mock_matcher2, Call)
      .WillOnce(Return(absl::InvalidArgumentError("bye")));

  ActionWill action_will =
      ActionWill()
          .ExpectStreamingInput(mock_matcher1.AsStdFunction())
          .ExpectStreamingInput(mock_matcher2.AsStdFunction());

  EXPECT_THAT(action_will.GetOrderedStreamingInputParameterMatchers()[0](
                  google::protobuf::Any()),
              StatusIs(absl::StatusCode::kInternal, HasSubstr("hi")));
  EXPECT_THAT(action_will.GetOrderedStreamingInputParameterMatchers()[1](
                  google::protobuf::Any()),
              StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("bye")));
}

TEST(ActionWill, TemplatedStreamingInputParameterMatcher) {
  MockFunction<absl::Status(
      const JointJoggingInfo::StreamingParams& streaming_parameter)>
      mock_matcher;
  JointJoggingInfo::StreamingParams params;
  params.mutable_goal_velocity()->add_joints(123);
  EXPECT_CALL(mock_matcher, Call(EqualsProto(params)))
      .WillOnce(Return(absl::OkStatus()));

  google::protobuf::Any any_params;
  ASSERT_TRUE(any_params.PackFrom(params));
  EXPECT_OK(ActionWill()
                .ExpectStreamingInput(mock_matcher.AsStdFunction())
                .GetOrderedStreamingInputParameterMatchers()[0](any_params));
}

TEST(ActionWill, StreamingInputTemplatedParameterMatcherFailsWithWrongType) {
  MockFunction<absl::Status(
      const JointJoggingInfo::StreamingParams& streaming_parameter)>
      mock_matcher;
  // The matcher itself won't be called if the Any proto unpacking fails.
  EXPECT_CALL(mock_matcher, Call).Times(0);

  // Pack with the wrong type.
  PointToPointMoveInfo::FixedParams params;
  params.mutable_goal_position()->add_joints(123);
  google::protobuf::Any any_params;
  ASSERT_TRUE(any_params.PackFrom(params));

  EXPECT_THAT(
      ActionWill()
          .ExpectStreamingInput(mock_matcher.AsStdFunction())
          .GetOrderedStreamingInputParameterMatchers()[0](any_params),
      StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("unpack")));
}

TEST(ActionWill, StreamingOutputDefaultsToEmpty) {
  EXPECT_THAT(ActionWill().OrderedStreamingOutputs(), IsEmpty());
}

TEST(ActionWill, StreamingOutputsStoredInOrder) {
  struct TestStreamingOutput {
    double value;
    int index;
  };

  TestStreamingOutput kExpectedOutput1{.value = 123, .index = 0};
  double kExpectedOutput2 = 45.6;

  std::vector<std::any> streaming_outputs{
      std::any(kExpectedOutput1),
      std::any(kExpectedOutput2),
  };
  ActionWill action_expectation;
  action_expectation.PublishStreamingOutput(streaming_outputs);
  auto outputs = action_expectation.OrderedStreamingOutputs();
  ASSERT_EQ(outputs.size(), 2);
  const TestStreamingOutput* output1 =
      std::any_cast<TestStreamingOutput>(&outputs[0]);
  ASSERT_NE(output1, nullptr);
  EXPECT_EQ(output1->index, kExpectedOutput1.index);
  EXPECT_EQ(output1->value, kExpectedOutput1.value);
  const double* output2 = std::any_cast<double>(&outputs[1]);
  EXPECT_EQ(*output2, kExpectedOutput2);
}

TEST(ActionWill, ReportStateVariablesInOrder) {
  std::vector<absl::flat_hash_map<std::string, StateVariableValue>>
      state_variable_maps{
          {{"my_var", true}},
          {{"my_var", false}},
      };
  ActionWill action_expectation;
  action_expectation.ReportStateVariablesInOrder(state_variable_maps);

  EXPECT_THAT(action_expectation.InOrderStateVariableMaps(),
              ElementsAreArray(state_variable_maps));
}

TEST(ActionWill, ReportStateVariablesBuildsUpList) {
  std::vector<absl::flat_hash_map<std::string, StateVariableValue>>
      state_variable_maps{
          {{"my_var", true}},
          {{"my_var", false}},
      };
  ActionWill action_expectation;
  action_expectation.ReportStateVariables(state_variable_maps[0]);
  EXPECT_THAT(action_expectation.InOrderStateVariableMaps(),
              ElementsAre(state_variable_maps[0]));

  action_expectation.ReportStateVariables(state_variable_maps[1]);
  EXPECT_THAT(action_expectation.InOrderStateVariableMaps(),
              ElementsAreArray(state_variable_maps));
}

TEST(ActionWill, ReportStateVariablesInOrderOverwritesPrevious) {
  ActionWill action_expectation;
  absl::flat_hash_map<std::string, StateVariableValue> variable_map{
      {"my_var", true}};
  action_expectation.ReportStateVariables(variable_map);
  EXPECT_THAT(action_expectation.InOrderStateVariableMaps(),
              ElementsAre(variable_map));

  action_expectation.ReportStateVariablesInOrder({});
  EXPECT_THAT(action_expectation.InOrderStateVariableMaps(), IsEmpty());
}

TEST(ActionWill, RobotStatusDefaultsToNullopt) {
  EXPECT_EQ(ActionWill().GetRobotStatus(), std::nullopt);
}

TEST(ActionWill, RobotStatusSavesProto) {
  ActionWill action_expectation;
  intrinsic_proto::icon::v1::GetStatusResponse expected = ParseTextProtoOrDie(
      R"pb(sessions: {
             key: 42,
             value: { action_ids: 123 }
           })pb");
  action_expectation.SetRobotStatus(expected);
  EXPECT_THAT(action_expectation.GetRobotStatus(),
              Optional(EqualsProto(expected)));
}

TEST(ActionWill, OperationalStatusDefaultsToNullopt) {
  EXPECT_EQ(ActionWill().GetOperationalStatus(), std::nullopt);
}

TEST(ActionWill, OperationalStatusSavesProto) {
  ActionWill action_expectation;
  intrinsic_proto::icon::v1::GetOperationalStatusResponse expected =
      ParseTextProtoOrDie(R"pb(operational_status: { state: DISABLED })pb");
  action_expectation.SetOperationalStatus(expected);
  EXPECT_THAT(action_expectation.GetOperationalStatus(),
              Optional(EqualsProto(expected)));
}

TEST(ActionWill, OnStartDefaultsToNullptr) {
  EXPECT_EQ(ActionWill().GetOnStart(), nullptr);
}

TEST(ActionWill, OnStartSavesCallback) {
  bool called = false;
  ActionWill action_expectation;
  action_expectation.OnStart([&called]() { called = true; });
  ASSERT_NE(action_expectation.GetOnStart(), nullptr);
  action_expectation.GetOnStart()();
  EXPECT_TRUE(called);
}

TEST(SessionWill, OnActionStoresActionWill) {
  ActionInstanceId id(1);
  SessionWill session_will;
  ActionWill action_will;
  action_will.ReportStateVariables({{"my_var", true}});
  session_will.OnAction(id, action_will);
  ASSERT_EQ(session_will.InOrderActionBehaviors().size(), 1);
  EXPECT_THAT(session_will.InOrderActionBehaviors()[0].action_id, Optional(id));
  EXPECT_THAT(session_will.InOrderActionBehaviors()[0]
                  .action_behaviors.InOrderStateVariableMaps(),
              ElementsAre(absl::flat_hash_map<std::string, StateVariableValue>{
                  {"my_var", true}}));
  EXPECT_FALSE(
      session_will.InOrderActionBehaviors()[0].is_already_running_action);
}

TEST(SessionWill, ForPreRunningActionStoresActionWill) {
  ActionInstanceId id(1);
  SessionWill session_will;
  ActionWill action_will;
  action_will.ReportStateVariables({{"my_var", true}});
  session_will.ForAlreadyRunningAction(id, action_will);
  ASSERT_EQ(session_will.InOrderActionBehaviors().size(), 1);
  EXPECT_THAT(session_will.InOrderActionBehaviors()[0].action_id, Optional(id));
  EXPECT_THAT(session_will.InOrderActionBehaviors()[0]
                  .action_behaviors.InOrderStateVariableMaps(),
              ElementsAre(absl::flat_hash_map<std::string, StateVariableValue>{
                  {"my_var", true}}));
  EXPECT_TRUE(
      session_will.InOrderActionBehaviors()[0].is_already_running_action);
}

TEST(SessionWill, SetRobotStatusStoresRobotStatus) {
  SessionWill session_will;
  intrinsic_proto::icon::v1::GetStatusResponse expected = ParseTextProtoOrDie(
      R"pb(sessions: {
             key: 42,
             value: { action_ids: 123 }
           })pb");
  session_will.SetRobotStatus(expected);
  ASSERT_EQ(session_will.InOrderActionBehaviors().size(), 1);
  EXPECT_THAT(session_will.InOrderActionBehaviors()[0]
                  .action_behaviors.GetRobotStatus(),
              Optional(EqualsProto(expected)));
  EXPECT_EQ(session_will.InOrderActionBehaviors()[0].action_id, std::nullopt);
}

TEST(SessionWill, StoresMultipleActionWillInOrder) {
  ActionInstanceId id1(1);
  ActionInstanceId id2(2);
  SessionWill session_will;
  ActionWill action_will1;
  action_will1.ReportStateVariables({{"my_var", true}});
  ActionWill action_will2;
  ActionWill action_will3;
  action_will3.ReportStateVariables({{"my_var", false}});

  session_will.OnAction(id1, action_will1);
  session_will.OnAction(id2, action_will2);
  session_will.ForAlreadyRunningAction(id1, action_will3);

  ASSERT_EQ(session_will.InOrderActionBehaviors().size(), 3);
  EXPECT_THAT(session_will.InOrderActionBehaviors()[0].action_id,
              Optional(id1));
  EXPECT_THAT(session_will.InOrderActionBehaviors()[0]
                  .action_behaviors.InOrderStateVariableMaps(),
              ElementsAre(absl::flat_hash_map<std::string, StateVariableValue>{
                  {"my_var", true}}));
  EXPECT_THAT(session_will.InOrderActionBehaviors()[1].action_id,
              Optional(id2));

  EXPECT_THAT(session_will.InOrderActionBehaviors()[2].action_id,
              Optional(id1));
  EXPECT_THAT(session_will.InOrderActionBehaviors()[2]
                  .action_behaviors.InOrderStateVariableMaps(),
              ElementsAre(absl::flat_hash_map<std::string, StateVariableValue>{
                  {"my_var", false}}));
  EXPECT_TRUE(
      session_will.InOrderActionBehaviors()[2].is_already_running_action);
}

}  // namespace
}  // namespace icon
}  // namespace intrinsic
