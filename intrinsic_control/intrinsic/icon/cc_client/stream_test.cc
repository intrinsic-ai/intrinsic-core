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

#include "intrinsic/icon/cc_client/stream.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/container/flat_hash_set.h"
#include "absl/functional/bind_front.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/any.pb.h"
#include "google/protobuf/wrappers.pb.h"
#include "google/rpc/status.pb.h"
#include "grpcpp/client_context.h"
#include "grpcpp/impl/call_op_set.h"
#include "grpcpp/support/status.h"
#include "grpcpp/support/sync_stream.h"
#include "internal/testing.h"
#include "intrinsic/icon/cc_client/client.h"
#include "intrinsic/icon/cc_client/testing/fake_action.pb.h"
#include "intrinsic/icon/common/id_types.h"
#include "intrinsic/icon/proto/v1/service.grpc.pb.h"
#include "intrinsic/icon/proto/v1/service.pb.h"
#include "intrinsic/icon/proto/v1/service_mock.grpc.pb.h"
#include "intrinsic/icon/proto/v1/types.pb.h"
#include "intrinsic/icon/server/example/noop_action.h"
#include "intrinsic/icon/server/fake_robot_connection.h"
#include "intrinsic/icon/server/session_interface.h"
#include "intrinsic/icon/server/testing/in_process_server.h"
#include "intrinsic/icon/server/testing/session_mock.h"
#include "intrinsic/util/status/status_conversion_rpc.h"
#include "protobuf-matchers/protocol-buffer-matchers.h"

static constexpr char kPartName1[] = "intrinsic.icon.RobotArm";
static constexpr char kPartName2[] = "intrinsic.icon.RobotGripper";
static constexpr char kServerName[] = "icon_server";
constexpr double kControlFrequencyHz = 1000;

using ::absl_testing::StatusIs;
using ::testing::Return;
using ::testing::StrictMock;
using ::testing::Test;

intrinsic_proto::icon::v1::ServerConfig ServerConfig() {
  intrinsic_proto::icon::v1::ServerConfig server_config;
  server_config.set_name(kServerName);
  server_config.set_frequency_hz(kControlFrequencyHz);
  return server_config;
}

namespace {
// TODO(b/150959510): Resolve grpc dependencies so that we can use
// "third_party/grpc/include/grpcpp/test/mock_stream.h" instead of this.
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
  MOCK_METHOD(bool, Write, (const W&, const ::grpc::WriteOptions));

  // ClientReaderWriterInterface
  MOCK_METHOD(void, WaitForInitialMetadata, ());
  MOCK_METHOD(bool, WritesDone, ());
};
}  // namespace

namespace intrinsic {
namespace icon {
namespace {

using ::absl_testing::StatusIs;
using ::protobuf_matchers::EquivToProto;
using ::testing::_;
using ::testing::DoAll;
using ::testing::Return;
using ::testing::SetArgPointee;

TEST(StreamWriter, OpenCloseSucceeds) {
  auto stream_mock = std::make_unique<MockClientReaderWriter<
      intrinsic_proto::icon::v1::OpenWriteStreamRequest,
      intrinsic_proto::icon::v1::OpenWriteStreamResponse>>();

  {
    ::testing::InSequence s;
    intrinsic_proto::icon::v1::OpenWriteStreamRequest expected_req;
    expected_req.set_session_id(223344);
    expected_req.mutable_add_write_stream()->set_action_id(123);
    expected_req.mutable_add_write_stream()->set_field_name("the_field_name");
    EXPECT_CALL(*stream_mock, Write(EquivToProto(expected_req), _))
        .WillOnce(Return(true));

    intrinsic_proto::icon::v1::OpenWriteStreamResponse resp;
    *resp.mutable_add_stream_response()->mutable_status() =
        ToGoogleRpcStatus(absl::OkStatus());
    EXPECT_CALL(*stream_mock, Read(_))
        .WillOnce(DoAll(SetArgPointee<0>(resp), Return(true)));

    // On close, read until it returns false. (So we expect an arbitrary number
    // of calls to read that return true (In this case, 17), followed by a
    // single false-returning call.
    EXPECT_CALL(*stream_mock, Read(_)).Times(17).WillRepeatedly(Return(true));
    EXPECT_CALL(*stream_mock, Read(_)).WillOnce(Return(false));
  }

  auto mock_stub =
      std::make_unique<intrinsic_proto::icon::v1::MockIconApiStub>();
  EXPECT_CALL(*mock_stub, OpenWriteStreamRaw(_))
      .WillOnce(Return(stream_mock.release()));

  EXPECT_OK(
      internal::StreamWriter<intrinsic_proto::icon::FakeActionParams>::Open(
          SessionId(223344), ActionInstanceId(123), "the_field_name",
          mock_stub.get()));
}

TEST(StreamWriter, OpenInitialRequestWriteFailureReturnsUnknown) {
  auto stream_mock = std::make_unique<MockClientReaderWriter<
      intrinsic_proto::icon::v1::OpenWriteStreamRequest,
      intrinsic_proto::icon::v1::OpenWriteStreamResponse>>();

  {
    ::testing::InSequence s;
    intrinsic_proto::icon::v1::OpenWriteStreamRequest expected_req;
    expected_req.set_session_id(223344);
    expected_req.mutable_add_write_stream()->set_action_id(123);
    expected_req.mutable_add_write_stream()->set_field_name("the_field_name");
    EXPECT_CALL(*stream_mock, Write(EquivToProto(expected_req), _))
        .WillOnce(Return(false));
    // Only allow a single WritesDone() call, more violates a gRPC contract.
    EXPECT_CALL(*stream_mock, WritesDone()).Times(1).WillOnce(Return(true));
  }

  auto mock_stub =
      std::make_unique<intrinsic_proto::icon::v1::MockIconApiStub>();
  EXPECT_CALL(*mock_stub, OpenWriteStreamRaw(_))
      .WillOnce(Return(stream_mock.release()));

  EXPECT_THAT(
      internal::StreamWriter<intrinsic_proto::icon::FakeActionParams>::Open(
          SessionId(223344), ActionInstanceId(123), "the_field_name",
          mock_stub.get()),
      StatusIs(absl::StatusCode::kAborted));
}

TEST(StreamWriter, OpenInitialRequestReadFailureReturnsUnknown) {
  auto stream_mock = std::make_unique<MockClientReaderWriter<
      intrinsic_proto::icon::v1::OpenWriteStreamRequest,
      intrinsic_proto::icon::v1::OpenWriteStreamResponse>>();

  {
    ::testing::InSequence s;
    intrinsic_proto::icon::v1::OpenWriteStreamRequest expected_req;
    expected_req.set_session_id(223344);
    expected_req.mutable_add_write_stream()->set_action_id(123);
    expected_req.mutable_add_write_stream()->set_field_name("the_field_name");
    EXPECT_CALL(*stream_mock, Write(EquivToProto(expected_req), _))
        .WillOnce(Return(true));
    EXPECT_CALL(*stream_mock, Read(_)).WillRepeatedly(Return(false));
    // Only allow a single WritesDone() call, more violates a gRPC contract.
    EXPECT_CALL(*stream_mock, WritesDone()).Times(1).WillOnce(Return(true));
  }

  auto mock_stub =
      std::make_unique<intrinsic_proto::icon::v1::MockIconApiStub>();
  EXPECT_CALL(*mock_stub, OpenWriteStreamRaw(_))
      .WillOnce(Return(stream_mock.release()));

  EXPECT_THAT(
      internal::StreamWriter<intrinsic_proto::icon::FakeActionParams>::Open(
          SessionId(223344), ActionInstanceId(123), "the_field_name",
          mock_stub.get()),
      StatusIs(absl::StatusCode::kUnknown));
}

TEST(StreamWriter, OpenInitialRequestErrorsReported) {
  auto stream_mock = std::make_unique<MockClientReaderWriter<
      intrinsic_proto::icon::v1::OpenWriteStreamRequest,
      intrinsic_proto::icon::v1::OpenWriteStreamResponse>>();

  {
    ::testing::InSequence s;
    intrinsic_proto::icon::v1::OpenWriteStreamRequest expected_req;
    expected_req.set_session_id(223344);
    expected_req.mutable_add_write_stream()->set_action_id(123);
    expected_req.mutable_add_write_stream()->set_field_name("the_field_name");
    EXPECT_CALL(*stream_mock, Write(EquivToProto(expected_req), _))
        .WillOnce(Return(true));

    intrinsic_proto::icon::v1::OpenWriteStreamResponse resp;
    *resp.mutable_add_stream_response()->mutable_status() =
        ToGoogleRpcStatus(absl::NotFoundError("Something went wrong."));
    EXPECT_CALL(*stream_mock, Read(_))
        .WillOnce(DoAll(SetArgPointee<0>(resp), Return(true)))
        .WillRepeatedly(Return(false));
  }

  auto mock_stub =
      std::make_unique<intrinsic_proto::icon::v1::MockIconApiStub>();
  EXPECT_CALL(*mock_stub, OpenWriteStreamRaw(_))
      .WillOnce(Return(stream_mock.release()));

  EXPECT_THAT(
      internal::StreamWriter<intrinsic_proto::icon::FakeActionParams>::Open(
          SessionId(223344), ActionInstanceId(123), "the_field_name",
          mock_stub.get()),
      StatusIs(absl::StatusCode::kNotFound, "Something went wrong."));
}

TEST(StreamWriter, OpenInitialRequestResponseMissingReturnsUnknown) {
  auto stream_mock = std::make_unique<MockClientReaderWriter<
      intrinsic_proto::icon::v1::OpenWriteStreamRequest,
      intrinsic_proto::icon::v1::OpenWriteStreamResponse>>();

  {
    ::testing::InSequence s;
    intrinsic_proto::icon::v1::OpenWriteStreamRequest expected_req;
    expected_req.set_session_id(223344);
    expected_req.mutable_add_write_stream()->set_action_id(123);
    expected_req.mutable_add_write_stream()->set_field_name("the_field_name");
    EXPECT_CALL(*stream_mock, Write(EquivToProto(expected_req), _))
        .WillOnce(Return(true));

    // Leave the response empty.
    intrinsic_proto::icon::v1::OpenWriteStreamResponse resp;
    EXPECT_CALL(*stream_mock, Read(_))
        .WillOnce(DoAll(SetArgPointee<0>(resp), Return(true)))
        .WillRepeatedly(Return(false));
  }

  auto mock_stub =
      std::make_unique<intrinsic_proto::icon::v1::MockIconApiStub>();
  EXPECT_CALL(*mock_stub, OpenWriteStreamRaw(_))
      .WillOnce(Return(stream_mock.release()));

  EXPECT_THAT(
      internal::StreamWriter<intrinsic_proto::icon::FakeActionParams>::Open(
          SessionId(223344), ActionInstanceId(123), "the_field_name",
          mock_stub.get()),
      StatusIs(absl::StatusCode::kUnknown));
}

TEST(StreamWriter, WriteSucceeds) {
  auto stream_mock = std::make_unique<MockClientReaderWriter<
      intrinsic_proto::icon::v1::OpenWriteStreamRequest,
      intrinsic_proto::icon::v1::OpenWriteStreamResponse>>();

  {
    ::testing::InSequence s;
    intrinsic_proto::icon::v1::OpenWriteStreamRequest expected_open_req;
    expected_open_req.set_session_id(223344);
    expected_open_req.mutable_add_write_stream()->set_action_id(123);
    expected_open_req.mutable_add_write_stream()->set_field_name(
        "the_field_name");
    EXPECT_CALL(*stream_mock, Write(EquivToProto(expected_open_req), _))
        .WillOnce(Return(true));

    intrinsic_proto::icon::v1::OpenWriteStreamResponse resp;
    *resp.mutable_add_stream_response()->mutable_status() =
        ToGoogleRpcStatus(absl::OkStatus());
    EXPECT_CALL(*stream_mock, Read(_))
        .WillOnce(DoAll(SetArgPointee<0>(resp), Return(true)));

    intrinsic_proto::icon::v1::OpenWriteStreamRequest expected_value_req;
    intrinsic_proto::icon::FakeActionParams expected_value;
    expected_value.set_value(true);
    ASSERT_TRUE(
        expected_value_req.mutable_write_value()->mutable_value()->PackFrom(
            expected_value));
    EXPECT_CALL(*stream_mock, Write(EquivToProto(expected_value_req), _))
        .WillOnce(Return(true));

    intrinsic_proto::icon::v1::OpenWriteStreamResponse write_value_resp;
    *write_value_resp.mutable_write_value_response() =
        ToGoogleRpcStatus(absl::OkStatus());
    EXPECT_CALL(*stream_mock, Read(_))
        .WillOnce(DoAll(SetArgPointee<0>(write_value_resp), Return(true)));

    EXPECT_CALL(*stream_mock, Read(_)).WillOnce(Return(false));
  }

  auto mock_stub =
      std::make_unique<intrinsic_proto::icon::v1::MockIconApiStub>();
  EXPECT_CALL(*mock_stub, OpenWriteStreamRaw(_))
      .WillOnce(Return(stream_mock.release()));

  ASSERT_OK_AND_ASSIGN(
      auto writer,
      internal::StreamWriter<intrinsic_proto::icon::FakeActionParams>::Open(
          SessionId(223344), ActionInstanceId(123), "the_field_name",
          mock_stub.get()));

  intrinsic_proto::icon::FakeActionParams value;
  value.set_value(true);
  EXPECT_OK(writer->Write(value));
}

TEST(StreamWriter, WriteFailureReturnsAborted) {
  auto stream_mock = std::make_unique<MockClientReaderWriter<
      intrinsic_proto::icon::v1::OpenWriteStreamRequest,
      intrinsic_proto::icon::v1::OpenWriteStreamResponse>>();

  {
    ::testing::InSequence s;
    intrinsic_proto::icon::v1::OpenWriteStreamRequest expected_open_req;
    expected_open_req.set_session_id(223344);
    expected_open_req.mutable_add_write_stream()->set_action_id(123);
    expected_open_req.mutable_add_write_stream()->set_field_name(
        "the_field_name");
    EXPECT_CALL(*stream_mock, Write(EquivToProto(expected_open_req), _))
        .WillOnce(Return(true));

    intrinsic_proto::icon::v1::OpenWriteStreamResponse resp;
    *resp.mutable_add_stream_response()->mutable_status() =
        ToGoogleRpcStatus(absl::OkStatus());
    EXPECT_CALL(*stream_mock, Read(_))
        .WillOnce(DoAll(SetArgPointee<0>(resp), Return(true)));

    intrinsic_proto::icon::v1::OpenWriteStreamRequest expected_value_req;
    intrinsic_proto::icon::FakeActionParams expected_value;
    expected_value.set_value(true);
    ASSERT_TRUE(
        expected_value_req.mutable_write_value()->mutable_value()->PackFrom(
            expected_value));
    EXPECT_CALL(*stream_mock, Write(EquivToProto(expected_value_req), _))
        .WillOnce(Return(false));

    EXPECT_CALL(*stream_mock, Read(_)).WillRepeatedly(Return(false));
    // Only allow a single WritesDone() call, more violates a gRPC contract.
    EXPECT_CALL(*stream_mock, WritesDone()).Times(1).WillOnce(Return(true));
  }

  auto mock_stub =
      std::make_unique<intrinsic_proto::icon::v1::MockIconApiStub>();
  EXPECT_CALL(*mock_stub, OpenWriteStreamRaw(_))
      .WillOnce(Return(stream_mock.release()));

  ASSERT_OK_AND_ASSIGN(
      auto writer,
      internal::StreamWriter<intrinsic_proto::icon::FakeActionParams>::Open(
          SessionId(223344), ActionInstanceId(123), "the_field_name",
          mock_stub.get()));

  intrinsic_proto::icon::FakeActionParams value;
  value.set_value(true);
  EXPECT_THAT(writer->Write(value), StatusIs(absl::StatusCode::kAborted));
}

// Tests using this fixture should only create a single session, since this
// only provides a single mock object, which we expect to be deleted by each
// each session.
class ActionServerMockStubTest : public ::testing::Test {
 public:
  ActionServerMockStubTest()
      : session_mock_(std::make_unique<StrictMock<SessionMock>>()),
        robot_connection_(
            ServerConfig(),
            /*part_names=*/{kPartName1, kPartName2},
            /*create_session=*/
            absl::bind_front(&ActionServerMockStubTest::MakeSessionMock, this)),
        server_(robot_connection_),
        client_(server_.MakeStub()),
        stub_(server_.MakeStub()) {}

  void SetUp() override { ASSERT_OK(client_.Enable()); }

 protected:
  std::unique_ptr<SessionInterface> MakeSessionMock(
      const absl::flat_hash_set<std::string>& part_names,
      SessionId session_id) {
    return std::move(session_mock_);
  }

  std::unique_ptr<SessionMock> session_mock_;
  FakeRobotConnection robot_connection_;
  InProcessApplicationLayerServer server_;
  Client client_;
  std::unique_ptr<intrinsic_proto::icon::v1::IconApi::StubInterface> stub_;
};

// Initializes the stream by allocating the part, and a adding a single action
// for the part. Checks the response. Returns the session_id associated with the
// action session.
SessionId InitializeWithPartAndActionForPartCollection(
    grpc::ClientReaderWriterInterface<
        intrinsic_proto::icon::v1::OpenSessionRequest,
        intrinsic_proto::icon::v1::OpenSessionResponse>* stream,
    absl::string_view part_name, int action_instance_id,
    absl::string_view action_type_name) {
  intrinsic_proto::icon::v1::OpenSessionRequest session_request;
  session_request.mutable_initial_session_data()
      ->mutable_allocate_parts()
      ->add_part(kPartName1);

  constexpr int kActionInstanceId = 1;
  intrinsic_proto::icon::v1::ActionInstance* action_instance =
      session_request.mutable_add_actions_and_reactions()
          ->add_action_instances();
  action_instance->set_action_type_name(kNoopAction);
  action_instance->set_part_name(kPartName1);
  action_instance->set_action_instance_id(kActionInstanceId);

  EXPECT_TRUE(stream->Write(session_request));

  intrinsic_proto::icon::v1::OpenSessionResponse session_response;
  EXPECT_TRUE(stream->Read(&session_response));
  EXPECT_OK(intrinsic::MakeStatusFromRpcStatus(session_response.status()));
  EXPECT_TRUE(session_response.has_action_response());
  return SessionId(session_response.initial_session_data().session_id());
}

TEST_F(ActionServerMockStubTest, StreamWriteDelegatesToSession) {
  google::protobuf::FloatValue value_to_write;
  value_to_write.set_value(-3.14159);
  google::protobuf::Any value_to_write_any;
  ASSERT_TRUE(value_to_write_any.PackFrom(value_to_write));
  constexpr int kActionInstanceId = 1;
  constexpr absl::string_view kInputName = "some_streaming_input";
  absl::Status expected_error =
      absl::AlreadyExistsError("Test error, please ignore.");
  EXPECT_CALL(*session_mock_,
              WriteToStreamingInput(
                  ActionInstanceId(kActionInstanceId), kInputName,
                  ::protobuf_matchers::EquivToProto(value_to_write_any)))
      // The first write is successful.
      .WillOnce(Return(absl::OkStatus()))
      // The second write returns an error.
      .WillOnce(Return(expected_error))
      // The third write is successful.
      .WillOnce(Return(absl::OkStatus()));

  grpc::ClientContext ctx;

  EXPECT_CALL(*session_mock_, PollReactions)
      .WillRepeatedly(Return(std::optional<ReactionEvent>()));
  EXPECT_CALL(*session_mock_, AddActionsAndReactions)
      .WillRepeatedly(Return(absl::Status()));

  grpc::ClientContext context;
  std::unique_ptr<grpc::ClientReaderWriterInterface<
      intrinsic_proto::icon::v1::OpenSessionRequest,
      intrinsic_proto::icon::v1::OpenSessionResponse>>
      stream = stub_->OpenSession(&context);

  SessionId session_id = InitializeWithPartAndActionForPartCollection(
      stream.get(), kPartName1, kActionInstanceId, kNoopAction);

  ASSERT_OK_AND_ASSIGN(
      auto stream_writer,
      internal::StreamWriter<google::protobuf::FloatValue>::Open(
          session_id, ActionInstanceId(kActionInstanceId), kInputName,
          stub_.get()));

  EXPECT_OK(stream_writer->Write(value_to_write));
  EXPECT_THAT(stream_writer->Write(value_to_write),
              StatusIs(expected_error.code(), expected_error.message()));
  EXPECT_OK(stream_writer->Write(value_to_write));
}

}  // namespace
}  // namespace icon
}  // namespace intrinsic
