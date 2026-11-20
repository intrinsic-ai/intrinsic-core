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

#ifndef INTRINSIC_GEOMETRY_STORAGE_GEOMETRY_DESERIALIZER_H_
#define INTRINSIC_GEOMETRY_STORAGE_GEOMETRY_DESERIALIZER_H_

#include <optional>

#include "absl/status/statusor.h"
#include "intrinsic/geometry/api/geometry.h"
#include "intrinsic/geometry/api/geometry_options.h"

#include "intrinsic/geometry/proto/geometry_storage_refs.pb.h"

#include "intrinsic/geometry/proto/v1/geometry_storage_refs.pb.h"

namespace intrinsic::geo {

// The GeometryDeserializer is responsible for reading geometry data from some
// source based on the given id. The exact details of where the data is stored
// has been abstracted but once the data has been fetched it is placed into a
// Geometry instance.
class GeometryDeserializer {
 public:
  virtual ~GeometryDeserializer() = default;


  // DEPRECATED: `GetGeometry` with
  // `intrinsic_proto::geometry::GeometryStorageRefs` exists primarily for
  // backwards compatibility. Use v1::GeometryStorageRefs version below instead.
  //
  // TODO(b/442842561): Remove this method once all users are migrated to v1.
  // Retrieves a shape and renderable for the given `geo_storage_refs`.
  virtual absl::StatusOr<Geometry> GetGeometry(
      const intrinsic_proto::geometry::GeometryStorageRefs& geo_storage_refs,
      const GeometryOptions& options) const = 0;


  // Retrieves a shape and renderable for the given `geo_storage_refs`. One way
  // to get a valid `GeometryStorageRef` is to save the return value of
  // `GeometrySerializer::SaveGeometry()` when saving a piece of geometry.
  virtual absl::StatusOr<Geometry> GetGeometry(
      const intrinsic_proto::geometry::v1::GeometryStorageRefs&
          geo_storage_refs,
      std::optional<intrinsic_proto::geometry::v1::MaterialProperties>
          material_properties) const = 0;
};

}  // namespace intrinsic::geo

namespace intrinsic {
using ::intrinsic::geo::GeometryDeserializer;
}  // namespace intrinsic

#endif  // INTRINSIC_GEOMETRY_STORAGE_GEOMETRY_DESERIALIZER_H_
