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

#ifndef INTRINSIC_WORLD_SERVICE_OBJECT_WORLD_SERVICE_H_
#define INTRINSIC_WORLD_SERVICE_OBJECT_WORLD_SERVICE_H_

#include <future>  // NOLINT(build/c++11)
#include <memory>

#include "absl/base/thread_annotations.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "google/protobuf/empty.pb.h"
#include "grpcpp/server_context.h"
#include "grpcpp/support/status.h"
#include "intrinsic/geometry/storage/geometry_library.h"
#include "intrinsic/platform/pubsub/pubsub.h"
#include "intrinsic/world/objects/object_world.h"
#include "intrinsic/world/proto/object_world_refs.pb.h"
#include "intrinsic/world/proto/object_world_service.grpc.pb.h"
#include "intrinsic/world/proto/object_world_service.pb.h"
#include "intrinsic/world/proto/object_world_updates.pb.h"
#include "intrinsic/world/service/world_mutex.h"
#include "intrinsic/world/service/world_storage.h"

namespace intrinsic {
namespace object_world {

// Implementation of the ObjectWorldService.
class ObjectWorldService final
    : public intrinsic_proto::world::ObjectWorldService::Service {
 public:
  // Creates a new instance. You need to follow-up with Init() on the returned,
  // new instance to finish initialization.
  static absl::StatusOr<std::unique_ptr<ObjectWorldService>> CreateService(
      WorldStorage& world_storage,
      absl::AnyInvocable<std::unique_ptr<GeometryLibrary>(grpc::ServerContext*)
                             const>
          make_geo_lib);

  static absl::StatusOr<std::unique_ptr<ObjectWorldService>> CreateService(
      std::future<WorldStorage*> world_storage,
      absl::AnyInvocable<std::unique_ptr<GeometryLibrary>(grpc::ServerContext*)
                             const>
          make_geo_lib);

  void Init(bool disable_asset_frame_edits) {
    disable_asset_frame_edits_ = disable_asset_frame_edits;
  }

  grpc::Status GetWorld(grpc::ServerContext* context,
                        const intrinsic_proto::world::GetWorldRequest* request,
                        intrinsic_proto::world::World* response) override;

  grpc::Status CloneWorld(
      grpc::ServerContext* context,
      const intrinsic_proto::world::CloneWorldRequest* request,
      intrinsic_proto::world::WorldMetadata* response) override;

  grpc::Status SwapWorld(
      grpc::ServerContext* context,
      const intrinsic_proto::world::SwapWorldRequest* request,
      google::protobuf::Empty* response) override;

  grpc::Status DeleteWorld(
      grpc::ServerContext* context,
      const intrinsic_proto::world::DeleteWorldRequest* request,
      google::protobuf::Empty* response) override;

  grpc::Status ListWorlds(
      grpc::ServerContext* context,
      const intrinsic_proto::world::ListWorldsRequest* request,
      intrinsic_proto::world::ListWorldsResponse* response) override;

  grpc::Status CreateWorld(
      grpc::ServerContext* context,
      const intrinsic_proto::world::CreateWorldRequest* request,
      intrinsic_proto::world::WorldMetadata* response) override;

  grpc::Status GetObject(
      grpc::ServerContext* context,
      const intrinsic_proto::world::GetObjectRequest* request,
      intrinsic_proto::world::Object* response) override;

  grpc::Status ListObjects(
      grpc::ServerContext* context,
      const intrinsic_proto::world::ListObjectsRequest* request,
      intrinsic_proto::world::ListObjectsResponse* response) override;

  grpc::Status CreateObject(
      grpc::ServerContext* context,
      const intrinsic_proto::world::CreateObjectRequest* request,
      intrinsic_proto::world::Object* response) override;

  grpc::Status DeleteObject(
      grpc::ServerContext* context,
      const intrinsic_proto::world::DeleteObjectRequest* request,
      google::protobuf::Empty* response) override;

  grpc::Status UpdateObject(
      grpc::ServerContext* context,
      const intrinsic_proto::world::UpdateObjectRequest* request,
      intrinsic_proto::world::Object* response) override;

  grpc::Status UpdateObjectName(
      grpc::ServerContext* context,
      const intrinsic_proto::world::UpdateObjectNameRequest* request,
      intrinsic_proto::world::Object* response) override;

  grpc::Status ReparentObject(
      grpc::ServerContext* context,
      const intrinsic_proto::world::ReparentObjectRequest* request,
      intrinsic_proto::world::Object* response) override;

  grpc::Status ToggleCollisions(
      grpc::ServerContext* context,
      const intrinsic_proto::world::ToggleCollisionsRequest* request,
      intrinsic_proto::world::Objects* response) override;

  grpc::Status UpdateObjectJoints(
      grpc::ServerContext* context,
      const intrinsic_proto::world::UpdateObjectJointsRequest* request,
      intrinsic_proto::world::Object* response) override;

  grpc::Status UpdateKinematicObjectProperties(
      grpc::ServerContext* context,
      const intrinsic_proto::world::UpdateKinematicObjectPropertiesRequest*
          request,
      intrinsic_proto::world::Object* response) override;

  grpc::Status UpdateObjectProperties(
      grpc::ServerContext* context,
      const intrinsic_proto::world::UpdateObjectPropertiesRequest* request,
      intrinsic_proto::world::Object* response) override;

  grpc::Status UpdateEntityProperties(
      grpc::ServerContext* context,
      const intrinsic_proto::world::UpdateEntityPropertiesRequest* request,
      intrinsic_proto::world::Entity* response) override;

  grpc::Status GetFrame(grpc::ServerContext* context,
                        const intrinsic_proto::world::GetFrameRequest* request,
                        intrinsic_proto::world::Frame* response) override;

  grpc::Status ListFrames(
      grpc::ServerContext* context,
      const intrinsic_proto::world::ListFramesRequest* request,
      intrinsic_proto::world::ListFramesResponse* response) override;

  grpc::Status CreateFrame(
      grpc::ServerContext* context,
      const intrinsic_proto::world::CreateFrameRequest* request,
      intrinsic_proto::world::Frame* response) override;

  grpc::Status DeleteFrame(
      grpc::ServerContext* context,
      const intrinsic_proto::world::DeleteFrameRequest* request,
      google::protobuf::Empty* response) override;

  grpc::Status UpdateFrameName(
      grpc::ServerContext* context,
      const intrinsic_proto::world::UpdateFrameNameRequest* request,
      intrinsic_proto::world::Frame* response) override;

  grpc::Status ReparentFrame(
      grpc::ServerContext* context,
      const intrinsic_proto::world::ReparentFrameRequest* request,
      intrinsic_proto::world::Frame* response) override;

  grpc::Status UpdateFrameProperties(
      grpc::ServerContext* context,
      const intrinsic_proto::world::UpdateFramePropertiesRequest* request,
      intrinsic_proto::world::Frame* response) override;

  grpc::Status GetTransform(
      grpc::ServerContext* context,
      const intrinsic_proto::world::GetTransformRequest* request,
      intrinsic_proto::world::GetTransformResponse* response) override;

  grpc::Status UpdateTransform(
      grpc::ServerContext* context,
      const intrinsic_proto::world::UpdateTransformRequest* request,
      intrinsic_proto::world::UpdateTransformResponse* response) override;

  grpc::Status UpdateWorldResources(
      grpc::ServerContext* context,
      const intrinsic_proto::world::UpdateWorldResourcesRequest* request,
      intrinsic_proto::world::UpdateWorldResourcesResponse* response) override;

  grpc::Status AreFootprintsCompatible(
      grpc::ServerContext* context,
      const intrinsic_proto::world::AreFootprintsCompatibleRequest* request,
      intrinsic_proto::world::AreFootprintsCompatibleResponse* response)
      override;

  grpc::Status GetCollisionSettings(
      grpc::ServerContext* context,
      const intrinsic_proto::world::GetCollisionSettingsRequest* request,
      intrinsic_proto::world::CollisionSettings* response) override;

  grpc::Status UpdateCollisionSettings(
      grpc::ServerContext* context,
      const intrinsic_proto::world::UpdateCollisionSettingsRequest* request,
      intrinsic_proto::world::World* response) override;

  grpc::Status ExtractResourceInstances(
      grpc::ServerContext* context,
      const intrinsic_proto::world::ExtractResourceInstancesRequest* request,
      intrinsic_proto::world::ExtractResourceInstancesResponse* response)
      override;

  grpc::Status CheckCollisions(
      grpc::ServerContext* context,
      const intrinsic_proto::world::CheckCollisionsRequest* request,
      intrinsic_proto::world::CheckCollisionsResponse* response) override;

  grpc::Status CreateWorldFromResourceInstances(
      grpc::ServerContext* context,
      const intrinsic_proto::world::CreateWorldFromResourceInstancesRequest*
          request,
      intrinsic_proto::world::WorldMetadata* response) override;

  grpc::Status CreateWorldFromResourceSetData(
      grpc::ServerContext* context,
      const intrinsic_proto::world::CreateWorldFromResourceSetDataRequest*
          request,
      intrinsic_proto::world::CreateWorldFromResourceSetDataResponse* response)
      override;

  grpc::Status CompareWorlds(
      grpc::ServerContext* context,
      const intrinsic_proto::world::CompareWorldsRequest* request,
      intrinsic_proto::world::CompareWorldsResponse* response) override;

  grpc::Status SyncObject(
      grpc::ServerContext* context,
      const intrinsic_proto::world::SyncObjectRequest* request,
      intrinsic_proto::world::SyncObjectResponse* response) override;

  ~ObjectWorldService() final;

 private:
  ObjectWorldService(std::future<WorldStorage*> world_storage,
                     absl::AnyInvocable<std::unique_ptr<GeometryLibrary>(
                         grpc::ServerContext*) const>
                         make_geo_lib);
  ObjectWorldService(const ObjectWorldService&) = delete;
  ObjectWorldService& operator=(const ObjectWorldService&) = delete;

  // World storage that might be shared with other world services.
  absl::Mutex world_storage_mutex_;
  WorldStorage* world_storage_ ABSL_GUARDED_BY(world_storage_mutex_) = nullptr;
  std::future<WorldStorage*> world_storage_future_;
  WorldStorage& WorldStore() ABSL_LOCKS_EXCLUDED(world_storage_mutex_);

  // Helper method to sync a WorldObject between two worlds.
  absl::Status SyncObjectImpl(
      const ObjectWorld& from_world, ObjectWorld& to_world,
      const intrinsic_proto::world::ObjectReference& object_reference);

  // Helper method to sync a WorldFrame between two worlds.
  absl::Status SyncFrameImpl(
      const ObjectWorld& from_world, ObjectWorld& to_world,
      const intrinsic_proto::world::FrameReference& frame);

  // The geometry storage to use when reading or writing protos.
  std::unique_ptr<GeometryLibrary> GeoLib(grpc::ServerContext* context) const;

  absl::AnyInvocable<std::unique_ptr<GeometryLibrary>(grpc::ServerContext*)
                         const>
      make_geo_lib_;

  PubSub pubsub_;

  absl::Status CreatePublisherForWorld(
      absl::string_view world_id, const std::shared_ptr<WorldAndMutex>& world)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(world->mtx);
  void DeletePublisherForWorld(absl::string_view world_id,
                               const std::shared_ptr<WorldAndMutex>& world)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(world->mtx);
  void OnWorldChanged(absl::string_view world_id,
                      const std::shared_ptr<WorldAndMutex>& world,
                      bool has_state_change)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(world->mtx);

  absl::Status PublishWorldStateOnce(
      absl::string_view world_id,
      const std::shared_ptr<WorldAndMutex>& world_ptr)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(world_ptr->mtx);

  bool disable_asset_frame_edits_ = false;
};

}  // namespace object_world
}  // namespace intrinsic

#endif  // INTRINSIC_WORLD_SERVICE_OBJECT_WORLD_SERVICE_H_
