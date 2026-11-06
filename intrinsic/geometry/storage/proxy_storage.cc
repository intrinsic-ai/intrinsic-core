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

#include "intrinsic/geometry/storage/proxy_storage.h"

#include <optional>

#include "absl/log/check.h"
#include "absl/status/statusor.h"
#include "intrinsic/geometry/api/geometry.h"
#include "intrinsic/geometry/api/geometry_options.h"
#include "intrinsic/geometry/proto/geometry_storage_refs.pb.h"
#include "intrinsic/geometry/proto/v1/geometry_storage_refs.pb.h"
#include "intrinsic/geometry/storage/geometry_deserializer.h"
#include "intrinsic/geometry/storage/geometry_library.h"
#include "intrinsic/geometry/storage/geometry_serializer.h"

namespace intrinsic::geo {
ProxyStorageLibrary::ProxyStorageLibrary(GeometryLibrary& storage)
    : storage_(&storage) {}

const GeometryDeserializer& ProxyStorageLibrary::Deserializer() const {
  return *this;
}

GeometrySerializer& ProxyStorageLibrary::Serializer() { return *this; }

absl::StatusOr<Geometry> ProxyStorageLibrary::GetGeometry(
    const intrinsic_proto::geometry::GeometryStorageRefs& geo_storage_refs,
    const GeometryOptions& options) const {
  return storage_->Deserializer().GetGeometry(geo_storage_refs, options);
}

absl::StatusOr<Geometry> ProxyStorageLibrary::GetGeometry(
    const intrinsic_proto::geometry::v1::GeometryStorageRefs& geo_storage_refs,
    std::optional<intrinsic_proto::geometry::v1::MaterialProperties>
        material_properties) const {
  return storage_->Deserializer().GetGeometry(geo_storage_refs,
                                              material_properties);
}

absl::StatusOr<intrinsic_proto::geometry::GeometryStorageRefs>
ProxyStorageLibrary::SaveGeometry(const Geometry& geometry) {
  return storage_->Serializer().SaveGeometry(geometry);
}

absl::StatusOr<intrinsic_proto::geometry::v1::GeometryStorageRefs>
ProxyStorageLibrary::SaveGeometryV1(const Geometry& geometry,
                                    const SaveGeometryOptions& options) {
  return storage_->Serializer().SaveGeometryV1(geometry, options);
}

}  // namespace intrinsic::geo
