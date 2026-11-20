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

#include "intrinsic/icon/hardware_modules/kuka_rsi/kuka_system_status/kuka_eki_system_status.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "internal/testing.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/hardware_modules/kuka_rsi/kuka_rsi_util.h"
#include "intrinsic/icon/testing/mock_tcp_client.h"
#include "intrinsic/icon/utils/bitmask_enums.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/icon/utils/realtime_status_macro.h"
#include "intrinsic/icon/utils/realtime_status_matchers.h"
#include "intrinsic/icon/utils/realtime_status_or.h"
#include "intrinsic/icon/utils/xml/realtime_xml_generator.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/world/robot_payload/robot_payload.h"

using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;
using ::testing::_;
using ::testing::DoubleNear;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::Ne;
using ::testing::Pointwise;
using ::testing::Return;

namespace intrinsic::kuka {

icon::RealtimeStatusOr<std::string> CreateEkiTelemetryXml(
    const KukaEkiSystemStatus::EkiTelemetry& telemetry) {
  RealtimeXmlGenerator generator(100, 100);

  INTRINSIC_RT_ASSIGN_OR_RETURN(auto root, generator.AddRootElement("Robot"));
  INTRINSIC_RT_RETURN_IF_ERROR(
      root->AddElementWithText("Cycle", telemetry.cycle).status());
  // Must stay alive until `generator.GenerateXMLString(...)` completes because
  // `RtXmlElement::SetText` stores a non-owning `absl::string_view`.
  const std::string software_version =
      telemetry.software_version.empty()
          ? kMinimumRsiHwmSoftwareVersion.ToString()
          : telemetry.software_version;
  INTRINSIC_RT_RETURN_IF_ERROR(
      root->AddElementWithText("SoftwareVersion", software_version).status());
  const std::string protocol_version =
      telemetry.protocol_version.empty()
          ? kMinimumRsiHwmProtocolVersion.ToString()
          : telemetry.protocol_version;
  INTRINSIC_RT_RETURN_IF_ERROR(
      root->AddElementWithText("ProtocolVersion", protocol_version).status());

  INTRINSIC_RT_RETURN_IF_ERROR(
      root->AddElementWithText("RobotModel", telemetry.robot_model).status());
  INTRINSIC_RT_RETURN_IF_ERROR(
      root->AddElementWithText("SerialNo", telemetry.serial_no).status());
  INTRINSIC_RT_ASSIGN_OR_RETURN(auto last_pos, root->AddElement("LastPos"));
  std::vector<std::string>
      joint_position_strings;  // Must stay alive until XML string is written.
  joint_position_strings.reserve(kKukaNumJoints);
  for (int i = 0; i < kKukaNumJoints; ++i) {
    joint_position_strings.push_back("A" + std::to_string(i + 1));
    INTRINSIC_RT_RETURN_IF_ERROR(last_pos->AddAttributeWithValue(
        joint_position_strings.back(), telemetry.position[i]));
  }

  INTRINSIC_RT_RETURN_IF_ERROR(
      root->AddElementWithText("EmergencyStop", telemetry.emergency_stop_active)
          .status());
  INTRINSIC_RT_RETURN_IF_ERROR(
      root->AddElementWithText("EmergencyStopInternal",
                               telemetry.internal_emergency_stop_active)
          .status());
  INTRINSIC_RT_RETURN_IF_ERROR(
      root->AddElementWithText("OperatorSafetyClosed",
                               telemetry.operator_safety_closed)
          .status());
  INTRINSIC_RT_RETURN_IF_ERROR(
      root->AddElementWithText("OpMode", telemetry.op_mode).status());
  INTRINSIC_RT_RETURN_IF_ERROR(
      root->AddElementWithText("MessagesPresent", telemetry.messages_present)
          .status());
  INTRINSIC_RT_RETURN_IF_ERROR(
      root->AddElementWithText("RSIActive", telemetry.rsi_active).status());
  INTRINSIC_RT_RETURN_IF_ERROR(
      root->AddElementWithText("ProgramActive", telemetry.program_active)
          .status());
  INTRINSIC_RT_RETURN_IF_ERROR(
      root->AddElementWithText("ProgramSelected", telemetry.program_selected)
          .status());
  // Must stay alive until `generator.GenerateXMLString(...)` completes.
  const std::string messages = absl::StrJoin(telemetry.message_numbers, ";");
  INTRINSIC_RT_RETURN_IF_ERROR(
      root->AddElementWithText("Messages", messages).status());

  std::string xml_str;
  xml_str.resize(1000);
  INTRINSIC_RT_ASSIGN_OR_RETURN(
      auto size, generator.GenerateXMLString(
                     absl::MakeSpan(xml_str.data(), xml_str.size())));
  xml_str.resize(size);
  return xml_str;
}

Version CreateVersionWithDifferentPatch(Version version) {
  return Version({version.Major(), version.Minor(), version.Patch() + 1});
}

std::string CreateEkiTelemetryXmlChecked(
    const KukaEkiSystemStatus::EkiTelemetry& telemetry) {
  auto result = CreateEkiTelemetryXml(telemetry);
  CHECK(result.ok()) << result.status();
  return result.value();
}

class KukaEkiSystemStatusTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto tcp_client = std::make_unique<testing::MockTcpClient>();
    tcp_client_ = tcp_client.get();
    ON_CALL(*tcp_client, Connect(_)).WillByDefault(Return(absl::OkStatus()));
    ON_CALL(*tcp_client, Disconnect()).WillByDefault(Return());
    ON_CALL(*tcp_client, IsConnected()).WillByDefault(Return(true));
    ON_CALL(*tcp_client, Send(_, _)).WillByDefault(Return(icon::OkStatus()));
    ON_CALL(*tcp_client, Receive(_, _, _, _, _))
        .WillByDefault(Return(icon::FailedPreconditionError("")));

    EXPECT_CALL(*tcp_client, IsConnected()).WillRepeatedly(Return(true));
    EXPECT_CALL(*tcp_client, Connect(_)).WillOnce((Return(absl::OkStatus())));

    ASSERT_OK_AND_ASSIGN(kuka_eki_system_status_,
                         KukaEkiSystemStatus::Create(
                             KukaEkiClientParams{.eki_address = "localhost",
                                                 .eki_port = 54600,
                                                 .update_frequency = 10.0},
                             std::move(tcp_client)));
  }

  std::function<icon::RealtimeStatus(absl::Duration timeout,
                                     absl::Span<uint8_t> buffer,
                                     size_t& bytes_read, int recv_flags)>
  CreateEkiTelemetryMockFunction(const std::string& telemetry_str) {
    return [telemetry = telemetry_str](absl::Duration timeout,
                                       absl::Span<uint8_t> buffer,
                                       size_t& bytes_read, int recv_flags) {
      CHECK(buffer.size() >= telemetry.size());
      (void)memcpy(buffer.data(), (telemetry.data()), telemetry.size());
      bytes_read = telemetry.size();
      return icon::OkStatus();
    };
  }

  absl::StatusOr<std::string> CreateEkiCommandXml(
      std::optional<KukaPayload> payload = {}) {
    RealtimeXmlGenerator generator(100, 100);

    INTRINSIC_RT_ASSIGN_OR_RETURN(auto root,
                                  generator.AddRootElement("Intrinsic"));
    INTRINSIC_RT_RETURN_IF_ERROR(root->AddElement("Update").status());
    if (payload.has_value()) {
      INTRINSIC_RT_ASSIGN_OR_RETURN(auto payload_node,
                                    root->AddElement("Payload"));
      INTRINSIC_RT_RETURN_IF_ERROR(payload_node->AddElement("Apply").status());
      INTRINSIC_RT_RETURN_IF_ERROR(
          payload_node->AddAttributeWithValue("Mass", payload->mass));
      INTRINSIC_RT_ASSIGN_OR_RETURN(auto com_node,
                                    payload_node->AddElement("CM"));
      INTRINSIC_RT_RETURN_IF_ERROR(
          com_node->AddAttributeWithValue("X", payload->center_of_gravity_x));
      INTRINSIC_RT_RETURN_IF_ERROR(
          com_node->AddAttributeWithValue("Y", payload->center_of_gravity_y));
      INTRINSIC_RT_RETURN_IF_ERROR(
          com_node->AddAttributeWithValue("Z", payload->center_of_gravity_z));
      INTRINSIC_RT_RETURN_IF_ERROR(
          com_node->AddAttributeWithValue("A", payload->center_of_gravity_a));
      INTRINSIC_RT_RETURN_IF_ERROR(
          com_node->AddAttributeWithValue("B", payload->center_of_gravity_b));
      INTRINSIC_RT_RETURN_IF_ERROR(
          com_node->AddAttributeWithValue("C", payload->center_of_gravity_c));

      INTRINSIC_RT_ASSIGN_OR_RETURN(auto inertia_node,
                                    payload_node->AddElement("J"));
      INTRINSIC_RT_RETURN_IF_ERROR(
          inertia_node->AddAttributeWithValue("X", payload->inertia_xx));
      INTRINSIC_RT_RETURN_IF_ERROR(
          inertia_node->AddAttributeWithValue("Y", payload->inertia_yy));
      INTRINSIC_RT_RETURN_IF_ERROR(
          inertia_node->AddAttributeWithValue("Z", payload->inertia_zz));
    }
    std::string xml_str;
    xml_str.resize(1000);
    INTRINSIC_RT_ASSIGN_OR_RETURN(
        auto size, generator.GenerateXMLString(
                       absl::MakeSpan(xml_str.data(), xml_str.size())));
    xml_str.resize(size);
    return xml_str;
  }

 protected:
  std::unique_ptr<KukaEkiSystemStatus> kuka_eki_system_status_;
  testing::MockTcpClient* tcp_client_;
};

struct ActiveErrorFlagsTestData {
  std::string test_name;
  KukaEkiSystemStatus::EkiTelemetry eki_telemetry;
  KukaErrorFlagsMask expected_error_flags;
};

class ActiveErrorFlagsTest
    : public KukaEkiSystemStatusTest,
      public ::testing::WithParamInterface<ActiveErrorFlagsTestData> {};

TEST_P(ActiveErrorFlagsTest, CorrectActiveErrorFlags) {
  ASSERT_OK_AND_ASSIGN(std::string default_eki_command_xml,
                       CreateEkiCommandXml());
  EXPECT_CALL(*tcp_client_,
              Send(absl::MakeSpan(reinterpret_cast<const uint8_t*>(
                                      default_eki_command_xml.data()),
                                  default_eki_command_xml.size()),
                   _))
      .WillRepeatedly(Return(icon::OkStatus()));

  // Allocate as shared since the lambda will outlive this test scope and might
  // be called from the test suite.
  auto cycle = std::make_shared<size_t>(1);
  EXPECT_CALL(*tcp_client_, Receive(_, _, _, _, _))
      .WillRepeatedly([cycle, telemetry = GetParam().eki_telemetry](
                          absl::Duration timeout, bool recv_until_buffer_full,
                          int recv_flags, absl::Span<uint8_t> buffer,
                          size_t& bytes_read) {
        KukaEkiSystemStatus::EkiTelemetry adjusted_telemetry = telemetry;
        adjusted_telemetry.cycle = (*cycle)++;
        std::string eki_telemetry_string =
            CreateEkiTelemetryXmlChecked(adjusted_telemetry);
        CHECK(buffer.size() >= eki_telemetry_string.size());
        (void)memcpy(buffer.data(), (eki_telemetry_string.data()),
                     eki_telemetry_string.size());

        bytes_read = eki_telemetry_string.size();
        return icon::OkStatus();
      });
  kuka_eki_system_status_->WaitForNewData(absl::Seconds(10));

  // Sleep a bit more to make sure that the TCP packet has been processed.
  absl::SleepFor(absl::Seconds(1));
  std::vector<std::string> active_error_flag_strings;
  if (kuka_eki_system_status_->ActiveErrorFlags().ok()) {
    for (size_t i = 0; i < 32; i++) {
      auto value = static_cast<KukaErrorFlagsMask>(1 << i);
      if (HasBitSet(*kuka_eki_system_status_->ActiveErrorFlags(), value)) {
        active_error_flag_strings.push_back(
            std::string(KukaErrorFlagsMaskToString(value)));
      }
    }
  }
  EXPECT_THAT(kuka_eki_system_status_->ActiveErrorFlags(),
              IsOkAndHolds(GetParam().expected_error_flags))
      << "Active flags: " << absl::StrJoin(active_error_flag_strings, ", ");
  kuka_eki_system_status_
      .reset();  // This instance needs to be destroyed here so that it won't
                 // access the mock strings anymore.
}

INSTANTIATE_TEST_SUITE_P(
    ActiveErrorFlagsTest, ActiveErrorFlagsTest,
    ::testing::Values(
        ActiveErrorFlagsTestData{
            .test_name = "normal_operation",
            .eki_telemetry = KukaEkiSystemStatus::EkiTelemetry{},
            .expected_error_flags = KukaErrorFlagsMask::kNone},
        ActiveErrorFlagsTestData{
            .test_name = "internal_emergency_stop_active",
            .eki_telemetry =
                KukaEkiSystemStatus::EkiTelemetry{
                    .internal_emergency_stop_active = true,
                },
            .expected_error_flags =
                KukaErrorFlagsMask::kInternalEmergencyStopActive},
        ActiveErrorFlagsTestData{
            .test_name = "emergency_stop_active",
            .eki_telemetry =
                KukaEkiSystemStatus::EkiTelemetry{
                    .emergency_stop_active = true,
                },
            .expected_error_flags = KukaErrorFlagsMask::kEmergencyStopActive},
        ActiveErrorFlagsTestData{
            .test_name = "messages_present",
            .eki_telemetry =
                KukaEkiSystemStatus::EkiTelemetry{
                    .messages_present = true,
                },
            .expected_error_flags =
                {}  // Since the KUKA system variables $STOPMESS is only cleared
                    // when the $DRIVES_ON is set, there are many false
                    // positives if we report that messages are present.
                    // Therefore, `messages_present` is only checked when RSI is
                    // getting started.
        },
        ActiveErrorFlagsTestData{
            .test_name = "emergency_stop_active_and_messages_present",
            .eki_telemetry =
                KukaEkiSystemStatus::EkiTelemetry{
                    .emergency_stop_active = true,
                    .messages_present = true,
                },
            .expected_error_flags = KukaErrorFlagsMask::kEmergencyStopActive},
        ActiveErrorFlagsTestData{
            .test_name = "operator_safety_open__external",
            .eki_telemetry =
                KukaEkiSystemStatus::EkiTelemetry{
                    .operator_safety_closed = false,
                    .op_mode = OpMode::kExternal,
                    .program_active = true,
                },
            .expected_error_flags = KukaErrorFlagsMask::kOperatorSafetyOpen},
        ActiveErrorFlagsTestData{
            .test_name = "operator_safety_open__automatic",
            .eki_telemetry =
                KukaEkiSystemStatus::EkiTelemetry{
                    .operator_safety_closed = false,
                    .op_mode = OpMode::kAutomatic,
                    .program_active = true,
                },
            .expected_error_flags = KukaErrorFlagsMask::kOperatorSafetyOpen},
        ActiveErrorFlagsTestData{
            .test_name = "operator_safety_open_ignored__T1",
            .eki_telemetry =
                KukaEkiSystemStatus::EkiTelemetry{
                    .operator_safety_closed = false,
                    .op_mode = OpMode::kT1,
                },
            .expected_error_flags = KukaErrorFlagsMask::kNone},
        ActiveErrorFlagsTestData{
            .test_name = "operator_safety_open_ignored__T2",
            .eki_telemetry =
                KukaEkiSystemStatus::EkiTelemetry{
                    .operator_safety_closed = false,
                    .op_mode = OpMode::kT2,
                },
            .expected_error_flags = KukaErrorFlagsMask::kNone},
        ActiveErrorFlagsTestData{
            .test_name = "software_version_mismatch",
            .eki_telemetry =
                KukaEkiSystemStatus::EkiTelemetry{
                    .software_version = "0.0.9",
                },
            .expected_error_flags = KukaErrorFlagsMask::kVersionMismatch},
        ActiveErrorFlagsTestData{
            .test_name = "protocol_version_mismatch",
            .eki_telemetry =
                KukaEkiSystemStatus::EkiTelemetry{
                    .protocol_version = "0.0.9",
                },
            .expected_error_flags = KukaErrorFlagsMask::kVersionMismatch},
        ActiveErrorFlagsTestData{
            .test_name = "software_version_invalid",
            .eki_telemetry =
                KukaEkiSystemStatus::EkiTelemetry{
                    .software_version = "0.1",
                },
            .expected_error_flags = KukaErrorFlagsMask::kVersionMismatch},
        ActiveErrorFlagsTestData{
            .test_name = "protocol_version_invalid",
            .eki_telemetry =
                KukaEkiSystemStatus::EkiTelemetry{
                    .protocol_version = "0.1",
                },
            .expected_error_flags = KukaErrorFlagsMask::kVersionMismatch},
        ActiveErrorFlagsTestData{
            .test_name = "software_patch_mismatch_is_ok",
            .eki_telemetry =
                KukaEkiSystemStatus::EkiTelemetry{
                    .software_version = CreateVersionWithDifferentPatch(
                                            kMinimumRsiHwmSoftwareVersion)
                                            .ToString()},
            .expected_error_flags = KukaErrorFlagsMask::kNone},
        ActiveErrorFlagsTestData{
            .test_name = "protocol_patch_mismatch_is_ok",
            .eki_telemetry =
                KukaEkiSystemStatus::EkiTelemetry{
                    .protocol_version = CreateVersionWithDifferentPatch(
                                            kMinimumRsiHwmProtocolVersion)
                                            .ToString()},
            .expected_error_flags = KukaErrorFlagsMask::kNone},
        ActiveErrorFlagsTestData{
            .test_name = "software_version_mismatch_in_compatible_range_is_ok",
            .eki_telemetry =
                KukaEkiSystemStatus::EkiTelemetry{.protocol_version = "1.1.9"},
            .expected_error_flags = KukaErrorFlagsMask::kNone},
        ActiveErrorFlagsTestData{
            .test_name = "protocol_version_mismatch_in_compatible_range_is_ok",
            .eki_telemetry =
                KukaEkiSystemStatus::EkiTelemetry{.protocol_version = "1.1.9"},
            .expected_error_flags = KukaErrorFlagsMask::kNone}),
    [](const ::testing::TestParamInfo<ActiveErrorFlagsTest::ParamType>& info) {
      return info.param.test_name;
    });

TEST_F(KukaEkiSystemStatusTest, CurrentOpMode) {
  ASSERT_OK_AND_ASSIGN(std::string default_eki_command_xml,
                       CreateEkiCommandXml());
  EXPECT_CALL(*tcp_client_,
              Send(absl::MakeSpan(reinterpret_cast<const uint8_t*>(
                                      default_eki_command_xml.data()),
                                  default_eki_command_xml.size()),
                   _))
      .WillRepeatedly(Return(icon::OkStatus()));

  auto telemetry_eki_xml = CreateEkiTelemetryXmlChecked(
      KukaEkiSystemStatus::EkiTelemetry{.op_mode = OpMode::kExternal});

  // Register a call that will return the needed telemetry data.
  EXPECT_CALL(*tcp_client_, Receive(_, _, _, _, _))
      .WillRepeatedly(
          [&telemetry_eki_xml](absl::Duration timeout,
                               bool recv_until_buffer_full, int recv_flags,
                               absl::Span<uint8_t> buffer, size_t& bytes_read) {
            CHECK(buffer.size() >= telemetry_eki_xml.size());
            (void)memcpy(buffer.data(), (telemetry_eki_xml.data()),
                         telemetry_eki_xml.size());
            bytes_read = telemetry_eki_xml.size();
            return icon::OkStatus();
          });

  // Wait until the Mock EKI reports that RSI is running.
  while (!kuka_eki_system_status_->CurrentPosition().ok()) {
    absl::SleepFor(absl::Seconds(0.1));
  }
  EXPECT_THAT(kuka_eki_system_status_->CurrentOpMode(),
              IsOkAndHolds(OpMode::kExternal));

  kuka_eki_system_status_
      .reset();  // This instance needs to be destroyed here so that it won't
                 // access the mock strings anymore.
}

TEST_F(KukaEkiSystemStatusTest, ReceivePosition) {
  ASSERT_OK_AND_ASSIGN(std::string default_eki_command_xml,
                       CreateEkiCommandXml());
  EXPECT_CALL(*tcp_client_,
              Send(absl::MakeSpan(reinterpret_cast<const uint8_t*>(
                                      default_eki_command_xml.data()),
                                  default_eki_command_xml.size()),
                   _))
      .WillRepeatedly(Return(icon::OkStatus()));
  eigenmath::Vectord<kKukaNumJoints> position_deg = {0, 42, 0, 1, 0, 3};
  eigenmath::Vectord<kKukaNumJoints> position_rad = position_deg * M_PI / 180.0;

  // Register a call that will return the needed telemetry data.
  EXPECT_CALL(*tcp_client_, Receive(_, _, _, _, _))
      .WillRepeatedly([position_deg](absl::Duration timeout,
                                     bool recv_until_buffer_full,
                                     int recv_flags, absl::Span<uint8_t> buffer,
                                     size_t& bytes_read) {
        auto telemetry_eki_xml =
            CreateEkiTelemetryXmlChecked(KukaEkiSystemStatus::EkiTelemetry{
                .position = position_deg, .op_mode = OpMode::kExternal});
        CHECK(buffer.size() >= telemetry_eki_xml.size());
        (void)memcpy(buffer.data(), (telemetry_eki_xml.data()),
                     telemetry_eki_xml.size());
        bytes_read = telemetry_eki_xml.size();
        return icon::OkStatus();
      });

  // Wait until the Mock EKI reports that RSI is running.
  while (!kuka_eki_system_status_->CurrentPosition().ok()) {
    absl::SleepFor(absl::Seconds(0.1));
  }
  EXPECT_THAT(
      kuka_eki_system_status_->CurrentPosition(),
      icon::RealtimeIsOkAndHolds(Pointwise(DoubleNear(0.0001), position_rad)));

  kuka_eki_system_status_
      .reset();  // This instance needs to be destroyed here so that it won't
                 // access the mock strings anymore.
}

TEST_F(KukaEkiSystemStatusTest, ReportsFailedConnection) {
  EXPECT_CALL(*tcp_client_, IsConnected()).WillRepeatedly(Return(false));
  EXPECT_CALL(*tcp_client_, Connect(_))
      .WillRepeatedly((Return(absl::OkStatus())));

  absl::SleepFor(absl::Seconds(1));
  EXPECT_THAT(
      kuka_eki_system_status_->ErrorMessages(),
      ::testing::Contains(AllOf(HasSubstr("No connection"),
                                HasSubstr("localhost"), HasSubstr("12345"))));
  kuka_eki_system_status_
      .reset();  // This instance needs to be destroyed here so that it won't
                 // access the mock strings anymore.
}

TEST_F(KukaEkiSystemStatusTest, Reconnect) {
  ASSERT_OK_AND_ASSIGN(std::string default_eki_command_xml,
                       CreateEkiCommandXml());
  // Expect reconnect
  EXPECT_CALL(*tcp_client_, Connect(_)).WillOnce((Return(absl::OkStatus())));
  EXPECT_CALL(*tcp_client_,
              Send(absl::MakeSpan(reinterpret_cast<const uint8_t*>(
                                      default_eki_command_xml.data()),
                                  default_eki_command_xml.size()),
                   _))
      .WillRepeatedly(Return(icon::OkStatus()));

  // Register calls that will return the needed telemetry data.
  EXPECT_CALL(*tcp_client_, Receive(_, _, _, _, _))
      // Return a good value once.
      .WillOnce([](absl::Duration timeout, bool recv_until_buffer_full,
                   int recv_flags, absl::Span<uint8_t> buffer,
                   size_t& bytes_read) {
        auto telemetry_eki_xml = CreateEkiTelemetryXmlChecked(
            KukaEkiSystemStatus::EkiTelemetry{.cycle = 1});
        CHECK(buffer.size() >= telemetry_eki_xml.size());
        (void)memcpy(buffer.data(), (telemetry_eki_xml.data()),
                     telemetry_eki_xml.size());
        bytes_read = telemetry_eki_xml.size();
        return icon::OkStatus();
      })
      // Once connection failure
      .WillOnce([this](absl::Duration timeout, bool recv_until_buffer_full,
                       int recv_flags, absl::Span<uint8_t> buffer,
                       size_t& bytes_read) {
        // IsConnected() needs to return false once as well after Receive()
        // failed.
        EXPECT_CALL(*tcp_client_, IsConnected())
            .WillOnce(Return(false))
            .WillRepeatedly(Return(true));
        bytes_read = 0;
        return icon::CancelledError("");
      })
      // Now connection is good again
      .WillRepeatedly([](absl::Duration timeout, bool recv_until_buffer_full,
                         int recv_flags, absl::Span<uint8_t> buffer,
                         size_t& bytes_read) {
        auto telemetry_eki_xml_after_reconnect = CreateEkiTelemetryXmlChecked(
            KukaEkiSystemStatus::EkiTelemetry{.cycle = 42});
        CHECK(buffer.size() >= telemetry_eki_xml_after_reconnect.size());
        (void)memcpy(buffer.data(), (telemetry_eki_xml_after_reconnect.data()),
                     telemetry_eki_xml_after_reconnect.size());
        bytes_read = telemetry_eki_xml_after_reconnect.size();
        return icon::OkStatus();
      });

  // Wait until the Mock EKI reports that RSI is running.
  while (!kuka_eki_system_status_->Telemetry() ||
         kuka_eki_system_status_->Telemetry()->cycle != 42) {
    absl::SleepFor(absl::Seconds(0.1));
  }
  kuka_eki_system_status_
      .reset();  // This instance needs to be destroyed here so that it won't
                 // access the mock strings anymore.
}

TEST_F(KukaEkiSystemStatusTest, ResetFaults) {
  EXPECT_CALL(*tcp_client_, Send(_, _))
      .WillRepeatedly(Return(icon::OkStatus()));
  // Allocate as shared since the lambda will outlive this test scope and might
  // be called from the test suite.
  auto cycle = std::make_shared<size_t>(1);
  // Register calls that will return the needed telemetry data.
  EXPECT_CALL(*tcp_client_, Receive(_, _, _, _, _))
      .WillRepeatedly([cycle](absl::Duration timeout,
                              bool recv_until_buffer_full, int recv_flags,
                              absl::Span<uint8_t> buffer, size_t& bytes_read) {
        auto telemetry_eki_xml = CreateEkiTelemetryXmlChecked(
            KukaEkiSystemStatus::EkiTelemetry{.cycle = (*cycle)++,
                                              .emergency_stop_active = true,
                                              .message_numbers = {15041}});

        CHECK(buffer.size() >= telemetry_eki_xml.size());
        (void)memcpy(buffer.data(), (telemetry_eki_xml.data()),
                     telemetry_eki_xml.size());
        bytes_read = telemetry_eki_xml.size();
        return icon::OkStatus();
      });

  kuka_eki_system_status_->WaitForNewData(absl::Seconds(10));
  EXPECT_THAT(kuka_eki_system_status_->ActiveErrorFlags(),
              IsOkAndHolds(Ne(KukaErrorFlagsMask::kNone)));
  EXPECT_FALSE(kuka_eki_system_status_->ErrorMessages().empty());
  kuka_eki_system_status_->ResetFaultStorage();
  EXPECT_THAT(kuka_eki_system_status_->ActiveErrorFlags(),
              IsOkAndHolds(Eq(KukaErrorFlagsMask::kNone)));
  EXPECT_TRUE(kuka_eki_system_status_->ErrorMessages().empty());
}

TEST_F(KukaEkiSystemStatusTest, WaitForDataSucceeds) {
  // Allocate as shared since the lambda will outlive this test scope and might
  // be called from the test suite.
  auto cycle = std::make_shared<size_t>(1);
  EXPECT_CALL(*tcp_client_, Send(_, _))
      .WillRepeatedly(Return(icon::OkStatus()));
  // Register calls that will return the needed telemetry data.
  EXPECT_CALL(*tcp_client_, Receive(_, _, _, _, _))
      .WillRepeatedly([cycle](absl::Duration timeout,
                              bool recv_until_buffer_full, int recv_flags,
                              absl::Span<uint8_t> buffer, size_t& bytes_read) {
        auto telemetry_eki_xml = CreateEkiTelemetryXmlChecked(
            KukaEkiSystemStatus::EkiTelemetry{.cycle = *cycle});

        CHECK(buffer.size() >= telemetry_eki_xml.size());
        (void)memcpy(buffer.data(), (telemetry_eki_xml.data()),
                     telemetry_eki_xml.size());
        (*cycle)++;
        bytes_read = telemetry_eki_xml.size();
        return icon::OkStatus();
      });

  EXPECT_TRUE(kuka_eki_system_status_->WaitForNewData(absl::Seconds(10)));
  EXPECT_TRUE(kuka_eki_system_status_->WaitForNewData(absl::Seconds(10)));
}

TEST_F(KukaEkiSystemStatusTest, WaitForDataTimesOut) {
  EXPECT_CALL(*tcp_client_, Send(_, _))
      .WillRepeatedly(Return(icon::OkStatus()));
  // Allocate as shared since the lambda will outlive this test scope and might
  // be called from the test suite.
  auto cycle = std::make_shared<size_t>(1);
  // Register calls that will return the needed telemetry data.
  EXPECT_CALL(*tcp_client_, Receive(_, _, _, _, _))
      .WillRepeatedly([cycle](absl::Duration timeout,
                              bool recv_until_buffer_full, int recv_flags,
                              absl::Span<uint8_t> buffer, size_t& bytes_read) {
        auto telemetry_eki_xml =
            CreateEkiTelemetryXmlChecked(KukaEkiSystemStatus::EkiTelemetry{
                .cycle = *cycle,
            });
        // We only increase until reaching 2 cycles, since the first
        // WaitForNewData() should succeed.
        if (*cycle <= 2) (*cycle)++;
        CHECK(buffer.size() >= telemetry_eki_xml.size());
        (void)memcpy(buffer.data(), (telemetry_eki_xml.data()),
                     telemetry_eki_xml.size());

        bytes_read = telemetry_eki_xml.size();
        return icon::OkStatus();
      });

  EXPECT_TRUE(kuka_eki_system_status_->WaitForNewData(absl::Seconds(10)));
  EXPECT_FALSE(kuka_eki_system_status_->WaitForNewData(absl::Seconds(1)));
}

TEST_F(KukaEkiSystemStatusTest, KSSMessageReporting) {
  // Allocate as shared since the lambda will outlive this test scope and might
  // be called from the test suite.
  auto cycle = std::make_shared<size_t>(1);
  EXPECT_CALL(*tcp_client_, Send(_, _))
      .WillRepeatedly(Return(icon::OkStatus()));
  // Register calls that will return the needed telemetry data.
  EXPECT_CALL(*tcp_client_, Receive(_, _, _, _, _))
      .WillRepeatedly([cycle](absl::Duration timeout,
                              bool recv_until_buffer_full, int recv_flags,
                              absl::Span<uint8_t> buffer,

                              size_t& bytes_read) {
        auto telemetry_eki_xml =
            CreateEkiTelemetryXmlChecked(KukaEkiSystemStatus::EkiTelemetry{
                .cycle = *cycle, .message_numbers = {15041, 13015, 15040}});

        CHECK(buffer.size() >= telemetry_eki_xml.size());
        (void)memcpy(buffer.data(), (telemetry_eki_xml.data()),
                     telemetry_eki_xml.size());
        (*cycle)++;
        bytes_read = telemetry_eki_xml.size();
        return icon::OkStatus();
      });

  // Wait until the Mock EKI reports the KSS messages.
  auto start = absl::Now();
  auto error_messages = kuka_eki_system_status_->ErrorMessages();
  while (error_messages.empty()) {
    error_messages = kuka_eki_system_status_->ErrorMessages();
    if ((absl::Now() - start) > absl::Seconds(15)) {
      ADD_FAILURE() << "Timed out waiting for KSS messages";
      break;
    }
    absl::SleepFor(absl::Seconds(0.5));
  }
  // Order of messages is important since they have a defined priority.
  EXPECT_THAT(
      error_messages,
      ElementsAre(HasSubstr("15040"), HasSubstr("15041"), HasSubstr("13015")));
}

TEST_F(KukaEkiSystemStatusTest, SetPayload) {
  ASSERT_OK_AND_ASSIGN(
      auto payload, RobotPayload::Create(
                        100, Pose3d::Identity(),
                        eigenmath::Vector3d(0.001, 0.001, 0.001).asDiagonal()));
  ASSERT_OK_AND_ASSIGN(KukaPayload expected_payload, FromRobotPayload(payload));

  ASSERT_OK_AND_ASSIGN(std::string default_eki_command_xml,
                       CreateEkiCommandXml());
  ASSERT_OK_AND_ASSIGN(std::string eki_command_xml_with_payload,
                       CreateEkiCommandXml(expected_payload));
  // We expect that the payload command XML is sent once. Otherwise, the default
  // command XML is sent.
  EXPECT_CALL(*tcp_client_,
              Send(absl::MakeSpan(reinterpret_cast<const uint8_t*>(
                                      eki_command_xml_with_payload.data()),
                                  eki_command_xml_with_payload.size()),
                   _))
      .WillOnce(Return(icon::OkStatus()));
  EXPECT_CALL(*tcp_client_,
              Send(absl::MakeSpan(reinterpret_cast<const uint8_t*>(
                                      default_eki_command_xml.data()),
                                  default_eki_command_xml.size()),
                   _))
      .WillRepeatedly(Return(icon::OkStatus()));

  // Allocate as shared since the lambda will outlive this test scope and might
  // be called from the test suite.
  auto cycle = std::make_shared<std::atomic_size_t>(1);
  // Register a call that will return the needed telemetry data.
  EXPECT_CALL(*tcp_client_, Receive(_, _, _, _, _))
      .WillRepeatedly([cycle](absl::Duration timeout,
                              bool recv_until_buffer_full, int recv_flags,
                              absl::Span<uint8_t> buffer, size_t& bytes_read) {
        auto telemetry_eki_xml = CreateEkiTelemetryXmlChecked(
            KukaEkiSystemStatus::EkiTelemetry{.cycle = (*cycle)++,
                                              .protocol_version = "1.1.0",
                                              .op_mode = OpMode::kExternal,
                                              .max_payload_mass = 6.0});

        CHECK(buffer.size() >= telemetry_eki_xml.size());
        (void)memcpy(buffer.data(), (telemetry_eki_xml.data()),
                     telemetry_eki_xml.size());
        bytes_read = telemetry_eki_xml.size();
        return icon::OkStatus();
      });
  ASSERT_TRUE(kuka_eki_system_status_->WaitForNewData(absl::Seconds(10)));
  EXPECT_OK(kuka_eki_system_status_->SetPayload(payload));

  kuka_eki_system_status_
      .reset();  // This instance needs to be destroyed here so that it won't
                 // access the mock strings anymore.
}

TEST_F(KukaEkiSystemStatusTest, SetPayloadFailsIfProtocolVersionTooLow) {
  ASSERT_OK_AND_ASSIGN(auto payload,
                       RobotPayload::Create(100, Pose3d::Identity(),
                                            eigenmath::Matrix3d::Zero()));
  // Allocate as shared since the lambda will outlive this test scope and might
  // be called from the test suite.
  auto cycle = std::make_shared<std::atomic_size_t>(1);
  // Register a call that will return the needed telemetry data.
  EXPECT_CALL(*tcp_client_, Receive(_, _, _, _, _))
      .WillRepeatedly([cycle](absl::Duration timeout,
                              bool recv_until_buffer_full, int recv_flags,
                              absl::Span<uint8_t> buffer, size_t& bytes_read) {
        auto telemetry_eki_xml =
            CreateEkiTelemetryXmlChecked(KukaEkiSystemStatus::EkiTelemetry{
                .cycle = (*cycle)++,
                .protocol_version = "1.0.9",
                .op_mode = OpMode::kExternal,
            });

        CHECK(buffer.size() >= telemetry_eki_xml.size());
        (void)memcpy(buffer.data(), (telemetry_eki_xml.data()),
                     telemetry_eki_xml.size());
        bytes_read = telemetry_eki_xml.size();
        return icon::OkStatus();
      });
  ASSERT_TRUE(kuka_eki_system_status_->WaitForNewData(absl::Seconds(10)));
  EXPECT_THAT(kuka_eki_system_status_->SetPayload(payload),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       HasSubstr("Intrinsic option package too old")));

  kuka_eki_system_status_
      .reset();  // This instance needs to be destroyed here so that it won't
                 // access the mock strings anymore.
}

}  // namespace intrinsic::kuka
