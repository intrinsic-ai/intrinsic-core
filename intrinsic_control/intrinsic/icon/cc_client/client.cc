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

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "grpcpp/client_context.h"
#include "grpcpp/support/time.h"
#include "intrinsic/icon/cc_client/operational_status.h"
#include "intrinsic/icon/cc_client/robot_config.h"
#include "intrinsic/icon/common/part_properties.h"
#include "intrinsic/icon/common/slot_part_map.h"
#include "intrinsic/icon/control/logging_mode.h"
#include "intrinsic/icon/proto/v1/service.grpc.pb.h"
#include "intrinsic/icon/proto/v1/service.pb.h"
#include "intrinsic/icon/proto/v1/types.pb.h"
#include "intrinsic/util/grpc/channel_interface.h"
#include "intrinsic/util/proto_time.h"
#include "intrinsic/util/status/status_conversion_grpc.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/robot_payload/robot_payload.h"

namespace intrinsic {
namespace icon {

Client::Client(std::shared_ptr<ChannelInterface> icon_channel)
    : channel_(icon_channel),
      stub_(intrinsic_proto::icon::v1::IconApi::NewStub(
          icon_channel->GetChannel())),
      timeout_(kClientDefaultTimeout),
      client_context_factory_(icon_channel->GetClientContextFactory()) {}

Client::Client(
    std::unique_ptr<intrinsic_proto::icon::v1::IconApi::StubInterface> stub,
    ClientContextFactory client_context_factory)
    : channel_(nullptr),
      stub_(std::move(stub)),
      timeout_(kClientDefaultTimeout),
      client_context_factory_(std::move(client_context_factory)) {}

absl::StatusOr<intrinsic_proto::icon::v1::ActionSignature>
Client::GetActionSignatureByName(absl::string_view action_type_name) const {
  std::unique_ptr<::grpc::ClientContext> context = client_context_factory_();
  context->set_deadline(::grpc::DeadlineFromDuration(timeout_));
  intrinsic_proto::icon::v1::GetActionSignatureByNameRequest request;
  // NOLINTNEXTLINE(google3-readability-redundant-string-conversions)
  request.set_name(std::string(action_type_name));
  intrinsic_proto::icon::v1::GetActionSignatureByNameResponse response;
  INTR_RETURN_IF_ERROR(ToAbslStatus(
      stub_->GetActionSignatureByName(context.get(), request, &response)));
  if (!response.has_action_signature()) {
    return absl::NotFoundError(
        absl::StrCat("Could not get action signature: action type \"",
                     action_type_name, "\" not found."));
  }
  return response.action_signature();
}

absl::StatusOr<RobotConfig> Client::GetConfig() const {
  std::unique_ptr<::grpc::ClientContext> context = client_context_factory_();
  context->set_deadline(::grpc::DeadlineFromDuration(timeout_));
  intrinsic_proto::icon::v1::GetConfigRequest request;
  intrinsic_proto::icon::v1::GetConfigResponse response;
  INTR_RETURN_IF_ERROR(
      ToAbslStatus(stub_->GetConfig(context.get(), request, &response)));
  return RobotConfig(response);
}

absl::StatusOr<intrinsic_proto::icon::v1::GetStatusResponse> Client::GetStatus()
    const {
  std::unique_ptr<::grpc::ClientContext> context = client_context_factory_();
  context->set_deadline(::grpc::DeadlineFromDuration(timeout_));
  intrinsic_proto::icon::v1::GetStatusRequest request;
  intrinsic_proto::icon::v1::GetStatusResponse response;
  INTR_RETURN_IF_ERROR(
      ToAbslStatus(stub_->GetStatus(context.get(), request, &response)));
  return response;
}

absl::StatusOr<intrinsic_proto::icon::PartStatus> Client::GetSinglePartStatus(
    absl::string_view part_name) const {
  INTR_ASSIGN_OR_RETURN(
      intrinsic_proto::icon::v1::GetStatusResponse robot_status, GetStatus());
  auto part_status_it = robot_status.part_status().find(std::string(part_name));
  if (part_status_it == robot_status.part_status().end()) {
    return absl::NotFoundError(
        absl::StrCat("Robot status does not contain Part status for Part '",
                     part_name, "'"));
  }
  return part_status_it->second;
}

absl::Status Client::RestartServer() const {
  std::unique_ptr<::grpc::ClientContext> context = client_context_factory_();
  context->set_deadline(::grpc::DeadlineFromDuration(timeout_));
  google::protobuf::Empty resp;
  return ToAbslStatus(stub_->RestartServer(context.get(), {}, &resp));
}

absl::StatusOr<bool> Client::IsActionCompatible(
    absl::string_view part_name, absl::string_view action_type_name) const {
  std::unique_ptr<::grpc::ClientContext> context = client_context_factory_();
  context->set_deadline(::grpc::DeadlineFromDuration(timeout_));
  intrinsic_proto::icon::v1::IsActionCompatibleRequest request;
  // NOLINTNEXTLINE(google3-readability-redundant-string-conversions)
  request.set_part_name(std::string(part_name));
  // NOLINTNEXTLINE(google3-readability-redundant-string-conversions)
  request.set_action_type_name(std::string(action_type_name));
  intrinsic_proto::icon::v1::IsActionCompatibleResponse response;
  INTR_RETURN_IF_ERROR(ToAbslStatus(
      stub_->IsActionCompatible(context.get(), request, &response)));
  return response.is_compatible();
}

absl::StatusOr<bool> Client::IsActionCompatible(
    const SlotPartMap& slot_part_map,
    absl::string_view action_type_name) const {
  std::unique_ptr<::grpc::ClientContext> context = client_context_factory_();
  context->set_deadline(::grpc::DeadlineFromDuration(timeout_));
  intrinsic_proto::icon::v1::IsActionCompatibleRequest request;
  *request.mutable_slot_part_map() = ToProto(slot_part_map);
  // NOLINTNEXTLINE(google3-readability-redundant-string-conversions)
  request.set_action_type_name(std::string(action_type_name));
  intrinsic_proto::icon::v1::IsActionCompatibleResponse response;
  INTR_RETURN_IF_ERROR(ToAbslStatus(
      stub_->IsActionCompatible(context.get(), request, &response)));
  return response.is_compatible();
}

absl::StatusOr<std::vector<intrinsic_proto::icon::v1::ActionSignature>>
Client::ListActionSignatures() const {
  std::unique_ptr<::grpc::ClientContext> context = client_context_factory_();
  context->set_deadline(::grpc::DeadlineFromDuration(timeout_));
  intrinsic_proto::icon::v1::ListActionSignaturesRequest request;
  intrinsic_proto::icon::v1::ListActionSignaturesResponse response;
  INTR_RETURN_IF_ERROR(ToAbslStatus(
      stub_->ListActionSignatures(context.get(), request, &response)));
  std::vector<intrinsic_proto::icon::v1::ActionSignature> out(
      response.action_signatures().begin(), response.action_signatures().end());
  std::sort(
      out.begin(), out.end(),
      [](const intrinsic_proto::icon::v1::ActionSignature& a,
         const intrinsic_proto::icon::v1::ActionSignature& b) {
        if ((&a != &b) && (a.action_type_name() == b.action_type_name())) {
          LOG(WARNING) << "Server returned duplicate action type name \""
                       << a.action_type_name() << "\"";
        }
        return a.action_type_name() < b.action_type_name();
      });
  return out;
}

absl::StatusOr<std::vector<std::string>> Client::ListCompatibleParts(
    absl::Span<const std::string> action_type_names) const {
  std::unique_ptr<::grpc::ClientContext> context = client_context_factory_();
  context->set_deadline(::grpc::DeadlineFromDuration(timeout_));
  intrinsic_proto::icon::v1::ListCompatiblePartsRequest request;
  *request.mutable_action_type_names() = {action_type_names.begin(),
                                          action_type_names.end()};
  intrinsic_proto::icon::v1::ListCompatiblePartsResponse response;
  INTR_RETURN_IF_ERROR(ToAbslStatus(
      stub_->ListCompatibleParts(context.get(), request, &response)));
  return std::vector<std::string>(response.parts().begin(),
                                  response.parts().end());
}

absl::StatusOr<std::vector<std::string>> Client::ListParts() const {
  std::unique_ptr<::grpc::ClientContext> context = client_context_factory_();
  context->set_deadline(::grpc::DeadlineFromDuration(timeout_));
  intrinsic_proto::icon::v1::ListPartsResponse response;
  INTR_RETURN_IF_ERROR(ToAbslStatus(stub_->ListParts(
      context.get(), intrinsic_proto::icon::v1::ListPartsRequest(),
      &response)));
  return std::vector<std::string>(response.parts().begin(),
                                  response.parts().end());
}

absl::Status Client::Enable() const {
  std::unique_ptr<::grpc::ClientContext> context = client_context_factory_();
  context->set_deadline(::grpc::DeadlineFromDuration(timeout_));
  intrinsic_proto::icon::v1::EnableRequest req;
  intrinsic_proto::icon::v1::EnableResponse resp;
  return ToAbslStatus(stub_->Enable(context.get(), req, &resp));
}

absl::Status Client::Disable(HardwareGroup group) const {
  std::unique_ptr<::grpc::ClientContext> context = client_context_factory_();
  context->set_deadline(::grpc::DeadlineFromDuration(timeout_));
  intrinsic_proto::icon::v1::DisableRequest req;
  if (group == kOperationalHardwareOnly) {
    req.set_group(
        intrinsic_proto::icon::v1::DisableRequest::OPERATIONAL_HARDWARE_ONLY);
  } else {
    req.set_group(intrinsic_proto::icon::v1::DisableRequest::ALL_HARDWARE);
  }
  intrinsic_proto::icon::v1::DisableResponse resp;
  return ToAbslStatus(stub_->Disable(context.get(), req, &resp));
}

absl::Status Client::ClearFaults() const {
  std::unique_ptr<::grpc::ClientContext> context = client_context_factory_();
  context->set_deadline(::grpc::DeadlineFromDuration(timeout_));
  intrinsic_proto::icon::v1::ClearFaultsRequest req;
  intrinsic_proto::icon::v1::ClearFaultsResponse resp;
  return ToAbslStatus(stub_->ClearFaults(context.get(), req, &resp));
}

absl::StatusOr<OperationalStatus> Client::GetOperationalStatus() const {
  std::unique_ptr<::grpc::ClientContext> context = client_context_factory_();
  context->set_deadline(::grpc::DeadlineFromDuration(timeout_));
  intrinsic_proto::icon::v1::GetOperationalStatusRequest req;
  intrinsic_proto::icon::v1::GetOperationalStatusResponse resp;
  INTR_RETURN_IF_ERROR(
      ToAbslStatus(stub_->GetOperationalStatus(context.get(), req, &resp)));
  return FromProto(resp.operational_status());
}

absl::StatusOr<OperationalStatus> Client::GetCellControlHardwareStatus() const {
  std::unique_ptr<::grpc::ClientContext> context = client_context_factory_();
  context->set_deadline(::grpc::DeadlineFromDuration(timeout_));
  intrinsic_proto::icon::v1::GetOperationalStatusRequest req;
  intrinsic_proto::icon::v1::GetOperationalStatusResponse resp;
  INTR_RETURN_IF_ERROR(
      ToAbslStatus(stub_->GetOperationalStatus(context.get(), req, &resp)));
  return FromProto(resp.cell_control_hardware_status());
}

absl::Status Client::SetSpeedOverride(double new_speed_override) {
  std::unique_ptr<::grpc::ClientContext> context = client_context_factory_();
  context->set_deadline(::grpc::DeadlineFromDuration(timeout_));
  intrinsic_proto::icon::v1::SetSpeedOverrideRequest req;
  req.set_override_factor(new_speed_override);
  intrinsic_proto::icon::v1::SetSpeedOverrideResponse resp;
  return ToAbslStatus(stub_->SetSpeedOverride(context.get(), req, &resp));
}

absl::StatusOr<double> Client::GetSpeedOverride() const {
  std::unique_ptr<::grpc::ClientContext> context = client_context_factory_();
  context->set_deadline(::grpc::DeadlineFromDuration(timeout_));
  intrinsic_proto::icon::v1::GetSpeedOverrideResponse resp;
  INTR_RETURN_IF_ERROR(ToAbslStatus(stub_->GetSpeedOverride(
      context.get(), intrinsic_proto::icon::v1::GetSpeedOverrideRequest(),
      &resp)));
  return resp.override_factor();
}

absl::Status Client::SetLoggingMode(LoggingMode logging_mode) const {
  std::unique_ptr<::grpc::ClientContext> context = client_context_factory_();
  context->set_deadline(::grpc::DeadlineFromDuration(timeout_));
  intrinsic_proto::icon::v1::SetLoggingModeRequest req;
  req.set_logging_mode(ToProto(logging_mode));
  intrinsic_proto::icon::v1::SetLoggingModeResponse resp;
  return ToAbslStatus(stub_->SetLoggingMode(context.get(), req, &resp));
}

absl::StatusOr<LoggingMode> Client::GetLoggingMode() const {
  std::unique_ptr<::grpc::ClientContext> context = client_context_factory_();
  context->set_deadline(::grpc::DeadlineFromDuration(timeout_));
  intrinsic_proto::icon::v1::GetLoggingModeResponse resp;
  INTR_RETURN_IF_ERROR(ToAbslStatus(stub_->GetLoggingMode(
      context.get(), intrinsic_proto::icon::v1::GetLoggingModeRequest(),
      &resp)));
  return FromProto(resp.logging_mode());
}

absl::Status Client::SetPartProperties(
    const PartPropertyMap& property_map) const {
  std::unique_ptr<::grpc::ClientContext> context = client_context_factory_();
  context->set_deadline(::grpc::DeadlineFromDuration(timeout_));
  intrinsic_proto::icon::v1::SetPartPropertiesRequest req;
  for (const auto& [part_name, properties] : property_map.properties) {
    intrinsic_proto::icon::v1::PartPropertyValues part_properties_proto;
    for (const auto& [property_name, property_value] : properties) {
      part_properties_proto.mutable_property_values_by_name()->insert(
          {property_name, ToProto(property_value)});
    }
    req.mutable_part_properties_by_part_name()->insert(
        {part_name, std::move(part_properties_proto)});
  }
  intrinsic_proto::icon::v1::SetPartPropertiesResponse resp;
  return ToAbslStatus(stub_->SetPartProperties(context.get(), req, &resp));
}

absl::StatusOr<TimestampedPartProperties> Client::GetPartProperties() const {
  std::unique_ptr<::grpc::ClientContext> context = client_context_factory_();
  context->set_deadline(::grpc::DeadlineFromDuration(timeout_));
  intrinsic_proto::icon::v1::GetPartPropertiesRequest req;
  intrinsic_proto::icon::v1::GetPartPropertiesResponse resp;
  INTR_RETURN_IF_ERROR(
      ToAbslStatus(stub_->GetPartProperties(context.get(), req, &resp)));
  TimestampedPartProperties properties;
  INTR_ASSIGN_OR_RETURN(properties.timestamp_wall,
                        ToAbslTime(resp.timestamp_wall()));
  INTR_ASSIGN_OR_RETURN(properties.timestamp_control,
                        ToAbslDuration(resp.timestamp_control()));

  for (const auto& [part_name, properties_proto] :
       resp.part_properties_by_part_name()) {
    for (const auto& [property_name, property_value_proto] :
         properties_proto.property_values_by_name()) {
      INTR_ASSIGN_OR_RETURN(properties.properties[part_name][property_name],
                            FromProto(property_value_proto));
    }
  }

  return properties;
}



absl::StatusOr<std::optional<RobotPayload>> Client::GetPayload(
    absl::string_view part_name, absl::string_view payload_name) const {
  std::unique_ptr<::grpc::ClientContext> context = client_context_factory_();
  context->set_deadline(::grpc::DeadlineFromDuration(timeout_));
  intrinsic_proto::icon::v1::GetPayloadRequest req;
  req.set_part_name(part_name);
  req.set_payload_name(payload_name);
  intrinsic_proto::icon::v1::GetPayloadResponse resp;
  INTR_RETURN_IF_ERROR(
      ToAbslStatus(stub_->GetPayload(context.get(), req, &resp)));
  if (!resp.has_payload()) {
    return std::nullopt;
  }
  return intrinsic::FromProto(resp.payload());
}

absl::Status Client::SetPayload(const RobotPayload& payload,
                                absl::string_view part_name,
                                absl::string_view payload_name) const {
  std::unique_ptr<::grpc::ClientContext> context = client_context_factory_();
  context->set_deadline(::grpc::DeadlineFromDuration(timeout_));
  intrinsic_proto::icon::v1::SetPayloadRequest req;
  req.set_part_name(part_name);
  req.set_payload_name(payload_name);
  *req.mutable_payload() = ToProto(payload);
  intrinsic_proto::icon::v1::SetPayloadResponse resp;
  return ToAbslStatus(stub_->SetPayload(context.get(), req, &resp));
}



}  // namespace icon
}  // namespace intrinsic
