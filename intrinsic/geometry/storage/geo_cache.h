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

#ifndef INTRINSIC_GEOMETRY_STORAGE_GEO_CACHE_H_
#define INTRINSIC_GEOMETRY_STORAGE_GEO_CACHE_H_

#include <optional>
#include <string>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "intrinsic/geometry/api/geometry.h"
#include "intrinsic/geometry/proto/geometry_storage_refs.pb.h"
#include "intrinsic/geometry/proto/v1/geometry_storage_refs.pb.h"
#include "intrinsic/util/lru_cache.h"
#include "intrinsic/util/proto/pb_hash.h"

namespace intrinsic::geo {
class GeometryFingerprintCacheInterface {
 public:
  virtual ~GeometryFingerprintCacheInterface() = default;
  // TODO(b/442842561): Remove v0 version functions from this interface once all
  // usages are migrated to v1 geometry protos.
  virtual std::optional<Geometry> GetGeometry(
      const intrinsic_proto::geometry::GeometryStorageRefs& geo_storage_refs)
      const = 0;

  virtual void InsertGeometry(
      const intrinsic_proto::geometry::GeometryStorageRefs& geo_storage_refs,
      const Geometry& geometry) = 0;

  virtual std::optional<Geometry> GetGeometry(
      const intrinsic_proto::geometry::v1::GeometryStorageRefs&
          geo_storage_refs) const = 0;

  virtual std::optional<intrinsic_proto::geometry::v1::GeometryStorageRefs>
  GetGeometryStorageRefsV1(absl::string_view fingerprint) const = 0;

  virtual void InsertGeometry(
      const intrinsic_proto::geometry::v1::GeometryStorageRefs&
          geo_storage_refs,
      const Geometry& geometry) = 0;
};

// A cache for geometry and geometry storage refs.
//
// Maps fingerprints to geometry storage refs and geometry.
class GeometryFingerprintCache : public GeometryFingerprintCacheInterface {
 public:
  explicit GeometryFingerprintCache(int cache_size = kGeometryCacheSize);

  ~GeometryFingerprintCache() override;

  std::optional<Geometry> GetGeometry(
      const intrinsic_proto::geometry::GeometryStorageRefs& geo_storage_refs)
      const override;

  std::optional<Geometry> GetGeometry(
      const intrinsic_proto::geometry::v1::GeometryStorageRefs&
          geo_storage_refs) const override;

  std::optional<intrinsic_proto::geometry::v1::GeometryStorageRefs>
  GetGeometryStorageRefsV1(absl::string_view fingerprint) const override;

  // Inserts the geometry.
  //
  // This operation is atomic.
  void InsertGeometry(
      const intrinsic_proto::geometry::GeometryStorageRefs& geo_storage_refs,
      const Geometry& geometry) override;

  void InsertGeometry(const intrinsic_proto::geometry::v1::GeometryStorageRefs&
                          geo_storage_refs,
                      const Geometry& geometry) override;

 private:
  static constexpr int kGeometryCacheSize = 1000;

  mutable absl::Mutex geometry_cache_mutex_;
  // TODO(b/442842561): Remove v0 cache once all usages are migrated to v1
  // geometry protos.
  mutable intrinsic::LruCache<intrinsic_proto::geometry::GeometryStorageRefs,
                              Geometry, intrinsic::pb_hash,
                              intrinsic::pb_equals>
      geometry_cache_ ABSL_GUARDED_BY(geometry_cache_mutex_);
  mutable intrinsic::LruCache<
      intrinsic_proto::geometry::v1::GeometryStorageRefs, Geometry,
      intrinsic::pb_hash, intrinsic::pb_equals>
      geometry_cache_v1_ ABSL_GUARDED_BY(geometry_cache_mutex_);

  mutable absl::Mutex fingerprint_to_geo_storage_refs_mutex_;
  absl::flat_hash_map<std::string,
                      intrinsic_proto::geometry::v1::GeometryStorageRefs>
      fingerprint_to_geo_storage_refs_v1_
          ABSL_GUARDED_BY(fingerprint_to_geo_storage_refs_mutex_);
};

}  // namespace intrinsic::geo

namespace intrinsic {
using ::intrinsic::geo::GeometryFingerprintCache;
}  // namespace intrinsic
#endif  // INTRINSIC_GEOMETRY_STORAGE_GEO_CACHE_H_
