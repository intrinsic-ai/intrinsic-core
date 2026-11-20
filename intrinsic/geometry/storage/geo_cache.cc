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

#include "intrinsic/geometry/storage/geo_cache.h"

#include <optional>
#include <utility>

#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "intrinsic/geometry/api/geometry.h"
#include "intrinsic/geometry/api/geometry_fingerprint.h"

namespace intrinsic::geo {
GeometryFingerprintCache::GeometryFingerprintCache(int cache_size)
    : geometry_cache_(cache_size), geometry_cache_v1_(cache_size) {}

GeometryFingerprintCache::~GeometryFingerprintCache() {
  absl::MutexLock lock(geometry_cache_mutex_);
  geometry_cache_.removeAll();
  geometry_cache_.clear();
  geometry_cache_v1_.removeAll();
  geometry_cache_v1_.clear();
}

std::optional<Geometry> GeometryFingerprintCache::GetGeometry(
    const intrinsic_proto::geometry::GeometryStorageRefs& geo_storage_refs)
    const {
  absl::MutexLock lock(geometry_cache_mutex_);
  if (auto* geo_from_map = geometry_cache_.lookup(geo_storage_refs);
      geo_from_map != nullptr) {
    Geometry geo = *geo_from_map;
    geometry_cache_.release(geo_storage_refs, geo_from_map);
    return std::move(geo);
  }
  return std::nullopt;
}

std::optional<Geometry> GeometryFingerprintCache::GetGeometry(
    const intrinsic_proto::geometry::v1::GeometryStorageRefs& geo_storage_refs)
    const {
  absl::MutexLock lock(geometry_cache_mutex_);
  if (auto* geo_from_map = geometry_cache_v1_.lookup(geo_storage_refs);
      geo_from_map != nullptr) {
    Geometry geo = *geo_from_map;
    geometry_cache_v1_.release(geo_storage_refs, geo_from_map);
    return std::move(geo);
  }
  return std::nullopt;
}

std::optional<intrinsic_proto::geometry::v1::GeometryStorageRefs>
GeometryFingerprintCache::GetGeometryStorageRefsV1(
    absl::string_view fingerprint) const {
  absl::MutexLock lock(fingerprint_to_geo_storage_refs_mutex_);
  if (fingerprint_to_geo_storage_refs_v1_.contains(fingerprint)) {
    return fingerprint_to_geo_storage_refs_v1_.at(fingerprint);
  }
  return std::nullopt;
}

void GeometryFingerprintCache::InsertGeometry(
    const intrinsic_proto::geometry::GeometryStorageRefs& geo_storage_refs,
    const Geometry& geometry) {
  absl::MutexLock lock(geometry_cache_mutex_);
  geometry_cache_.insert(geo_storage_refs, new Geometry(geometry), 1);
}

void GeometryFingerprintCache::InsertGeometry(
    const intrinsic_proto::geometry::v1::GeometryStorageRefs& geo_storage_refs,
    const Geometry& geometry) {
  {
    absl::MutexLock lg(geometry_cache_mutex_);
    geometry_cache_v1_.insert(geo_storage_refs, new Geometry(geometry), 1);
  }

  {
    absl::MutexLock lf(fingerprint_to_geo_storage_refs_mutex_);
    fingerprint_to_geo_storage_refs_v1_[GenerateFingerprintOrDie(geometry)] =
        geo_storage_refs;
  }
}

}  // namespace intrinsic::geo
