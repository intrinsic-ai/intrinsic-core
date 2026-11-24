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

#ifndef INTRINSIC_WORLD_FOOTPRINTS_OBJECT_WORLD_RESERVATION_H_
#define INTRINSIC_WORLD_FOOTPRINTS_OBJECT_WORLD_RESERVATION_H_

#include <optional>
#include <ostream>
#include <variant>

#include "absl/status/statusor.h"
#include "intrinsic/skills/proto/footprint.pb.h"
#include "intrinsic/world/objects/object_world_ids.h"
#include "intrinsic/world/proto/object_world_refs.pb.h"

namespace intrinsic {

// Represents a single object world reservation and an associated sharing type
// (read or write).
class ObjectWorldReservation {
 public:
  // Type-redefinition for convenience.
  using SharingType =
      intrinsic_proto::skills::ObjectWorldReservation::SharingType;

  // Redefinitions of the 'SharingType' values for convenience.
  static constexpr SharingType kSharingTypeRead =
      intrinsic_proto::skills::ObjectWorldReservation::READ;
  static constexpr SharingType kSharingTypeWrite =
      intrinsic_proto::skills::ObjectWorldReservation::WRITE;

  // Indicates which type of reservation an ObjectWorldReservation instance is
  // holding.
  enum class ReservationType { kObject, kFrame };

  // Constructs a new object reservation from the given sharing type and from
  // the given WorldObjectName.
  ObjectWorldReservation(SharingType sharing_type, WorldObjectName object);

  // Constructs a new frame reservation from the given sharing type and from the
  // given FrameReferenceByName.
  ObjectWorldReservation(SharingType sharing_type, FrameReferenceByName frame);

  // Constructs a new object reservation from the given sharing type and
  // ObjectReferenceByName.
  ObjectWorldReservation(
      SharingType sharing_type,
      const intrinsic_proto::world::ObjectReferenceByName& object);

  // Constructs a new frame reservation from the given sharing type and
  // FrameReferenceByName.
  ObjectWorldReservation(
      SharingType sharing_type,
      const intrinsic_proto::world::FrameReferenceByName& frame);

  // Constructs a new frame or object reservation from the given sharing type
  // and TransformNodeReferenceByName.
  ObjectWorldReservation(
      SharingType sharing_type,
      const intrinsic_proto::world::TransformNodeReferenceByName&
          transform_node);

  // Constructs a new instance from the given proto representation.
  static absl::StatusOr<ObjectWorldReservation> FromProto(
      const intrinsic_proto::skills::ObjectWorldReservation& proto);

  // Converts this instance to its proto representation.
  intrinsic_proto::skills::ObjectWorldReservation ToProto() const;

  // Returns the sharing type (read or write).
  SharingType GetSharingType() const;

  // Returns which kind of object world reservation is represented by this
  // instance (also see GetObjectReservation() and GetFrameReservation()).
  ReservationType GetReservationType() const;

  // If this instance reprents an object (GetReservationType() == kObject),
  // returns the name of this object. Else returns std::nullopt.
  std::optional<WorldObjectName> GetObjectReservation() const;

  // If this instance reprents a frame (GetReservationType() == kFrame), returns
  // the names of this frame and its parent object. Else returns std::nullopt.
  std::optional<FrameReferenceByName> GetFrameReservation() const;

  bool operator==(const ObjectWorldReservation& other) const;
  bool operator!=(const ObjectWorldReservation& other) const;

  friend std::ostream& operator<<(std::ostream& os,
                                  const ObjectWorldReservation& reservation);

 private:
  intrinsic_proto::skills::ObjectWorldReservation::SharingType sharing_type_;
  std::variant<WorldObjectName, FrameReferenceByName> reservation_;
};

}  // namespace intrinsic

#endif  // INTRINSIC_WORLD_FOOTPRINTS_OBJECT_WORLD_RESERVATION_H_
