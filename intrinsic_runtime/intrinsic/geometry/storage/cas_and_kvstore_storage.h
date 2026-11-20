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

#ifndef INTRINSIC_GEOMETRY_STORAGE_CAS_AND_KVSTORE_STORAGE_H_
#define INTRINSIC_GEOMETRY_STORAGE_CAS_AND_KVSTORE_STORAGE_H_

#include <map>
#include <memory>

#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "grpcpp/server_context.h"
#include "grpcpp/support/string_ref.h"
#include "intrinsic/geometry/storage/geo_cache.h"
#include "intrinsic/geometry/storage/geometry_library.h"
#include "intrinsic/platform/pubsub/kvstore.h"
#include "intrinsic/storage/content_addressable_storage/proto/cas_service.grpc.pb.h"

namespace intrinsic::geo {

// Creates an internal GeometryLibrary for platform runtime services
// (GeometryServer, world_binary, ObjectWorldService, and MotionPlannerService)
// that read belief worlds containing both persistent CAS ("intcas://") and
// transient KeyValueStore ("intkv://") geometries without calling
// GeometryService RPCs. This library is not exported to the external SDK;
// callers that write "intkv://" geometries use `GetKvStoreGeometryLibrary`
// directly.
//
// `Serializer().SaveGeometryV1()` writes and V0 `Deserializer()` reads delegate
// to CAS; V1 `Deserializer()` reads dispatch by URI scheme across CAS and
// KeyValueStore.
//
// `geometry_cache` must outlive the returned `GeometryLibrary`. When `context`
// is non-null, it configures downstream CAS calls and must outlive the returned
// `GeometryLibrary` (create a new instance per RPC); pass `nullptr` outside an
// RPC scope, in which case `cas_metadata` configures downstream CAS calls.
//
// Returns `INVALID_ARGUMENT` if `cas_stub` or `kv_store` is null, if
// `kv_store->key_prefix()` is invalid, or if `kv_read_timeout` is
// non-positive.
absl::StatusOr<std::unique_ptr<GeometryLibrary>>
CreateCasAndKvStoreGeometryLibrary(
    std::shared_ptr<intrinsic_proto::content_addressable_storage::v1::
                        ContentAddressableStorageService::StubInterface>
        cas_stub,
    grpc::ServerContext* context, GeometryFingerprintCache& geometry_cache,
    std::shared_ptr<KeyValueStore> kv_store,
    absl::Duration kv_read_timeout = kDefaultGetTimeout,
    std::multimap<grpc::string_ref, grpc::string_ref> cas_metadata = {});

}  // namespace intrinsic::geo

#endif  // INTRINSIC_GEOMETRY_STORAGE_CAS_AND_KVSTORE_STORAGE_H_
