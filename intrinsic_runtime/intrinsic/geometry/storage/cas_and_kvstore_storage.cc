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

#include "intrinsic/geometry/storage/cas_and_kvstore_storage.h"

#include <map>
#include <memory>
#include <optional>
#include <utility>

#include "absl/base/nullability.h"
#include "absl/log/die_if_null.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "grpcpp/server_context.h"
#include "grpcpp/support/string_ref.h"
#include "intrinsic/geometry/api/geometry.h"
#include "intrinsic/geometry/api/geometry_options.h"
#include "intrinsic/geometry/proto/geometry_storage_refs.pb.h"
#include "intrinsic/geometry/proto/v1/geometry_storage_refs.pb.h"
#include "intrinsic/geometry/proto/v1/material.pb.h"
#include "intrinsic/geometry/storage/cas_storage.h"
#include "intrinsic/geometry/storage/geo_cache.h"
#include "intrinsic/geometry/storage/geometry_deserializer.h"
#include "intrinsic/geometry/storage/geometry_library.h"
#include "intrinsic/geometry/storage/geometry_serializer.h"
#include "intrinsic/geometry/storage/geometry_storage_uri.h"
#include "intrinsic/geometry/storage/kvstore_storage.h"
#include "intrinsic/platform/pubsub/kvstore.h"
#include "intrinsic/storage/content_addressable_storage/proto/cas_service.grpc.pb.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic::geo {

using ::intrinsic_proto::content_addressable_storage::v1::
    ContentAddressableStorageService;
using CasStubInterface = ContentAddressableStorageService::StubInterface;

namespace {
using ::intrinsic_proto::geometry::v1::MaterialProperties;
using RefsV0 = ::intrinsic_proto::geometry::GeometryStorageRefs;
using RefsV1 = ::intrinsic_proto::geometry::v1::GeometryStorageRefs;

class CasAndKvStoreGeometryStorage final : public GeometryLibrary,
                                           private GeometryDeserializer,
                                           private GeometrySerializer {
 public:
  CasAndKvStoreGeometryStorage(std::unique_ptr<GeometryLibrary> cas_library,
                               std::unique_ptr<GeometryLibrary> kvstore_library)
      : cas_library_(ABSL_DIE_IF_NULL(std::move(cas_library))),
        kvstore_library_(ABSL_DIE_IF_NULL(std::move(kvstore_library))) {}

  const GeometryDeserializer& Deserializer() const override { return *this; }
  GeometrySerializer& Serializer() override { return *this; }

  absl::StatusOr<Geometry> GetGeometry(
      const RefsV0& refs, const GeometryOptions& options) const override {
    return cas_library_->Deserializer().GetGeometry(refs, options);
  }

  absl::StatusOr<Geometry> GetGeometry(
      const RefsV1& refs,
      std::optional<MaterialProperties> material_properties) const override {
    INTR_ASSIGN_OR_RETURN(const GeometryDeserializer& deserializer,
                          ResolveDeserializer(refs));
    return deserializer.GetGeometry(refs, std::move(material_properties));
  }

  absl::StatusOr<RefsV1> SaveGeometry(
      const Geometry& geometry, const SaveGeometryOptions& options) override {
    return cas_library_->Serializer().SaveGeometry(geometry, options);
  }

 private:
  // Returns the deserializer of the backend whose URI scheme matches
  // `refs.exact_geometry_ref()` (and non-empty `refs.renderable_ref()`).
  absl::StatusOr<const GeometryDeserializer&> ResolveDeserializer(
      const RefsV1& refs) const {
    INTR_ASSIGN_OR_RETURN(const GeometryStorageUriScheme scheme,
                          ResolveGeometryStorageUriScheme(refs));
    switch (scheme) {
      case GeometryStorageUriScheme::kCas:
        return cas_library_->Deserializer();
      case GeometryStorageUriScheme::kKvStore:
        return kvstore_library_->Deserializer();
    }
    return absl::InternalError("Unhandled GeometryStorageUriScheme.");
  }

  const absl_nonnull std::unique_ptr<GeometryLibrary> cas_library_;
  const absl_nonnull std::unique_ptr<GeometryLibrary> kvstore_library_;
};

}  // namespace

absl::StatusOr<std::unique_ptr<GeometryLibrary>>
CreateCasAndKvStoreGeometryLibrary(
    std::shared_ptr<CasStubInterface> cas_stub, grpc::ServerContext* context,
    GeometryFingerprintCache& geometry_cache,
    std::shared_ptr<KeyValueStore> kv_store, absl::Duration kv_read_timeout,
    std::multimap<grpc::string_ref, grpc::string_ref> cas_metadata) {
  if (cas_stub == nullptr) {
    return absl::InvalidArgumentError("cas_stub must not be null.");
  }
  if (kv_store == nullptr) {
    return absl::InvalidArgumentError("kv_store must not be null.");
  }
  INTR_ASSIGN_OR_RETURN(
      std::unique_ptr<GeometryLibrary> kvstore_library,
      GetKvStoreGeometryLibrary(std::move(kv_store), kv_read_timeout));
  std::unique_ptr<GeometryLibrary> cas_library = GetCasGeometryLibrary(
      std::move(cas_stub), context, geometry_cache, std::move(cas_metadata));
  return std::make_unique<CasAndKvStoreGeometryStorage>(
      std::move(cas_library), std::move(kvstore_library));
}

}  // namespace intrinsic::geo
