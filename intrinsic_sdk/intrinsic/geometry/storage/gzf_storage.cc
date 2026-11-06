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

#include "intrinsic/geometry/storage/gzf_storage.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "intrinsic/geometry/api/exact_geometry.h"
#include "intrinsic/geometry/api/geometry.h"
#include "intrinsic/geometry/api/geometry_fingerprint.h"
#include "intrinsic/geometry/api/geometry_options.h"
#include "intrinsic/geometry/api/io.h"
#include "intrinsic/geometry/api/renderable.h"
#include "intrinsic/geometry/api/renderable_generation.h"

#include "intrinsic/geometry/compatibility/io.h"

#include "intrinsic/geometry/proto/geometry.pb.h"
#include "intrinsic/geometry/proto/geometry_storage_refs.pb.h"
#include "intrinsic/geometry/proto/v1/geometry_storage_refs.pb.h"
#include "intrinsic/geometry/proto/v1/material.pb.h"
#include "intrinsic/geometry/storage/geometry_deserializer.h"
#include "intrinsic/geometry/storage/geometry_library.h"
#include "intrinsic/geometry/storage/geometry_serializer.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/gzfile/chunk_entry.h"
#include "intrinsic/world/gzfile/gzfile.h"

namespace intrinsic::geo {
namespace {

const ChunkId::ValueType kGeometryChunkId = 'GEOM';
const ChunkId::ValueType kGLBChunkId = 'GLTF';
// This chunk will carry all the geometry files (both visual and collision) in
// the right format for exporting to the GCS/CAS.
//
// Since this is just meant for export, these files are not read back in.
// TODO(stoyang): See if we actually need this still

const ChunkId::ValueType kExportChunkId = 'EXPO';


// Version 0 == binary glb string for Renderable..
const uint32_t kRenderableVersionNumber = 0;

// Version 0 == The geometry saved as binary AnyShape proto
// Version 1 == The geometry saved as binary v1::ExactGeometry proto

const uint32_t kGeometryVersionNumber = 0;

const uint32_t kGeometryVersionNumberV1 = 1;
// Version 0 == The geometry saved as a intrinsic_proto::geometry::Geometry for
// collision geometry, or a raw string for the gltf renderable.

const uint32_t kExportVersionNumber = 0;


absl::StatusOr<ChunkKey> ObjectIdToKeyOld(absl::string_view id) {
  uint64_t chunk_key;
  CHECK(absl::SimpleHexAtoi(id, &chunk_key));
  return absl::StrCat(chunk_key);
}


// TODO(b/442842561): Remove this method once all users are migrated to v1
// geometry protos.
absl::StatusOr<Geometry> ReadGeometryFromGzfV0(
    const GZFile& gzfile, const GeometryOptions& options,
    const intrinsic_proto::geometry::GeometryStorageRefs& geo_storage_refs) {
  // TODO(b/321989362): Don't use the fingerprint as the identifier.
  const std::string& id = geo_storage_refs.fingerprint();
  INTR_ASSIGN_OR_RETURN(ChunkKey chunk_key, ObjectIdToKeyOld(id));
  std::optional<ChunkEntry> shape_chunk_entry =
      gzfile.GetChunk(ChunkId(kGeometryChunkId), chunk_key);
  if (shape_chunk_entry == std::nullopt) {
    return NotFoundErrorBuilder() << absl::StrCat(
               "Geometry for object with ID '", id, "' not found in GZFile");
  }

  if (shape_chunk_entry->GetDataVersion() != kGeometryVersionNumber) {
    return DataLossErrorBuilder() << "Unknown shape geo version number "
                                  << shape_chunk_entry->GetDataVersion();
  }

  intrinsic_proto::geometry::LazyExactGeometry shape_proto;
  if (!shape_proto.ParseFromString(shape_chunk_entry->GetUncompressedData())) {
    return DataLossErrorBuilder() << "Could not parse the shape data";
  }

  INTR_ASSIGN_OR_RETURN(
      ExactGeometry shape,
      geometry_compatibility::ToExactGeometry(shape_proto, options));
  std::shared_ptr<const Renderable> renderable;

  const auto maybe_glb_chunk_entry =
      gzfile.GetChunk(ChunkId(kGLBChunkId), chunk_key);

  bool keep_renderable = false;
  if (maybe_glb_chunk_entry.has_value()) {
    const auto& chunk_entry = maybe_glb_chunk_entry.value();
    if (chunk_entry.GetDataVersion() != kRenderableVersionNumber) {
      return DataLossErrorBuilder() << "Unknown renderable version number "
                                    << chunk_entry.GetDataVersion();
    }

    renderable = std::make_shared<Renderable>(
        std::string(chunk_entry.GetUncompressedData()));
    keep_renderable = true;
  }

  return Geometry(std::move(shape), std::move(renderable), keep_renderable,
                  /*material_properties=*/std::nullopt,
                  /*provenance=*/std::nullopt);
}


// TODO(b/442842561): Rename this method once all users are migrated to v1
// geometry protos.
absl::StatusOr<Geometry> ReadGeometryFromGzfV1(
    const GZFile& gzfile,
    std::optional<intrinsic_proto::geometry::v1::MaterialProperties>
        material_properties,
    const intrinsic_proto::geometry::v1::GeometryStorageRefs&
        geo_storage_refs) {
  const std::string& exact_geo_id = geo_storage_refs.exact_geometry_ref();
  INTR_ASSIGN_OR_RETURN(ChunkKey exact_geo_chunk_key,
                        ObjectIdToKeyOld(exact_geo_id),
                        _ << "geo ref " << geo_storage_refs.DebugString());
  std::optional<ChunkEntry> exact_geo_chunk_entry =
      gzfile.GetChunk(ChunkId(kGeometryChunkId), exact_geo_chunk_key);

  if (exact_geo_chunk_entry == std::nullopt) {
    return NotFoundErrorBuilder()
           << absl::StrCat("Geometry for object with ID '", exact_geo_id,
                           "' not found in GZFile");
  }

  if (exact_geo_chunk_entry->GetDataVersion() != kGeometryVersionNumberV1) {
    return DataLossErrorBuilder() << "Unknown shape geo version number "
                                  << exact_geo_chunk_entry->GetDataVersion();
  }

  intrinsic_proto::geometry::v1::ExactGeometry exact_geo_proto;
  if (!exact_geo_proto.ParseFromString(
          exact_geo_chunk_entry->GetUncompressedData())) {
    return DataLossErrorBuilder() << "Could not parse the exact geo data";
  }

  INTR_ASSIGN_OR_RETURN(ExactGeometry shape, ToGeometry(exact_geo_proto));

  absl::string_view renderable_id = geo_storage_refs.renderable_ref();
  std::shared_ptr<const Renderable> renderable;

  if (!renderable_id.empty()) {
    INTR_ASSIGN_OR_RETURN(ChunkKey renderable_chunk_key,
                          ObjectIdToKeyOld(renderable_id));
    const auto glb_chunk_entry =
        gzfile.GetChunk(ChunkId(kGLBChunkId), renderable_chunk_key);

    if (glb_chunk_entry.has_value()) {
      const auto& chunk_entry = glb_chunk_entry.value();
      if (chunk_entry.GetDataVersion() != kRenderableVersionNumber) {
        return DataLossErrorBuilder() << "Unknown renderable version number "
                                      << chunk_entry.GetDataVersion();
      }

      renderable = std::make_shared<Renderable>(
          std::string(chunk_entry.GetUncompressedData()));
    }
  }

  return Geometry(std::move(shape), std::move(renderable),
                  geo_storage_refs.keep_renderable(), material_properties);
}

class GzfGeometryLibrary : public GeometryLibrary,
                           private GeometryDeserializer,
                           private GeometrySerializer {
 public:
  explicit GzfGeometryLibrary(GZFile& gzfile) : gzfile_(gzfile) {}

  GeometrySerializer& Serializer() override { return *this; }
  const GeometryDeserializer& Deserializer() const override { return *this; }


  // TODO(b/442842561): Remove this method once all users are migrated to v1
  // geometry protos.
  absl::StatusOr<Geometry> GetGeometry(
      const intrinsic_proto::geometry::GeometryStorageRefs& geo_storage_refs,
      const GeometryOptions& options) const override {
    return ReadGeometryFromGzfV0(gzfile_, options, geo_storage_refs);
  }


  absl::StatusOr<Geometry> GetGeometry(
      const intrinsic_proto::geometry::v1::GeometryStorageRefs&
          geo_storage_refs,
      std::optional<intrinsic_proto::geometry::v1::MaterialProperties>
          material_properties) const override {
    return ReadGeometryFromGzfV1(gzfile_, material_properties,
                                 geo_storage_refs);
  }


  // TODO(b/442842561): Remove this method once all users are migrated to v1
  // geometry protos.
  absl::StatusOr<intrinsic_proto::geometry::GeometryStorageRefs> SaveGeometry(
      const Geometry& geometry) override {
    INTR_ASSIGN_OR_RETURN(const std::string id, GenerateFingerprint(geometry));
    INTR_ASSIGN_OR_RETURN(ChunkKey chunk_key, ObjectIdToKeyOld(id));

    INTR_ASSIGN_OR_RETURN(
        const intrinsic_proto::geometry::LazyExactGeometry proto,
        geometry_compatibility::ToProto(geometry.GetExactGeometry()));
    ChunkEntry shape_chunk(kGeometryVersionNumber, proto.SerializeAsString());
    INTR_RETURN_IF_ERROR(
        gzfile_.SetChunk(ChunkId(kGeometryChunkId), chunk_key, shape_chunk));

    auto maybe_renderable = geometry.GetRenderable();
    if (geometry.KeepRenderableForSerialization() &&
        maybe_renderable != nullptr) {
      std::string glb_string = maybe_renderable->GetGLBString();

      INTR_RETURN_IF_ERROR(gzfile_.SetChunk(
          ChunkId(kGLBChunkId), chunk_key,
          ChunkEntry(kRenderableVersionNumber, std::move(glb_string))));
    }

    // Write out the files for export.
    // Geometry first.
    {
      INTR_ASSIGN_OR_RETURN(intrinsic_proto::geometry::Geometry geo_proto,
                            geometry_compatibility::ToProto(geometry));
      ChunkEntry export_geometry_chunk(kExportVersionNumber,
                                       geo_proto.SerializeAsString());
      INTR_RETURN_IF_ERROR(
          gzfile_.SetChunk(ChunkId(kExportChunkId), id, export_geometry_chunk));
    }

    // Then renderable.
    {
      INTR_ASSIGN_OR_RETURN(auto renderable, GetOrGenerateRenderable(geometry));
      if (renderable == nullptr) {
        return absl::InternalError("Renderable is null!");
      }
      ChunkEntry export_renderable_chunk(kExportVersionNumber,
                                         renderable->GetGLBString());
      INTR_RETURN_IF_ERROR(gzfile_.SetChunk(ChunkId(kExportChunkId),
                                            absl::StrCat(id, "-gltf"),
                                            export_renderable_chunk));
    }

    intrinsic_proto::geometry::GeometryStorageRefs geo_storage_refs;
    geo_storage_refs.set_fingerprint(id);
    return geo_storage_refs;
  }


  // TODO(b/442842561): Rename this method once all users are migrated to v1
  // geometry protos.
  absl::StatusOr<intrinsic_proto::geometry::v1::GeometryStorageRefs>
  SaveGeometryV1(const Geometry& geometry,
                 const SaveGeometryOptions& options) override {
    if (options.HasCustomKeys()) {
      return absl::InvalidArgumentError(
          "GzfGeometryLibrary requires 64-bit hex chunk keys and does not "
          "accept custom storage keys.");
    }
    INTR_ASSIGN_OR_RETURN(const std::string exact_geo_id,
                          GenerateFingerprint(geometry.GetExactGeometry()));
    INTR_ASSIGN_OR_RETURN(ChunkKey exact_geo_chunk_key,
                          ObjectIdToKeyOld(exact_geo_id));

    intrinsic_proto::geometry::v1::GeometryStorageRefs geo_storage_refs;
    geo_storage_refs.set_exact_geometry_ref(exact_geo_id);

    // Write out the exact geometry.
    INTR_ASSIGN_OR_RETURN(
        const intrinsic_proto::geometry::v1::ExactGeometry proto,
        ToProto(geometry.GetExactGeometry()));

    ChunkEntry exact_geo_chunk(kGeometryVersionNumberV1,
                               proto.SerializeAsString());
    INTR_RETURN_IF_ERROR(gzfile_.SetChunk(
        ChunkId(kGeometryChunkId), exact_geo_chunk_key, exact_geo_chunk));

    // Write out the renderable if we need to keep it.
    if (geometry.KeepRenderableForSerialization()) {
      INTR_ASSIGN_OR_RETURN(auto renderbale, GetOrGenerateRenderable(geometry));
      std::string glb_string = renderbale->GetGLBString();
      std::string renderable_id = GenerateFingerprint(*renderbale);

      INTR_ASSIGN_OR_RETURN(ChunkKey renderable_chunk_key,
                            ObjectIdToKeyOld(renderable_id));

      INTR_RETURN_IF_ERROR(gzfile_.SetChunk(
          ChunkId(kGLBChunkId), renderable_chunk_key,
          ChunkEntry(kRenderableVersionNumber, std::move(glb_string))));
      geo_storage_refs.set_renderable_ref(renderable_id);
    }
    geo_storage_refs.set_keep_renderable(
        geometry.KeepRenderableForSerialization());

    return geo_storage_refs;
  }

 private:
  GZFile& gzfile_;
};

class ReadOnlyGzfGeometryLibrary : public GeometryLibrary,
                                   private GeometryDeserializer,
                                   private GeometrySerializer {
 public:
  explicit ReadOnlyGzfGeometryLibrary(const GZFile& gzfile) : gzfile_(gzfile) {}

  GeometrySerializer& Serializer() override { return *this; }
  const GeometryDeserializer& Deserializer() const override { return *this; }


  // TODO(b/442842561): Remove this method once all users are migrated to v1
  // geometry protos.
  absl::StatusOr<Geometry> GetGeometry(
      const intrinsic_proto::geometry::GeometryStorageRefs& geo_storage_refs,
      const GeometryOptions& options) const override {
    return ReadGeometryFromGzfV0(gzfile_, options, geo_storage_refs);
  }


  absl::StatusOr<Geometry> GetGeometry(
      const intrinsic_proto::geometry::v1::GeometryStorageRefs&
          geo_storage_refs,
      std::optional<intrinsic_proto::geometry::v1::MaterialProperties>
          material_properties) const override {
    return ReadGeometryFromGzfV1(gzfile_, material_properties,
                                 geo_storage_refs);
  }


  absl::StatusOr<intrinsic_proto::geometry::GeometryStorageRefs> SaveGeometry(
      const Geometry& geometry) override {
    return absl::UnimplementedError(
        "ReadOnlyGzfGeometryLibrary does not support writing geometry");
  }


  // TODO(b/442842561): Rename this method once all users are migrated to v1
  // geometry protos.
  absl::StatusOr<intrinsic_proto::geometry::v1::GeometryStorageRefs>
  SaveGeometryV1(const Geometry& /*geometry*/,
                 const SaveGeometryOptions& /*options*/) override {
    return absl::UnimplementedError(
        "ReadOnlyGzfGeometryLibrary does not support writing geometry");
  }

 private:
  const GZFile& gzfile_;
};

}  // namespace

std::unique_ptr<GeometryLibrary> GetGzfGeometryLibrary(GZFile& gzfile) {
  return std::make_unique<GzfGeometryLibrary>(gzfile);
}

std::unique_ptr<GeometryLibrary> GetReadOnlyGzfGeometryLibrary(
    const GZFile& gzfile) {
  return std::make_unique<ReadOnlyGzfGeometryLibrary>(gzfile);
}

}  // namespace intrinsic::geo
