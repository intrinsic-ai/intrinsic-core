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

#include "intrinsic/world/service/test/world_service_fake.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/flags/declare.h"
#include "absl/flags/flag.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "grpc/grpc.h"
#include "grpc/grpc_security_constants.h"
#include "grpcpp/security/server_credentials.h"
#include "grpcpp/server.h"
#include "grpcpp/server_builder.h"
#include "grpcpp/server_context.h"
#include "grpcpp/support/channel_arguments.h"
#include "intrinsic/geometry/api/io.h"
#include "intrinsic/geometry/storage/dummy_storage.h"
#include "intrinsic/geometry/storage/geometry_library.h"
#include "intrinsic/geometry/storage/in_memory_storage.h"
#include "intrinsic/geometry/storage/proxy_storage.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/gzfile/gzfile.h"
#include "intrinsic/world/hashing/hashing.h"
#include "intrinsic/world/proto/object_world_service.grpc.pb.h"
#include "intrinsic/world/service/object_world_service.h"
#include "intrinsic/world/service/world_mutex.h"
#include "intrinsic/world/service/world_service.grpc.pb.h"
#include "intrinsic/world/service/world_storage.h"
#include "intrinsic/world/service/worldservice_impl.h"
#include "intrinsic/world/world.h"

// Defined in intrinsic/world/service/world_storage.cc.
ABSL_DECLARE_FLAG(bool, save_worlds_to_disk);

namespace intrinsic {

absl::StatusOr<std::unique_ptr<FakeWorldService>> FakeWorldService::Create(
    const FakeWorldService::CreateOptions& options) {
  return Create({}, options);
}

absl::StatusOr<std::unique_ptr<FakeWorldService>> FakeWorldService::Create(
    WorldHashMap<std::string, World> worlds,
    const FakeWorldService::CreateOptions& options) {
  return FakeWorldService::Create(std::move(worlds), GetMapGeometryLibrary(),
                                  options);
}

absl::StatusOr<std::unique_ptr<FakeWorldService>> FakeWorldService::Create(
    WorldHashMap<std::string, World> worlds,
    std::unique_ptr<GeometryLibrary> geo_lib,
    const FakeWorldService::CreateOptions& options) {
  // Keep WorldStorage from saving worlds to disk.
  absl::SetFlag(&FLAGS_save_worlds_to_disk, false);

  auto geo_lib_factory =
      std::make_shared<FakeGeoLibFactory>(std::move(geo_lib));
  INTR_ASSIGN_OR_RETURN(
      std::unique_ptr<WorldStorage> world_storage,
      WorldStorage::Create(
          /*worlds_path=*/"", geo_lib_factory->GetGeoLib(),
          /*compat_func=*/std::nullopt, options.should_load_worlds, {}));

  INTR_ASSIGN_OR_RETURN(
      std::unique_ptr<WorldServiceImpl> world_service,
      WorldServiceImpl::CreateService(*world_storage,
                                      /*compat_func=*/std::nullopt,
                                      geo_lib_factory->GetGeoLib()));

  INTR_ASSIGN_OR_RETURN(
      std::unique_ptr<object_world::ObjectWorldService> object_world_service,
      object_world::ObjectWorldService::CreateService(
          *world_storage,
          [geo_lib_factory = geo_lib_factory](grpc::ServerContext* context) {
            return std::make_unique<geo::ProxyStorageLibrary>(
                geo_lib_factory->GetGeoLib());
          }));

  grpc::ServerBuilder server_builder;

  server_builder.RegisterService(world_service.get())
      .RegisterService(object_world_service.get());

  std::string address;
  int auto_selected_port = 0;  // wildcard selector triggers auto-selection
  if (options.enable_local_tcp) {
    server_builder.AddListeningPort(
        "127.0.0.1:0", grpc::experimental::LocalServerCredentials(LOCAL_TCP),
        &auto_selected_port);
    server_builder.AddChannelArgument(GRPC_ARG_ALLOW_REUSEPORT, 0);
  }
  std::unique_ptr<grpc::Server> server = server_builder.BuildAndStart();
  if (options.enable_local_tcp) {
    address = absl::StrCat("127.0.0.1:", auto_selected_port);
  }

  object_world_service->Init(true);

  std::unique_ptr<FakeWorldService> fake_world_service =
      absl::WrapUnique(new FakeWorldService(
          std::move(geo_lib_factory), std::move(world_storage),
          std::move(world_service), std::move(object_world_service),
          std::move(server), std::move(address)));

  for (auto& [world_id, world] : worlds) {
    INTR_RETURN_IF_ERROR(
        fake_world_service->AddWorld(world_id, std::move(world)));
  }

  return fake_world_service;
}

absl::Status FakeWorldService::AddWorld(absl::string_view world_id,
                                        World world) {
  // Make sure that the geometry within the world is also in our storage object.
  GeometryLibrary& geolib = geo_lib_factory_->GetGeoLib();
  for (const auto entity_id : world.GetTypedEntityIds<GeometryEntityId>()) {
    INTR_ASSIGN_OR_RETURN(
        GeometryComponent * geo_component,
        world.GetComponentByEntityId<GeometryComponent>(entity_id));

    // Reserialize geometries into our geo_lib_.
    for (const std::string& geo_set_name : geo_component->GetGeometryNames()) {
      INTR_ASSIGN_OR_RETURN(
          NamedGeometrySet geos,
          geo_component->GetGeometry(
              geo_set_name, GetDummyGeometryLibrary()->Deserializer()));

      NamedGeometryProtoSet geo_protos;
      for (const auto& [geo_name, geo] : geos) {
        INTR_ASSIGN_OR_RETURN(geo_protos[geo_name],
                              ToProto(geo, &geolib.Serializer()));
      }

      geo_component->SetGeometry(geo_set_name, geo_protos);
    }
  }
  return world_storage_
      ->AddWorld(world_id, std::move(world),
                 /*skip_compat_check=*/false)
      .status();
}

absl::Status FakeWorldService::AddWorldFromFile(absl::string_view world_id,
                                                absl::string_view filename) {
  INTR_ASSIGN_OR_RETURN(std::unique_ptr<GZFile> gzfile, GZFile::Open(filename));
  INTR_ASSIGN_OR_RETURN(World world, World::FromFile(*gzfile));
  return AddWorld(world_id, std::move(world));
}

absl::StatusOr<World> FakeWorldService::GetWorldCopy(
    absl::string_view world_id) {
  INTR_ASSIGN_OR_RETURN(std::shared_ptr<WorldAndMutex> world_ptr,
                        world_storage_->GetWorld(world_id));

  absl::ReaderMutexLock lock(*world_ptr->mtx);
  return world_ptr->world.Clone();
}

absl::StatusOr<std::string> FakeWorldService::GetAddress() const {
  if (address_.empty()) {
    return absl::FailedPreconditionError(
        "FakeWorldService has to be created with enable_local_tcp=true in "
        "order to be reachable via a local TCP address.");
  }
  return address_;
}

FakeWorldService::FakeGeoLibFactory::FakeGeoLibFactory(
    std::shared_ptr<GeometryLibrary> geo_lib)
    : geo_lib_(geo_lib) {}

FakeWorldService::FakeWorldService(
    std::shared_ptr<FakeGeoLibFactory> geo_lib_factory,
    std::unique_ptr<WorldStorage> world_storage,
    std::unique_ptr<WorldServiceImpl> world_service,
    std::unique_ptr<object_world::ObjectWorldService> object_world_service,
    std::unique_ptr<grpc::Server> server, std::string address)
    : geo_lib_factory_(geo_lib_factory),
      world_storage_(std::move(world_storage)),
      world_service_(std::move(world_service)),
      object_world_service_(std::move(object_world_service)),
      server_(std::move(server)),
      address_(std::move(address)) {}

}  // namespace intrinsic
