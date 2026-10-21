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

#include "intrinsic/icon/actions/action_utils.h"

#include <string>
#include <utility>

#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/descriptor.pb.h"
#include "intrinsic/icon/proto/v1/types.pb.h"
#include "intrinsic/icon/release/source_location.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic {
namespace icon {

namespace {
struct FeatureInterfaceNameFormatter {
  void operator()(
      std::string* out,
      const intrinsic_proto::icon::v1::FeatureInterfaceTypes& t) const {
    absl::StrAppend(out,
                    intrinsic_proto::icon::v1::FeatureInterfaceTypes_Name(t));
  }
};
}  // namespace

absl::Status ActionSignatureBuilder::SetFixedParametersTypeImpl(
    absl::string_view fixed_parameters_message_type,
    const google::protobuf::FileDescriptorSet& fixed_parameters_descriptor_set,
    intrinsic::SourceLocation loc,
    intrinsic_proto::icon::v1::ActionSignature& dest_signature) {
  if (!dest_signature.fixed_parameters_message_type().empty()) {
    return absl::AlreadyExistsError(
        absl::StrCat(loc.file_name(), ":", loc.line(),
                     " Fixed parameters type already set to \"",
                     dest_signature.fixed_parameters_message_type(), "\""));
  }
  dest_signature.set_fixed_parameters_message_type(
      fixed_parameters_message_type);
  *dest_signature.mutable_fixed_parameters_descriptor_set() =
      fixed_parameters_descriptor_set;
  return absl::OkStatus();
}

absl::Status ActionSignatureBuilder::AddPartSlot(
    absl::string_view slot_name, absl::string_view slot_description,
    const absl::flat_hash_set<intrinsic_proto::icon::v1::FeatureInterfaceTypes>&
        required_feature_interfaces,
    const absl::flat_hash_set<intrinsic_proto::icon::v1::FeatureInterfaceTypes>&
        optional_feature_interfaces,
    intrinsic::SourceLocation loc) {
  if (bool inserted = part_slot_names_.emplace(slot_name).second; !inserted) {
    return absl::AlreadyExistsError(
        absl::StrCat(loc.file_name(), ":", loc.line(),
                     " Duplicate Part Slot name \"", slot_name, "\""));
  }
  absl::flat_hash_set<intrinsic_proto::icon::v1::FeatureInterfaceTypes>
      feature_interfaces_intersection;
  for (const auto& interface : required_feature_interfaces) {
    if (optional_feature_interfaces.contains(interface)) {
      feature_interfaces_intersection.insert(interface);
    }
  }
  if (!feature_interfaces_intersection.empty()) {
    return absl::InvalidArgumentError(absl::StrCat(
        "The following Feature interfaces were listed as both required and "
        "optional for Slot '",
        slot_name,
        "', please ensure each Feature Interface only appears once: [",
        absl::StrJoin(feature_interfaces_intersection, ", ",
                      FeatureInterfaceNameFormatter())));
  }
  intrinsic_proto::icon::v1::ActionSignature::PartSlotInfo info;
  info.set_description(std::string(slot_description));
  *(info.mutable_required_feature_interfaces()) = {
      required_feature_interfaces.begin(), required_feature_interfaces.end()};
  *(info.mutable_optional_feature_interfaces()) = {
      optional_feature_interfaces.begin(), optional_feature_interfaces.end()};
  signature_.mutable_part_slot_infos()->insert({std::string(slot_name), info});

  return absl::OkStatus();
}

absl::Status ActionSignatureBuilder::AddOptionalPartSlot(
    absl::string_view slot_name, absl::string_view slot_description,
    const absl::flat_hash_set<intrinsic_proto::icon::v1::FeatureInterfaceTypes>&
        required_feature_interfaces,
    const absl::flat_hash_set<intrinsic_proto::icon::v1::FeatureInterfaceTypes>&
        optional_feature_interfaces,
    intrinsic::SourceLocation loc) {
  INTR_RETURN_IF_ERROR(AddPartSlot(slot_name, slot_description,
                                   required_feature_interfaces,
                                   optional_feature_interfaces, loc));
  signature_.mutable_part_slot_infos()->at(slot_name).set_is_optional(true);
  return absl::OkStatus();
}

absl::Status ActionSignatureBuilder::AddRealtimeSignal(
    absl::string_view signal_name, absl::string_view signal_description,
    intrinsic::SourceLocation loc) {
  if (realtime_signal_names_.contains(signal_name)) {
    return absl::AlreadyExistsError(
        absl::StrCat(loc.file_name(), ":", loc.line(),
                     " Duplicate Realtime Signal name \"", signal_name, "\""));
  }
  intrinsic_proto::icon::v1::ActionSignature::RealtimeSignalInfo* signal_info =
      signature_.add_realtime_signal_infos();
  signal_info->set_signal_name(signal_name);
  signal_info->set_text_description(std::string(signal_description));
  realtime_signal_names_.insert(std::string(signal_name));
  return absl::OkStatus();
}

absl::Status ActionSignatureBuilder::AddSupportedBehaviorOverride(
    intrinsic_proto::icon::v1::BehaviorOverrideRequest behavior_override,
    absl::string_view description, intrinsic::SourceLocation loc) {
  if (supported_behavior_overrides_.contains(behavior_override)) {
    return absl::AlreadyExistsError(
        absl::StrCat(loc.file_name(), ":", loc.line(),
                     " Duplicate supported BehaviorOverrideRequest \"",
                     intrinsic_proto::icon::v1::BehaviorOverrideRequest_Name(
                         behavior_override),
                     "\""));
  }
  auto* info = signature_.add_behavior_override_infos();
  info->set_override_request(behavior_override);
  info->set_text_description(std::string(description));

  // Store the override to check for duplicates later.
  supported_behavior_overrides_.insert(behavior_override);
  return absl::OkStatus();
}

}  // namespace icon
}  // namespace intrinsic
