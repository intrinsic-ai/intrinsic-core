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

#include "intrinsic/hardware/gpio/gpio_client.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "grpcpp/server.h"
#include "grpcpp/support/status.h"
#include "grpcpp/support/time.h"
#include "internal/testing.h"
#include "intrinsic/hardware/gpio/gpio_service_proto_utils.h"
#include "intrinsic/hardware/gpio/v1/gpio_service.grpc.pb.h"
#include "intrinsic/hardware/gpio/v1/gpio_service.pb.h"
#include "intrinsic/hardware/gpio/v1/gpio_service_mock.grpc.pb.h"
#include "intrinsic/platform/pubsub/pubsub.h"
#include "intrinsic/platform/pubsub/subscription.h"
#include "intrinsic/util/grpc/connection_params.h"
#include "intrinsic/util/grpc/grpc.h"
#include "intrinsic/util/status/status_conversion_grpc.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/thread/thread.h"

namespace intrinsic::gpio {

namespace {

using intrinsic_proto::gpio::v1::MockGPIOServiceStub;
using intrinsic_proto::gpio::v1::OpenWriteSessionRequest;
using intrinsic_proto::gpio::v1::OpenWriteSessionResponse;
using intrinsic_proto::gpio::v1::ReadSignalsRequest;
using intrinsic_proto::gpio::v1::ReadSignalsResponse;
using intrinsic_proto::gpio::v1::SignalValueSet;
using intrinsic_proto::gpio::v1::WaitForValueRequest;

using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;
using ::testing::_;
using ::testing::AllOf;
using ::testing::DoAll;
using ::testing::HasSubstr;
using ::testing::Return;
using ::testing::SetArgPointee;

// A simple fake gpio service that only returns false values for all requested
// signal names.
class FakeGPIOService : public intrinsic_proto::gpio::v1::GPIOService::Service {
 public:
  // Shutdowns the server and its thread if started.
  ~FakeGPIOService() override {
    if (server_) {
      server_->Shutdown();
    }
    if (server_thread_.joinable()) {
      server_thread_.join();
    }
  }

  // Starts the server in a dedicated thread.
  absl::Status StartServer() {
    auto func = [this]() {
      ASSERT_OK_AND_ASSIGN(server_, intrinsic::CreateServer(8080, {this}));
      server_->Wait();
    };
    server_thread_ = Thread(std::move(func));
    return absl::OkStatus();
  }

  // Returns false value for all signal names requested
  ::grpc::Status ReadSignals(
      ::grpc::ServerContext* context,
      const ::intrinsic_proto::gpio::v1::ReadSignalsRequest* request,
      ::intrinsic_proto::gpio::v1::ReadSignalsResponse* response) override {
    for (const std::string& signal_name : request->signal_names()) {
      (*response->mutable_signal_values()->mutable_values())[signal_name] =
          SignalFalseValue();
    }
    return ::grpc::Status::OK;
  }

  // Waits until cancelled or times out.
  ::grpc::Status WaitForValue(
      ::grpc::ServerContext* context,
      const ::intrinsic_proto::gpio::v1::WaitForValueRequest* request,
      ::intrinsic_proto::gpio::v1::WaitForValueResponse* response) override {
    const absl::Time deadline =
        grpc::TimeFromGprTimespec(context->raw_deadline());

    while (absl::Now() < deadline) {
      if (context->IsCancelled()) {
        return ToGrpcStatus(absl::CancelledError("Cancelled."));
      }
      absl::SleepFor(absl::Milliseconds(100));
    }
    return ToGrpcStatus(absl::DeadlineExceededError("Timed out."));
  }

 private:
  std::unique_ptr<grpc::Server> server_;
  Thread server_thread_;
};
}  // namespace

TEST(GPIOClientTest, ReadSingleValueHappyPath) {
  auto service = std::make_unique<MockGPIOServiceStub>();

  const std::string signal_name = "/some/gpio/addr:01";
  SignalValueSet request;
  request.mutable_values()->insert({signal_name, SignalFalseValue()});

  ReadSignalsRequest gpio_request;
  gpio_request.add_signal_names(signal_name);

  ReadSignalsResponse match_fail_response;
  match_fail_response.mutable_signal_values()->mutable_values()->insert(
      {signal_name, SignalTrueValue()});

  ReadSignalsResponse match_success_response;
  match_success_response.mutable_signal_values()->mutable_values()->insert(
      {signal_name, SignalFalseValue()});

  EXPECT_CALL(*service, ReadSignals(_, gpio_request, _))
      .Times(2)
      .WillOnce(DoAll(SetArgPointee<2>(match_success_response),
                      Return(grpc::Status::OK)))
      .WillOnce(DoAll(SetArgPointee<2>(match_fail_response),
                      Return(grpc::Status::OK)));

  GPIOClient client(std::move(service), "gpio-service-name", {signal_name});

  for (bool expected_reply : {true, false}) {
    auto reply = client.ReadAndMatch(request);
    EXPECT_TRUE(reply.ok());
    EXPECT_EQ(*reply, expected_reply);
  }
}

TEST(GPIOClientTest, ReadSingleValueFailures) {
  auto service = std::make_unique<MockGPIOServiceStub>();

  const std::string signal_name = "/some/gpio/addr:01";
  SignalValueSet request;
  request.mutable_values()->insert({signal_name, SignalFalseValue()});

  ReadSignalsRequest gpio_request;
  gpio_request.add_signal_names(signal_name);

  EXPECT_CALL(*service, ReadSignals(_, gpio_request, _))
      .Times(2)
      .WillOnce(Return(grpc::Status(grpc::StatusCode::INTERNAL, "")))
      .WillOnce(
          Return(grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, "")));

  GPIOClient client(std::move(service), "gpio-service-name", {signal_name});

  EXPECT_THAT(client.ReadAndMatch(request),
              StatusIs(absl::StatusCode::kInternal));
  EXPECT_THAT(client.Read(gpio_request),
              StatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST(GPIOClientTest, ReadMultiValueHappyPath) {
  auto service = std::make_unique<MockGPIOServiceStub>();

  SignalValueSet request;
  request.mutable_values()->insert({"foo", SignalFalseValue()});
  request.mutable_values()->insert({"bar", SignalTrueValue()});

  ReadSignalsResponse match_fail_response;
  match_fail_response.mutable_signal_values()->mutable_values()->insert(
      {"foo", SignalTrueValue()});
  match_fail_response.mutable_signal_values()->mutable_values()->insert(
      {"bar", SignalFalseValue()});

  ReadSignalsResponse match_success_response;
  match_success_response.mutable_signal_values()->mutable_values()->insert(
      {"foo", SignalFalseValue()});
  match_success_response.mutable_signal_values()->mutable_values()->insert(
      {"bar", SignalTrueValue()});

  EXPECT_CALL(*service, ReadSignals)
      .Times(3)
      .WillOnce(DoAll(SetArgPointee<2>(match_fail_response),
                      Return(grpc::Status::OK)))
      .WillOnce(DoAll(SetArgPointee<2>(match_success_response),
                      Return(grpc::Status::OK)))
      .WillOnce(DoAll(SetArgPointee<2>(match_fail_response),
                      Return(grpc::Status::OK)));

  GPIOClient client(std::move(service), "gpio-service-name", {"foo", "bar"});

  for (bool expected_reply : {false, true, false}) {
    auto reply = client.ReadAndMatch(request);
    EXPECT_TRUE(reply.ok());
    EXPECT_EQ(*reply, expected_reply);
  }
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

TEST(GPIOClientTest, WriteHappyPath) {
  auto service = std::make_unique<MockGPIOServiceStub>();
  auto stream_mock =
      std::make_unique<MockClientReaderWriter<OpenWriteSessionRequest,
                                              OpenWriteSessionResponse>>();

  SignalValueSet desired_signals1;
  desired_signals1.mutable_values()->insert({"port.01", SignalTrueValue()});
  desired_signals1.mutable_values()->insert({"port.04", SignalTrueValue()});

  SignalValueSet desired_signals2;
  desired_signals2.mutable_values()->insert({"port.01", SignalTrueValue()});
  desired_signals2.mutable_values()->insert({"port.03", SignalTrueValue()});

  {
    ::testing::InSequence s;

    // The first two calls are for opening the initial session to claim the
    // signals
    OpenWriteSessionRequest initial_req;
    for (const auto& name : {"port.01", "port.03", "port.04"}) {
      initial_req.mutable_initial_session_data()->add_signal_names(name);
    }
    EXPECT_CALL(*stream_mock, Write(initial_req, _)).WillOnce(Return(true));
    EXPECT_CALL(*stream_mock, Read).WillOnce(Return(true));

    // Actual write for the first set of values
    OpenWriteSessionRequest write_req1;
    *write_req1.mutable_write_signals()->mutable_signal_values() =
        desired_signals1;
    EXPECT_CALL(*stream_mock, Write(write_req1, _)).WillOnce(Return(true));
    EXPECT_CALL(*stream_mock, Read).WillOnce(Return(true));

    // Actual write for the second set of values (streaming session should
    // already be open at this point)
    OpenWriteSessionRequest write_req2;
    *write_req2.mutable_write_signals()->mutable_signal_values() =
        desired_signals2;
    EXPECT_CALL(*stream_mock, Write(write_req2, _)).WillOnce(Return(true));
    EXPECT_CALL(*stream_mock, Read).WillOnce(Return(true));

    // Close the streaming session
    EXPECT_CALL(*stream_mock, WritesDone).WillOnce(Return(true));
    EXPECT_CALL(*stream_mock, Finish).WillOnce(Return(::grpc::Status::OK));
  }
  EXPECT_CALL(*service, OpenWriteSessionRaw)
      .WillOnce(Return(stream_mock.release()));

  GPIOClient client(std::move(service), "gpio-service-name",
                    {"port.01", "port.03", "port.04"});
  auto reply = client.Write(desired_signals1);
  EXPECT_TRUE(reply.ok());
  reply = client.Write(desired_signals2);
  EXPECT_TRUE(reply.ok());
}

TEST(GPIOClientTest, WriteSessionFailOnClaimSignalsWriteReturnsError) {
  auto service = std::make_unique<MockGPIOServiceStub>();
  auto stream_mock1 =
      std::make_unique<MockClientReaderWriter<OpenWriteSessionRequest,
                                              OpenWriteSessionResponse>>();

  SignalValueSet desired_signals;
  desired_signals.mutable_values()->insert({"port.01", SignalTrueValue()});
  desired_signals.mutable_values()->insert({"port.02", SignalFalseValue()});
  desired_signals.mutable_values()->insert({"port.03", SignalTrueValue()});

  {
    ::testing::InSequence s;

    // Write 1: For the first write call, stream session fails to open
    intrinsic_proto::gpio::v1::OpenWriteSessionRequest open_write_req;
    open_write_req.mutable_initial_session_data()->mutable_signal_names()->Add(
        "port.01");
    open_write_req.mutable_initial_session_data()->mutable_signal_names()->Add(
        "port.02");
    open_write_req.mutable_initial_session_data()->mutable_signal_names()->Add(
        "port.03");

    EXPECT_CALL(*stream_mock1, Write(open_write_req, ::testing::_))
        .WillOnce(Return(false));  // failure

    // No more data to read, so return false.
    EXPECT_CALL(*stream_mock1, Read).WillRepeatedly(Return(false));
    EXPECT_CALL(*stream_mock1, Finish)
        .WillOnce(
            Return(ToGrpcStatus(absl::InternalError("Something went wrong!"))));
  }
  EXPECT_CALL(*service, OpenWriteSessionRaw)
      .WillOnce(Return(stream_mock1.release()));
  GPIOClient client(std::move(service), "gpio-service-name",
                    {"port.01", "port.02", "port.03"});
  EXPECT_THAT(client.Write(desired_signals),
              ::testing::Not(::absl_testing::IsOk()));
}

TEST(GPIOClientTest, WriteSessionFailOnClaimSignalsReadReturnsError) {
  auto service = std::make_unique<MockGPIOServiceStub>();
  auto stream_mock1 =
      std::make_unique<MockClientReaderWriter<OpenWriteSessionRequest,
                                              OpenWriteSessionResponse>>();

  SignalValueSet desired_signals;
  desired_signals.mutable_values()->insert({"port.01", SignalTrueValue()});
  desired_signals.mutable_values()->insert({"port.02", SignalFalseValue()});
  desired_signals.mutable_values()->insert({"port.03", SignalTrueValue()});

  {
    ::testing::InSequence s;

    intrinsic_proto::gpio::v1::OpenWriteSessionRequest open_write_req;
    open_write_req.mutable_initial_session_data()->mutable_signal_names()->Add(
        "port.01");
    open_write_req.mutable_initial_session_data()->mutable_signal_names()->Add(
        "port.02");
    open_write_req.mutable_initial_session_data()->mutable_signal_names()->Add(
        "port.03");

    EXPECT_CALL(*stream_mock1, Write(open_write_req, ::testing::_))
        .WillOnce(Return(true));
    EXPECT_CALL(*stream_mock1, Read).WillOnce(Return(false));  // failure

    // No more data to read, so return false.
    EXPECT_CALL(*stream_mock1, Read).WillRepeatedly(Return(false));
    EXPECT_CALL(*stream_mock1, Finish)
        .WillOnce(
            Return(ToGrpcStatus(absl::InternalError("Something went wrong!"))));
  }
  EXPECT_CALL(*service, OpenWriteSessionRaw)
      .WillOnce(Return(stream_mock1.release()));
  GPIOClient client(std::move(service), "gpio-service-name",
                    {"port.01", "port.02", "port.03"});
  EXPECT_THAT(client.Write(desired_signals),
              ::testing::Not(::absl_testing::IsOk()));
}

TEST(GPIOClientTest,
     FirstWriteSessionFailSetValueWriteReturnsErrorButRetriesSucceed) {
  auto service = std::make_unique<MockGPIOServiceStub>();
  auto stream_mock1 =
      std::make_unique<MockClientReaderWriter<OpenWriteSessionRequest,
                                              OpenWriteSessionResponse>>();
  auto stream_mock2 =
      std::make_unique<MockClientReaderWriter<OpenWriteSessionRequest,
                                              OpenWriteSessionResponse>>();

  SignalValueSet desired_signals;
  desired_signals.mutable_values()->insert({"port.01", SignalTrueValue()});
  desired_signals.mutable_values()->insert({"port.02", SignalFalseValue()});
  desired_signals.mutable_values()->insert({"port.03", SignalTrueValue()});

  {
    ::testing::InSequence s;

    // Signals claimed successfully and session opened.
    intrinsic_proto::gpio::v1::OpenWriteSessionRequest open_write_req;
    open_write_req.mutable_initial_session_data()->mutable_signal_names()->Add(
        "port.01");
    open_write_req.mutable_initial_session_data()->mutable_signal_names()->Add(
        "port.02");
    open_write_req.mutable_initial_session_data()->mutable_signal_names()->Add(
        "port.03");

    EXPECT_CALL(*stream_mock1, Write(open_write_req, ::testing::_))
        .WillOnce(Return(true));
    EXPECT_CALL(*stream_mock1, Read).WillOnce(Return(true));

    intrinsic_proto::gpio::v1::OpenWriteSessionRequest set_value_req;
    *set_value_req.mutable_write_signals()->mutable_signal_values() =
        desired_signals;
    EXPECT_CALL(*stream_mock1, Write(set_value_req, ::testing::_))
        .WillOnce(Return(false));

    // No more data to read, so return false.
    EXPECT_CALL(*stream_mock1, Read).WillRepeatedly(Return(false));
    EXPECT_CALL(*stream_mock1, Finish)
        .WillOnce(
            Return(ToGrpcStatus(absl::InternalError("Something went wrong!"))));

    // Second attempt should succeed
    EXPECT_CALL(*stream_mock2, Write(open_write_req, ::testing::_))
        .WillOnce(Return(true));
    EXPECT_CALL(*stream_mock2, Read).WillOnce(Return(true));
    EXPECT_CALL(*stream_mock2, Write(set_value_req, ::testing::_))
        .WillOnce(Return(true));
    EXPECT_CALL(*stream_mock2, Read).WillOnce(Return(true));

    // Called in GPIOClient's destructor.
    EXPECT_CALL(*stream_mock2, WritesDone).WillOnce(Return(true));
    EXPECT_CALL(*stream_mock2, Finish)
        .WillOnce(Return(ToGrpcStatus(absl::OkStatus())));
  }

  EXPECT_CALL(*service, OpenWriteSessionRaw)
      .WillOnce(Return(stream_mock1.release()))
      .WillOnce(Return(stream_mock2.release()));
  GPIOClient client(std::move(service), "gpio-service-name",
                    {"port.01", "port.02", "port.03"});

  {
    // First try fails. There's no retry because there was no valid write stream
    // to begin with.
    EXPECT_THAT(client.Write(desired_signals),
                ::testing::Not(::absl_testing::IsOk()));

    // Second attempt succeeds
    EXPECT_THAT(client.Write(desired_signals), ::absl_testing::IsOk());
  }
}

TEST(GPIOClientTest,
     FirstWriteSessionFailSetValueReadReturnsErrorButretriesSucceed) {
  auto service = std::make_unique<MockGPIOServiceStub>();
  auto stream_mock1 =
      std::make_unique<MockClientReaderWriter<OpenWriteSessionRequest,
                                              OpenWriteSessionResponse>>();
  auto stream_mock2 =
      std::make_unique<MockClientReaderWriter<OpenWriteSessionRequest,
                                              OpenWriteSessionResponse>>();

  SignalValueSet desired_signals;
  desired_signals.mutable_values()->insert({"port.01", SignalTrueValue()});
  desired_signals.mutable_values()->insert({"port.02", SignalFalseValue()});
  desired_signals.mutable_values()->insert({"port.03", SignalTrueValue()});

  {
    ::testing::InSequence s;

    // Signals claimed successfully and session opened.
    intrinsic_proto::gpio::v1::OpenWriteSessionRequest open_write_req;
    open_write_req.mutable_initial_session_data()->mutable_signal_names()->Add(
        "port.01");
    open_write_req.mutable_initial_session_data()->mutable_signal_names()->Add(
        "port.02");
    open_write_req.mutable_initial_session_data()->mutable_signal_names()->Add(
        "port.03");

    EXPECT_CALL(*stream_mock1, Write(open_write_req, ::testing::_))
        .WillOnce(Return(true));
    EXPECT_CALL(*stream_mock1, Read).WillOnce(Return(true));

    intrinsic_proto::gpio::v1::OpenWriteSessionRequest set_value_req;
    *set_value_req.mutable_write_signals()->mutable_signal_values() =
        desired_signals;
    EXPECT_CALL(*stream_mock1, Write(set_value_req, ::testing::_))
        .WillOnce(Return(true));
    EXPECT_CALL(*stream_mock1, Read).WillOnce(Return(false));

    // No more data to read, so return false.
    EXPECT_CALL(*stream_mock1, WritesDone).WillOnce(Return(true));
    EXPECT_CALL(*stream_mock1, Read).WillRepeatedly(Return(false));
    EXPECT_CALL(*stream_mock1, Finish)
        .WillOnce(
            Return(ToGrpcStatus(absl::InternalError("Something went wrong!"))));

    // Second attempt should succeed
    EXPECT_CALL(*stream_mock2, Write(open_write_req, ::testing::_))
        .WillOnce(Return(true));
    EXPECT_CALL(*stream_mock2, Read).WillOnce(Return(true));
    EXPECT_CALL(*stream_mock2, Write(set_value_req, ::testing::_))
        .WillOnce(Return(true));
    EXPECT_CALL(*stream_mock2, Read).WillOnce(Return(true));

    // Called in GPIOClient's destructor.
    EXPECT_CALL(*stream_mock2, WritesDone).WillOnce(Return(true));
    EXPECT_CALL(*stream_mock2, Finish)
        .WillOnce(Return(ToGrpcStatus(absl::OkStatus())));
  }

  EXPECT_CALL(*service, OpenWriteSessionRaw)
      .WillOnce(Return(stream_mock1.release()))
      .WillOnce(Return(stream_mock2.release()));
  GPIOClient client(std::move(service), "gpio-service-name",
                    {"port.01", "port.02", "port.03"});

  {
    // First try fails (another attempt to open a new write session is not made
    // because the session stream was not valid before the first write).
    EXPECT_THAT(client.Write(desired_signals),
                ::testing::Not(::absl_testing::IsOk()));

    // Second attempt succeeds
    EXPECT_THAT(client.Write(desired_signals), ::absl_testing::IsOk());
  }
}

TEST(GPIOClientTest, ValidSessionReturnsErrorButWriteOpensANewOne) {
  // This test simulates the case where the GPIO service got restarted after a
  // successful `Write` operation(s), thus invaliding the write session stream
  // that GPIOClient maintains for the lifetime of the service (to claim
  // exclusive access to the write signals).
  // In this test, the GPIOClient should detect this scenario and automatically
  // open a new write session stream after closing the previous (invalid) one.

  auto service = std::make_unique<MockGPIOServiceStub>();
  auto stream_mock1 =
      std::make_unique<MockClientReaderWriter<OpenWriteSessionRequest,
                                              OpenWriteSessionResponse>>();
  auto stream_mock2 =
      std::make_unique<MockClientReaderWriter<OpenWriteSessionRequest,
                                              OpenWriteSessionResponse>>();

  SignalValueSet desired_signals;
  desired_signals.mutable_values()->insert({"port.01", SignalTrueValue()});
  desired_signals.mutable_values()->insert({"port.02", SignalFalseValue()});
  desired_signals.mutable_values()->insert({"port.03", SignalTrueValue()});

  {
    ::testing::InSequence s;

    // 1. Mocks for the first `Write` call.
    // Signals claimed successfully and session opened.
    intrinsic_proto::gpio::v1::OpenWriteSessionRequest open_write_req;
    open_write_req.mutable_initial_session_data()->mutable_signal_names()->Add(
        "port.01");
    open_write_req.mutable_initial_session_data()->mutable_signal_names()->Add(
        "port.02");
    open_write_req.mutable_initial_session_data()->mutable_signal_names()->Add(
        "port.03");

    EXPECT_CALL(*stream_mock1, Write(open_write_req, ::testing::_))
        .WillOnce(Return(true));
    EXPECT_CALL(*stream_mock1, Read).WillOnce(Return(true));

    intrinsic_proto::gpio::v1::OpenWriteSessionRequest set_value_req;
    *set_value_req.mutable_write_signals()->mutable_signal_values() =
        desired_signals;
    EXPECT_CALL(*stream_mock1, Write(set_value_req, ::testing::_))
        .WillOnce(Return(true));
    EXPECT_CALL(*stream_mock1, Read).WillOnce(Return(true));

    // 2. Mocks for the second `Write` call.
    // Returns error for the second `Write`. This should cause the gpio_client
    // to close this write session and open a new one.
    EXPECT_CALL(*stream_mock1, Write(set_value_req, ::testing::_))
        .WillOnce(Return(false));
    // // No more data to read, so return false.
    EXPECT_CALL(*stream_mock1, WritesDone).WillOnce(Return(true));
    EXPECT_CALL(*stream_mock1, Read).WillRepeatedly(Return(false));
    EXPECT_CALL(*stream_mock1, Finish)
        .WillOnce(
            Return(ToGrpcStatus(absl::InternalError("Something went wrong!"))));

    // Second attempt should succeed (gpio_client should attempt to open a
    // session).
    EXPECT_CALL(*stream_mock2, Write(open_write_req, ::testing::_))
        .WillOnce(Return(true));
    EXPECT_CALL(*stream_mock2, Read).WillOnce(Return(true));
    EXPECT_CALL(*stream_mock2, Write(set_value_req, ::testing::_))
        .WillOnce(Return(true));
    EXPECT_CALL(*stream_mock2, Read).WillOnce(Return(true));

    // Called in GPIOClient's destructor.
    EXPECT_CALL(*stream_mock2, WritesDone).WillOnce(Return(true));
    EXPECT_CALL(*stream_mock2, Finish)
        .WillOnce(Return(ToGrpcStatus(absl::OkStatus())));
  }

  EXPECT_CALL(*service, OpenWriteSessionRaw)
      .WillOnce(Return(stream_mock1.release()))
      .WillOnce(Return(stream_mock2.release()));
  GPIOClient client(std::move(service), "gpio-service-name",
                    {"port.01", "port.02", "port.03"});

  // First Write should succeed and `client` should have a valid write
  // session.
  EXPECT_OK(client.Write(desired_signals));

  // Second `Write` should also succeed even though the previously valid write
  // session errored out.
  EXPECT_OK(client.Write(desired_signals));
}

TEST(GPIOClientTest, ValidSessionReturnsErrorAndWriteFailsWithoutRetry) {
  auto service = std::make_unique<MockGPIOServiceStub>();
  auto stream_mock1 =
      std::make_unique<MockClientReaderWriter<OpenWriteSessionRequest,
                                              OpenWriteSessionResponse>>();
  auto stream_mock2 =
      std::make_unique<MockClientReaderWriter<OpenWriteSessionRequest,
                                              OpenWriteSessionResponse>>();

  SignalValueSet desired_signals;
  desired_signals.mutable_values()->insert({"port.01", SignalTrueValue()});
  desired_signals.mutable_values()->insert({"port.02", SignalFalseValue()});
  desired_signals.mutable_values()->insert({"port.03", SignalTrueValue()});

  {
    ::testing::InSequence s;

    // 1. Mocks for the first `Write` call.
    // Signals claimed successfully and session opened.
    intrinsic_proto::gpio::v1::OpenWriteSessionRequest open_write_req;
    open_write_req.mutable_initial_session_data()->mutable_signal_names()->Add(
        "port.01");
    open_write_req.mutable_initial_session_data()->mutable_signal_names()->Add(
        "port.02");
    open_write_req.mutable_initial_session_data()->mutable_signal_names()->Add(
        "port.03");

    EXPECT_CALL(*stream_mock1, Write(open_write_req, ::testing::_))
        .WillOnce(Return(true));
    EXPECT_CALL(*stream_mock1, Read).WillOnce(Return(true));

    intrinsic_proto::gpio::v1::OpenWriteSessionRequest set_value_req;
    *set_value_req.mutable_write_signals()->mutable_signal_values() =
        desired_signals;
    EXPECT_CALL(*stream_mock1, Write(set_value_req, ::testing::_))
        .WillOnce(Return(true));
    EXPECT_CALL(*stream_mock1, Read).WillOnce(Return(true));

    // 2. Mocks for the second `Write` call: return write error.
    EXPECT_CALL(*stream_mock1, Write(set_value_req, ::testing::_))
        .WillOnce(Return(false));
    // No more data to read, so return false.
    EXPECT_CALL(*stream_mock1, WritesDone).WillOnce(Return(true));
    EXPECT_CALL(*stream_mock1, Read).WillRepeatedly(Return(false));
    EXPECT_CALL(*stream_mock1, Finish)
        .WillOnce(
            Return(ToGrpcStatus(absl::InternalError("Something went wrong!"))));

    // 3. Mocks for the third `Write` call, which should attempt to open a new
    //    write session.
    EXPECT_CALL(*stream_mock2, Write(open_write_req, ::testing::_))
        .WillOnce(Return(true));
    EXPECT_CALL(*stream_mock2, Read).WillOnce(Return(true));
    EXPECT_CALL(*stream_mock2, Write(set_value_req, ::testing::_))
        .WillOnce(Return(true));
    EXPECT_CALL(*stream_mock2, Read).WillOnce(Return(true));

    // Called in GPIOClient's destructor.
    EXPECT_CALL(*stream_mock2, WritesDone).WillOnce(Return(true));
    EXPECT_CALL(*stream_mock2, Finish)
        .WillOnce(Return(ToGrpcStatus(absl::OkStatus())));
  }

  EXPECT_CALL(*service, OpenWriteSessionRaw)
      .WillOnce(Return(stream_mock1.release()))
      .WillOnce(Return(stream_mock2.release()));
  GPIOClient client(std::move(service), "gpio-service-name",
                    {"port.01", "port.02", "port.03"});

  // First Write should succeed and `client` should have a valid write
  // session.
  EXPECT_OK(client.Write(desired_signals));

  // Second `Write` should fail without retry option.
  constexpr bool kRetryOnSessionFailure = false;
  EXPECT_THAT(client.Write(desired_signals, kRetryOnSessionFailure),
              ::testing::Not(::absl_testing::IsOk()));

  // A follow up `Write` is like a retry and should work without an explicit
  // retry.
  EXPECT_OK(client.Write(desired_signals, kRetryOnSessionFailure));
}

TEST(GPIOClientTest, InvalidClientChannelCreation) {
  const std::string signal_name = "/some/gpio/addr:01";
  const std::string invalid_server_address = "invalid_address:8080";

  GPIOClient client(ConnectionParams::NoIngress(invalid_server_address),
                    {signal_name});

  // Repeated read and write calls should fail with an error
  SignalValueSet request;
  request.mutable_values()->insert({signal_name, SignalFalseValue()});
  for (auto expected_status :
       {absl::StatusCode::kUnavailable, absl::StatusCode::kUnavailable}) {
    auto reply = client.ReadAndMatch(request);
    EXPECT_THAT(reply, StatusIs(expected_status));
  }

  for (auto expected_status :
       {absl::StatusCode::kUnavailable, absl::StatusCode::kUnavailable}) {
    ReadSignalsRequest gpio_request;
    gpio_request.add_signal_names(signal_name);
    auto reply = client.Read(gpio_request);
    EXPECT_THAT(reply, StatusIs(expected_status));
  }

  for (auto expected_status :
       {absl::StatusCode::kUnavailable, absl::StatusCode::kUnavailable}) {
    auto reply = client.Write(request);
    EXPECT_THAT(reply, StatusIs(expected_status));
  }
}

TEST(GPIOClientTest, ServerAbortesWriteSessionButClientOpensANewOne) {
  auto service = std::make_unique<MockGPIOServiceStub>();
  auto stream_mock1 =
      std::make_unique<MockClientReaderWriter<OpenWriteSessionRequest,
                                              OpenWriteSessionResponse>>();
  auto stream_mock2 =
      std::make_unique<MockClientReaderWriter<OpenWriteSessionRequest,
                                              OpenWriteSessionResponse>>();

  SignalValueSet desired_signals;
  desired_signals.mutable_values()->insert({"port.01", SignalTrueValue()});
  desired_signals.mutable_values()->insert({"port.02", SignalFalseValue()});
  desired_signals.mutable_values()->insert({"port.03", SignalTrueValue()});

  {
    ::testing::InSequence s;

    // 1. Mocks for the first `Write` call.
    // Signals claimed successfully and session opened.
    intrinsic_proto::gpio::v1::OpenWriteSessionRequest open_write_req;
    open_write_req.mutable_initial_session_data()->mutable_signal_names()->Add(
        "port.01");
    open_write_req.mutable_initial_session_data()->mutable_signal_names()->Add(
        "port.02");
    open_write_req.mutable_initial_session_data()->mutable_signal_names()->Add(
        "port.03");

    EXPECT_CALL(*stream_mock1, Write(open_write_req, ::testing::_))
        .WillOnce(Return(true));
    EXPECT_CALL(*stream_mock1, Read).WillOnce(Return(true));

    intrinsic_proto::gpio::v1::OpenWriteSessionRequest set_value_req;
    *set_value_req.mutable_write_signals()->mutable_signal_values() =
        desired_signals;
    EXPECT_CALL(*stream_mock1, Write(set_value_req, ::testing::_))
        .WillOnce(Return(true));
    EXPECT_CALL(*stream_mock1, Read).WillOnce(Return(true));

    // 2. Mocks for the second `Write` call: abort the session.
    EXPECT_CALL(*stream_mock1, Write(set_value_req, ::testing::_))
        .WillOnce(Return(true));
    intrinsic_proto::gpio::v1::OpenWriteSessionResponse abort_session;
    abort_session.mutable_status()->set_code(grpc::StatusCode::ABORTED);
    EXPECT_CALL(*stream_mock1, Read)
        .WillOnce(DoAll(SetArgPointee<0>(abort_session), Return(true)));

    // 2b. Mocks to clean up the aborted write session.
    EXPECT_CALL(*stream_mock1, WritesDone).WillOnce(Return(true));
    EXPECT_CALL(*stream_mock1, Read).WillRepeatedly(Return(false));
    EXPECT_CALL(*stream_mock1, Finish)
        .WillOnce(Return(ToGrpcStatus(absl::OkStatus())));

    // 2c. Mocks for the retry `Write` call, which should attempt to open a
    // new session.
    EXPECT_CALL(*stream_mock2, Write(open_write_req, ::testing::_))
        .WillOnce(Return(true));
    EXPECT_CALL(*stream_mock2, Read).WillOnce(Return(true));
    EXPECT_CALL(*stream_mock2, Write(set_value_req, ::testing::_))
        .WillOnce(Return(true));
    EXPECT_CALL(*stream_mock2, Read).WillOnce(Return(true));

    // Called in GPIOClient's destructor.
    EXPECT_CALL(*stream_mock2, WritesDone).WillOnce(Return(true));
    EXPECT_CALL(*stream_mock2, Finish)
        .WillOnce(Return(ToGrpcStatus(absl::OkStatus())));
  }

  EXPECT_CALL(*service, OpenWriteSessionRaw)
      .WillOnce(Return(stream_mock1.release()))
      .WillOnce(Return(stream_mock2.release()));
  GPIOClient client(std::move(service), "gpio-service-name",
                    {"port.01", "port.02", "port.03"});

  // 1. Should succeed.
  EXPECT_OK(client.Write(desired_signals));

  // 2. Should handle the aborted session error, and try again.
  EXPECT_OK(client.Write(desired_signals));
}

TEST(GPIOClientTest, ServerNonAbortedErrorShouldNotResultInRetry) {
  auto service = std::make_unique<MockGPIOServiceStub>();
  auto stream_mock1 =
      std::make_unique<MockClientReaderWriter<OpenWriteSessionRequest,
                                              OpenWriteSessionResponse>>();
  SignalValueSet desired_signals;
  desired_signals.mutable_values()->insert({"port.01", SignalTrueValue()});
  desired_signals.mutable_values()->insert({"port.02", SignalFalseValue()});
  desired_signals.mutable_values()->insert({"port.03", SignalTrueValue()});

  {
    ::testing::InSequence s;

    // 1. Mocks for the first `Write` call.
    // Signals claimed successfully and session opened.
    intrinsic_proto::gpio::v1::OpenWriteSessionRequest open_write_req;
    open_write_req.mutable_initial_session_data()->mutable_signal_names()->Add(
        "port.01");
    open_write_req.mutable_initial_session_data()->mutable_signal_names()->Add(
        "port.02");
    open_write_req.mutable_initial_session_data()->mutable_signal_names()->Add(
        "port.03");

    EXPECT_CALL(*stream_mock1, Write(open_write_req, ::testing::_))
        .WillOnce(Return(true));
    EXPECT_CALL(*stream_mock1, Read).WillOnce(Return(true));

    intrinsic_proto::gpio::v1::OpenWriteSessionRequest set_value_req;
    *set_value_req.mutable_write_signals()->mutable_signal_values() =
        desired_signals;
    EXPECT_CALL(*stream_mock1, Write(set_value_req, ::testing::_))
        .WillOnce(Return(true));
    EXPECT_CALL(*stream_mock1, Read).WillOnce(Return(true));

    // 2. Mocks for the second `Write` call: trigger non-session-ending error.
    EXPECT_CALL(*stream_mock1, Write(set_value_req, ::testing::_))
        .WillOnce(Return(true));
    intrinsic_proto::gpio::v1::OpenWriteSessionResponse non_fatal_session_error;
    non_fatal_session_error.mutable_status()->set_code(
        grpc::StatusCode::CANCELLED);
    EXPECT_CALL(*stream_mock1, Read)
        .WillOnce(
            DoAll(SetArgPointee<0>(non_fatal_session_error), Return(true)));

    // 3. Mocks for the third `Write` call, which should use the same write
    // session.
    EXPECT_CALL(*stream_mock1, Write(set_value_req, ::testing::_))
        .WillOnce(Return(true));
    EXPECT_CALL(*stream_mock1, Read).WillOnce(Return(true));

    // Called in GPIOClient's destructor.
    EXPECT_CALL(*stream_mock1, WritesDone).WillOnce(Return(true));
    EXPECT_CALL(*stream_mock1, Finish)
        .WillOnce(Return(ToGrpcStatus(absl::OkStatus())));
  }

  EXPECT_CALL(*service, OpenWriteSessionRaw)
      .WillOnce(Return(stream_mock1.release()));
  GPIOClient client(std::move(service), "gpio-service-name",
                    {"port.01", "port.02", "port.03"});

  // 1. Should succeed.
  EXPECT_OK(client.Write(desired_signals));

  // 2. Should get cancelled (non-session-ending error). No retry.
  EXPECT_THAT(client.Write(desired_signals),
              StatusIs(absl::StatusCode::kCancelled));

  // 3. Should succeed.
  EXPECT_OK(client.Write(desired_signals));
}

TEST(GPIOClientTest, DelayedClientChannelCreation) {
  const std::string signal_name = "/some/gpio/addr:01";
  SignalValueSet request;
  request.mutable_values()->insert({signal_name, SignalFalseValue()});

  ReadSignalsRequest gpio_request;
  gpio_request.add_signal_names(signal_name);

  ReadSignalsResponse match_success_response;
  match_success_response.mutable_signal_values()->mutable_values()->insert(
      {signal_name, SignalFalseValue()});

  GPIOClient client(ConnectionParams::LocalPort(8080), {signal_name});

  // First request should fail with unavailable status because the server is not
  // ready yet.
  {
    auto reply = client.ReadAndMatch(request);
    EXPECT_THAT(reply, StatusIs(absl::StatusCode::kUnavailable));
  }

  FakeGPIOService service;
  ASSERT_OK(service.StartServer());
  EXPECT_THAT(client.ReadAndMatch(request), IsOkAndHolds(true));
}

TEST(GPIOClientTest, WaitForValueSucceeds) {
  auto service = std::make_unique<MockGPIOServiceStub>();

  const std::string signal_name = "/some/gpio/addr:01";

  EXPECT_CALL(*service, WaitForValue(_, _, _))
      .WillOnce(Return(grpc::Status::OK));

  GPIOClient client(std::move(service), "gpio-service-name", {signal_name});

  WaitForValueRequest wait_request;

  EXPECT_OK(client.WaitForValue(wait_request, absl::ZeroDuration()));
}

TEST(GPIOClientTest, GetSignalDescriptionError) {
  auto service = std::make_unique<MockGPIOServiceStub>();

  const std::string signal_name = "/some/gpio/addr:01";

  EXPECT_CALL(*service, GetSignalDescriptions(_, _, _))
      .WillOnce(Return(grpc::Status::CANCELLED));

  GPIOClient client(std::move(service), "gpio-service-name", {signal_name});

  EXPECT_THAT(client.GetSignalDescriptions(),
              StatusIs(absl::StatusCode::kCancelled,
                       AllOf(HasSubstr("Failed to get signal descriptions"),
                             HasSubstr("gpio-service-name"))));
}

TEST(GPIOClientTest, SubscribeToSignalHappyPath) {
  auto service = std::make_unique<MockGPIOServiceStub>();

  const std::string signal_name = "port.01";
  const std::string topic_name = "/gpio/port.01";

  intrinsic_proto::gpio::v1::GetSignalDescriptionsResponse response;
  auto* desc = response.add_signal_descriptions();
  desc->set_signal_name(signal_name);
  desc->set_pubsub_topic_name(topic_name);

  EXPECT_CALL(*service, GetSignalDescriptions(_, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(response), Return(grpc::Status::OK)));

  GPIOClient client(std::move(service), "gpio-service-name", {signal_name});

  auto sub_or = client.SubscribeToSignal(
      signal_name,
      [](const intrinsic_proto::gpio::v1::SignalValue& signal_value) {});
  EXPECT_OK(sub_or);
  EXPECT_EQ(sub_or->TopicName(), topic_name);
}

TEST(GPIOClientTest, SubscribeToSignalAlternateNameHappyPath) {
  auto service = std::make_unique<MockGPIOServiceStub>();

  const std::string signal_name = "port.01";
  const std::string alt_name = "alias.01";
  const std::string topic_name = "/gpio/port.01";

  intrinsic_proto::gpio::v1::GetSignalDescriptionsResponse response;
  auto* desc = response.add_signal_descriptions();
  desc->set_signal_name(signal_name);
  desc->add_alternate_signal_names(alt_name);
  desc->set_pubsub_topic_name(topic_name);

  EXPECT_CALL(*service, GetSignalDescriptions(_, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(response), Return(grpc::Status::OK)));

  GPIOClient client(std::move(service), "gpio-service-name", {signal_name});

  auto sub_or = client.SubscribeToSignal(
      alt_name,
      [](const intrinsic_proto::gpio::v1::SignalValue& signal_value) {});
  EXPECT_OK(sub_or);
  EXPECT_EQ(sub_or->TopicName(), topic_name);
}

TEST(GPIOClientTest, SubscribeToSignalNotFound) {
  auto service = std::make_unique<MockGPIOServiceStub>();

  intrinsic_proto::gpio::v1::GetSignalDescriptionsResponse response;
  auto* desc = response.add_signal_descriptions();
  desc->set_signal_name("other_signal");

  EXPECT_CALL(*service, GetSignalDescriptions(_, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(response), Return(grpc::Status::OK)));

  GPIOClient client(std::move(service), "gpio-service-name", {"port.01"});

  auto sub_or = client.SubscribeToSignal(
      "port.01",
      [](const intrinsic_proto::gpio::v1::SignalValue& signal_value) {});
  EXPECT_THAT(sub_or, StatusIs(absl::StatusCode::kNotFound,
                               HasSubstr("Signal not found: port.01")));
}

TEST(GPIOClientTest, SubscribeToSignalNoTopic) {
  auto service = std::make_unique<MockGPIOServiceStub>();

  const std::string signal_name = "port.01";

  intrinsic_proto::gpio::v1::GetSignalDescriptionsResponse response;
  auto* desc = response.add_signal_descriptions();
  desc->set_signal_name(signal_name);
  desc->set_pubsub_topic_name("");  // No topic

  EXPECT_CALL(*service, GetSignalDescriptions(_, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(response), Return(grpc::Status::OK)));

  GPIOClient client(std::move(service), "gpio-service-name", {signal_name});

  auto sub_or = client.SubscribeToSignal(
      signal_name,
      [](const intrinsic_proto::gpio::v1::SignalValue& signal_value) {});
  EXPECT_THAT(sub_or, StatusIs(absl::StatusCode::kNotFound,
                               HasSubstr("does not have a pubsub topic")));
}

}  // namespace intrinsic::gpio
