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

#include "intrinsic/icon/cc_client/client.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/log_severity.h"
#include "absl/log/scoped_mock_log.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/time/time.h"
#include "grpcpp/support/status.h"
#include "internal/testing.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/cc_client/operational_status.h"
#include "intrinsic/icon/cc_client/testing/channel_fake.h"
#include "intrinsic/icon/cc_client/testing/rtcl_controller_channel_fake.h"
#include "intrinsic/icon/common/part_properties.h"
#include "intrinsic/icon/control/logging_mode.h"
#include "intrinsic/icon/proto/part_status.pb.h"
#include "intrinsic/icon/proto/v1/service.pb.h"
#include "intrinsic/icon/proto/v1/service_mock.grpc.pb.h"
#include "intrinsic/icon/proto/v1/types.pb.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/util/grpc/channel_interface.h"
#include "intrinsic/util/macros.h"
#include "intrinsic/util/proto/parse_text_proto.h"
#include "intrinsic/util/proto_time.h"
#include "intrinsic/util/status/status_conversion_grpc.h"
#include "intrinsic/world/robot_payload/robot_payload.h"
#include "protobuf-matchers/protocol-buffer-matchers.h"

namespace intrinsic {
namespace icon {
namespace {

using ::absl::ScopedMockLog;
using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;
using ::intrinsic::ParseTextProto;
using ::protobuf_matchers::EqualsProto;
using ::testing::_;
using ::testing::AnyNumber;
using ::testing::AtLeast;
using ::testing::DoAll;
using ::testing::DoubleEq;
using ::testing::ElementsAre;
using ::testing::EndsWith;
using ::testing::HasSubstr;
using ::testing::Key;
using ::testing::Pair;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::SetArgPointee;
using ::testing::VariantWith;

struct Mock {
  intrinsic_proto::icon::v1::MockIconApiStub* stub;
  std::unique_ptr<Client> client;
};

Mock CreateMockClient() {
  Mock mock;
  auto stub = std::make_unique<intrinsic_proto::icon::v1::MockIconApiStub>();
  mock.stub = stub.get();
  mock.client = std::make_unique<Client>(std::move(stub));
  return mock;
}

TEST(Client, ConstructDestructWithChannelInterface) {
  ASSERT_OK_AND_ASSIGN(
      std::shared_ptr<ChannelInterface> icon_channel,
      RtclControllerChannelFake::Builder().Build(DefaultPartConfigs()));
  Client client(icon_channel);
}

TEST(Client, SpeedOverride) {
  ASSERT_OK_AND_ASSIGN(
      std::shared_ptr<ChannelInterface> icon_channel,
      RtclControllerChannelFake::Builder().Build(DefaultPartConfigs()));
  Client client(icon_channel);
  EXPECT_THAT(client.GetSpeedOverride(), IsOkAndHolds(DoubleEq(1.0)));
  EXPECT_OK(client.SetSpeedOverride(0.2));
  EXPECT_THAT(client.GetSpeedOverride(), IsOkAndHolds(DoubleEq(0.2)));
  // Values outside of [0,1] are illegal.
  EXPECT_THAT(client.SetSpeedOverride(-0.2),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("must be between 0 and 1")));
  EXPECT_THAT(client.SetSpeedOverride(2.0),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("must be between 0 and 1")));
}

TEST(Client, SetLoggingMode) {
  ASSERT_OK_AND_ASSIGN(
      std::shared_ptr<ChannelInterface> icon_channel,
      RtclControllerChannelFake::Builder().Build(DefaultPartConfigs()));
  Client client(icon_channel);

  EXPECT_THAT(client.GetLoggingMode(), IsOkAndHolds(LoggingMode::kThrottled));

  EXPECT_OK(client.SetLoggingMode(LoggingMode::kFullRate));
  EXPECT_THAT(client.GetLoggingMode(), IsOkAndHolds(LoggingMode::kFullRate));

  EXPECT_OK(client.SetLoggingMode(LoggingMode::kThrottled));
  EXPECT_THAT(client.GetLoggingMode(), IsOkAndHolds(LoggingMode::kThrottled));
}

TEST(Client, GetActionSignatureByName) {
  Mock mock = CreateMockClient();

  // Prepare mock method.
  intrinsic_proto::icon::v1::GetActionSignatureByNameRequest expected_request;
  expected_request.set_name("fake_action");
  intrinsic_proto::icon::v1::GetActionSignatureByNameResponse response;
  response.mutable_action_signature()->set_action_type_name("fake_action");
  EXPECT_CALL(*mock.stub,
              GetActionSignatureByName(_, EqualsProto(expected_request), _))
      .WillOnce(DoAll(SetArgPointee<2>(response), Return(::grpc::Status::OK)));

  // Invoke it through client.
  ASSIGN_OR_DIE(intrinsic_proto::icon::v1::ActionSignature signature,
                mock.client->GetActionSignatureByName("fake_action"));

  // Check results.
  EXPECT_THAT(signature, EqualsProto(response.action_signature()));
}

TEST(Client, GetActionSignatureByNameNotFound) {
  Mock mock = CreateMockClient();

  // Prepare mock method.
  intrinsic_proto::icon::v1::GetActionSignatureByNameRequest expected_request;
  expected_request.set_name("fake_action");
  intrinsic_proto::icon::v1::GetActionSignatureByNameResponse response;
  EXPECT_CALL(*mock.stub,
              GetActionSignatureByName(_, EqualsProto(expected_request), _))
      .WillOnce(DoAll(SetArgPointee<2>(response), Return(::grpc::Status::OK)));

  // Invoke it through client and check result.
  EXPECT_THAT(mock.client->GetActionSignatureByName("fake_action"),
              StatusIs(absl::StatusCode::kNotFound,
                       HasSubstr("\"fake_action\" not found")));
}

TEST(Client, GetActionSignatureByNameGrpcFails) {
  Mock mock = CreateMockClient();

  EXPECT_CALL(*mock.stub, GetActionSignatureByName(_, _, _))
      .WillRepeatedly(
          Return(::grpc::Status(grpc::StatusCode::CANCELLED, "cancelled")));

  EXPECT_THAT(mock.client->GetActionSignatureByName("fake_action"),
              StatusIs(absl::StatusCode::kCancelled, HasSubstr("cancelled")));
}

TEST(Client, GetStatusSucceeds) {
  Mock mock = CreateMockClient();

  // Prepare mock method.
  intrinsic_proto::icon::v1::GetStatusRequest request;
  ASSERT_OK_AND_ASSIGN(
      auto response,
      ParseTextProto<intrinsic_proto::icon::v1::GetStatusResponse>(
          R"pb(
            part_status {
              key: "robot_arm"
              value: { gripper_state { sensed_state: SENSED_STATE_HOLDING } }
            }
            safety_status {
              mode_of_safe_operation: MODE_OF_SAFE_OPERATION_TEACHING_2
              estop_button_status: BUTTON_STATUS_ENGAGED
              enable_button_status: BUTTON_STATUS_DISENGAGED
            }
          )pb"));

  EXPECT_CALL(*mock.stub, GetStatus(_, EqualsProto(request), _))
      .WillOnce(DoAll(SetArgPointee<2>(response), Return(::grpc::Status::OK)));

  // Invoke it through client and check results.
  ASSERT_OK_AND_ASSIGN(
      intrinsic_proto::icon::v1::GetStatusResponse result_status,
      mock.client->GetStatus());
  EXPECT_THAT(result_status, EqualsProto(response));
}

TEST(Client, GetSinglePartStatusSucceeds) {
  Mock mock = CreateMockClient();

  // Prepare mock method.
  intrinsic_proto::icon::v1::GetStatusRequest request;
  ASSERT_OK_AND_ASSIGN(
      auto response,
      ParseTextProto<intrinsic_proto::icon::v1::GetStatusResponse>(
          R"pb(
            part_status {
              key: "robot_arm"
              value: { gripper_state { sensed_state: SENSED_STATE_HOLDING } }
            }
          )pb"));

  EXPECT_CALL(*mock.stub, GetStatus(_, EqualsProto(request), _))
      .WillRepeatedly(
          DoAll(SetArgPointee<2>(response), Return(::grpc::Status::OK)));

  // Error for unknown Part.
  EXPECT_THAT(mock.client->GetSinglePartStatus("snare_drum"),
              StatusIs(absl::StatusCode::kNotFound));
  // Extracts PartStatus proto from GetStatusResponse.
  ASSERT_OK_AND_ASSIGN(intrinsic_proto::icon::PartStatus part_status,
                       mock.client->GetSinglePartStatus("robot_arm"));
  EXPECT_THAT(part_status, EqualsProto(response.part_status().at("robot_arm")));
}

TEST(Client, IsActionCompatibleSucceeds) {
  Mock mock = CreateMockClient();

  // Prepare mock method.
  intrinsic_proto::icon::v1::IsActionCompatibleRequest expected_request;
  expected_request.set_part_name("arm");
  expected_request.set_action_type_name("move_arm");
  intrinsic_proto::icon::v1::IsActionCompatibleResponse response;
  response.set_is_compatible(true);
  EXPECT_CALL(*mock.stub,
              IsActionCompatible(_, EqualsProto(expected_request), _))
      .WillOnce(DoAll(SetArgPointee<2>(response), Return(::grpc::Status::OK)));

  // Invoke it through client and check results.
  EXPECT_THAT(mock.client->IsActionCompatible("arm", "move_arm"),
              IsOkAndHolds(true));
}

TEST(Client, IsActionCompatibleWithSlotPartMapSucceeds) {
  Mock mock = CreateMockClient();

  // Prepare mock method.
  intrinsic_proto::icon::v1::IsActionCompatibleRequest expected_request;
  expected_request.mutable_slot_part_map()
      ->mutable_slot_name_to_part_name()
      ->insert({"arm_slot", "arm_part"});
  expected_request.set_action_type_name("move_arm");
  intrinsic_proto::icon::v1::IsActionCompatibleResponse response;
  response.set_is_compatible(true);
  EXPECT_CALL(*mock.stub,
              IsActionCompatible(_, EqualsProto(expected_request), _))
      .WillOnce(DoAll(SetArgPointee<2>(response), Return(::grpc::Status::OK)));

  // Invoke it through client and check results.
  EXPECT_THAT(
      mock.client->IsActionCompatible({{"arm_slot", "arm_part"}}, "move_arm"),
      IsOkAndHolds(true));
}

TEST(Client, IsActionCompatibleGrpcFails) {
  Mock mock = CreateMockClient();

  EXPECT_CALL(*mock.stub, IsActionCompatible(_, _, _))
      .WillOnce(
          Return(::grpc::Status(grpc::StatusCode::CANCELLED, "cancelled")));

  EXPECT_THAT(mock.client->IsActionCompatible("left_arm", "dance_a_jig"),
              StatusIs(absl::StatusCode::kCancelled, HasSubstr("cancelled")));
}

TEST(Client, ListActionSignaturesSucceeds) {
  Mock mock = CreateMockClient();

  // Prepare mock method.
  intrinsic_proto::icon::v1::ListActionSignaturesRequest expected_request;
  intrinsic_proto::icon::v1::ListActionSignaturesResponse response;
  intrinsic_proto::icon::v1::ActionSignature* signature =
      response.add_action_signatures();
  signature->set_action_type_name("fake_action");
  EXPECT_CALL(*mock.stub,
              ListActionSignatures(_, EqualsProto(expected_request), _))
      .WillOnce(DoAll(SetArgPointee<2>(response), Return(::grpc::Status::OK)));

  // Invoke it through client.
  ASSERT_OK_AND_ASSIGN(
      std::vector<intrinsic_proto::icon::v1::ActionSignature> results,
      mock.client->ListActionSignatures());

  // Check results.
  EXPECT_EQ(results.size(), 1);
  EXPECT_THAT(results.at(0), EqualsProto(*signature));
}

TEST(Client, ListActionSignaturesSucceedsNoWarning) {
  Mock mock = CreateMockClient();
  ScopedMockLog log;

  // Ignore logs in other modules
  EXPECT_CALL(log,
              Log(_, Not(EndsWith("intrinsic/icon/cc_client/client.cc")), _))
      .Times(AnyNumber());

  // Prepare mock method.
  intrinsic_proto::icon::v1::ListActionSignaturesRequest expected_request;
  intrinsic_proto::icon::v1::ListActionSignaturesResponse response;
  response.add_action_signatures()->set_action_type_name(
      "intrinsic.cartesian_position");
  response.add_action_signatures()->set_action_type_name(
      "intrinsic.cartesian_velocity");
  response.add_action_signatures()->set_action_type_name(
      "intrinsic.dio_gripper");
  response.add_action_signatures()->set_action_type_name(
      "intrinsic.point_to_point_move");
  response.add_action_signatures()->set_action_type_name(
      "intrinsic.streaming_joint_position_move");
  response.add_action_signatures()->set_action_type_name("intrinsic.stop");

  EXPECT_CALL(*mock.stub,
              ListActionSignatures(_, EqualsProto(expected_request), _))
      .WillOnce(DoAll(SetArgPointee<2>(response), Return(::grpc::Status::OK)));

  log.StartCapturingLogs();

  // Invoke it through client.
  ASSERT_OK_AND_ASSIGN(
      std::vector<intrinsic_proto::icon::v1::ActionSignature> results,
      mock.client->ListActionSignatures());

  log.StopCapturingLogs();

  // Check that results are sorted alphabetically.
  EXPECT_EQ(results.size(), 6);
  EXPECT_THAT(results.at(0).action_type_name(), "intrinsic.cartesian_position");
  EXPECT_THAT(results.at(1).action_type_name(), "intrinsic.cartesian_velocity");
  EXPECT_THAT(results.at(2).action_type_name(), "intrinsic.dio_gripper");
  EXPECT_THAT(results.at(3).action_type_name(),
              "intrinsic.point_to_point_move");
  EXPECT_THAT(results.at(4).action_type_name(), "intrinsic.stop");
  EXPECT_THAT(results.at(5).action_type_name(),
              "intrinsic.streaming_joint_position_move");
}

TEST(Client, ListActionSignaturesDuplicateProducesWarning) {
  Mock mock = CreateMockClient();
  ScopedMockLog log;

  // Ignore logs in other modules
  EXPECT_CALL(log,
              Log(_, Not(EndsWith("intrinsic/icon/cc_client/client.cc")), _))
      .Times(AnyNumber());

  // Expect a log warning.
  EXPECT_CALL(log, Log(absl::LogSeverity::kWarning,
                       EndsWith("intrinsic/icon/cc_client/client.cc"),
                       "Server returned duplicate action type name "
                       "\"intrinsic.cartesian_velocity\""))
      .Times(AtLeast(1));

  // Prepare mock method.
  intrinsic_proto::icon::v1::ListActionSignaturesRequest expected_request;
  intrinsic_proto::icon::v1::ListActionSignaturesResponse response;
  // intrinsic.cartesian_velocity is duplicated
  response.add_action_signatures()->set_action_type_name(
      "intrinsic.cartesian_velocity");
  response.add_action_signatures()->set_action_type_name(
      "intrinsic.cartesian_position");
  response.add_action_signatures()->set_action_type_name(
      "intrinsic.cartesian_velocity");
  response.add_action_signatures()->set_action_type_name(
      "intrinsic.dio_gripper");
  response.add_action_signatures()->set_action_type_name(
      "intrinsic.point_to_point_move");
  response.add_action_signatures()->set_action_type_name(
      "intrinsic.streaming_joint_position_move");
  response.add_action_signatures()->set_action_type_name("intrinsic.stop");

  EXPECT_CALL(*mock.stub,
              ListActionSignatures(_, EqualsProto(expected_request), _))
      .WillOnce(DoAll(SetArgPointee<2>(response), Return(::grpc::Status::OK)));

  log.StartCapturingLogs();

  // Invoke it through client.
  ASSERT_OK_AND_ASSIGN(
      std::vector<intrinsic_proto::icon::v1::ActionSignature> results,
      mock.client->ListActionSignatures());

  log.StopCapturingLogs();

  // Check that results are sorted alphabetically.
  EXPECT_EQ(results.size(), 7);
  EXPECT_THAT(results.at(0).action_type_name(), "intrinsic.cartesian_position");
  EXPECT_THAT(results.at(1).action_type_name(), "intrinsic.cartesian_velocity");
  EXPECT_THAT(results.at(2).action_type_name(), "intrinsic.cartesian_velocity");
  EXPECT_THAT(results.at(3).action_type_name(), "intrinsic.dio_gripper");
  EXPECT_THAT(results.at(4).action_type_name(),
              "intrinsic.point_to_point_move");
  EXPECT_THAT(results.at(5).action_type_name(), "intrinsic.stop");
  EXPECT_THAT(results.at(6).action_type_name(),
              "intrinsic.streaming_joint_position_move");
}

TEST(Client, ListActionSignaturesResultsAreSorted) {
  Mock mock = CreateMockClient();

  // Prepare mock method.
  intrinsic_proto::icon::v1::ListActionSignaturesRequest expected_request;
  intrinsic_proto::icon::v1::ListActionSignaturesResponse response;
  response.add_action_signatures()->set_action_type_name("action_c");
  response.add_action_signatures()->set_action_type_name("action_a");
  response.add_action_signatures()->set_action_type_name("action_b");
  EXPECT_CALL(*mock.stub,
              ListActionSignatures(_, EqualsProto(expected_request), _))
      .WillOnce(DoAll(SetArgPointee<2>(response), Return(::grpc::Status::OK)));

  // Invoke it through client.
  ASSERT_OK_AND_ASSIGN(
      std::vector<intrinsic_proto::icon::v1::ActionSignature> results,
      mock.client->ListActionSignatures());

  // Check that results are sorted.
  EXPECT_EQ(results.size(), 3);
  EXPECT_THAT(results.at(0).action_type_name(), "action_a");
  EXPECT_THAT(results.at(1).action_type_name(), "action_b");
  EXPECT_THAT(results.at(2).action_type_name(), "action_c");
}

TEST(Client, ListActionSignatureGrpcFails) {
  Mock mock = CreateMockClient();

  EXPECT_CALL(*mock.stub, ListActionSignatures(_, _, _))
      .WillOnce(
          Return(::grpc::Status(grpc::StatusCode::CANCELLED, "cancelled")));

  EXPECT_THAT(mock.client->ListActionSignatures(),
              StatusIs(absl::StatusCode::kCancelled, HasSubstr("cancelled")));
}

TEST(Client, ListCompatiblePartsSucceeds) {
  Mock mock = CreateMockClient();

  // Prepare mock method.
  intrinsic_proto::icon::v1::ListCompatiblePartsRequest expected_request;
  expected_request.add_action_type_names("fake_action");
  intrinsic_proto::icon::v1::ListCompatiblePartsResponse response;
  response.add_parts("left_arm");
  response.add_parts("right_arm");
  EXPECT_CALL(*mock.stub,
              ListCompatibleParts(_, EqualsProto(expected_request), _))
      .WillOnce(DoAll(SetArgPointee<2>(response), Return(::grpc::Status::OK)));

  // Invoke it through client.
  ASSIGN_OR_DIE(std::vector<std::string> parts,
                mock.client->ListCompatibleParts({"fake_action"}));

  // Check results.
  EXPECT_THAT(parts,
              ElementsAre(std::string("left_arm"), std::string("right_arm")));
}

TEST(Client, ListCompatiblePartsGrpcFails) {
  Mock mock = CreateMockClient();

  EXPECT_CALL(*mock.stub, ListCompatibleParts(_, _, _))
      .WillOnce(
          Return(::grpc::Status(grpc::StatusCode::CANCELLED, "cancelled")));

  EXPECT_THAT(mock.client->ListCompatibleParts({"fake_action"}),
              StatusIs(absl::StatusCode::kCancelled, HasSubstr("cancelled")));
}

TEST(Client, ListPartsSucceeds) {
  Mock mock = CreateMockClient();

  // Prepare mock method.
  intrinsic_proto::icon::v1::ListPartsRequest expected_request;
  intrinsic_proto::icon::v1::ListPartsResponse response;
  response.add_parts("left_arm");
  response.add_parts("right_arm");
  EXPECT_CALL(*mock.stub, ListParts(_, EqualsProto(expected_request), _))
      .WillOnce(DoAll(SetArgPointee<2>(response), Return(::grpc::Status::OK)));

  // Invoke it through client.
  ASSIGN_OR_DIE(std::vector<std::string> parts, mock.client->ListParts());

  // Check results.
  EXPECT_THAT(parts,
              ElementsAre(std::string("left_arm"), std::string("right_arm")));
}

TEST(Client, ListPartsGrpcFails) {
  Mock mock = CreateMockClient();

  EXPECT_CALL(*mock.stub, ListParts(_, _, _))
      .WillOnce(
          Return(::grpc::Status(grpc::StatusCode::CANCELLED, "cancelled")));

  EXPECT_THAT(mock.client->ListParts(),
              StatusIs(absl::StatusCode::kCancelled, HasSubstr("cancelled")));
}

TEST(Client, EnableSucceeds) {
  Mock mock = CreateMockClient();
  EXPECT_CALL(*mock.stub, Enable(_, _, _)).WillOnce(Return(::grpc::Status::OK));
  EXPECT_OK(mock.client->Enable());
}

TEST(Client, EnableForwardsErrors) {
  Mock mock = CreateMockClient();
  EXPECT_CALL(*mock.stub, Enable(_, _, _))
      .WillOnce(Return(ToGrpcStatus(absl::PermissionDeniedError("Oh no!"))));
  EXPECT_THAT(mock.client->Enable(),
              StatusIs(absl::StatusCode::kPermissionDenied, "Oh no!"));
}

TEST(Client, DisableSucceeds) {
  Mock mock = CreateMockClient();
  EXPECT_CALL(*mock.stub,
              Disable(_, EqualsProto(R"pb(group: ALL_HARDWARE)pb"), _))
      .WillOnce(Return(::grpc::Status::OK));
  EXPECT_OK(mock.client->Disable());
}

TEST(Client, DisableForwardsErrors) {
  Mock mock = CreateMockClient();
  EXPECT_CALL(*mock.stub, Disable(_, _, _))
      .WillOnce(Return(ToGrpcStatus(absl::PermissionDeniedError("Oh no!"))));
  EXPECT_THAT(mock.client->Disable(),
              StatusIs(absl::StatusCode::kPermissionDenied, "Oh no!"));
}

TEST(Client, ClearFaultsSucceeds) {
  Mock mock = CreateMockClient();
  EXPECT_CALL(*mock.stub, ClearFaults(_, _, _))
      .WillOnce(Return(::grpc::Status::OK));
  EXPECT_OK(mock.client->ClearFaults());
}

TEST(Client, ClearFaultsForwardsErrors) {
  Mock mock = CreateMockClient();
  EXPECT_CALL(*mock.stub, ClearFaults(_, _, _))
      .WillOnce(Return(ToGrpcStatus(absl::PermissionDeniedError("Oh no!"))));
  EXPECT_THAT(mock.client->ClearFaults(),
              StatusIs(absl::StatusCode::kPermissionDenied, "Oh no!"));
}

TEST(Client, GetOperationalStatusSucceeds) {
  Mock mock = CreateMockClient();
  intrinsic_proto::icon::v1::GetOperationalStatusResponse resp;
  resp.mutable_operational_status()->set_state(
      intrinsic_proto::icon::v1::OperationalState::FAULTED);
  resp.mutable_operational_status()->set_fault_reason("foo");
  EXPECT_CALL(*mock.stub, GetOperationalStatus)
      .WillOnce(DoAll(SetArgPointee<2>(resp), Return(::grpc::Status::OK)));
  ASSERT_OK_AND_ASSIGN(OperationalStatus operational_status,
                       mock.client->GetOperationalStatus());
  EXPECT_TRUE(IsFaulted(operational_status));
  EXPECT_EQ(operational_status.fault_reason(), "foo");
}

TEST(Client, GetOperationalStatusForwardsErrors) {
  Mock mock = CreateMockClient();
  EXPECT_CALL(*mock.stub, GetOperationalStatus)
      .WillOnce(Return(ToGrpcStatus(absl::PermissionDeniedError("Oh no!"))));
  EXPECT_THAT(mock.client->GetOperationalStatus(),
              StatusIs(absl::StatusCode::kPermissionDenied, "Oh no!"));
}

TEST(Client, GetCellControlHardwareStatusSucceeds) {
  Mock mock = CreateMockClient();
  intrinsic_proto::icon::v1::GetOperationalStatusResponse resp;
  resp.mutable_cell_control_hardware_status()->set_state(
      intrinsic_proto::icon::v1::OperationalState::FAULTED);
  resp.mutable_cell_control_hardware_status()->set_fault_reason("foo");
  EXPECT_CALL(*mock.stub, GetOperationalStatus)
      .WillOnce(DoAll(SetArgPointee<2>(resp), Return(::grpc::Status::OK)));
  ASSERT_OK_AND_ASSIGN(OperationalStatus cell_control_hardware_status,
                       mock.client->GetCellControlHardwareStatus());
  EXPECT_TRUE(IsFaulted(cell_control_hardware_status));
  EXPECT_EQ(cell_control_hardware_status.fault_reason(), "foo");
}

TEST(Client, GetCellControlHardwareStatusForwardsErrors) {
  Mock mock = CreateMockClient();
  EXPECT_CALL(*mock.stub, GetOperationalStatus)
      .WillOnce(Return(ToGrpcStatus(absl::PermissionDeniedError("Oh no!"))));
  EXPECT_THAT(mock.client->GetCellControlHardwareStatus(),
              StatusIs(absl::StatusCode::kPermissionDenied, "Oh no!"));
}

TEST(Client, GetPartPropertiesSucceeds) {
  Mock mock = CreateMockClient();
  intrinsic_proto::icon::v1::GetPartPropertiesResponse resp;
  EXPECT_OK(intrinsic::FromAbslTime(absl::FromUnixSeconds(12345),
                                    resp.mutable_timestamp_wall()));
  EXPECT_OK(intrinsic::FromAbslDuration(absl::Seconds(4567),
                                        resp.mutable_timestamp_control()));

  intrinsic_proto::icon::v1::PartPropertyValues property_values_proto;
  property_values_proto.mutable_property_values_by_name()->insert(
      {"double_prop", ToProto(PartPropertyValue(1.234))});
  resp.mutable_part_properties_by_part_name()->insert(
      {"arm", property_values_proto});
  EXPECT_CALL(*mock.stub, GetPartProperties(_, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(resp), Return(::grpc::Status::OK)));
  ASSERT_OK_AND_ASSIGN(TimestampedPartProperties properties,
                       mock.client->GetPartProperties());
  EXPECT_EQ(properties.timestamp_wall, absl::FromUnixSeconds(12345));
  EXPECT_EQ(properties.timestamp_control, absl::Seconds(4567));
  EXPECT_THAT(
      properties.properties,
      ElementsAre(Pair("arm", ElementsAre(Pair("double_prop",
                                               VariantWith<double>(1.234))))));
}

TEST(Client, SetPartPropertiesSucceeds) {
  Mock mock = CreateMockClient();

  intrinsic_proto::icon::v1::SetPartPropertiesRequest req;
  EXPECT_CALL(*mock.stub, SetPartProperties(_, _, _))
      .WillOnce(
          DoAll(SaveArg<1>(&req),
                SetArgPointee<2>(
                    intrinsic_proto::icon::v1::SetPartPropertiesResponse()),
                Return(::grpc::Status::OK)));

  PartPropertyMap property_values{
      .properties = {{"arm", {{"double_prop", 1.234}}}}};
  EXPECT_OK(mock.client->SetPartProperties(property_values));

  ASSERT_THAT(req.part_properties_by_part_name(), ElementsAre(Key("arm")));
  EXPECT_THAT(
      req.part_properties_by_part_name().at("arm").property_values_by_name(),
      ElementsAre(
          Pair("double_prop", EqualsProto(ToProto(PartPropertyValue(1.234))))));
}

TEST(Client, GetPayloadSucceeds) {
  Mock mock = CreateMockClient();
  ASSERT_OK_AND_ASSIGN(
      RobotPayload want_payload,
      RobotPayload::Create(1.234, Pose3d(), eigenmath::Matrix3d::Identity()));
  intrinsic_proto::icon::v1::GetPayloadResponse resp;

  *resp.mutable_payload() = intrinsic::ToProto(want_payload);

  EXPECT_CALL(*mock.stub, GetPayload(_, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(resp), Return(::grpc::Status::OK)));

  ASSERT_OK_AND_ASSIGN(std::optional<RobotPayload> payload,
                       mock.client->GetPayload("arm", "my_payload"));
  EXPECT_EQ(payload, want_payload);
}

TEST(Client, GetNulloptPayloadSucceeds) {
  Mock mock = CreateMockClient();
  intrinsic_proto::icon::v1::GetPayloadResponse resp;

  EXPECT_CALL(*mock.stub, GetPayload(_, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(resp), Return(::grpc::Status::OK)));

  ASSERT_OK_AND_ASSIGN(std::optional<RobotPayload> payload,
                       mock.client->GetPayload("arm", "my_payload"));
  EXPECT_FALSE(payload.has_value());
}

TEST(Client, SetPayloadSucceeds) {
  Mock mock = CreateMockClient();

  intrinsic_proto::icon::v1::SetPayloadRequest req;
  EXPECT_CALL(*mock.stub, SetPayload(_, _, _))
      .WillOnce(DoAll(
          SaveArg<1>(&req),
          SetArgPointee<2>(intrinsic_proto::icon::v1::SetPayloadResponse()),
          Return(::grpc::Status::OK)));

  RobotPayload payload;
  ASSERT_OK(payload.SetMass(1.234));
  EXPECT_OK(mock.client->SetPayload(payload, "arm", "my_payload"));

  EXPECT_EQ(req.part_name(), "arm");
  EXPECT_EQ(req.payload_name(), "my_payload");
  EXPECT_EQ(req.payload().mass_kg(), 1.234);
}

}  // namespace
}  // namespace icon
}  // namespace intrinsic
