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

#ifndef INTRINSIC_WORLD_FOOTPRINTS_VOLUME_RESERVATION_H_
#define INTRINSIC_WORLD_FOOTPRINTS_VOLUME_RESERVATION_H_

#include <functional>
#include <utility>
#include <variant>

#include "absl/status/statusor.h"
#include "intrinsic/geometry/api/affine_transform_of_geometry.h"
#include "intrinsic/geometry/storage/geometry_deserializer.h"
#include "intrinsic/geometry/storage/geometry_serializer.h"
#include "intrinsic/skills/proto/footprint.pb.h"

namespace intrinsic {

// Enum to capture whether or not there's a conflict. This is currently done to
// avoid usage of error-prone StatusOr<bool>.
enum class VolumeReservationConflict : int {
  kNoConflict = 0,
  kHasConflict = 1,
};

using VolumeReservationVariant = std::variant<TransformedGeometry>;

// This class holds a single volume resource. Each volume resource has a sharing
// type and value. Two volumes are in conflict if
//   1. their types do not allow sharing (e.g., WRITE & WRITE)
//   2. the geometric volumes intersect
class VolumeReservation {
 public:
  using IntersectFunction = std::function<bool(
      const TransformedGeometry& left, const TransformedGeometry& right)>;

  // Constructs a new VolumeReservation.
  VolumeReservation(
      intrinsic_proto::skills::VolumeReservation::SharingType sharing_type,
      VolumeReservationVariant value)
      : sharing_type_(sharing_type), value_(std::move(value)) {}

  const VolumeReservationVariant& Value() const { return value_; }

  const intrinsic_proto::skills::VolumeReservation::SharingType& SharingType()
      const {
    return sharing_type_;
  }

  // Returns the conflict status between this and another volume reservation.
  // Volume intersection is tested using the passed-in intersector function.
  absl::StatusOr<VolumeReservationConflict> HasConflict(
      const VolumeReservation& other,
      const IntersectFunction& intersector) const;

  // Proto conversion functions.
  absl::StatusOr<intrinsic_proto::skills::VolumeReservation> ToProto(
      GeometrySerializer* geolib) const;

  static absl::StatusOr<VolumeReservation> FromProto(
      const intrinsic_proto::skills::VolumeReservation& proto,
      const GeometryDeserializer& geolib);

 private:
  // The volume sharing type.
  intrinsic_proto::skills::VolumeReservation::SharingType sharing_type_;

  // The volume reservation.
  VolumeReservationVariant value_;
};

}  // namespace intrinsic

#endif  // INTRINSIC_WORLD_FOOTPRINTS_VOLUME_RESERVATION_H_
