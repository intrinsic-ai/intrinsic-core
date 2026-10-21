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

#ifndef INTRINSIC_SIMULATION_GAZEBO_SPAWNER_SPAWNER_MANAGER_H_
#define INTRINSIC_SIMULATION_GAZEBO_SPAWNER_SPAWNER_MANAGER_H_

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <variant>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/flags/declare.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"
#include "google/protobuf/empty.pb.h"
#include "grpcpp/server.h"
#include "gz/sim/EntityComponentManager.hh"
#include "gz/sim/SdfEntityCreator.hh"
#include "gz/sim/System.hh"
#include "gz/sim/config.hh"
#include "intrinsic/geometry/storage/geometry_deserializer.h"
#include "intrinsic/geometry/storage/geometry_library.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/scene/proto/v1/scene_object.pb.h"
#include "intrinsic/simulation/gazebo/components/spawner_component.h"
#include "intrinsic/simulation/gazebo/proto/spawner_service.grpc.pb.h"
#include "intrinsic/simulation/gazebo/proto/spawner_service.pb.h"
#include "intrinsic/util/thread/concurrent_queue.h"
#include "intrinsic/world/objects/object_world_ids.h"
#include "intrinsic/world/proto/object_world_refs.pb.h"
#include "intrinsic/world/proto/object_world_service.grpc.pb.h"
#include "sdf/Model.hh"

ABSL_DECLARE_FLAG(std::string, spawner_service_address);

namespace intrinsic {
namespace simulation {

class GZ_SIM_VISIBLE SpawnerManager
    : public gz::sim::System,
      public gz::sim::ISystemPreUpdate,
      public gz::sim::ISystemUpdate,
      public gz::sim::ISystemConfigure,
      public xfa::simulation::SpawnerService::Service {
 public:
  using ObjectWorldServiceStub =
      intrinsic_proto::world::ObjectWorldService::StubInterface;

  static absl::StatusOr<std::unique_ptr<SpawnerManager>> Create(
      std::string_view world_service_address,
      std::string_view geometry_service_address,
      std::string_view simulator_world_id);
  static absl::StatusOr<std::unique_ptr<SpawnerManager>> Create(
      std::shared_ptr<ObjectWorldServiceStub> object_world_service_stub,
      std::unique_ptr<GeometryLibrary> geometry_library,
      std::string_view simulator_world_id);
  ~SpawnerManager() override;

  void Configure(const gz::sim::Entity& _entity,
                 const std::shared_ptr<const sdf::Element>& _sdf,
                 gz::sim::EntityComponentManager& ecm,
                 gz::sim::EventManager& eventMgr) final;

  void PreUpdate(const gz::sim::UpdateInfo& _info,
                 gz::sim::EntityComponentManager& ecm) final;

  void Update(const gz::sim::UpdateInfo& _info,
              gz::sim::EntityComponentManager& ecm) override;

  grpc::Status SpawnObject(grpc::ServerContext* context,
                           const xfa::simulation::SpawnObjectRequest* request,
                           google::protobuf::Empty* response) override;

 private:
  SpawnerManager(
      std::shared_ptr<ObjectWorldServiceStub> object_world_service_stub,
      std::unique_ptr<GeometryLibrary> geometry_library,
      absl::string_view simulator_world_id);

  std::unique_ptr<grpc::Server> spawner_service_ = nullptr;

  absl::StatusOr<sdf::Model> SpawnSceneObject(
      intrinsic::Pose3d spawned_object_pose, absl::string_view name,
      const intrinsic_proto::scene_object::v1::SceneObject& scene_object,
      const std::optional<intrinsic_proto::world::TransformNodeReference>&
          parent_node) const;
  // Spawns the gazebo entities corresponding to the model and parent
  // information.
  absl::Status SpawnGzEntities(sdf::Model model,
                               std::optional<SpawnCmdParentData> parent_data,
                               gz::sim::EntityComponentManager& ecm);

  // The to_be_spawned_ map is from resource names to spawner_t_object poses,
  // where the resource name refers to the spawner.
  struct SpawnRequest {
    // If spawning a scene object then this is a world-space pose. If spawning
    // from a spawner resource, then this pose is relative to the spawner.
    intrinsic::Pose3d spawn_pose;

    // If spawning a scene object then this is the parent data to attach the
    // spawned world object and entities to. If spawning from a spawner
    // resource, then this is ignored.
    std::optional<SpawnCmdParentData> parent;

    // The source of the spawned object.
    intrinsic_proto::scene_object::v1::SceneObject source;

    // The name of the spawned object.
    std::string name;

    // The callback to use with the status of spawning this object. If the
    // status is absl::OkStatus(), then the object is successfully spawned.
    std::function<void(const absl::Status&)> callback;
  };
  intrinsic::ConcurrentQueue<SpawnRequest> to_be_spawned_;

  std::unique_ptr<gz::sim::SdfEntityCreator> entity_creator_ = nullptr;

  std::shared_ptr<ObjectWorldServiceStub> object_world_service_stub_;

  std::unique_ptr<GeometryLibrary> geometry_library_;

  const std::string simulator_world_id_;
};

}  // namespace simulation
}  // namespace intrinsic

#endif  // INTRINSIC_SIMULATION_GAZEBO_SPAWNER_SPAWNER_MANAGER_H_
