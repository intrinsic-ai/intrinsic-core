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

#include "intrinsic/geometry/storage/cas_storage.h"

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "grpcpp/client_context.h"
#include "grpcpp/server_context.h"
#include "grpcpp/support/string_ref.h"
#include "grpcpp/support/time.h"
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
#include "intrinsic/geometry/proto/v1/exact_geometry.pb.h"
#include "intrinsic/geometry/proto/v1/geometry.pb.h"
#include "intrinsic/geometry/proto/v1/geometry_storage_refs.pb.h"
#include "intrinsic/geometry/proto/v1/material.pb.h"
#include "intrinsic/geometry/storage/geo_cache.h"
#include "intrinsic/geometry/storage/geometry_deserializer.h"
#include "intrinsic/geometry/storage/geometry_library.h"
#include "intrinsic/geometry/storage/geometry_serializer.h"
#include "intrinsic/kubernetes/acl/cc/client_context.h"
#include "intrinsic/storage/content_addressable_storage/cpp/client_helpers.h"
#include "intrinsic/storage/content_addressable_storage/proto/cas_service.grpc.pb.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic::geo {
namespace {

constexpr absl::Time kCasDeadline = absl::InfiniteFuture();

absl::StatusOr<std::tuple<std::string, std::string>> UploadToCASV0(
    std::function<std::unique_ptr<grpc::ClientContext>()>& make_client_context,
    const Geometry& geometry,
    intrinsic_proto::content_addressable_storage::v1::
        ContentAddressableStorageService::StubInterface* cas_stub) {
  INTR_ASSIGN_OR_RETURN(intrinsic_proto::geometry::Geometry geo_proto,
                        geometry_compatibility::ToProto(geometry));

  std::string geometry_uri;
  std::string renderable_uri;
  {
    std::string geo_proto_contents;
    if (!geo_proto.SerializeToString(&geo_proto_contents)) {
      return absl::InternalError("Failed to serialize Geometry proto");
    }
    auto cli_context = make_client_context();
    INTR_ASSIGN_OR_RETURN(geometry_uri,
                          ContentAddressableStorageCreate(
                              cli_context.get(), cas_stub, geo_proto_contents),
                          _ << "UploadToCAS failed to "
                               "ContentAddressableStorageCreate() geometry");
  }
  {
    INTR_ASSIGN_OR_RETURN(auto renderable, GetOrGenerateRenderable(geometry));
    const auto glb_contents = renderable->GetGLBString();
    auto cli_context = make_client_context();
    INTR_ASSIGN_OR_RETURN(
        renderable_uri,
        ContentAddressableStorageCreate(cli_context.get(), cas_stub,
                                        glb_contents),
        _ << "UploadToCAS failed to ContentAddressableStorageCreate() "
             "glb_contents");
  }

  return std::make_tuple(geometry_uri, renderable_uri);
}

struct CasRefsV1 {
  std::string exact_geo_uri;
  std::string renderable_uri;
};

absl::StatusOr<CasRefsV1> UploadToCASV1(
    std::function<std::unique_ptr<grpc::ClientContext>()>& make_client_context,
    const Geometry& geometry,
    intrinsic_proto::content_addressable_storage::v1::
        ContentAddressableStorageService::StubInterface* cas_stub) {
  INTR_ASSIGN_OR_RETURN(
      intrinsic_proto::geometry::v1::ExactGeometry exact_geo_proto,
      ToProto(geometry.GetExactGeometry()));

  CasRefsV1 refs;
  {
    std::string exact_geo_proto_contents;
    if (!exact_geo_proto.SerializeToString(&exact_geo_proto_contents)) {
      return absl::InternalError("Failed to serialize ExactGeometry proto");
    }
    auto cli_context = make_client_context();
    INTR_ASSIGN_OR_RETURN(
        refs.exact_geo_uri,
        ContentAddressableStorageCreate(cli_context.get(), cas_stub,
                                        exact_geo_proto_contents),
        _ << "UploadToCAS failed to "
             "ContentAddressableStorageCreate() geometry");
  }
  {
    INTR_ASSIGN_OR_RETURN(auto renderable, GetOrGenerateRenderable(geometry));
    const auto glb_contents = renderable->GetGLBString();
    auto cli_context = make_client_context();
    INTR_ASSIGN_OR_RETURN(
        refs.renderable_uri,
        ContentAddressableStorageCreate(cli_context.get(), cas_stub,
                                        glb_contents),
        _ << "UploadToCAS failed to ContentAddressableStorageCreate() "
             "glb_contents");
  }
  return refs;
}

using ::intrinsic_proto::content_addressable_storage::v1::
    ContentAddressableStorageService;

class CasGeometryLibrary : public GeometryLibrary,
                           private GeometryDeserializer,
                           private GeometrySerializer {
 public:
  // geometry_cache must outlive this object.
  CasGeometryLibrary(
      std::shared_ptr<ContentAddressableStorageService::StubInterface> cas_stub,
      GeometryFingerprintCacheInterface& geometry_cache,
      std::function<std::unique_ptr<grpc::ClientContext>()> make_client_context)
      : cache_(&geometry_cache),
        cas_stub_(cas_stub),
        make_client_context_(std::move(make_client_context)) {}

  ~CasGeometryLibrary() override = default;

  const GeometryDeserializer& Deserializer() const override { return *this; }
  GeometrySerializer& Serializer() override { return *this; }

  absl::StatusOr<Geometry> GetGeometry(
      const intrinsic_proto::geometry::GeometryStorageRefs& geo_storage_refs,
      const GeometryOptions& options) const override {
    if (std::optional<Geometry> geo = cache_->GetGeometry(geo_storage_refs);
        geo.has_value()) {
      return geo.value();
    }

    if (geo_storage_refs.geometry_ref().empty()) {
      return absl::InvalidArgumentError(
          "Geometry storage ref must have a `geometry_ref`");
    }

    auto context = make_client_context_();
    INTR_ASSIGN_OR_RETURN(
        auto geo_bytes,
        ContentAddressableStorageGet(context.get(), cas_stub_.get(),
                                     geo_storage_refs.geometry_ref()),
        _ << "Failed to get geometry from CAS");
    intrinsic_proto::geometry::Geometry geo_proto;
    if (!geo_proto.ParseFromString(geo_bytes)) {
      return absl::InternalError("Failed to parse Geometry proto");
    }
    INTR_ASSIGN_OR_RETURN(Geometry geo,
                          geometry_compatibility::ToGeometry(geo_proto, options,
                                                             geo_storage_refs));

    cache_->InsertGeometry(geo_storage_refs, geo);
    return geo;
  }

  absl::StatusOr<Geometry> GetGeometry(
      const intrinsic_proto::geometry::v1::GeometryStorageRefs&
          geo_storage_refs,
      std::optional<intrinsic_proto::geometry::v1::MaterialProperties>
          material_properties) const override {
    if (std::optional<Geometry> geo = cache_->GetGeometry(geo_storage_refs);
        geo.has_value()) {
      if (material_properties.has_value()) {
        return Geometry(geo->GetExactGeometry(), geo->GetRenderable(),
                        geo->KeepRenderableForSerialization(),
                        material_properties.value());
      }
      return geo.value();
    }

    if (geo_storage_refs.exact_geometry_ref().empty()) {
      return absl::InvalidArgumentError(
          "Geometry storage ref must have a `exact_geometry_ref`");
    }

    // Get the exact geometry from CAS.
    auto exact_geo_context = make_client_context_();
    INTR_ASSIGN_OR_RETURN(
        auto geo_bytes,
        ContentAddressableStorageGet(exact_geo_context.get(), cas_stub_.get(),
                                     geo_storage_refs.exact_geometry_ref()),
        _ << "Failed to get geometry from CAS");
    intrinsic_proto::geometry::v1::ExactGeometry geo_proto;
    if (!geo_proto.ParseFromString(geo_bytes)) {
      return absl::InternalError("Failed to parse ExactGeometry proto");
    }
    INTR_ASSIGN_OR_RETURN(ExactGeometry exact_geo, ToGeometry(geo_proto));

    std::shared_ptr<const Renderable> renderable = nullptr;
    // Optionally get the renderable from CAS.
    if (geo_storage_refs.keep_renderable() &&
        !geo_storage_refs.renderable_ref().empty()) {
      auto renderable_context = make_client_context_();
      INTR_ASSIGN_OR_RETURN(auto gltf_contents,
                            ContentAddressableStorageGet(
                                renderable_context.get(), cas_stub_.get(),
                                geo_storage_refs.renderable_ref()),
                            _ << "Failed to get geometry from CAS");
      renderable = std::make_shared<Renderable>(gltf_contents);
    }

    Geometry geo(std::move(exact_geo), std::move(renderable),
                 geo_storage_refs.keep_renderable(), material_properties);
    cache_->InsertGeometry(geo_storage_refs, geo);
    return std::move(geo);
  }

  absl::StatusOr<intrinsic_proto::geometry::GeometryStorageRefs> SaveGeometry(
      const Geometry& geometry) override {
    INTR_ASSIGN_OR_RETURN(const std::string fingerprint,
                          GenerateFingerprint(geometry));
    if (std::optional<intrinsic_proto::geometry::GeometryStorageRefs> refs =
            cache_->GetGeometryStorageRefs(fingerprint);
        refs.has_value()) {
      return refs.value();
    }

    // Write to CAS, and optionally cache the actual geometry.
    std::string geometry_uri;
    std::string renderable_uri;
    INTR_ASSIGN_OR_RETURN(
        const auto refs_tuple,
        UploadToCASV0(make_client_context_, geometry, cas_stub_.get()));
    std::tie(geometry_uri, renderable_uri) = refs_tuple;
    intrinsic_proto::geometry::GeometryStorageRefs storage_refs;
    storage_refs.set_fingerprint(fingerprint);
    storage_refs.set_geometry_ref(geometry_uri);
    storage_refs.set_renderable_ref(renderable_uri);

    cache_->InsertGeometry(storage_refs, geometry);

    return storage_refs;
  }

  absl::StatusOr<intrinsic_proto::geometry::v1::GeometryStorageRefs>
  SaveGeometryV1(const Geometry& geometry) override {
    INTR_ASSIGN_OR_RETURN(const std::string fingerprint,
                          GenerateFingerprint(geometry));
    if (std::optional<intrinsic_proto::geometry::v1::GeometryStorageRefs> refs =
            cache_->GetGeometryStorageRefsV1(fingerprint);
        refs.has_value()) {
      return refs.value();
    }

    // Write to CAS, and optionally cache the actual geometry.
    INTR_ASSIGN_OR_RETURN(
        (auto [exact_geometry_uri, renderable_uri]),
        UploadToCASV1(make_client_context_, geometry, cas_stub_.get()));
    intrinsic_proto::geometry::v1::GeometryStorageRefs storage_refs;
    *storage_refs.mutable_exact_geometry_ref() = std::move(exact_geometry_uri);
    *storage_refs.mutable_renderable_ref() = std::move(renderable_uri);
    storage_refs.set_keep_renderable(geometry.KeepRenderableForSerialization());
    cache_->InsertGeometry(storage_refs, geometry);

    return storage_refs;
  }

 private:
  GeometryFingerprintCacheInterface* cache_;
  std::shared_ptr<ContentAddressableStorageService::StubInterface> cas_stub_;
  std::function<std::unique_ptr<grpc::ClientContext>()> make_client_context_;
};

}  // namespace

std::unique_ptr<GeometryLibrary> GetCasGeometryLibrary(
    std::shared_ptr<intrinsic_proto::content_addressable_storage::v1::
                        ContentAddressableStorageService::StubInterface>
        cas_stub,
    grpc::ServerContext* context, GeometryFingerprintCache& geometry_cache,
    std::multimap<grpc::string_ref, grpc::string_ref> metadata) {
  // TODO(b/435152643): Remove the hard coded deadline here and increase the
  // deadline at call sites.
  if (context == nullptr) {
    return std::make_unique<CasGeometryLibrary>(
        std::move(cas_stub), geometry_cache,
        [metadata = std::move(metadata)]() {
          std::unique_ptr<grpc::ClientContext> ctx_with_cas_deadline =
              acl::NewClientContext(std::make_unique<grpc::ClientContext>(),
                                    metadata,
                                    /*propagate_all_metadata=*/true);
          ctx_with_cas_deadline->set_deadline(
              grpc::GprTimeSpecFromTime(kCasDeadline));
          return ctx_with_cas_deadline;
        });
  }
  return std::make_unique<CasGeometryLibrary>(
      std::move(cas_stub), geometry_cache, [ctx = context]() {
        std::unique_ptr<grpc::ClientContext> ctx_with_cas_deadline =
            acl::NewClientContext(ctx);
        ctx_with_cas_deadline->set_deadline(
            grpc::GprTimeSpecFromTime(kCasDeadline));
        return ctx_with_cas_deadline;
      });
}

}  // namespace intrinsic::geo
