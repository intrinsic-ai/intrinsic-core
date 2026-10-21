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

#ifndef INTRINSIC_SKILLS_INTERNAL_EQUIPMENT_UTILITIES_H_
#define INTRINSIC_SKILLS_INTERNAL_EQUIPMENT_UTILITIES_H_

#include <string>

#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "google/protobuf/repeated_ptr_field.h"
#include "intrinsic/resources/proto/resource_handle.pb.h"
#include "intrinsic/skills/proto/equipment.pb.h"
#include "intrinsic/skills/proto/footprint.pb.h"

namespace intrinsic::skills {

// Specifies equipment reservations from a Skill's EquipmentRequired
// implementation.
absl::StatusOr<google::protobuf::RepeatedPtrField<
    intrinsic_proto::skills::ResourceReservation>>
ReserveEquipmentRequired(
    const absl::flat_hash_map<std::string,
                              intrinsic_proto::skills::ResourceSelector>&
        equipment_required,
    const google::protobuf::Map<std::string,
                                intrinsic_proto::resources::ResourceHandle>&
        resource_handles);

}  // namespace intrinsic::skills

#endif  // INTRINSIC_SKILLS_INTERNAL_EQUIPMENT_UTILITIES_H_
