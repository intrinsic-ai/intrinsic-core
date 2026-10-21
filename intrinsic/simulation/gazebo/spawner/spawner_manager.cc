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

#include "intrinsic/simulation/gazebo/spawner/spawner_manager.h"

#include <cstdint>
#include <cstdlib>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "grpcpp/client_context.h"
#include "grpcpp/security/server_credentials.h"
#include "grpcpp/server_builder.h"
#include "grpcpp/support/status.h"
#include "gz/sim/Entity.hh"
#include "gz/sim/EntityComponentManager.hh"
#include "gz/sim/EventManager.hh"
#include "gz/sim/Model.hh"
#include "gz/sim/SdfEntityCreator.hh"
#include "gz/sim/Types.hh"
#include "gz/sim/Util.hh"
#include "gz/sim/components/DetachableJoint.hh"
#include "gz/sim/components/Link.hh"
#include "gz/sim/components/Model.hh"
#include "gz/sim/components/Name.hh"
#include "gz/sim/components/ParentEntity.hh"
#include "gz/sim/components/World.hh"
#include "intrinsic/connect/cc/grpc/channel.h"
#include "intrinsic/geometry/storage/dummy_storage.h"
#include "intrinsic/geometry/storage/geometry_service_storage.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/proto_conversion.h"
#include "intrinsic/scene/proto/v1/scene_object.pb.h"
#include "intrinsic/simulation/gazebo/components/spawner_component.h"
#include "intrinsic/simulation/gazebo/components/world_component.h"
#include "intrinsic/simulation/gazebo/plugins/world_model_config_plugin_constants.h"
#include "intrinsic/simulation/gazebo/proto/spawner_service.pb.h"
#include "intrinsic/simulation/gazebo/world_util.h"
#include "intrinsic/simulation/world/world_to_sdf.h"
#include "intrinsic/util/status/return.h"
#include "intrinsic/util/status/status_builder_grpc.h"
#include "intrinsic/util/status/status_conversion_grpc.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/status/status_macros_grpc.h"
#include "intrinsic/world/entity_id.h"
#include "intrinsic/world/objects/frame.h"
#include "intrinsic/world/objects/object_world.h"
#include "intrinsic/world/objects/object_world_client.h"
#include "intrinsic/world/objects/object_world_creation_utils.h"
#include "intrinsic/world/objects/object_world_ids.h"
#include "intrinsic/world/objects/transform_node.h"
#include "intrinsic/world/objects/world_object.h"
#include "intrinsic/world/objects/world_object_internal.h"
#include "intrinsic/world/proto/object_world_service.pb.h"
#include "intrinsic/world/world.h"
#include "sdf/Element.hh"
#include "sdf/Model.hh"
#include "sdf/Param.hh"
#include "sdf/Plugin.hh"
#include "sdf/Root.hh"
#include "sdf/Types.hh"
#include "sdf/World.hh"

ABSL_FLAG(std::string, spawner_service_address, "0.0.0.0:13596",
          "Spawner service address. This service is only advertised if the "
          "simulator gets its SDF from the world service and the world has "
          "spawner entities.");

namespace intrinsic {
namespace simulation {

namespace {

bool ModelMatchesWorldObject(const sdf::Model* m,
                             const world::WorldObject& object) {
  for (const sdf::Plugin& p : m->Plugins()) {
    if (p.Name() == "intrinsic::simulation::WorldModelConfigPlugin") {
      // The config plugin should have the object name in the model data tag.
      sdf::ElementPtr e = p.Element();
      for (sdf::ElementPtr child = e->GetFirstElement(); child != nullptr;
           child = child->GetNextElement()) {
        const std::string object_name = object.Name().value();
        std::string world_object_name_tag =
            std::string(kWorldModelConfigPlugin_WorldObjectNameTag);
        if (child->GetName() == kWorldModelConfigPlugin_ModelDataTag &&
            child->Get<std::string>(world_object_name_tag) == object_name) {
          return true;
        }
      }
    }
  }

  return false;
}

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

// Returns a pair of object name and link name for the given transform node.
absl::StatusOr<std::pair<std::string, std::string>> GetObjectNameAndLinkName(
    const intrinsic::world::TransformNode& parent_node,
    const intrinsic::world::ObjectWorldClient& sim_world_client) {
  if (parent_node.Id() == intrinsic::RootObjectId()) {
    return std::make_pair(RootObjectName().value(), "");
  }

  if (auto object = world::WorldObject::FromTransformNode(parent_node);
      // For objects, return object name and root link name.
      object.has_value()) {
    const google::protobuf::Map<::std::string,
                                ::intrinsic_proto::world::Entity>& entities =
        object->Proto().entities();
    absl::flat_hash_set<std::string> entity_ids;
    for (const auto& [_, entity] : entities) {
      entity_ids.insert(entity.id());
    }
    auto root_link_entity = absl::c_find_if(entities, [&](const auto& e) {
      return !entity_ids.contains(e.second.parent_id());
    });
    if (root_link_entity == entities.end()) {
      return absl::InternalError(
          absl::Substitute("Object '$0' does not have a root link entity.",
                           object->Name().value()));
    }
    const std::string& root_link_name = root_link_entity->second.name();

    return std::make_pair(object->Name().value(), root_link_name);
  } else if (auto frame = world::Frame::FromTransformNode(parent_node);
             frame.has_value()) {
    // For frames, return object name and the closest parent link name.
    INTR_ASSIGN_OR_RETURN(auto object,
                          sim_world_client.GetObject(frame->ObjectId()));
    const google::protobuf::Map<::std::string,
                                ::intrinsic_proto::world::Entity>& entities =
        object.Proto().entities();
    auto frame_entity_it = absl::c_find_if(entities, [&](const auto& e) {
      return e.second.name() == frame->Name().value();
    });
    if (frame_entity_it == entities.end()) {
      return absl::InternalError(
          absl::Substitute("Frame '$0' not found among entities of object '$1'",
                           object.Name().value(), frame->Name().value()));
    }
    absl::flat_hash_set<std::string> child_frame_names;
    for (const auto& child_frame_name : object.ChildFrameNames()) {
      child_frame_names.insert(child_frame_name.value());
    }
    // Search the closest parent link by iterating through parent entities.
    // Assumes that entity names are interchangeable with frame/link names.
    const auto* current_entity = &frame_entity_it->second;
    while (child_frame_names.contains(current_entity->name())) {
      if (!entities.contains(current_entity->parent_id())) {
        return absl::InternalError(absl::Substitute(
            "Frame '$0' does not have a parent entity with id '$1' within "
            "object '$2' while searching for the closest parent link of frame "
            "'$3'.",
            current_entity->name(), current_entity->parent_id(),
            object.Name().value(), frame->Name().value()));
      }

      current_entity = &entities.at(current_entity->parent_id());
    }
    const std::string& link_name = current_entity->name();
    return std::make_pair(object.Name().value(), link_name);
  }
  return absl::InvalidArgumentError(absl::Substitute(
      "Node $0 is neither a world object nor a frame.", parent_node.Id()));
}

}  // namespace

absl::StatusOr<std::unique_ptr<SpawnerManager>> SpawnerManager::Create(
    std::string_view world_service_address,
    std::string_view geometry_service_address,
    std::string_view simulator_world_id) {
  INTR_ASSIGN_OR_RETURN(
      std::shared_ptr<ObjectWorldServiceStub> object_world_service_stub,
      details::GetObjectWorldServiceStub(world_service_address));

  std::unique_ptr<GeometryLibrary> geometry_library;
  if (!geometry_service_address.empty()) {
    geometry_library = GetGeometryServiceGeometryLibrary(
        geometry_service_address, absl::Seconds(300));
  } else {
    LOG(WARNING) << "Geometry service address is empty. This may prevent object"
                 << "spawning.";
  }

  return Create(std::move(object_world_service_stub),
                std::move(geometry_library), simulator_world_id);
}

absl::StatusOr<std::unique_ptr<SpawnerManager>> SpawnerManager::Create(
    std::shared_ptr<ObjectWorldServiceStub> object_world_service_stub,
    std::unique_ptr<GeometryLibrary> geometry_library,
    std::string_view simulator_world_id) {
  return absl::WrapUnique(
      new SpawnerManager(std::move(object_world_service_stub),
                         std::move(geometry_library), simulator_world_id));
}

SpawnerManager::SpawnerManager(
    std::shared_ptr<ObjectWorldServiceStub> object_world_service_stub,
    std::unique_ptr<GeometryLibrary> geometry_library,
    absl::string_view simulator_world_id)
    : to_be_spawned_(1024),
      object_world_service_stub_(std::move(object_world_service_stub)),
      geometry_library_(std::move(geometry_library)),
      simulator_world_id_(simulator_world_id) {}

SpawnerManager::~SpawnerManager() {
  if (spawner_service_ != nullptr) {
    spawner_service_->Shutdown();
  }
}

void SpawnerManager::Configure(const gz::sim::Entity& _entity,
                               const std::shared_ptr<const sdf::Element>& _sdf,
                               gz::sim::EntityComponentManager& ecm,
                               gz::sim::EventManager& eventMgr) {
  entity_creator_ = std::make_unique<gz::sim::SdfEntityCreator>(ecm, eventMgr);

  // Start the spawner service
  grpc::ServerBuilder builder;

  // TODO: b/285210704 - Come up with a better way of specifying this.
  std::string spawner_service_address;
  const char* spawner_service_address_env = getenv("SPAWNER_SERVICE_ADDRESS");
  if (spawner_service_address_env) {
    spawner_service_address = spawner_service_address_env;
  } else {
    spawner_service_address = absl::GetFlag(FLAGS_spawner_service_address);
  }

  if (!spawner_service_address.empty()) {
    builder.AddListeningPort(
        spawner_service_address,
        grpc::InsecureServerCredentials());  // NOLINT (insecure)
    builder.RegisterService(this);
    spawner_service_ = builder.BuildAndStart();
  }

  LOG_IF(ERROR, spawner_service_ == nullptr)
      << "Unable to start spawner service!";
  LOG_IF(INFO, spawner_service_ != nullptr)
      << "Starting spawner service on: " << spawner_service_address;
}

grpc::Status SpawnerManager::SpawnObject(
    grpc::ServerContext* context,
    const xfa::simulation::SpawnObjectRequest* request,
    google::protobuf::Empty* response) {
  if (object_world_service_stub_ == nullptr) {
    return FailedPreconditionErrorBuilderGrpc()
           << "Not connected to object world service";
  }

  const absl::Duration kSpawnTimeout = absl::Minutes(1);
  const absl::Time start_time = absl::Now();

  // We use a shared_ptr here because both the callback could outlive this RPC,
  // and the RPC could outlive the callback, making it unclear where ownership
  // should lie. We could use the `SpawnerManager` class to store this via a
  // UUID, but that makes concurrency somewhat difficult to reason about, and we
  // can simply solve the problem with a shared pointer.
  struct SpawnNotification {
    absl::Status spawn_status;
    absl::Notification spawn_notification;
  };
  auto notification = std::make_shared<SpawnNotification>();
  notification->spawn_status = absl::OkStatus();

  INTR_ASSIGN_OR_RETURN_GRPC(
      intrinsic::Pose3d parent_t_object,
      intrinsic_proto::FromProtoNormalized(request->parent_t_object()));

  std::optional<SpawnCmdParentData> parent_data;
  if (request->has_parent()) {
    world::ObjectWorldClient sim_world_client(simulator_world_id_,
                                              object_world_service_stub_);
    INTR_ASSIGN_OR_RETURN_GRPC(
        auto parent_node, sim_world_client.GetTransformNode(request->parent()));
    INTR_ASSIGN_OR_RETURN_GRPC(
        (auto [parent_object_name, parent_link_name]),
        GetObjectNameAndLinkName(parent_node, sim_world_client));
    parent_data = SpawnCmdParentData{
        .parent_node = request->parent(),
        .parent_object_name = parent_object_name,
        .parent_link_name = parent_link_name,
    };
  }
  INTR_RETURN_IF_ERROR_GRPC(ToGrpcStatus(to_be_spawned_.Enqueue(
      SpawnRequest{
          .spawn_pose = parent_t_object,
          .parent = parent_data,
          .source = request->scene_object(),
          .name = request->name(),
          .callback =
              // Explicitly capture notification by value to preserve ownership
          [notification](const absl::Status& s) {
            notification->spawn_status = s;
            notification->spawn_notification.Notify();
          },
      },
      kSpawnTimeout)));

  // Wait for this object to actually spawn.
  if (!notification->spawn_notification.WaitForNotificationWithDeadline(
          start_time + kSpawnTimeout)) {
    return DeadlineExceededErrorBuilderGrpc()
           << "Could not spawn a new object within " << kSpawnTimeout
           << ". Is the simulator running very slow?";
  }

  absl::Status spawn_result = notification->spawn_status;
  LOG(INFO) << "Serviced spawn request with status: " << spawn_result;
  return ToGrpcStatus(spawn_result);
}

absl::StatusOr<sdf::Model> SpawnerManager::SpawnSceneObject(
    intrinsic::Pose3d spawned_object_pose, absl::string_view name,
    const intrinsic_proto::scene_object::v1::SceneObject& scene_object,
    const std::optional<intrinsic_proto::world::TransformNodeReference>&
        parent_node) const {
  if (object_world_service_stub_ == nullptr) {
    return absl::FailedPreconditionError(
        "Not connected to object world service");
  }

  world::ObjectWorldClient sim_world_client(simulator_world_id_,
                                            object_world_service_stub_);

  // Create the object via the object world service
  INTR_ASSIGN_OR_RETURN(
      auto parent_transform_node,
      parent_node.has_value()
          ? sim_world_client.GetTransformNode(parent_node.value())
          : sim_world_client.GetRootObject());
  world::ObjectWorldClient::CreateObjectOptions create_object_options{
      .parent = parent_transform_node,
      .parent_t_created_object = spawned_object_pose,
  };
  INTR_RETURN_IF_ERROR(sim_world_client.CreateObjectFromSceneObject(
      scene_object, intrinsic::WorldObjectName(name), create_object_options));

  INTR_ASSIGN_OR_RETURN(
      auto generated_object,
      sim_world_client.GetObject(intrinsic::WorldObjectName(name)));

  std::string node_name = [&]() {
    if (auto frame = world::Frame::FromTransformNode(parent_transform_node);
        frame.has_value()) {
      return frame->Name().value();
    } else if (auto object =
                   world::WorldObject::FromTransformNode(parent_transform_node);
               object.has_value()) {
      return object->Name().value();
    }
    return std::string();
  }();

  LOG(INFO) << "Spawned object '" << generated_object.Name().value()
            << "' at pose: " << spawned_object_pose
            << " relative to parent transform node: " << node_name;

  World generated_object_world = World::CreateEmptyWorld();
  {
    INTR_ASSIGN_OR_RETURN(
        std::unique_ptr<object_world::ObjectWorld>
            generated_object_object_world,
        object_world::ObjectWorld::CreateView(generated_object_world));

    INTR_ASSIGN_OR_RETURN(
        auto root_object,
        generated_object_object_world->GetObject(RootObjectName()));
    // We will spawn the new gazebo Model as child of world so generate the
    // model relative to root.
    INTR_ASSIGN_OR_RETURN(auto root_t_object,
                          sim_world_client.GetTransform(generated_object));
    INTR_ASSIGN_OR_RETURN(
        auto new_object,
        root_object->CreateChildObject(intrinsic::WorldObjectName(name),
                                       WorldObjectNameType::kNameIsGlobalAlias,
                                       root_t_object, geometry_library_.get(),
                                       scene_object));

    // Make sure to deserialize the geometries for this object.
    for (EntityId entity_id : new_object->GetEntityIds()) {
      absl::StatusOr<GeometryComponent*> geo_component =
          generated_object_world.GetComponentByEntityId<GeometryComponent>(
              entity_id);
      if (!geo_component.ok()) continue;

      for (const std::string& name : (*geo_component)->GetGeometryNames()) {
        INTR_RETURN_IF_ERROR(
            (*geo_component)
                ->GetGeometry(name,
                              geometry_library_
                                  ? geometry_library_->Deserializer()
                                  : GetDummyGeometryLibrary()->Deserializer())
                .status());
      }
    }
  }

  // Generate an SDF with this new object.
  INTR_ASSIGN_OR_RETURN(
      std::unique_ptr<WorldSdfAdapter> adapter,
      WorldSdfAdapter::Create(
          generated_object_world,
          WorldSdfAdapter::Options{
              .mesh_savepath = absl::GetFlag(FLAGS_mesh_savepath),
              // Only enable the object interaction moderator if the object is
              // spawned not attached to any other object.
              .enable_object_interaction_moderator =
                  parent_transform_node.Id() == RootObjectId(),
          }));
  sdf::Root sdf_root;
  sdf::Errors errors = sdf_root.LoadSdfString(adapter->GetSDF());
  if (!errors.empty()) {
    return absl::InternalError(errors[0].Message());
  }

  std::function<const sdf::Model*(const sdf::Model*)> search_for_model =
      [&](const sdf::Model* m) -> const sdf::Model* {
    if (ModelMatchesWorldObject(m, generated_object)) {
      return m;
    }

    // Search nested models.
    for (uint64_t i = 0; i < m->ModelCount(); ++i) {
      const sdf::Model* nested_match = search_for_model(m->ModelByIndex(i));
      if (nested_match != nullptr) {
        return nested_match;
      }
    }

    // Not found.
    return nullptr;
  };

  // Modifies the model WorldModelConfigPlugin to match the object plugin in the
  // sim world, assuming entity ids are not stable but the link names and object
  // names are identical.
  std::function<absl::StatusOr<sdf::Model>(const sdf::Model*)> modify_model =
      [&](const sdf::Model* m) -> absl::StatusOr<sdf::Model> {
    sdf::Model ret = *m;
    const std::string world_id_tag(kWorldModelConfigPlugin_WorldIdTag);
    const std::string model_data_tag(kWorldModelConfigPlugin_ModelDataTag);
    const std::string link_data_tag(kWorldModelConfigPlugin_LinkDataTag);
    const std::string world_object_resource_id_tag(
        kWorldModelConfigPlugin_WorldObjectResourceIdTag);
    const std::string world_object_name_tag(
        kWorldModelConfigPlugin_WorldObjectNameTag);
    // Modify the links ids to match the sim world link ids.
    sdf::ElementPtr plugin_element = nullptr;
    for (sdf::Plugin& p : ret.Plugins()) {
      if (p.Name() != "intrinsic::simulation::WorldModelConfigPlugin") {
        continue;
      }
      bool same_object_name = false;
      sdf::ElementPtr e = p.Element();
      for (sdf::ElementPtr child = e->GetFirstElement(); child != nullptr;
           child = child->GetNextElement()) {
        // Find the plugin with the same object name.
        std::string world =
            std::string(kWorldModelConfigPlugin_WorldObjectResourceIdTag);
        if (child->GetName() == kWorldModelConfigPlugin_ModelDataTag &&
            child->Get<std::string>(world_object_name_tag) ==
                generated_object.Name().value()) {
          same_object_name = true;
          break;
        }
      }
      if (!same_object_name) {
        return absl::InternalError(
            "Could not find plugin with same object name in generated model.");
      }
      plugin_element = e;
      break;
    }

    absl::flat_hash_map<std::string, EntityId> entity_name_to_entity_id;
    for (const auto& [entity_id, entity] :
         generated_object.Proto().entities()) {
      INTR_ASSIGN_OR_RETURN(EntityId real_entity_id,
                            object_world::ObjectWorldResourceIdToEntityId(
                                ObjectWorldResourceId(entity_id)));
      entity_name_to_entity_id[entity.name()] = real_entity_id;
    }
    for (sdf::ElementPtr child = plugin_element->GetFirstElement();
         child != nullptr; child = child->GetNextElement()) {
      if (child->GetName() == model_data_tag) {
        if (sdf::ParamPtr world_id_param = child->GetAttribute(world_id_tag);
            world_id_param != nullptr) {
          INTR_ASSIGN_OR_RETURN(EntityId collection_entity_id,
                                object_world::ObjectWorldResourceIdToEntityId(
                                    generated_object.Id()));
          world_id_param->Set(collection_entity_id);
        }
        if (sdf::ParamPtr world_object_resource_id_param =
                child->GetAttribute(world_object_resource_id_tag);
            world_object_resource_id_param != nullptr) {
          world_object_resource_id_param->Set(generated_object.Id().value());
        }
      }
      if (child->GetName() == link_data_tag) {
        if (sdf::ParamPtr link_name_param = child->GetAttribute("name");
            link_name_param != nullptr) {
          std::string link_name = link_name_param->GetAsString();
          if (sdf::ParamPtr link_id_param = child->GetAttribute(world_id_tag);
              link_id_param != nullptr) {
            if (!entity_name_to_entity_id.contains(link_name)) {
              return absl::InternalError(
                  absl::Substitute("Link name '$0' not found in generated "
                                   "model.",
                                   link_name));
            }
            link_id_param->Set(entity_name_to_entity_id.at(link_name));
          }
        } else {
          return absl::InternalError(absl::Substitute(
              "Link name not found in element <$0>.", link_data_tag));
        }
      }
    }

    return ret;
  };

  uint64_t num_worlds = sdf_root.WorldCount();
  if (num_worlds == 0) {
    const sdf::Model* model = sdf_root.Model();
    if (model == nullptr) {
      return absl::InternalError("Generated SDF has no world or model.");
    }
    model = search_for_model(model);
    if (model == nullptr) {
      return absl::InternalError(
          "Could not find new product in generated model SDF");
    }
    return modify_model(model);
  }

  CHECK_EQ(num_worlds, 1)
      << "Generated more than one world when spawning object!";

  sdf::World* sdf_world = sdf_root.WorldByIndex(0);
  for (uint64_t i = 0; i < sdf_world->ModelCount(); ++i) {
    const sdf::Model* m = search_for_model(sdf_world->ModelByIndex(i));
    if (m != nullptr) {
      return modify_model(m);
    }
  }

  return absl::InternalError(
      "Could not find new product model in generated SDF.");
}

absl::Status SpawnerManager::SpawnGzEntities(
    sdf::Model model_sdf, std::optional<SpawnCmdParentData> parent_data,
    gz::sim::EntityComponentManager& ecm) {
  gz::sim::Entity parent_link_entity = gz::sim::kNullEntity;
  if (parent_data.has_value() &&
      parent_data->parent_object_name != RootObjectName().value()) {
    const auto [_, parent_object_name, parent_link_name] = *parent_data;
    if (parent_link_name.empty()) {
      return absl::InternalError(absl::Substitute(
          "Parent data has a parent object name '$0' but no parent link name. "
          "We don't know which link to attach the spawned object to.",
          parent_object_name));
    }
    auto parent_entity = ecm.EntityByComponents(
        gz::sim::components::Model(),
        intrinsic::simulation::WorldObjectName(parent_object_name));
    if (parent_entity == gz::sim::kNullEntity) {
      return absl::InternalError(absl::Substitute(
          "Failed to find parent entity: $0", parent_object_name));
    }
    parent_link_entity =
        ecm.EntityByComponents(gz::sim::components::Link(),
                               gz::sim::components::ParentEntity(parent_entity),
                               gz::sim::components::Name(parent_link_name));
    if (parent_link_entity == gz::sim::kNullEntity) {
      return absl::InternalError(absl::Substitute(
          "Failed to find parent link entity corresponding to parent "
          "object name: $0 and link name: $1",
          parent_object_name, parent_link_name));
    }
  }

  gz::sim::Entity model_entity = entity_creator_->CreateEntities(&model_sdf);
  if (model_entity == gz::sim::kNullEntity) {
    return absl::InternalError(
        "Failed to create model entity for Spawn SceneObject request.");
  }
  LOG(INFO) << "Created model entity: " << model_entity << " for model "
            << model_sdf.Name();
  gz::sim::Entity world_entity =
      ecm.EntityByComponents(gz::sim::components::World());
  entity_creator_->SetParent(model_entity, world_entity);

  // Create a detachable joint between the parent link and the spawned
  // model's canonical link to simulate rigid attachment.
  if (parent_link_entity != gz::sim::kNullEntity) {
    auto joint_entity = ecm.CreateEntity();
    if (joint_entity == gz::sim::kNullEntity) {
      return absl::InternalError(
          "Failed to create entity for detachable joint");
    }

    gz::sim::Model model_gz(model_entity);
    gz::sim::components::DetachableJointInfo info(
        {.parentLink = parent_link_entity,
         .childLink = model_gz.CanonicalLink(ecm)});

    auto* component = ecm.CreateComponent(
        joint_entity, gz::sim::components::DetachableJoint(info));
    if (component == nullptr) {
      return absl::InternalError("Failed to create component");
    }
  }
  return absl::OkStatus();
}

void SpawnerManager::PreUpdate(const gz::sim::UpdateInfo& _info,
                               gz::sim::EntityComponentManager& ecm) {
  struct SpawnEvent {
    gz::sim::Entity entity;
    std::function<void(const absl::Status&)> spawn_callback =
        [](const absl::Status&) {};
    std::string name;
    intrinsic::Pose3d pose;
    intrinsic_proto::scene_object::v1::SceneObject scene_object;
    std::optional<SpawnCmdParentData> parent_data;
  };

  std::vector<gz::sim::Entity> processed_entities;
  std::vector<SpawnEvent> spawned_entities;
  ecm.Each<SpawnSceneObjectCmd>(
      [&](const gz::sim::Entity& entity, SpawnSceneObjectCmd* cmd) {
        processed_entities.push_back(entity);

        intrinsic::Pose3d spawned_object_pose;
        if (auto spawn_pose = ecm.Component<SpawnCmdPose>(entity);
            spawn_pose != nullptr) {
          spawned_object_pose = spawn_pose->Data();
        }

        std::function<void(const absl::Status&)> callback;
        if (auto spawn_callback = ecm.Component<SpawnCmdCallback>(entity);
            spawn_callback != nullptr) {
          callback = spawn_callback->Data();
        }

        std::optional<SpawnCmdParentData> parent_data;
        if (auto spawn_parent = ecm.Component<SpawnCmdParent>(entity);
            spawn_parent != nullptr) {
          parent_data = spawn_parent->Data();
        }

        const intrinsic_proto::scene_object::v1::SceneObject& scene_object =
            cmd->Data();

        std::string spawned_object_name;
        if (auto spawn_name = ecm.Component<SpawnCmdName>(entity);
            spawn_name != nullptr) {
          spawned_object_name = spawn_name->Data();
        }
        if (spawned_object_name.empty()) {
          spawned_object_name = absl::StrCat(
              scene_object.name(), "_",
              absl::ToInt64Nanoseconds(absl::Now() - absl::UnixEpoch()));
        }
        spawned_entities.push_back(SpawnEvent{
            .entity = entity,
            .spawn_callback = callback,
            .name = spawned_object_name,
            .pose = spawned_object_pose,
            .scene_object = scene_object,
            .parent_data = parent_data,
        });
        return true;
      });

  // Remove all of the possible spawn commands from the entities we encountered.
  for (const gz::sim::Entity& entity : processed_entities) {
    ecm.RemoveComponent<SpawnSceneObjectCmd>(entity);
    ecm.RemoveComponent<SpawnCmdPose>(entity);
    ecm.RemoveComponent<SpawnCmdName>(entity);
    ecm.RemoveComponent<SpawnCmdParent>(entity);
  }

  for (const SpawnEvent& spawn_event : spawned_entities) {
    std::optional<intrinsic_proto::world::TransformNodeReference> parent_node;
    if (spawn_event.parent_data.has_value()) {
      parent_node = spawn_event.parent_data->parent_node;
    }

    absl::StatusOr<sdf::Model> model =
        SpawnSceneObject(spawn_event.pose, spawn_event.name,
                         spawn_event.scene_object, parent_node);
    if (!model.ok()) {
      spawn_event.spawn_callback(model.status());
      continue;
    }
    absl::Status spawn_entities_status =
        SpawnGzEntities(std::move(*model), spawn_event.parent_data, ecm);
    spawn_event.spawn_callback(spawn_entities_status);
  }
}

void SpawnerManager::Update(const gz::sim::UpdateInfo& _info,
                            gz::sim::EntityComponentManager& ecm) {
  // If we have nothing to spawn then return.
  if (to_be_spawned_.IsEmpty()) {
    return;
  }

  INTR_ASSIGN_OR_RETURN(SpawnRequest req,
                        to_be_spawned_.Dequeue(absl::ZeroDuration()),
                        _.LogError().With(ReturnVoid()));

  auto error_adapter = [&req](const absl::Status& s) { req.callback(s); };

  gz::sim::Entity world_entity =
      ecm.EntityByComponents(gz::sim::components::World());
  CHECK_NE(world_entity, gz::sim::kNullEntity);

  if (req.parent.has_value()) {
    INTR_RETURN_IF_ERROR(AddComponentWithValue<SpawnCmdParent>(
                             ecm, world_entity, req.parent.value()))
        .With(error_adapter);
  }

  const intrinsic_proto::scene_object::v1::SceneObject& scene_object =
      req.source;
  INTR_RETURN_IF_ERROR(AddComponentWithValue<SpawnSceneObjectCmd>(
                           ecm, world_entity, scene_object))
      .With(error_adapter);

  INTR_RETURN_IF_ERROR(
      AddComponentWithValue<SpawnCmdPose>(ecm, world_entity, req.spawn_pose))
      .With(error_adapter);

  INTR_RETURN_IF_ERROR(
      AddComponentWithValue<SpawnCmdName>(ecm, world_entity, req.name))
      .With(error_adapter);

  INTR_RETURN_IF_ERROR(
      AddComponentWithValue<SpawnCmdCallback>(ecm, world_entity, req.callback))
      .With(error_adapter);
}

}  // namespace simulation
}  // namespace intrinsic
