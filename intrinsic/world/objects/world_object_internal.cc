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

#include "intrinsic/world/objects/world_object_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/container/flat_hash_set.h"
#include "absl/crc/crc32c.h"
#include "absl/flags/flag.h"
#include "absl/log/log.h"
#include "absl/random/random.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "google/protobuf/wrappers.pb.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/geometry/api/geometry_options.h"
#include "intrinsic/geometry/api/io.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/proto_conversion.h"
#include "intrinsic/resources/proto/geometric_resource_data.pb.h"
#include "intrinsic/resources/proto/resource_instance.pb.h"
#include "intrinsic/scene/config/scene_object_config.h"
#include "intrinsic/scene/constants.h"
#include "intrinsic/scene/conversion/object_properties_conversion.h"
#include "intrinsic/scene/proto/v1/collision_rules.pb.h"
#include "intrinsic/scene/proto/v1/entity.pb.h"
#include "intrinsic/scene/proto/v1/scene_object.pb.h"
#include "intrinsic/scene/proto/v1/scene_object_config.pb.h"
#include "intrinsic/scene/proto/v1/simulation_spec.pb.h"
#include "intrinsic/scene/util/object_user_data.h"
#include "intrinsic/scene/validate/scene_object_validation.h"
#include "intrinsic/simulation/world/sim_plugins.h"
#include "intrinsic/util/status/ret_check.h"
#include "intrinsic/util/status/return.h"
#include "intrinsic/util/status/status_builder.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/collision/collision_context.pb.h"
#include "intrinsic/world/component/attachment_component.h"
#include "intrinsic/world/component/collections_component.h"
#include "intrinsic/world/component/collections_member_component.h"
#include "intrinsic/world/component/collision_component.h"
#include "intrinsic/world/component/geometry_component.h"
#include "intrinsic/world/component/kinematics_component.h"
#include "intrinsic/world/component/physics_component.h"
#include "intrinsic/world/component/ppr_component.h"
#include "intrinsic/world/component/robot_component.h"
#include "intrinsic/world/component/sensor_component.h"
#include "intrinsic/world/component/simulation_component.h"
#include "intrinsic/world/component/user_data_component.h"
#include "intrinsic/world/entity.h"
#include "intrinsic/world/entity_id.h"
#include "intrinsic/world/geometry_types.h"
#include "intrinsic/world/hashing/hashing.h"
#include "intrinsic/world/objects/defaulting_world_object_visitor.h"
#include "intrinsic/world/objects/frame_internal.h"
#include "intrinsic/world/objects/kinematic_object_internal.h"
#include "intrinsic/world/objects/object_entity_filter.h"
#include "intrinsic/world/objects/object_world.h"
#include "intrinsic/world/objects/object_world_creation_utils.h"
#include "intrinsic/world/objects/object_world_data.h"
#include "intrinsic/world/objects/object_world_ids.h"
#include "intrinsic/world/objects/physical_object.h"
#include "intrinsic/world/objects/transform_node_internal.h"
#include "intrinsic/world/objects/user_data_conversion.h"
#include "intrinsic/world/proto/geometry_component.pb.h"
#include "intrinsic/world/proto/kinematics_component.pb.h"
#include "intrinsic/world/proto/robot_component.pb.h"
#include "intrinsic/world/proto/simulation_component.pb.h"
#include "intrinsic/world/proto/world_entity.pb.h"
#include "intrinsic/world/util/walk_attachment_tree.h"
#include "intrinsic/world/world.h"

ABSL_FLAG(bool, always_deserialize_geometries, false,
          "If set to true, will always deserialize all geometries when "
          "creating new world objects.");

namespace intrinsic {
namespace object_world {

using intrinsic_proto::scene_object::v1::EntityPoseUpdate;
using intrinsic_proto::scene_object::v1::SceneObject;
using intrinsic_proto::scene_object::v1::SceneObjectConfig;

namespace {

// Defaults to `kinematic_chain`.
// intrinsic/world/conversion/sdf/world_from_sdf.h;rcl=634544184;l=123
static constexpr char kDefaultKinematicSolverKey[] = "kinematic_chain";

WorldHashSet<CollisionEntityId> FilterToCollisionEntityIds(
    const WorldHashSet<AttachmentEntityId>& attachment_ids,
    const World& world) {
  WorldHashSet<CollisionEntityId> collision_ids;
  for (AttachmentEntityId attachment_id : attachment_ids) {
    if (absl::StatusOr<CollisionEntityId> collision_id =
            world.ValidateEntity<CollisionEntityId>(attachment_id);
        collision_id.ok()) {
      collision_ids.insert(*collision_id);
    }
  }
  return collision_ids;
}

struct SplitSceneObjectConfigResult {
  SceneObjectConfig non_kinematic_config;
  std::vector<std::pair<world::ObjectEntityFilter, Pose3d>> kinematic_updates;
};

bool IsKinematic(const SceneObject& scene_object) {
  using EntityProto = intrinsic_proto::scene_object::v1::Entity;
  return absl::c_any_of(scene_object.entities(), [](const EntityProto& e) {
    return e.entity_type_case() == EntityProto::kJoint &&
           e.joint().has_kinematics_component() &&
           e.joint().kinematics_component().motion_type() !=
               intrinsic_proto::world::KinematicsComponent::MOTION_TYPE_FIXED;
  });
}

absl::StatusOr<SplitSceneObjectConfigResult> SplitSceneObjectConfig(
    const SceneObject& scene_object, const SceneObjectConfig& config) {
  SplitSceneObjectConfigResult result;
  result.non_kinematic_config = config;
  result.non_kinematic_config.clear_entity_pose_updates();
  if (result.non_kinematic_config.has_updates()) {
    result.non_kinematic_config.mutable_updates()->clear_updates();
  }

  const bool is_kinematic_object = IsKinematic(scene_object);

  std::string root_name;
  for (const auto& e : scene_object.entities()) {
    if (e.parent_name().empty()) {
      root_name = e.name();
      break;
    }
  }

  auto handle_pose_update =
      [&](const EntityPoseUpdate& update) -> absl::StatusOr<bool> {
    if (update.entity_name() == root_name) {
      LOG(WARNING) << "Ignoring root entity pose update for "
                   << update.entity_name()
                   << " during child resource creation.";
      return true;
    }

    if (!is_kinematic_object) {
      return false;
    }

    bool has_entity = false;
    for (const auto& entity : scene_object.entities()) {
      has_entity = entity.name() == update.entity_name();
      if (has_entity) {
        if (!entity.has_link() && !entity.has_joint()) {
          return false;
        } else {
          break;
        }
      }
    }
    if (!has_entity) {
      return false;
    }

    INTR_ASSIGN_OR_RETURN(Pose3d pose,
                          FromProtoNormalized(update.parent_t_this()));
    result.kinematic_updates.push_back(
        {world::ObjectEntityFilter::FromEntityNames({update.entity_name()}),
         pose});
    return true;
  };

  for (const auto& update : config.entity_pose_updates()) {
    INTR_ASSIGN_OR_RETURN(bool handled, handle_pose_update(update));
    if (!handled) {
      *result.non_kinematic_config.add_entity_pose_updates() = update;
    }
  }

  for (const auto& update : config.updates().updates()) {
    bool handled = false;
    if (update.has_entity_pose()) {
      INTR_ASSIGN_OR_RETURN(handled, handle_pose_update(update.entity_pose()));
    }

    if (!handled) {
      *result.non_kinematic_config.mutable_updates()->add_updates() = update;
    }
  }

  return result;
}

// Returns true if `filter` is unset or specifies only the base/root entity of
// an object.
bool FilterMatchesOnlyBaseEntity(const world::ObjectEntityFilter& filter) {
  if (filter == world::ObjectEntityFilter()) {
    return true;
  }
  return filter.IncludesBaseEntity() && !filter.IncludesAllEntities() &&
         !filter.IncludesFinalEntity() && filter.EntityIds().empty() &&
         filter.EntityNames().empty();
}

}  // namespace

WorldObject::WorldObject(ObjectWorldResourceId id, WorldObjectName name,
                         WorldHashSet<AttachmentEntityId> entity_ids,
                         ObjectWorldData& data)
    // 'parent' can be set later after all objects in a world have been created.
    : TransformNode(id, /*parent=*/nullptr, data),
      name_(name),
      entity_ids_(std::move(entity_ids)) {
  const World& world = GetEntityWorld();
  if (absl::StatusOr<AttachmentEntityId> root_id =
          world.GetRootEntity({entity_ids_.begin(), entity_ids_.end()});
      root_id.ok()) {
    root_entity_id_ = *root_id;
  }
  for (const AttachmentEntityId entity_id : entity_ids_) {
    if (absl::StatusOr<const WorldEntity*> entity =
            world.GetEntityById(entity_id);
        entity.ok()) {
      const_cast<WorldHashMap<std::string, std::vector<AttachmentEntityId>>&>(
          entity_ids_by_local_name_)[(*entity)->GetLocalName()]
          .push_back(entity_id);
    }
  }
}

void WorldObject::RegisterFrameEntity(AttachmentEntityId frame_id) {
  if (const_cast<WorldHashSet<AttachmentEntityId>&>(entity_ids_)
          .insert(frame_id)
          .second) {
    if (absl::StatusOr<const WorldEntity*> entity =
            GetEntityWorld().GetEntityById(frame_id);
        entity.ok()) {
      auto& mutable_entity_ids_by_local_name = const_cast<
          WorldHashMap<std::string, std::vector<AttachmentEntityId>>&>(
          entity_ids_by_local_name_);
      mutable_entity_ids_by_local_name[(*entity)->GetLocalName()].push_back(
          frame_id);
    }
  }
  GetObjectWorldData().RegisterEntity(frame_id, this);
}

void WorldObject::UnregisterFrameEntity(AttachmentEntityId frame_id) {
  if (const_cast<WorldHashSet<AttachmentEntityId>&>(entity_ids_)
          .erase(frame_id) > 0) {
    if (absl::StatusOr<const WorldEntity*> entity =
            GetEntityWorld().GetEntityById(frame_id);
        entity.ok()) {
      auto& mutable_entity_ids_by_local_name = const_cast<
          WorldHashMap<std::string, std::vector<AttachmentEntityId>>&>(
          entity_ids_by_local_name_);
      auto it =
          mutable_entity_ids_by_local_name.find((*entity)->GetLocalName());
      if (it != mutable_entity_ids_by_local_name.end()) {
        std::erase(it->second, frame_id);
        if (it->second.empty()) {
          mutable_entity_ids_by_local_name.erase(it);
        }
      }
    }
  }
  GetObjectWorldData().UnregisterEntity(frame_id);
}

void WorldObject::UpdateFrameEntityLocalName(AttachmentEntityId frame_id,
                                             absl::string_view old_name,
                                             absl::string_view new_name) {
  if (!entity_ids_.contains(frame_id) || old_name == new_name) {
    return;
  }
  auto& mutable_entity_ids_by_local_name =
      const_cast<WorldHashMap<std::string, std::vector<AttachmentEntityId>>&>(
          entity_ids_by_local_name_);
  if (auto it = mutable_entity_ids_by_local_name.find(old_name);
      it != mutable_entity_ids_by_local_name.end()) {
    std::erase(it->second, frame_id);
    if (it->second.empty()) {
      mutable_entity_ids_by_local_name.erase(it);
    }
  }
  mutable_entity_ids_by_local_name[std::string(new_name)].push_back(frame_id);
}

absl::StatusOr<WorldObjectNameType> WorldObject::GetNameType() const {
  INTR_ASSIGN_OR_RETURN(bool name_is_global_alias, NameIsGlobalAlias());
  return name_is_global_alias ? WorldObjectNameType::kNameIsGlobalAlias
                              : WorldObjectNameType::kNameIsNotGlobalAlias;
}

absl::StatusOr<WorldObjectName> WorldObject::GetAvailableChildObjectName(
    WorldObjectName suggested_name, WorldObjectNameType name_type) const {
  INTR_RETURN_IF_ERROR(
      CheckNameIsCompatibleWithObjectView(suggested_name.value()))
      << "Suggested name " << suggested_name
      << " is not a object world compatible name as a child of " << GetName();
  WorldObjectName target_name = suggested_name;
  auto is_available = [name_type, this](WorldObjectName name) {
    if (!CheckObjectNameAgainstChildrenObjectAndFrameNames(name).ok()) {
      return false;
    }
    if (name_type == WorldObjectNameType::kNameIsGlobalAlias ||
        GetId() == RootObjectId()) {
      if (!GetObjectWorldData()
               .CheckObjectNameAgainstGlobalFrameNames(name)
               .ok()) {
        return false;
      }
      if (!GetObjectWorldData()
               .CheckObjectNameAgainstGlobalObjectNames(name)
               .ok()) {
        return false;
      }
    }
    return true;
  };

  int index = 0;
  while (!is_available(target_name)) {
    ++index;
    target_name = WorldObjectName(
        absl::Substitute("$0_$1", suggested_name.value(), index));
  }
  // Be extra safe here. If the implementation of
  // CheckNameIsCompatibleWithObjectView changes we can catch it here.
  INTR_RET_CHECK_OK(CheckNameIsCompatibleWithObjectView(target_name.value()))
      << "Generated name " << target_name
      << " is not a object world compatible name as a child of " << GetName();
  return target_name;
}

std::vector<WorldObjectName> WorldObject::GetFullPathName() const {
  if (GetParent() == nullptr) return {};
  std::vector<WorldObjectName> result = GetParent()->GetFullPathName();
  result.push_back(name_);
  return result;
}

std::vector<WorldObject*> WorldObject::GetChildren() { return children_; }

std::vector<const WorldObject*> WorldObject::GetChildren() const {
  return {children_.begin(), children_.end()};
}

void WorldObject::AddChildAsymmetric(WorldObject* child) {
  children_.push_back(child);
}

absl::Status WorldObject::RemoveChildAsymmetric(const WorldObject& child) {
  auto it = absl::c_find(children_, &child);
  if (it == children_.end()) {
    return absl::NotFoundError(absl::StrCat(
        "Object \"", child.GetName().value(),
        "\" is not registered as a child of object \"", GetName().value(),
        "\" and thus could not be removed as a child."));
  }
  children_.erase(it);
  return absl::OkStatus();
}

absl::StatusOr<Frame*> WorldObject::GetFrame(
    const ObjectWorldResourceId& frame_id) {
  auto it = frames_by_id_.find(frame_id);
  if (it == frames_by_id_.end()) {
    return absl::NotFoundError(
        absl::StrCat("Frame with id \"", frame_id.value(),
                     "\" does not exist on object with id=\"", GetId().value(),
                     "\" and name=\"", name_.value(), "\"."));
  }
  return it->second.get();
}

absl::StatusOr<const Frame*> WorldObject::GetFrame(
    const ObjectWorldResourceId& frame_id) const {
  // Delegate to non-const implementation.
  INTR_ASSIGN_OR_RETURN(Frame * result,
                        const_cast<WorldObject*>(this)->GetFrame(frame_id));
  return result;
}

absl::StatusOr<Frame*> WorldObject::GetFrame(const FrameName& frame_name) {
  auto it = absl::c_find_if(frames_by_id_, [&frame_name](auto& id_and_frame) {
    return id_and_frame.second->GetName() == frame_name;
  });
  if (it == frames_by_id_.end()) {
    return absl::NotFoundError(
        absl::StrCat("Frame with name \"", frame_name.value(),
                     "\" does not exist on object with id=\"", GetId().value(),
                     "\" and name=\"", name_.value(), "\"."));
  }
  return it->second.get();
}

absl::StatusOr<const Frame*> WorldObject::GetFrame(
    const FrameName& frame_name) const {
  // Delegate to non-const implementation.
  INTR_ASSIGN_OR_RETURN(Frame * result,
                        const_cast<WorldObject*>(this)->GetFrame(frame_name));
  return result;
}

namespace {

template <class T>
std::vector<T> GetFramesImpl(
    const WorldHashMap<ObjectWorldResourceId, std::unique_ptr<Frame>>&
        frames_by_id,
    bool immediate_children_only) {
  std::vector<T> frames;
  frames.reserve(frames_by_id.size());
  for (auto& [_, frame] : frames_by_id) {
    if (!frame->GetParentFrame() || !immediate_children_only) {
      frames.push_back(frame.get());
    }
  }
  return frames;
}

// Returns true if the geometry component has any geometries with only v0 refs.
bool NeedsDeserialization(
    const intrinsic_proto::world::GeometryComponent& geo_component) {
  for (const auto& [name, geo_set] : geo_component.named_geometries()) {
    if (geo_set.named_geometries().empty() && !geo_set.geometries().empty()) {
      return true;
    }
  }

  return false;
}

absl::Status ReserializeGeometryComponent(GeometryComponent* geo_component,
                                          GeometryLibrary& geo_lib) {
  for (const std::string& name : geo_component->GetGeometryNames()) {
    INTR_ASSIGN_OR_RETURN(
        auto geo_set, geo_component->GetGeometry(name, geo_lib.Deserializer()));
    NamedGeometryProtoSet geo_proto_set;
    for (const auto& [sub_name, geo] : geo_set) {
      INTR_ASSIGN_OR_RETURN(geo_proto_set[sub_name],
                            ToProto(geo, &geo_lib.Serializer()));
    }
    geo_component->SetGeometry(name, std::move(geo_proto_set));

    // Make sure it remains deserialized.
    INTR_RETURN_IF_ERROR(
        geo_component->GetGeometry(name, geo_lib.Deserializer()).status());
  }

  return absl::OkStatus();
}

}  // namespace

std::vector<Frame*> WorldObject::GetFrames() {
  return GetFramesImpl<Frame*>(frames_by_id_,
                               /*immediate_children_only=*/false);
}

std::vector<const Frame*> WorldObject::GetFrames() const {
  return GetFramesImpl<const Frame*>(frames_by_id_,
                                     /*immediate_children_only=*/false);
}

std::vector<const Frame*> WorldObject::GetFramesSorted() const {
  std::vector<const Frame*> frames = GetFrames();
  absl::c_sort(frames, [](const Frame* a, const Frame* b) {
    return a->GetName() < b->GetName();
  });
  return frames;
}

namespace {

void CollectFramesPartiallySorted(const Frame* frame,
                                  std::vector<const Frame*>& frames) {
  frames.push_back(frame);
  for (const Frame* child : frame->GetChildFrames()) {
    CollectFramesPartiallySorted(child, frames);
  }
}

}  // namespace

std::vector<const Frame*> WorldObject::GetFramesPartiallySorted() const {
  std::vector<const Frame*> frames;
  frames.reserve(frames_by_id_.size());
  for (const Frame* frame : GetChildFrames()) {
    CollectFramesPartiallySorted(frame, frames);
  }
  return frames;
}

std::vector<Frame*> WorldObject::GetChildFrames() {
  return GetFramesImpl<Frame*>(frames_by_id_,
                               /*immediate_children_only=*/true);
}

std::vector<const Frame*> WorldObject::GetChildFrames() const {
  return GetFramesImpl<const Frame*>(frames_by_id_,
                                     /*immediate_children_only=*/true);
}

std::vector<const Frame*> WorldObject::GetChildFramesSorted() const {
  std::vector<const Frame*> frames = GetChildFrames();
  absl::c_sort(frames, [](const Frame* a, const Frame* b) {
    return a->GetName() < b->GetName();
  });
  return frames;
}

absl::StatusOr<std::optional<std::string>> WorldObject::GetResourceName()
    const {
  std::optional<CollectionsEntityId> collection_id = GetCollectionEntity();
  if (!collection_id.has_value()) {
    return std::nullopt;
  }
  INTR_ASSIGN_OR_RETURN(const WorldEntity* entity,
                        GetEntityWorld().GetEntityById(*collection_id));
  if (!entity->HasComponent<PPRComponent>()) {
    return std::nullopt;
  }
  INTR_ASSIGN_OR_RETURN(std::optional<const PPRComponent*> ppr,
                        entity->GetComponent<PPRComponent>());
  if (!ppr || !(*ppr)->ResourceName()) {
    return std::nullopt;
  }
  return std::string(*(*ppr)->ResourceName());
}

absl::Status WorldObject::SetResourceName(absl::string_view resource_name) {
  INTR_ASSIGN_OR_RETURN(World * world, GetMutableEntityWorld());
  std::optional<CollectionsEntityId> collection_id = GetCollectionEntity();
  if (!collection_id.has_value()) {
    return absl::FailedPreconditionError(
        "Cannot set resource name of root object.");
  }

  std::vector<EntityId> object_entity_ids{*collection_id};
  object_entity_ids.insert(object_entity_ids.end(), entity_ids_.begin(),
                           entity_ids_.end());

  for (const EntityId entity_id : object_entity_ids) {
    INTR_ASSIGN_OR_RETURN(WorldEntity * entity,
                          world->GetEntityById(entity_id));
    INTR_ASSIGN_OR_RETURN(PPRComponent * ppr,
                          entity->GetOrCreateComponent<PPRComponent>());
    if (ppr->ResourceName() && ppr->ResourceName() != resource_name) {
      return absl::FailedPreconditionError(absl::Substitute(
          "Cannot set resource name '$0' for object '$1' because it "
          "is already associated with a another resource '$2'.",
          resource_name, GetName().value(), *ppr->ResourceName()));
    }
    ppr->SetResourceName(resource_name);
  }
  return absl::OkStatus();
}

absl::Status WorldObject::ApplyGeometryOptionOverrides(
    const intrinsic_proto::world::GeometryOptions& options,
    GeometryLibrary& geolib) {
  INTR_ASSIGN_OR_RETURN(auto* entity_world, GetMutableEntityWorld());
  for (const auto& entity_id : entity_ids_) {
    if (!entity_world->ValidateEntity<GeometryComponentType>(entity_id).ok()) {
      continue;
    }

    INTR_ASSIGN_OR_RETURN(
        GeometryComponent * geometry_component,
        entity_world->GetComponentByEntityId<GeometryComponent>(entity_id));
    // TODO: b/469159066 -- We need to deserialize the geometry here in order to
    // set the proto options due to our design choices when defining our protos.
    for (const auto& name : geometry_component->GetGeometryNames()) {
      INTR_RETURN_IF_ERROR(
          geometry_component->GetGeometry(name, geolib.Deserializer())
              .status());
    }

    INTR_RETURN_IF_ERROR(
        geometry_component->ApplyGeometryOptionOverrides(options));

    // TODO: b/469159066 -- Reserialize so we can write them out again.
    INTR_RETURN_IF_ERROR(
        ReserializeGeometryComponent(geometry_component, geolib));
  }

  return absl::OkStatus();
}

absl::Status WorldObject::ApplyUserData(
    const intrinsic_proto::world::UpdateUserData& update) {
  const std::optional<CollectionsEntityId> collection_entity_id =
      GetCollectionEntity();
  if (!collection_entity_id.has_value()) {
    return absl::FailedPreconditionError(absl::Substitute(
        "Object with name \"$0\" has no collection entity and thus cannot "
        "have any user data associated with it.",
        GetName().value()));
  }
  INTR_ASSIGN_OR_RETURN(auto* entity_world, GetMutableEntityWorld());

  INTR_ASSIGN_OR_RETURN(
      auto* user_data_component,
      entity_world->GetOrCreateComponentByEntityId<UserDataComponent>(
          *collection_entity_id));
  INTR_ASSIGN_OR_RETURN(auto user_data_proto, user_data_component->ToProto());
  INTR_ASSIGN_OR_RETURN(const auto policy,
                        ToSceneObjectUserDataPolicy(update.policy()));
  INTR_RETURN_IF_ERROR(intrinsic::scene_object::MergeSceneObjectUserData(
      *user_data_proto.mutable_user_data_protos(), update.user_data(), policy));
  INTR_RETURN_IF_ERROR(user_data_component->UpdateFromProto(user_data_proto));

  return absl::OkStatus();
}

absl::Status WorldObject::SetFrames(
    WorldHashMap<ObjectWorldResourceId, std::unique_ptr<Frame>> frames) {
  frames_by_id_.clear();
  for (auto&& [_, frame] : frames) {
    INTR_RETURN_IF_ERROR(AddFrameAsymmetric(std::move(frame)));
  }
  return absl::OkStatus();
}

absl::StatusOr<SimulationComponent*> WorldObject::GetSimulationComponent() {
  const std::optional<CollectionsEntityId> collection_id =
      GetCollectionEntity();
  if (!collection_id.has_value()) {
    return absl::NotFoundError(
        absl::Substitute("Object with name \"$0\" has no SimulationComponent.",
                         GetName().value()));
  }
  INTR_ASSIGN_OR_RETURN(World * world, GetMutableEntityWorld());
  INTR_ASSIGN_OR_RETURN(WorldEntity * collection_entity,
                        world->GetEntityById(*collection_id));
  if (!collection_entity->HasComponent<SimulationComponent>()) {
    return absl::NotFoundError(
        absl::Substitute("Object with name \"$0\" has no SimulationComponent.",
                         GetName().value()));
  }
  INTR_ASSIGN_OR_RETURN(SimulationComponent * simulation_component,
                        collection_entity->GetComponent<SimulationComponent>());
  return simulation_component;
}

absl::StatusOr<const SimulationComponent*> WorldObject::GetSimulationComponent()
    const {
  // Can't delegate to non-const implementation because `GetEntityWorld()` needs
  // to called here vs `GetMutableEntityWorld()` for non-const method.
  const std::optional<CollectionsEntityId> collection_id =
      GetCollectionEntity();
  if (!collection_id.has_value()) {
    return absl::NotFoundError(
        absl::Substitute("Object with name \"$0\" has no SimulationComponent.",
                         GetName().value()));
  }

  INTR_ASSIGN_OR_RETURN(const WorldEntity* collection_entity,
                        GetEntityWorld().GetEntityById(*collection_id));
  if (!collection_entity->HasComponent<SimulationComponent>()) {
    return absl::NotFoundError(
        absl::Substitute("Object with name \"$0\" has no SimulationComponent.",
                         GetName().value()));
  }
  INTR_ASSIGN_OR_RETURN(const SimulationComponent* simulation_component,
                        collection_entity->GetComponent<SimulationComponent>());
  return simulation_component;
}

absl::Status WorldObject::SetSimulationComponent(
    const intrinsic_proto::world::SimulationComponent& proto) {
  const std::optional<CollectionsEntityId> collection_id =
      GetCollectionEntity();
  if (!collection_id.has_value()) {
    return absl::InvalidArgumentError(absl::Substitute(
        "The object with name \"$0\" cannot have a simulation component.",
        GetName().value()));
  }
  INTR_ASSIGN_OR_RETURN(World * world, GetMutableEntityWorld());
  INTR_ASSIGN_OR_RETURN(WorldEntity * collection_entity,
                        world->GetEntityById(*collection_id));
  INTR_ASSIGN_OR_RETURN(
      SimulationComponent * simulation_component,
      collection_entity->GetOrCreateComponent<SimulationComponent>());

  // TODO(b/401045619) Only the writeable properties are updated in the
  // simulation component.
  simulation_component->SetIsStatic(proto.is_static());
  simulation_component->SetDisabled(proto.disabled());

  return absl::OkStatus();
}

absl::StatusOr<const WorldHashMap<std::string, std::string>*>
WorldObject::GetUserDataMap() const {
  if (!GetCollectionEntity()) {
    return absl::NotFoundError(
        absl::Substitute("Object with name \"$0\" has no collection entity and "
                         "thus cannot have any user data associated with it.",
                         GetName().value()));
  }
  INTR_ASSIGN_OR_RETURN(const WorldEntity* collection_entity,
                        GetEntityWorld().GetEntityById(*GetCollectionEntity()));
  if (!collection_entity->HasComponent<UserDataComponent>()) {
    return absl::NotFoundError(
        absl::Substitute("Object with name \"$0\" has no UserDataComponent.",
                         GetName().value()));
  }
  INTR_ASSIGN_OR_RETURN(const UserDataComponent* user_data_component,
                        collection_entity->GetComponent<UserDataComponent>());
  return &user_data_component->UserDataMap();
}

absl::StatusOr<WorldHashMap<std::string, std::string>*>
WorldObject::GetMutableUserDataMap() {
  if (!GetCollectionEntity()) {
    return absl::NotFoundError(
        absl::Substitute("Object with name \"$0\" has no collection entity and "
                         "thus cannot have any user data associated with it.",
                         GetName().value()));
  }
  INTR_ASSIGN_OR_RETURN(World * world, GetMutableEntityWorld());
  INTR_ASSIGN_OR_RETURN(
      UserDataComponent * user_data_component,
      world->GetOrCreateComponentByEntityId<UserDataComponent>(
          *GetCollectionEntity()));
  return &user_data_component->MutableUserDataMap();
}

absl::StatusOr<const WorldObject::UserDataProtos* absl_nonnull>
WorldObject::GetUserDataProtos() const {
  if (!GetCollectionEntity()) {
    return absl::NotFoundError(
        absl::Substitute("Object with name \"$0\" has no collection entity and "
                         "thus cannot have any user data associated with it.",
                         GetName().value()));
  }
  INTR_ASSIGN_OR_RETURN(const WorldEntity* collection_entity,
                        GetEntityWorld().GetEntityById(*GetCollectionEntity()));
  if (!collection_entity->HasComponent<UserDataComponent>()) {
    return absl::NotFoundError(
        absl::Substitute("Object with name \"$0\" has no UserDataComponent.",
                         GetName().value()));
  }
  INTR_ASSIGN_OR_RETURN(const UserDataComponent* user_data_component,
                        collection_entity->GetComponent<UserDataComponent>());
  return &user_data_component->UserDataProtos();
}

absl::StatusOr<WorldObject::UserDataProtos* absl_nonnull>
WorldObject::GetMutableUserDataProtos() {
  if (!GetCollectionEntity()) {
    return absl::NotFoundError(
        absl::Substitute("Object with name \"$0\" has no collection entity and "
                         "thus cannot have any user data associated with it.",
                         GetName().value()));
  }
  INTR_ASSIGN_OR_RETURN(World * world, GetMutableEntityWorld());
  INTR_ASSIGN_OR_RETURN(
      UserDataComponent * user_data_component,
      world->GetOrCreateComponentByEntityId<UserDataComponent>(
          *GetCollectionEntity()));
  return &user_data_component->MutableUserDataProtos();
}

absl::StatusOr<WorldObject*> WorldObject::CreateChildObject(
    const WorldObjectName& name, WorldObjectNameType name_type,
    const Pose3d& object_t_new_object,
    std::optional<std::string> entity_local_name,
    std::unique_ptr<GeometryComponent> geometry_component) {
  INTR_ASSIGN_OR_RETURN(AttachmentEntityId root_id, GetRootEntityId());
  return CreateSingleEntityObject(name, name_type, root_id, object_t_new_object,
                                  entity_local_name,
                                  std::move(geometry_component));
}

absl::StatusOr<WorldObject*> WorldObject::CreateChildObject(
    const WorldObjectName& name, WorldObjectNameType name_type,
    const Pose3d& object_t_new_object, GeometryLibrary* geolib,
    const intrinsic_proto::world::WorldFragment& fragment) {
  INTR_ASSIGN_OR_RETURN(AttachmentEntityId root_id, GetRootEntityId());
  return CreateObjectFromFragment(name, name_type, root_id, object_t_new_object,
                                  geolib, fragment);
}

absl::StatusOr<WorldObject*> WorldObject::CreateChildObject(
    const WorldObjectName& name, WorldObjectNameType name_type,
    const Pose3d& object_t_new_object, GeometryLibrary* geolib,
    const intrinsic_proto::scene_object::v1::SceneObject& scene_object) {
  INTR_ASSIGN_OR_RETURN(AttachmentEntityId root_id, GetRootEntityId());
  return CreateObjectFromSceneObject(name, name_type, root_id,
                                     object_t_new_object, geolib, scene_object);
}

absl::StatusOr<WorldObject*> WorldObject::CreateChildResource(
    const intrinsic_proto::resources::GeometricResourceInstanceData&
        resource_instance_data,
    const WorldObjectNameType name_type, const Pose3d& object_t_new_object,
    GeometryLibrary* geolib, const scene_object::UpdatePolicy update_policy) {
  INTR_ASSIGN_OR_RETURN(AttachmentEntityId root_id, GetRootEntityId());

  WorldObject* world_object = nullptr;
  std::vector<std::pair<world::ObjectEntityFilter, Pose3d>> kinematic_updates;
  if (resource_instance_data.has_scene_object()) {
    // Apply scene object config as necessary
    intrinsic_proto::scene_object::v1::SceneObject scene_object =
        resource_instance_data.scene_object();
    const WorldObjectName scene_object_name(resource_instance_data.name());
    if (resource_instance_data.has_scene_object_config()) {
      // Split the config.
      INTR_ASSIGN_OR_RETURN(
          SplitSceneObjectConfigResult split_result,
          SplitSceneObjectConfig(scene_object,
                                 resource_instance_data.scene_object_config()));

      kinematic_updates = std::move(split_result.kinematic_updates);

      // Process the scene object according to the non-kinematic updates.
      INTR_ASSIGN_OR_RETURN(
          scene_object::SceneObjectUpdateResult update_result,
          scene_object::ProcessSceneObjectConfig(
              scene_object, split_result.non_kinematic_config, update_policy));

      // TODO(stoyan): It would be good to propagate these up to the caller.
      for (const auto& update_error : update_result.update_errors) {
        LOG(WARNING) << "Error when processing scene object '"
                     << scene_object_name << "': [" << update_error.index << "]"
                     << update_error.status;
      }

      scene_object = update_result.result;
    }

    INTR_ASSIGN_OR_RETURN(
        world_object,
        CreateObjectFromSceneObject(scene_object_name, name_type, root_id,
                                    object_t_new_object, geolib, scene_object),
        _ << "Failed to create object for scene object: " << scene_object_name);

  } else if (resource_instance_data.has_world_fragment()) {
    INTR_ASSIGN_OR_RETURN(world_object,
                          CreateObjectFromFragment(
                              WorldObjectName(resource_instance_data.name()),
                              name_type, root_id, object_t_new_object, geolib,
                              resource_instance_data.world_fragment()));
  } else {
    return absl::InvalidArgumentError(
        "Resource instance data must have either a world "
        "fragment or a scene object.");
  }

  // Add a PPRComponent to every entity we're creating as part of this resource.
  const WorldHashSet<AttachmentEntityId>& object_ids =
      world_object->GetEntityIds();
  std::vector<AttachmentEntityId> ids(object_ids.begin(), object_ids.end());
  WorldHashSet<AttachmentEntityId> visited;

  INTR_ASSIGN_OR_RETURN(World * entity_world,
                        world_object->GetMutableEntityWorld());
  while (!ids.empty()) {
    auto entity_id = ids.back();
    ids.pop_back();
    if (visited.contains(entity_id)) {
      continue;
    }
    visited.insert(entity_id);

    INTR_ASSIGN_OR_RETURN(WorldEntity * entity,
                          entity_world->GetEntityById(entity_id));
    INTR_ASSIGN_OR_RETURN(PPRComponent * ppr,
                          entity->GetOrCreateComponent<PPRComponent>());
    ppr->SetResourceName(resource_instance_data.name());

    for (const auto& child_id : entity_world->GetChildrenOf(entity_id)) {
      ids.push_back(child_id);
    }
  }

  INTR_ASSIGN_OR_RETURN(
      PPRComponent * ppr_component,
      entity_world->GetOrCreateComponentByEntityId<PPRComponent>(
          *world_object->GetCollectionEntity()));
  ppr_component->SetResourceName(resource_instance_data.name());

  if (!kinematic_updates.empty()) {
    class RobotKinematicsUpdater : public WorldObjectVisitor {
     public:
      using RobotKinematicsUpdate =
          std::vector<std::pair<world::ObjectEntityFilter, Pose3d>>;
      explicit RobotKinematicsUpdater(RobotKinematicsUpdate update)
          : update_(std::move(update)) {}
      virtual absl::Status Visit(RootObject& root_object) override {
        return absl::InternalError("New object cannot be root");
      }
      virtual absl::Status Visit(PhysicalObject& physical_object) override {
        return absl::InvalidArgumentError(absl::StrCat(
            "Unable to update kinematics for non-kinematic object '",
            physical_object.GetName(),
            "'. Did this object provide EntityPoseUpdate for links with its "
            "scene object configuration?"));
      }
      virtual absl::Status Visit(KinematicObject& kinematic_object) override {
        return kinematic_object.UpdateRobotKinematics(update_);
      }

     private:
      RobotKinematicsUpdate update_;
    };

    RobotKinematicsUpdater visitor(std::move(kinematic_updates));
    absl::Status update_kinematics_status = world_object->Accept(visitor);

    if (!update_kinematics_status.ok()) {
      if (update_policy == scene_object::UpdatePolicy::kSkipFailed) {
        LOG(WARNING) << update_kinematics_status;
      } else {
        return update_kinematics_status;
      }
    }
  }

  return world_object;
}

absl::StatusOr<WorldObject*> WorldObject::CreateChildObjectOfFinalEntity(
    const WorldObjectName& name, WorldObjectNameType name_type,
    const Pose3d& object_t_new_object,
    std::optional<std::string> entity_local_name,
    std::unique_ptr<GeometryComponent> geometry_component) {
  INTR_ASSIGN_OR_RETURN(AttachmentEntityId root_id, GetRootEntityId());
  INTR_ASSIGN_OR_RETURN(
      AttachmentEntityId parent_id,
      FinalEntityIfKinematicObjectOrElseRootEntity(),
      _ << absl::Substitute("While creating child object '$0', which will be "
                            "attached to the final entity of object '$1'",
                            name.value(), GetName().value()));
  Pose3d parent_entity_t_new_object =
      GetEntityWorld().GetTransform(parent_id, root_id) * object_t_new_object;
  return CreateSingleEntityObject(name, name_type, parent_id,
                                  parent_entity_t_new_object, entity_local_name,
                                  std::move(geometry_component));
}

absl::StatusOr<WorldObject*> WorldObject::CreateChildObjectOfFinalEntity(
    const WorldObjectName& name, WorldObjectNameType name_type,
    const Pose3d& object_t_new_object, GeometryLibrary* geolib,
    const intrinsic_proto::world::WorldFragment& fragment) {
  INTR_ASSIGN_OR_RETURN(AttachmentEntityId root_id, GetRootEntityId());
  INTR_ASSIGN_OR_RETURN(
      AttachmentEntityId parent_id,
      FinalEntityIfKinematicObjectOrElseRootEntity(),
      _ << absl::Substitute("While creating child object '$0', which will be "
                            "attached to the final entity of object '$1'",
                            name.value(), GetName().value()));
  Pose3d parent_entity_t_new_object =
      GetEntityWorld().GetTransform(parent_id, root_id) * object_t_new_object;
  return CreateObjectFromFragment(name, name_type, parent_id,
                                  parent_entity_t_new_object, geolib, fragment);
}

absl::StatusOr<WorldObject*> WorldObject::CreateChildObjectOfFinalEntity(
    const WorldObjectName& name, WorldObjectNameType name_type,
    const Pose3d& object_t_new_object, GeometryLibrary* geolib,
    const intrinsic_proto::scene_object::v1::SceneObject& scene_object) {
  INTR_ASSIGN_OR_RETURN(AttachmentEntityId root_id, GetRootEntityId());
  INTR_ASSIGN_OR_RETURN(
      AttachmentEntityId parent_id,
      FinalEntityIfKinematicObjectOrElseRootEntity(),
      _ << absl::Substitute("While creating child object '$0', which will be "
                            "attached to the final entity of object '$1'",
                            name.value(), GetName().value()));
  Pose3d parent_entity_t_new_object =
      GetEntityWorld().GetTransform(parent_id, root_id) * object_t_new_object;
  return CreateObjectFromSceneObject(name, name_type, parent_id,
                                     parent_entity_t_new_object, geolib,
                                     scene_object);
}

absl::StatusOr<WorldObject*> WorldObject::CloneExistingObjectAndAttach(
    const WorldObject& existing_object, const WorldObjectName& name,
    WorldObjectNameType name_type, bool clone_frames,
    std::optional<Pose3d> object_t_new_object) {
  INTR_ASSIGN_OR_RETURN(AttachmentEntityId root_id, GetRootEntityId());
  return CloneObject(existing_object, name, name_type, root_id, clone_frames,
                     /*parent_entity_t_new_object=*/object_t_new_object);
}

absl::StatusOr<WorldObject*>
WorldObject::CloneExistingObjectAndAttachToFinalEntity(
    const WorldObject& existing_object, const WorldObjectName& name,
    WorldObjectNameType name_type, bool clone_frames,
    std::optional<Pose3d> object_t_new_object) {
  INTR_ASSIGN_OR_RETURN(
      AttachmentEntityId parent_id,
      FinalEntityIfKinematicObjectOrElseRootEntity(),
      _ << absl::Substitute("While creating child object '$0' cloned from "
                            "existing object '$1', which will be "
                            "attached to the final entity of object '$2'",
                            name.value(), existing_object.GetName().value(),
                            GetName().value()));
  std::optional<Pose3d> parent_entity_t_new_object;
  if (object_t_new_object) {
    INTR_ASSIGN_OR_RETURN(AttachmentEntityId root_id, GetRootEntityId());
    parent_entity_t_new_object =
        GetEntityWorld().GetTransform(parent_id, root_id) *
        *object_t_new_object;
  }
  return CloneObject(existing_object, name, name_type, parent_id, clone_frames,
                     parent_entity_t_new_object);
}

absl::Status WorldObject::DeleteIfNoChildObjects() {
  return Delete(/*delete_child_objects=*/false);
}

absl::Status WorldObject::DeleteIncludingChildObjects() {
  return Delete(/*delete_child_objects=*/true);
}

absl::Status WorldObject::ReparentChildObjectsAndDelete() {
  WorldObject* parent = GetParent();
  if (parent == nullptr) {
    return absl::InvalidArgumentError("Cannot delete root object.");
  }

  std::vector<WorldObject*> children = GetChildren();

  // Checks for name collisions before attempting to reparent to avoid
  // reparenting some child objects, then returning an error due to name
  // collision.
  for (WorldObject* child : children) {
    INTR_RETURN_IF_ERROR(
        parent->CheckObjectNameAgainstChildrenObjectAndFrameNames(
            child->GetName()));
  }

  // Reparents all child objects to this object's parent while retaining their
  // global pose.
  for (WorldObject* child : children) {
    auto status = child->ReparentTo(*parent);
    if (!status.ok()) {
      return absl::InternalError(
          absl::Substitute("Failed to reparent child $0 with error: $1",
                           child->GetName(), status.message()));
    }
  }

  // Deletes this object (since it has no more child objects).
  return Delete(/*delete_child_objects=*/false);
}

absl::StatusOr<Frame*> WorldObject::CreateChildFrame(
    const FrameName& new_frame_name, const world::ObjectEntityFilter& filter,
    const Pose3d& entity_t_new_frame) {
  INTR_ASSIGN_OR_RETURN(
      WorldHashSet<AttachmentEntityId> entities,
      object_world_object_entity_filter_details::
          GetObjectEntitiesMatchingEntityFilter(*this, filter,
                                                /*expanded_list=*/false));
  if (entities.size() != 1) {
    return absl::InvalidArgumentError(
        "Unable to find single entity from entity filter.");
  }

  AttachmentEntityId parent_id = *entities.begin();

  Frame* parent_frame = nullptr;
  for (Frame* child_frame : GetChildFrames()) {
    if (child_frame->GetEntityId() == parent_id) {
      parent_frame = child_frame;
      break;
    }

    for (Frame* nested_frame : child_frame->GetChildFramesRecursively()) {
      if (nested_frame->GetEntityId() == parent_id) {
        parent_frame = nested_frame;
        break;
      }
    }
  }

  return CreateEntityAndFrame(new_frame_name, parent_id, parent_frame,
                              entity_t_new_frame);
}

absl::StatusOr<Frame*> WorldObject::CreateChildFrame(
    const FrameName& new_frame_name, const Pose3d& object_t_new_frame) {
  return CreateChildFrame(new_frame_name,
                          world::ObjectEntityFilter().IncludeBaseEntity(),
                          object_t_new_frame);
}

absl::StatusOr<Frame*> WorldObject::CreateChildFrameOfFinalEntity(
    const FrameName& new_frame_name, const Pose3d& object_t_new_frame) {
  INTR_ASSIGN_OR_RETURN(AttachmentEntityId root_id, GetRootEntityId());
  INTR_ASSIGN_OR_RETURN(
      AttachmentEntityId parent_id,
      FinalEntityIfKinematicObjectOrElseRootEntity(),
      _ << absl::Substitute("While creating child frame '$0' which will be "
                            "attached to the final entity of object '$1'",
                            new_frame_name.value(), GetName().value()));
  Pose3d parent_entity_t_new_frame =
      GetEntityWorld().GetTransform(parent_id, root_id) * object_t_new_frame;
  return CreateChildFrame(new_frame_name,
                          world::ObjectEntityFilter().IncludeFinalEntity(),
                          parent_entity_t_new_frame);
}

absl::Status WorldObject::CheckFrameNameIsAvailable(
    const FrameName& name) const {
  if (name.value().empty()) {
    return absl::InvalidArgumentError("The frame name must not be empty.");
  }

  if (name == FlangeFrameName() || name == SensorFrameName()) {
    return absl::InvalidArgumentError(absl::Substitute(
        "The desired new name \"$0\" is reserved.", name.value()));
  }

  INTR_RETURN_IF_ERROR(CheckNameIsCompatibleWithObjectView(name.value()));

  if (GetId() == RootObjectId()) {
    INTR_RETURN_IF_ERROR(
        GetObjectWorldData().CheckGlobalFrameNameAgainstGlobalObjectNames(
            name));
  }

  for (const Frame* frame : GetFrames()) {
    if (frame->GetName() == name) {
      return absl::InvalidArgumentError(absl::StrCat(
          "The frame name \"", name.value(), "\" is already in use."));
    }
  }

  for (const WorldObject* object : GetChildren()) {
    if (object->GetName().value() == name.value()) {
      return absl::InvalidArgumentError(
          absl::StrCat("The frame name \"", name.value(),
                       "\" is already in use by a sibling object"));
    }
  }

  return absl::OkStatus();
}

namespace {

absl::Status CheckObjectNameIsAvailable(const WorldObjectName& name,
                                        bool name_is_global_alias,
                                        const WorldObject& parent,
                                        const World& world,
                                        const ObjectWorldData& data) {
  if (name.value().empty()) {
    return absl::InvalidArgumentError("Requested world object name is empty");
  }

  if (name_is_global_alias) {
    // Check that object name is available as an alias.
    absl::Status alias_status = world.FindByAlias(name.value()).status();
    if (!absl::IsNotFound(alias_status)) {
      if (alias_status.ok()) {
        return absl::InvalidArgumentError(
            absl::Substitute("Requested name \"$0\" for new world object is "
                             "already in use in this world.",
                             name.value()));
      } else {
        return alias_status;
      }
    }

    // Check for collision of object name with existing global frame names.
    INTR_RETURN_IF_ERROR(data.CheckObjectNameAgainstGlobalFrameNames(name));
  }
  INTR_RETURN_IF_ERROR(
      parent.CheckObjectNameAgainstChildrenObjectAndFrameNames(name));

  INTR_RETURN_IF_ERROR(CheckNameIsCompatibleWithObjectView(name.value()));

  return absl::OkStatus();
}

absl::Status InitializeCollectionsEntityForObject(
    EntityId collection_id, const WorldObjectName& name,
    const WorldObjectNameType& name_type, const std::vector<EntityId>& link_ids,
    World* entity_world) {
  INTR_ASSIGN_OR_RETURN(WorldEntity * collection_entity,
                        entity_world->GetEntityById(collection_id));

  // Set object name.
  INTR_RETURN_IF_ERROR(collection_entity->SetLocalName(name.value()));
  if (name_type == WorldObjectNameType::kNameIsGlobalAlias) {
    INTR_RETURN_IF_ERROR(entity_world->SetAlias(collection_id, name.value()));
  }

  // Add link entities to collection.
  INTR_ASSIGN_OR_RETURN(
      CollectionsComponent * collections,
      collection_entity->GetOrCreateComponent<CollectionsComponent>());
  INTR_RETURN_IF_ERROR(collections->SetCollectionMembers(
      CollectionsComponent::kLinks, std::vector<CollectionsMemberEntityId>(
                                        link_ids.begin(), link_ids.end())));

  return absl::OkStatus();
}

absl::Status InitializeCollectionsMemberComponentForObjectEntity(
    EntityId member_id, CollectionsEntityId collection_id,
    World* entity_world) {
  INTR_ASSIGN_OR_RETURN(
      CollectionsMemberComponent * member,
      entity_world->GetOrCreateComponentByEntityId<CollectionsMemberComponent>(
          member_id));
  INTR_RETURN_IF_ERROR(member->AddParentCollection(
      CollectionsEntityId(collection_id), CollectionsComponent::kLinks));

  return absl::OkStatus();
}

template <class Component>
absl::Status CloneIfPresent(const WorldEntity* src_entity,
                            WorldEntity* dst_entity) {
  if (src_entity->HasComponent<Component>()) {
    INTR_ASSIGN_OR_RETURN(const Component* src_component,
                          src_entity->GetComponent<Component>());
    INTR_RETURN_IF_ERROR(dst_entity->SetComponent(src_component->Clone()));
  }

  return absl::OkStatus();
}

}  // namespace

absl::StatusOr<WorldObject*> WorldObject::CreateSingleEntityObject(
    const WorldObjectName& name, WorldObjectNameType name_type,
    AttachmentEntityId parent_entity_id,
    const Pose3d& parent_entity_t_new_object,
    std::optional<std::string> entity_local_name,
    std::unique_ptr<GeometryComponent> geometry_component) {
  INTR_RETURN_IF_ERROR(CheckObjectNameIsAvailable(
      name, name_type == WorldObjectNameType::kNameIsGlobalAlias, *this,
      GetEntityWorld(), GetObjectWorldData()));

  if (geometry_component->GetGeometryNames().empty()) {
    return absl::InvalidArgumentError(
        "Cannot create an object without any collision and visual geometry.");
  }

  // Create two entities.
  INTR_ASSIGN_OR_RETURN(World * world, GetMutableEntityWorld());
  EntityId collection_id = world->CreateEntity();
  EntityId link_id = world->CreateEntity();

  // Setup collections entity.
  INTR_RETURN_IF_ERROR(InitializeCollectionsEntityForObject(
      collection_id, name, name_type, {link_id}, world));

  // Setup link entity.
  INTR_ASSIGN_OR_RETURN(WorldEntity * link_entity,
                        world->GetEntityById(link_id));
  if (entity_local_name.has_value()) {
    INTR_RETURN_IF_ERROR(link_entity->SetLocalName(*entity_local_name));
  } else {
    INTR_RETURN_IF_ERROR(link_entity->SetLocalName("base_link"));
  }

  // Create attachment component.
  INTR_RETURN_IF_ERROR(
      world
          ->CreateAttachmentComponent(parent_entity_id, link_id,
                                      parent_entity_t_new_object, absl::Now())
          .status());

  // Create an empty collision component.
  INTR_RETURN_IF_ERROR(link_entity->CreateComponent<CollisionComponent>());

  // Create a geometry component with the given geometry.
  INTR_RETURN_IF_ERROR(
      link_entity->SetComponent(std::move(geometry_component)));

  // Create a default physics component.
  INTR_RETURN_IF_ERROR(link_entity->CreateComponent<PhysicsComponent>());

  // Create a collections member component pointing to the collections entity.
  INTR_RETURN_IF_ERROR(InitializeCollectionsMemberComponentForObjectEntity(
      link_id, CollectionsEntityId(collection_id), world));

  // Create a physical object.
  ObjectWorldResourceId object_id =
      ObjectWorldResourceIdForObject(CollectionsEntityId(collection_id));
  auto object = std::make_unique<PhysicalObject>(
      object_id, name, CollectionsEntityId(collection_id),
      WorldHashSet<AttachmentEntityId>{AttachmentEntityId(link_id)},
      GetObjectWorldData());
  WorldObject* object_raw = object.get();

  object->SetParentAsymmetric(this);
  this->AddChildAsymmetric(object_raw);

  // Store physical object.
  INTR_RETURN_IF_ERROR(GetObjectWorldData().InsertObject(std::move(object)));

  return object_raw;
}

namespace {

struct IntegrationWorld {
  World world;  // Owns the entities pointed to by 'entities'.
  CollectionsEntityId collection_id;
};

// TODO(stoyang): Combine this code with the one used by world_service_impl.cc?
absl::StatusOr<IntegrationWorld> ValidateAndFixFragment(
    const intrinsic_proto::world::WorldFragment& fragment, WorldObjectName name,
    bool name_is_global_alias, const Pose3d& parent_entity_t_new_object,
    GeometryLibrary* geolib) {
  World integration_world = World::CreateEmptyWorld();

  // Check to see if we need to deserialize any of the geometries.
  bool deserialize_geometries =
      absl::GetFlag(FLAGS_always_deserialize_geometries);
  for (const auto& [_, entity] : fragment.entities()) {
    if (deserialize_geometries) {
      break;
    }
    deserialize_geometries = deserialize_geometries ||
                             NeedsDeserialization(entity.geometry_component());
  }

  if (deserialize_geometries) {
    if (geolib == nullptr) {
      return absl::InvalidArgumentError(
          "Must provide deserializer when trying to deserialize geometries!");
    }
    INTR_RETURN_IF_ERROR(
        integration_world
            .IntegrateFragment(kRootEntityId, kDefaultEntityIdPrefix, fragment,
                               geolib->Deserializer())
            .status());

    // Reserialize the geometry protos so that we have the protos in place when
    // we might need to write them out. This happens automatically with v1
    // protos, but any v0 protos will not be present.
    for (const GeometryEntityId entity_id :
         integration_world.GetTypedEntityIds<GeometryEntityId>()) {
      INTR_ASSIGN_OR_RETURN(
          auto* geo_component,
          integration_world.GetComponentByEntityId<GeometryComponent>(
              entity_id));
      INTR_RETURN_IF_ERROR(
          ReserializeGeometryComponent(geo_component, *geolib));
    }
  } else {
    INTR_RETURN_IF_ERROR(
        integration_world
            .IntegrateFragment(kRootEntityId, kDefaultEntityIdPrefix, fragment)
            .status());
  }

  // Make sure that the input contains exactly one collection. Not all input
  // entities need to be part of this collection since the input could
  // correspond to an object with frames and the entities would not be part of
  // the collection.
  // TODO(b/237700846): Drop this 'fix' code once data is migrated.
  std::vector<CollectionsEntityId> input_collection_ids =
      integration_world.GetTypedEntityIds<CollectionsEntityId>();
  CollectionsEntityId collection_id;
  if (input_collection_ids.size() > 1) {
    return absl::InvalidArgumentError(
        "Cannot create object from a set of entities that contains more "
        "than one collections entity.");
  } else if (input_collection_ids.empty()) {
    // Auto-create a collections entity if it is missing. This is intended for
    // backwards compatibility and will only work in very simple cases.
    LOG(WARNING) << "Creating object from a set of entities which does not "
                    "contain a collection entity. This only works in very "
                    "simple cases. Please update the entity set (e.g., a "
                    "WorldFragment).";
    WorldHashSet<EntityId> all_ids = integration_world.GetEntityIds();
    all_ids.erase(kRootEntityId);
    collection_id = integration_world.CreateTypedEntity<CollectionsEntityId>();
    INTR_ASSIGN_OR_RETURN(
        CollectionsComponent * collection,
        integration_world.GetComponentByEntityId<CollectionsComponent>(
            collection_id));
    INTR_RETURN_IF_ERROR(collection->SetCollectionMembers(
        CollectionsComponent::kLinks, std::vector<CollectionsMemberEntityId>(
                                          all_ids.begin(), all_ids.end())));
    for (EntityId id : all_ids) {
      INTR_ASSIGN_OR_RETURN(WorldEntity * entity,
                            integration_world.GetEntityById(id));
      INTR_ASSIGN_OR_RETURN(
          CollectionsMemberComponent * member,
          entity->GetOrCreateComponent<CollectionsMemberComponent>());
      INTR_RETURN_IF_ERROR(member->AddParentCollection(
          collection_id, CollectionsComponent::kLinks));
    }
  } else {
    collection_id = input_collection_ids.front();
  }

  // Clear all aliases in the given entities as they may conflict with entity
  // aliases in the world we are integrating to.
  for (EntityId id : integration_world.GetEntityIds()) {
    INTR_RETURN_IF_ERROR(integration_world.SetAlias(id, ""));
  }

  // Assign object name to the collection.
  INTR_ASSIGN_OR_RETURN(WorldEntity * collection_entity,
                        integration_world.GetEntityById(collection_id));
  INTR_RETURN_IF_ERROR(collection_entity->SetLocalName(name.value()));
  if (name_is_global_alias) {
    INTR_RETURN_IF_ERROR(
        integration_world.SetAlias(collection_id, name.value()));
  } else {
    INTR_RETURN_IF_ERROR(integration_world.SetAlias(collection_id, ""));
  }

  // To be able to meaningfully add the input entities to the world as an
  // object, they need to have a common root entity which is attached to the
  // root.
  std::vector<AttachmentEntityId> root_children =
      integration_world.GetChildrenOf(kRootEntityId);
  if (root_children.size() > 1) {
    return absl::InvalidArgumentError(
        "Cannot create object from a set of entities that does not have a "
        "common root.");
  } else if (root_children.empty()) {
    return absl::InvalidArgumentError(
        "Cannot create object from a set of entities that does not have at "
        "least one attachment entity.");
  } else {
    AttachmentEntityId object_root_id = root_children.front();
    // Set desired object offset. Note that this overwrites the existing
    // "root_t_object_root" offset in the input entities which could be
    // non-zero.
    integration_world.UpdateAttachmentPose(kRootEntityId, object_root_id,
                                           parent_entity_t_new_object);
    if (!name_is_global_alias) {
      INTR_RETURN_IF_ERROR(integration_world.SetAlias(object_root_id, ""));
    }
  }

  // Check that (after the automated fixes above) the input entities on their
  // own would produce an object-view compatible world.
  INTR_RETURN_IF_ERROR(ObjectWorld::CreateView(integration_world).status());

  return IntegrationWorld{
      .world = std::move(integration_world),
      .collection_id = collection_id,
  };
}

// We hash the name into a 16 bit unsigned int to use as a prefix for all of the
// fragment entities in the CreateObjectFromFragment call.
uint16_t ComputeEntityIdPrefix(const std::string& name) {
  return static_cast<uint16_t>(
      static_cast<uint32_t>(absl::ComputeCrc32c(name)) >> 16);
}

// Excludes adjacent links from collision for the given collection entity.
absl::Status ExcludeAdjacentLinksFromCollision(
    World* world, CollectionsEntityId collections_id) {
  INTR_ASSIGN_OR_RETURN(
      const CollectionsComponent* collections_component,
      world->GetComponentByEntityId<CollectionsComponent>(collections_id));

  auto FindParentLink = [world,
                         collections_id](const AttachmentEntityId link_id)
      -> absl::StatusOr<std::optional<PhysicalEntityId>> {
    AttachmentEntityId parent_id = link_id;
    while (true) {
      // Gets the parent entity.
      const auto entity_id = parent_id;
      INTR_ASSIGN_OR_RETURN(
          const AttachmentComponent* attachment,
          world->GetComponentByEntityId<AttachmentComponent>(parent_id));
      parent_id = attachment->GetParentId();

      // This should never happen in practice but return error to avoid infinite
      // loop.
      if (parent_id == entity_id) {
        return absl::InvalidArgumentError(absl::StrCat(
            "Entity ", entity_id.value(), " is attached to itself."));
      }

      if (parent_id == kRootEntityId || parent_id == kInvalidEntityId) {
        return std::nullopt;
      }

      // Checks if the parent entity is a link.
      // Note that it is possible for a link to be directly connected with
      // another link, and not necessarily via a joint.
      const auto parent_collections_member =
          world->GetComponentByEntityId<CollectionsMemberComponent>(parent_id);
      if (parent_collections_member.ok() &&
          (*parent_collections_member)
              ->IsMemberOfCollection(collections_id,
                                     CollectionsComponent::kLinks)) {
        INTR_ASSIGN_OR_RETURN(
            const PhysicalEntityId physical_entity_id,
            world->ValidateEntity<PhysicalEntityId>(parent_id));
        return physical_entity_id;
      }
    }

    return std::nullopt;
  };

  // Iterates through links and adds collision exclusion between the adjacent
  // links.
  for (const auto link_member_id : collections_component->GetCollectionMembers(
           CollectionsComponent::kLinks)) {
    INTR_ASSIGN_OR_RETURN(
        const auto link_attachment_id,
        world->ValidateEntity<AttachmentEntityId>(link_member_id));

    INTR_ASSIGN_OR_RETURN(const std::optional<PhysicalEntityId> parent_link,
                          FindParentLink(link_attachment_id));
    if (!parent_link.has_value()) {
      continue;
    }

    INTR_ASSIGN_OR_RETURN(
        const auto child_link,
        world->ValidateEntity<PhysicalEntityId>(link_member_id));

    INTR_RETURN_IF_ERROR(world->AddExclusionPair(child_link, *parent_link));
  }
  return absl::OkStatus();
}

absl::Status ApplyRobotSimulationSpec(
    World* world, const CollectionsEntityId collections_id,
    const absl::string_view object_name,
    const intrinsic_proto::scene_object::v1::RobotSimPluginSpec& spec) {
  using IconSimDevice = ::intrinsic_proto::world::RobotComponent::IconSimDevice;

  auto joint_name_to_id_func = [](const World* world,
                                  const CollectionsEntityId collections_id)
      -> absl::StatusOr<WorldHashMap<std::string, JointEntityId>> {
    WorldHashMap<std::string, JointEntityId> joint_names_to_id;
    INTR_ASSIGN_OR_RETURN(auto all_joint_ids,
                          world->ValidateCollectionMembers<JointEntityId>(
                              collections_id, CollectionsComponent::kJoints));
    for (const auto joint_id : all_joint_ids) {
      joint_names_to_id.insert(
          {world->GetLocalNameForEntityById(joint_id), joint_id});
    }

    return joint_names_to_id;
  };

  INTR_ASSIGN_OR_RETURN(
      auto* robot_component,
      world->GetComponentByEntityId<RobotComponent>(collections_id),
      _ << "Cannot set robot simulation spec for non-kinematic object: "
        << object_name << ".");

  // Sets IconSimDevice plugin for individual devices.
  INTR_ASSIGN_OR_RETURN(const auto joint_name_to_id,
                        joint_name_to_id_func(world, collections_id));
  std::vector<IconSimDevice> sim_devices;
  for (const auto& device_spec : spec.device_specs()) {
    const auto& device_name = device_spec.name();
    if (device_name.empty()) {
      return absl::InvalidArgumentError(
          "A simulated joint device does not provide a name.");
    }

    // If joint entity is set, it should be non-empty and part of the
    // existing kinematic structure.
    if (device_spec.has_joint_entity()) {
      const auto& device_joint = device_spec.joint_entity();
      if (device_joint.empty()) {
        LOG(ERROR) << "Simulated joint device '" << device_name
                   << "' does not provide a joint name";
        return absl::InvalidArgumentError(
            "A simulated joint device does not provide which joint it is "
            "attached to.");
      }

      if (!joint_name_to_id.contains(device_joint)) {
        LOG(ERROR) << "Simulated joint device '" << device_name
                   << "' is attached to a non-existent joint: " << device_joint;
        return absl::InvalidArgumentError(absl::StrCat(
            "A simulated joint device is attached to a non-existent joint: ",
            device_joint, "."));
      }
    }

    IconSimDevice device;
    device.set_name(device_name);
    device.set_type(device_spec.type());
    if (device_spec.has_joint_entity()) {
      device.set_joint(device_spec.joint_entity());
      // Update initial state to current joint value. After the initial import
      // from SDF, any SceneObject or World updates override the initial
      // position from the SDF.
      INTR_ASSIGN_OR_RETURN(auto* kin_component,
                            world->GetComponentByEntityId<KinematicsComponent>(
                                joint_name_to_id.at(device.joint())),
                            _ << "Failed to populate initial joint value from "
                                 "KinematicsComponent "
                                 "for joint '"
                              << device_spec.joint_entity() << "'.");
      device.set_initial(kin_component->GetRawValue());
    }
    sim_devices.push_back(device);
  }
  robot_component->SetIconSimDevices(sim_devices);

  return absl::OkStatus();
}

absl::Status ApplySimulationSpec(
    World* world, const CollectionsEntityId collections_id,
    const absl::string_view object_name,
    const intrinsic_proto::scene_object::v1::SimulationSpec& sim_spec) {
  using SimulationSpec = ::intrinsic_proto::scene_object::v1::SimulationSpec;

  switch (sim_spec.object_spec_case()) {
    case SimulationSpec::kRobot:
      INTR_RETURN_IF_ERROR(ApplyRobotSimulationSpec(
          world, collections_id, object_name, sim_spec.robot()));
      break;
    case SimulationSpec::OBJECT_SPEC_NOT_SET:
      break;
  }

  INTR_ASSIGN_OR_RETURN(
      auto* simulation_component,
      world->GetOrCreateComponentByEntityId<SimulationComponent>(
          collections_id));
  simulation_component->SetIsStatic(sim_spec.is_static());
  simulation_component->SetDisabled(sim_spec.disabled());

  std::string legacy_multi_camera_plugin_xml;
  std::vector<absl::string_view> extra_plugins;
  // TODO(b/538734403): Handles legacy scene objects with explicit multi camera
  // plugin spec.
  if (sim_spec.has_multi_camera_plugin()) {
    legacy_multi_camera_plugin_xml = simulation::PluginSdfTrait<
        intrinsic_proto::scene_object::v1::MultiCameraPluginSpec>::
        ToString(sim_spec.multi_camera_plugin());
    extra_plugins.push_back(legacy_multi_camera_plugin_xml);
  }
  extra_plugins.insert(extra_plugins.end(),
                       sim_spec.extra_inlined_plugins().begin(),
                       sim_spec.extra_inlined_plugins().end());

  if (!extra_plugins.empty()) {
    INTR_ASSIGN_OR_RETURN(
        auto* user_data_component,
        world->GetOrCreateComponentByEntityId<UserDataComponent>(
            collections_id));
    google::protobuf::StringValue str_val;
    str_val.set_value(absl::StrJoin(extra_plugins, "\n"));
    static_cast<void>(
        user_data_component->MutableUserDataProtos()[sdf::kGazeboPlugins]
            .PackFrom(str_val));  // TODO: Handle return value
  }

  return absl::OkStatus();
}

// TODO(stoyang): Consider moving this out to a public util file.
absl::StatusOr<intrinsic_proto::RuleSet> GenerateRuleSetFromCollisionRules(
    const intrinsic_proto::scene_object::v1::CollisionRules& rules,
    const WorldHashMap<std::string, AttachmentEntityId>& model_entities) {
  WorldHashSet<std::pair<std::string, std::string>> excluded_entity_pairs;

  // Compress the exclusion rules into a set of entity pairs.
  for (const auto& collision_rule : rules.exclusion_rules()) {
    const auto& entity_pair = collision_rule.entity_pair();
    const auto& left_entity_name = entity_pair.left_entity().entity_name();
    const auto& right_entity_name = entity_pair.right_entity().entity_name();

    // Check for the same left and right entity name.
    if (!left_entity_name.empty() && left_entity_name == right_entity_name) {
      LOG(WARNING) << "Collision rule has the same left and right entity name '"
                   << left_entity_name
                   << "'. This is effectively a no-op and will be ignored.";
      continue;
    }

    if (collision_rule.is_excluded()) {
      excluded_entity_pairs.insert({left_entity_name, right_entity_name});
      excluded_entity_pairs.erase({right_entity_name, left_entity_name});
    } else {
      excluded_entity_pairs.erase({left_entity_name, right_entity_name});
      excluded_entity_pairs.erase({right_entity_name, left_entity_name});
    }
  }

  intrinsic_proto::RuleSet rule_set;

  intrinsic_proto::world::CollisionAction exclusion_action;
  exclusion_action.set_is_excluded(true);
  for (const auto& [left_entity_name, right_entity_name] :
       excluded_entity_pairs) {
    if (!left_entity_name.empty() &&
        !model_entities.contains(left_entity_name)) {
      return absl::InvalidArgumentError(
          absl::StrCat("Collision rule references non-existent entity \"",
                       left_entity_name, "\"."));
    }

    if (!right_entity_name.empty() &&
        !model_entities.contains(right_entity_name)) {
      return absl::InvalidArgumentError(
          absl::StrCat("Collision rule references non-existent entity \"",
                       right_entity_name, "\"."));
    }

    auto* rule = rule_set.add_rules();
    *rule->mutable_action() = exclusion_action;

    if (!left_entity_name.empty()) {
      rule->add_id_1(model_entities.at(left_entity_name).value());
    }

    if (!right_entity_name.empty()) {
      rule->add_id_2(model_entities.at(right_entity_name).value());
    }
  }

  return rule_set;
}

}  // namespace

absl::StatusOr<WorldObject*> WorldObject::CreateObjectFromSceneObject(
    const WorldObjectName& name, WorldObjectNameType name_type,
    const AttachmentEntityId parent_entity_id,
    const Pose3d& parent_entity_t_new_object, GeometryLibrary* geolib,
    const intrinsic_proto::scene_object::v1::SceneObject& scene_object) {
  const absl::Time create_time = absl::Now();
  // Validate the input scene object before doing anything else.
  INTR_RETURN_IF_ERROR(scene_object::ValidateSceneObject(scene_object))
      << " For SceneObject: " << name;

  using EntityProto = intrinsic_proto::scene_object::v1::Entity;
  World integrate_world = World::CreateEmptyWorld();

  const bool name_is_global_alias =
      name_type == WorldObjectNameType::kNameIsGlobalAlias;
  INTR_RETURN_IF_ERROR(CheckObjectNameIsAvailable(name, name_is_global_alias,
                                                  *this, integrate_world,
                                                  GetObjectWorldData()));

  // Create a collection entity to contain the new object.
  const CollectionsEntityId collections_id =
      integrate_world.CreateTypedEntity<CollectionsEntityId>();
  INTR_ASSIGN_OR_RETURN(WorldEntity * collections_entity,
                        integrate_world.GetEntityById(collections_id));
  INTR_RETURN_IF_ERROR(collections_entity->SetLocalName(name.value()));
  if (name_is_global_alias) {
    INTR_RETURN_IF_ERROR(
        integrate_world.SetAlias(collections_id, name.value()));
  } else {
    INTR_RETURN_IF_ERROR(integrate_world.SetAlias(collections_id, ""));
  }
  INTR_ASSIGN_OR_RETURN(
      CollectionsComponent * collections_component,
      collections_entity->GetComponent<CollectionsComponent>());

  // Generate entities based on the model definition. We store them here based
  // on the entity names to facilitate parenting relationships.
  WorldHashMap<std::string, AttachmentEntityId> model_entities;
  for (const EntityProto& entity : scene_object.entities()) {
    // All entities have an attachment component.
    AttachmentEntityId new_entity_id =
        integrate_world.CreateTypedEntity<AttachmentEntityId>();
    INTR_ASSIGN_OR_RETURN(WorldEntity * new_entity,
                          integrate_world.GetEntityById(new_entity_id));

    INTR_RETURN_IF_ERROR(new_entity->SetLocalName(entity.name()));

    // Must be set by the type of the entity.
    std::vector<intrinsic_proto::world::CollectionsComponent::CollectionType>
        collection_types;

    switch (entity.entity_type_case()) {
      case EntityProto::kFrame: {
        // Only need to add the frame to the coordinate frames of the
        // associated collections entity.
        collection_types.push_back(CollectionsComponent::kCoordinateFrames);
        if (entity.frame().is_attachment_frame()) {
          collection_types.push_back(CollectionsComponent::kAttachmentFrames);
        }
        break;
      }
      case EntityProto::kLink: {
        // Create empty collision component for this entity.
        INTR_RETURN_IF_ERROR(new_entity->CreateComponent<CollisionComponent>());
        INTR_RETURN_IF_ERROR(
            new_entity->CreateComponentFromProto<PhysicsComponent>(
                entity.link().physics_component()));
        INTR_ASSIGN_OR_RETURN(
            GeometryComponent * geo_component,
            new_entity->GetOrCreateComponent<GeometryComponent>());

        // Check to see if we need to deserialize any of the geometries.
        const bool deserialize_geometries =
            absl::GetFlag(FLAGS_always_deserialize_geometries) ||
            NeedsDeserialization(entity.link().geometry_component());

        if (deserialize_geometries) {
          INTR_RET_CHECK(geolib != nullptr);
          INTR_RETURN_IF_ERROR(geo_component->UpdateFromProto(
              entity.link().geometry_component(), geolib->Deserializer()))
              << "Failed to create link entity: " << entity.name();
          INTR_RETURN_IF_ERROR(
              ReserializeGeometryComponent(geo_component, *geolib));
        } else {
          INTR_RETURN_IF_ERROR(geo_component->UpdateFromProto(
              entity.link().geometry_component()))
              << "Failed to create link entity: " << entity.name();
        }

        collection_types.push_back(CollectionsComponent::kLinks);
        break;
      }
      case EntityProto::kJoint: {
        INTR_RETURN_IF_ERROR(
            new_entity->CreateComponentFromProto<KinematicsComponent>(
                entity.joint().kinematics_component()))
            << "Failed to create joint entity: " << entity.name();
        collection_types.push_back(CollectionsComponent::kJoints);
        break;
      }
      case EntityProto::kSensor: {
        INTR_RETURN_IF_ERROR(
            new_entity->CreateComponentFromProto<SensorComponent>(
                entity.sensor().sensor_component()))
            << "Failed to create sensor entity: " << entity.name();
        collection_types.push_back(CollectionsComponent::kSensors);
        break;
      }
      default: {
        return absl::InvalidArgumentError(
            absl::Substitute("Model $0 contains entity $1 that is neither "
                             "frame, link, joint, nor sensor.",
                             scene_object.name(), entity.name()));
      }
    }

    for (const auto collection_type : collection_types) {
      // Mark entity as a collections member.
      INTR_ASSIGN_OR_RETURN(
          CollectionsMemberComponent * entity_collections_member,
          new_entity->GetOrCreateComponent<CollectionsMemberComponent>());
      INTR_RETURN_IF_ERROR(entity_collections_member->AddParentCollection(
          collections_id, collection_type));

      // Add entity to collection's list.
      std::vector<CollectionsMemberEntityId> new_collection_members(
          collections_component->GetCollectionMembers(collection_type));
      INTR_ASSIGN_OR_RETURN(
          CollectionsMemberEntityId member_id,
          integrate_world.ValidateEntity<CollectionsMemberEntityId>(
              new_entity_id));
      new_collection_members.push_back(member_id);
      INTR_RETURN_IF_ERROR(collections_component->SetCollectionMembers(
          collection_type, new_collection_members));
    }

    if (model_entities.contains(entity.name())) {
      return absl::InvalidArgumentError(
          absl::StrCat("Duplicate entity name \"", entity.name(),
                       "\" found in scene object."));
    }
    model_entities[entity.name()] = new_entity_id;
  }

  // Establish parenting relationship between created entities.
  for (const EntityProto& entity : scene_object.entities()) {
    AttachmentEntityId entity_id = model_entities.at(entity.name());
    INTR_ASSIGN_OR_RETURN(
        AttachmentComponent * entity_attachment,
        integrate_world.GetComponentByEntityId<AttachmentComponent>(entity_id));

    AttachmentEntityId entity_parent_id = kRootEntityId;
    if (!entity.parent_name().empty() &&
        model_entities.contains(entity.parent_name())) {
      entity_parent_id = model_entities.at(entity.parent_name());
    }

    if (entity_id == entity_parent_id) {
      return absl::InvalidArgumentError(absl::Substitute(
          "Object '$0' has entity '$1' that is attached to itself.",
          name.value(), entity.name()));
    }

    entity_attachment->SetParentId(entity_parent_id);

    Pose3d parent_t_this;
    if (entity.has_parent_t_this()) {
      INTR_ASSIGN_OR_RETURN(parent_t_this, FromProto(entity.parent_t_this()));
    }
    if (entity_parent_id == kRootEntityId) {
      if (!parent_t_this.isApprox(Pose3d::Identity())) {
        LOG(WARNING) << "Scene object entity '" << entity.name()
                     << "' has a non-identity parent_t_this pose '"
                     << parent_t_this
                     << "', when its parent_name is empty. This pose will be "
                        "overridden by the parent_entity_t_new_object pose for "
                        "consistency with the object_t_new_object API.";
      }
      parent_t_this = parent_entity_t_new_object;
    }
    entity_attachment->SetParentTThis(parent_t_this, create_time);
  }

  // Exclude adjacent links from collision by default.
  INTR_RETURN_IF_ERROR(
      ExcludeAdjacentLinksFromCollision(&integrate_world, collections_id))
      << "Failed to exclude adjacent links from collision for object: "
      << name.value();

  // Add collision rules from the scene object.
  if (scene_object.has_collision_rules()) {
    INTR_ASSIGN_OR_RETURN(
        auto extra_rule_set,
        GenerateRuleSetFromCollisionRules(scene_object.collision_rules(),
                                          model_entities),
        _ << "Failed to add collision rules for object: " << name.value());

    // Set the rule set after all of the extra rules have been added.
    auto rule_set = integrate_world.GetDefaultRuleSet();
    rule_set.MergeFrom(extra_rule_set);
    INTR_RETURN_IF_ERROR(integrate_world.SetDefaultRuleSet(rule_set));
  }

  const bool is_kinematic_object = IsKinematic(scene_object);

  const bool has_devices =
      scene_object.has_simulation_spec() &&
      scene_object.simulation_spec().has_robot() &&
      scene_object.simulation_spec().robot().device_specs_size() > 0;

  // Allow non robots to have RobotComponent.
  if (is_kinematic_object || has_devices) {
    INTR_RETURN_IF_ERROR(collections_entity->CreateComponent<RobotComponent>());
  }

  if (is_kinematic_object) {
    RobotCollectionsEntityId robot_id(collections_id.value());

    // Sort the robot links and joints.
    INTR_RETURN_IF_ERROR(integrate_world.SortRobotLinkAndJointLists(robot_id));

    // Update the poses of joint attachments based on their joint values.
    for (const auto& joint_id : collections_component->GetCollectionMembers(
             CollectionsComponent::kJoints)) {
      INTR_ASSIGN_OR_RETURN(
          JointEntityId joint_attachment_id,
          integrate_world.ValidateEntity<JointEntityId>(joint_id));
      INTR_RETURN_IF_ERROR(
          integrate_world.RefreshDofParentTThis(joint_attachment_id));
    }
  }

  // Handles sim plugins.
  if (scene_object.has_simulation_spec()) {
    INTR_RETURN_IF_ERROR(ApplySimulationSpec(&integrate_world, collections_id,
                                             name.value(),
                                             scene_object.simulation_spec()))
            .LogError()
        << "Failed to apply simulation spec for object: " << name.value();
  }

  INTR_ASSIGN_OR_RETURN(World * world, GetMutableEntityWorld());
  INTR_ASSIGN_OR_RETURN(
      const auto id_mapping,
      world->IntegrateWorld(parent_entity_id,
                            ComputeEntityIdPrefix(name.value()),
                            integrate_world));

  CollectionsEntityId real_collections_id(id_mapping.at(collections_id));

  // Create a kinematic object or physical object based on the presence of
  // RobotComponent.
  ObjectWorldResourceId object_id =
      ObjectWorldResourceIdForObject(real_collections_id);

  std::unique_ptr<PhysicalObject> object;

  // TODO(b/301646385) Not all RobotCollectionsEntityIds are actually robots,
  // so ensure that the collection has non-zero DOFs before identifying it as
  // a KinematicObject.

  WorldHashSet<AttachmentEntityId> member_ids;
  for (const auto& [_, id] : model_entities) {
    member_ids.emplace(id_mapping.at(id));
  }

  if (is_kinematic_object) {
    INTR_ASSIGN_OR_RETURN(
        RobotCollectionsEntityId robot_entity_id,
        world->ValidateEntity<RobotCollectionsEntityId>(real_collections_id));
    auto kinematic_object = std::make_unique<KinematicObject>(
        object_id, name, robot_entity_id, std::move(member_ids),
        GetObjectWorldData());

    const auto& kinematics_proto = scene_object.properties().kinematics();
    if (kinematics_proto.has_limits()) {
      INTR_ASSIGN_OR_RETURN(const auto cart_limits,
                            scene_object::FromProto(kinematics_proto.limits()));
      INTR_RETURN_IF_ERROR(kinematic_object->SetCartesianLimits(cart_limits));
    }

    // If we have named configurations, we need to set them on the robot.
    if (!kinematics_proto.named_configurations().empty()) {
      INTR_ASSIGN_OR_RETURN(auto joint_ids,
                            kinematic_object->GetJointEntityIds());

      std::map<std::string, int> joint_index_by_name;
      int index = 0;
      for (const auto& joint_id : joint_ids) {
        INTR_ASSIGN_OR_RETURN(
            const WorldEntity* joint_entity,
            kinematic_object->GetEntityWorld().GetEntityById(joint_id));
        joint_index_by_name[joint_entity->GetLocalName()] = index++;
      }

      // For each named configuration, we need parse the joint names and arrange
      // the positions within the VectorXd before setting it on the robot.
      for (const auto& named_config : kinematics_proto.named_configurations()) {
        eigenmath::VectorXd joint_positions(joint_ids.size());
        for (const auto& [joint_name, joint_position] :
             named_config.joint_positions()) {
          joint_positions[joint_index_by_name.at(joint_name)] = joint_position;
        }

        INTR_RETURN_IF_ERROR(kinematic_object->SetNamedJointConfiguration(
            named_config.name(), joint_positions));
      }
    }

    // Adds kinematic solver.
    // - We require that adding the ik_solver succeeds if ik_solver key is
    //   provided in SceneObject. Else, we try to use the default solver but
    //   ignore any errors.
    // - This is in contrast to the world_from_sdf logic that applies the
    //   default kinematic solver for an object that has non-fixed joints
    //   (including for grippers).
    // http://intrinsic/world/conversion/sdf/world_from_sdf.cc;l=582-584;rcl=630193737
    const auto& provided_ik_solvers = kinematics_proto.ik_solvers();
    // TODO(b/343309271): Support multiple ik_solvers.
    if (provided_ik_solvers.size() > 1) {
      std::vector<std::string> solver_keys;
      for (const auto& ik_solver : provided_ik_solvers) {
        solver_keys.push_back(ik_solver.ik_solver());
      }
      return absl::InvalidArgumentError(absl::StrCat(
          "Multiple ik solvers found for object '", name.value(), "': ",
          absl::StrJoin(solver_keys, ", "), ". Only one should be provided."));
    }

    const std::string solver_key = provided_ik_solvers.empty()
                                       ? kDefaultKinematicSolverKey
                                       : provided_ik_solvers[0].ik_solver();
    const std::optional<std::string> tip_link_name =
        provided_ik_solvers.empty() ||
                !provided_ik_solvers[0].has_tip_link_name()
            ? std::nullopt
            : std::make_optional(provided_ik_solvers[0].tip_link_name());

    if (provided_ik_solvers.empty()) {
      LOG(INFO) << "Using default kinematic solver key: " << solver_key;
    }

    const auto ik_status =
        kinematic_object->AddIkSolverKey(solver_key, tip_link_name);
    if (!ik_status.ok()) {
      LOG(WARNING) << "Failed to add ik solver '" << solver_key
                   << "' for object '" << name.value()
                   << "' with error: " << ik_status;
      // Raises the error only if ik_solver was provided.
      if (!provided_ik_solvers.empty()) {
        return ik_status;
      }
    }

    object = std::move(kinematic_object);
  } else {
    object = std::make_unique<PhysicalObject>(
        object_id, name, real_collections_id, std::move(member_ids),
        GetObjectWorldData());
  }

  // Add the user-data field to the generated object
  if (!scene_object.user_data().empty()) {
    INTR_ASSIGN_OR_RETURN(WorldObject::UserDataProtos * user_data,
                          object->GetMutableUserDataProtos());
    for (const auto& [key, value] : scene_object.user_data()) {
      (*user_data)[key] = value;
    }
  }

  WorldObject* object_raw = object.get();

  object->SetParentAsymmetric(this);
  this->AddChildAsymmetric(object_raw);

  INTR_RETURN_IF_ERROR(object->FindAndAddFrames(/*non_critical_errors=*/nullptr,
                                                /*attachment_graph=*/nullptr));

  // Store the physical object.
  INTR_RETURN_IF_ERROR(GetObjectWorldData().InsertObject(std::move(object)));

  return object_raw;
}

absl::StatusOr<WorldObject*> WorldObject::CreateObjectFromFragment(
    const WorldObjectName& name, WorldObjectNameType name_type,
    AttachmentEntityId parent_entity_id,
    const Pose3d& parent_entity_t_new_object, GeometryLibrary* geolib,
    const intrinsic_proto::world::WorldFragment& fragment) {
  INTR_RETURN_IF_ERROR(CheckObjectNameIsAvailable(
      name, name_type == WorldObjectNameType::kNameIsGlobalAlias, *this,
      GetEntityWorld(), GetObjectWorldData()));

  // Verify input entities and possible apply some automated fixes.
  INTR_ASSIGN_OR_RETURN(
      IntegrationWorld integration_world,
      ValidateAndFixFragment(
          fragment, name, name_type == WorldObjectNameType::kNameIsGlobalAlias,
          parent_entity_t_new_object, geolib));

  // Add fixed input entities to entity world.
  INTR_ASSIGN_OR_RETURN(World * world, GetMutableEntityWorld());
  INTR_ASSIGN_OR_RETURN(
      (WorldHashMap<EntityId, EntityId> mapping),
      world->IntegrateWorld(parent_entity_id,
                            ComputeEntityIdPrefix(name.value()),
                            integration_world.world));

  // Find the entity ids corresponding to the created object.
  CollectionsEntityId integrated_collection_id =
      CollectionsEntityId(mapping[integration_world.collection_id]);
  WorldHashSet<AttachmentEntityId> integrated_member_ids;
  {
    INTR_ASSIGN_OR_RETURN(
        const WorldEntity* collection_entity,
        integration_world.world.GetEntityById(integration_world.collection_id));
    INTR_ASSIGN_OR_RETURN(
        const CollectionsComponent* collection,
        collection_entity->GetComponent<CollectionsComponent>());
    for (CollectionsMemberEntityId member_id :
         collection->GetAllCollectionMembers()) {
      INTR_ASSIGN_OR_RETURN(
          AttachmentEntityId member_attachment_id,
          integration_world.world.ValidateEntity<AttachmentEntityId>(
              member_id));
      integrated_member_ids.insert(
          AttachmentEntityId(mapping[member_attachment_id]));
    }
  }

  // Create a kinematic object or physical object based on the presence of
  // RobotComponent.
  ObjectWorldResourceId object_id =
      ObjectWorldResourceIdForObject(integrated_collection_id);
  std::unique_ptr<PhysicalObject> object;
  // TODO(b/301646385) Not all RobotCollectionsEntityIds are actually robots,
  // so ensure that the collection has non-zero DOFs before identifying it as
  // a KinematicObject.
  auto robot_entity_id =
      world->ValidateEntity<RobotCollectionsEntityId>(integrated_collection_id);
  bool is_kinematic_object = false;
  if (robot_entity_id.ok()) {
    absl::StatusOr<std::vector<JointEntityId>> joints =
        GetEntityWorld().GetRobotDofs(*robot_entity_id);
    is_kinematic_object = joints.ok() && !(*joints).empty();
  }
  if (is_kinematic_object) {
    object = std::make_unique<KinematicObject>(
        object_id, name, *robot_entity_id, std::move(integrated_member_ids),
        GetObjectWorldData());
  } else {
    object = std::make_unique<PhysicalObject>(
        object_id, name, integrated_collection_id,
        std::move(integrated_member_ids), GetObjectWorldData());
  }
  WorldObject* object_raw = object.get();

  object->SetParentAsymmetric(this);
  this->AddChildAsymmetric(object_raw);

  INTR_RETURN_IF_ERROR(object->FindAndAddFrames(/*non_critical_errors=*/nullptr,
                                                /*attachment_graph=*/nullptr));

  // Store the physical object.
  INTR_RETURN_IF_ERROR(GetObjectWorldData().InsertObject(std::move(object)));

  return object_raw;
}

namespace {

absl::Status VerifyLinkCanBeCloned(const WorldObject& existing_object,
                                   AttachmentEntityId existing_link_id) {
  INTR_ASSIGN_OR_RETURN(
      const WorldEntity* existing_link,
      existing_object.GetEntityWorld().GetEntityById(existing_link_id));

  if (existing_link->HasComponent<SensorComponent>()) {
    const std::vector<std::string> unsupported_components = {"SensorComponent"};
    return absl::InvalidArgumentError(absl::Substitute(
        "Cannot clone the object named \"$0\". The objects "
        "member entity \"$1\" has the following unsupported components: $2.",
        existing_object.GetName().value(),
        ObjectWorldResourceIdForEntity(existing_link_id).value(),
        absl::StrJoin(unsupported_components, ", ")));
  }
  return absl::OkStatus();
}

absl::Status VerifyObjectCanBeCloned(const WorldObject& existing_object) {
  if (existing_object.GetId() == RootObjectId()) {
    return absl::InvalidArgumentError("Cannot clone the root object");
  }

  INTR_ASSIGN_OR_RETURN(std::optional<std::string> resource_name,
                        existing_object.GetResourceName());
  if (resource_name) {
    return absl::InvalidArgumentError(
        absl::Substitute("Cannot clone the object named \"$0\" since it is an "
                         "instance of a resource (resource name: \"$1\").",
                         existing_object.GetName().value(), *resource_name));
  }

  const CollectionsEntityId existing_collection_id =
      *existing_object.GetCollectionEntity();
  INTR_ASSIGN_OR_RETURN(
      const WorldEntity* existing_collection,
      existing_object.GetEntityWorld().GetEntityById(existing_collection_id));

  if (existing_collection->HasComponent<RobotComponent>()) {
    return absl::InvalidArgumentError(
        absl::Substitute("Cannot clone the object named \"$0\". The objects "
                         "collection entity has an unsupported RobotComponent.",
                         existing_object.GetName().value()));
  }
  for (const AttachmentEntityId link_id : existing_object.GetEntityIds()) {
    INTR_RETURN_IF_ERROR(VerifyLinkCanBeCloned(existing_object, link_id));
  }

  return absl::OkStatus();
}

}  // namespace

// TODO: b/326691373 -- This function is currently busted for kinematic objects
// and probably needs some TLC.
absl::StatusOr<WorldObject*> WorldObject::CloneObject(
    const WorldObject& existing_object, const WorldObjectName& name,
    WorldObjectNameType name_type, AttachmentEntityId parent_entity_id,
    bool clone_frames, std::optional<Pose3d> parent_entity_t_new_object) {
  const absl::Time clone_time = absl::Now();
  INTR_RETURN_IF_ERROR(VerifyObjectCanBeCloned(existing_object));

  INTR_RETURN_IF_ERROR(CheckObjectNameIsAvailable(
      name, name_type == WorldObjectNameType::kNameIsGlobalAlias, *this,
      GetEntityWorld(), GetObjectWorldData()));

  const CollectionsEntityId existing_collection_id =
      *existing_object.GetCollectionEntity();
  INTR_ASSIGN_OR_RETURN(
      const WorldEntity* existing_collection,
      existing_object.GetEntityWorld().GetEntityById(existing_collection_id));

  // Create collection entities.
  INTR_ASSIGN_OR_RETURN(World * world, GetMutableEntityWorld());
  EntityId new_collection_id = world->CreateEntity();
  INTR_ASSIGN_OR_RETURN(WorldEntity * new_collection,
                        world->GetEntityById(new_collection_id));
  WorldHashMap<AttachmentEntityId, AttachmentEntityId> existing_to_new_link_ids;
  // First create entities with an invalid attachment component
  for (const AttachmentEntityId existing_link_id :
       existing_object.GetEntityIds()) {
    INTR_ASSIGN_OR_RETURN(
        const WorldEntity* existing_link,
        existing_object.GetEntityWorld().GetEntityById(existing_link_id));

    // Skip cloning frames -- they are handled below.
    if (IsFrameEntity(existing_link).value_or(true)) {
      continue;
    }

    EntityId new_link_id = world->CreateEntity();
    INTR_ASSIGN_OR_RETURN(WorldEntity * new_link,
                          world->GetEntityById(new_link_id));
    // Setup link entity.
    // Do NOT copy alias - it might be set if the existing object stores its
    // name in a legacy format.
    INTR_RETURN_IF_ERROR(new_link->SetLocalName(existing_link->GetLocalName()));
    INTR_RETURN_IF_ERROR(new_link->AddLabels(existing_link->GetLabels()));

    // Create attachment components attached to parent_entity_id first.
    // Do the fix up in the next loop.
    Pose3d new_parent_t_new_object;

    INTR_ASSIGN_OR_RETURN(
        AttachmentEntityId new_attachment_id,
        world->CreateAttachmentComponent(parent_entity_id, new_link_id,
                                         new_parent_t_new_object, clone_time));
    existing_to_new_link_ids.insert({existing_link_id, new_attachment_id});

    // Clone collision component and update all referenced entities accordingly.
    if (existing_link->HasComponent<CollisionComponent>()) {
      INTR_ASSIGN_OR_RETURN(const CollisionComponent* existing_collision,
                            existing_link->GetComponent<CollisionComponent>());
      INTR_RETURN_IF_ERROR(
          new_link->GetOrCreateComponent<CollisionComponent>().status());
      // Copy collision exclusions only if the cloned object is from the same
      // entity world
      if (&existing_object.GetEntityWorld() == &GetEntityWorld()) {
        for (const PhysicalEntityId& other_id :
             existing_collision->GetExclusions()) {
          INTR_RETURN_IF_ERROR(
              world->AddExclusionPair(PhysicalEntityId(new_link_id), other_id));
        }
      }
    }

    // Create a collections member component pointing to the collections entity.
    INTR_RETURN_IF_ERROR(InitializeCollectionsMemberComponentForObjectEntity(
        new_link_id, CollectionsEntityId(new_collection_id), world));

    // Clone other components.
    INTR_RETURN_IF_ERROR(
        CloneIfPresent<GeometryComponent>(existing_link, new_link));
    INTR_RETURN_IF_ERROR(
        CloneIfPresent<PhysicsComponent>(existing_link, new_link));
    INTR_RETURN_IF_ERROR(
        CloneIfPresent<UserDataComponent>(existing_link, new_link));
  }

  // Setup attachment and poses.
  for (const auto [existing_link_id, new_link_id] : existing_to_new_link_ids) {
    INTR_ASSIGN_OR_RETURN(
        const WorldEntity* existing_link,
        existing_object.GetEntityWorld().GetEntityById(existing_link_id));
    INTR_ASSIGN_OR_RETURN(const AttachmentComponent* existing_attachment,
                          existing_link->GetComponent<AttachmentComponent>());
    INTR_ASSIGN_OR_RETURN(WorldEntity * new_link,
                          world->GetEntityById(new_link_id));
    INTR_ASSIGN_OR_RETURN(AttachmentComponent * new_attachment,
                          new_link->GetComponent<AttachmentComponent>());
    if (const auto existing_and_new_parent_attachment_id =
            existing_to_new_link_ids.find(existing_attachment->GetParentId());
        existing_and_new_parent_attachment_id !=
        existing_to_new_link_ids.end()) {
      new_attachment->SetParentId(
          existing_and_new_parent_attachment_id->second);
      new_attachment->SetParentTThis(existing_attachment->GetParentTThis(),
                                     clone_time);
    } else {
      Pose3d new_parent_t_new_object;
      if (parent_entity_t_new_object.has_value()) {
        new_parent_t_new_object = *parent_entity_t_new_object;
      } else {
        // Use two GetTransform() calls since cloned object might not be in
        // the same world.
        const Pose3d root_t_parent =
            GetEntityWorld().GetTransform(kRootEntityId, parent_entity_id);
        const Pose3d root_t_link =
            existing_object.GetEntityWorld().GetTransform(kRootEntityId,
                                                          existing_link_id);
        new_parent_t_new_object = root_t_parent.inverse() * root_t_link;
      }
      new_attachment->SetParentTThis(new_parent_t_new_object, absl::Now());
      new_attachment->SetParentId(parent_entity_id);
    }
  }

  {  // Process the rule set updates for making a copy of an object.
    auto rule_set = world->GetDefaultRuleSet();

    // Add the new entity ids to the list ids for rules with the existing ids.
    for (auto& rule : *rule_set.mutable_rules()) {
      std::vector<EntityId> extra_ids_1;
      for (const auto& id : rule.id_1()) {
        for (const auto [existing_link_id, new_link_id] :
             existing_to_new_link_ids) {
          if (id == existing_link_id.value()) {
            extra_ids_1.push_back(new_link_id);
          }
        }
      }

      std::vector<EntityId> extra_ids_2;
      for (const auto& id : rule.id_2()) {
        for (const auto [existing_link_id, new_link_id] :
             existing_to_new_link_ids) {
          if (id == existing_link_id.value()) {
            extra_ids_2.push_back(new_link_id);
          }
        }
      }

      for (const auto& id : extra_ids_1) {
        rule.add_id_1(id.value());
      }
      for (const auto& id : extra_ids_2) {
        rule.add_id_2(id.value());
      }
    }

    // Split any rules that are many to all, so that it's always one to all.
    std::vector<intrinsic_proto::Rule> extra_rules;
    for (auto itr = rule_set.mutable_rules()->begin();
         itr != rule_set.mutable_rules()->end();) {
      // This is a {...} vs {} type of rule.
      if (itr->id_1_size() > 1 && itr->id_2_size() == 0) {
        // split and erase the current rule
        for (const auto& id : itr->id_1()) {
          intrinsic_proto::Rule rule_copy = *itr;
          rule_copy.clear_id_1();
          rule_copy.add_id_1(id);
          extra_rules.push_back(std::move(rule_copy));
        }

        itr = rule_set.mutable_rules()->erase(itr);
        continue;
      }

      // This is a {} vs {...} type of rule.
      if (itr->id_1_size() == 0 && itr->id_2_size() > 1) {
        // split and erase the current rule
        for (const auto& id : itr->id_2()) {
          intrinsic_proto::Rule rule_copy = *itr;
          rule_copy.clear_id_2();
          rule_copy.add_id_2(id);
          extra_rules.push_back(std::move(rule_copy));
        }

        itr = rule_set.mutable_rules()->erase(itr);
        continue;
      }

      // This rule is not interesting and so we will just keep it as is.
      itr++;
    }

    rule_set.mutable_rules()->Reserve(rule_set.rules_size() +
                                      extra_rules.size());
    for (auto&& rule : extra_rules) {
      *rule_set.add_rules() = std::move(rule);
    }

    INTR_RETURN_IF_ERROR(world->SetDefaultRuleSet(rule_set));
  }

  // Setup collections entity.
  auto new_link_ids = std::views::values(existing_to_new_link_ids);
  INTR_RETURN_IF_ERROR(InitializeCollectionsEntityForObject(
      new_collection_id, name, name_type,
      {new_link_ids.begin(), new_link_ids.end()}, world));

  // Clone other components.
  INTR_RETURN_IF_ERROR(
      CloneIfPresent<PPRComponent>(existing_collection, new_collection));
  INTR_RETURN_IF_ERROR(
      CloneIfPresent<SimulationComponent>(existing_collection, new_collection));
  INTR_RETURN_IF_ERROR(
      CloneIfPresent<UserDataComponent>(existing_collection, new_collection));

  // Create a physical object.
  ObjectWorldResourceId object_id =
      ObjectWorldResourceIdForObject(CollectionsEntityId(new_collection_id));
  auto new_object = std::make_unique<PhysicalObject>(
      object_id, name, CollectionsEntityId(new_collection_id),
      WorldHashSet<AttachmentEntityId>{new_link_ids.begin(),
                                       new_link_ids.end()},
      GetObjectWorldData());
  WorldObject* new_object_raw = new_object.get();

  new_object->SetParentAsymmetric(this);
  this->AddChildAsymmetric(new_object_raw);

  // Store physical object.
  INTR_RETURN_IF_ERROR(
      GetObjectWorldData().InsertObject(std::move(new_object)));

  // Clone frames if requested.
  if (clone_frames) {
    WorldHashMap<const Frame*, Frame*> existing_to_new_frames;

    for (const Frame* existing_frame :
         existing_object.GetFramesPartiallySorted()) {
      INTR_ASSIGN_OR_RETURN(const AttachmentComponent* existing_attachment,
                            existing_object.GetEntityWorld()
                                .GetComponentByEntityId<AttachmentComponent>(
                                    existing_frame->GetEntityId()));
      const Pose3d parent_entity_t_new_frame =
          existing_attachment->GetParentTThis();

      if (existing_frame->GetParentFrame()) {
        // Clone frame attached to another frame.

        // Works because we are iterating in partially sorted order and parents
        // are created before their children.
        Frame* new_parent_frame =
            existing_to_new_frames[existing_frame->GetParentFrame()];

        INTR_ASSIGN_OR_RETURN(
            existing_to_new_frames[existing_frame],
            new_object_raw->CreateEntityAndFrame(
                existing_frame->GetName(),
                /*parent_entity_id=*/new_parent_frame->GetEntityId(),
                new_parent_frame, parent_entity_t_new_frame));
      } else {
        // Clone frame attached to an entity of the object.
        AttachmentEntityId existing_parent_entity_id =
            existing_attachment->GetParentId();
        auto new_parent_entity_id =
            existing_to_new_link_ids.find(existing_parent_entity_id);
        if (new_parent_entity_id == existing_to_new_link_ids.end()) {
          return absl::InternalError(absl::Substitute(
              "Cannot find the matching parent entity id when cloning $0",
              existing_frame->GetName().value()));
        }
        INTR_ASSIGN_OR_RETURN(
            existing_to_new_frames[existing_frame],
            new_object_raw->CreateEntityAndFrame(
                existing_frame->GetName(),
                /*parent_entity_id=*/new_parent_entity_id->second,
                /*parent_frame=*/nullptr, parent_entity_t_new_frame));
      }
    }
  }

  return new_object_raw;
}

absl::StatusOr<Frame*> WorldObject::CreateEntityAndFrame(
    const FrameName& name, AttachmentEntityId parent_entity_id,
    Frame* parent_frame, const Pose3d& parent_entity_t_new_frame) {
  const absl::Time create_time = absl::Now();
  INTR_RETURN_IF_ERROR(CheckFrameNameIsAvailable(name)).SetPrepend()
      << absl::Substitute(
             "Cannot create a frame named \"$0\" under object \"$1\". ",
             name.value(), GetName().value());

  INTR_ASSIGN_OR_RETURN(World * world, GetMutableEntityWorld());
  EntityId id = world->CreateEntity();
  INTR_ASSIGN_OR_RETURN(WorldEntity * entity, world->GetEntityById(id));
  INTR_RETURN_IF_ERROR(entity->SetLocalName(name.value()));
  INTR_ASSIGN_OR_RETURN(
      AttachmentEntityId attachment_id,
      world->CreateAttachmentComponent(parent_entity_id, id,
                                       parent_entity_t_new_frame, create_time));

  ObjectWorldResourceId frame_id = ObjectWorldResourceIdForFrame(attachment_id);
  auto frame = std::make_unique<Frame>(frame_id, name, this, parent_frame,
                                       attachment_id, GetObjectWorldData());
  Frame* frame_raw = frame.get();

  if (parent_frame) {
    parent_frame->AddChildFrameAsymmetric(frame_raw);
  }

  INTR_RETURN_IF_ERROR(AddFrameAsymmetric(std::move(frame)));
  return frame_raw;
}

absl::Status WorldObject::AddFrameAsymmetric(std::unique_ptr<Frame> frame) {
  if (frames_by_id_.contains(frame->GetId())) {
    return absl::InternalError(absl::Substitute(
        "Frame \"$0\" could not be added to object \"$1\" since it already has "
        "a frame with id \"$2\".",
        frame->GetName().value(), GetName().value(), frame->GetId().value()));
  }
  for (const auto& [_, existing_frame] : frames_by_id_) {
    if (existing_frame->GetName() == frame->GetName()) {
      return absl::InternalError(absl::Substitute(
          "Frame \"$0\" could not be added to object \"$1\" since it already "
          "has a frame with that name.",
          frame->GetName().value(), GetName().value()));
    }
  }

  // Assign the newly created frame as a coordinate and optionally attachment
  // frame of the parent collections component. If this frame is already in the
  // entities list, then it was added as a "virtual" frame (e.g. a camera
  // sensor), so we should avoid adding it.
  std::optional<CollectionsEntityId> parent_collection_id =
      GetCollectionEntity();
  AttachmentEntityId frame_id = frame->GetEntityId();
  if (parent_collection_id.has_value() && !entity_ids_.contains(frame_id)) {
    INTR_ASSIGN_OR_RETURN(const WorldEntity* entity,
                          GetEntityWorld().GetEntityById(frame_id));
    absl::StatusOr<const CollectionsMemberComponent*>
        collections_member_component =
            entity->GetComponent<CollectionsMemberComponent>();
    const bool is_coordinate_frame =
        collections_member_component.ok() &&
        (*collections_member_component)
            ->IsMemberOfCollection(*parent_collection_id,
                                   CollectionsComponent::kCoordinateFrames);

    // The frame may be getting added as part of a view on top of an existing
    // world that has the coordinate frame in place already, so we shouldn't
    // add it if it's already there.
    if (!is_coordinate_frame) {
      INTR_ASSIGN_OR_RETURN(World * world, GetMutableEntityWorld(),
                            _ << "Attempting to add frame '" << frame->GetName()
                              << "' to object '" << GetName() << "'");

      INTR_ASSIGN_OR_RETURN(WorldEntity * mutable_entity,
                            world->GetEntityById(frame_id));
      INTR_ASSIGN_OR_RETURN(
          auto mutable_collections_member_component,
          mutable_entity->GetOrCreateComponent<CollectionsMemberComponent>());
      INTR_RETURN_IF_ERROR(
          mutable_collections_member_component->AddParentCollection(
              *parent_collection_id, CollectionsComponent::kCoordinateFrames));

      // Update the list of members in the parent collection.
      INTR_ASSIGN_OR_RETURN(auto parent_collection,
                            world->GetComponentByEntityId<CollectionsComponent>(
                                *parent_collection_id));
      auto members = parent_collection->GetCollectionMembers(
          CollectionsComponent::kCoordinateFrames);
      members.emplace_back(frame_id.value());
      INTR_RETURN_IF_ERROR(parent_collection->SetCollectionMembers(
          CollectionsComponent::kCoordinateFrames, members));
    }

    // Also add the frame as an attachment frame if it was an attachment frame
    // in the original object.
    if (parent_collection_id.has_value() && frame->IsAttachmentFrame()) {
      INTR_ASSIGN_OR_RETURN(World * world, GetMutableEntityWorld(),
                            _ << "Attempting to add frame '" << frame->GetName()
                              << "' to object '" << GetName() << "'");

      INTR_ASSIGN_OR_RETURN(WorldEntity * mutable_entity,
                            world->GetEntityById(frame_id));
      INTR_ASSIGN_OR_RETURN(
          auto mutable_collections_member_component,
          mutable_entity->GetOrCreateComponent<CollectionsMemberComponent>());
      INTR_RETURN_IF_ERROR(
          mutable_collections_member_component->AddParentCollection(
              *parent_collection_id, CollectionsComponent::kAttachmentFrames));
    }
  }

  RegisterFrameEntity(frame_id);
  frames_by_id_.insert({frame->GetId(), std::move(frame)});
  return absl::OkStatus();
}

absl::StatusOr<std::unique_ptr<Frame>> WorldObject::RemoveFrameAsymmetric(
    const Frame& frame) {
  if (!frames_by_id_.contains(frame.GetId())) {
    return absl::InternalError(
        absl::Substitute("Frame \"$0\" could not be removed from object \"$1\" "
                         "since it was not found.",
                         frame.GetName().value(), GetName().value()));
  }

  // If frame is movable that means that it's not a "virtual" frame that's
  // mirroring some other entity (like a sensor).
  AttachmentEntityId frame_id = frame.GetEntityId();
  if (frame.CheckIsMovable().ok()) {
    UnregisterFrameEntity(frame_id);

    // Remove the frame from the list of coordinate frames of the parent
    // collections component.
    std::optional<CollectionsEntityId> parent_collection_id =
        GetCollectionEntity();
    if (parent_collection_id.has_value()) {
      INTR_ASSIGN_OR_RETURN(World * world, GetMutableEntityWorld());
      INTR_ASSIGN_OR_RETURN(WorldEntity * entity,
                            world->GetEntityById(frame_id));

      // Note -- we don't use GetOrCreate here - since this object has a parent
      // collection, we expect the frame to have been added to it at some point.
      INTR_ASSIGN_OR_RETURN(auto collections_member_component,
                            entity->GetComponent<CollectionsMemberComponent>());

      using CollectionType =
          intrinsic_proto::world::CollectionsComponent::CollectionType;
      INTR_ASSIGN_OR_RETURN(
          const WorldHashSet<CollectionType>* frame_collection_types,
          collections_member_component->GetCollectionTypesByParentId(
              *parent_collection_id));
      std::vector<CollectionType> collection_types(
          frame_collection_types->begin(), frame_collection_types->end());

      for (const CollectionType type : collection_types) {
        INTR_RETURN_IF_ERROR(
            collections_member_component->DeleteParentCollection(
                *parent_collection_id, type));
      }

      INTR_ASSIGN_OR_RETURN(auto parent_collection,
                            world->GetComponentByEntityId<CollectionsComponent>(
                                *parent_collection_id));
      INTR_ASSIGN_OR_RETURN(
          CollectionsMemberEntityId frame_member,
          GetEntityWorld().ValidateEntity<CollectionsMemberEntityId>(frame_id));

      for (const CollectionType type : collection_types) {
        std::vector<CollectionsMemberEntityId> members_without_frame;
        for (CollectionsMemberEntityId entity_member :
             parent_collection->GetCollectionMembers(type)) {
          if (entity_member != frame_member) {
            members_without_frame.push_back(entity_member);
          }
        }
        INTR_RETURN_IF_ERROR(parent_collection->SetCollectionMembers(
            type, members_without_frame));
      }

      // If there's no remaining collection parents of this frame, e.g. it was
      // reparented to root, then we should delete the collections member
      // component.
      if (collections_member_component->GetParentCollectionsIdToTypesMap()
              .empty()) {
        INTR_RETURN_IF_ERROR(
            entity->RemoveComponent<CollectionsMemberComponent>());
      }
    }
  }

  return std::move(frames_by_id_.extract(frame.GetId()).mapped());
}

absl::StatusOr<AttachmentEntityId> WorldObject::GetRootEntityId() const {
  if (root_entity_id_ != kInvalidEntityId) {
    return root_entity_id_;
  }
  // This cannot fail by construction (see ObjectWorld::CreateView).
  return GetEntityWorld().GetRootEntity(
      {entity_ids_.begin(), entity_ids_.end()});
}

absl::StatusOr<AttachmentEntityId> WorldObject::GetTransformOriginEntityId()
    const {
  return GetRootEntityId();
}

absl::StatusOr<AttachmentEntityId> WorldObject::ResolveSingleEntityIdFromFilter(
    const world::ObjectEntityFilter& filter) const {
  if (FilterMatchesOnlyBaseEntity(filter)) {
    return GetRootEntityId();
  }
  if (!filter.IncludesAllEntities() && !filter.IncludesBaseEntity() &&
      !filter.IncludesFinalEntity() && filter.EntityIds().empty() &&
      filter.EntityNames().size() == 1) {
    const auto it =
        entity_ids_by_local_name_.find(*filter.EntityNames().begin());
    if (it != entity_ids_by_local_name_.end()) {
      if (it->second.size() == 1) {
        return it->second.front();
      }
      if (it->second.size() > 1) {
        return absl::InvalidArgumentError(
            "Found more than one entity matching given filter.");
      }
    }
  }
  INTR_ASSIGN_OR_RETURN(
      const WorldHashSet<AttachmentEntityId> entities,
      object_world_object_entity_filter_details::
          GetObjectEntitiesMatchingEntityFilter(*this, filter,
                                                /*expanded_list=*/false));
  if (entities.empty()) {
    return absl::NotFoundError("No matching entities found for given filter");
  }
  if (entities.size() != 1) {
    return absl::InvalidArgumentError(
        "Found more than one entity matching given filter.");
  }
  return *entities.begin();
}

absl::StatusOr<AttachmentEntityId> WorldObject::GetTransformEntityId(
    const world::ObjectEntityFilter& filter) const {
  return ResolveSingleEntityIdFromFilter(filter);
}

absl::StatusOr<bool> WorldObject::IsCollisionExcluded(
    const EntityId& entity_id) const {
  INTR_ASSIGN_OR_RETURN(const WorldEntity* entity,
                        GetEntityWorld().GetEntityById(entity_id));
  if (!entity->HasComponent<CollisionComponent>()) {
    return false;
  }
  const auto* collision_component =
      entity->GetComponent<CollisionComponent>().value();
  for (const auto& object_entity_id : entity_ids_) {
    const absl::StatusOr<PhysicalEntityId> physical_entity_id =
        GetEntityWorld().ValidateEntity<PhysicalEntityId>(object_entity_id);
    if (!physical_entity_id.ok()) {
      continue;
    }
    if (!collision_component->GetExclusions().contains(*physical_entity_id)) {
      return false;
    }
  }
  return true;
}

absl::StatusOr<bool> WorldObject::IsCollisionExcluded(
    const WorldObject& other_object) const {
  for (const auto& entity_id : entity_ids_) {
    INTR_ASSIGN_OR_RETURN(const auto* entity,
                          GetEntityWorld().GetEntityById(entity_id));
    if (!entity->HasComponent<CollisionComponent>()) {
      continue;
    }
    INTR_ASSIGN_OR_RETURN(const bool is_excluded,
                          other_object.IsCollisionExcluded(entity_id));
    if (!is_excluded) return false;
  }
  return true;
}

absl::Status WorldObject::EnableCollisionsWith(
    WorldObject& other, const world::ObjectEntityFilter& this_entity_filter,
    const world::ObjectEntityFilter& other_entity_filter) {
  return ToggleCollisionsWith(other, /*enable_collisions=*/true,
                              this_entity_filter, other_entity_filter);
}

absl::Status WorldObject::DisableCollisionsWith(
    WorldObject& other, const world::ObjectEntityFilter& this_entity_filter,
    const world::ObjectEntityFilter& other_entity_filter) {
  return ToggleCollisionsWith(other, /*enable_collisions=*/false,
                              this_entity_filter, other_entity_filter);
}

absl::Status WorldObject::CheckObjectNameAgainstChildrenObjectAndFrameNames(
    const WorldObjectName& new_child_name,
    const WorldObject* exclude_object) const {
  for (const WorldObject* child_object : GetChildren()) {
    if (exclude_object == child_object) continue;
    if (child_object->GetName().value() == new_child_name.value()) {
      return absl::InvalidArgumentError(
          absl::Substitute("New object name \"$0\" cannot be used here since "
                           "there is already an "
                           "object with that name under object \"$1\"",
                           new_child_name.value(), GetName().value()));
    }
  }

  for (const Frame* child_frame : GetChildFrames()) {
    if (child_frame->GetName().value() == new_child_name.value()) {
      return absl::InvalidArgumentError(absl::Substitute(
          "New object name \"$0\" cannot be used because there is already a "
          "frame with that name under object \"$1\"",
          new_child_name.value(), GetName().value()));
    }
  }
  return absl::OkStatus();
}

absl::StatusOr<WorldHashSet<AttachmentEntityId>> WorldObject::FinalEntities()
    const {
  auto collection_entity_id = GetCollectionEntity();
  if (!collection_entity_id.has_value()) {
    return absl::InvalidArgumentError(
        "Object does not have an associated collection");
  }
  return GetEntityWorld().GetFinalEntitiesOfRobot(*collection_entity_id);
}

namespace object_world_object_entity_filter_details {
namespace {

absl::Status MatchEntityNamesInFilter(
    const WorldObject& object, const World& world,
    const WorldHashSet<AttachmentEntityId>& all_object_entities,
    const world::ObjectEntityFilter& entity_filter,
    WorldHashSet<AttachmentEntityId>& result) {
  bool all_matched = false;
  if (entity_filter.EntityNames().size() == 1) {
    const std::string& expected_name = *entity_filter.EntityNames().begin();
    for (const AttachmentEntityId entity_id : all_object_entities) {
      INTR_ASSIGN_OR_RETURN(const WorldEntity* entity,
                            world.GetEntityById(entity_id));
      if (entity->GetLocalName() == expected_name) {
        result.insert(entity_id);
        all_matched = true;
      }
    }
  } else {
    absl::flat_hash_set<std::string_view> matched_expected_names;
    for (const AttachmentEntityId entity_id : all_object_entities) {
      INTR_ASSIGN_OR_RETURN(const WorldEntity* entity,
                            world.GetEntityById(entity_id));
      const auto it = entity_filter.EntityNames().find(entity->GetLocalName());
      if (it != entity_filter.EntityNames().end()) {
        result.insert(entity_id);
        matched_expected_names.insert(*it);
      }
    }
    all_matched =
        (matched_expected_names.size() == entity_filter.EntityNames().size());
  }

  if (!all_matched) {
    WorldHashSet<std::string> existing_entity_names;
    for (const AttachmentEntityId entity_id : all_object_entities) {
      INTR_ASSIGN_OR_RETURN(const WorldEntity* entity,
                            world.GetEntityById(entity_id));
      existing_entity_names.insert(entity->GetLocalName());
    }

    for (const std::string& expected_name : entity_filter.EntityNames()) {
      if (!existing_entity_names.contains(expected_name)) {
        return absl::NotFoundError(absl::StrCat(
            "Entity name \"", expected_name, "\" not found in object \"",
            object.GetName().value(), "\" which has entities [",
            absl::StrJoin(existing_entity_names, ", "), "]"));
      }
    }
  }
  return absl::OkStatus();
}

}  // namespace

absl::StatusOr<WorldHashSet<AttachmentEntityId>>
GetObjectEntitiesMatchingEntityFilter(
    const WorldObject& object, const world::ObjectEntityFilter& entity_filter,
    bool expanded_list) {
  WorldHashSet<AttachmentEntityId> result;

  // If no filter was set, or if only the base entity was requested without
  // expansion, just return the base entity.
  if (!expanded_list && FilterMatchesOnlyBaseEntity(entity_filter)) {
    INTR_ASSIGN_OR_RETURN(AttachmentEntityId object_root_id,
                          object.GetRootEntityId());
    result.insert(object_root_id);
    return result;
  }

  const World& world = object.GetEntityWorld();

  WorldHashSet<AttachmentEntityId> all_object_entities = object.GetEntityIds();

  // Ensure that attachment frames are included when doing the filter search.
  // These are normally not present in the list of entities of an object as they
  // are represented by their own frame object.
  if (object.GetCollectionEntity().has_value()) {
    INTR_ASSIGN_OR_RETURN(auto collection_members,
                          world.FilterCollectionMembers<AttachmentEntityId>(
                              object.GetCollectionEntity().value()));
    all_object_entities.insert(collection_members.begin(),
                               collection_members.end());
  }

  if (entity_filter.IncludesAllEntities()) {
    result = std::move(all_object_entities);
    return result;
  }

  if (entity_filter.IncludesBaseEntity()) {
    INTR_ASSIGN_OR_RETURN(AttachmentEntityId object_root_id,
                          object.GetRootEntityId());
    if (expanded_list) {
      // Search downwards from the object's root entity.
      INTR_ASSIGN_OR_RETURN(
          WorldHashSet<AttachmentEntityId> rigidly_attached_to_object_root,
          GetRigidlyAttachedChildrenEntities(world, object_root_id));
      // Remove entities that are not part of the object.
      absl::erase_if(rigidly_attached_to_object_root,
                     [&](const AttachmentEntityId& id) {
                       return !all_object_entities.contains(id);
                     });
      result.insert(rigidly_attached_to_object_root.begin(),
                    rigidly_attached_to_object_root.end());
    } else {
      result.insert(object_root_id);
    }
  }

  if (entity_filter.IncludesFinalEntity()) {
    INTR_ASSIGN_OR_RETURN(WorldHashSet<AttachmentEntityId> final_link_ids,
                          object.FinalEntities());
    if (expanded_list) {
      for (const AttachmentEntityId& final_link_id : final_link_ids) {
        // Search upwards and downwards since the final entity in a kinematic
        // object is not necessarily the root entity of the final link.
        INTR_ASSIGN_OR_RETURN(
            WorldHashSet<AttachmentEntityId> rigidly_attached_to_final_link,
            GetRigidlyAttachedEntities(world, final_link_id));
        // Remove entities that are not part of the object.
        absl::erase_if(rigidly_attached_to_final_link,
                       [&](const AttachmentEntityId& id) {
                         return !all_object_entities.contains(id);
                       });
        result.insert(rigidly_attached_to_final_link.begin(),
                      rigidly_attached_to_final_link.end());
      }
    } else {
      result.insert(final_link_ids.begin(), final_link_ids.end());
    }
  }

  for (const auto& entity_reference : entity_filter.EntityIds()) {
    INTR_ASSIGN_OR_RETURN(EntityId entity_id,
                          ObjectWorldResourceIdToEntityId(entity_reference));
    if (all_object_entities.contains(AttachmentEntityId(entity_id.value()))) {
      result.emplace(entity_id.value());
    } else {
      return absl::NotFoundError(absl::StrCat(
          "Entity ", entity_reference.value(), " not found in object \"",
          object.GetName().value() + "\""));
    }
  }

  if (!entity_filter.EntityNames().empty()) {
    INTR_RETURN_IF_ERROR(MatchEntityNamesInFilter(
        object, world, all_object_entities, entity_filter, result));
  }

  return result;
}

absl::StatusOr<WorldHashSet<CollisionEntityId>>
GetObjectCollisionEntitiesMatchingEntityFilter(
    const WorldObject& object, const world::ObjectEntityFilter& entity_filter) {
  // If the filter is unspecified, then include all entities for collision.
  world::ObjectEntityFilter filter = entity_filter;
  if (filter == world::ObjectEntityFilter()) {
    filter.IncludeAllEntities();
  }

  INTR_ASSIGN_OR_RETURN(
      WorldHashSet<AttachmentEntityId> attachment_ids,
      GetObjectEntitiesMatchingEntityFilter(object, filter,
                                            /*expanded_list=*/true));
  return FilterToCollisionEntityIds(attachment_ids, object.GetEntityWorld());
}

}  // namespace object_world_object_entity_filter_details

absl::Status WorldObject::CheckIsMovable(
    const std::optional<world::ObjectEntityFilter>& filter) const {
  if (!filter.has_value()) {
    return absl::OkStatus();
  }

  INTR_ASSIGN_OR_RETURN(const AttachmentEntityId entity_to_move_id,
                        ResolveSingleEntityIdFromFilter(*filter),
                        _ << "Cannot resolve entity of an object to move.");

  INTR_ASSIGN_OR_RETURN(AttachmentEntityId root_entity_id, GetRootEntityId());
  if (entity_to_move_id == root_entity_id) {
    return absl::OkStatus();
  }

  for (const Frame* frame : GetFrames()) {
    if (frame->GetEntityId() == entity_to_move_id) {
      return frame->CheckIsMovable();
    }
  }

  INTR_ASSIGN_OR_RETURN(const WorldEntity* entity,
                        GetEntityWorld().GetEntityById(entity_to_move_id));
  return absl::InvalidArgumentError(
      absl::StrCat("Entity \"", entity->GetLocalName(),
                   "\" is not a frame, and therefore cannot be moved."));
}

absl::StatusOr<std::unique_ptr<Frame>>
WorldObject::CreateSensorFrameIfObjectHasSensorEntity() {
  INTR_ASSIGN_OR_RETURN(
      std::vector<AttachmentEntityId> found_sensor_ids,
      FindSensorEntities(GetObjectWorldData().GetEntityWorld(), GetEntityIds(),
                         GetCollectionEntity()));
  if (found_sensor_ids.empty()) {
    // Object does not have a sensor entity.
    return nullptr;
  }

  // TODO(b/291793346) A collection entity can contain multiple sensor
  // entities, e.g. plenoptic_unit device has multiple camera sensors.
  // For now, create frame for one sensor entity.
  std::sort(found_sensor_ids.begin(), found_sensor_ids.end());
  auto min_sensor_id_it = absl::c_find_if(
      found_sensor_ids, [&](const AttachmentEntityId& sensor_id) {
        absl::StatusOr<const WorldEntity*> sensor =
            GetObjectWorldData().GetEntityWorld().GetEntityById(sensor_id);
        if (!sensor.ok()) return false;
        return (*sensor)->GetAlias().empty();
      });
  if (min_sensor_id_it == found_sensor_ids.end()) {
    return nullptr;
  }

  AttachmentEntityId found_sensor_id = *min_sensor_id_it;

  ObjectWorldResourceId frame_id =
      ObjectWorldResourceIdForFrame(found_sensor_id);
  return std::make_unique<Frame>(frame_id, SensorFrameName(), this,
                                 /*parent_frame=*/nullptr, found_sensor_id,
                                 GetObjectWorldData());
}

absl::StatusOr<std::unique_ptr<Frame>> WorldObject::CreateFrameFromEntity(
    AttachmentEntityId candidate_id,
    const WorldHashMap<ObjectWorldResourceId, std::unique_ptr<Frame>>& frames,
    const WorldHashMap<FrameName, AttachmentEntityId>& found_frame_entities,
    bool candidate_is_collection_member,
    std::vector<std::string>* non_critical_errors) {
  INTR_ASSIGN_OR_RETURN(
      const WorldEntity* entity,
      GetObjectWorldData().GetEntityWorld().GetEntityById(candidate_id));
  INTR_ASSIGN_OR_RETURN(
      const AttachmentComponent* attachment,
      GetObjectWorldData()
          .GetEntityWorld()
          .GetComponentByEntityId<AttachmentComponent>(candidate_id));

  FrameName frame_name;
  if (entity->GetLocalName().empty()) {
    INTR_ASSIGN_OR_RETURN(
        std::string cand_desc,
        DescribeEntityForError(GetObjectWorldData().GetEntityWorld(),
                               candidate_id));
    if (non_critical_errors != nullptr) {
      non_critical_errors->push_back(
          absl::Substitute("Entity ($0) must have a local name which will be "
                           "used as frame name.",
                           cand_desc));
    }
    // Proceed with a random, unique name so that we can collect more
    // potential errors.
    frame_name = FrameName(absl::StrCat(
        "frame_",
        absl::Uniform<uint64_t>(absl::BitGen())));  // NOLINT (cl/653534410)
  } else {
    frame_name = FrameName(entity->GetLocalName());
  }

  // If the frame is a global frame, check that its name will not collide
  // with an existing global object name.
  if (GetId() == RootObjectId()) {
    INTR_RETURN_IF_ERROR(
        GetObjectWorldData().CheckGlobalFrameNameAgainstGlobalObjectNames(
            frame_name));
  }

  INTR_RETURN_IF_ERROR(CheckNameIsCompatibleWithObjectView(frame_name.value()));

  ObjectWorldResourceId frame_id = ObjectWorldResourceIdForFrame(candidate_id);

  if (found_frame_entities.contains(frame_name)) {
    INTR_ASSIGN_OR_RETURN(
        std::string first_desc,
        DescribeEntityForError(GetObjectWorldData().GetEntityWorld(),
                               found_frame_entities.at(frame_name)));
    INTR_ASSIGN_OR_RETURN(
        std::string second_desc,
        DescribeEntityForError(GetObjectWorldData().GetEntityWorld(),
                               candidate_id));
    if (non_critical_errors != nullptr) {
      non_critical_errors->push_back(
          absl::StrCat("All frames under one object (or under "
                       "root) need to have a unique name. Failed using the "
                       "local_name of entity (",
                       first_desc, ") and the local_name of entity (",
                       second_desc, ") since they are equal."));
    }
    // Proceed with a random, unique name so that we can collect more
    // potential errors.
    frame_name = FrameName(absl::StrCat(
        entity->GetLocalName(),
        absl::Uniform<uint64_t>(absl::BitGen())));  // NOLINT (cl/653534410)
  }

  Frame* parent_frame = nullptr;
  if (attachment->GetParentId() != kRootEntityId) {
    ObjectWorldResourceId parent_id =
        ObjectWorldResourceIdForFrame(attachment->GetParentId());
    if (frames.contains(parent_id)) {
      parent_frame = frames.at(parent_id).get();
    }
  }

  return std::make_unique<Frame>(frame_id, frame_name, this, parent_frame,
                                 candidate_id, GetObjectWorldData());
}

absl::Status WorldObject::FindAndAddFrames(
    std::vector<std::string>* absl_nullable non_critical_errors,
    const AttachmentGraph* absl_nullable attachment_graph) {
  WorldHashMap<ObjectWorldResourceId, std::unique_ptr<Frame>> frames;
  WorldHashMap<FrameName, AttachmentEntityId> found_frame_entities;

  INTR_ASSIGN_OR_RETURN(
      std::vector<AttachmentEntityId> candidate_ids,
      GetChildFrameEntitiesRecursively(GetObjectWorldData().GetEntityWorld(),
                                       GetEntityIds(), attachment_graph));

  // First search for a frame named 'sensor', which will be our sensor frame.
  auto sensor_frame_it = absl::c_find_if(
      candidate_ids, [&](const AttachmentEntityId& candidate_id) {
        return GetObjectWorldData().GetEntityWorld().GetLocalNameForEntityById(
                   candidate_id) == SensorFrameName().value();
      });

  // If there is no frame named sensor, then find some entity with a sensor
  // component, and add its frame here. Otherwise, the sensor frame will be
  // created like any other frame.
  if (sensor_frame_it == candidate_ids.end()) {
    INTR_ASSIGN_OR_RETURN(std::unique_ptr<Frame> sensor_frame,
                          CreateSensorFrameIfObjectHasSensorEntity());
    if (sensor_frame != nullptr) {
      found_frame_entities.emplace(sensor_frame->GetName(),
                                   sensor_frame->GetEntityId());
      frames.emplace(sensor_frame->GetId(), std::move(sensor_frame));
    }
  }

  // Create regular frames from any frame entities that are children.
  for (AttachmentEntityId candidate_id : candidate_ids) {
    INTR_ASSIGN_OR_RETURN(
        std::unique_ptr<Frame> frame,
        CreateFrameFromEntity(candidate_id, frames, found_frame_entities,
                              /*candidate_is_collection_member=*/false,
                              non_critical_errors));
    found_frame_entities.emplace(frame->GetName(), frame->GetEntityId());
    frames.emplace(frame->GetId(), std::move(frame));
  }

  // TODO(b/389985415): Remove old world compatibility code.
  // Create additional frames from non-root entities in the object's entity
  // collection if they have an alias. This is for compatibility with older
  // worlds so that we can map entities with aliases reliably to
  // corresponding frames (or objects). The creation of such a frame will
  // trigger a warning.
  INTR_ASSIGN_OR_RETURN(AttachmentEntityId root_id, GetRootEntityId());
  for (const AttachmentEntityId& candidate_id : GetEntityIds()) {
    if (candidate_id == root_id) continue;

    // Skip frame entities that we already found.
    if (absl::c_any_of(found_frame_entities, [candidate_id](const auto& kv) {
          return kv.second == candidate_id;
        })) {
      continue;
    }

    INTR_ASSIGN_OR_RETURN(
        const WorldEntity* entity,
        GetObjectWorldData().GetEntityWorld().GetEntityById(candidate_id));
    if (entity->GetAlias().empty()) {
      continue;
    }

    INTR_ASSIGN_OR_RETURN(
        std::unique_ptr<Frame> frame,
        CreateFrameFromEntity(candidate_id, frames, found_frame_entities,
                              /*candidate_is_collection_member=*/true,
                              non_critical_errors));

    found_frame_entities.emplace(frame->GetName(), frame->GetEntityId());
    frames.emplace(frame->GetId(), std::move(frame));
  }

  // Setup the frame parentage properly.
  INTR_RETURN_IF_ERROR(SetFrames(std::move(frames)));

  for (Frame* frame : GetFrames()) {
    if (frame->GetParentFrame()) {
      frame->GetParentFrame()->AddChildFrameAsymmetric(frame);
    }
  }

  return absl::OkStatus();
}

}  // namespace object_world
}  // namespace intrinsic
