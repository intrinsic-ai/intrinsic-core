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

#include "intrinsic/simulation/gazebo/outfeed/outfeed_manager.h"

#include <cstdlib>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/flags/flag.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/substitute.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "grpcpp/security/server_credentials.h"
#include "grpcpp/server_builder.h"
#include "grpcpp/server_context.h"
#include "grpcpp/support/status.h"
#include "gz/math/AxisAlignedBox.hh"
#include "gz/math/Pose3.hh"
#include "gz/math/Vector3.hh"
#include "gz/math/eigen3/Conversions.hh"
#include "gz/sim/Entity.hh"
#include "gz/sim/EventManager.hh"
#include "gz/sim/SdfEntityCreator.hh"
#include "gz/sim/Types.hh"
#include "gz/sim/Util.hh"
#include "gz/sim/components/Pose.hh"
#include "gz/sim/components/World.hh"
#include "intrinsic/math/proto_conversion.h"
#include "intrinsic/simulation/gazebo/components/outfeed_component.h"
#include "intrinsic/simulation/gazebo/components/world_component.h"
#include "intrinsic/simulation/gazebo/proto/outfeed_service.pb.h"
#include "intrinsic/simulation/gazebo/world_util.h"
#include "intrinsic/util/status/return.h"
#include "intrinsic/util/status/status_builder.h"
#include "intrinsic/util/status/status_builder_grpc.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/status/status_macros_grpc.h"
#include "intrinsic/world/objects/object_world_client.h"
#include "intrinsic/world/objects/object_world_ids.h"
#include "intrinsic/world/objects/world_object.h"
#include "sdf/Element.hh"
#include "sdf/Model.hh"
#include "sdf/Plugin.hh"

ABSL_FLAG(std::string, outfeed_service_address, "0.0.0.0:13597",
          "Outfeed service address. This service is only advertised if the "
          "simulator gets its SDF from the world service and the world has "
          "outfeed entities.");

namespace intrinsic {
namespace simulation {

namespace {

constexpr gz::math::Vector3d kDefaultOutfeedBounds =
    gz::math::Vector3d(1, 1, 1);

template <typename ComponentTy, typename ComponentValueTy>
absl::Status AddComponentWithValue(gz::sim::EntityComponentManager& ecm,
                                   gz::sim::Entity entity,
                                   ComponentValueTy val) {
  if (auto comp = ecm.Component<ComponentTy>(entity); comp != nullptr) {
    LOG(WARNING) << "Entity '" << gz::sim::scopedName(entity, ecm, "::", false)
                 << "' already has a " << ComponentTy::typeName
                 << ". Duplicate ticks per update?";
    comp->Data() = std::move(val);
  } else if (ecm.CreateComponent<ComponentTy>(
                 entity, ComponentTy(std::move(val))) == nullptr) {
    return InternalErrorBuilder()
           << "Unable to create " << ComponentTy::typeName
           << " for entity: " << gz::sim::scopedName(entity, ecm, "::", false);
  }
  return absl::OkStatus();
}

}  // namespace

absl::StatusOr<std::unique_ptr<OutfeedManager>> OutfeedManager::Create(
    std::string_view world_service_address,
    std::string_view simulator_world_id) {
  INTR_ASSIGN_OR_RETURN(
      auto object_world_service_stub,
      details::GetObjectWorldServiceStub(world_service_address));
  return Create(std::move(object_world_service_stub), simulator_world_id);
}

absl::StatusOr<std::unique_ptr<OutfeedManager>> OutfeedManager::Create(
    std::shared_ptr<ObjectWorldServiceStub> object_world_service_stub,
    std::string_view simulator_world_id) {
  return absl::WrapUnique(new OutfeedManager(
      std::move(object_world_service_stub), simulator_world_id));
}

OutfeedManager::OutfeedManager(
    std::shared_ptr<ObjectWorldServiceStub> object_world_service_stub,
    std::string_view simulator_world_id)
    : to_be_outfed_(1024),
      object_world_service_stub_(std::move(object_world_service_stub)),
      simulator_world_id_(simulator_world_id) {}

OutfeedManager::~OutfeedManager() {
  if (outfeed_service_ != nullptr) {
    outfeed_service_->Shutdown();
  }
}

void OutfeedManager::Configure(const gz::sim::Entity& _entity,
                               const std::shared_ptr<const sdf::Element>& _sdf,
                               gz::sim::EntityComponentManager& ecm,
                               gz::sim::EventManager& eventMgr) {
  entity_creator_ = std::make_unique<gz::sim::SdfEntityCreator>(ecm, eventMgr);

  // Start the outfeed service
  grpc::ServerBuilder builder;

  // TODO: b/285210704 - Come up with a better way of specifying this.
  std::string outfeed_service_address;
  const char* outfeed_service_address_env = getenv("OUTFEED_SERVICE_ADDRESS");
  if (outfeed_service_address_env) {
    outfeed_service_address = outfeed_service_address_env;
  } else {
    outfeed_service_address = absl::GetFlag(FLAGS_outfeed_service_address);
  }

  if (!outfeed_service_address.empty()) {
    builder.AddListeningPort(
        outfeed_service_address,
        grpc::InsecureServerCredentials());  // NOLINT (insecure)
    builder.RegisterService(this);
    outfeed_service_ = builder.BuildAndStart();
  }

  LOG_IF(ERROR, outfeed_service_ == nullptr)
      << "Unable to start outfeed service!";
  LOG_IF(INFO, outfeed_service_ != nullptr)
      << "Starting outfeed service on: " << outfeed_service_address;
}

grpc::Status OutfeedManager::ProcessOutfeed(
    grpc::ServerContext* context,
    const xfa::simulation::ProcessOutfeedRequest* request,
    xfa::simulation::ProcessOutfeedResponse* response) {
  const absl::Duration kOutfeedTimeout = absl::Seconds(5);
  const absl::Time start_time = absl::Now();

  OutfeedRequest outfeed_request{
      .bounds = kDefaultOutfeedBounds,
      .world_name_prefix = request->prefix(),
      .source = gz::math::eigen3::convert(
          intrinsic_proto::FromProto(request->position())),
  };

  if (request->has_bounds()) {
    outfeed_request.bounds = gz::math::eigen3::convert(
        intrinsic_proto::FromProto(request->bounds()));
  }

  struct OutfeedNotification {
    absl::Status outfeed_status;
    absl::Notification outfeed_notification;
  };
  auto notification = std::make_shared<OutfeedNotification>();

  outfeed_request.callback = [notification](const absl::Status& s) {
    notification->outfeed_status = s;
    notification->outfeed_notification.Notify();
  };

  INTR_RETURN_IF_ERROR_GRPC(ToGrpcStatus(
      to_be_outfed_.Enqueue(std::move(outfeed_request), kOutfeedTimeout)));
  if (!notification->outfeed_notification.WaitForNotificationWithDeadline(
          start_time + kOutfeedTimeout)) {
    return DeadlineExceededErrorBuilderGrpc()
           << "Could not process outfeed within " << kOutfeedTimeout
           << ". Is the simulator running very slow?";
  }

  absl::Status outfeed_result = notification->outfeed_status;
  LOG(INFO) << "Serviced outfeed request with status: " << outfeed_result;
  return ToGrpcStatus(outfeed_result);
}

absl::Status OutfeedManager::RemoveProduct(
    gz::sim::Entity model_entity, gz::sim::EntityComponentManager& ecm) {
  // Delete the object from the sim world
  auto world_id_component = ecm.Component<WorldObjectResourceId>(model_entity);
  if (world_id_component == nullptr) {
    return absl::InternalError(
        absl::Substitute("Could not find world resource id for product '$0'.",
                         gz::sim::scopedName(model_entity, ecm, "::", false)));
  }
  const std::string object_id = world_id_component->Data();

  world::ObjectWorldClient sim_world_client(simulator_world_id_,
                                            object_world_service_stub_);
  INTR_ASSIGN_OR_RETURN(
      world::WorldObject product_object,
      sim_world_client.GetObject(ObjectWorldResourceId(object_id)));
  LOG(INFO) << "Removing product from outfeed: " << product_object.Name();
  INTR_RETURN_IF_ERROR(sim_world_client.DeleteObject(product_object));

  // Delete the object from the simulator.
  entity_creator_->RequestRemoveEntity(model_entity);
  return absl::OkStatus();
}

void OutfeedManager::PreUpdate(const gz::sim::UpdateInfo& _info,
                               gz::sim::EntityComponentManager& ecm) {
  std::vector<gz::sim::Entity> processed_entities;

  // Find all entities that need outfeeding and outfeed objects.
  ecm.Each<OutfeedCmd>([&](const gz::sim::Entity& entity, OutfeedCmd* cmd) {
    processed_entities.push_back(entity);

    const OutfeedCmdData& data = cmd->Data();
    if (data.prefix.empty()) {
      data.callback(InvalidArgumentErrorBuilder()
                    << "Must specify object name prefix for outfeed.");
      return true;
    }

    const gz::math::Vector3d& outfeed_bounds = data.bounds;

    // Find all entities within the bounds that have a product PPR name.
    absl::flat_hash_map<gz::sim::Entity, std::string> entity_object_names;
    ecm.Each<intrinsic::simulation::WorldObjectName>(
        [outfeed_pos = data.world_pos, &outfeed_bounds, &ecm,
         &entity_object_names](
            const gz::sim::Entity& entity,
            const intrinsic::simulation::WorldObjectName* object_name) {
          // Does this entity have a world pose? If so we can update its pose in
          // the Intrinsic world.
          if (ecm.Component<gz::sim::components::Pose>(entity) == nullptr) {
            return true;
          }

          const gz::math::AxisAlignedBox worldBoundsBox(
              outfeed_pos - 0.5 * outfeed_bounds,
              outfeed_pos + 0.5 * outfeed_bounds);

          gz::math::Pose3d pose = gz::sim::worldPose(entity, ecm);
          if (worldBoundsBox.Contains(pose.Pos())) {
            entity_object_names[entity] = object_name->Data();
          }
          return true;
        });

    // For each entity, if it matches the outfeed configuration then we need to
    // remove it.
    for (auto&& [entity, object_name] : entity_object_names) {
      if (absl::StartsWith(object_name, data.prefix)) {
        INTR_RETURN_IF_ERROR(RemoveProduct(entity, ecm))
            .LogError()
            .With([data](const absl::Status& s) {
              data.callback(s);
              return false;
            });
      }
    }

    data.callback(absl::OkStatus());
    return true;
  });

  for (const gz::sim::Entity& entity : processed_entities) {
    ecm.RemoveComponent<OutfeedCmd>(entity);
  }
}

void OutfeedManager::Update(const gz::sim::UpdateInfo& _info,
                            gz::sim::EntityComponentManager& ecm) {
  // If we have nothing to outfeed then return.
  if (to_be_outfed_.IsEmpty()) {
    return;
  }

  INTR_ASSIGN_OR_RETURN(OutfeedRequest req,
                        to_be_outfed_.Dequeue(absl::ZeroDuration()),
                        _.LogError().With(ReturnVoid()));

  gz::sim::Entity world_entity =
      ecm.EntityByComponents(gz::sim::components::World());
  if (world_entity == gz::sim::kNullEntity) {
    req.callback(InternalErrorBuilder() << "Could not find world entity");
    return;
  }

  OutfeedCmdData cmd{.world_pos = req.source,
                     .bounds = req.bounds,
                     .prefix = req.world_name_prefix,
                     .callback = req.callback};
  INTR_RETURN_IF_ERROR(
      AddComponentWithValue<OutfeedCmd>(ecm, world_entity, cmd))
      .With([&req](const absl::Status& s) { req.callback(s); });
}

}  // namespace simulation
}  // namespace intrinsic
