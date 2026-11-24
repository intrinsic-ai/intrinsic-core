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

#include "intrinsic/world/footprints/footprint.h"

#include <utility>
#include <variant>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "intrinsic/geometry/api/affine_transform_of_geometry.h"
#include "intrinsic/geometry/storage/geometry_deserializer.h"
#include "intrinsic/geometry/storage/geometry_serializer.h"
#include "intrinsic/skills/proto/footprint.pb.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/collision/coal_collision_checker.h"
#include "intrinsic/world/component/collections_component.h"
#include "intrinsic/world/entity_id.h"
#include "intrinsic/world/footprints/entity_reservation.h"
#include "intrinsic/world/footprints/object_world_reservation.h"
#include "intrinsic/world/footprints/volume_reservation.h"
#include "intrinsic/world/util/entity_search_util.h"
#include "intrinsic/world/world.h"

namespace intrinsic {

absl::StatusOr<FootprintConflict> Footprint::HasConflict(
    const Footprint& other, const World& world) const {
  return HasConflictUsingIntersectFunction(other, world,
                                           AreGeometriesInCollision);
}

absl::StatusOr<FootprintConflict> Footprint::HasConflictUsingIntersectFunction(
    const Footprint& other, const World& world,
    const IntersectFunction& intersector) const {
  // TODO(b/210110465): Move the HasConflict functionality to an internal
  // utility library or split the Footprint class into an internal and public
  // version. That will make this check obsolete.
  if (!object_reservations_.empty()) {
    return absl::FailedPreconditionError(
        "Footprint::HasConflict and "
        "Footprint::HasConflictUsingIntersectFunction only work for "
        "entity-based footprints, but this footprint contains object "
        "reservations.");
  }

  // If either of the footprints locks the universe, then there will be a
  // conflict.
  // TODO(keegang): May need to have special handling for rests, but ok for now
  // since we don't use the universe lock anywhere.
  if (lock_the_universe_ || other.lock_the_universe_) {
    return FootprintConflict::kHasConflict;
  }

  // Check if there's a conflict in any entity reservation. We do these
  // comparisons first as they a relatively inexpensive to compute.
  for (const EntityReservation& entity_reservation_x : entities_) {
    for (const EntityReservation& entity_reservation_y : other.entities_) {
      INTR_ASSIGN_OR_RETURN(
          EntityReservationConflict conflict,
          entity_reservation_x.HasConflict(entity_reservation_y, world));
      if (conflict == EntityReservationConflict::kHasConflict) {
        return FootprintConflict::kHasConflict;
      }
    }
  }

  // Check if there's a conflict in any volume reservation.
  return HasVolumeConflictUsingIntersectFunction(other, world, intersector);
}

absl::StatusOr<FootprintConflict>
Footprint::HasVolumeConflictUsingIntersectFunction(
    const Footprint& other, const World& world,
    const IntersectFunction& intersector) const {
  // Loop through the volume reservations for conflicts
  for (const VolumeReservation& volume_reservation_x : volumes_) {
    for (const VolumeReservation& volume_reservation_y : other.volumes_) {
      INTR_ASSIGN_OR_RETURN(
          VolumeReservationConflict conflict,
          volume_reservation_x.HasConflict(volume_reservation_y, intersector));
      if (conflict == VolumeReservationConflict::kHasConflict) {
        return FootprintConflict::kHasConflict;
      }
    }
  }

  return FootprintConflict::kNoConflict;
}

absl::StatusOr<FootprintConflict> Footprint::HasVolumeConflict(
    const Footprint& other, const World& world) const {
  return HasVolumeConflictUsingIntersectFunction(other, world,
                                                 AreGeometriesInCollision);
}

std::vector<TransformedGeometry> Footprint::ExtractShapes() const {
  std::vector<TransformedGeometry> shapes;
  for (const auto& volume : volumes_) {
    if (const auto* shape_data =
            std::get_if<TransformedGeometry>(&volume.Value())) {
      shapes.push_back(*shape_data);
    }
  }
  return shapes;
}

absl::Status Footprint::AddMissingPermissions(const World& world) {
  if (lock_the_universe_) {
    LOG(WARNING) << "Attempting to add missing permissions to a footprint with "
                    "lock_the_universe_ set";
  }

  if (!object_reservations_.empty()) {
    return absl::FailedPreconditionError(
        "Footprint::AddMissingPermissions only works for entity-based "
        "footprints, but this footprint contains object reservations.");
  }

  // We make a copy here because we are going to modify the reservations within
  // the loop and that would normally invalidate the iterator being used.
  const std::vector<EntityReservation> loop_reservations(entities_.begin(),
                                                         entities_.end());

  for (auto& reservation : loop_reservations) {
    INTR_ASSIGN_OR_RETURN(const auto entities,
                          GetEntities(world, reservation.Value()));

    for (const EntityId id : entities) {
      if (id == kInvalidEntityId) {
        return absl::InvalidArgumentError("Invalid entity id");
      } else if (!world.HasEntity(id)) {
        return absl::InvalidArgumentError("Unknown entity id");
      }

      auto maybe_collection =
          world.GetComponentByEntityId<CollectionsComponent>(id);
      if (maybe_collection.ok()) {
        const auto collection_members =
            maybe_collection.value()->GetAllCollectionMembers();
        for (const auto& collection_member_id : collection_members) {
          if (collection_member_id == kInvalidEntityId) {
            return absl::InvalidArgumentError(
                "Invalid collection member entity id");
          }

          // The members of the collection get the same access as the collection
          EntityReservation member_reservation_read(
              intrinsic_proto::skills::EntityReservation::READ,
              collection_member_id);
          EntityReservation member_reservation_write(
              intrinsic_proto::skills::EntityReservation::WRITE,
              collection_member_id);

          if (reservation.SharingType() ==
              intrinsic_proto::skills::EntityReservation::WRITE) {
            if (!ContainsEntity(member_reservation_write)) {
              InsertEntity(member_reservation_write);
            }
            if (ContainsEntity(member_reservation_read)) {
              EraseEntity(member_reservation_read);
            }
          } else if (reservation.SharingType() ==
                     intrinsic_proto::skills::EntityReservation::READ) {
            if (!ContainsEntity(member_reservation_write) &&
                !ContainsEntity(member_reservation_read)) {
              InsertEntity(member_reservation_read);
            }
          } else {
            return absl::InvalidArgumentError("Unknown sharing type");
          }
        }
      }
    }
  }

  // Reset the boolean as we have just added all of the missing permissions.
  skip_missing_permissions_ = true;
  return absl::OkStatus();
}

absl::StatusOr<intrinsic_proto::skills::Footprint> Footprint::ToProto(
    GeometrySerializer* geolib) const {
  intrinsic_proto::skills::Footprint proto;
  proto.set_lock_the_universe(lock_the_universe_);
  proto.set_skip_missing_permissions(skip_missing_permissions_);

  for (const auto& entity : entities_) {
    *proto.add_entity() = entity.ToProto();
  }
  for (const ObjectWorldReservation& object_reservation :
       object_reservations_) {
    *proto.add_object_reservation() = object_reservation.ToProto();
  }
  for (const auto& volume : volumes_) {
    INTR_ASSIGN_OR_RETURN(*proto.add_volume(), volume.ToProto(geolib));
  }

  return proto;
}

absl::StatusOr<Footprint> Footprint::FromProto(
    const intrinsic_proto::skills::Footprint& proto,
    const GeometryDeserializer& geolib) {
  Footprint footprint;
  footprint.SetLockTheUniverse(proto.lock_the_universe());
  footprint.SetSkipMissingPermissions(proto.skip_missing_permissions());

  for (const auto& entity_proto : proto.entity()) {
    INTR_ASSIGN_OR_RETURN(auto entity_reservation,
                          EntityReservation::FromProto(entity_proto));
    footprint.entities_.insert(entity_reservation);
  }
  footprint.object_reservations_.reserve(proto.object_reservation_size());
  for (const intrinsic_proto::skills::ObjectWorldReservation&
           reservation_proto : proto.object_reservation()) {
    INTR_ASSIGN_OR_RETURN(ObjectWorldReservation object_reservation,
                          ObjectWorldReservation::FromProto(reservation_proto));
    footprint.object_reservations_.push_back(std::move(object_reservation));
  }
  footprint.volumes_.reserve(proto.volume_size());
  for (const auto& volume_proto : proto.volume()) {
    INTR_ASSIGN_OR_RETURN(auto volume_reservation,
                          VolumeReservation::FromProto(volume_proto, geolib));
    footprint.volumes_.push_back(volume_reservation);
  }

  return footprint;
}

}  // namespace intrinsic
