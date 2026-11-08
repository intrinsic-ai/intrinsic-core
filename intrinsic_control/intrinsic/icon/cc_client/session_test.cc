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

#include "intrinsic/icon/cc_client/session.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "google/protobuf/util/message_differencer.h"
#include "google/protobuf/wrappers.pb.h"
#include "grpcpp/impl/call_op_set.h"
#include "grpcpp/support/status.h"
#include "grpcpp/support/sync_stream.h"
#include "internal/testing.h"
#include "intrinsic/icon/actions/point_to_point_move_info.h"
#include "intrinsic/icon/cc_client/client.h"
#include "intrinsic/icon/cc_client/client_utils.h"
#include "intrinsic/icon/cc_client/condition.h"
#include "intrinsic/icon/cc_client/testing/channel_fake.h"
#include "intrinsic/icon/cc_client/testing/fake_action.pb.h"
#include "intrinsic/icon/cc_client/testing/rtcl_controller_channel_fake.h"
#include "intrinsic/icon/common/builtins.h"
#include "intrinsic/icon/common/id_types.h"
#include "intrinsic/icon/proto/concatenate_trajectory_protos.h"
#include "intrinsic/icon/proto/joint_space.pb.h"
#include "intrinsic/icon/proto/streaming_output.pb.h"
#include "intrinsic/icon/proto/v1/service.pb.h"
#include "intrinsic/icon/proto/v1/service_mock.grpc.pb.h"
#include "intrinsic/icon/proto/v1/types.pb.h"
#include "intrinsic/icon/server/fake_robot_connection.h"
#include "intrinsic/icon/server/testing/fake_operational_state.h"
#include "intrinsic/icon/server/testing/in_process_server.h"
#include "intrinsic/icon/server/testing/test_with_status_spec.h"
#include "intrinsic/logging/proto/context.pb.h"
#include "intrinsic/util/grpc/channel_interface.h"
#include "intrinsic/util/proto/parse_text_proto.h"
#include "intrinsic/util/proto_time.h"
#include "intrinsic/util/status/get_extended_status.h"
#include "intrinsic/util/status/status_conversion_grpc.h"
#include "intrinsic/util/status/status_conversion_rpc.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/thread/thread.h"
#include "protobuf-matchers/protocol-buffer-matchers.h"

// TODO(b/150959510): Resolve grpc dependencies so that we can use
// "third_party/grpc/include/grpcpp/test/mock_stream.h" instead of this.
namespace {
template <class W, class R>
class MockClientReaderWriter
    : public ::grpc::ClientReaderWriterInterface<W, R> {
 public:
  MockClientReaderWriter() = default;

  // ClientStreamingInterface
  MOCK_METHOD(::grpc::Status, Finish, ());

  // ReaderInterface
  MOCK_METHOD(bool, NextMessageSize, (uint32_t*));
  MOCK_METHOD(bool, Read, (R*));

  // WriterInterface
  MOCK_METHOD(bool, Write, (const W&, ::grpc::WriteOptions));

  // ClientReaderWriterInterface
  MOCK_METHOD(void, WaitForInitialMetadata, ());
  MOCK_METHOD(bool, WritesDone, ());
};

template <class R>
class MockClientReader : public ::grpc::ClientReaderInterface<R> {
 public:
  MockClientReader() = default;

  // ClientStreamingInterface
  MOCK_METHOD(::grpc::Status, Finish, ());

  // ReaderInterface
  MOCK_METHOD(bool, NextMessageSize, (uint32_t*));
  MOCK_METHOD(bool, Read, (R*));

  /// ClientReaderInterface
  MOCK_METHOD(void, WaitForInitialMetadata, ());
};

}  // namespace

namespace intrinsic {
namespace icon {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;
using ::intrinsic::ParseTextProtoOrDie;
using ::protobuf_matchers::EqualsProto;
using ::testing::_;
using ::testing::AllOf;
using ::testing::DoAll;
using ::testing::Gt;
using ::testing::HasSubstr;
using ::testing::InSequence;
using ::testing::Invoke;
using ::testing::Property;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::SetArgPointee;
using ::testing::SizeIs;
using ::testing::Truly;
using ::testing::WithArg;

constexpr char kPartName1[] = "intrinsic.icon.RobotArm";
constexpr char kPartName2[] = "intrinsic.icon.RobotGripper";
constexpr char kIconServerName[] = "icon_server";
constexpr double kControlFrequencyHz = 1000;

intrinsic_proto::icon::v1::ServerConfig ServerConfig() {
  intrinsic_proto::icon::v1::ServerConfig server_config;
  server_config.set_name(kIconServerName);
  server_config.set_frequency_hz(kControlFrequencyHz);
  return server_config;
}

PointToPointMoveInfo::FixedParams GetValidPointToPointMoveParams() {
  return CreatePointToPointMoveFixedParams({0, 0, 0, 0, 0, 0},
                                           {0, 0, 0, 0, 0, 0});
}

class SessionFakeStubTest : public ::testing::Test {
 public:
  SessionFakeStubTest()
      : operational_state_(std::make_shared<FakeOperationalState>()),
        robot_connection_(ServerConfig(),
                          /*part_names=*/{kPartName1, kPartName2},
                          operational_state_),
        server_(robot_connection_),
        client_(server_.MakeStub()) {}

  void SetUp() override { ASSERT_OK(client_.Enable()); }

 protected:
  std::shared_ptr<FakeOperationalState> operational_state_;
  FakeRobotConnection robot_connection_;
  InProcessApplicationLayerServer server_;
  Client client_;
};

TEST_F(SessionFakeStubTest, StartTimesOutIfDisabled) {
  ASSERT_OK(operational_state_->SetDisabled());
  EXPECT_THAT(
      Session::Start(/*stub=*/server_.MakeStub(), /*parts=*/{kPartName1},
                     /*client_context_factory=*/DefaultClientContextFactory,
                     /*context=*/{},
                     /*deadline=*/absl::Now() + absl::Milliseconds(100)),
      StatusIs(absl::StatusCode::kDeadlineExceeded));
}

TEST_F(SessionFakeStubTest, Start) {
  EXPECT_OK(Session::Start(server_.MakeStub(), {kPartName1}));
}

TEST_F(SessionFakeStubTest, MakeTwoSessionsForTheSamePart) {
  // ASSERT_OK_AND_ASSIGN() instead of EXPECT_OK() so that we can ensure that
  // the Session stays alive for the duration of the test case.
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Session> session,
                       Session::Start(server_.MakeStub(), {kPartName1}));
  EXPECT_THAT(Session::Start(server_.MakeStub(), {kPartName1}),
              StatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST_F(SessionFakeStubTest, MakeTwoSessionsForDifferentParts) {
  // ASSERT_OK_AND_ASSIGN() instead of EXPECT_OK() so that we can ensure that
  // the Session stays alive for the duration of the test case.
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Session> session,
                       Session::Start(server_.MakeStub(), {kPartName1}));
  EXPECT_OK(Session::Start(server_.MakeStub(), {kPartName2}));
}

TEST_F(SessionFakeStubTest, CallEndsAtDestruction) {
  // Creating a second session with the same part allocation will fail if the
  // previous session doesn't end first. Therefore, if the call didn't end at
  // destruction, the second call to Start() would fail.
  EXPECT_OK(Session::Start(server_.MakeStub(), {kPartName1}));
  EXPECT_OK(Session::Start(server_.MakeStub(), {kPartName1}));
}

TEST_F(SessionFakeStubTest, EndSessionSuccess) {
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Session> session,
                       Session::Start(server_.MakeStub(), {kPartName1}));
  EXPECT_OK(session->End());
}

TEST_F(SessionFakeStubTest, EndSessionFailsOnSecondCall) {
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Session> session,
                       Session::Start(server_.MakeStub(), {kPartName1}));
  EXPECT_OK(session->End());
  EXPECT_THAT(session->End(), StatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST_F(SessionFakeStubTest, RunWatcherLoopTimesOut) {
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Session> session,
                       Session::Start(server_.MakeStub(), {kPartName1}));
  absl::Time start = absl::Now();
  EXPECT_THAT(session->RunWatcherLoop(start + absl::Milliseconds(100)),
              StatusIs(absl::StatusCode::kDeadlineExceeded));
  EXPECT_GE(absl::Now() - start, absl::Milliseconds(100));
}

ACTION_P(write_initial_response, session_id, status) {
  intrinsic_proto::icon::v1::OpenSessionResponse initial_response_message;
  initial_response_message.mutable_initial_session_data()->set_session_id(
      session_id);
  *initial_response_message.mutable_status() = SaveStatusAsRpcStatus(status);
  *arg0 = initial_response_message;
}

ACTION_P(write_action_response, status) {
  intrinsic_proto::icon::v1::OpenSessionResponse response_message;
  response_message.mutable_action_response();
  *response_message.mutable_status() = SaveStatusAsRpcStatus(status);
  *arg0 = response_message;
}

void MockStubSetup(
    std::unique_ptr<
        MockClientReaderWriter<intrinsic_proto::icon::v1::OpenSessionRequest,
                               intrinsic_proto::icon::v1::OpenSessionResponse>>
        action_stream_mock,
    std::unique_ptr<
        MockClientReader<intrinsic_proto::icon::v1::WatchReactionsResponse>>
        watcher_stream_mock,
    intrinsic_proto::icon::v1::MockIconApiStub* stub) {
  EXPECT_CALL(*stub, OpenSessionRaw)
      .WillOnce(
          Return(action_stream_mock.release()));  // `stub` takes ownership
  EXPECT_CALL(*stub, WatchReactionsRaw)
      .WillOnce(
          Return(watcher_stream_mock.release()));  // `stub` takes ownership
}

void MockCallSetup(
    MockClientReaderWriter<intrinsic_proto::icon::v1::OpenSessionRequest,
                           intrinsic_proto::icon::v1::OpenSessionResponse>*
        action_stream_mock,
    const absl::Status& status) {
  EXPECT_CALL(*action_stream_mock, Write).WillOnce(Return(true));
  constexpr int64_t kSessionId = 1;
  EXPECT_CALL(*action_stream_mock, Read)
      .WillOnce(DoAll(WithArg<0>(write_initial_response(kSessionId, status)),
                      Return(true)));
}

void MockActionResponseWithStatus(
    MockClientReaderWriter<intrinsic_proto::icon::v1::OpenSessionRequest,
                           intrinsic_proto::icon::v1::OpenSessionResponse>*
        action_stream_mock,
    const absl::Status& status) {
  EXPECT_CALL(*action_stream_mock, Write).WillOnce(Return(true));
  EXPECT_CALL(*action_stream_mock, Read)
      .WillOnce(DoAll(WithArg<0>(write_action_response(status)), Return(true)));
}

void MockEndActionCall(
    MockClientReaderWriter<intrinsic_proto::icon::v1::OpenSessionRequest,
                           intrinsic_proto::icon::v1::OpenSessionResponse>*
        action_stream_mock,
    const absl::Status& action_call_status) {
  // Grpc check-fails if WritesDone is called multiple times, prevent bugs
  // like b/290158026.
  EXPECT_CALL(*action_stream_mock, WritesDone).Times(1).WillOnce(Return(true));
  EXPECT_CALL(*action_stream_mock, Read).WillOnce(Return(false));
  EXPECT_CALL(*action_stream_mock, Finish())
      .WillOnce(Return(ToGrpcStatus(action_call_status)));
}

std::pair<
    std::unique_ptr<
        MockClientReaderWriter<intrinsic_proto::icon::v1::OpenSessionRequest,
                               intrinsic_proto::icon::v1::OpenSessionResponse>>,
    std::unique_ptr<
        MockClientReader<intrinsic_proto::icon::v1::WatchReactionsResponse>>>
MakeStreamMockForActionSuccess(int num_actions = 1) {
  auto action_stream_mock = std::make_unique<
      MockClientReaderWriter<intrinsic_proto::icon::v1::OpenSessionRequest,
                             intrinsic_proto::icon::v1::OpenSessionResponse>>();

  auto watcher_stream_mock = std::make_unique<
      MockClientReader<intrinsic_proto::icon::v1::WatchReactionsResponse>>();
  {
    InSequence s;
    MockCallSetup(action_stream_mock.get(), absl::OkStatus());
    for (int action_ctr = 0; action_ctr < num_actions; ++action_ctr) {
      MockActionResponseWithStatus(action_stream_mock.get(), absl::OkStatus());
    }
    MockEndActionCall(action_stream_mock.get(), absl::OkStatus());
  }

  EXPECT_CALL(*watcher_stream_mock, Read)
      .WillOnce(Return(true))
      .WillRepeatedly(Return(false));

  return {std::move(action_stream_mock), std::move(watcher_stream_mock)};
}

std::pair<
    std::unique_ptr<
        MockClientReaderWriter<intrinsic_proto::icon::v1::OpenSessionRequest,
                               intrinsic_proto::icon::v1::OpenSessionResponse>>,
    std::unique_ptr<
        MockClientReader<intrinsic_proto::icon::v1::WatchReactionsResponse>>>
MakeStreamMockForActionSuccessSaveAddActionRequest(
    intrinsic_proto::icon::v1::OpenSessionRequest* request) {
  auto action_stream_mock = std::make_unique<
      MockClientReaderWriter<intrinsic_proto::icon::v1::OpenSessionRequest,
                             intrinsic_proto::icon::v1::OpenSessionResponse>>();

  auto watcher_stream_mock = std::make_unique<
      MockClientReader<intrinsic_proto::icon::v1::WatchReactionsResponse>>();
  {
    InSequence s;
    MockCallSetup(action_stream_mock.get(), absl::OkStatus());
    EXPECT_CALL(*action_stream_mock, Write)
        .WillOnce(DoAll(SaveArg<0>(request), Return(true)));
    EXPECT_CALL(*action_stream_mock, Read)
        .WillOnce(DoAll(WithArg<0>(write_action_response(absl::OkStatus())),
                        Return(true)));
    MockEndActionCall(action_stream_mock.get(), absl::OkStatus());
  }

  EXPECT_CALL(*watcher_stream_mock, Read)
      .WillOnce(Return(true))
      .WillRepeatedly(Return(false));

  return {std::move(action_stream_mock), std::move(watcher_stream_mock)};
}

TEST(SessionMockTest, GetLatestOutput) {
  auto [action_stream_mock, watcher_stream_mock] =
      MakeStreamMockForActionSuccess();

  auto stub_mock =
      std::make_unique<intrinsic_proto::icon::v1::MockIconApiStub>();
  MockStubSetup(std::move(action_stream_mock), std::move(watcher_stream_mock),
                stub_mock.get());

  intrinsic_proto::icon::v1::GetLatestStreamingOutputResponse response;
  intrinsic_proto::icon::StreamingOutput* expected_output =
      response.mutable_output();
  expected_output->set_timestamp_ns(3932);
  {
    google::protobuf::FloatValue payload;
    payload.set_value(1.234);
    ASSERT_TRUE(expected_output->mutable_payload()->PackFrom(payload));
  }
  constexpr ActionInstanceId kActionId(1);
  EXPECT_CALL(*stub_mock,
              GetLatestStreamingOutput(
                  _,
                  Property(&intrinsic_proto::icon::v1::
                               GetLatestStreamingOutputRequest::action_id,
                           kActionId.value()),
                  _))
      .WillOnce(Return(ToGrpcStatus(absl::InternalError("Oh no."))))
      .WillOnce(DoAll(SetArgPointee<2>(response), Return(grpc::Status::OK)));

  ASSERT_OK_AND_ASSIGN(auto session,
                       Session::Start(std::move(stub_mock), {kPartName1}));
  EXPECT_OK(session->AddAction(
      ActionDescriptor("action_type_name", kActionId, kPartName1)));
  EXPECT_THAT(session->GetLatestOutput(kActionId, absl::InfiniteFuture()),
              StatusIs(absl::StatusCode::kInternal));
  EXPECT_THAT(session->GetLatestOutput(kActionId, absl::InfiniteFuture()),
              IsOkAndHolds(EqualsProto(*expected_output)));
}

absl::StatusOr<intrinsic_proto::icon::JointTrajectoryPVA>
BuildTestProtoTrajectory(int length) {
  intrinsic_proto::icon::JointTrajectoryPVA trajectory;
  for (int i = 0; i < length; i++) {
    INTR_ASSIGN_OR_RETURN(*trajectory.add_time_since_start(),
                          intrinsic::FromAbslDuration(absl::Milliseconds(i)));
    auto state = trajectory.add_state();
    state->add_position(i);
    state->add_velocity(2 * i);
    state->add_acceleration(3 * i);
  }
  return trajectory;
}

// Test case in which the trajectory is so short that it is transmitted within
// one segment.
TEST(SessionMockTest,
     GetPlannedTrajectoryWorksForTrajectoryWithOnlyOneSegment) {
  auto [action_stream_mock, watcher_stream_mock] =
      MakeStreamMockForActionSuccess();
  auto stub_mock =
      std::make_unique<intrinsic_proto::icon::v1::MockIconApiStub>();
  MockStubSetup(std::move(action_stream_mock), std::move(watcher_stream_mock),
                stub_mock.get());

  // Build expected response with a single trajectory segment.
  intrinsic_proto::icon::v1::GetPlannedTrajectoryResponse expected_response;
  ASSERT_OK_AND_ASSIGN(*expected_response.mutable_planned_trajectory_segment(),
                       BuildTestProtoTrajectory(/*length=*/63));

  // Build a mocked stream reader which fills in the expected response,
  // subsequently returns false.
  auto mock_stream_reader = std::make_unique<MockClientReader<
      ::intrinsic_proto::icon::v1::GetPlannedTrajectoryResponse>>();
  EXPECT_CALL(*mock_stream_reader, Read(_))
      .WillOnce(DoAll(SetArgPointee<0>(expected_response), Return(true)))
      .WillOnce(Return(false));
  EXPECT_CALL(*mock_stream_reader, Finish()).WillOnce(Return(grpc::Status::OK));

  constexpr ActionInstanceId kActionId(1);
  // Make the stub_mock return the mocked stream reader.
  EXPECT_CALL(*stub_mock,
              GetPlannedTrajectoryRaw(
                  _, Property(&intrinsic_proto::icon::v1::
                                  GetPlannedTrajectoryRequest::action_id,
                              kActionId.value())))
      .WillOnce(Return(mock_stream_reader.release()));

  ASSERT_OK_AND_ASSIGN(auto session,
                       Session::Start(std::move(stub_mock), {kPartName1}));
  EXPECT_OK(session->AddAction(
      ActionDescriptor("action_type_name", kActionId, kPartName1)));
  // Calling GetPlannedTrajectory() should return the expected response,
  // containing exactly one trajectory segment.
  EXPECT_THAT(session->GetPlannedTrajectory(kActionId),
              IsOkAndHolds(
                  EqualsProto(expected_response.planned_trajectory_segment())));
}

// Test case in which the trajectory is so long that it is transmitted within
// several segments.
TEST(SessionMockTest,
     GetPlannedTrajectoryWorksForTrajectoryWithSeveralSegments) {
  auto [action_stream_mock, watcher_stream_mock] =
      MakeStreamMockForActionSuccess();
  auto stub_mock =
      std::make_unique<intrinsic_proto::icon::v1::MockIconApiStub>();
  MockStubSetup(std::move(action_stream_mock), std::move(watcher_stream_mock),
                stub_mock.get());

  ASSERT_OK_AND_ASSIGN(auto test_trajectory,
                       BuildTestProtoTrajectory(/*length=*/300));
  ASSERT_OK_AND_ASSIGN(std::vector<intrinsic_proto::icon::JointTrajectoryPVA>
                           trajectory_segments,
                       SplitTrajectoryProto(test_trajectory, 64));
  // The given problem parameterization will lead to 5 segments.
  ASSERT_EQ(trajectory_segments.size(), 5);

  // Build expected responses, each holding a trajectory segment.
  std::vector<intrinsic_proto::icon::v1::GetPlannedTrajectoryResponse>
      expected_stream_responses;
  for (const auto& seg : trajectory_segments) {
    intrinsic_proto::icon::v1::GetPlannedTrajectoryResponse response;
    *response.mutable_planned_trajectory_segment() = seg;
    expected_stream_responses.push_back(response);
  }

  // Build a mocked stream reader which fills in the expected responses,
  // subsequently returns false.
  auto mock_stream_reader = std::make_unique<MockClientReader<
      ::intrinsic_proto::icon::v1::GetPlannedTrajectoryResponse>>();
  EXPECT_CALL(*mock_stream_reader, Read(_))
      .WillOnce(
          DoAll(SetArgPointee<0>(expected_stream_responses[0]), Return(true)))
      .WillOnce(
          DoAll(SetArgPointee<0>(expected_stream_responses[1]), Return(true)))
      .WillOnce(
          DoAll(SetArgPointee<0>(expected_stream_responses[2]), Return(true)))
      .WillOnce(
          DoAll(SetArgPointee<0>(expected_stream_responses[3]), Return(true)))
      .WillOnce(
          DoAll(SetArgPointee<0>(expected_stream_responses[4]), Return(true)))
      .WillOnce(Return(false));
  EXPECT_CALL(*mock_stream_reader, Finish()).WillOnce(Return(grpc::Status::OK));

  constexpr ActionInstanceId kActionId(1);
  // Make the stub_mock return the mocked stream reader.
  EXPECT_CALL(*stub_mock,
              GetPlannedTrajectoryRaw(
                  _, Property(&intrinsic_proto::icon::v1::
                                  GetPlannedTrajectoryRequest::action_id,
                              kActionId.value())))
      .WillOnce(Return(mock_stream_reader.release()));

  ASSERT_OK_AND_ASSIGN(auto session,
                       Session::Start(std::move(stub_mock), {kPartName1}));
  EXPECT_OK(session->AddAction(
      ActionDescriptor("action_type_name", kActionId, kPartName1)));
  // Calling GetPlannedTrajectory() should return the expected response,
  // containing the entire test trajectory.
  EXPECT_THAT(session->GetPlannedTrajectory(kActionId),
              IsOkAndHolds(EqualsProto(test_trajectory)));
}

TEST(SessionMockTest, AddActionWithPartName) {
  intrinsic_proto::icon::v1::OpenSessionRequest request;
  auto stream_mocks =
      MakeStreamMockForActionSuccessSaveAddActionRequest(&request);

  auto stub_mock =
      std::make_unique<intrinsic_proto::icon::v1::MockIconApiStub>();
  MockStubSetup(std::move(stream_mocks.first), std::move(stream_mocks.second),
                stub_mock.get());

  ASSERT_OK_AND_ASSIGN(auto session,
                       Session::Start(std::move(stub_mock), {kPartName1}));

  constexpr ActionInstanceId kActionId(1);
  EXPECT_OK(session->AddAction(
      ActionDescriptor("action_type_name", kActionId, kPartName1)));
  EXPECT_THAT(request, EqualsProto(absl::StrFormat(
                           R"pb(
                             add_actions_and_reactions {
                               action_instances {
                                 part_name: "%s"
                                 action_type_name: "action_type_name"
                                 action_instance_id: 1
                               }
                             }
                           )pb",
                           kPartName1)));
}

TEST(SessionMockTest, AddActionWithSlotPartmap) {
  intrinsic_proto::icon::v1::OpenSessionRequest request;
  auto stream_mocks =
      MakeStreamMockForActionSuccessSaveAddActionRequest(&request);

  auto stub_mock =
      std::make_unique<intrinsic_proto::icon::v1::MockIconApiStub>();
  MockStubSetup(std::move(stream_mocks.first), std::move(stream_mocks.second),
                stub_mock.get());

  ASSERT_OK_AND_ASSIGN(auto session,
                       Session::Start(std::move(stub_mock), {kPartName1}));

  constexpr ActionInstanceId kActionId(1);
  EXPECT_OK(session->AddAction(ActionDescriptor("action_type_name", kActionId,
                                                {{"some_slot", kPartName1}})));
  EXPECT_THAT(
      request,
      EqualsProto(absl::StrFormat(
          R"pb(
            add_actions_and_reactions {
              action_instances {
                slot_part_map {
                  slot_name_to_part_name { key: "some_slot" value: "%s" }
                }
                action_type_name: "action_type_name"
                action_instance_id: 1
              }
            }
          )pb",
          kPartName1)));
}

TEST(SessionMockTest, AddActionsExpectedActionsAdded) {
  auto stream_mocks = MakeStreamMockForActionSuccess();

  auto stub_mock =
      std::make_unique<intrinsic_proto::icon::v1::MockIconApiStub>();
  MockStubSetup(std::move(stream_mocks.first), std::move(stream_mocks.second),
                stub_mock.get());

  ASSERT_OK_AND_ASSIGN(auto session,
                       Session::Start(std::move(stub_mock), {kPartName1}));

  constexpr ActionInstanceId kActionId1(1);
  constexpr ActionInstanceId kActionId2(2);
  ASSERT_OK_AND_ASSIGN(
      std::vector<Action> actions,
      session->AddActions({
          ActionDescriptor("action_type_name", kActionId1, kPartName1),
          ActionDescriptor("action_type_name", kActionId2, kPartName1)
              .WithReaction(ReactionDescriptor(Comparison(
                  "var", Comparison::BooleanOperator::kEqual, true))),
      }));

  EXPECT_EQ(actions.size(), 2);
  absl::flat_hash_set<ActionInstanceId> action_instance_ids;
  for (const Action& action : actions) {
    action_instance_ids.insert(action.id());
  }
  EXPECT_TRUE(action_instance_ids.contains(kActionId1));
  EXPECT_TRUE(action_instance_ids.contains(kActionId2));
}

TEST(SessionMockTest, AddActionWithAllOptions) {
  auto stream_mocks = MakeStreamMockForActionSuccess();

  auto stub_mock =
      std::make_unique<intrinsic_proto::icon::v1::MockIconApiStub>();
  MockStubSetup(std::move(stream_mocks.first), std::move(stream_mocks.second),
                stub_mock.get());

  ASSERT_OK_AND_ASSIGN(auto session,
                       Session::Start(std::move(stub_mock), {kPartName1}));

  constexpr ActionInstanceId kActionId(1);
  intrinsic_proto::icon::FakeActionParams params;
  params.set_value(true);
  EXPECT_OK(session->AddAction(
      ActionDescriptor("action_type_name", kActionId, kPartName1)
          .WithFixedParams(params)));
}

TEST(SessionMockTest, AddActionAfterSessionEnd) {
  auto action_stream_mock = std::make_unique<
      MockClientReaderWriter<intrinsic_proto::icon::v1::OpenSessionRequest,
                             intrinsic_proto::icon::v1::OpenSessionResponse>>();
  auto watcher_stream_mock = std::make_unique<
      MockClientReader<intrinsic_proto::icon::v1::WatchReactionsResponse>>();

  {
    InSequence s;
    MockCallSetup(action_stream_mock.get(), absl::OkStatus());
    MockEndActionCall(action_stream_mock.get(), absl::OkStatus());
  }

  EXPECT_CALL(*watcher_stream_mock, Read)
      .WillOnce(Return(true))
      .WillRepeatedly(Return(false));

  auto stub_mock =
      std::make_unique<intrinsic_proto::icon::v1::MockIconApiStub>();
  MockStubSetup(std::move(action_stream_mock), std::move(watcher_stream_mock),
                stub_mock.get());

  ASSERT_OK_AND_ASSIGN(auto session,
                       Session::Start(std::move(stub_mock), {kPartName1}));
  EXPECT_OK(session->End());

  constexpr ActionInstanceId kActionId(1);
  EXPECT_THAT(session->AddAction(
                  ActionDescriptor("action_type_name", kActionId, kPartName1)),
              StatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST(SessionMockTest, AddFreestandingReactionAfterSessionEnd) {
  auto action_stream_mock = std::make_unique<
      MockClientReaderWriter<intrinsic_proto::icon::v1::OpenSessionRequest,
                             intrinsic_proto::icon::v1::OpenSessionResponse>>();
  auto watcher_stream_mock = std::make_unique<
      MockClientReader<intrinsic_proto::icon::v1::WatchReactionsResponse>>();

  {
    InSequence s;
    MockCallSetup(action_stream_mock.get(), absl::OkStatus());
    MockEndActionCall(action_stream_mock.get(), absl::OkStatus());
  }

  EXPECT_CALL(*watcher_stream_mock, Read)
      .WillOnce(Return(true))
      .WillRepeatedly(Return(false));

  auto stub_mock =
      std::make_unique<intrinsic_proto::icon::v1::MockIconApiStub>();
  MockStubSetup(std::move(action_stream_mock), std::move(watcher_stream_mock),
                stub_mock.get());

  ASSERT_OK_AND_ASSIGN(auto session,
                       Session::Start(std::move(stub_mock), {kPartName1}));
  EXPECT_OK(session->End());

  EXPECT_THAT(
      session->AddFreestandingReactions({ReactionDescriptor(IsTrue("foo"))}),
      StatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST(SessionMockTest, AddActionAbortedError) {
  auto action_stream_mock = std::make_unique<
      MockClientReaderWriter<intrinsic_proto::icon::v1::OpenSessionRequest,
                             intrinsic_proto::icon::v1::OpenSessionResponse>>();
  auto watcher_stream_mock = std::make_unique<
      MockClientReader<intrinsic_proto::icon::v1::WatchReactionsResponse>>();

  {
    InSequence s;
    MockCallSetup(action_stream_mock.get(), absl::OkStatus());
    MockActionResponseWithStatus(action_stream_mock.get(),
                                 absl::AbortedError("aborted."));
    MockEndActionCall(action_stream_mock.get(), absl::OkStatus());
  }

  EXPECT_CALL(*watcher_stream_mock, Read)
      .WillOnce(Return(true))
      .WillRepeatedly(Return(false));

  auto stub_mock =
      std::make_unique<intrinsic_proto::icon::v1::MockIconApiStub>();
  MockStubSetup(std::move(action_stream_mock), std::move(watcher_stream_mock),
                stub_mock.get());

  ASSERT_OK_AND_ASSIGN(auto session,
                       Session::Start(std::move(stub_mock), {kPartName1}));

  constexpr ActionInstanceId kActionId(1);
  EXPECT_THAT(session->AddAction(
                  ActionDescriptor("action_type_name", kActionId, kPartName1)),
              StatusIs(absl::StatusCode::kAborted));
  EXPECT_THAT(session->End(), StatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST(SessionMockTest,
     AddActionAbortedErrorSessionEndsNonOkStillReturnsAborted) {
  auto action_stream_mock = std::make_unique<
      MockClientReaderWriter<intrinsic_proto::icon::v1::OpenSessionRequest,
                             intrinsic_proto::icon::v1::OpenSessionResponse>>();
  auto watcher_stream_mock = std::make_unique<
      MockClientReader<intrinsic_proto::icon::v1::WatchReactionsResponse>>();

  {
    InSequence s;
    MockCallSetup(action_stream_mock.get(), absl::OkStatus());
    MockActionResponseWithStatus(action_stream_mock.get(),
                                 absl::AbortedError("aborted."));
    MockEndActionCall(action_stream_mock.get(),
                      absl::CancelledError("Cancelled"));
  }

  EXPECT_CALL(*watcher_stream_mock, Read)
      .WillOnce(Return(true))
      .WillRepeatedly(Return(false));

  auto stub_mock =
      std::make_unique<intrinsic_proto::icon::v1::MockIconApiStub>();
  MockStubSetup(std::move(action_stream_mock), std::move(watcher_stream_mock),
                stub_mock.get());

  ASSERT_OK_AND_ASSIGN(auto session,
                       Session::Start(std::move(stub_mock), {kPartName1}));

  constexpr ActionInstanceId kActionId(1);
  EXPECT_THAT(session->AddAction(
                  ActionDescriptor("action_type_name", kActionId, kPartName1)),
              StatusIs(absl::StatusCode::kAborted));
  EXPECT_THAT(session->End(), StatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST(SessionMockTest, RunWatcherLoopForwardsError) {
  auto action_stream_mock = std::make_unique<
      MockClientReaderWriter<intrinsic_proto::icon::v1::OpenSessionRequest,
                             intrinsic_proto::icon::v1::OpenSessionResponse>>();
  auto watcher_stream_mock = std::make_unique<
      MockClientReader<intrinsic_proto::icon::v1::WatchReactionsResponse>>();

  {
    InSequence s;
    MockCallSetup(action_stream_mock.get(), absl::OkStatus());
    MockEndActionCall(action_stream_mock.get(), absl::OkStatus());
  }

  absl::Status expected_error = absl::InternalError("my session failed");
  {
    InSequence s;
    EXPECT_CALL(*watcher_stream_mock, Read)
        .WillOnce(Return(true))
        .WillOnce(Return(false));
    EXPECT_CALL(*watcher_stream_mock, Finish)
        .WillOnce(Return(ToGrpcStatus(expected_error)));
  }
  auto stub_mock =
      std::make_unique<intrinsic_proto::icon::v1::MockIconApiStub>();
  MockStubSetup(std::move(action_stream_mock), std::move(watcher_stream_mock),
                stub_mock.get());

  ASSERT_OK_AND_ASSIGN(auto session,
                       Session::Start(std::move(stub_mock), {kPartName1}));
  EXPECT_THAT(session->RunWatcherLoop(absl::Now() + absl::Seconds(1)),
              StatusIs(expected_error.code(), expected_error.message()));
}

TEST(SessionMockTest, RunWatcherLoopUntilReactionForwardsError) {
  auto action_stream_mock = std::make_unique<
      MockClientReaderWriter<intrinsic_proto::icon::v1::OpenSessionRequest,
                             intrinsic_proto::icon::v1::OpenSessionResponse>>();
  auto watcher_stream_mock = std::make_unique<
      MockClientReader<intrinsic_proto::icon::v1::WatchReactionsResponse>>();

  {
    InSequence s;
    MockCallSetup(action_stream_mock.get(), absl::OkStatus());
    EXPECT_CALL(*action_stream_mock, Write).WillOnce(Return(true));
    EXPECT_CALL(*action_stream_mock, Read)
        .WillOnce(DoAll(WithArg<0>(write_action_response(absl::OkStatus())),
                        Return(true)));
    MockEndActionCall(action_stream_mock.get(), absl::OkStatus());
  }

  absl::Status expected_error = absl::InternalError("my session failed");

  {
    InSequence s;
    EXPECT_CALL(*watcher_stream_mock, Read)
        .WillOnce(Return(true))
        .WillOnce(Return(false));
    EXPECT_CALL(*watcher_stream_mock, Finish)
        .WillOnce(Return(ToGrpcStatus(expected_error)));
  }
  auto stub_mock =
      std::make_unique<intrinsic_proto::icon::v1::MockIconApiStub>();
  MockStubSetup(std::move(action_stream_mock), std::move(watcher_stream_mock),
                stub_mock.get());

  ASSERT_OK_AND_ASSIGN(auto session,
                       Session::Start(std::move(stub_mock), {kPartName1}));

  constexpr ActionInstanceId kActionId(1);
  Comparison condition("var", Comparison::BooleanOperator::kEqual, true);

  constexpr ReactionHandle kReactionHandle(1);
  EXPECT_OK(session->AddAction(
      ActionDescriptor("action_type_name", kActionId, kPartName1)
          .WithReaction(
              ReactionDescriptor(condition).WithHandle(kReactionHandle))));
  EXPECT_THAT(session->RunWatcherLoopUntilReaction(
                  kReactionHandle, absl::Now() + absl::Seconds(1)),
              StatusIs(expected_error.code(), expected_error.message()));
}

TEST(SessionMockTest, AddActionNonAbortedError) {
  auto action_stream_mock = std::make_unique<
      MockClientReaderWriter<intrinsic_proto::icon::v1::OpenSessionRequest,
                             intrinsic_proto::icon::v1::OpenSessionResponse>>();
  auto watcher_stream_mock = std::make_unique<
      MockClientReader<intrinsic_proto::icon::v1::WatchReactionsResponse>>();

  {
    InSequence s;
    MockCallSetup(action_stream_mock.get(), absl::OkStatus());
    MockActionResponseWithStatus(
        action_stream_mock.get(),
        absl::FailedPreconditionError("failed precondition."));
    MockEndActionCall(action_stream_mock.get(), absl::OkStatus());
  }

  EXPECT_CALL(*watcher_stream_mock, Read)
      .WillOnce(Return(true))
      .WillRepeatedly(Return(false));

  auto stub_mock =
      std::make_unique<intrinsic_proto::icon::v1::MockIconApiStub>();
  MockStubSetup(std::move(action_stream_mock), std::move(watcher_stream_mock),
                stub_mock.get());

  ASSERT_OK_AND_ASSIGN(auto session,
                       Session::Start(std::move(stub_mock), {kPartName1}));

  constexpr ActionInstanceId kActionId(1);
  EXPECT_THAT(session->AddAction(
                  ActionDescriptor("action_type_name", kActionId, kPartName1)),
              StatusIs(absl::StatusCode::kFailedPrecondition));
  EXPECT_OK(session->End());
}

TEST(SessionMockTest, AddActionFailsToWriteToServer) {
  auto action_stream_mock = std::make_unique<
      MockClientReaderWriter<intrinsic_proto::icon::v1::OpenSessionRequest,
                             intrinsic_proto::icon::v1::OpenSessionResponse>>();
  auto watcher_stream_mock = std::make_unique<
      MockClientReader<intrinsic_proto::icon::v1::WatchReactionsResponse>>();

  {
    InSequence s;
    MockCallSetup(action_stream_mock.get(), absl::OkStatus());
    // Fail to write during action add
    EXPECT_CALL(*action_stream_mock, Write).WillOnce(Return(false));
    MockEndActionCall(action_stream_mock.get(),
                      absl::CancelledError("Cancelled"));
  }

  EXPECT_CALL(*watcher_stream_mock, Read)
      .WillOnce(Return(true))
      .WillRepeatedly(Return(false));

  auto stub_mock =
      std::make_unique<intrinsic_proto::icon::v1::MockIconApiStub>();
  MockStubSetup(std::move(action_stream_mock), std::move(watcher_stream_mock),
                stub_mock.get());

  ASSERT_OK_AND_ASSIGN(auto session,
                       Session::Start(std::move(stub_mock), {kPartName1}));
  constexpr ActionInstanceId kActionId(1);
  EXPECT_THAT(session->AddAction(
                  ActionDescriptor("action_type_name", kActionId, kPartName1)),
              StatusIs(absl::StatusCode::kAborted));
}

TEST(SessionMockTest, AddActionFailsToReadFromServer) {
  auto action_stream_mock = std::make_unique<
      MockClientReaderWriter<intrinsic_proto::icon::v1::OpenSessionRequest,
                             intrinsic_proto::icon::v1::OpenSessionResponse>>();
  auto watcher_stream_mock = std::make_unique<
      MockClientReader<intrinsic_proto::icon::v1::WatchReactionsResponse>>();

  {
    InSequence s;
    MockCallSetup(action_stream_mock.get(), absl::OkStatus());
    // Fail to write during action add
    EXPECT_CALL(*action_stream_mock, Write).WillOnce(Return(true));
    EXPECT_CALL(*action_stream_mock, Read).WillOnce(Return(false));
    MockEndActionCall(action_stream_mock.get(),
                      absl::CancelledError("Cancelled"));
  }

  EXPECT_CALL(*watcher_stream_mock, Read)
      .WillOnce(Return(true))
      .WillRepeatedly(Return(false));

  auto stub_mock =
      std::make_unique<intrinsic_proto::icon::v1::MockIconApiStub>();
  MockStubSetup(std::move(action_stream_mock), std::move(watcher_stream_mock),
                stub_mock.get());

  ASSERT_OK_AND_ASSIGN(auto session,
                       Session::Start(std::move(stub_mock), {kPartName1}));
  constexpr ActionInstanceId kActionId(1);
  EXPECT_THAT(session->AddAction(
                  ActionDescriptor("action_type_name", kActionId, kPartName1)),
              StatusIs(absl::StatusCode::kAborted));
}

TEST(SessionMockTest, StartFailsOnWriteReturnsFinishError) {
  auto action_stream_mock = std::make_unique<
      MockClientReaderWriter<intrinsic_proto::icon::v1::OpenSessionRequest,
                             intrinsic_proto::icon::v1::OpenSessionResponse>>();

  const absl::Status expected_error = absl::CancelledError("Cancelled");
  {
    InSequence s;
    EXPECT_CALL(*action_stream_mock, Write).WillOnce(Return(false));
    EXPECT_CALL(*action_stream_mock, Read).WillOnce(Return(false));
    EXPECT_CALL(*action_stream_mock, Finish())
        .WillOnce(Return(ToGrpcStatus(expected_error)));
  }

  auto stub_mock =
      std::make_unique<intrinsic_proto::icon::v1::MockIconApiStub>();
  EXPECT_CALL(*stub_mock, OpenSessionRaw)
      .WillOnce(
          Return(action_stream_mock.release()));  // `stub_mock` takes ownership

  EXPECT_THAT(Session::Start(std::move(stub_mock), {kPartName1}),
              StatusIs(expected_error.code()));
}

TEST(SessionMockTest, StartFailsOnReadReturnsFinishStatus) {
  auto action_stream_mock = std::make_unique<
      MockClientReaderWriter<intrinsic_proto::icon::v1::OpenSessionRequest,
                             intrinsic_proto::icon::v1::OpenSessionResponse>>();

  const absl::Status expected_error = absl::CancelledError("Cancelled");
  {
    InSequence s;
    EXPECT_CALL(*action_stream_mock, Write).WillOnce(Return(true));
    EXPECT_CALL(*action_stream_mock, Read).WillRepeatedly(Return(false));
    EXPECT_CALL(*action_stream_mock, Finish())
        .WillOnce(Return(ToGrpcStatus(expected_error)));
  }

  auto stub_mock =
      std::make_unique<intrinsic_proto::icon::v1::MockIconApiStub>();
  EXPECT_CALL(*stub_mock, OpenSessionRaw)
      .WillOnce(
          Return(action_stream_mock.release()));  // `stub_mock` takes ownership

  EXPECT_THAT(Session::Start(std::move(stub_mock), {kPartName1}),
              StatusIs(expected_error.code()));
}

TEST(SessionMockTest, StartInitializationFailureReturnsInitStatus) {
  auto action_stream_mock = std::make_unique<
      MockClientReaderWriter<intrinsic_proto::icon::v1::OpenSessionRequest,
                             intrinsic_proto::icon::v1::OpenSessionResponse>>();
  auto watcher_stream_mock = std::make_unique<
      MockClientReader<intrinsic_proto::icon::v1::WatchReactionsResponse>>();

  const absl::Status expected_error =
      absl::FailedPreconditionError("Failed Precondition");
  {
    InSequence s;
    MockCallSetup(action_stream_mock.get(), expected_error);
    MockEndActionCall(action_stream_mock.get(), absl::OkStatus());
  }

  auto stub_mock =
      std::make_unique<intrinsic_proto::icon::v1::MockIconApiStub>();
  EXPECT_CALL(*stub_mock, OpenSessionRaw)
      .WillOnce(
          Return(action_stream_mock.release()));  // `stub_mock` takes ownership

  EXPECT_THAT(Session::Start(std::move(stub_mock), {kPartName1}),
              StatusIs(expected_error.code()));
}

TEST(SessionMockTest,
     StartMissingInitializationDataFromServerReturnsInternalError) {
  auto action_stream_mock = std::make_unique<
      MockClientReaderWriter<intrinsic_proto::icon::v1::OpenSessionRequest,
                             intrinsic_proto::icon::v1::OpenSessionResponse>>();

  {
    InSequence s;
    EXPECT_CALL(*action_stream_mock, Write).WillOnce(Return(true));
    intrinsic_proto::icon::v1::OpenSessionResponse response;
    EXPECT_CALL(*action_stream_mock, Read)
        .WillOnce(DoAll(SetArgPointee<0>(response), Return(true)));
    MockEndActionCall(action_stream_mock.get(), absl::OkStatus());
  }

  auto stub_mock =
      std::make_unique<intrinsic_proto::icon::v1::MockIconApiStub>();
  EXPECT_CALL(*stub_mock, OpenSessionRaw)
      .WillOnce(
          Return(action_stream_mock.release()));  // `stub_mock` takes ownership

  EXPECT_THAT(Session::Start(std::move(stub_mock), {kPartName1}),
              StatusIs(absl::StatusCode::kInternal));
}

TEST(SessionMockTest, AddActionWithReaction) {
  auto stream_mocks = MakeStreamMockForActionSuccess();

  auto stub_mock =
      std::make_unique<intrinsic_proto::icon::v1::MockIconApiStub>();
  MockStubSetup(std::move(stream_mocks.first), std::move(stream_mocks.second),
                stub_mock.get());

  ASSERT_OK_AND_ASSIGN(auto session,
                       Session::Start(std::move(stub_mock), {kPartName1}));

  constexpr ActionInstanceId kActionId(1);
  Comparison condition("var", Comparison::BooleanOperator::kEqual, true);
  EXPECT_OK(session->AddAction(
      ActionDescriptor("action_type_name", kActionId, kPartName1)
          .WithReaction(ReactionDescriptor(condition))));
}

TEST(SessionMockTest, AddActionWithDuplicateReactionHandles) {
  auto action_stream_mock = std::make_unique<
      MockClientReaderWriter<intrinsic_proto::icon::v1::OpenSessionRequest,
                             intrinsic_proto::icon::v1::OpenSessionResponse>>();
  auto watcher_stream_mock = std::make_unique<
      MockClientReader<intrinsic_proto::icon::v1::WatchReactionsResponse>>();

  {
    InSequence s;
    MockCallSetup(action_stream_mock.get(), absl::OkStatus());
    MockEndActionCall(action_stream_mock.get(), absl::OkStatus());
  }

  EXPECT_CALL(*watcher_stream_mock, Read)
      .WillOnce(Return(true))
      .WillRepeatedly(Return(false));

  auto stub_mock =
      std::make_unique<intrinsic_proto::icon::v1::MockIconApiStub>();
  MockStubSetup(std::move(action_stream_mock), std::move(watcher_stream_mock),
                stub_mock.get());

  ASSERT_OK_AND_ASSIGN(auto session,
                       Session::Start(std::move(stub_mock), {kPartName1}));

  constexpr ActionInstanceId kActionId(1);
  constexpr ReactionHandle kReactionHandle(1);
  Comparison condition("var", Comparison::BooleanOperator::kEqual, true);
  EXPECT_THAT(session->AddAction(
                  ActionDescriptor("action_type_name", kActionId, kPartName1)
                      .WithReaction(ReactionDescriptor(condition).WithHandle(
                          kReactionHandle))
                      .WithReaction(ReactionDescriptor(condition).WithHandle(
                          kReactionHandle))),
              StatusIs(absl::StatusCode::kAlreadyExists));
}

TEST(SessionMockTest, AddMultipleActionsWithDuplicateReactionHandles) {
  auto action_stream_mock = std::make_unique<
      MockClientReaderWriter<intrinsic_proto::icon::v1::OpenSessionRequest,
                             intrinsic_proto::icon::v1::OpenSessionResponse>>();
  auto watcher_stream_mock = std::make_unique<
      MockClientReader<intrinsic_proto::icon::v1::WatchReactionsResponse>>();

  {
    InSequence s;
    MockCallSetup(action_stream_mock.get(), absl::OkStatus());
    MockEndActionCall(action_stream_mock.get(), absl::OkStatus());
  }

  EXPECT_CALL(*watcher_stream_mock, Read)
      .WillOnce(Return(true))
      .WillRepeatedly(Return(false));

  auto stub_mock =
      std::make_unique<intrinsic_proto::icon::v1::MockIconApiStub>();
  MockStubSetup(std::move(action_stream_mock), std::move(watcher_stream_mock),
                stub_mock.get());

  ASSERT_OK_AND_ASSIGN(auto session,
                       Session::Start(std::move(stub_mock), {kPartName1}));

  constexpr ActionInstanceId kActionId1(1);
  constexpr ActionInstanceId kActionId2(2);
  constexpr ReactionHandle kReactionHandle(1);
  Comparison condition("var", Comparison::BooleanOperator::kEqual, true);
  EXPECT_THAT(session->AddActions(
                  {ActionDescriptor("action_type_name", kActionId1, kPartName1)
                       .WithReaction(ReactionDescriptor(condition).WithHandle(
                           kReactionHandle)),
                   ActionDescriptor("action_type_name", kActionId2, kPartName1)
                       .WithReaction(ReactionDescriptor(condition).WithHandle(
                           kReactionHandle))}),
              StatusIs(absl::StatusCode::kAlreadyExists));
}

TEST(SessionMockTest, AddActionWithReactionAndRealtimeActionOnCondition) {
  constexpr int kNumActions = 2;
  auto stream_mocks = MakeStreamMockForActionSuccess(kNumActions);

  auto stub_mock =
      std::make_unique<intrinsic_proto::icon::v1::MockIconApiStub>();
  MockStubSetup(std::move(stream_mocks.first), std::move(stream_mocks.second),
                stub_mock.get());

  ASSERT_OK_AND_ASSIGN(auto session,
                       Session::Start(std::move(stub_mock), {kPartName1}));

  constexpr ActionInstanceId kActionId1(1);
  EXPECT_OK(session->AddAction(
      ActionDescriptor("action_type_name", kActionId1, kPartName1)));

  constexpr ActionInstanceId kActionId2(2);
  Comparison condition("var", Comparison::BooleanOperator::kEqual, true);
  EXPECT_OK(session->AddAction(
      ActionDescriptor("action_type_name", kActionId2, kPartName1)
          .WithReaction(ReactionDescriptor(condition)
                            .WithRealtimeActionOnCondition(kActionId1))));
}

TEST(SessionMockTest,
     AddActionWithReactionAndParallelRealtimeActionOnCondition) {
  constexpr int kNumActions = 2;
  auto stream_mocks = MakeStreamMockForActionSuccess(kNumActions);

  auto stub_mock =
      std::make_unique<intrinsic_proto::icon::v1::MockIconApiStub>();
  MockStubSetup(std::move(stream_mocks.first), std::move(stream_mocks.second),
                stub_mock.get());

  ASSERT_OK_AND_ASSIGN(auto session,
                       Session::Start(std::move(stub_mock), {kPartName1}));

  constexpr ActionInstanceId kActionId1(1);
  EXPECT_OK(session->AddAction(
      ActionDescriptor("action_type_name", kActionId1, kPartName1)));

  constexpr ActionInstanceId kActionId2(2);
  Comparison condition("var", Comparison::BooleanOperator::kEqual, true);
  EXPECT_OK(session->AddAction(
      ActionDescriptor("action_type_name", kActionId2, kPartName1)
          .WithReaction(
              ReactionDescriptor(condition)
                  .WithParallelRealtimeActionOnCondition(kActionId1))));
}

TEST(SessionMockTest, AddActionWithWatcherOnCondition) {
  auto stream_mocks = MakeStreamMockForActionSuccess();

  auto stub_mock =
      std::make_unique<intrinsic_proto::icon::v1::MockIconApiStub>();
  MockStubSetup(std::move(stream_mocks.first), std::move(stream_mocks.second),
                stub_mock.get());

  ASSERT_OK_AND_ASSIGN(auto session,
                       Session::Start(std::move(stub_mock), {kPartName1}));

  constexpr ActionInstanceId kActionId(1);
  Comparison condition("var", Comparison::BooleanOperator::kEqual, true);
  EXPECT_OK(session->AddAction(
      ActionDescriptor("action_type_name", kActionId, kPartName1)
          .WithReaction(
              ReactionDescriptor(condition).WithWatcherOnCondition([]() {}))));
}

TEST(SessionMockTest, AddActionWithFireOnceReaction) {
  auto stream_mocks = MakeStreamMockForActionSuccess();

  auto stub_mock =
      std::make_unique<intrinsic_proto::icon::v1::MockIconApiStub>();
  MockStubSetup(std::move(stream_mocks.first), std::move(stream_mocks.second),
                stub_mock.get());

  ASSERT_OK_AND_ASSIGN(auto session,
                       Session::Start(std::move(stub_mock), {kPartName1}));

  constexpr ActionInstanceId kActionId(1);
  Comparison condition("var", Comparison::BooleanOperator::kEqual, true);
  EXPECT_OK(session->AddAction(
      ActionDescriptor("action_type_name", kActionId, kPartName1)
          .WithReaction(ReactionDescriptor(condition).FireOnce())));
}

TEST(SessionMockTest, AddActionWithRealtimeSignalOnCondition) {
  intrinsic_proto::icon::v1::OpenSessionRequest request;
  auto stream_mocks =
      MakeStreamMockForActionSuccessSaveAddActionRequest(&request);

  auto stub_mock =
      std::make_unique<intrinsic_proto::icon::v1::MockIconApiStub>();
  MockStubSetup(std::move(stream_mocks.first), std::move(stream_mocks.second),
                stub_mock.get());

  ASSERT_OK_AND_ASSIGN(auto session,
                       Session::Start(std::move(stub_mock), {kPartName1}));

  constexpr ActionInstanceId kActionId(1);
  Comparison condition("var", Comparison::BooleanOperator::kEqual, true);
  EXPECT_OK(session->AddAction(
      ActionDescriptor("action_type_name", kActionId, kPartName1)
          .WithReaction(ReactionDescriptor(condition)
                            .WithRealtimeSignalOnCondition("signal"))));
  EXPECT_THAT(request, EqualsProto(absl::StrFormat(
                           R"pb(
                             add_actions_and_reactions {
                               action_instances {
                                 action_type_name: "action_type_name"
                                 action_instance_id: 1
                                 part_name: "%s"
                               }
                               reactions {
                                 condition {
                                   comparison {
                                     state_variable_name: "var"
                                     operation: EQUAL
                                     bool_value: true
                                     max_abs_error: %.10f
                                   }
                                 }
                                 action_association {
                                   action_instance_id: 1
                                   triggered_signal_name: "signal"
                                 }
                               }
                             }
                           )pb",
                           kPartName1, kDefaultMaxAbsError)));
}

TEST(SessionMockTest,
     AddActionWithRealtimeSignalOnConditionWhichOverridesPreviousSignal) {
  intrinsic_proto::icon::v1::OpenSessionRequest request;
  auto stream_mocks =
      MakeStreamMockForActionSuccessSaveAddActionRequest(&request);
  auto stub_mock =
      std::make_unique<intrinsic_proto::icon::v1::MockIconApiStub>();
  MockStubSetup(std::move(stream_mocks.first), std::move(stream_mocks.second),
                stub_mock.get());

  ASSERT_OK_AND_ASSIGN(auto session,
                       Session::Start(std::move(stub_mock), {kPartName1}));

  constexpr ActionInstanceId kActionId(1);
  Comparison condition("var", Comparison::BooleanOperator::kEqual, true);
  EXPECT_OK(session->AddAction(
      ActionDescriptor("action_type_name", kActionId, kPartName1)
          .WithReaction(ReactionDescriptor(condition)
                            .WithRealtimeSignalOnCondition("signal1")
                            .WithRealtimeSignalOnCondition("signal2"))));
  EXPECT_THAT(request, EqualsProto(absl::StrFormat(
                           R"pb(
                             add_actions_and_reactions {
                               action_instances {
                                 action_type_name: "action_type_name"
                                 action_instance_id: 1
                                 part_name: "%s"
                               }
                               reactions {
                                 condition {
                                   comparison {
                                     state_variable_name: "var"
                                     operation: EQUAL
                                     bool_value: true
                                     max_abs_error: %.10f
                                   }
                                 }
                                 action_association {
                                   action_instance_id: 1
                                   triggered_signal_name: "signal2"
                                 }
                               }
                             }
                           )pb",
                           kPartName1, kDefaultMaxAbsError)));
}

TEST(SessionMockTest, AddFreestandingReactions) {
  auto stream_mocks = MakeStreamMockForActionSuccess(2);

  auto stub_mock =
      std::make_unique<intrinsic_proto::icon::v1::MockIconApiStub>();
  MockStubSetup(std::move(stream_mocks.first), std::move(stream_mocks.second),
                stub_mock.get());

  ASSERT_OK_AND_ASSIGN(auto session,
                       Session::Start(std::move(stub_mock), {kPartName1}));

  Comparison condition("var", Comparison::BooleanOperator::kEqual, true);
  const ReactionHandle kReactionHandle1(1);
  const ReactionHandle kReactionHandle2(2);

  EXPECT_OK(session->AddFreestandingReactions(
      {ReactionDescriptor(condition).WithHandle(kReactionHandle1)}));
  EXPECT_OK(session->AddFreestandingReactions(
      {ReactionDescriptor(condition).WithHandle(kReactionHandle2)}));
}

TEST(SessionMockTest, AddDuplicateReactionHandleFails) {
  auto action_stream_mock = std::make_unique<
      MockClientReaderWriter<intrinsic_proto::icon::v1::OpenSessionRequest,
                             intrinsic_proto::icon::v1::OpenSessionResponse>>();
  auto watcher_stream_mock = std::make_unique<
      MockClientReader<intrinsic_proto::icon::v1::WatchReactionsResponse>>();

  {
    InSequence s;
    MockCallSetup(action_stream_mock.get(), absl::OkStatus());
    MockEndActionCall(action_stream_mock.get(), absl::OkStatus());
  }

  EXPECT_CALL(*watcher_stream_mock, Read)
      .WillOnce(Return(true))
      .WillRepeatedly(Return(false));

  auto stub_mock =
      std::make_unique<intrinsic_proto::icon::v1::MockIconApiStub>();
  MockStubSetup(std::move(action_stream_mock), std::move(watcher_stream_mock),
                stub_mock.get());

  ASSERT_OK_AND_ASSIGN(auto session,
                       Session::Start(std::move(stub_mock), {kPartName1}));

  Comparison condition("var", Comparison::BooleanOperator::kEqual, true);
  const ReactionHandle kReactionHandle(1);

  EXPECT_THAT(session->AddFreestandingReactions(
                  {ReactionDescriptor(condition).WithHandle(kReactionHandle),
                   ReactionDescriptor(condition).WithHandle(kReactionHandle)}),
              StatusIs(absl::StatusCode::kAlreadyExists));
}

TEST(SessionMockTest, StopAllActionsSucceeds) {
  auto stream_mocks = MakeStreamMockForActionSuccess();

  auto stub_mock =
      std::make_unique<intrinsic_proto::icon::v1::MockIconApiStub>();
  MockStubSetup(std::move(stream_mocks.first), std::move(stream_mocks.second),
                stub_mock.get());

  ASSERT_OK_AND_ASSIGN(auto session,
                       Session::Start(std::move(stub_mock), {kPartName1}));

  EXPECT_OK(session->StopAllActions());
}

TEST(SessionMockTest, RemoveActionSuccess) {
  auto stream_mocks = MakeStreamMockForActionSuccess();

  auto stub_mock =
      std::make_unique<intrinsic_proto::icon::v1::MockIconApiStub>();
  MockStubSetup(std::move(stream_mocks.first), std::move(stream_mocks.second),
                stub_mock.get());

  ASSERT_OK_AND_ASSIGN(auto session,
                       Session::Start(std::move(stub_mock), {kPartName1}));

  constexpr ActionInstanceId kActionId(1);
  EXPECT_OK(session->RemoveAction(kActionId));
}

TEST(SessionMockTest, RemoveActionAfterSessionEnd) {
  auto action_stream_mock = std::make_unique<
      MockClientReaderWriter<intrinsic_proto::icon::v1::OpenSessionRequest,
                             intrinsic_proto::icon::v1::OpenSessionResponse>>();
  auto watcher_stream_mock = std::make_unique<
      MockClientReader<intrinsic_proto::icon::v1::WatchReactionsResponse>>();

  {
    InSequence s;
    MockCallSetup(action_stream_mock.get(), absl::OkStatus());
    MockEndActionCall(action_stream_mock.get(), absl::OkStatus());
  }

  EXPECT_CALL(*watcher_stream_mock, Read)
      .WillOnce(Return(true))
      .WillRepeatedly(Return(false));

  auto stub_mock =
      std::make_unique<intrinsic_proto::icon::v1::MockIconApiStub>();
  MockStubSetup(std::move(action_stream_mock), std::move(watcher_stream_mock),
                stub_mock.get());

  ASSERT_OK_AND_ASSIGN(auto session,
                       Session::Start(std::move(stub_mock), {kPartName1}));
  EXPECT_OK(session->End());

  constexpr ActionInstanceId kActionId(1);
  EXPECT_THAT(session->RemoveAction(kActionId),
              StatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST(SessionMockTest, RemoveActionsSuccess) {
  auto stream_mocks = MakeStreamMockForActionSuccess();

  auto stub_mock =
      std::make_unique<intrinsic_proto::icon::v1::MockIconApiStub>();
  MockStubSetup(std::move(stream_mocks.first), std::move(stream_mocks.second),
                stub_mock.get());

  ASSERT_OK_AND_ASSIGN(auto session,
                       Session::Start(std::move(stub_mock), {kPartName1}));

  constexpr ActionInstanceId kActionId1(1);
  constexpr ActionInstanceId kActionId2(2);
  EXPECT_OK(session->RemoveActions({kActionId1, kActionId2}));
}

TEST(SessionMockTest, RemoveActionsAfterSessionEnd) {
  auto action_stream_mock = std::make_unique<
      MockClientReaderWriter<intrinsic_proto::icon::v1::OpenSessionRequest,
                             intrinsic_proto::icon::v1::OpenSessionResponse>>();
  auto watcher_stream_mock = std::make_unique<
      MockClientReader<intrinsic_proto::icon::v1::WatchReactionsResponse>>();

  {
    InSequence s;
    MockCallSetup(action_stream_mock.get(), absl::OkStatus());
    MockEndActionCall(action_stream_mock.get(), absl::OkStatus());
  }

  EXPECT_CALL(*watcher_stream_mock, Read)
      .WillOnce(Return(true))
      .WillRepeatedly(Return(false));

  auto stub_mock =
      std::make_unique<intrinsic_proto::icon::v1::MockIconApiStub>();
  MockStubSetup(std::move(action_stream_mock), std::move(watcher_stream_mock),
                stub_mock.get());

  ASSERT_OK_AND_ASSIGN(auto session,
                       Session::Start(std::move(stub_mock), {kPartName1}));
  EXPECT_OK(session->End());

  constexpr ActionInstanceId kActionId1(1);
  constexpr ActionInstanceId kActionId2(3);
  EXPECT_THAT(session->RemoveActions({kActionId1, kActionId2}),
              StatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST(SessionMockTest, ClearAllActionsSuccess) {
  auto stream_mocks = MakeStreamMockForActionSuccess();

  auto stub_mock =
      std::make_unique<intrinsic_proto::icon::v1::MockIconApiStub>();
  MockStubSetup(std::move(stream_mocks.first), std::move(stream_mocks.second),
                stub_mock.get());

  ASSERT_OK_AND_ASSIGN(auto session,
                       Session::Start(std::move(stub_mock), {kPartName1}));

  EXPECT_OK(session->ClearAllActionsAndReactions());
}

TEST(SessionMockTest, ClearAllActionsAfterSessionEnd) {
  auto action_stream_mock = std::make_unique<
      MockClientReaderWriter<intrinsic_proto::icon::v1::OpenSessionRequest,
                             intrinsic_proto::icon::v1::OpenSessionResponse>>();
  auto watcher_stream_mock = std::make_unique<
      MockClientReader<intrinsic_proto::icon::v1::WatchReactionsResponse>>();

  {
    InSequence s;
    MockCallSetup(action_stream_mock.get(), absl::OkStatus());
    MockEndActionCall(action_stream_mock.get(), absl::OkStatus());
  }

  EXPECT_CALL(*watcher_stream_mock, Read)
      .WillOnce(Return(true))
      .WillRepeatedly(Return(false));

  auto stub_mock =
      std::make_unique<intrinsic_proto::icon::v1::MockIconApiStub>();
  MockStubSetup(std::move(action_stream_mock), std::move(watcher_stream_mock),
                stub_mock.get());

  ASSERT_OK_AND_ASSIGN(auto session,
                       Session::Start(std::move(stub_mock), {kPartName1}));
  EXPECT_OK(session->End());

  EXPECT_THAT(session->ClearAllActionsAndReactions(),
              StatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST(SessionMockTest, StartAction) {
  auto action_stream_mock = std::make_unique<
      MockClientReaderWriter<intrinsic_proto::icon::v1::OpenSessionRequest,
                             intrinsic_proto::icon::v1::OpenSessionResponse>>();
  auto watcher_stream_mock = std::make_unique<
      MockClientReader<intrinsic_proto::icon::v1::WatchReactionsResponse>>();

  {
    InSequence s;
    MockCallSetup(action_stream_mock.get(), absl::OkStatus());
    MockActionResponseWithStatus(action_stream_mock.get(), absl::OkStatus());
    MockActionResponseWithStatus(action_stream_mock.get(), absl::OkStatus());
    MockEndActionCall(action_stream_mock.get(), absl::OkStatus());
  }
  EXPECT_CALL(*watcher_stream_mock, Read)
      .WillOnce(Return(true))
      .WillRepeatedly(Return(false));

  auto stub_mock =
      std::make_unique<intrinsic_proto::icon::v1::MockIconApiStub>();
  MockStubSetup(std::move(action_stream_mock), std::move(watcher_stream_mock),
                stub_mock.get());

  ASSERT_OK_AND_ASSIGN(auto session,
                       Session::Start(std::move(stub_mock), {kPartName1}));
  ASSERT_OK_AND_ASSIGN(
      auto action, session->AddAction(ActionDescriptor(
                       "action_type_name", ActionInstanceId(1), kPartName1)));
  ASSERT_THAT(session->StartAction(action), IsOk());
}

TEST(SessionMockTest, StartActions) {
  auto action_stream_mock = std::make_unique<
      MockClientReaderWriter<intrinsic_proto::icon::v1::OpenSessionRequest,
                             intrinsic_proto::icon::v1::OpenSessionResponse>>();
  auto watcher_stream_mock = std::make_unique<
      MockClientReader<intrinsic_proto::icon::v1::WatchReactionsResponse>>();

  {
    InSequence s;
    MockCallSetup(action_stream_mock.get(), absl::OkStatus());
    MockActionResponseWithStatus(action_stream_mock.get(), absl::OkStatus());
    MockActionResponseWithStatus(action_stream_mock.get(), absl::OkStatus());
    MockActionResponseWithStatus(action_stream_mock.get(), absl::OkStatus());
    MockEndActionCall(action_stream_mock.get(), absl::OkStatus());
  }
  EXPECT_CALL(*watcher_stream_mock, Read)
      .WillOnce(Return(true))
      .WillRepeatedly(Return(false));

  auto stub_mock =
      std::make_unique<intrinsic_proto::icon::v1::MockIconApiStub>();
  MockStubSetup(std::move(action_stream_mock), std::move(watcher_stream_mock),
                stub_mock.get());

  ASSERT_OK_AND_ASSIGN(auto session,
                       Session::Start(std::move(stub_mock), {kPartName1}));
  ASSERT_OK_AND_ASSIGN(
      auto action, session->AddAction(ActionDescriptor(
                       "action_type_name", ActionInstanceId(1), kPartName1)));
  ASSERT_OK_AND_ASSIGN(
      auto action2, session->AddAction(ActionDescriptor(
                        "action2_type_name", ActionInstanceId(2), kPartName2)));
  ASSERT_THAT(
      session->StartActions({action, action2}, /*stop_active_actions=*/true),
      IsOk());
}

TEST(SessionMockTest, StartActionAfterSessionEnd) {
  auto action_stream_mock = std::make_unique<
      MockClientReaderWriter<intrinsic_proto::icon::v1::OpenSessionRequest,
                             intrinsic_proto::icon::v1::OpenSessionResponse>>();
  auto watcher_stream_mock = std::make_unique<
      MockClientReader<intrinsic_proto::icon::v1::WatchReactionsResponse>>();

  {
    InSequence s;
    MockCallSetup(action_stream_mock.get(), absl::OkStatus());
    MockActionResponseWithStatus(action_stream_mock.get(), absl::OkStatus());
    MockEndActionCall(action_stream_mock.get(), absl::OkStatus());
  }

  EXPECT_CALL(*watcher_stream_mock, Read)
      .WillOnce(Return(true))
      .WillRepeatedly(Return(false));

  auto stub_mock =
      std::make_unique<intrinsic_proto::icon::v1::MockIconApiStub>();
  MockStubSetup(std::move(action_stream_mock), std::move(watcher_stream_mock),
                stub_mock.get());

  ASSERT_OK_AND_ASSIGN(auto session,
                       Session::Start(std::move(stub_mock), {kPartName1}));
  ASSERT_OK_AND_ASSIGN(
      auto action, session->AddAction(ActionDescriptor(
                       "action_type_name", ActionInstanceId(1), kPartName1)));
  EXPECT_OK(session->End());
  ASSERT_THAT(session->StartAction(action),
              StatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST(SessionMockTest, StartActionFailureIsPropagated) {
  auto action_stream_mock = std::make_unique<
      MockClientReaderWriter<intrinsic_proto::icon::v1::OpenSessionRequest,
                             intrinsic_proto::icon::v1::OpenSessionResponse>>();
  auto watcher_stream_mock = std::make_unique<
      MockClientReader<intrinsic_proto::icon::v1::WatchReactionsResponse>>();

  {
    InSequence s;
    MockCallSetup(action_stream_mock.get(), absl::OkStatus());
    MockActionResponseWithStatus(action_stream_mock.get(), absl::OkStatus());
    MockActionResponseWithStatus(action_stream_mock.get(),
                                 absl::InternalError("Failed to start action"));
    MockEndActionCall(action_stream_mock.get(), absl::OkStatus());
  }
  EXPECT_CALL(*watcher_stream_mock, Read)
      .WillOnce(Return(true))
      .WillRepeatedly(Return(false));

  auto stub_mock =
      std::make_unique<intrinsic_proto::icon::v1::MockIconApiStub>();
  MockStubSetup(std::move(action_stream_mock), std::move(watcher_stream_mock),
                stub_mock.get());

  ASSERT_OK_AND_ASSIGN(auto session,
                       Session::Start(std::move(stub_mock), {kPartName1}));
  ASSERT_OK_AND_ASSIGN(
      auto action, session->AddAction(ActionDescriptor(
                       "action_type_name", ActionInstanceId(1), kPartName1)));

  ASSERT_THAT(session->StartAction(action),
              StatusIs(absl::StatusCode::kInternal));
}

intrinsic_proto::icon::v1::WatchReactionsResponse GetReaction(
    ReactionId reaction_id) {
  intrinsic_proto::icon::v1::WatchReactionsResponse reaction;
  reaction.mutable_reaction_event()->set_reaction_id(reaction_id.value());
  return reaction;
}

bool HasAddActionsAndReactions(
    const intrinsic_proto::icon::v1::OpenSessionRequest& req) {
  return req.has_add_actions_and_reactions();
}

// Helper class that saves a number of Reaction protos from an
// OpenSessionRequest. The synchronization afforded by SaveReactions and
// WaitForReactions is needed because WatchActions (where the Reactions are
// required to build a reaction event proto) is called in a background thread.
class ReactionSaver {
 public:
  // Blocks until SaveReactions has finished, and then returns the saved
  // ReactionIds. Subsequent calls will not block.
  //
  // The ReactionIds can then be used to generate Reaction events (see below).
  std::vector<intrinsic_proto::icon::v1::Reaction>& WaitForReactions() {
    notification_.WaitForNotification();
    return reactions_;
  }

  // Use this as an Action (with ::testing::Invoke) to save Reaction protos from
  // an AddReactions RPC call. Calling this unblocks WaitForReactions. Any
  // subsequent calls will return false and do nothing.
  bool SaveReactions(const intrinsic_proto::icon::v1::OpenSessionRequest& req,
                     const ::grpc::WriteOptions& options) {
    if (notification_.HasBeenNotified()) {
      return false;
    }
    reactions_ = {req.add_actions_and_reactions().reactions().begin(),
                  req.add_actions_and_reactions().reactions().end()};
    notification_.Notify();
    return true;
  }

 private:
  std::vector<intrinsic_proto::icon::v1::Reaction> reactions_;
  absl::Notification notification_;
};

// Custom action that returns a Reaction event corresponding to the Nth reaction
// proto captured by a ReactionSaver.
struct ReturnReactionEvent {
  ReactionSaver* reaction_saver;
  size_t reaction_index;

  bool operator()(
      intrinsic_proto::icon::v1::WatchReactionsResponse* response) const {
    std::vector<intrinsic_proto::icon::v1::Reaction> reactions =
        reaction_saver->WaitForReactions();
    CHECK(reaction_index < reactions.size())
        << "ReturnReactionEvent got out-of-bounds index " << reaction_index
        << ". Did your AddReactions call add the expected number of IDs?";
    *response = GetReaction(
        ReactionId(reactions[reaction_index].reaction_instance_id()));
    return true;
  }
};

// Custom action that finds a Reaction proto with the given Condition, and
// returns a corresponding Reaction event.
struct ReturnReactionEventForCondition {
  ReactionSaver* reaction_saver;
  Condition condition;

  bool operator()(
      intrinsic_proto::icon::v1::WatchReactionsResponse* response) const {
    const intrinsic_proto::icon::v1::Condition comparison_proto =
        ToProto(condition);
    // There's an important side effect in the if statement.
    // NOLINTNEXTLINE(readability-use-anyofallof)
    for (const auto& reaction : reaction_saver->WaitForReactions()) {
      if (google::protobuf::util::MessageDifferencer::Equals(
              comparison_proto, reaction.condition())) {
        *response = GetReaction(ReactionId(reaction.reaction_instance_id()));
        return true;
      }
    }
    return false;
  }
};

// Sets up expectations for
// * Setup of an OpenSession
// * Adding an action with a single reaction
// * Reading a corresponding reaction event
// * Closing the WatchReactions and OpenSession streams
//
// The unique_ptr this returns must outlive the session.
ABSL_MUST_USE_RESULT std::unique_ptr<ReactionSaver>
SetupSingleReactionExpectations(
    MockClientReaderWriter<intrinsic_proto::icon::v1::OpenSessionRequest,
                           intrinsic_proto::icon::v1::OpenSessionResponse>*
        action_stream_mock,
    MockClientReader<intrinsic_proto::icon::v1::WatchReactionsResponse>*
        watcher_stream_mock) {
  auto reaction_saver = std::make_unique<ReactionSaver>();
  {
    InSequence s;
    MockCallSetup(action_stream_mock, absl::OkStatus());
    EXPECT_CALL(*action_stream_mock,
                Write(Truly(&HasAddActionsAndReactions), _))
        .WillOnce(Invoke(reaction_saver.get(), &ReactionSaver::SaveReactions));
    EXPECT_CALL(*action_stream_mock, Read)
        .WillOnce(DoAll(WithArg<0>(write_action_response(absl::OkStatus())),
                        Return(true)));
    MockEndActionCall(action_stream_mock, absl::OkStatus());
  }
  // No gmock sequencing for watcher reads, but the Notification takes care of
  // that.
  EXPECT_CALL(*watcher_stream_mock, Read)
      .WillOnce(Return(true))
      .WillOnce(ReturnReactionEvent{.reaction_saver = reaction_saver.get(),
                                    .reaction_index = 0})
      .WillOnce(Return(false));
  return reaction_saver;
}

TEST(SessionMockTest, RunWatchersInvokesCallbackExpectedNumberOfTimes) {
  auto action_stream_mock = std::make_unique<
      MockClientReaderWriter<intrinsic_proto::icon::v1::OpenSessionRequest,
                             intrinsic_proto::icon::v1::OpenSessionResponse>>();
  auto watcher_stream_mock = std::make_unique<
      MockClientReader<intrinsic_proto::icon::v1::WatchReactionsResponse>>();

  constexpr ActionInstanceId kActionId(1);
  Comparison condition1("var1", Comparison::BooleanOperator::kEqual, true);
  Comparison condition2("var2", Comparison::BooleanOperator::kEqual, true);

  ReactionSaver reaction_saver;
  // Mock action sequence for session, as an expected series of in order
  // calls. Expect no sequence of watcher stream calls w.r.t. action stream
  // calls.
  // Watcher read calls will block until an action is sent.
  {
    InSequence s;
    MockCallSetup(action_stream_mock.get(), absl::OkStatus());
    // Get the ReactionIds we need to create the reaction events below.
    EXPECT_CALL(*action_stream_mock,
                Write(Truly(&HasAddActionsAndReactions), _))
        .WillOnce(Invoke(&reaction_saver, &ReactionSaver::SaveReactions));
    EXPECT_CALL(*action_stream_mock, Read)
        .WillOnce(DoAll(WithArg<0>(write_action_response(absl::OkStatus())),
                        Return(true)));

    MockEndActionCall(action_stream_mock.get(), absl::OkStatus());
  }

  // Read() is called in a separate thread, so regular After() expectations
  // don't work. Instead, rely on the Notification from `reaction_saver`.
  // Setup reactions to be returned when polled, which match
  // our expected number of reaction callback invocations.
  EXPECT_CALL(*watcher_stream_mock, Read)
      .WillOnce(Return(true))
      // Two calls of the callback for condition1 (the second is below)
      .WillOnce(ReturnReactionEventForCondition{
          .reaction_saver = &reaction_saver, .condition = condition1})
      // One for condition2
      .WillOnce(ReturnReactionEventForCondition{
          .reaction_saver = &reaction_saver, .condition = condition2})
      // One call with an unknown ReactionId
      .WillOnce(
          [&reaction_saver](
              intrinsic_proto::icon::v1::WatchReactionsResponse* response) {
            ReactionId::ValueType max_reaction_id = 0;
            for (const auto& reaction : reaction_saver.WaitForReactions()) {
              max_reaction_id =
                  std::max(reaction.reaction_instance_id(), max_reaction_id);
            }
            *response = GetReaction(ReactionId(max_reaction_id + 1));
            return true;
          })
      .WillOnce(ReturnReactionEventForCondition{
          .reaction_saver = &reaction_saver, .condition = condition1})
      .WillOnce(Return(false));

  auto stub_mock =
      std::make_unique<intrinsic_proto::icon::v1::MockIconApiStub>();
  MockStubSetup(std::move(action_stream_mock), std::move(watcher_stream_mock),
                stub_mock.get());

  ASSERT_OK_AND_ASSIGN(auto session,
                       Session::Start(std::move(stub_mock), {kPartName1}));

  int num_calls_reaction_1 = 0;
  const int kNumExpectedCallsReaction1 = 2;
  int num_calls_reaction_2 = 0;
  constexpr int kNumExpectedCallsReaction2 = 1;
  EXPECT_OK(session->AddAction(
      ActionDescriptor("action_type_name", kActionId, kPartName1)
          .WithReaction(
              ReactionDescriptor(condition1)
                  .WithWatcherOnCondition([&session, &num_calls_reaction_1]() {
                    ++num_calls_reaction_1;
                    if (num_calls_reaction_1 == kNumExpectedCallsReaction1) {
                      session->QuitWatcherLoop();
                    }
                  }))
          .WithReaction(ReactionDescriptor(condition2)
                            .WithWatcherOnCondition([&num_calls_reaction_2]() {
                              ++num_calls_reaction_2;
                            }))));

  EXPECT_OK(session->RunWatcherLoop());
  EXPECT_EQ(num_calls_reaction_1, kNumExpectedCallsReaction1);
  EXPECT_EQ(num_calls_reaction_2, kNumExpectedCallsReaction2);
}

TEST(SessionMockTest, RunWatchersAbortsSessionIfCallDies) {
  auto action_stream_mock = std::make_unique<
      MockClientReaderWriter<intrinsic_proto::icon::v1::OpenSessionRequest,
                             intrinsic_proto::icon::v1::OpenSessionResponse>>();
  auto watcher_stream_mock = std::make_unique<
      MockClientReader<intrinsic_proto::icon::v1::WatchReactionsResponse>>();

  // Mock action sequence for session, as an expected series of in order calls.
  // Expect no sequence of watcher stream calls w.r.t. action stream calls.
  {
    InSequence s;
    MockCallSetup(action_stream_mock.get(), absl::OkStatus());
    MockActionResponseWithStatus(action_stream_mock.get(), absl::OkStatus());
    MockEndActionCall(action_stream_mock.get(), absl::OkStatus());
  }

  EXPECT_CALL(*watcher_stream_mock, Read)
      .WillOnce(Return(true))
      .WillOnce(Return(false));

  auto stub_mock =
      std::make_unique<intrinsic_proto::icon::v1::MockIconApiStub>();
  MockStubSetup(std::move(action_stream_mock), std::move(watcher_stream_mock),
                stub_mock.get());

  ASSERT_OK_AND_ASSIGN(auto session,
                       Session::Start(std::move(stub_mock), {kPartName1}));

  constexpr ActionInstanceId kActionId(1);
  Comparison condition("var", Comparison::BooleanOperator::kEqual, true);

  EXPECT_OK(session->AddAction(
      ActionDescriptor("action_type_name", kActionId, kPartName1)
          .WithReaction(
              ReactionDescriptor(condition).WithWatcherOnCondition([]() {}))));

  EXPECT_THAT(session->RunWatcherLoop(), StatusIs(absl::StatusCode::kAborted));
  EXPECT_THAT(session->End(), StatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST(SessionMockTest, RunWatchersCallbackEndsSession) {
  auto action_stream_mock = std::make_unique<
      MockClientReaderWriter<intrinsic_proto::icon::v1::OpenSessionRequest,
                             intrinsic_proto::icon::v1::OpenSessionResponse>>();
  auto watcher_stream_mock = std::make_unique<
      MockClientReader<intrinsic_proto::icon::v1::WatchReactionsResponse>>();

  // Mock action sequence for session, as an expected series of in order calls.
  // Expect no sequence of watcher stream calls w.r.t. action stream calls.
  std::unique_ptr<ReactionSaver> reaction_saver =
      SetupSingleReactionExpectations(action_stream_mock.get(),
                                      watcher_stream_mock.get());

  auto stub_mock =
      std::make_unique<intrinsic_proto::icon::v1::MockIconApiStub>();
  MockStubSetup(std::move(action_stream_mock), std::move(watcher_stream_mock),
                stub_mock.get());

  ASSERT_OK_AND_ASSIGN(auto session,
                       Session::Start(std::move(stub_mock), {kPartName1}));

  constexpr ActionInstanceId kActionId(1);
  Comparison condition1("var1", Comparison::BooleanOperator::kEqual, true);
  EXPECT_OK(session->AddAction(
      ActionDescriptor("action_type_name", kActionId, kPartName1)
          .WithReaction(ReactionDescriptor(condition1)
                            .WithWatcherOnCondition([&session]() {
                              EXPECT_THAT(session->End(), IsOk());
                            }))));

  EXPECT_OK(session->RunWatcherLoop());
  EXPECT_THAT(session->End(), StatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST(SessionMockTest, RunWatchersCleanup) {
  auto action_stream_mock = std::make_unique<
      MockClientReaderWriter<intrinsic_proto::icon::v1::OpenSessionRequest,
                             intrinsic_proto::icon::v1::OpenSessionResponse>>();
  auto watcher_stream_mock = std::make_unique<
      MockClientReader<intrinsic_proto::icon::v1::WatchReactionsResponse>>();

  // Mock action sequence for session, as an expected series of in order calls.
  // Expect no sequence of watcher stream calls w.r.t. action stream calls.
  {
    InSequence s;
    MockCallSetup(action_stream_mock.get(), absl::OkStatus());
    MockEndActionCall(action_stream_mock.get(), absl::OkStatus());
  }

  // Use this Notification to wait until the reaction queue has been filled
  // before ending the session.
  absl::Notification queue_filled;
  // Setup reactions to be returned when polled, to fill the reaction queue.
  intrinsic_proto::icon::v1::WatchReactionsResponse reaction_1;
  constexpr ReactionId kReactionId1(1);
  reaction_1.mutable_reaction_event()->set_reaction_id(kReactionId1.value());
  EXPECT_CALL(*watcher_stream_mock, Read)
      .WillOnce(Return(true))
      .WillOnce(DoAll(SetArgPointee<0>(reaction_1), Return(true)))
      .WillOnce(
          [&queue_filled](
              intrinsic_proto::icon::v1::WatchReactionsResponse* /*unused*/) {
            queue_filled.Notify();
            return false;
          });

  auto stub_mock =
      std::make_unique<intrinsic_proto::icon::v1::MockIconApiStub>();
  MockStubSetup(std::move(action_stream_mock), std::move(watcher_stream_mock),
                stub_mock.get());

  ASSERT_OK_AND_ASSIGN(auto session,
                       Session::Start(std::move(stub_mock), {kPartName1}));

  // Wait until we know the Session has a reaction event in its queue.
  queue_filled.WaitForNotification();
  // End the session without running the watcher loop. This should ignore any
  // leftover events.
  ASSERT_OK(session->End());
}

TEST(SessionChannelFakeTest, RunWatchersReentrant) {
  constexpr ActionInstanceId kActionId0 = ActionInstanceId(0);
  constexpr ActionInstanceId kActionId1 = ActionInstanceId(1);

  ASSERT_OK_AND_ASSIGN(
      auto channel,
      RtclControllerChannelFake::Builder()
          .OnNextSession(
              SessionWill()
                  .OnAction(kActionId0, ActionWill().ReportStateVariables(
                                            {{kIsDone, true}}))
                  .OnAction(kActionId1, ActionWill().ReportStateVariables(
                                            {{kIsDone, true}})))
          .Build(DefaultPartConfigs()));

  ASSERT_OK(Client(channel).Enable());
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Session> session,
                       Session::Start(channel, {"robot_arm"}));

  ASSERT_OK_AND_ASSIGN(
      auto actions,
      session->AddActions({
          ActionDescriptor(PointToPointMoveInfo::kActionTypeName, kActionId0,
                           "robot_arm")
              .WithFixedParams(GetValidPointToPointMoveParams())
              .WithReaction(ReactionDescriptor(IsDone()).WithWatcherOnCondition(
                  [&session]() { session->QuitWatcherLoop(); })),
          ActionDescriptor(PointToPointMoveInfo::kActionTypeName, kActionId1,
                           "robot_arm")
              .WithFixedParams(GetValidPointToPointMoveParams())
              .WithReaction(ReactionDescriptor(IsDone()).WithWatcherOnCondition(
                  [&session]() { session->QuitWatcherLoop(); })),
      }));

  // Ensure we can call RunWatcherLoop() more than once.
  EXPECT_OK(session->StartAction(actions.front()));
  EXPECT_OK(session->RunWatcherLoop());
  EXPECT_OK(session->ResetWatcherLoopState());
  EXPECT_OK(session->StartAction(actions.back()));
  EXPECT_OK(session->RunWatcherLoop());
}

TEST(SessionChannelFakeTest, CannotRunWatcherLoopTwiceWithoutReset) {
  ASSERT_OK_AND_ASSIGN(auto channel, RtclControllerChannelFake::Builder().Build(
                                         DefaultPartConfigs()));

  ASSERT_OK(Client(channel).Enable());
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Session> session,
                       Session::Start(channel, {"robot_arm"}));

  absl::Notification watcher_loop_finished;
  intrinsic::Thread watcher_thread([&]() {
    // This will block until QuitWatcherLoop() is called or deadline expires.
    session->RunWatcherLoop(absl::Now() + absl::Seconds(10)).IgnoreError();
    watcher_loop_finished.Notify();
  });

  EXPECT_OK(session->QuitWatcherLoop(absl::Seconds(1)));
  EXPECT_TRUE(
      watcher_loop_finished.WaitForNotificationWithTimeout(absl::Seconds(10)));
  // Calling RunWatcherLoop again should fail.
  EXPECT_THAT(session->RunWatcherLoop(),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       HasSubstr("Watcher loop is already finished")));
}

TEST(SessionChannelFakeTest, CannotResetWatcherLoopWhileRunning) {
  ASSERT_OK_AND_ASSIGN(auto channel, RtclControllerChannelFake::Builder().Build(
                                         DefaultPartConfigs()));

  ASSERT_OK(Client(channel).Enable());
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Session> session,
                       Session::Start(channel, {"robot_arm"}));

  intrinsic::Thread watcher_thread([&]() {
    // This will block until QuitWatcherLoop() is called or deadline expires.
    session->RunWatcherLoop(absl::Now() + absl::Seconds(10)).IgnoreError();
  });

  // We do not want to depend here on WaitForWatcherLoopStart(), which is
  // difficult to test. Therefore, we wait a bit. That should be reliable.
  absl::SleepFor(absl::Seconds(1));
  // Calling ResetWatcherLoopState while the loop is running shall fail.
  EXPECT_THAT(
      session->ResetWatcherLoopState(),
      StatusIs(
          absl::StatusCode::kFailedPrecondition,
          HasSubstr("ResetWatcherLoopState() called while RunWatcherLoop()")));
}

TEST(SessionChannelFakeTest, CannotResetWatcherLoopWhileWaitingForStart) {
  ASSERT_OK_AND_ASSIGN(auto channel, RtclControllerChannelFake::Builder().Build(
                                         DefaultPartConfigs()));

  ASSERT_OK(Client(channel).Enable());
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Session> session,
                       Session::Start(channel, {"robot_arm"}));

  intrinsic::Thread watcher_thread([&]() {
    // This will block until the test ends or timeout is reached.
    session->WaitForWatcherLoopStart(absl::Seconds(3)).IgnoreError();
  });

  // We cannot to depend here on WaitForWatcherLoopStart(), since we are using
  // it in the other thread. Therefore, we wait a bit. That should be reliable.
  absl::SleepFor(absl::Seconds(1));
  // Calling ResetWatcherLoopState while the loop is running shall fail.
  EXPECT_THAT(
      session->ResetWatcherLoopState(),
      StatusIs(
          absl::StatusCode::kFailedPrecondition,
          HasSubstr("ResetWatcherLoopState() called while RunWatcherLoop()")));
}

TEST(SessionChannelFakeTest, WaitForLoopStartReturnsEarlyOnSessionEnd) {
  ASSERT_OK_AND_ASSIGN(auto channel, RtclControllerChannelFake::Builder().Build(
                                         DefaultPartConfigs()));
  const auto timeout = absl::Seconds(10);
  ASSERT_OK(Client(channel).Enable());
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Session> session,
                       Session::Start(channel, {"robot_arm"}));
  const auto start = absl::Now();
  intrinsic::Thread watcher_thread(
      [&]() { session->WaitForWatcherLoopStart(timeout).IgnoreError(); });
  // We cannot to depend here on WaitForWatcherLoopStart(), since we are using
  // it in the other thread. Therefore, we wait a bit. That should be reliable.
  absl::SleepFor(absl::Seconds(1));
  EXPECT_OK(session->End());
  // If early function return would fail, the duration should be larger than the
  // configured timeout. Therefore, we check that it is smaller.
  const auto duration = absl::Now() - start;
  EXPECT_LT(duration, timeout);
}

TEST(SessionChannelFakeTest, RunWatcherLoopWhileAlreadyRunningFails) {
  ASSERT_OK_AND_ASSIGN(auto channel, RtclControllerChannelFake::Builder().Build(
                                         DefaultPartConfigs()));

  ASSERT_OK(Client(channel).Enable());
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Session> session,
                       Session::Start(channel, {"robot_arm"}));

  absl::Notification watcher_loop_thread_running;
  intrinsic::Thread watcher_thread([&]() {
    watcher_loop_thread_running.Notify();
    // This will block until QuitWatcherLoop() is called or deadline expires.
    session->RunWatcherLoop(absl::InfiniteFuture()).IgnoreError();
  });

  watcher_loop_thread_running.WaitForNotification();
  // We do not want to depend here on WaitForWatcherLoopStart(), which is
  // difficult to test.  Therefore, we wait a bit. That should be reliable.
  absl::SleepFor(absl::Seconds(1));
  // The watcher loop is now running and blocked in AwaitWithDeadline.
  // Calling RunWatcherLoop again should fail.
  EXPECT_THAT(session->RunWatcherLoop(),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       HasSubstr("Watcher loop is already running")));

  session->QuitWatcherLoop();
}

TEST(SessionChannelFakeTest, RunWatcherLoopTwiceInALoop) {
  ASSERT_OK_AND_ASSIGN(auto channel, RtclControllerChannelFake::Builder().Build(
                                         DefaultPartConfigs()));
  // Test that it works reliably to run a watcher loop twice with random delays
  // in the 2 threads.
  ASSERT_OK(Client(channel).Enable());
  for (int i = 0; i < 100; i++) {
    ASSERT_OK_AND_ASSIGN(std::unique_ptr<Session> session,
                         Session::Start(channel, {"robot_arm"}));
    absl::Notification watcher_loop_quitter_thread_running;
    absl::Notification watcher_loop_quit_requested;

    intrinsic::Thread watcher_thread([&]() {
      // Notification to signal when the thread is started since launching a
      // thread can take quite some time. Using this, the following random sleep
      // can be shorter while still having it random which thread wakes up
      // first.
      watcher_loop_quitter_thread_running.Notify();
      absl::SleepFor(absl::Milliseconds(rand() % 100));
      EXPECT_OK(session->QuitWatcherLoop(absl::Seconds(1)));
      watcher_loop_quit_requested.Notify();
    });

    watcher_loop_quitter_thread_running.WaitForNotification();

    absl::SleepFor(absl::Milliseconds(rand() % 100));
    EXPECT_OK(session->RunWatcherLoop());
    watcher_loop_quit_requested.WaitForNotification();
    EXPECT_OK(session->ResetWatcherLoopState());
    {
      absl::Notification watcher_loop_quitter_thread_running;
      intrinsic::Thread watcher_thread([&]() {
        // Notification to signal when the thread is started since launching a
        // thread can take quite some time. Using this, the following random
        // sleep can be shorter while still having it random which thread wakes
        // up first.
        watcher_loop_quitter_thread_running.Notify();
        absl::SleepFor(absl::Milliseconds(rand() % 100));
        EXPECT_OK(session->QuitWatcherLoop(absl::Seconds(1)));
      });
      watcher_loop_quitter_thread_running.WaitForNotification();
      absl::SleepFor(absl::Milliseconds(rand() % 100));
      EXPECT_OK(session->RunWatcherLoop());
    }
  }
}

TEST(SessionChannelFakeTest, QuitWatcherLoopWithDeadline) {
  ASSERT_OK_AND_ASSIGN(auto channel, RtclControllerChannelFake::Builder().Build(
                                         DefaultPartConfigs()));

  ASSERT_OK(Client(channel).Enable());
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Session> session,
                       Session::Start(channel, {"robot_arm"}));

  // 1. Times out if loop not started.
  EXPECT_THAT(session->QuitWatcherLoop(absl::Now() + absl::Milliseconds(10)),
              StatusIs(absl::StatusCode::kDeadlineExceeded));

  // 2. Succeeds if loop is running.
  intrinsic::Thread watcher_thread(
      [&]() { EXPECT_OK(session->RunWatcherLoop()); });
  EXPECT_OK(session->QuitWatcherLoop(absl::Now() + absl::Seconds(1)));
  watcher_thread.join();

  // 3. Fails if loop has finished.
  EXPECT_THAT(session->QuitWatcherLoop(absl::Now() + absl::Milliseconds(10)),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       HasSubstr("Watcher loop is already finished")));
}

TEST(SessionChannelFakeTest, WaitForWatcherLoopStartWithDeadline) {
  ASSERT_OK_AND_ASSIGN(auto channel, RtclControllerChannelFake::Builder().Build(
                                         DefaultPartConfigs()));

  ASSERT_OK(Client(channel).Enable());
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Session> session,
                       Session::Start(channel, {"robot_arm"}));

  // 1. Times out if loop not started.
  EXPECT_THAT(
      session->WaitForWatcherLoopStart(absl::Now() + absl::Milliseconds(10)),
      StatusIs(absl::StatusCode::kDeadlineExceeded));

  // 2. Succeeds if loop is running.
  intrinsic::Thread watcher_thread([&]() {
    // This will block until QuitWatcherLoop() is called or deadline expires.
    session->RunWatcherLoop(absl::InfiniteFuture()).IgnoreError();
  });

  EXPECT_OK(session->WaitForWatcherLoopStart(absl::Now() + absl::Seconds(1)));

  session->QuitWatcherLoop();
}

TEST(SessionMockTest, RunWatchersUntilReactionIdQuitsWhenReactionOccurs) {
  auto action_stream_mock = std::make_unique<
      MockClientReaderWriter<intrinsic_proto::icon::v1::OpenSessionRequest,
                             intrinsic_proto::icon::v1::OpenSessionResponse>>();
  auto watcher_stream_mock = std::make_unique<
      MockClientReader<intrinsic_proto::icon::v1::WatchReactionsResponse>>();

  // Mock action sequence for session, as an expected series of in order calls.
  // Expect no sequence of watcher stream calls w.r.t. action stream calls.
  // Watcher read calls will block until an action is sent.
  std::unique_ptr<ReactionSaver> reaction_saver =
      SetupSingleReactionExpectations(action_stream_mock.get(),
                                      watcher_stream_mock.get());

  auto stub_mock =
      std::make_unique<intrinsic_proto::icon::v1::MockIconApiStub>();
  MockStubSetup(std::move(action_stream_mock), std::move(watcher_stream_mock),
                stub_mock.get());

  ASSERT_OK_AND_ASSIGN(auto session,
                       Session::Start(std::move(stub_mock), {kPartName1}));

  constexpr ActionInstanceId kActionId(1);
  Comparison condition("var", Comparison::BooleanOperator::kEqual, true);

  constexpr ReactionHandle kReactionHandle(1);
  EXPECT_OK(session->AddAction(
      ActionDescriptor("action_type_name", kActionId, kPartName1)
          .WithReaction(
              ReactionDescriptor(condition).WithHandle(kReactionHandle))));

  EXPECT_OK(session->RunWatcherLoopUntilReaction(kReactionHandle));
}

TEST(SessionMockTest,
     RunWatchersUntilReactionIdRunsExistingCallbackBeforeQuitting) {
  auto action_stream_mock = std::make_unique<
      MockClientReaderWriter<intrinsic_proto::icon::v1::OpenSessionRequest,
                             intrinsic_proto::icon::v1::OpenSessionResponse>>();
  auto watcher_stream_mock = std::make_unique<
      MockClientReader<intrinsic_proto::icon::v1::WatchReactionsResponse>>();

  // Mock action sequence for session, as an expected series of in order calls.
  // Expect no sequence of watcher stream calls w.r.t. action stream calls.
  // Watcher read calls will block until an action is sent.
  std::unique_ptr<ReactionSaver> reaction_saver =
      SetupSingleReactionExpectations(action_stream_mock.get(),
                                      watcher_stream_mock.get());

  auto stub_mock =
      std::make_unique<intrinsic_proto::icon::v1::MockIconApiStub>();
  MockStubSetup(std::move(action_stream_mock), std::move(watcher_stream_mock),
                stub_mock.get());

  ASSERT_OK_AND_ASSIGN(auto session,
                       Session::Start(std::move(stub_mock), {kPartName1}));

  constexpr ActionInstanceId kActionId(1);
  Comparison condition("var", Comparison::BooleanOperator::kEqual, true);

  constexpr ReactionHandle kReactionHandle(1);
  int num_calls = 0;
  EXPECT_OK(session->AddAction(
      ActionDescriptor("action_type_name", kActionId, kPartName1)
          .WithReaction(
              ReactionDescriptor(condition)
                  .WithHandle(kReactionHandle)
                  .WithWatcherOnCondition([&num_calls]() { num_calls++; }))));

  EXPECT_OK(session->RunWatcherLoopUntilReaction(kReactionHandle));
  EXPECT_EQ(num_calls, 1);
}

// TODO(b/244528889): Enable once expectations are powerful enough to cleanly
//   model this use case.
TEST(ChannelFakeTest, DISABLED_ReusedActionDoesNotQuitWatcherLoopOnReaction) {
  constexpr ReactionHandle kReaction0Done = ReactionHandle(0);
  constexpr ReactionHandle kReaction1Done = ReactionHandle(1);
  constexpr ActionInstanceId kActionId0 = ActionInstanceId(0);
  constexpr ActionInstanceId kActionId1 = ActionInstanceId(1);

  ASSERT_OK_AND_ASSIGN(
      auto channel,
      RtclControllerChannelFake::Builder()
          .OnNextSession(
              SessionWill()
                  .OnAction(kActionId0, ActionWill().ReportStateVariables(
                                            {{kIsDone, true}}))
                  .OnAction(kActionId0, ActionWill().ReportStateVariables(
                                            {{kIsDone, true}}))
                  .OnAction(kActionId1, ActionWill().ReportStateVariables(
                                            {{kIsDone, true}})))
          .Build(DefaultPartConfigs()));

  std::unique_ptr<Session> session =
      Session::Start(channel, {test_part_names::kArmName}).value();

  int action1_called = 0;
  ASSERT_OK_AND_ASSIGN(
      Action action1,
      session->AddAction(
          ActionDescriptor(PointToPointMoveInfo::kActionTypeName, kActionId1,
                           test_part_names::kArmName)
              .WithFixedParams(GetValidPointToPointMoveParams())
              .WithReaction(ReactionDescriptor(IsDone())
                                .WithHandle(kReaction1Done)
                                .WithWatcherOnCondition([&action1_called]() {
                                  action1_called++;
                                }))));

  int called = 0;
  ASSERT_OK_AND_ASSIGN(
      Action action0,
      session->AddAction(
          ActionDescriptor(PointToPointMoveInfo::kActionTypeName, kActionId0,
                           test_part_names::kArmName)
              .WithFixedParams(GetValidPointToPointMoveParams())
              .WithReaction(
                  ReactionDescriptor(IsDone())
                      .WithHandle(kReaction0Done)
                      .WithWatcherOnCondition([&called, &session, &action1]() {
                        called++;
                        if (called == 2) {
                          ASSERT_OK(session->StartAction(action1));
                        }
                      }))));

  EXPECT_OK(session->StartAction(action0));
  EXPECT_OK(session->RunWatcherLoopUntilReaction(kReaction0Done));
  EXPECT_OK(session->StartAction(action0));
  EXPECT_OK(session->RunWatcherLoopUntilReaction(kReaction1Done));
  EXPECT_EQ(action1_called, 1);
}

TEST(SessionMockTest, ClientSendsContext) {
  auto action_stream_mock = std::make_unique<
      MockClientReaderWriter<intrinsic_proto::icon::v1::OpenSessionRequest,
                             intrinsic_proto::icon::v1::OpenSessionResponse>>();
  auto watcher_stream_mock = std::make_unique<
      MockClientReader<intrinsic_proto::icon::v1::WatchReactionsResponse>>();

  intrinsic_proto::data_logger::Context expected_context =
      ParseTextProtoOrDie(R"pb(
        deployment_id: "asdf"
        executive_session_id: 12
        executive_plan_id: 34
        executive_plan_action_id: 56
        skill_id: 78
      )pb");
  {
    InSequence s;
    intrinsic_proto::icon::v1::OpenSessionRequest expected_open_session_request;
    expected_open_session_request.mutable_initial_session_data()
        ->mutable_allocate_parts()
        ->add_part(kPartName1);
    expected_open_session_request.mutable_log_context()->MergeFrom(
        expected_context);

    EXPECT_CALL(*action_stream_mock,
                Write(EqualsProto(expected_open_session_request), _))
        .WillOnce(Return(true));
    EXPECT_CALL(*action_stream_mock, Read)
        .WillOnce([](intrinsic_proto::icon::v1::OpenSessionResponse* response) {
          response->mutable_initial_session_data()->set_session_id(123);
          return true;
        });

    MockEndActionCall(action_stream_mock.get(), absl::OkStatus());
  }

  EXPECT_CALL(*watcher_stream_mock, Read)
      .WillOnce(Return(true))
      .WillRepeatedly(Return(false));

  auto stub_mock =
      std::make_unique<intrinsic_proto::icon::v1::MockIconApiStub>();
  MockStubSetup(std::move(action_stream_mock), std::move(watcher_stream_mock),
                stub_mock.get());

  ASSERT_OK_AND_ASSIGN(
      auto session,
      Session::Start(std::move(stub_mock), {kPartName1},
                     DefaultClientContextFactory, expected_context));

  EXPECT_OK(session->End());
}

TEST(ActionDescriptorTest, GetId) {
  ActionDescriptor action_descriptor("my_action", ActionInstanceId(1),
                                     "my_part");
  EXPECT_EQ(action_descriptor.Id().value(), 1);
}

TEST(ActionDescriptorTest, GetTypeName) {
  ActionDescriptor action_descriptor("my_action", ActionInstanceId(1),
                                     "my_part");
  EXPECT_EQ(action_descriptor.ActionTypeName(), "my_action");
}

TEST(SessionTest, ThreadSafeAddActionQuitWatcherLoop) {
  constexpr ActionInstanceId kActionId1(1);
  constexpr ActionInstanceId kActionId2(2);
  ASSERT_OK_AND_ASSIGN(auto channel, RtclControllerChannelFake::Builder().Build(
                                         DefaultPartConfigs()));
  ASSERT_OK_AND_ASSIGN(auto session,
                       Session::Start(channel, {test_part_names::kArmName}));
  {
    intrinsic::Thread clear_all_thread([&]() {
      // EXPECT_OK macro needs a new line.
      EXPECT_OK(session->ClearAllActionsAndReactions());
    });
    EXPECT_OK(session->ClearAllActionsAndReactions());
  }
  intrinsic::Thread add_action_thread([&]() {
    EXPECT_OK(session->AddAction(
        ActionDescriptor(PointToPointMoveInfo::kActionTypeName, kActionId2,
                         test_part_names::kArmName)
            .WithFixedParams(GetValidPointToPointMoveParams())));
  });
  ASSERT_OK_AND_ASSIGN(
      Action action,
      session->AddAction(
          ActionDescriptor(PointToPointMoveInfo::kActionTypeName, kActionId1,
                           test_part_names::kArmName)
              .WithFixedParams(GetValidPointToPointMoveParams())));
  EXPECT_OK(session->StartAction(action));
  intrinsic::Thread quit_watcher_loop_thread([&]() {
    absl::SleepFor(absl::Seconds(1));
    session->QuitWatcherLoop();
  });
  EXPECT_OK(session->RunWatcherLoop());
}

TEST(SessionTest, ThreadSafeEnd) {
  constexpr ActionInstanceId kActionId1(1);
  ASSERT_OK_AND_ASSIGN(auto channel, RtclControllerChannelFake::Builder().Build(
                                         DefaultPartConfigs()));
  ASSERT_OK_AND_ASSIGN(auto session,
                       Session::Start(channel, {test_part_names::kArmName}));
  ASSERT_OK_AND_ASSIGN(
      Action action,
      session->AddAction(
          ActionDescriptor(PointToPointMoveInfo::kActionTypeName, kActionId1,
                           test_part_names::kArmName)
              .WithFixedParams(GetValidPointToPointMoveParams())));
  EXPECT_OK(session->StartAction(action));
  intrinsic::Thread quit_watcher_loop_thread([&]() {
    absl::SleepFor(absl::Seconds(1));
    (void)session->End();
  });
  EXPECT_OK(session->End());
}

TEST(SessionTest, ThreadSafeRunWatcherLoopUntilReaction) {
  constexpr ActionInstanceId kActionId1(1);
  constexpr ReactionHandle kReaction1Done = ReactionHandle(1);
  ASSERT_OK_AND_ASSIGN(
      auto channel,
      RtclControllerChannelFake::Builder()
          .OnNextSession(SessionWill().OnAction(
              kActionId1, ActionWill().ReportStateVariables({{kIsDone, true}})))
          .Build(DefaultPartConfigs()));
  ASSERT_OK_AND_ASSIGN(auto session,
                       Session::Start(channel, {test_part_names::kArmName}));
  absl::Notification done;
  ASSERT_OK_AND_ASSIGN(
      Action action,
      session->AddAction(ActionDescriptor(PointToPointMoveInfo::kActionTypeName,
                                          kActionId1, test_part_names::kArmName)
                             .WithFixedParams(GetValidPointToPointMoveParams())
                             .WithReaction(ReactionDescriptor(IsDone())
                                               .WithHandle(kReaction1Done)
                                               .WithWatcherOnCondition([&]() {
                                                 done.Notify();
                                                 session->QuitWatcherLoop();
                                               }))));
  EXPECT_OK(session->StartAction(action));
  intrinsic::Thread quit_watcher_loop_thread([&]() {
    absl::SleepFor(absl::Seconds(1));
    session->QuitWatcherLoop();
  });
  EXPECT_OK(session->RunWatcherLoopUntilReaction(kReaction1Done));
  EXPECT_TRUE(done.HasBeenNotified());
}

class SessionWithExtendedStatusTest : public TestWithStatusSpec {};

TEST_F(SessionWithExtendedStatusTest,
       ForwardsServerSidedErrorWithExtendedStatus) {
  constexpr ActionInstanceId kActionId1(1);
  ASSERT_OK_AND_ASSIGN(
      auto channel,
      RtclControllerChannelFake::Builder()
          .OnNextSession(SessionWill().OnAction(
              kActionId1,
              ActionWill().SetOperationalStatus(ParseTextProtoOrDie(R"pb(
                operational_status: { state: FAULTED fault_reason: "my_fault" }
              )pb"))))
          .Build(DefaultPartConfigs()));
  ASSERT_OK_AND_ASSIGN(auto session,
                       Session::Start(channel, {test_part_names::kArmName}));
  ASSERT_OK_AND_ASSIGN(
      Action action,
      session->AddAction(
          ActionDescriptor(PointToPointMoveInfo::kActionTypeName, kActionId1,
                           test_part_names::kArmName)
              .WithFixedParams(GetValidPointToPointMoveParams())));
  EXPECT_OK(session->StartAction(action));
  absl::Status status = session->RunWatcherLoop();
  EXPECT_THAT(status,
              StatusIs(absl::StatusCode::kAborted, HasSubstr("my_fault")));
  EXPECT_OK(session->ResetWatcherLoopState());
  auto extended_status = GetExtendedStatus(status);
  ASSERT_TRUE(extended_status.has_value());
  EXPECT_THAT(extended_status->user_report().message(),
              AllOf(SizeIs(Gt(20)), HasSubstr("my_fault")));
  EXPECT_THAT(session->StartAction(action),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       HasSubstr("Session has already ended")))
      << "Expect that session cannot be reused after server-side error.";
  EXPECT_THAT(session->RunWatcherLoop(), StatusIs(absl::StatusCode::kAborted));
}

}  // namespace
}  // namespace icon
}  // namespace intrinsic
