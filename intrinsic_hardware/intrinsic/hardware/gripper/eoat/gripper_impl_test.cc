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

#include "intrinsic/hardware/gripper/eoat/gripper_impl.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "grpcpp/support/status.h"
#include "grpcpp/support/sync_stream.h"
#include "internal/testing.h"
#include "intrinsic/assets/services/proto/v1/service_state.pb.h"
#include "intrinsic/hardware/gpio/v1/gpio_service.pb.h"
#include "intrinsic/hardware/gpio/v1/gpio_service_mock.grpc.pb.h"
#include "intrinsic/hardware/gpio/v1/signal.pb.h"
#include "intrinsic/hardware/gripper/eoat/eoat_service.pb.h"
#include "intrinsic/hardware/gripper/eoat/gripper_config.pb.h"
#include "intrinsic/hardware/gripper/eoat/gripper_extended_status.h"
#include "intrinsic/hardware/gripper/eoat/gripper_utils.h"
#include "intrinsic/util/status/extended_status.pb.h"
#include "intrinsic/util/status/get_extended_status.h"
#include "intrinsic/util/status/status_conversion_grpc.h"

namespace intrinsic::gripper {

using ::intrinsic_proto::eoat::SuctionGripperConfig;
using ::intrinsic_proto::gpio::v1::GetSignalDescriptionsResponse;
using ::intrinsic_proto::gpio::v1::MockGPIOServiceStub;
using ::intrinsic_proto::gpio::v1::OpenWriteSessionRequest;
using ::intrinsic_proto::gpio::v1::OpenWriteSessionResponse;
using ::intrinsic_proto::gpio::v1::SignalDescription;
using ::intrinsic_proto::gpio::v1::SignalValue;

using ::absl_testing::StatusIs;
using ::testing::_;
using ::testing::DoAll;
using ::testing::HasSubstr;
using ::testing::Return;
using ::testing::SetArgPointee;

namespace {

// Mock of the GPIO write session stream. The GPIO client talks to the stream
// through `ClientReaderWriterInterface`, so the mock can be handed out by
// `MockGPIOServiceStub::OpenWriteSessionRaw`.
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

// Asserts that `status` carries an ExtendedStatus of the gripper component
// with `code`.
void ExpectGripperCode(const grpc::Status& status, ExtendedStatusCode code) {
  std::optional<intrinsic_proto::status::ExtendedStatus> es =
      GetExtendedStatus(status);
  ASSERT_TRUE(es.has_value()) << status.error_message();
  EXPECT_EQ(es->status_code().component(), kExtendedStatusComponent);
  EXPECT_EQ(es->status_code().code(), code);
}

}  // namespace

static SignalValue FalseValue() {
  SignalValue value;
  value.set_bool_value(false);
  return value;
}

static SignalValue TrueValue() {
  SignalValue value;
  value.set_bool_value(true);
  return value;
}

static SuctionGripperConfig CreateSuctionGripperConfig() {
  SuctionGripperConfig config;
  config.mutable_grasp()->mutable_value_set()->mutable_values()->insert(
      {"portG", TrueValue()});
  config.mutable_release()->mutable_value_set()->mutable_values()->insert(
      {"portR", FalseValue()});
  config.mutable_blowoff_on()->mutable_value_set()->mutable_values()->insert(
      {"portB", FalseValue()});
  config.mutable_blowoff_off()->mutable_value_set()->mutable_values()->insert(
      {"portB", TrueValue()});
  config.mutable_blowoff_off()->mutable_value_set()->mutable_values()->insert(
      {"portS", TrueValue()});
  config.mutable_gripping_indicated()
      ->mutable_value_set()
      ->mutable_values()
      ->insert({"portGI", FalseValue()});
  return config;
}

static GetSignalDescriptionsResponse CreateValidSignalDescriptionResponse() {
  GetSignalDescriptionsResponse response;
  // Adds all the write signals.
  for (const auto& name : {"portG", "portR", "portB", "portS"}) {
    SignalDescription signal_desc;
    signal_desc.set_signal_name(name);
    signal_desc.set_can_write(true);
    *response.add_signal_descriptions() = signal_desc;
  }

  // Adds all the read signals.
  for (const auto& name : {"portGI"}) {
    SignalDescription signal_desc;
    signal_desc.set_signal_name(name);
    signal_desc.set_can_read(true);
    *response.add_signal_descriptions() = signal_desc;
  }
  return response;
}

static absl::flat_hash_set<std::string> GripperSignalsToClaim(
    const SuctionGripperConfig& config) {
  return ::intrinsic::gripper::SignalsToClaim(config);
}

TEST(GripperImplTest, CheckHealth) {
  auto gpio_stub_ = std::make_unique<MockGPIOServiceStub>();

  GripperImpl<SuctionGripperConfig> gripper(
      CreateSuctionGripperConfig(), GripperSignalsToClaim, "gpio-service",
      std::move(gpio_stub_));

  EXPECT_EQ(gripper.GetState().state_code(),
            intrinsic_proto::services::v1::SelfState::STATE_CODE_UNSPECIFIED);

  // Can't enable/disable while in unspecified state.
  EXPECT_THAT(ToAbslStatus(gripper.Enable()),
              StatusIs(absl::StatusCode::kFailedPrecondition));
  EXPECT_THAT(ToAbslStatus(gripper.Disable()),
              StatusIs(absl::StatusCode::kFailedPrecondition));
}
// TODO(b/356479998): Re-enable the health check.
// TEST(GripperImplTest, ClearFaultsEnableDisableSuccess) {
//   auto gpio_stub_ = std::make_unique<MockGPIOServiceStub>();

//   EXPECT_CALL(*gpio_stub_, GetSignalDescriptions)
//       .Times(3)
//       .WillOnce(Return(::grpc::Status(grpc::StatusCode::INTERNAL, "")))
//       .WillOnce(DoAll(SetArgPointee<2>(CreateBadSignalDescriptionResponse()),
//                       Return(grpc::Status::OK)))
//       .WillOnce(DoAll(SetArgPointee<2>(CreateValidSignalDescriptionResponse()),
//                       Return(grpc::Status::OK)));

//   GripperImpl<SuctionGripperConfig> gripper(
//       CreateSuctionGripperConfig(), GripperSignalsToClaim, "gpio-service",
//       std::move(gpio_stub_));

//   // Clearing faults should fail first due to error in
//   `GetSignalDescriptions`. EXPECT_THAT(ToAbslStatus(gripper.ClearFaults()),
//               StatusIs(absl::StatusCode::kInternal));

//   // Fail again due to mismatch configuration.
//   EXPECT_THAT(ToAbslStatus(gripper.ClearFaults()),
//               StatusIs(absl::StatusCode::kInternal));

//   // This one should pass and the operational state should be `DISABLED`.
//   EXPECT_OK(ToAbslStatus(gripper.ClearFaults()));
//   EXPECT_EQ(gripper.GetState().state_code(),
//             intrinsic_proto::services::v1::SelfState::STATE_CODE_DISABLED);

//   // Enable/disable should succeed since the faults have been cleared.
//   EXPECT_OK(ToAbslStatus(gripper.Enable()));
//   EXPECT_EQ(gripper.GetState().state_code(),
//             intrinsic_proto::services::v1::SelfState::STATE_CODE_ENABLED);

//   EXPECT_OK(ToAbslStatus(gripper.Disable()));
//   EXPECT_EQ(gripper.GetState().state_code(),
//             intrinsic_proto::services::v1::SelfState::STATE_CODE_DISABLED);
// }

TEST(GripperImplTest, GrippingApis) {
  auto gpio_stub_ = std::make_unique<MockGPIOServiceStub>();

  EXPECT_CALL(*gpio_stub_, ReadSignals)
      .Times(1)
      .WillOnce(Return(grpc::Status::OK));

  EXPECT_CALL(*gpio_stub_, GetSignalDescriptions)
      .Times(1)
      .WillOnce(DoAll(SetArgPointee<2>(CreateValidSignalDescriptionResponse()),
                      Return(grpc::Status::OK)));

  GripperImpl<SuctionGripperConfig> gripper(
      CreateSuctionGripperConfig(), GripperSignalsToClaim, "gpio-service",
      std::move(gpio_stub_));

  EXPECT_EQ(gripper.GetState().state_code(),
            intrinsic_proto::services::v1::SelfState::STATE_CODE_UNSPECIFIED);

  // Can't invoke gripper APIs in unspecified state.
  {
    intrinsic_proto::eoat::GrippingIndicatedResponse response;
    auto reply = gripper.GrippingIndicated(response);
    EXPECT_THAT(ToAbslStatus(reply),
                StatusIs(absl::StatusCode::kFailedPrecondition));
    ExpectGripperCode(reply, kGripperNotEnabled);

    const grpc::Status grasp_status = gripper.Grasp();
    EXPECT_THAT(ToAbslStatus(grasp_status),
                StatusIs(absl::StatusCode::kFailedPrecondition,
                         HasSubstr("Unspecified")));
    ExpectGripperCode(grasp_status, kGripperNotEnabled);
    EXPECT_THAT(ToAbslStatus(gripper.Release()),
                StatusIs(absl::StatusCode::kFailedPrecondition));

    for (const auto turn_on : {true, false}) {
      intrinsic_proto::eoat::BlowOffRequest blow_off_request;
      blow_off_request.set_turn_on(turn_on);
      EXPECT_THAT(ToAbslStatus(gripper.BlowOff(blow_off_request)),
                  StatusIs(absl::StatusCode::kFailedPrecondition));
    }
  }

  EXPECT_OK(ToAbslStatus(gripper.ClearFaults()));
  EXPECT_EQ(gripper.GetState().state_code(),
            intrinsic_proto::services::v1::SelfState::STATE_CODE_DISABLED);
  {
    intrinsic_proto::eoat::GrippingIndicatedResponse response;
    auto reply = gripper.GrippingIndicated(response);
    EXPECT_THAT(ToAbslStatus(reply),
                StatusIs(absl::StatusCode::kFailedPrecondition));
  }

  EXPECT_OK(ToAbslStatus(gripper.Enable()));

  {
    intrinsic_proto::eoat::GrippingIndicatedResponse response;
    auto reply = gripper.GrippingIndicated(response);
    EXPECT_OK(ToAbslStatus(reply));
  }
}

TEST(GripperImplTest, ClearFaultsFailureCarriesExtendedStatus) {
  auto gpio_stub_ = std::make_unique<MockGPIOServiceStub>();
  EXPECT_CALL(*gpio_stub_, GetSignalDescriptions)
      .Times(1)
      .WillOnce(Return(grpc::Status(grpc::StatusCode::UNAVAILABLE,
                                    "failed to connect to all addresses")));

  GripperImpl<SuctionGripperConfig> gripper(
      CreateSuctionGripperConfig(), GripperSignalsToClaim, "gpio-service",
      std::move(gpio_stub_));

  const grpc::Status status = gripper.ClearFaults();
  // The health state machine reports the failure as INTERNAL; the extended
  // status names the cause.
  EXPECT_THAT(ToAbslStatus(status),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("failed to connect to all addresses")));
  ExpectGripperCode(status, kGripperClearFaultsFailed);
  EXPECT_THAT(GetExtendedStatus(status)->user_report().message(),
              HasSubstr("failed to connect to all addresses"));
  EXPECT_EQ(gripper.GetState().state_code(),
            intrinsic_proto::services::v1::SelfState::STATE_CODE_ERROR);
}

// The GPIO service answers the write with NOT_FOUND, which the GPIO client
// forwards verbatim; the gripper reports it as `kGripperIoSignalsNotFound`
// while keeping the canonical status.
TEST(GripperImplTest, GraspReportsUnknownSignalsAsExtendedStatus) {
  auto gpio_stub_ = std::make_unique<MockGPIOServiceStub>();
  auto stream_mock =
      std::make_unique<MockClientReaderWriter<OpenWriteSessionRequest,
                                              OpenWriteSessionResponse>>();

  EXPECT_CALL(*gpio_stub_, GetSignalDescriptions)
      .Times(1)
      .WillOnce(DoAll(SetArgPointee<2>(CreateValidSignalDescriptionResponse()),
                      Return(grpc::Status::OK)));
  {
    ::testing::InSequence s;
    // Claiming the signals succeeds.
    EXPECT_CALL(*stream_mock, Write(_, _)).WillOnce(Return(true));
    EXPECT_CALL(*stream_mock, Read).WillOnce(Return(true));
    // The write itself is rejected by the GPIO service.
    OpenWriteSessionResponse rejected;
    rejected.mutable_status()->set_code(grpc::StatusCode::NOT_FOUND);
    rejected.mutable_status()->set_message("Signal `portG` was not found.");
    EXPECT_CALL(*stream_mock, Write(_, _)).WillOnce(Return(true));
    EXPECT_CALL(*stream_mock, Read)
        .WillOnce(DoAll(SetArgPointee<0>(rejected), Return(true)));
    // The session is closed when the gripper is destroyed.
    EXPECT_CALL(*stream_mock, WritesDone).WillOnce(Return(true));
    EXPECT_CALL(*stream_mock, Finish).WillOnce(Return(grpc::Status::OK));
  }
  EXPECT_CALL(*gpio_stub_, OpenWriteSessionRaw)
      .WillOnce(Return(stream_mock.release()));

  GripperImpl<SuctionGripperConfig> gripper(
      CreateSuctionGripperConfig(), GripperSignalsToClaim, "gpio-service",
      std::move(gpio_stub_));
  ASSERT_OK(ToAbslStatus(gripper.ClearFaults()));
  ASSERT_OK(ToAbslStatus(gripper.Enable()));

  const grpc::Status status = gripper.Grasp();
  EXPECT_THAT(ToAbslStatus(status), StatusIs(absl::StatusCode::kNotFound,
                                             "Signal `portG` was not found."));
  ExpectGripperCode(status, kGripperIoSignalsNotFound);
}

}  // namespace intrinsic::gripper
