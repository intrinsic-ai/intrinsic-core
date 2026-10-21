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

#ifndef INTRINSIC_SIMULATION_GAZEBO_OUTFEED_OUTFEED_MANAGER_H_
#define INTRINSIC_SIMULATION_GAZEBO_OUTFEED_OUTFEED_MANAGER_H_

#include <memory>
#include <string>
#include <variant>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_set.h"
#include "absl/flags/declare.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "grpcpp/server.h"
#include "grpcpp/support/status.h"
#include "gz/sim/Entity.hh"
#include "gz/sim/EntityComponentManager.hh"
#include "gz/sim/EventManager.hh"
#include "gz/sim/SdfEntityCreator.hh"
#include "gz/sim/System.hh"
#include "gz/sim/Types.hh"
#include "gz/sim/config.hh"
#include "intrinsic/geometry/storage/geometry_deserializer.h"
#include "intrinsic/simulation/gazebo/proto/outfeed_service.grpc.pb.h"
#include "intrinsic/simulation/gazebo/proto/outfeed_service.pb.h"
#include "intrinsic/util/thread/concurrent_queue.h"
#include "intrinsic/world/objects/object_world_ids.h"
#include "intrinsic/world/proto/object_world_service.grpc.pb.h"
#include "intrinsic/world/service/world_service.grpc.pb.h"
#include "sdf/Element.hh"
#include "sdf/Model.hh"

ABSL_DECLARE_FLAG(std::string, outfeed_service_address);

namespace intrinsic {
namespace simulation {

class GZ_SIM_VISIBLE OutfeedManager
    : public gz::sim::System,
      public gz::sim::ISystemPreUpdate,
      public gz::sim::ISystemUpdate,
      public gz::sim::ISystemConfigure,
      public xfa::simulation::OutfeedService::Service {
 public:
  using ObjectWorldServiceStub =
      intrinsic_proto::world::ObjectWorldService::StubInterface;

  static absl::StatusOr<std::unique_ptr<OutfeedManager>> Create(
      std::string_view world_service_address,
      std::string_view simulator_world_id);
  static absl::StatusOr<std::unique_ptr<OutfeedManager>> Create(
      std::shared_ptr<ObjectWorldServiceStub> object_world_service_stub,
      std::string_view simulator_world_id);

  ~OutfeedManager() override;

  void Configure(const gz::sim::Entity& _entity,
                 const std::shared_ptr<const sdf::Element>& _sdf,
                 gz::sim::EntityComponentManager& ecm,
                 gz::sim::EventManager& eventMgr) final;

  void PreUpdate(const gz::sim::UpdateInfo& _info,
                 gz::sim::EntityComponentManager& ecm) final;

  void Update(const gz::sim::UpdateInfo& _info,
              gz::sim::EntityComponentManager& ecm) override;

  grpc::Status ProcessOutfeed(
      grpc::ServerContext* context,
      const xfa::simulation::ProcessOutfeedRequest* request,
      xfa::simulation::ProcessOutfeedResponse* response) override;

 private:
  explicit OutfeedManager(
      std::shared_ptr<ObjectWorldServiceStub> object_world_service_stub,
      std::string_view simulator_world_id);

  std::unique_ptr<grpc::Server> outfeed_service_ = nullptr;

  absl::Status RemoveProduct(gz::sim::Entity model_entity,
                             gz::sim::EntityComponentManager& ecm);

  // An outfeed request is a notification from the calling RPC that we would
  // like to remove some objects from simulataion.
  struct OutfeedRequest {
    // The bounds around the outfeed position to remove an object from. If the
    // source is set to be a resource name, then this is ignored.
    gz::math::Vector3d bounds;

    // The prefix of the Intrinsic world object name for the models that should
    // be removed. If the source is set to be a resource name, then this is
    // derived from the outfeed object.
    std::string world_name_prefix;

    // The source of the outfeed request. Either the name of a resource or the
    // world space position from which we'd like to remove objects.
    gz::math::Vector3d source;

    // The callback to use with the status of spawning this object. If the
    // status is absl::OkStatus(), then the object is successfully spawned.
    std::function<void(const absl::Status&)> callback;
  };
  intrinsic::ConcurrentQueue<OutfeedRequest> to_be_outfed_;

  std::unique_ptr<gz::sim::SdfEntityCreator> entity_creator_ = nullptr;

  std::shared_ptr<ObjectWorldServiceStub> object_world_service_stub_;

  const std::string simulator_world_id_;
};

}  // namespace simulation
}  // namespace intrinsic

#endif  // INTRINSIC_SIMULATION_GAZEBO_OUTFEED_OUTFEED_MANAGER_H_
