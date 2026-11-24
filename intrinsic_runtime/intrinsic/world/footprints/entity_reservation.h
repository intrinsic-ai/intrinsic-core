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

#ifndef INTRINSIC_WORLD_FOOTPRINTS_ENTITY_RESERVATION_H_
#define INTRINSIC_WORLD_FOOTPRINTS_ENTITY_RESERVATION_H_

#include <string>
#include <variant>

#include "absl/container/btree_set.h"
#include "absl/status/statusor.h"
#include "absl/types/variant.h"
#include "intrinsic/skills/proto/footprint.pb.h"
#include "intrinsic/util/proto/pb_hash.h"
#include "intrinsic/world/entity_id.h"
#include "intrinsic/world/labels.h"
#include "intrinsic/world/proto/entity_search.pb.h"
#include "intrinsic/world/world.h"

namespace intrinsic {

// Enum to capture whether or not there's a conflict. This is currently done to
// avoid usage of error-prone StatusOr<bool>.
enum class EntityReservationConflict : int {
  kNoConflict = 0,
  kHasConflict = 1,
};

// The class holds a single entity reservation. Each entity reservation has a
// sharing type and value. Two reservations are in conflict if
//   1. their types do not allow sharing (e.g., WRITE & WRITE)
//   2. the underlying EntityId sets overlap.
class EntityReservation {
 public:
  // Constructs a new EntityReservation from EntitySearchCriteria.
  EntityReservation(
      intrinsic_proto::skills::EntityReservation::SharingType sharing_type,
      intrinsic_proto::world::EntitySearchCriteria entity);

  // Constructs a new EntityReservation.
  EntityReservation(
      intrinsic_proto::skills::EntityReservation::SharingType sharing_type,
      std::variant<EntityId, std::string, LabelId> value);

  // Returns the conflict status between this and another entity reservation.
  absl::StatusOr<EntityReservationConflict> HasConflict(
      const EntityReservation& other, const World& world) const;

  const intrinsic_proto::world::EntitySearchCriteria& Value() const {
    return entity_;
  }

  const intrinsic_proto::skills::EntityReservation::SharingType& SharingType()
      const {
    return sharing_type_;
  }

  // Comparison operators.
  bool operator==(const EntityReservation& other) const {
    return sharing_type_ == other.sharing_type_ &&
           intrinsic::pb_equals{}(entity_, other.entity_);
  }
  bool operator!=(const EntityReservation& other) const {
    return !(*this == other);
  }

  // Computes the Hash value.
  template <typename H>
  friend H AbslHashValue(H state, const EntityReservation& value) {
    return H::combine(std::move(state), value.sharing_type_,
                      intrinsic::pb_hash{}(value.entity_));
    return state;
  }

  // Proto conversion functions.
  intrinsic_proto::skills::EntityReservation ToProto() const;

  static absl::StatusOr<EntityReservation> FromProto(
      const intrinsic_proto::skills::EntityReservation& proto);

 private:
  // Convert the EntityReservationVariant to a set of EntityIds.
  absl::StatusOr<absl::btree_set<EntityId>> ExtractSortedEntityIds(
      const World& world) const;

  // The entity sharing type.
  const intrinsic_proto::skills::EntityReservation::SharingType sharing_type_;

  // The actual entity reservation value.
  const intrinsic_proto::world::EntitySearchCriteria entity_;
};

}  // namespace intrinsic

#endif  // INTRINSIC_WORLD_FOOTPRINTS_ENTITY_RESERVATION_H_
