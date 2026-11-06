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

#ifndef INTRINSIC_GEOMETRY_STORAGE_GEOMETRY_SERIALIZER_H_
#define INTRINSIC_GEOMETRY_STORAGE_GEOMETRY_SERIALIZER_H_

#include <optional>
#include <string>

#include "absl/status/statusor.h"
#include "intrinsic/geometry/api/geometry.h"

#include "intrinsic/geometry/proto/geometry_storage_refs.pb.h"

#include "intrinsic/geometry/proto/v1/geometry_storage_refs.pb.h"

namespace intrinsic::geo {

// Options for GeometrySerializer::SaveGeometryV1.
struct SaveGeometryOptions {
  // Caller-specified storage key for the exact geometry. Must not be empty if
  // set. Backends that cannot store under caller-specified keys reject it.
  std::optional<std::string> exact_key = std::nullopt;
  // Caller-specified storage key for the renderable. Must not be empty if set.
  // Backends that cannot store under caller-specified keys reject it.
  std::optional<std::string> renderable_key = std::nullopt;
  // Whether to overwrite existing data stored under a caller-specified key.
  // Only applies when `exact_key` or `renderable_key` is set; ignored
  // otherwise.
  bool overwrite_if_exists = false;

  bool HasCustomKeys() const {
    return exact_key.has_value() || renderable_key.has_value();
  }
};

// The GeometrySerializer is responsible for writing geometry to some storage
// backend for a given geometry.
// The exact details of where the data is stored has been abstracted. It is up
// to the implementation to decide how much deduplication to support.
class GeometrySerializer {
 public:
  virtual ~GeometrySerializer() = default;


  // DEPRECATED: this interface exists only for backwards compatibility. Use
  // `v1::GeometryStorageRefs` API below instead.
  //
  // TODO(b/442842561): Remove this method once all users are migrated to v1.
  // Stores the given geometry object and returns its associated id. The id can
  // be used with a GeometryDeserializer configured to the same store to fetch
  // the geometry later.
  virtual absl::StatusOr<intrinsic_proto::geometry::GeometryStorageRefs>
  SaveGeometry(const Geometry& geometry) = 0;


  // Stores the given geometry object using `options` and returns its storage
  // refs. The storage refs can be used with a GeometryDeserializer configured
  // to the same store to fetch the geometry later. Pass `{}` for default
  // options.
  // TODO(b/442842561): Rename to `SaveGeometry` once the above method is
  // removed.
  virtual absl::StatusOr<intrinsic_proto::geometry::v1::GeometryStorageRefs>
  SaveGeometryV1(const Geometry& geometry,
                 const SaveGeometryOptions& options) = 0;
};

}  // namespace intrinsic::geo

namespace intrinsic {
using ::intrinsic::geo::GeometrySerializer;
}  // namespace intrinsic

#endif  // INTRINSIC_GEOMETRY_STORAGE_GEOMETRY_SERIALIZER_H_
