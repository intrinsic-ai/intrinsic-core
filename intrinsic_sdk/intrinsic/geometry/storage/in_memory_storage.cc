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

#include "intrinsic/geometry/storage/in_memory_storage.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "intrinsic/geometry/api/exact_geometry.h"
#include "intrinsic/geometry/api/geometry.h"
#include "intrinsic/geometry/api/geometry_fingerprint.h"
#include "intrinsic/geometry/api/geometry_options.h"
#include "intrinsic/geometry/api/renderable.h"
#include "intrinsic/geometry/api/renderable_generation.h"
#include "intrinsic/geometry/proto/geometry_storage_refs.pb.h"
#include "intrinsic/geometry/proto/v1/geometry_storage_refs.pb.h"
#include "intrinsic/geometry/proto/v1/material.pb.h"
#include "intrinsic/util/status/status_builder.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/hashing/hashing.h"

namespace intrinsic::geo {

MapGeometryLibrary::MapGeometryLibrary(RawMaps initial_geometry_maps) {
  absl::MutexLock l(geometry_mutex_);
  geometry_map_ = std::move(initial_geometry_maps.geometry_map);
  exact_geometry_map_ = std::move(initial_geometry_maps.exact_geometry_map);
  renderable_map_ = std::move(initial_geometry_maps.renderable_map);
}


absl::StatusOr<Geometry> MapGeometryLibrary::GetGeometry(
    const intrinsic_proto::geometry::GeometryStorageRefs& geo_storage_refs,
    const GeometryOptions& options) const {
  // TODO(b/321989362): Don't use the fingerprint as the identifier.
  const std::string& id = geo_storage_refs.fingerprint();
  absl::MutexLock l(geometry_mutex_);
  auto itr = geometry_map_.find(id);
  if (itr == geometry_map_.end()) {
    return intrinsic::NotFoundErrorBuilder()
           << "Could not find geometry with id: " << id;
  }

  return Geometry(
      ExactGeometry(itr->second.GetExactGeometry(), options),
      itr->second.GetRenderable(), itr->second.KeepRenderableForSerialization(),
      itr->second.material_properties(), /*provenance=*/std::nullopt);
}


absl::StatusOr<Geometry> MapGeometryLibrary::GetGeometry(
    const intrinsic_proto::geometry::v1::GeometryStorageRefs& geo_storage_refs,
    std::optional<intrinsic_proto::geometry::v1::MaterialProperties>
        material_properties) const {
  const std::string& exact_geo_ref = geo_storage_refs.exact_geometry_ref();
  absl::MutexLock l(geometry_mutex_);
  auto exact_geo_itr = exact_geometry_map_.find(exact_geo_ref);
  if (exact_geo_itr == exact_geometry_map_.end()) {
    return intrinsic::NotFoundErrorBuilder()
           << "Could not find exact geometry with ref: " << exact_geo_ref;
  }

  std::shared_ptr<const Renderable> renderable;

  const std::string& renderable_ref = geo_storage_refs.renderable_ref();
  if (!renderable_ref.empty()) {
    auto renderable_itr = renderable_map_.find(renderable_ref);
    if (renderable_itr == renderable_map_.end()) {
      return intrinsic::NotFoundErrorBuilder()
             << "Could not find renderable with ref: " << renderable_ref;
    }
    renderable = renderable_itr->second;
  }

  const bool keep_renderable =
      geo_storage_refs.keep_renderable() && renderable != nullptr;

  return Geometry(exact_geo_itr->second, std::move(renderable), keep_renderable,
                  material_properties);
}


absl::StatusOr<intrinsic_proto::geometry::GeometryStorageRefs>
MapGeometryLibrary::SaveGeometry(const Geometry& geometry) {
  INTR_ASSIGN_OR_RETURN(std::string geo_hash_id, GenerateFingerprint(geometry));

  {
    absl::MutexLock l(geometry_mutex_);
    geometry_map_.try_emplace(geo_hash_id, geometry);
  }

  intrinsic_proto::geometry::GeometryStorageRefs storage_refs;
  storage_refs.set_fingerprint(geo_hash_id);
  return storage_refs;
}


absl::StatusOr<intrinsic_proto::geometry::v1::GeometryStorageRefs>
MapGeometryLibrary::SaveGeometryV1(const Geometry& geometry,
                                   const SaveGeometryOptions& options) {
  if (options.HasCustomKeys()) {
    return absl::UnimplementedError(
        "Custom storage keys are not yet implemented in MapGeometryLibrary.");
  }
  INTR_ASSIGN_OR_RETURN(std::string geo_hash_id, GenerateFingerprint(geometry));
  intrinsic_proto::geometry::v1::GeometryStorageRefs storage_refs;
  {
    INTR_ASSIGN_OR_RETURN(std::string exact_geo_hash_id,
                          GenerateFingerprint(geometry.GetExactGeometry()));

    absl::MutexLock l(geometry_mutex_);
    geometry_map_.try_emplace(geo_hash_id, geometry);
    exact_geometry_map_.try_emplace(exact_geo_hash_id,
                                    geometry.GetExactGeometry());
    storage_refs.set_exact_geometry_ref(exact_geo_hash_id);
    // Write out the renderable if we need to keep it.
    if (geometry.KeepRenderableForSerialization()) {
      INTR_ASSIGN_OR_RETURN(auto renderable, GetOrGenerateRenderable(geometry));
      std::string renderable_hash_id = GenerateFingerprint(*renderable);
      renderable_map_.try_emplace(renderable_hash_id, renderable);
      storage_refs.set_renderable_ref(renderable_hash_id);
    }
    storage_refs.set_keep_renderable(geometry.KeepRenderableForSerialization());
  }

  return storage_refs;
}

MapGeometryLibrary::RawMaps MapGeometryLibrary::GetMaps() const {
  absl::MutexLock l(geometry_mutex_);
  return {.geometry_map = geometry_map_,
          .exact_geometry_map = exact_geometry_map_,
          .renderable_map = renderable_map_};
}

std::unique_ptr<MapGeometryLibrary> GetMapGeometryLibrary() {
  return std::make_unique<MapGeometryLibrary>();
}

std::unique_ptr<MapGeometryLibrary> GetMapGeometryLibrary(
    MapGeometryLibrary::RawMaps initial_geometry_maps) {
  return std::make_unique<MapGeometryLibrary>(std::move(initial_geometry_maps));
}

}  // namespace intrinsic::geo
