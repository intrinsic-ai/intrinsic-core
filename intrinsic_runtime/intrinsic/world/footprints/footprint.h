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

#ifndef INTRINSIC_WORLD_FOOTPRINTS_FOOTPRINT_H_
#define INTRINSIC_WORLD_FOOTPRINTS_FOOTPRINT_H_

#include <functional>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "intrinsic/geometry/api/affine_transform_of_geometry.h"
#include "intrinsic/geometry/storage/geometry_deserializer.h"
#include "intrinsic/geometry/storage/geometry_serializer.h"
#include "intrinsic/skills/proto/footprint.pb.h"
#include "intrinsic/world/footprints/entity_reservation.h"
#include "intrinsic/world/footprints/object_world_reservation.h"
#include "intrinsic/world/footprints/volume_reservation.h"
#include "intrinsic/world/world.h"

namespace intrinsic {

// Enum to capture whether or not there's a conflict. This is currently done to
// avoid usage of error-prone StatusOr<bool>.
enum class FootprintConflict : int {
  kNoConflict = 0,
  kHasConflict = 1,
};

// This class represents a footprint. A footprint captures all of the
// reservations used by a skill. If two footprints have a conflict then they
// cannot execute in parallel.
class Footprint {
 public:
  using IntersectFunction = std::function<bool(
      const TransformedGeometry& left, const TransformedGeometry& right)>;

  // Add an entity reservation to the footprint.
  void InsertEntity(const EntityReservation& entity_reservation) {
    entities_.insert(entity_reservation);
  }

  // Remove an entity reservation to the footprint.
  void EraseEntity(const EntityReservation& entity_reservation) {
    entities_.erase(entity_reservation);
  }

  // Returns true if the exact entity reservation is present in this footprint.
  bool ContainsEntity(const EntityReservation& entity_reservation) const {
    return entities_.contains(entity_reservation);
  }

  // Add a world reservation such as an object or frame to the footprint.
  void AddObjectReservation(const ObjectWorldReservation& reservation) {
    object_reservations_.push_back(reservation);
  }

  // Returns all ObjectWorldReservation's in the footprint.
  const std::vector<ObjectWorldReservation>& GetObjectReservations() const {
    return object_reservations_;
  }

  // Add a volume reservation to the footprint.
  void AddVolume(const VolumeReservation& volume_reservation) {
    volumes_.push_back(volume_reservation);
  }

  void SetLockTheUniverse(bool lock_the_universe) {
    lock_the_universe_ = lock_the_universe;
  }

  bool IsTheUniverseLocked() const { return lock_the_universe_; }

  void SetSkipMissingPermissions(bool skip_missing_permissions) {
    skip_missing_permissions_ = skip_missing_permissions;
  }

  bool SkipsMissingPermissions() const { return skip_missing_permissions_; }

  // Clear all volume reservations.
  void ClearVolumes() { volumes_.clear(); }

  // Returns true if this footprint has any volume reservations.
  bool HasVolumes() { return !volumes_.empty(); }

  // Return a vector of the volume reservations.
  std::vector<VolumeReservation> ExtractVolumes() const { return volumes_; }

  // Return vector of TransformedGeometry objects used in the volume
  // reservations, regardless of their sharing type.
  std::vector<TransformedGeometry> ExtractShapes() const;

  // Return a vector of the entity reservations.
  std::vector<EntityReservation> ExtractEntities() const {
    return {entities_.begin(), entities_.end()};
  }

  // Adds read or write permissions for entities in a collection if the
  // collection entity has been given permissions. This will allow for a World
  // subset to have the relevant entities when one is created based on this
  // footprint.
  //
  // This method only works for entity-based footprints, i.e., footprints that
  // contain no object world reservations.
  absl::Status AddMissingPermissions(const World& world);

  // Proto conversion functions.
  absl::StatusOr<intrinsic_proto::skills::Footprint> ToProto(
      GeometrySerializer* geolib) const;

  static absl::StatusOr<Footprint> FromProto(
      const intrinsic_proto::skills::Footprint& proto,
      const GeometryDeserializer& geolib);

  // Tests if this footprint conflicts with another footprint in the world.
  // Volume checking is done using a default intersector function.
  //
  // This method only works for entity-based footprints, i.e., footprints that
  // contain no object world reservations.
  absl::StatusOr<FootprintConflict> HasConflict(const Footprint& other,
                                                const World& world) const;

  // Version of the above with a custom volume intersect function.
  //
  // This method only works for entity-based footprints, i.e., footprints that
  // contain no object world reservations.
  absl::StatusOr<FootprintConflict> HasConflictUsingIntersectFunction(
      const Footprint& other, const World& world,
      const IntersectFunction& intersector) const;

  // Tests if this footprint conflicts with another footprint in the world.
  // Volume checking is done using a default intersector function.
  //
  // This method only works for entity-based footprints, i.e., footprints that
  // contain no object world reservations.
  absl::StatusOr<FootprintConflict> HasVolumeConflict(const Footprint& other,
                                                      const World& world) const;

  // Version of the above with a custom volume intersect function.
  //
  // This method only works for entity-based footprints, i.e., footprints that
  // contain no object world reservations.
  absl::StatusOr<FootprintConflict> HasVolumeConflictUsingIntersectFunction(
      const Footprint& other, const World& world,
      const IntersectFunction& intersector) const;

 private:
  // A linked hash set of entity reservations. We use a linked hash set to
  // preserve order when serializing/deserializing contents.
  absl::flat_hash_set<EntityReservation> entities_;

  // A list of world reservations (objects or frames).
  std::vector<ObjectWorldReservation> object_reservations_;

  // A list of volume reservations.
  std::vector<VolumeReservation> volumes_;

  // If set, then we will lock the universe instead of individual entities.
  bool lock_the_universe_ = false;

  // If set, we do not need to add the extra chain permissions to this footprint
  // before it becomes a complete footprint.
  bool skip_missing_permissions_ = false;
};

}  // namespace intrinsic

#endif  // INTRINSIC_WORLD_FOOTPRINTS_FOOTPRINT_H_
