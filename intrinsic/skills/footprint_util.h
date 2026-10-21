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

#ifndef INTRINSIC_SKILLS_FOOTPRINT_UTIL_H_
#define INTRINSIC_SKILLS_FOOTPRINT_UTIL_H_

#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "intrinsic/geometry/api/affine_transform_of_geometry.h"
#include "intrinsic/geometry/storage/geometry_deserializer.h"
#include "intrinsic/skills/proto/footprint.pb.h"
#include "intrinsic/world/world.h"
#include "intrinsic/world/world_acl_spec.h"

namespace intrinsic {

// Returns an instance of the Footprint proto that locks the universe.
const intrinsic_proto::skills::Footprint& LockTheUniverseFootprint();

// Converts a Footprint proto to a WorldACLSpec for use with a World object.
// Processes explicit entity and object reservations (and lock_the_universe) to
// establish read/write access control on world entities. Volume reservations do
// not synthesize entity ACLs and are deconflicted geometrically in
// `AreFootprintsCompatible()`.
absl::StatusOr<WorldACLSpec> ToWorldACLSpec(
    const World& world, const intrinsic_proto::skills::Footprint& footprint,
    const GeometryDeserializer& geolib);

// Returns ok status if the given footprints are compatible for concurrent
// execution.
// Evaluates compatibility in two stages:
// 1. Stage 1 (World ACL Spec): Checks that entity and object reservations do
//    not contain conflicting access permissions (e.g. read/write mutexes on the
//    same entity).
// 2. Stage 2 (Geometric Clearance): Checks that 3D volume reservations between
//    the two footprints do not intersect in workspace coordinates (via
//    `HasVolumeConflict()`).
absl::Status AreFootprintsCompatible(
    const World& world, const GeometryDeserializer& geolib,
    const intrinsic_proto::skills::Footprint& footprint_left,
    const intrinsic_proto::skills::Footprint& footprint_right);

// Returns a set of shapes associated with the given footprint and their sharing
// types.
absl::StatusOr<std::vector<
    std::pair<TransformedGeometry,
              intrinsic_proto::skills::VolumeReservation::SharingType>>>
ConvertFootprintToShapes(const World& world,
                         const intrinsic_proto::skills::Footprint& footprint,
                         const GeometryDeserializer& geolib);

// Returns a set of shapes associated with the given volume resource and their
// sharing types.
absl::StatusOr<std::vector<
    std::pair<TransformedGeometry,
              intrinsic_proto::skills::VolumeReservation::SharingType>>>
ConvertResourceToShapes(
    const World& world,
    const intrinsic_proto::skills::VolumeReservation& volume_reservation,
    const GeometryDeserializer& geolib);

}  // namespace intrinsic

#endif  // INTRINSIC_SKILLS_FOOTPRINT_UTIL_H_
