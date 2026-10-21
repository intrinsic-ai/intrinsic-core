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

#ifndef INTRINSIC_SKILLS_INTERNAL_WORLD_SERVICE_UTILS_H_
#define INTRINSIC_SKILLS_INTERNAL_WORLD_SERVICE_UTILS_H_

#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "intrinsic/geometry/storage/geometry_deserializer.h"
#include "intrinsic/geometry/storage/geometry_library.h"
#include "intrinsic/geometry/storage/geometry_serializer.h"
#include "intrinsic/world/proto/object_world_service.grpc.pb.h"
#include "intrinsic/world/world.h"
#include "intrinsic/world/world.pb.h"

namespace intrinsic {
namespace skills {

// Deserializes all geometry components in the world in-place using the given
// deserializer.
absl::Status DeserializeWorldGeometries(
    World& world, const GeometryDeserializer& deserializer);

// Downloads the world with the given ID from ObjectWorldService (querying
// ListObjects and GetCollisionSettings) as an ObjectWorldService World proto.
absl::StatusOr<intrinsic_proto::world::World>
DownloadWorldProtoFromObjectWorldService(
    absl::string_view world_id,
    intrinsic_proto::world::ObjectWorldService::StubInterface&
        object_world_service);

// Constructs a World instance from an ObjectWorldService World proto (as
// returned by `DownloadWorldProtoFromObjectWorldService` or by
// `ObjectWorldService.GetWorld` with `WORLD_VIEW_FULL`). Only the objects and
// collision settings of `world_proto` are used. If geolib is provided,
// deserializes world geometries in-place.
absl::StatusOr<World> CreateWorldFromObjectWorldProto(
    intrinsic_proto::world::World world_proto,
    GeometryLibrary* geolib = nullptr);

// Downloads the world with the given ID from ObjectWorldService (querying
// ListObjects and GetCollisionSettings) and constructs a World instance.
// If geolib is provided, deserializes world geometries in-place.
absl::StatusOr<World> DownloadWorldFromObjectWorldService(
    absl::string_view world_id,
    intrinsic_proto::world::ObjectWorldService::StubInterface&
        object_world_service,
    GeometryLibrary* geolib = nullptr);

}  // namespace skills
}  // namespace intrinsic

#endif  // INTRINSIC_SKILLS_INTERNAL_WORLD_SERVICE_UTILS_H_
