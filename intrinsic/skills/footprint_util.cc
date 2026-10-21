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

#include "intrinsic/skills/footprint_util.h"

#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "intrinsic/choreographer/footprints/footprint.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/geometry/api/affine_transform_of_geometry.h"
#include "intrinsic/geometry/api/geometry.h"
#include "intrinsic/geometry/api/geometry_options.h"
#include "intrinsic/geometry/api/io.h"
#include "intrinsic/geometry/proto/transformed_geometry_storage_refs.pb.h"
#include "intrinsic/geometry/proto/v1/transformed_geometry.pb.h"
#include "intrinsic/geometry/storage/geometry_deserializer.h"
#include "intrinsic/math/proto_conversion.h"
#include "intrinsic/skills/proto/footprint.pb.h"
#include "intrinsic/util/eigen.h"
#include "intrinsic/util/status/status_builder.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/entity_id.h"
#include "intrinsic/world/hashing/hashing.h"
#include "intrinsic/world/util/entity_search_util.h"
#include "intrinsic/world/world.h"
#include "intrinsic/world/world_acl_spec.h"
#include "intrinsic/world/world_entity_acl_spec.h"

namespace intrinsic {

const intrinsic_proto::skills::Footprint& LockTheUniverseFootprint() {
  static intrinsic_proto::skills::Footprint* kLockTheUniverse = []() {
    auto* footprint = new intrinsic_proto::skills::Footprint();
    footprint->set_lock_the_universe(true);
    return footprint;
  }();
  return *kLockTheUniverse;
}

absl::StatusOr<WorldACLSpec> ToWorldACLSpec(
    const World& world,
    const intrinsic_proto::skills::Footprint& footprint_proto,
    const GeometryDeserializer& geolib) {
  // We use no access as the default so that only things explicitly called out
  // in the footprint are converted to ACL requests.
  WorldACLSpec result(WorldEntityACLSpec::NoAccess());

  // Parse the proto so we can re-use some of the methods within the custom
  // Footprint class.
  INTR_ASSIGN_OR_RETURN(Footprint footprint,
                        Footprint::FromProto(footprint_proto, geolib));

  if (!footprint.SkipsMissingPermissions()) {
    INTR_RETURN_IF_ERROR(footprint.AddMissingPermissions(world));
  }

  for (const auto& entity_reservation : footprint.ExtractEntities()) {
    INTR_ASSIGN_OR_RETURN(WorldHashSet<EntityId> selected_entities,
                          GetEntities(world, entity_reservation.Value()));

    switch (entity_reservation.SharingType()) {
      case intrinsic_proto::skills::EntityReservation::WRITE: {
        result.AllowFullAccessFor(selected_entities);
        break;
      }
      case intrinsic_proto::skills::EntityReservation::READ: {
        result.AllowReadFor(selected_entities);
        break;
      }
      default: {
        return intrinsic::UnimplementedErrorBuilder()
               << "Unknown EntityReservation::SharingType with value: "
               << entity_reservation.SharingType();
      }
    }
  }

  if (footprint.IsTheUniverseLocked()) {
    result.AllowFullAccessFor(world.GetEntityIds());
    return std::move(result);
  }

  return std::move(result);
}

absl::Status AreFootprintsCompatible(
    const World& world, const GeometryDeserializer& geolib,
    const intrinsic_proto::skills::Footprint& footprint_left,
    const intrinsic_proto::skills::Footprint& footprint_right) {
  INTR_ASSIGN_OR_RETURN(auto world_acl_left,
                        ToWorldACLSpec(world, footprint_left, geolib));
  INTR_ASSIGN_OR_RETURN(auto world_acl_right,
                        ToWorldACLSpec(world, footprint_right, geolib));
  INTR_RETURN_IF_ERROR(world_acl_left.IsCompatibleWith(world_acl_right));

  // Evaluate Stage 2 (geometric clearance): if either footprint contains no
  // volumes, no 3D volume collisions can exist and the footprints are
  // compatible.
  if (footprint_left.volume_size() == 0 || footprint_right.volume_size() == 0) {
    return absl::OkStatus();
  }

  INTR_ASSIGN_OR_RETURN(Footprint left_footprint,
                        Footprint::FromProto(footprint_left, geolib));
  INTR_ASSIGN_OR_RETURN(Footprint right_footprint,
                        Footprint::FromProto(footprint_right, geolib));

  INTR_ASSIGN_OR_RETURN(
      FootprintConflict has_conflict,
      left_footprint.HasVolumeConflict(right_footprint, world));
  if (has_conflict == FootprintConflict::kNoConflict) {
    return absl::OkStatus();
  }

  return absl::InvalidArgumentError(
      "There was a conflict between the two footprints");
}

absl::StatusOr<std::vector<
    std::pair<TransformedGeometry,
              intrinsic_proto::skills::VolumeReservation::SharingType>>>
ConvertFootprintToShapes(const World& world,
                         const intrinsic_proto::skills::Footprint& footprint,
                         const GeometryDeserializer& geolib) {
  std::vector<
      std::pair<TransformedGeometry,
                intrinsic_proto::skills::VolumeReservation::SharingType>>
      result;
  for (const auto& volume_reservation : footprint.volume()) {
    INTR_ASSIGN_OR_RETURN(auto shapes, ConvertResourceToShapes(
                                           world, volume_reservation, geolib));
    result.insert(result.end(), shapes.begin(), shapes.end());
  }

  return std::move(result);
}

absl::StatusOr<std::vector<
    std::pair<TransformedGeometry,
              intrinsic_proto::skills::VolumeReservation::SharingType>>>
ConvertResourceToShapes(
    const World& world,
    const intrinsic_proto::skills::VolumeReservation& volume_reservation,
    const GeometryDeserializer& geolib) {
  const GeometryOptions& options = GeometryOptions::Default();
  std::vector<
      std::pair<TransformedGeometry,
                intrinsic_proto::skills::VolumeReservation::SharingType>>
      result;
  switch (volume_reservation.volume_oneof_case()) {
    case intrinsic_proto::skills::VolumeReservation::kTransformedGeometry: {
      INTR_ASSIGN_OR_RETURN(
          TransformedGeometry geometry,
          ToGeometry(volume_reservation.transformed_geometry(), &geolib));
      result.emplace_back(std::move(geometry), volume_reservation.type());
      break;
    }
    case intrinsic_proto::skills::VolumeReservation::kShape: {
      // TODO(stoyang): Move FromProto(TransformedGeometryStorageRefs) to io.h
      INTR_ASSIGN_OR_RETURN(
          Geometry geometry,
          geolib.GetGeometry(volume_reservation.shape().geometry_storage_refs(),
                             options));
      INTR_ASSIGN_OR_RETURN(const eigenmath::MatrixXd matrix,
                            intrinsic_proto::FromProto(
                                volume_reservation.shape().ref_t_shape_aff()));
      INTR_ASSIGN_OR_RETURN(eigenmath::Matrix4d ref_t_shape_aff,
                            toAffineMatrix4d(matrix));

      result.emplace_back(TransformedGeometry(geometry, ref_t_shape_aff),
                          volume_reservation.type());
      break;
    }
    case intrinsic_proto::skills::VolumeReservation::VOLUME_ONEOF_NOT_SET:
    default:
      return FailedPreconditionErrorBuilder()
             << "Unknown volume type: "
             << volume_reservation.volume_oneof_case();
  }

  return std::move(result);
}

}  // namespace intrinsic
