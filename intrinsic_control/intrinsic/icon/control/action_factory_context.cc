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

#include "intrinsic/icon/control/action_factory_context.h"

#include <any>
#include <array>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "intrinsic/icon/control/realtime_signal_types.h"
#include "intrinsic/icon/control/slot_types.h"
#include "intrinsic/icon/control/streaming_io_registry.h"
#include "intrinsic/icon/control/streaming_io_types.h"
#include "intrinsic/icon/proto/v1/types.pb.h"
#include "intrinsic/kinematics/types/joint_trajectories.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic::icon {

const ::intrinsic_proto::icon::v1::ServerConfig&
ActionFactoryContext::ServerConfig() const {
  return server_config_;
}

absl::StatusOr<StreamingInputId>
ActionFactoryContext::AddStreamingInputParserAny(
    absl::string_view input_name, absl::string_view proto_type_name,
    std::function<absl::StatusOr<std::any>(
        const ::google::protobuf::Any& streaming_input)>
        parser) {
  INTR_ASSIGN_OR_RETURN(StreamingInputId id,
                        streaming_io_registry_.AddStreamingInputParserAny(
                            input_name, proto_type_name, std::move(parser)));
  used_streaming_inputs_.insert(std::string(input_name));
  return id;
}

absl::Status ActionFactoryContext::AddStreamingOutputConverterAny(
    absl::string_view proto_type_name, size_t realtime_type_size,
    std::function<absl::StatusOr<::google::protobuf::Any>(
        const std::array<char,
                         StreamingIoRegistry::kMaxStreamingOutputSizeBytes>&
            streaming_output)>
        converter) {
  INTR_RETURN_IF_ERROR(streaming_io_registry_.AddStreamingOutputConverterAny(
      proto_type_name, realtime_type_size, std::move(converter)));
  streaming_output_set_ = true;
  return absl::OkStatus();
}

absl::StatusOr<SlotInfo> ActionFactoryContext::GetSlotInfo(
    absl::string_view slot_name) {
  if (!signature_.part_slot_infos().contains(slot_name)) {
    return absl::NotFoundError(absl::StrCat(
        "Action type '", signature_.action_type_name(),
        "' does not declare slot '", slot_name, "' in its signature."));
  }
  if (signature_.part_slot_infos().at(slot_name).is_optional()) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Slot '", slot_name, "' is an optional slot for action type '",
        signature_.action_type_name(),
        "'. Use GetOptionalSlotInfo() instead!"));
  }
  auto slot_info = slot_name_to_slot_info_.find(slot_name);
  if (slot_info == slot_name_to_slot_info_.end()) {
    return absl::NotFoundError(
        absl::StrCat("Instance of Action type '", signature_.action_type_name(),
                     "' does not have access to slot '", slot_name,
                     "'. This is a configuration error, so check whether the "
                     "Action has the correct SlotPartMap."));
  }
  used_slot_names_.insert(std::string(slot_name));
  return slot_info->second;
}

absl::StatusOr<std::optional<SlotInfo>>
ActionFactoryContext::GetOptionalSlotInfo(absl::string_view slot_name) {
  auto slot_info = signature_.part_slot_infos().find(slot_name);
  if (slot_info == signature_.part_slot_infos().end()) {
    return absl::NotFoundError(absl::StrCat(
        "Action type '", signature_.action_type_name(),
        "' does not declare slot '", slot_name, "' in its signature."));
  }
  if (!slot_info->second.is_optional()) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Slot '", slot_name, "' is not an optional slot for action type '",
        signature_.action_type_name(), "'. Use GetSlotInfo() instead!"));
  }

  used_slot_names_.insert(std::string(slot_name));
  if (!slot_name_to_slot_info_.contains(slot_name)) {
    return std::nullopt;
  }
  return slot_name_to_slot_info_.at(slot_name);
}

absl::StatusOr<RealtimeSignalId> ActionFactoryContext::GetRealtimeSignalId(
    absl::string_view realtime_signal_name) {
  auto signal_name_and_id =
      realtime_signal_name_to_signal_id_.find(realtime_signal_name);
  if (signal_name_and_id == realtime_signal_name_to_signal_id_.end()) {
    return absl::NotFoundError(absl::StrCat(
        "Action type '", signature_.action_type_name(),
        "' does not declare realtime signal '", realtime_signal_name,
        "' in its signature. Available signals: [",
        absl::StrJoin(realtime_signal_name_to_signal_id_, ", ",
                      [](std::string* str, const auto& name_and_id) {
                        absl::StrAppend(str, name_and_id.first);
                      }),
        "]"));
  }
  used_realtime_signals_.insert(std::string(realtime_signal_name));
  return signal_name_and_id->second;
}

void ActionFactoryContext::StoreTrajectory(
    const JointTrajectoryPVA& trajectory) {
  trajectory_map_[action_instance_id_] = trajectory;
}

absl::Status ActionFactoryContext::Validate() const {
  if (signature_.has_streaming_output_info() && !streaming_output_set_) {
    return absl::FailedPreconditionError(absl::StrCat(
        "Factory for Action type '", signature_.action_type_name(),
        "' did not register a converter for streaming output '",
        signature_.streaming_output_info().parameter_name(), "' of type '",
        signature_.streaming_output_info().value_message_type(), "'"));
  }
  for (const ::intrinsic_proto::icon::v1::ActionSignature::ParameterInfo&
           streaming_input_info : signature_.streaming_input_infos()) {
    if (!used_streaming_inputs_.contains(
            streaming_input_info.parameter_name())) {
      return absl::FailedPreconditionError(absl::StrCat(
          "Factory for Action type '", signature_.action_type_name(),
          "' did not register a parser for streaming input '",
          streaming_input_info.parameter_name(), "' of type '",
          streaming_input_info.value_message_type(), "'"));
    }
  }
  for (const auto& [slot_name, slot_info] : signature_.part_slot_infos()) {
    if (!used_slot_names_.contains(slot_name)) {
      return absl::FailedPreconditionError(absl::StrCat(
          "Factory for Action type '", signature_.action_type_name(),
          "' did not query the SlotInfo object for Slot '", slot_name, "'"));
    }
  }
  for (const intrinsic_proto::icon::v1::ActionSignature::RealtimeSignalInfo&
           signal_info : signature_.realtime_signal_infos()) {
    if (!used_realtime_signals_.contains(signal_info.signal_name())) {
      return absl::FailedPreconditionError(absl::StrCat(
          "Factory for Action type '", signature_.action_type_name(),
          "' did not query the RealtimeSignalId object for signal '",
          signal_info.signal_name(), "'"));
    }
  }
  return absl::OkStatus();
}

}  // namespace intrinsic::icon
