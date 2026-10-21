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

#include "intrinsic/icon/server/signature_utils.h"

#include <string>
#include <type_traits>
#include <utility>

#include "absl/algorithm/container.h"
#include "absl/container/btree_map.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/types/span.h"
#include "intrinsic/icon/proto/v1/types.pb.h"

namespace intrinsic::icon {

SlotPartCompatibility PartCompatibleWithSlot(
    const intrinsic_proto::icon::v1::PartConfig& part_config,
    const intrinsic_proto::icon::v1::ActionSignature::PartSlotInfo& slot_info) {
  // The C++ Proto API forces repeated enum fields to the underlying type of the
  // enum...
  absl::flat_hash_set<
      std::underlying_type_t<intrinsic_proto::icon::v1::FeatureInterfaceTypes>>
      part_feature_interfaces{part_config.feature_interfaces().begin(),
                              part_config.feature_interfaces().end()};
  if (slot_info.required_feature_interfaces().empty()) {
    if (slot_info.optional_feature_interfaces().empty()) {
      // Any part is compatible with a slot that has neither optional nor
      // required interfaces.
      return {};
    }
    // If the signature lists only optional interfaces, a part must support at
    // least one of them.
    bool has_at_least_one_optional_interface = absl::c_any_of(
        slot_info.optional_feature_interfaces(),
        [&part_feature_interfaces](
            std::underlying_type_t<
                intrinsic_proto::icon::v1::FeatureInterfaceTypes>
                optional_feature_interface) {
          return part_feature_interfaces.contains(optional_feature_interface);
        });

    if (!has_at_least_one_optional_interface) {
      absl::flat_hash_set<std::string> optional_interfaces_str;
      optional_interfaces_str.reserve(
          slot_info.optional_feature_interfaces_size());
      for (const auto interface : slot_info.optional_feature_interfaces()) {
        optional_interfaces_str.insert(
            intrinsic_proto::icon::v1::FeatureInterfaceTypes_Name(interface));
      }

      return {.part_name = part_config.name(),
              .missing_optional_interfaces = optional_interfaces_str};
    }
    return {};
  }
  // If a slot lists any required interfaces, the checks on optional ones above
  // don't matter, and we check only the required interfaces.
  absl::flat_hash_set<std::string> missing_interfaces;
  bool has_all_required_interfaces = absl::c_all_of(
      slot_info.required_feature_interfaces(),
      [&part_feature_interfaces,
       &missing_interfaces](std::underlying_type_t<
                            intrinsic_proto::icon::v1::FeatureInterfaceTypes>
                                required_feature_interface) {
        if (!part_feature_interfaces.contains(required_feature_interface)) {
          missing_interfaces.insert(
              intrinsic_proto::icon::v1::FeatureInterfaceTypes_Name(
                  required_feature_interface));
          return false;
        }
        return true;
      });

  if (has_all_required_interfaces) {
    return {};
  }

  return {.part_name = part_config.name(),
          .missing_required_interfaces = missing_interfaces};
}

absl::Status ActionCompatibleWithSlotPartMap(
    const absl::btree_map<std::string, std::string>& slot_part_map,
    const intrinsic_proto::icon::v1::ActionSignature& action_signature,
    absl::Span<const intrinsic_proto::icon::v1::PartConfig> part_configs) {
  // Two for loops to ensure that
  // 1. `slot_part_map` has entries for all (required) slots for
  //    `action_type_name`.
  // 2. `slot_part_map` *does not* have any slots that do not appear in the
  //    action type's signature.
  for (const auto& [slot_name, slot_info] :
       action_signature.part_slot_infos()) {
    auto slot_to_part_mapping = slot_part_map.find(slot_name);
    if (slot_to_part_mapping == slot_part_map.end()) {
      if (!slot_info.is_optional()) {
        return absl::InvalidArgumentError(absl::StrCat(
            "Required slot '", slot_name, "' is missing from SlotPartMap."));
      }
      // If the slot is optional, simply continue.
      continue;
    }
  }
  absl::flat_hash_map<
      std::string,
      absl::flat_hash_set<intrinsic_proto::icon::v1::FeatureInterfaceTypes>>
      used_feature_interfaces_by_part_name;
  for (const auto& slot_name_and_part_name : slot_part_map) {
    // We need to manually assign these, rather than using a structured binding
    // (i.e. for (const auto& [slot_name, part_name] ...)) because a lambda
    // cannot capture a structured binding.
    const std::string& slot_name = slot_name_and_part_name.first;
    const std::string& part_name = slot_name_and_part_name.second;
    auto part_config = absl::c_find_if(
        part_configs,
        [&](const intrinsic_proto::icon::v1::PartConfig& part_config) {
          return part_config.name() == part_name;
        });
    if (part_config == part_configs.end()) {
      return absl::NotFoundError(
          absl::StrCat("Could not find config for Part '", part_name, "'"));
    }
    // Check if this Part works for the requested Slot.
    auto slot_info_it = action_signature.part_slot_infos().find(slot_name);
    if (slot_info_it == action_signature.part_slot_infos().end()) {
      return absl::NotFoundError(absl::StrCat(
          "SlotPartMap for Action type '", action_signature.action_type_name(),
          "' references Slot name '", slot_name,
          "', which is not a Slot the Action type accepts."));
    }
    if (const auto result =
            PartCompatibleWithSlot(*part_config, slot_info_it->second);
        !result.Compatible()) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Part '", part_name, "' is not compatible with Slot '", slot_name,
          "' for Action type '", action_signature.action_type_name(),
          "'. Explanation: ", result.Explain()));
    }
    for (const auto& feature_interface :
         slot_info_it->second.required_feature_interfaces()) {
      if (bool inserted =
              used_feature_interfaces_by_part_name[part_name]
                  .insert(static_cast<
                          intrinsic_proto::icon::v1::FeatureInterfaceTypes>(
                      feature_interface))
                  .second;
          !inserted) {
        return absl::InvalidArgumentError(
            absl::StrCat("Multiple slots are using feature interface '",
                         intrinsic_proto::icon::v1::FeatureInterfaceTypes_Name(
                             feature_interface),
                         "' of part '", part_name, "'."));
      }
    }
    for (const auto& feature_interface :
         slot_info_it->second.optional_feature_interfaces()) {
      // For optional feature interfaces, we need to check whether the part has
      // that feature interface before adding it.
      if (absl::c_find(part_config->feature_interfaces(), feature_interface) ==
          part_config->feature_interfaces().end()) {
        continue;
      }
      if (bool inserted =
              used_feature_interfaces_by_part_name[part_name]
                  .insert(static_cast<
                          intrinsic_proto::icon::v1::FeatureInterfaceTypes>(
                      feature_interface))
                  .second;
          !inserted) {
        return absl::InvalidArgumentError(
            absl::StrCat("Multiple slots are using feature interface '",
                         intrinsic_proto::icon::v1::FeatureInterfaceTypes_Name(
                             feature_interface),
                         "' of part '", part_name, "'."));
      }
    }
  }
  return absl::OkStatus();
}

}  // namespace intrinsic::icon
