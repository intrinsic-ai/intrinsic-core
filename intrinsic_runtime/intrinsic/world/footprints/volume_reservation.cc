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

#include "intrinsic/world/footprints/volume_reservation.h"

#include <optional>
#include <utility>
#include <variant>

#include "absl/status/statusor.h"
#include "gloop/util/gtl/flat_set.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/geometry/api/affine_transform_of_geometry.h"
#include "intrinsic/geometry/api/geometry.h"
#include "intrinsic/geometry/api/geometry_options.h"
#include "intrinsic/geometry/api/io.h"
#include "intrinsic/geometry/proto/geometry_storage_refs.pb.h"
#include "intrinsic/geometry/proto/transformed_geometry_storage_refs.pb.h"
#include "intrinsic/geometry/proto/v1/transformed_geometry.pb.h"
#include "intrinsic/geometry/storage/geometry_deserializer.h"
#include "intrinsic/geometry/storage/geometry_serializer.h"
#include "intrinsic/math/proto_conversion.h"
#include "intrinsic/skills/proto/footprint.pb.h"
#include "intrinsic/util/eigen.h"
#include "intrinsic/util/status/status_builder.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic {

namespace {

// Set of volume types that are allowed to overlap. For example, multiple EMPTY
// locks are allowed to share the same reservation. Note that the code only
// checks one direction and so the set should be symmetric ({A,B} -> {B,A}).
inline constexpr auto kVolumeCompatibility = gtl::fixed_flat_set_of<
    std::pair<intrinsic_proto::skills::VolumeReservation::SharingType,
              intrinsic_proto::skills::VolumeReservation::SharingType>>({
    {intrinsic_proto::skills::VolumeReservation::EMPTY,
     intrinsic_proto::skills::VolumeReservation::EMPTY},
    {intrinsic_proto::skills::VolumeReservation::WRITE_ALLOWING_STATIC,
     intrinsic_proto::skills::VolumeReservation::STATIC},
    {intrinsic_proto::skills::VolumeReservation::STATIC,
     intrinsic_proto::skills::VolumeReservation::WRITE_ALLOWING_STATIC},
});

}  // namespace

absl::StatusOr<VolumeReservationConflict> VolumeReservation::HasConflict(
    const VolumeReservation& other,
    const IntersectFunction& intersector) const {
  // If the two types are compatible, then there's no conflict.
  if (kVolumeCompatibility.contains({sharing_type_, other.sharing_type_})) {
    return VolumeReservationConflict::kNoConflict;
  }

  // We currently only support volume reservations with ShapeData.
  const auto* shape_data = std::get_if<TransformedGeometry>(&value_);
  const auto* other_shape_data =
      std::get_if<TransformedGeometry>(&other.value_);
  if (shape_data == nullptr || other_shape_data == nullptr) {
    return FailedPreconditionErrorBuilder()
           << "VolumeReservation does not contain TransformedGeometry. "
              "VolumeFromPath reservation conflict checking not implemented";
  }

  // Run the passed-in intersector function to determine if there's a conflict.
  return intersector(*shape_data, *other_shape_data)
             ? VolumeReservationConflict::kHasConflict
             : VolumeReservationConflict::kNoConflict;
}

absl::StatusOr<intrinsic_proto::skills::VolumeReservation>
VolumeReservation::ToProto(GeometrySerializer* geolib) const {
  intrinsic_proto::skills::VolumeReservation proto;
  proto.set_type(sharing_type_);

  if (const auto* shape_data = std::get_if<TransformedGeometry>(&value_)) {
    INTR_ASSIGN_OR_RETURN(*proto.mutable_transformed_geometry(),
                          ::intrinsic::ToProto(*shape_data, geolib));
  }

  return proto;
}

absl::StatusOr<VolumeReservation> VolumeReservation::FromProto(
    const intrinsic_proto::skills::VolumeReservation& proto,
    const GeometryDeserializer& geolib) {
  intrinsic_proto::skills::VolumeReservation::SharingType sharing_type =
      proto.type();
  const GeometryOptions& options = GeometryOptions::Default();
  std::optional<VolumeReservationVariant> value;
  switch (proto.volume_oneof_case()) {
    case intrinsic_proto::skills::VolumeReservation::kTransformedGeometry: {
      INTR_ASSIGN_OR_RETURN(TransformedGeometry geometry,
                            ToGeometry(proto.transformed_geometry(), &geolib));
      value = std::move(geometry);
      break;
    }
    case intrinsic_proto::skills::VolumeReservation::kShape: {
      INTR_ASSIGN_OR_RETURN(
          Geometry geometry,
          geolib.GetGeometry(proto.shape().geometry_storage_refs(), options));
      INTR_ASSIGN_OR_RETURN(
          const eigenmath::MatrixXd matrix,
          intrinsic_proto::FromProto(proto.shape().ref_t_shape_aff()));
      INTR_ASSIGN_OR_RETURN(eigenmath::Matrix4d ref_t_shape_aff,
                            toAffineMatrix4d(matrix));
      value = TransformedGeometry(geometry, ref_t_shape_aff);
      break;
    }
    case intrinsic_proto::skills::VolumeReservation::VOLUME_ONEOF_NOT_SET:
    default:
      return FailedPreconditionErrorBuilder()
             << "Unknown volume type: " << proto.volume_oneof_case();
  }

  return VolumeReservation(sharing_type, std::move(value.value()));
}

}  // namespace intrinsic
