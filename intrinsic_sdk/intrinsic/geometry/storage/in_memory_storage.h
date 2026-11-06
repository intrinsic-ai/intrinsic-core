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

#ifndef INTRINSIC_GEOMETRY_STORAGE_IN_MEMORY_STORAGE_H_
#define INTRINSIC_GEOMETRY_STORAGE_IN_MEMORY_STORAGE_H_

#include <memory>
#include <optional>
#include <string>

#include "absl/base/thread_annotations.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "intrinsic/geometry/api/exact_geometry.h"
#include "intrinsic/geometry/api/geometry.h"
#include "intrinsic/geometry/api/geometry_options.h"
#include "intrinsic/geometry/api/renderable.h"
#include "intrinsic/geometry/proto/geometry_storage_refs.pb.h"
#include "intrinsic/geometry/proto/v1/geometry_storage_refs.pb.h"
#include "intrinsic/geometry/storage/geometry_deserializer.h"
#include "intrinsic/geometry/storage/geometry_library.h"
#include "intrinsic/geometry/storage/geometry_serializer.h"
#include "intrinsic/world/hashing/hashing.h"

namespace intrinsic::geo {

// A GeometryLibrary backed by an in-memory hash map.
class MapGeometryLibrary : public GeometryLibrary,
                           private GeometrySerializer,
                           private GeometryDeserializer {
 public:
  struct RawMaps {
    WorldHashMap<std::string, Geometry> geometry_map;
    WorldHashMap<std::string, ExactGeometry> exact_geometry_map;
    WorldHashMap<std::string, std::shared_ptr<const Renderable>> renderable_map;
  };

  MapGeometryLibrary() = default;
  explicit MapGeometryLibrary(RawMaps initial_geometry_maps);

  const GeometryDeserializer& Deserializer() const override { return *this; }
  GeometrySerializer& Serializer() override { return *this; }

  // Returns a copy of the internal geometry maps. Useful for test
  // introspection.
  RawMaps GetMaps() const;


  absl::StatusOr<Geometry> GetGeometry(
      const intrinsic_proto::geometry::GeometryStorageRefs& geo_storage_refs,
      const GeometryOptions& options) const override;


  absl::StatusOr<Geometry> GetGeometry(
      const intrinsic_proto::geometry::v1::GeometryStorageRefs&
          geo_storage_refs,
      std::optional<intrinsic_proto::geometry::v1::MaterialProperties>
          material_properties) const override;


  absl::StatusOr<intrinsic_proto::geometry::GeometryStorageRefs> SaveGeometry(
      const Geometry& geometry) override;


  absl::StatusOr<intrinsic_proto::geometry::v1::GeometryStorageRefs>
  SaveGeometryV1(const Geometry& geometry,
                 const SaveGeometryOptions& options) override;

 private:
  mutable absl::Mutex geometry_mutex_;
  // TODO(442842561): Remove this map once all users are migrated to v1 geometry
  // protos.
  WorldHashMap<std::string, Geometry> geometry_map_
      ABSL_GUARDED_BY(geometry_mutex_);
  WorldHashMap<std::string, ExactGeometry> exact_geometry_map_
      ABSL_GUARDED_BY(geometry_mutex_);
  WorldHashMap<std::string, std::shared_ptr<const Renderable>> renderable_map_
      ABSL_GUARDED_BY(geometry_mutex_);
};

// Returns a GeometryLibrary backed by an in-memory hash map.
std::unique_ptr<MapGeometryLibrary> GetMapGeometryLibrary();
// Returns a GeometryLibrary backed by an in-memory hash map that is
// initialized to the contents of `initial_geometry_map`. Takes
// `initial_geometry_map` by value to allow move semantics where possible.
std::unique_ptr<MapGeometryLibrary> GetMapGeometryLibrary(
    MapGeometryLibrary::RawMaps initial_geometry_maps);

}  // namespace intrinsic::geo

namespace intrinsic {
using ::intrinsic::geo::GetMapGeometryLibrary;
using ::intrinsic::geo::MapGeometryLibrary;
}  // namespace intrinsic

#endif  // INTRINSIC_GEOMETRY_STORAGE_IN_MEMORY_STORAGE_H_
