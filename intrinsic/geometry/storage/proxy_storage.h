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

#ifndef INTRINSIC_GEOMETRY_STORAGE_PROXY_STORAGE_H_
#define INTRINSIC_GEOMETRY_STORAGE_PROXY_STORAGE_H_

#include <optional>

#include "absl/status/statusor.h"
#include "intrinsic/geometry/api/geometry.h"
#include "intrinsic/geometry/api/geometry_options.h"
#include "intrinsic/geometry/proto/geometry_storage_refs.pb.h"
#include "intrinsic/geometry/proto/v1/geometry_storage_refs.pb.h"
#include "intrinsic/geometry/storage/geometry_deserializer.h"
#include "intrinsic/geometry/storage/geometry_library.h"
#include "intrinsic/geometry/storage/geometry_serializer.h"

namespace intrinsic::geo {
// A GeometryLibrary that is backed by a another library.
class ProxyStorageLibrary : public GeometryLibrary,
                            private GeometrySerializer,
                            private GeometryDeserializer {
 public:
  explicit ProxyStorageLibrary(GeometryLibrary& storage);
  ~ProxyStorageLibrary() override = default;

  const GeometryDeserializer& Deserializer() const override;
  GeometrySerializer& Serializer() override;

  // TODO(b/442842561): Remove this method once all users are migrated to v1.
  absl::StatusOr<Geometry> GetGeometry(
      const intrinsic_proto::geometry::GeometryStorageRefs& geo_storage_refs,
      const GeometryOptions& options) const override;

  absl::StatusOr<Geometry> GetGeometry(
      const intrinsic_proto::geometry::v1::GeometryStorageRefs&
          geo_storage_refs,
      std::optional<intrinsic_proto::geometry::v1::MaterialProperties>
          material_properties) const override;

  // TODO(442842561): Remove this method once all users are migrated to v1.
  absl::StatusOr<intrinsic_proto::geometry::GeometryStorageRefs> SaveGeometry(
      const Geometry& geometry) override;

  absl::StatusOr<intrinsic_proto::geometry::v1::GeometryStorageRefs>
  SaveGeometryV1(const Geometry& geometry,
                 const SaveGeometryOptions& options) override;

 private:
  GeometryLibrary* storage_;
};

}  // namespace intrinsic::geo
#endif  // INTRINSIC_GEOMETRY_STORAGE_PROXY_STORAGE_H_
