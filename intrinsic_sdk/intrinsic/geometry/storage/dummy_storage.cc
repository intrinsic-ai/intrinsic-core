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

#include "intrinsic/geometry/storage/dummy_storage.h"

#include <memory>
#include <optional>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "intrinsic/geometry/api/exact_geometry.h"
#include "intrinsic/geometry/api/geometry.h"
#include "intrinsic/geometry/api/geometry_fingerprint.h"
#include "intrinsic/geometry/api/geometry_options.h"
#include "intrinsic/geometry/proto/geometry_storage_refs.pb.h"
#include "intrinsic/geometry/proto/v1/geometry_storage_refs.pb.h"
#include "intrinsic/geometry/proto/v1/material.pb.h"
#include "intrinsic/geometry/storage/geometry_deserializer.h"
#include "intrinsic/geometry/storage/geometry_library.h"
#include "intrinsic/geometry/storage/geometry_serializer.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic::geo {
namespace {

class DummyGeometryLibrary : public GeometryLibrary,
                             private GeometryDeserializer,
                             private GeometrySerializer {
  GeometrySerializer& Serializer() override { return *this; }
  const GeometryDeserializer& Deserializer() const override { return *this; }


  absl::StatusOr<Geometry> GetGeometry(
      const intrinsic_proto::geometry::GeometryStorageRefs& geo_storage_refs,
      const GeometryOptions& options) const override {
    return Geometry(ExactGeometry(result_.GetExactGeometry(), options),
                    result_.GetRenderable(),
                    result_.KeepRenderableForSerialization(),
                    result_.material_properties(), /*provenance=*/std::nullopt);
  }


  absl::StatusOr<Geometry> GetGeometry(
      const intrinsic_proto::geometry::v1::GeometryStorageRefs&
          geo_storage_refs,
      std::optional<intrinsic_proto::geometry::v1::MaterialProperties>
          material_properties) const override {
    return Geometry(
        ExactGeometry(result_.GetExactGeometry()), result_.GetRenderable(),
        result_.KeepRenderableForSerialization(), material_properties);
  }


  absl::StatusOr<intrinsic_proto::geometry::GeometryStorageRefs> SaveGeometry(
      const Geometry& geometry) override {
    intrinsic_proto::geometry::GeometryStorageRefs geo_storage_refs;
    INTR_ASSIGN_OR_RETURN(std::string fingerprint,
                          GenerateFingerprint(geometry));
    geo_storage_refs.set_fingerprint(fingerprint);
    return geo_storage_refs;
  }


  absl::StatusOr<intrinsic_proto::geometry::v1::GeometryStorageRefs>
  SaveGeometryV1(const Geometry& geometry,
                 const SaveGeometryOptions& options) override {
    if (options.HasCustomKeys()) {
      return absl::InvalidArgumentError(
          "DummyGeometryLibrary is a stateless stub and does not accept custom "
          "storage keys.");
    }
    intrinsic_proto::geometry::v1::GeometryStorageRefs geo_storage_refs;
    INTR_ASSIGN_OR_RETURN(std::string exact_fingerprint,
                          GenerateFingerprint(geometry.GetExactGeometry()));
    geo_storage_refs.set_exact_geometry_ref(exact_fingerprint);
    if (geometry.GetRenderable() != nullptr &&
        geometry.KeepRenderableForSerialization()) {
      std::string renderable_fingerprint =
          GenerateFingerprint(*geometry.GetRenderable());
      geo_storage_refs.set_renderable_ref(renderable_fingerprint);
      geo_storage_refs.set_keep_renderable(
          geometry.KeepRenderableForSerialization());
    }
    return geo_storage_refs;
  }

 private:
  const Geometry result_ = {};
};

}  // namespace

std::unique_ptr<GeometryLibrary> GetDummyGeometryLibrary() {
  return std::make_unique<DummyGeometryLibrary>();
}

}  // namespace intrinsic::geo
