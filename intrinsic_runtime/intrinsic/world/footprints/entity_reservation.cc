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

#include "intrinsic/world/footprints/entity_reservation.h"

#include <string>
#include <utility>
#include <variant>

#include "absl/container/btree_set.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gloop/util/gtl/flat_set.h"
#include "intrinsic/skills/proto/footprint.pb.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/entity_id.h"
#include "intrinsic/world/labels.h"
#include "intrinsic/world/util/entity_search_util.h"
#include "intrinsic/world/world.h"
#include "ortools/base/stl_util.h"

namespace intrinsic {

namespace {

// Set of entity types that are allowed to overlap. For example, multiple READ
// locks are allowed to share the same resource.  Note that the code only checks
// one direction and so the set should be symmetric ({A,B} -> {B,A}).
inline constexpr auto kEntityCompatibility = gtl::fixed_flat_set_of<
    std::pair<intrinsic_proto::skills::EntityReservation::SharingType,
              intrinsic_proto::skills::EntityReservation::SharingType>>(
    {{intrinsic_proto::skills::EntityReservation::READ,
      intrinsic_proto::skills::EntityReservation::READ}});

// Converts from the variant type to an EntitySearchCriteria representing it.
intrinsic_proto::world::EntitySearchCriteria ConvertFromVariant(
    std::variant<EntityId, std::string, LabelId> value) {
  intrinsic_proto::world::EntitySearchCriteria result;

  if (auto* entity_id = std::get_if<EntityId>(&value)) {
    result.mutable_by_id()->set_entity_id(entity_id->value());
  } else if (auto* alias = std::get_if<std::string>(&value)) {
    result.mutable_by_alias()->set_alias(*alias);
  } else if (auto* label_id = std::get_if<LabelId>(&value)) {
    result.mutable_by_labels()->add_label_ids(label_id->value());
  } else {
    LOG(FATAL) << "Unknown variant type";
  }

  return result;
}

}  // namespace

EntityReservation::EntityReservation(
    intrinsic_proto::skills::EntityReservation::SharingType sharing_type,
    intrinsic_proto::world::EntitySearchCriteria entity)
    : sharing_type_(sharing_type), entity_(std::move(entity)) {}

EntityReservation::EntityReservation(
    intrinsic_proto::skills::EntityReservation::SharingType sharing_type,
    std::variant<EntityId, std::string, LabelId> value)
    : sharing_type_(sharing_type),
      entity_(ConvertFromVariant(std::move(value))) {}

absl::StatusOr<EntityReservationConflict> EntityReservation::HasConflict(
    const EntityReservation& other, const World& world) const {
  // If the two types are compatible, then there's no conflict.
  if (kEntityCompatibility.contains({sharing_type_, other.sharing_type_})) {
    return EntityReservationConflict::kNoConflict;
  }

  // Convert entity resource to a set of entity ids.
  INTR_ASSIGN_OR_RETURN(absl::btree_set<EntityId> entities,
                        ExtractSortedEntityIds(world));
  INTR_ASSIGN_OR_RETURN(absl::btree_set<EntityId> other_entities,
                        other.ExtractSortedEntityIds(world));

  // If the entity id sets overlap, then there is a conflict.
  return gtl::SortedContainersHaveIntersection(entities, other_entities)
             ? EntityReservationConflict::kHasConflict
             : EntityReservationConflict::kNoConflict;
}

absl::StatusOr<absl::btree_set<EntityId>>
EntityReservation::ExtractSortedEntityIds(const World& world) const {
  INTR_ASSIGN_OR_RETURN(auto unsorted_entities, GetEntities(world, entity_));

  absl::btree_set<EntityId> entities;
  entities.insert(unsorted_entities.begin(), unsorted_entities.end());

  return entities;
}

intrinsic_proto::skills::EntityReservation EntityReservation::ToProto() const {
  intrinsic_proto::skills::EntityReservation proto;
  proto.set_type(sharing_type_);
  *proto.mutable_entity() = entity_;
  return proto;
}

absl::StatusOr<EntityReservation> EntityReservation::FromProto(
    const intrinsic_proto::skills::EntityReservation& proto) {
  if (!proto.has_entity()) {
    return absl::InvalidArgumentError(
        "Missing entity search criteria for EntityReservation");
  }

  return EntityReservation(proto.type(), proto.entity());
}

}  // namespace intrinsic
