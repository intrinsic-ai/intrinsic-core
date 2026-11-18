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

#include "intrinsic/geometry/service/geometry_server.h"

#include <memory>
#include <string>
#include <utility>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "grpcpp/security/server_credentials.h"
#include "grpcpp/server.h"
#include "grpcpp/server_builder.h"
#include "intrinsic/geometry/service/geometry_service_impl.h"
#include "intrinsic/geometry/storage/cas_and_kvstore_storage.h"
#include "intrinsic/geometry/storage/cas_client.h"
#include "intrinsic/geometry/storage/geo_cache.h"
#include "intrinsic/geometry/storage/geometry_library.h"
#include "intrinsic/longrunning/cc/operation_scheduler.h"
#include "intrinsic/longrunning/cc/operation_scheduler_interface.h"
#include "intrinsic/longrunning/cc/operations_service_impl.h"
#include "intrinsic/longrunning/cc/wrappers.h"
#include "intrinsic/platform/pubsub/kvstore.h"
#include "intrinsic/platform/pubsub/pubsub.h"
#include "intrinsic/storage/content_addressable_storage/proto/cas_service.grpc.pb.h"
#include "intrinsic/util/grpc/grpc.h"
#include "intrinsic/util/macros.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic::geo {
namespace {

using ::intrinsic_proto::content_addressable_storage::v1::
    ContentAddressableStorageService;

absl::StatusOr<std::unique_ptr<GeometryServiceImpl>> CreateCloudGeometryService(
    std::shared_ptr<longrunning::OperationSchedulerInterface> scheduler,
    std::shared_ptr<ContentAddressableStorageService::Stub> cas_stub,
    std::shared_ptr<KeyValueStore> kv_store, int cache_size) {
  if (scheduler == nullptr) {
    return absl::InvalidArgumentError("Cannot have a nullptr scheduler");
  }
  if (kv_store == nullptr) {
    return absl::InvalidArgumentError("Cannot have a nullptr kv_store");
  }

  return std::make_unique<GeometryServiceImpl>(
      std::move(scheduler),
      [cas_stub = std::move(cas_stub), kv_store = std::move(kv_store)](
          grpc::ServerContext* context, GeometryFingerprintCache& cache)
          -> absl::StatusOr<std::unique_ptr<GeometryLibrary>> {
        return CreateCasAndKvStoreGeometryLibrary(cas_stub, context, cache,
                                                  kv_store);
      },
      cache_size);
}

}  // namespace

absl::Status RunGeometryServer(const GeometryServerOptions& options) {
  const PubSub pubsub;
  INTR_ASSIGN_OR_RETURN(KeyValueStore kv_store_value, pubsub.KeyValueStore());
  auto kv_store = std::make_shared<KeyValueStore>(std::move(kv_store_value));

  INTR_ASSIGN_OR_RETURN(
      auto scheduler,
      longrunning::wrap_internal::WrapSchedulerWithPrefix(
          "geometry", std::make_shared<longrunning::OperationScheduler>()));

  INTR_ASSIGN_OR_RETURN(auto cas_service_stub,
                        MakeCASServiceStub(options.cas_service_address));

  INTR_ASSIGN_OR_RETURN(
      auto geometry_service,
      CreateCloudGeometryService(scheduler, std::move(cas_service_stub),
                                 std::move(kv_store),
                                 options.geometry_cas_cache_size));

  longrunning::OperationsServiceImpl operations_service(scheduler);

  const std::string server_address = absl::StrCat("[::]:", options.port);
  std::shared_ptr<grpc::ServerCredentials> creds =
      grpc::InsecureServerCredentials();  // NOLINT

  grpc::ServerBuilder builder;
  builder.AddListeningPort(server_address, creds);
  builder.AddChannelArgument(GRPC_ARG_ALLOW_REUSEPORT, 0);
  builder.RegisterService(geometry_service.get());
  builder.RegisterService(&operations_service);

  // Set the max message receive size to unlimited.
  // The default GRPC_DEFAULT_MAX_RECV_MESSAGE_LENGTH is 4MB, too small for
  // geometry meshes.
  builder.SetMaxReceiveMessageSize(-1);

  std::unique_ptr<grpc::Server> server = builder.BuildAndStart();
  if (server == nullptr) {
    return absl::InternalError(
        absl::StrCat("Cannot create Geometry Server ", server_address));
  }

  LOG(INFO) << "Geometry Server listening on " << server_address;

  // Registers signal handler and waits till shutdown.
  ShutdownParams shutdown_params;
  shutdown_params.health_grace_duration = absl::ZeroDuration();
  shutdown_params.shutdown_timeout = options.shutdown_grace_period;
  return RegisterSignalHandlerAndWait(server.get(), shutdown_params,
                                      options.handlers_registered);
}

}  // namespace intrinsic::geo
