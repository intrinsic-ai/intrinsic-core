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

#include "intrinsic/world/service/test/utils.h"

#include <memory>
#include <string>
#include <utility>

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "grpc/grpc_security_constants.h"
#include "grpcpp/security/server_credentials.h"
#include "grpcpp/server_builder.h"
#include "grpcpp/support/channel_arguments.h"
#include "intrinsic/geometry/proto/geometry_service.grpc.pb.h"
#include "intrinsic/geometry/service/in_memory_geometry_service.h"
#include "intrinsic/geometry/storage/geometry_service_storage.h"
#include "intrinsic/production/external/tsl/net.h"
#include "intrinsic/util/path_resolver/path_resolver.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/gzfile/gzfile.h"
#include "intrinsic/world/hashing/hashing.h"
#include "intrinsic/world/service/test/world_service_fake.h"
#include "intrinsic/world/world.h"

namespace intrinsic {

absl::StatusOr<WorldServers> InitializeWorldService(World world) {
  auto geo_service = std::make_unique<geo::InMemoryGeometryService>();
  const std::string geometry_service_address =
      absl::StrCat("localhost:", tsl::net::PickUnusedPortOrDie());
  grpc::ServerBuilder builder;
  builder.AddListeningPort(geometry_service_address,
                           grpc::experimental::LocalServerCredentials(
                               grpc_local_connect_type::LOCAL_TCP));
  builder.RegisterService(geo_service.get());
  builder.SetMaxReceiveMessageSize(-1);
  builder.SetMaxSendMessageSize(-1);
  std::unique_ptr<grpc::Server> geo_server = builder.BuildAndStart();
  if (geo_server == nullptr) {
    return absl::InternalError("Failed to start geometry server.");
  }
  auto geo_lib = GetGeometryServiceGeometryLibrary(
      intrinsic_proto::geometry::GeometryService::NewStub(
          geo_server->InProcessChannel(grpc::ChannelArguments())));

  WorldHashMap<std::string, World> worlds;
  worlds.emplace("world", std::move(world));

  INTR_ASSIGN_OR_RETURN(
      std::unique_ptr<FakeWorldService> world_service,
      FakeWorldService::Create(
          /*worlds=*/std::move(worlds), std::move(geo_lib),
          FakeWorldService::CreateOptions{.enable_local_tcp = true}));
  return WorldServers{.geometry_service = std::move(geo_service),
                      .geometry_server = std::move(geo_server),
                      .geometry_service_address = geometry_service_address,
                      .world_service = std::move(world_service)};
}

absl::StatusOr<WorldServers> InitializeWorldService(
    absl::string_view world_gzf_path) {
  // Load a World from a fragment.
  std::string full_path;
  if (absl::StartsWith(world_gzf_path, "/")) {
    full_path = world_gzf_path;
  } else {
    full_path = PathResolver::ResolveRunfilesPathForTest(world_gzf_path);
  }
  INTR_ASSIGN_OR_RETURN(std::unique_ptr<intrinsic::GZFile> gzf,
                        intrinsic::GZFile::Open(full_path));
  INTR_ASSIGN_OR_RETURN(World world, World::FromFile(*gzf));
  return InitializeWorldService(std::move(world));
}

}  // namespace intrinsic
