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

#include "intrinsic/geometry/storage/geometry_service_storage.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/log/check.h"
#include "absl/log/die_if_null.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "grpcpp/client_context.h"
#include "grpcpp/server_context.h"
#include "grpcpp/support/channel_arguments.h"
#include "grpcpp/support/time.h"
#include "intrinsic/connect/cc/grpc/channel.h"
#include "intrinsic/geometry/api/exact_geometry.h"
#include "intrinsic/geometry/api/geometry.h"
#include "intrinsic/geometry/api/geometry_fingerprint.h"
#include "intrinsic/geometry/api/geometry_options.h"
#include "intrinsic/geometry/api/io.h"
#include "intrinsic/geometry/compatibility/io.h"
#include "intrinsic/geometry/proto/geometry.pb.h"
#include "intrinsic/geometry/proto/geometry_service.grpc.pb.h"
#include "intrinsic/geometry/proto/geometry_service.pb.h"
#include "intrinsic/geometry/proto/geometry_storage_refs.pb.h"
#include "intrinsic/geometry/proto/v1/geometry_storage_refs.pb.h"
#include "intrinsic/geometry/proto/v1/material.pb.h"
#include "intrinsic/geometry/storage/geo_cache.h"
#include "intrinsic/geometry/storage/geometry_deserializer.h"
#include "intrinsic/geometry/storage/geometry_library.h"
#include "intrinsic/geometry/storage/geometry_serializer.h"
#include "intrinsic/kubernetes/acl/cc/client_context.h"
#include "intrinsic/util/macros.h"
#include "intrinsic/util/status/status_conversion_grpc.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic::geo {
namespace {

using ::intrinsic_proto::geometry::GeometryStorageRefs;

absl::Time kGeometryServiceDeadline = absl::InfiniteFuture();

// We don't use `ConfigureClientContext` from `intrinsic/util/grpc` here because
// we don't want to set a default deadline for geometry service calls.
// Geometry protos can be very large and take a non-trivial time to transfer.
void ConfigureClientContextForGeo(grpc::ClientContext* client_context) {
  // Blocks until the channel is ready.
  client_context->set_wait_for_ready(true);
  client_context->set_deadline(
      ::grpc::GprTimeSpecFromTime(kGeometryServiceDeadline));
}

class GeometryServiceGeometryLibrary : public GeometryLibrary,
                                       public GeometrySerializer,
                                       public GeometryDeserializer {
 public:
  GeometryServiceGeometryLibrary(
      std::unique_ptr<intrinsic_proto::geometry::GeometryService::StubInterface>
          stub_ptr,
      GeometryFingerprintCacheInterface& geometry_cache,
      std::function<std::unique_ptr<grpc::ClientContext>()> make_client_context)
      : owned_cache_(nullptr),
        cache_(&geometry_cache),
        stub_ptr_(ABSL_DIE_IF_NULL(std::move(stub_ptr))),
        stub_(stub_ptr_.get()),
        make_client_context_(std::move(make_client_context)) {}

  explicit GeometryServiceGeometryLibrary(
      std::unique_ptr<intrinsic_proto::geometry::GeometryService::StubInterface>
          stub_ptr,
      std::function<std::unique_ptr<grpc::ClientContext>()> make_client_context)
      : owned_cache_(std::make_unique<GeometryFingerprintCache>()),
        cache_(owned_cache_.get()),
        stub_ptr_(ABSL_DIE_IF_NULL(std::move(stub_ptr))),
        stub_(stub_ptr_.get()),
        make_client_context_(std::move(make_client_context)) {}

  GeometryServiceGeometryLibrary(
      intrinsic_proto::geometry::GeometryService::StubInterface* stub,
      GeometryFingerprintCacheInterface& geometry_cache,
      std::function<std::unique_ptr<grpc::ClientContext>()> make_client_context)
      : owned_cache_(nullptr),
        cache_(&geometry_cache),
        stub_(ABSL_DIE_IF_NULL(stub)),
        make_client_context_(std::move(make_client_context)) {}

  GeometryServiceGeometryLibrary(
      intrinsic_proto::geometry::GeometryService::StubInterface* stub,
      std::function<std::unique_ptr<grpc::ClientContext>()> make_client_context)
      : owned_cache_(std::make_unique<GeometryFingerprintCache>()),
        cache_(owned_cache_.get()),
        stub_(ABSL_DIE_IF_NULL(stub)),
        make_client_context_(std::move(make_client_context)) {}

  GeometryServiceGeometryLibrary(
      intrinsic_proto::geometry::GeometryService::StubInterface* stub,
      int cache_size,
      std::function<std::unique_ptr<grpc::ClientContext>()> make_client_context)
      : owned_cache_(std::make_unique<GeometryFingerprintCache>(cache_size)),
        cache_(owned_cache_.get()),
        stub_(ABSL_DIE_IF_NULL(stub)),
        make_client_context_(std::move(make_client_context)) {}

  ~GeometryServiceGeometryLibrary() override = default;

  GeometrySerializer& Serializer() override { return *this; }
  const GeometryDeserializer& Deserializer() const override { return *this; }

  absl::StatusOr<Geometry> GetGeometry(
      const intrinsic_proto::geometry::GeometryStorageRefs& geo_storage_refs,
      const GeometryOptions& options) const override {
    if (std::optional<Geometry> geo = cache_->GetGeometry(geo_storage_refs);
        geo.has_value()) {
      return Geometry(ExactGeometry(geo->GetExactGeometry(), options),
                      geo->GetRenderable(),
                      geo->KeepRenderableForSerialization(),
                      geo->material_properties(), /*provenance=*/std::nullopt);
    }

    intrinsic_proto::geometry::GetGeometryRequest request;
    *request.mutable_geometry_storage_refs_v0() = geo_storage_refs;

    auto client_context = make_client_context_();
    ConfigureClientContextForGeo(client_context.get());

    intrinsic_proto::geometry::GeometryWithMetadata response;
    INTR_RETURN_IF_ERROR(ToAbslStatus(
        stub_->GetGeometry(client_context.get(), request, &response)));

    Geometry geo;
    if (response.has_inline_geometry()) {
      INTR_ASSIGN_OR_RETURN(geo, ToGeometry(response.inline_geometry()));
    } else if (response.has_geometry_v0()) {
      INTR_ASSIGN_OR_RETURN(
          geo, geometry_compatibility::ToGeometry(response.geometry_v0(),
                                                  options, geo_storage_refs));
    } else {
      return absl::InternalError("GeometryService returned empty geometry.");
    }

    Geometry geo_with_options = Geometry(
        ExactGeometry(geo.GetExactGeometry(), options), geo.GetRenderable(),
        geo.KeepRenderableForSerialization(), geo.material_properties(),
        /*provenance=*/std::nullopt);

    // Cache under the v1 storage refs returned by GeometryService.
    cache_->InsertGeometry(response.geometry_storage_refs(), geo_with_options);
    // Also cache under the caller's v0 storage refs so subsequent lookups hit
    // the cache.
    cache_->InsertGeometry(geo_storage_refs, geo_with_options);
    return geo_with_options;
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
      return *geo;
    }

    intrinsic_proto::geometry::GetGeometryRequest request;
    *request.mutable_geometry_storage_refs() = geo_storage_refs;

    auto client_context = make_client_context_();
    ConfigureClientContextForGeo(client_context.get());

    intrinsic_proto::geometry::GeometryWithMetadata response;
    INTR_RETURN_IF_ERROR(ToAbslStatus(
        stub_->GetGeometry(client_context.get(), request, &response)));

    Geometry geo;
    if (response.has_inline_geometry()) {
      INTR_ASSIGN_OR_RETURN(geo, ToGeometry(response.inline_geometry()));
    } else {
      return absl::InternalError("GeometryService returned empty geometry.");
    }

    // Add the material properties to the result geo.
    if (material_properties.has_value()) {
      geo = Geometry(ExactGeometry(geo.GetExactGeometry()), geo.GetRenderable(),
                     geo.KeepRenderableForSerialization(),
                     material_properties.value());
    }

    cache_->InsertGeometry(geo_storage_refs, geo);
    return geo;
  }

  absl::StatusOr<GeometryStorageRefs> SaveGeometry(
      const Geometry& geometry) override {
    INTR_ASSIGN_OR_RETURN(std::string hash_id, GenerateFingerprint(geometry));
    if (auto refs = cache_->GetGeometryStorageRefs(hash_id); refs.has_value()) {
      return std::move(refs).value();
    }

    INTR_RETURN_IF_ERROR(SaveGeometryV1(geometry).status());

    GeometryStorageRefs v0_refs;
    v0_refs.set_fingerprint(hash_id);

    cache_->InsertGeometry(v0_refs, geometry);
    return v0_refs;
  }

  absl::StatusOr<intrinsic_proto::geometry::v1::GeometryStorageRefs>
  SaveGeometryV1(const Geometry& geometry) override {
    INTR_ASSIGN_OR_RETURN(std::string hash_id, GenerateFingerprint(geometry));
    if (auto refs = cache_->GetGeometryStorageRefsV1(hash_id);
        refs.has_value()) {
      return std::move(refs).value();
    }

    intrinsic_proto::geometry::CreateGeometryRequest request;
    INTR_ASSIGN_OR_RETURN(auto inlined_geo_proto, ToInlinedProto(geometry));
    *request.mutable_data()->mutable_inline_geometry() =
        std::move(inlined_geo_proto.inline_geometry_data());

    auto client_context = make_client_context_();
    ConfigureClientContextForGeo(client_context.get());
    intrinsic_proto::geometry::GeometryWithMetadata response;

    INTR_RETURN_IF_ERROR(ToAbslStatus(
        stub_->CreateGeometry(client_context.get(), request, &response)));
    if (!response.has_geometry_storage_refs()) {
      return absl::InternalError(
          "GeometryService.CreateGeometry returned empty v1 geometry storage "
          "refs.");
    }
    auto response_storage_refs_v1 = response.geometry_storage_refs();
    cache_->InsertGeometry(response_storage_refs_v1, geometry);
    return response_storage_refs_v1;
  }

 private:
  std::unique_ptr<GeometryFingerprintCacheInterface> owned_cache_;
  GeometryFingerprintCacheInterface* cache_;

  std::unique_ptr<intrinsic_proto::geometry::GeometryService::StubInterface>
      stub_ptr_;
  intrinsic_proto::geometry::GeometryService::StubInterface* stub_;
  std::function<std::unique_ptr<grpc::ClientContext>()> make_client_context_;
};

}  // namespace

std::unique_ptr<GeometryLibrary> GetGeometryServiceGeometryLibrary(
    std::unique_ptr<intrinsic_proto::geometry::GeometryService::StubInterface>
        stub,
    GeometryFingerprintCacheInterface& geometry_cache,
    grpc::ServerContext* context) {
  if (context == nullptr) {
    return std::make_unique<GeometryServiceGeometryLibrary>(
        std::move(stub), geometry_cache,
        []() { return std::make_unique<grpc::ClientContext>(); });
  }
  return std::make_unique<GeometryServiceGeometryLibrary>(
      std::move(stub), geometry_cache,
      [ctx = context]() { return acl::NewClientContext(ctx); });
}

// Same as above, but using a non-owned stub pointer.
std::unique_ptr<GeometryLibrary> GetGeometryServiceGeometryLibrary(
    intrinsic_proto::geometry::GeometryService::StubInterface* stub,
    GeometryFingerprintCacheInterface& geometry_cache,
    grpc::ServerContext* context) {
  if (context == nullptr) {
    return std::make_unique<GeometryServiceGeometryLibrary>(
        stub, geometry_cache,
        []() { return std::make_unique<grpc::ClientContext>(); });
  }
  return std::make_unique<GeometryServiceGeometryLibrary>(
      stub, geometry_cache,
      [ctx = context]() { return acl::NewClientContext(ctx); });
}

// Same as above, but creates a stub for the given `grpc_target` address itself.
std::unique_ptr<GeometryLibrary> GetGeometryServiceGeometryLibrary(
    absl::string_view grpc_target,
    GeometryFingerprintCacheInterface& geometry_cache, absl::Duration timeout,
    grpc::ServerContext* context) {
  grpc::ChannelArguments channel_args;
  channel_args.SetMaxReceiveMessageSize(-1);
  channel_args.SetMaxSendMessageSize(-1);

  ASSIGN_OR_DIE(auto channel,
                connect::CreateClientChannel(grpc_target, absl::Now() + timeout,
                                             channel_args));

  auto stub =
      intrinsic_proto::geometry::GeometryService::NewStub(std::move(channel));
  if (context == nullptr) {
    return std::make_unique<GeometryServiceGeometryLibrary>(
        std::move(stub), geometry_cache,
        []() { return std::make_unique<grpc::ClientContext>(); });
  }
  return std::make_unique<GeometryServiceGeometryLibrary>(
      std::move(stub), geometry_cache,
      [ctx = context]() { return acl::NewClientContext(ctx); });
}

std::unique_ptr<GeometryLibrary> GetGeometryServiceGeometryLibrary(
    std::unique_ptr<intrinsic_proto::geometry::GeometryService::StubInterface>
        stub,
    grpc::ServerContext* context) {
  if (context == nullptr) {
    return std::make_unique<GeometryServiceGeometryLibrary>(
        std::move(stub),
        []() { return std::make_unique<grpc::ClientContext>(); });
  }
  return std::make_unique<GeometryServiceGeometryLibrary>(
      std::move(stub),
      [ctx = context]() { return acl::NewClientContext(ctx); });
}

std::unique_ptr<GeometryLibrary> GetGeometryServiceGeometryLibrary(
    intrinsic_proto::geometry::GeometryService::StubInterface* stub,
    grpc::ServerContext* context) {
  if (context == nullptr) {
    return std::make_unique<GeometryServiceGeometryLibrary>(
        stub, []() { return std::make_unique<grpc::ClientContext>(); });
  }
  return std::make_unique<GeometryServiceGeometryLibrary>(
      stub, [ctx = context]() { return acl::NewClientContext(ctx); });
}

std::unique_ptr<GeometryLibrary> GetGeometryServiceGeometryLibrary(
    absl::string_view grpc_target, absl::Duration timeout,
    grpc::ServerContext* context) {
  grpc::ChannelArguments channel_args;
  channel_args.SetMaxReceiveMessageSize(-1);
  channel_args.SetMaxSendMessageSize(-1);

  ASSIGN_OR_DIE(auto channel,
                connect::CreateClientChannel(grpc_target, absl::Now() + timeout,
                                             channel_args));

  auto stub =
      intrinsic_proto::geometry::GeometryService::NewStub(std::move(channel));
  if (context == nullptr) {
    return std::make_unique<GeometryServiceGeometryLibrary>(
        std::move(stub),
        []() { return std::make_unique<grpc::ClientContext>(); });
  }
  return std::make_unique<GeometryServiceGeometryLibrary>(
      std::move(stub),
      [ctx = context]() { return acl::NewClientContext(ctx); });
}

std::unique_ptr<GeometryLibrary> GetGeometryServiceGeometryLibrary(
    intrinsic_proto::geometry::GeometryService::StubInterface* stub,
    int cache_size) {
  return std::make_unique<GeometryServiceGeometryLibrary>(
      stub, cache_size,
      []() { return std::make_unique<grpc::ClientContext>(); });
}

}  // namespace intrinsic::geo
