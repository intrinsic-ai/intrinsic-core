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

#include "intrinsic/icon/server/testing/fake_part_collection.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gloop/util/gtl/iterator_adaptors.h"
#include "intrinsic/icon/common/part_properties.h"
#include "intrinsic/icon/control/parts/payload_property.h"
#include "intrinsic/icon/proto/part_status.pb.h"
#include "intrinsic/icon/proto/safety_status.pb.h"
#include "intrinsic/icon/proto/v1/types.pb.h"
#include "intrinsic/icon/server/part_collection.h"
#include "intrinsic/util/macros.h"
#include "intrinsic/world/robot_payload/robot_payload.h"

namespace intrinsic::icon {
namespace {

class FakeStatusGetter : public PartCollection::StatusGetter {
 public:
  explicit FakeStatusGetter(
      absl::flat_hash_map<std::string, intrinsic_proto::icon::PartStatus>
          statuses)
      : statuses_(std::move(statuses)) {
    for (const auto& [name, _] : statuses_) {
      part_properties_.emplace(name, FakeProperties{});
    }
  }

  absl::StatusOr<
      absl::flat_hash_map<std::string, intrinsic_proto::icon::PartStatus>>
  GetPartStatuses(absl::Time deadline) override {
    return statuses_;
  }

  absl::StatusOr<intrinsic_proto::icon::SafetyStatus> GetSafetyStatus(
      absl::Time deadline) override {
    return safety_status_;
  }

  absl::StatusOr<TimestampedPartProperties> GetPartProperties() override {
    TimestampedPartProperties timestamped_properties;
    timestamped_properties.timestamp_wall = absl::Now();
    timestamped_properties.timestamp_control =
        timestamped_properties.timestamp_wall - absl::UnixEpoch();
    for (const auto& [name, properties] : part_properties_) {
      timestamped_properties.properties[name] = properties.payload_property;
      timestamped_properties
          .properties[name][kFakePartCollectionBoolPropertyName] =
          properties.bool_property;
      timestamped_properties
          .properties[name][kFakePartCollectionDoublePropertyName] =
          properties.double_property;
    }
    return timestamped_properties;
  }

  absl::Status SetPartProperties(const PartPropertyMap& properties) override {
    absl::flat_hash_map<std::string, FakeProperties> new_part_properties =
        part_properties_;
    for (const auto& part_name_and_properties : properties.properties) {
      const std::string& part_name = part_name_and_properties.first;
      const auto& part_properties = part_name_and_properties.second;
      auto it = new_part_properties.find(part_name);

      if (it == new_part_properties.end()) {
        return absl::NotFoundError(absl::StrCat(
            "Part '", part_name, "' not found. Available parts: ",
            absl::StrJoin(new_part_properties, ", ",
                          [](std::string* os, const auto& name_and_properties) {
                            absl::StrAppend(os, name_and_properties.first);
                          })));
      }

      for (const auto& property : part_properties) {
        const std::string& property_name = property.first;
        const auto& value = property.second;
        auto property_not_found_error = [&property_name, &part_name, &it]() {
          return absl::NotFoundError(absl::StrCat(
              "Property '", property_name, "' of part '", part_name,
              "' not found. Available properties: ",
              kFakePartCollectionBoolPropertyName, ", ",
              kFakePartCollectionDoublePropertyName, ", ",
              absl::StrJoin(gtl::key_view(it->second.payload_property), ", ")));
        };
        if (property_name == kFakePartCollectionBoolPropertyName) {
          if (!std::holds_alternative<bool>(value)) {
            return absl::InvalidArgumentError(absl::StrCat(
                "Property '", kFakePartCollectionBoolPropertyName,
                "' of part '", part_name,
                "' is a bool, but the value you're trying to write is not"));
          }
          it->second.bool_property = std::get<bool>(value);
        } else if (property_name == kFakePartCollectionDoublePropertyName) {
          if (!std::holds_alternative<double>(value)) {
            return absl::InvalidArgumentError(absl::StrCat(
                "Property '", kFakePartCollectionDoublePropertyName,
                "' of part '", part_name,
                "' is a double, but the value you're trying to write is not"));
          }
          it->second.double_property = std::get<double>(value);
        } else if (property_name.starts_with(
                       kFakePartCollectionPayloadPropertyName)) {
          if (it->second.payload_property.contains(property_name)) {
            if (property_name.ends_with(kHasValueSuffix)) {
              it->second.payload_property[property_name] =
                  std::get<bool>(value);
            } else {
              if (!std::holds_alternative<double>(value)) {
                return absl::InvalidArgumentError(absl::StrCat(
                    "Property '", property_name, "' of part '", part_name,
                    "' is a double, but the value you're trying to write is "
                    "not"));
              }
              it->second.payload_property[property_name] =
                  std::get<double>(value);
            }
          } else {
            return property_not_found_error();
          }
        } else {
          return property_not_found_error();
        }
      }
    }
    part_properties_ = new_part_properties;
    return absl::OkStatus();
  }

 private:
  const absl::flat_hash_map<std::string, intrinsic_proto::icon::PartStatus>
      statuses_;
  const intrinsic_proto::icon::SafetyStatus safety_status_;
  struct FakeProperties {
    FakeProperties() {
      // Always succeeds for the default payload.
      ASSIGN_OR_DIE(payload_property,
                    ToPartPropertyMap(RobotPayload(),
                                      kFakePartCollectionPayloadPropertyName));
    }
    bool bool_property = false;
    double double_property = 0.0;
    absl::flat_hash_map<std::string, PartPropertyValue> payload_property;
  };
  // One FakeProperties object per part (status)
  absl::flat_hash_map<std::string, FakeProperties> part_properties_;
};

}  // namespace

absl::StatusOr<PartCollection> FakePartCollectionNamesOnly(
    const absl::flat_hash_set<std::string>& part_names) {
  absl::flat_hash_map<std::string, intrinsic_proto::icon::PartStatus> statuses;
  for (const auto& name : part_names) {
    statuses.insert({name, intrinsic_proto::icon::PartStatus()});
  }
  return FakePartCollectionWithStatuses(statuses);
}

absl::StatusOr<PartCollection> FakePartCollectionWithStatuses(
    const absl::flat_hash_map<std::string, intrinsic_proto::icon::PartStatus>&
        statuses) {
  std::vector<intrinsic_proto::icon::v1::PartConfig> part_configs;
  for (const auto& [name, status] : statuses) {
    intrinsic_proto::icon::v1::PartConfig& part_config =
        part_configs.emplace_back();
    part_config.set_name(name);
  }
  return PartCollection::Create(std::move(part_configs),
                                std::make_unique<FakeStatusGetter>(statuses));
}

absl::StatusOr<PartCollection> FakePartCollectionWithConfigs(
    std::vector<intrinsic_proto::icon::v1::PartConfig> part_configs) {
  absl::flat_hash_map<std::string, intrinsic_proto::icon::PartStatus> statuses;
  for (const auto& part_config : part_configs) {
    statuses.insert({part_config.name(), intrinsic_proto::icon::PartStatus()});
  }
  std::sort(part_configs.begin(), part_configs.end(),
            [](const auto& a, const auto& b) { return a.name() < b.name(); });
  return PartCollection::Create(std::move(part_configs),
                                std::make_unique<FakeStatusGetter>(statuses));
}
}  // namespace intrinsic::icon
