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

#include "intrinsic/skills/cc/equipment_pack.h"

#include <string>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "intrinsic/resources/proto/resource_handle.pb.h"
#include "intrinsic/skills/proto/equipment.pb.h"
#include "intrinsic/skills/proto/skill_service.pb.h"
#include "intrinsic/skills/proto/skills.pb.h"

namespace intrinsic {
namespace skills {

EquipmentPack::EquipmentPack(
    const google::protobuf::Map<std::string,
                                intrinsic_proto::resources::ResourceHandle>&
        resource_handles)
    : equipment_map_(resource_handles.begin(), resource_handles.end()) {}

absl::StatusOr<EquipmentPack> EquipmentPack::GetEquipmentPack(
    const intrinsic_proto::skills::GetFootprintRequest& request) {
  if (!request.has_instance()) {
    return absl::InvalidArgumentError(
        "In `request`, expected a skill `instance`, but the `instance` is "
        "missing.");
  }
  return EquipmentPack(request.instance().resource_handles());
}

absl::StatusOr<EquipmentPack> EquipmentPack::GetEquipmentPack(
    const intrinsic_proto::skills::ExecuteRequest& request) {
  if (!request.has_instance()) {
    return absl::InvalidArgumentError(
        "In `request`, expected a skill `instance`, but the `instance` is "
        "missing.");
  }
  return EquipmentPack(request.instance().resource_handles());
}

absl::StatusOr<EquipmentPack> EquipmentPack::GetEquipmentPack(
    const intrinsic_proto::skills::PreviewRequest& request) {
  if (!request.has_instance()) {
    return absl::InvalidArgumentError(
        "In `request`, expected a skill `instance`, but the `instance` is "
        "missing.");
  }
  return EquipmentPack(request.instance().resource_handles());
}

absl::StatusOr<intrinsic_proto::resources::ResourceHandle>
EquipmentPack::GetHandle(absl::string_view key) const {
  if (!equipment_map_.contains(key)) {
    return internal::MissingEquipmentError(key);
  }

  return equipment_map_.at(key);
}

absl::Status EquipmentPack::Remove(absl::string_view key) {
  if (equipment_map_.erase(key) == 0) {
    return internal::MissingEquipmentError(key);
  }
  return absl::OkStatus();
}

absl::Status EquipmentPack::Add(
    absl::string_view key, intrinsic_proto::resources::ResourceHandle handle) {
  if (equipment_map_.contains(key)) {
    return absl::InvalidArgumentError(
        absl::StrCat("Equipment pack already contains handle for key: ", key));
  }

  equipment_map_[key] = handle;
  return absl::OkStatus();
}

namespace internal {

absl::Status MissingEquipmentError(absl::string_view key) {
  return {absl::StatusCode::kInvalidArgument,
          absl::StrCat("Missing resource handle for slot: ", key)};
}

absl::Status EquipmentContentsTypeError() {
  return {absl::StatusCode::kInvalidArgument,
          "Failed to unpack Any proto into `equipment`. "
          "Maybe the given `equipment` is of an incorrect protobuf::Message "
          "type?"};
}

}  // namespace internal

}  // namespace skills
}  // namespace intrinsic
