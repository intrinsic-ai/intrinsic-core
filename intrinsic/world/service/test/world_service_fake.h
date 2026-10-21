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

#ifndef INTRINSIC_WORLD_SERVICE_TEST_WORLD_SERVICE_FAKE_H_
#define INTRINSIC_WORLD_SERVICE_TEST_WORLD_SERVICE_FAKE_H_

#include <memory>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "grpcpp/channel.h"
#include "grpcpp/server.h"
#include "grpcpp/support/channel_arguments.h"
#include "intrinsic/geometry/api/shape_factory.h"
#include "intrinsic/geometry/proto/v1/geometry_storage_refs.pb.h"
#include "intrinsic/geometry/storage/geometry_library.h"
#include "intrinsic/world/hashing/hashing.h"
#include "intrinsic/world/proto/object_world_service.grpc.pb.h"
#include "intrinsic/world/service/object_world_service.h"
#include "intrinsic/world/service/world_service.grpc.pb.h"
#include "intrinsic/world/service/world_storage.h"
#include "intrinsic/world/service/worldservice_impl.h"
#include "intrinsic/world/world.h"

namespace intrinsic {

// In-process world service that is suitable for use in tests.
//
// Does not support geometry related queries (yet).
class FakeWorldService {
 public:
  struct CreateOptions {
    bool enable_local_tcp = false;
    WorldStorage::LoadWorlds should_load_worlds = WorldStorage::LoadWorlds::kNo;
    static CreateOptions Default() { return {}; }
  };

  // Creates a new instance seeded without any worlds.
  //
  // You can connect to the server via an in-process channel (see New*Stub()).
  // If 'enable_local_tcp' is true, you can additionally connect to the server
  // over local TCP (see GetAddress()).
  static absl::StatusOr<std::unique_ptr<FakeWorldService>> Create(
      const CreateOptions& options = CreateOptions::Default());

  // Creates a new instance seeded with the given worlds.
  //
  // You can connect to the server via an in-process channel (see New*Stub()).
  // If 'enable_local_tcp' is true, you can additionally connect to the server
  // over local TCP (see GetAddress()).
  static absl::StatusOr<std::unique_ptr<FakeWorldService>> Create(
      WorldHashMap<std::string, World> worlds,
      const CreateOptions& options = CreateOptions::Default());

  // Creates a new instance which is seeded with the given worlds and which
  // uses the given geometry storage.
  //
  // You can connect to the server via an in-process channel (see New*Stub()).
  // If 'enable_local_tcp' is true, you can additionally connect to the server
  // over local TCP (see GetAddress()).
  static absl::StatusOr<std::unique_ptr<FakeWorldService>> Create(
      WorldHashMap<std::string, World> worlds,
      std::unique_ptr<GeometryLibrary> geo_lib, const CreateOptions& options);

  // Adds the given world to the running world service. Returns an error if
  // the world already exists.
  absl::Status AddWorld(absl::string_view world_id, World world);

  // Loads the given world gzf file and adds it to the running world service.
  // Returns an error if loading fails or the `world_id` already exists.
  absl::Status AddWorldFromFile(absl::string_view world_id,
                                absl::string_view filename);

  // Returns a copy of the world with the given id in the world service.
  // Returns an error if the world does not exist.
  absl::StatusOr<World> GetWorldCopy(absl::string_view world_id);

  // Returns the local TCP address on which the GRPC service is listening.
  // Requires 'enable_local_tcp' to be set in Create().
  absl::StatusOr<std::string> GetAddress() const;

  // Returns a new stub for the fake world service that uses an in-process
  // channel.
  std::unique_ptr<intrinsic_proto::world::internal::WorldService::Stub>
  NewStub() {
    return intrinsic_proto::world::internal::WorldService::NewStub(
        server_->InProcessChannel(grpc::ChannelArguments()));
  }

  // Returns a new ObjectWorldService stub for the fake world service that
  // uses an in-process channel.
  std::unique_ptr<intrinsic_proto::world::ObjectWorldService::Stub>
  NewObjectStub() {
    return intrinsic_proto::world::ObjectWorldService::NewStub(
        server_->InProcessChannel(grpc::ChannelArguments()));
  }

  // Returns a reference to the ObjectWorldService instance.
  intrinsic_proto::world::ObjectWorldService::Service& GetObjectWorldService() {
    return *object_world_service_;
  }

  std::shared_ptr<grpc::Channel> InProcessChannel() const {
    return server_->InProcessChannel(grpc::ChannelArguments());
  }

  // Returns a pointer to the geometry library.
  std::shared_ptr<GeometryLibrary> GetGeometryLibrary() {
    return geo_lib_factory_->GetSharedGeometryLib();
  }

  absl::StatusOr<intrinsic_proto::geometry::v1::GeometryStorageRefs>
  CreateSphereGeometry(float radius) {
    return GetGeometryLibrary()->Serializer().SaveGeometryV1(
        MakeSphere(radius));
  }

 private:
  class FakeGeoLibFactory {
   public:
    explicit FakeGeoLibFactory(std::shared_ptr<GeometryLibrary> geo_lib);
    std::shared_ptr<GeometryLibrary> GetSharedGeometryLib() { return geo_lib_; }
    GeometryLibrary& GetGeoLib() { return *geo_lib_; }

   private:
    std::shared_ptr<GeometryLibrary> geo_lib_;
  };

  FakeWorldService(
      std::shared_ptr<FakeGeoLibFactory> geo_lib_factory,
      std::unique_ptr<WorldStorage> world_storage,
      std::unique_ptr<WorldServiceImpl> world_service,
      std::unique_ptr<object_world::ObjectWorldService> object_world_service,
      std::unique_ptr<grpc::Server> server, std::string address);

  std::shared_ptr<FakeGeoLibFactory> geo_lib_factory_;
  std::unique_ptr<WorldStorage> world_storage_;
  std::unique_ptr<WorldServiceImpl> world_service_;
  std::unique_ptr<object_world::ObjectWorldService> object_world_service_;
  std::unique_ptr<grpc::Server> server_;
  std::string address_;
};

}  // namespace intrinsic

#endif  // INTRINSIC_WORLD_SERVICE_TEST_WORLD_SERVICE_FAKE_H_
