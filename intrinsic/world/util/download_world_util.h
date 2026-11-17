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

#ifndef INTRINSIC_WORLD_UTIL_DOWNLOAD_WORLD_UTIL_H_
#define INTRINSIC_WORLD_UTIL_DOWNLOAD_WORLD_UTIL_H_

#include <optional>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "intrinsic/geometry/storage/geometry_deserializer.h"
#include "intrinsic/geometry/storage/geometry_library.h"
#include "intrinsic/world/proto/object_world_service.grpc.pb.h"
#include "intrinsic/world/proto/object_world_service.pb.h"
#include "intrinsic/world/world.h"

namespace intrinsic {

// Deserializes geometry components in the world in-place using the given
// deserializer. If `geometry_key` is provided, only geometries with that key
// (e.g. `kKindCollisionGeometry`) are deserialized; otherwise all geometry
// sets are deserialized.
absl::Status DeserializeWorldGeometries(
    World& world, const GeometryDeserializer& deserializer,
    std::optional<absl::string_view> geometry_key = std::nullopt);

// Downloads the world with the given ID from ObjectWorldService (querying
// ListObjects and GetCollisionSettings concurrently) as an ObjectWorldService
// World proto.
absl::StatusOr<intrinsic_proto::world::World>
DownloadWorldProtoFromObjectWorldService(
    absl::string_view world_id,
    intrinsic_proto::world::ObjectWorldService::StubInterface&
        object_world_service);

// Downloads the world with the given ID from ObjectWorldService (querying
// ListObjects and GetCollisionSettings) and constructs a World instance.
// If geolib is provided, deserializes world geometries in-place.
absl::StatusOr<World> DownloadWorldFromObjectWorldService(
    absl::string_view world_id,
    intrinsic_proto::world::ObjectWorldService::StubInterface&
        object_world_service,
    GeometryLibrary* geolib = nullptr);

}  // namespace intrinsic

#endif  // INTRINSIC_WORLD_UTIL_DOWNLOAD_WORLD_UTIL_H_
