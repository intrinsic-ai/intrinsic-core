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

#include "intrinsic/world/world.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <iterator>
#include <memory>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/container/btree_set.h"
#include "absl/log/check.h"
#include "absl/log/die_if_null.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "absl/strings/substitute.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "geometry_msgs/msg/transform_stamped.hpp"
#include "google/protobuf/io/coded_stream.h"
#include "google/protobuf/io/zero_copy_stream_impl_lite.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/geometry/api/affine_transform_of.h"
#include "intrinsic/geometry/api/affine_transform_of_geometry.h"
#include "intrinsic/geometry/api/exact_geometry.h"
#include "intrinsic/geometry/api/fuse_geometries.h"
#include "intrinsic/geometry/api/geometry.h"
#include "intrinsic/geometry/api/geometry_options.h"
#include "intrinsic/geometry/shapes/point_cloud.h"
#include "intrinsic/geometry/storage/dummy_storage.h"
#include "intrinsic/geometry/storage/geometry_deserializer.h"
#include "intrinsic/geometry/storage/geometry_serializer.h"
#include "intrinsic/geometry/storage/gzf_storage.h"
#include "intrinsic/kinematics/skeleton.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/proto/header.pb.h"
#include "intrinsic/math/proto/quaternion.pb.h"
#include "intrinsic/math/proto/tf_message.pb.h"
#include "intrinsic/math/proto/transform.pb.h"
#include "intrinsic/math/proto/transform_stamped.pb.h"
#include "intrinsic/math/proto/vector3.pb.h"
#include "intrinsic/math/proto_conversion.h"
#include "intrinsic/math/tf2_convert_intrinsic.h"
#include "intrinsic/util/aggregate_type.h"
#include "intrinsic/util/macros.h"
#include "intrinsic/util/object_store/object_ref.h"
#include "intrinsic/util/proto_time.h"
#include "intrinsic/util/status/status_builder.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/aspects/chained_robot_cartesian_kinematic_view.h"
#include "intrinsic/world/aspects/entity_dof_kinematic_view.h"
#include "intrinsic/world/aspects/entity_grouping.h"
#include "intrinsic/world/aspects/entity_robot_cartesian_kinematic_view.h"
#include "intrinsic/world/aspects/entity_world_interface.h"
#include "intrinsic/world/cartesian_kinematic_view.h"
#include "intrinsic/world/collision/coal_collision_checker.h"
#include "intrinsic/world/collision/collision_checker.h"
#include "intrinsic/world/collision/collision_checker_utils.h"
#include "intrinsic/world/collision/collision_context.pb.h"
#include "intrinsic/world/collision/rule_matching.h"
#include "intrinsic/world/collision/util/rule_set_util.h"
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
#include "intrinsic/world/dof_kinematic_view.h"
#include "intrinsic/world/entity.h"
#include "intrinsic/world/entity_id.h"
#include "intrinsic/world/geometry_types.h"
#include "intrinsic/world/grouping.h"
#include "intrinsic/world/gzfile/chunk_entry.h"
#include "intrinsic/world/gzfile/gzfile.h"
#include "intrinsic/world/hashing/hashing.h"
#include "intrinsic/world/internal/inner_world.h"
#include "intrinsic/world/kinematics_builder.h"
#include "intrinsic/world/labels.h"
#include "intrinsic/world/objects/object_world_ids.h"
#include "intrinsic/world/proto/collections_component.pb.h"
#include "intrinsic/world/proto/kinematics_component.pb.h"
#include "intrinsic/world/proto/physics_component.pb.h"
#include "intrinsic/world/proto/sensor_component.pb.h"
#include "intrinsic/world/proto/world_entity.pb.h"
#include "intrinsic/world/proto/world_fragment.pb.h"
#include "intrinsic/world/world.pb.h"
#include "rclcpp/rclcpp.hpp"
#include "tf2/exceptions.hpp"
#include "tf2/transform_datatypes.hpp"
#include "tf2_ros/buffer_interface.hpp"

namespace intrinsic {
using ::intrinsic_proto::TFMessage;
namespace {

// The chunk id for the top-level World proto.
constexpr ChunkId::ValueType kWorldProtoChunkId = 'WRLD';

// Version 0 == The data is a World proto in binary format.
constexpr uint32_t kWorldProtoChunkVersionNumber = 0;

// The name of the root entity.
constexpr absl::string_view kRootEntityName = "Root";
constexpr absl::string_view kRootEntityNameLower = "root";

// TODO(stoyang): Consider moving to the geometry/api folder
absl::StatusOr<NamedGeometrySet> MaybeFuseGeometries(
    const NamedGeometrySet& transformed_geos) {
  NamedGeometrySet result;
  if (transformed_geos.empty()) {
    return result;
  } else if (transformed_geos.size() == 1) {
    return transformed_geos;
  }

  std::vector<TransformedGeometry> geos_to_be_fused;
  geos_to_be_fused.reserve(transformed_geos.size());
  for (const auto& [_, transformed_geo] : transformed_geos) {
    geos_to_be_fused.push_back(transformed_geo);
  }

  INTR_ASSIGN_OR_RETURN(TransformedGeometry merged_geo,
                        FuseGeometries(geos_to_be_fused));

  result.emplace("merged_geo", std::move(merged_geo));
  return result;
}

std::vector<AttachmentEntityId> GetChildrenOf(
    const world_internal::InnerWorld& world, AttachmentEntityId entity_handle) {
  if (entity_handle == kInvalidEntityId) {
    return {};
  }

  std::vector<AttachmentEntityId> results;
  ASSIGN_OR_DIE(auto all_entities, world.GetAllEntities());
  for (const auto& [entity_id, entity] : all_entities) {
    auto status_or_attachment = entity->GetComponent<AttachmentComponent>();
    if (status_or_attachment.ok() &&
        (*status_or_attachment)->GetParentId() == entity_handle) {
      results.emplace_back(entity_id);
    }
  }

  return results;
}

template <class K, class V>
std::vector<K> SortedKeys(const WorldHashMap<K, V>& map) {
  std::vector<K> ret;
  for (const auto& [k, v] : map) {
    ret.emplace_back(k);
  }
  std::sort(ret.begin(), ret.end());
  return ret;
}

template <class K>
std::vector<K> SortedKeys(WorldHashSet<K> set) {
  std::vector<K> ret(set.begin(), set.end());
  std::sort(ret.begin(), ret.end());
  return ret;
}

// TODO(b/241293084) remove this method once world files have had their
// RobotComponents updated
absl::Status UpdateRobotComponent(
    std::shared_ptr<world_internal::InnerWorld> inner_world) {
  INTR_ASSIGN_OR_RETURN(const auto entity_id_to_ptr_map,
                        inner_world->GetAllEntities());
  for (auto& id_to_ptr : entity_id_to_ptr_map) {
    auto& entity_ptr = id_to_ptr.second;
    if (!entity_ptr->HasComponent<RobotComponent>()) {
      continue;
    }

    INTR_ASSIGN_OR_RETURN(auto robot_component,
                          entity_ptr->GetComponent<RobotComponent>());
    INTR_ASSIGN_OR_RETURN(auto icon_devices,
                          robot_component->GetIconSimDevices());
    // remove deprecated topic fields from icon sim devices
    for (auto& device : icon_devices) {
      device.clear_output_topic();
      device.clear_input_topic();
    }
    robot_component->SetIconSimDevices(icon_devices);
  }

  return absl::OkStatus();
}

// Creates a new entity that is a clone of the input entity with the relevant
// remapped ids inside.
absl::StatusOr<std::unique_ptr<WorldEntity>> CreateRemapedEntity(
    const WorldEntity& entity,
    const WorldHashMap<EntityId, EntityId>& id_mapping) {
  // Copy the entity data. In this step, we also update the entities within
  // the data with the new mapping.
  auto new_entity = entity.Clone();
  if (new_entity->HasComponent<AttachmentComponent>()) {
    INTR_ASSIGN_OR_RETURN(auto* component,
                          new_entity->GetComponent<AttachmentComponent>());
    EntityId parent_id = component->GetParentId();
    if (id_mapping.contains(parent_id)) {
      component->SetParentId(AttachmentEntityId(id_mapping.at(parent_id)));
    } else if (parent_id != kRootEntityId) {
      return absl::InvalidArgumentError(
          absl::StrCat("Entity attachment data references an entity with id ",
                       parent_id.value(), " that is not available."));
    }
  }
  if (new_entity->HasComponent<CollectionsComponent>()) {
    INTR_ASSIGN_OR_RETURN(auto* component,
                          new_entity->GetComponent<CollectionsComponent>());
    INTR_RETURN_IF_ERROR(component->RekeyIds(id_mapping));
  }
  if (new_entity->HasComponent<CollectionsMemberComponent>()) {
    INTR_ASSIGN_OR_RETURN(
        auto* component,
        new_entity->GetComponent<CollectionsMemberComponent>());
    INTR_RETURN_IF_ERROR(component->RekeyIds(id_mapping));
  }
  if (new_entity->HasComponent<CollisionComponent>()) {
    INTR_ASSIGN_OR_RETURN(auto* component,
                          new_entity->GetComponent<CollisionComponent>());
    INTR_RETURN_IF_ERROR(
        component->RekeyIds(id_mapping, /* drop_unknown_ids = */ true));
  }
  if (new_entity->HasComponent<RobotComponent>()) {
    INTR_ASSIGN_OR_RETURN(auto* component,
                          new_entity->GetComponent<RobotComponent>());
    INTR_RETURN_IF_ERROR(component->RekeyIds(id_mapping));
  }

  return new_entity;
}

// Merges the rule sets in order
intrinsic_proto::RuleSet MergeRules(const intrinsic_proto::RuleSet& set1,
                                    const intrinsic_proto::RuleSet& set2) {
  intrinsic_proto::RuleSet merged_rules = set1;
  merged_rules.MergeFrom(set2);
  return merged_rules;
}

void RemapRuleSetEntityIds(intrinsic_proto::RuleSet& rule_set,
                           const WorldHashMap<EntityId, EntityId>& mapping) {
  for (auto& rule : *rule_set.mutable_rules()) {
    for (int i = 0; i < rule.id_1_size(); ++i) {
      rule.set_id_1(i, mapping.at(EntityId(rule.id_1(i))).value());
    }
    for (int i = 0; i < rule.id_2_size(); ++i) {
      rule.set_id_2(i, mapping.at(EntityId(rule.id_2(i))).value());
    }
  }
}

}  // namespace

World::World(std::shared_ptr<world_internal::InnerWorld> inner_world) noexcept
    : inner_world_(std::move(inner_world)) {}

std::optional<std::string> World::TryGetObjectNameFromCollection(
    CollectionsEntityId collection_entity_id) const {
  auto collection_entity_or = GetEntityById(collection_entity_id);
  if (!collection_entity_or.ok()) {
    return std::nullopt;
  }
  const auto* collection_entity = *collection_entity_or;

  if (!collection_entity->GetAlias().empty()) {
    return collection_entity->GetAlias();
  }

  absl::StatusOr<AttachmentEntityId> root_id_or =
      GetRootEntity(collection_entity_id);
  if (root_id_or.ok()) {
    auto root_entity_or = GetEntityById(*root_id_or);
    if (root_entity_or.ok()) {
      if (!(*root_entity_or)->GetAlias().empty()) {
        return (*root_entity_or)->GetAlias();
      }
    }
  }

  if (!collection_entity->GetLocalName().empty()) {
    return collection_entity->GetLocalName();
  }

  return std::nullopt;
}

absl::StatusOr<std::string> World::TryGetObjectNameForEntity(
    EntityId entity_id) const {
  absl::StatusOr<const WorldEntity*> entity_or = GetEntityById(entity_id);
  if (!entity_or.ok()) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Requesting name from non-existent entity ", entity_id.value()));
  }

  absl::StatusOr<const CollectionsMemberComponent*>
      collection_member_component_or =
          GetComponentByEntityId<CollectionsMemberComponent>(entity_id);
  if (collection_member_component_or.ok()) {
    // This entity belongs to a collection. Get the parent collection and get
    // the name for it.
    //
    // This is some ambiguity here since a collection member can have multiple
    // parents. We'll take the first one that returns for now.
    const CollectionsMemberComponent* collection_member_component =
        collection_member_component_or.value();
    for (const auto& parent_id_and_type :
         collection_member_component->GetParentCollectionsIdToTypesMap()) {
      auto maybe_object_name =
          TryGetObjectNameFromCollection(parent_id_and_type.first);
      if (maybe_object_name.has_value()) {
        return absl::StrCat(maybe_object_name.value(), ".",
                            entity_or.value()->GetLocalName());
      }
    }
  }

  auto collection_entity_id_or = ValidateEntity<CollectionsEntityId>(entity_id);
  if (collection_entity_id_or.ok()) {
    std::optional<std::string> maybe_name =
        TryGetObjectNameFromCollection(collection_entity_id_or.value());
    if (maybe_name) {
      return maybe_name.value();
    } else {
      return absl::StrCat("Object(eid=", entity_id.value(), ")");
    }
  }

  // Logging a warning here to see if this occurs. It shouldn't be expected, and
  // we can remove this once we confirm it doesn't show up.
  LOG(WARNING) << "TryGetObjectNameForEntity called on entity "
               << entity_id.value()
               << " which is not a collections member or collection itself.";
  return absl::Substitute("<$0.$1,eid=$2>", entity_or.value()->GetAlias(),
                          entity_or.value()->GetLocalName(), entity_id.value());
}

World::World(World&& other) noexcept
    : inner_world_(std::move(other.inner_world_)),
      dof_id_to_entity_id_(std::move(other.dof_id_to_entity_id_)),
      group_ids_(std::move(other.group_ids_)),
      robot_group_id_to_robot_collections_entity_ids_(
          std::move(other.robot_group_id_to_robot_collections_entity_ids_)) {
  CHECK_NE(other.grouping_aspect_, nullptr);

  model_space_spatial_trees_ = std::move(other.model_space_spatial_trees_);
  grouping_aspect_ = std::move(other.grouping_aspect_);
  grouping_aspect_->UpdateEntityWorld(this);
}

World& World::operator=(World&& other) noexcept {
  inner_world_ = std::move(other.inner_world_);
  dof_id_to_entity_id_ = std::move(other.dof_id_to_entity_id_);
  group_ids_ = std::move(other.group_ids_);
  robot_group_id_to_robot_collections_entity_ids_ =
      std::move(other.robot_group_id_to_robot_collections_entity_ids_);
  model_space_spatial_trees_ = std::move(other.model_space_spatial_trees_);
  return *this;
}

void World::CloneFrom(const World& other,
                      const WorldHashSet<EntityId>& subset_ids) {
  CHECK_EQ(grouping_aspect_, nullptr);

  for (const auto& [dof_id, ent_id] : other.dof_id_to_entity_id_) {
    if (subset_ids.contains(ent_id)) {
      dof_id_to_entity_id_.emplace(dof_id, ent_id);
    }
  }

  group_ids_.insert(other.group_ids_.begin(), other.group_ids_.end());
  for (const auto& [group_id, robot_ids] :
       other.robot_group_id_to_robot_collections_entity_ids_) {
    // Make a copy and remove any robot ids that are not part of the subset.
    std::vector<RobotCollectionsEntityId> robot_ids_copy = *robot_ids;
    robot_ids_copy.erase(
        std::remove_if(robot_ids_copy.begin(), robot_ids_copy.end(),
                       [&](const RobotCollectionsEntityId& id) {
                         return !subset_ids.contains(id);
                       }),
        robot_ids_copy.end());

    if (robot_ids_copy.empty()) {
      // If the group is now empty we can avoid adding it to the list, remove
      // the group from the overall group list too since it has no members.
      group_ids_.erase(group_id);
    } else {
      robot_group_id_to_robot_collections_entity_ids_[group_id] =
          std::make_unique<std::vector<RobotCollectionsEntityId>>(
              robot_ids_copy);
    }
  }

  // Only add cached trees for entities in the subset.
  model_space_spatial_trees_ = std::make_shared<SpatialTreesMap>();
  for (const EntityId& entity_id : subset_ids) {
    PhysicalEntityId phys_ent_id(entity_id.value());
    if (other.model_space_spatial_trees_->contains(phys_ent_id)) {
      model_space_spatial_trees_->emplace(
          phys_ent_id, other.model_space_spatial_trees_->at(phys_ent_id));
    }
  }

  VLOG(1) << "Cloning: Grouping";
  grouping_aspect_ = std::make_unique<entity_grouping_details::EntityGrouping>(
      static_cast<entity_aspect_world_details::EntityWorld*>(this));
}

absl::StatusOr<intrinsic_proto::world::internal::World> World::SerializeImpl(
    GeometrySerializer* geolib) const {
  intrinsic_proto::world::internal::World result;
  auto rule_set = inner_world_->GetDefaultRuleSet();
  if (rule_set.rules_size() != 0) {
    *result.mutable_rule_set() = std::move(rule_set);
  }

  for (const auto& entity_id : SortedKeys(GetEntityIds())) {
    INTR_ASSIGN_OR_RETURN(const auto* entity, GetEntityById(entity_id));
    INTR_ASSIGN_OR_RETURN(auto entity_proto, entity->ToProto(geolib));
    (*result.mutable_entities())[entity_id.value()] = std::move(entity_proto);
  }
  for (const auto& dof_id : SortedKeys(dof_id_to_entity_id_)) {
    EntityId ent_id = dof_id_to_entity_id_.at(dof_id);
    (*result.mutable_dof_id_to_entity_id())[dof_id.value()] = ent_id.value();
  }
  for (const auto& group_id : group_ids_) {
    result.add_group_ids(group_id.value());
  }
  std::sort(result.mutable_group_ids()->begin(),
            result.mutable_group_ids()->end());
  for (const auto& group_id :
       SortedKeys(robot_group_id_to_robot_collections_entity_ids_)) {
    const auto& robot_ids =
        robot_group_id_to_robot_collections_entity_ids_.at(group_id);
    auto& proto_robot_ids =
        (*result.mutable_group_id_to_robot_ids())[group_id.value()];
    for (auto robot_id : *robot_ids) {
      proto_robot_ids.add_robot_id(robot_id.value());
    }
    std::sort(proto_robot_ids.mutable_robot_id()->begin(),
              proto_robot_ids.mutable_robot_id()->end());
  }

  return result;
}

World World::Clone() const { return CloneSubset(GetEntityIds()); }

World World::CloneSubset(const WorldHashSet<EntityId>& subset_ids) const {
  CHECK(!subset_ids.contains(kInvalidEntityId));

  // Make sure that the root is part of the set.
  WorldHashSet<EntityId> full_subset = subset_ids;
  full_subset.insert(kRootEntityId);

  // Create a new inner world with the subset ids.
  auto new_inner_world = std::make_shared<world_internal::InnerWorld>();
  CHECK_OK(new_inner_world->CloneFrom(*inner_world_, full_subset));

  // Clone the non inner world portions of the world.
  World world_clone(std::move(new_inner_world));
  world_clone.CloneFrom(*this, full_subset);

  // Fix any missing attachment links between subset ids and root
  for (const auto& id : world_clone.GetTypedEntityIds<AttachmentEntityId>()) {
    if (id == kRootEntityId) {
      // The root entity is already valid.
      continue;
    }

    ASSIGN_OR_DIE(auto* attachment,
                  world_clone.GetComponentByEntityId<AttachmentComponent>(id));
    AttachmentEntityId parent_id = attachment->GetParentId();
    CHECK_NE(parent_id, kInvalidEntityId)
        << "Entity with id '" << id << "' has an invalid parent id";
    if (full_subset.contains(parent_id)) {
      // We have the parent so we can skip the re-attachment code.
      continue;
    }

    // Find a valid parent to the entity
    while (parent_id != kRootEntityId) {
      ASSIGN_OR_DIE(const AttachmentComponent* parent_component,
                    GetComponentByEntityId<AttachmentComponent>(parent_id));
      parent_id = parent_component->GetParentId();
      CHECK_NE(parent_id, kInvalidEntityId);

      if (full_subset.contains(parent_id)) {
        break;
      }
    }

    // At this point we have a valid parent to attach to
    attachment->SetParentId(parent_id);
    attachment->SetParentTThis(GetTransform(parent_id, id));
  }

  return world_clone;
}

World World::CreateEmptyWorld() {
  auto geolib = GetDummyGeometryLibrary();
  World result(std::make_shared<world_internal::InnerWorld>());

  intrinsic_proto::world::internal::World proto;
  CHECK_OK(result.ParseWorldData(proto, &geolib->Deserializer()))
      << "Empty deserialize should not return an error";
  return result;
}

absl::StatusOr<World> World::Deserialize(
    const intrinsic_proto::world::internal::World& data,
    const GeometryDeserializer& geolib) {
  World result(std::make_shared<world_internal::InnerWorld>());
  INTR_RETURN_IF_ERROR(result.ParseWorldData(data, &geolib));
  return std::move(result);
}

absl::StatusOr<World> World::Deserialize(
    const intrinsic_proto::world::internal::World& data) {
  World result(std::make_shared<world_internal::InnerWorld>());
  INTR_RETURN_IF_ERROR(result.ParseWorldData(data, nullptr));
  return std::move(result);
}

absl::Status World::ParseWorldData(
    const intrinsic_proto::world::internal::World& world_data,
    const GeometryDeserializer* geolib) {
  if (!GetEntityIds().empty()) {
    return absl::InternalError("Tried to parse proto without an empty world");
  }

  // Parse the entities from the proto and construct any entity based aspects.
  INTR_RETURN_IF_ERROR(ParseWorldDataImpl(world_data, geolib));

  return absl::OkStatus();
}

absl::Status World::ParseWorldData(const GZFile& world_data) {
  if (!GetEntityIds().empty()) {
    return absl::InternalError("Tried to parse GZFile without an empty world");
  }

  // Parse the World proto chunk, if present.
  auto maybe_chunk = world_data.GetChunk(ChunkId(kWorldProtoChunkId));
  if (!maybe_chunk.has_value()) {
    return absl::InvalidArgumentError("GZFile should have a world proto chunk");
  }

  if (maybe_chunk.value().GetDataVersion() != kWorldProtoChunkVersionNumber) {
    return DataLossErrorBuilder()
           << "World proto chunk has incompatible data version: "
           << maybe_chunk.value().GetDataVersion();
  }

  intrinsic_proto::world::internal::World updated_world_data;
  if (!updated_world_data.ParseFromString(
          maybe_chunk.value().GetUncompressedData())) {
    return DataLossErrorBuilder() << "Could not parse the World proto";
  }

  // Parse the entities from the proto and construct any entity based aspects.
  auto geolib = GetReadOnlyGzfGeometryLibrary(world_data);
  INTR_RETURN_IF_ERROR(
      ParseWorldDataImpl(updated_world_data, &geolib->Deserializer()));

  return absl::OkStatus();
}

absl::Status World::ParseWorldDataImpl(
    const intrinsic_proto::world::internal::World& world_proto,
    const GeometryDeserializer* geolib) {
  // Parse all of the entities (converted and natural)
  for (const auto& [entity_id, entity_proto] : world_proto.entities()) {
    INTR_ASSIGN_OR_RETURN(auto entity,
                          geolib != nullptr
                              ? WorldEntity::FromProto(entity_proto, *geolib)
                              : WorldEntity::FromProto(entity_proto),
                          _ << "Parsing entity " << entity_proto.local_name()
                            << "(" << entity_id << ")");
    if (entity_id != kRootEntityId.id.value() &&
        entity->HasLabel(labels::WorldOrigin())) {
      return DataLossErrorBuilder()
             << "Found entity with WorldOrigin that is not the root: "
             << entity_id;
    }

    INTR_RETURN_IF_ERROR(
        inner_world_->AddEntity(EntityId(entity_id), std::move(entity)));
  }

  // Setup the default rule set from the proto.
  INTR_RETURN_IF_ERROR(inner_world_->SetDefaultRuleSet(world_proto.rule_set()));

  // Ensure that the root entity is present for this world.
  if (world_proto.entities().empty()) {
    auto root_entity = WorldEntity::Create();
    INTR_RETURN_IF_ERROR(root_entity->SetLocalName(kRootEntityName));
    INTR_RETURN_IF_ERROR(root_entity->CreateComponent<AttachmentComponent>());
    INTR_RETURN_IF_ERROR(root_entity->CreateComponent<CollisionComponent>());
    INTR_RETURN_IF_ERROR(root_entity->CreateComponent<GeometryComponent>());
    INTR_RETURN_IF_ERROR(
        inner_world_->AddEntity(kRootEntityId, std::move(root_entity)));
  } else if (!inner_world_->HasEntity(kRootEntityId)) {
    return absl::DataLossError("Root entity is not present in the data");
  }

  // Ensure that the root entity has the WorldOrigin label.
  INTR_ASSIGN_OR_RETURN(auto* root_entity,
                        inner_world_->GetEntityById(kRootEntityId));
  INTR_RETURN_IF_ERROR(root_entity->AddLabels({labels::WorldOrigin()}));

  INTR_RETURN_IF_ERROR(root_entity->ValidateEntity<PhysicalEntityId>());

  // Copy collision-related data.
  model_space_spatial_trees_ = std::make_shared<SpatialTreesMap>();

  // Copy the ID translation maps.
  for (const auto& [dof_id, ent_id] : world_proto.dof_id_to_entity_id()) {
    dof_id_to_entity_id_[DofId(dof_id)] = EntityId(ent_id);
  }
  for (const auto& group_id : world_proto.group_ids()) {
    group_ids_.emplace(group_id);
  }
  for (const auto& [group_id, proto_robot_ids] :
       world_proto.group_id_to_robot_ids()) {
    robot_group_id_to_robot_collections_entity_ids_[GroupId(group_id)] =
        std::make_unique<std::vector<RobotCollectionsEntityId>>(
            proto_robot_ids.robot_id().begin(),
            proto_robot_ids.robot_id().end());
  }

  VLOG(1) << "Attempting to create and init: Grouping";
  grouping_aspect_ = std::make_unique<entity_grouping_details::EntityGrouping>(
      static_cast<entity_aspect_world_details::EntityWorld*>(this));

  // Make sure all RobotComponents no longer store deprecated data.
  // TODO(b/241293084) remove this method once world files have had their
  // RobotComponents updated
  INTR_RETURN_IF_ERROR(UpdateRobotComponent(inner_world_));

  return absl::OkStatus();
}

absl::StatusOr<intrinsic::World> World::FromFile(const GZFile& gzfile) {
  World result(std::make_shared<world_internal::InnerWorld>());
  INTR_RETURN_IF_ERROR(result.ParseWorldData(gzfile));
  return std::move(result);
}

absl::Status World::ToFile(GZFile* gzfile) const {
  CHECK(gzfile != nullptr);

  // Write the World proto chunk.
  auto geolib = GetGzfGeometryLibrary(*gzfile);
  INTR_ASSIGN_OR_RETURN(intrinsic_proto::world::internal::World world_proto,
                        Serialize(geolib->Serializer()));

  std::string pbtxt;
  {
    // Enable deterministic serialization (see b/172338083). Also, this block is
    // scoped to make sure output_stream does not hold a pointer to pbtxt when
    // std::move(pbtxt) is called below.
    google::protobuf::io::StringOutputStream output_stream(&pbtxt);
    google::protobuf::io::CodedOutputStream coded_stream(&output_stream);
    coded_stream.SetSerializationDeterministic(true);
    CHECK(world_proto.SerializeToCodedStream(&coded_stream));
  }
  ChunkEntry chunk(kWorldProtoChunkVersionNumber, std::move(pbtxt));
  INTR_RETURN_IF_ERROR(gzfile->SetChunk(ChunkId(kWorldProtoChunkId), chunk));

  return absl::OkStatus();
}

absl::Status World::ToFile(GZFile* gzfile,
                           const GeometryDeserializer& deserializer) {
  CHECK(gzfile != nullptr);

  // Serialize all of the geometries.
  auto geolib = GetGzfGeometryLibrary(*gzfile);
  for (const auto geo_id : GetTypedEntityIds<GeometryEntityId>()) {
    INTR_ASSIGN_OR_RETURN(GeometryComponent * geo_component,
                          GetComponentByEntityId<GeometryComponent>(geo_id));
    for (const std::string& geo_set_name : geo_component->GetGeometryNames()) {
      INTR_ASSIGN_OR_RETURN(
          NamedGeometrySet geos,
          geo_component->GetGeometry(geo_set_name, deserializer));
      for (const auto& [_, geo] : geos) {
        INTR_RETURN_IF_ERROR(
            geolib->Serializer().SaveGeometryV1(geo.shape()).status());
      }
    }
  }

  // Write the World proto chunk.
  INTR_ASSIGN_OR_RETURN(intrinsic_proto::world::internal::World world_proto,
                        Serialize());

  std::string pbtxt;
  {
    // Enable deterministic serialization (see b/172338083). Also, this block is
    // scoped to make sure output_stream does not hold a pointer to pbtxt when
    // std::move(pbtxt) is called below.
    google::protobuf::io::StringOutputStream output_stream(&pbtxt);
    google::protobuf::io::CodedOutputStream coded_stream(&output_stream);
    coded_stream.SetSerializationDeterministic(true);
    CHECK(world_proto.SerializeToCodedStream(&coded_stream));
  }
  ChunkEntry chunk(kWorldProtoChunkVersionNumber, std::move(pbtxt));
  INTR_RETURN_IF_ERROR(gzfile->SetChunk(ChunkId(kWorldProtoChunkId), chunk));

  return absl::OkStatus();
}

absl::Status World::UpdateFromProtoImpl(
    const intrinsic_proto::world::internal::World& world_proto,
    const GeometryDeserializer* geolib) {
  // TODO(stoyang): Ensure permission errors don't have an affect on entities.
  INTR_ASSIGN_OR_RETURN(auto inner_entities, inner_world_->GetAllEntities());

  // Parse all of the entities (converted and natural)
  for (const auto& [entity_id, entity_proto] : world_proto.entities()) {
    EntityId id(entity_id);

    if (inner_entities.contains(id)) {
      if (id != kRootEntityId.id) {
        for (const auto& label : entity_proto.labels()) {
          if (label.name() == labels::WorldOrigin().value()) {
            return DataLossErrorBuilder()
                   << "Found entity with WorldOrigin that is not the root: "
                   << entity_id;
          }
        }
      }

      // Update the entity pointer of the existing entity
      WorldEntity* entity = inner_entities.at(id);
      INTR_RETURN_IF_ERROR(geolib != nullptr
                               ? entity->UpdateFromProto(entity_proto, *geolib)
                               : entity->UpdateFromProto(entity_proto))
          << "For entity \"" << entity->GetLocalName() << "\" with id: " << id;

      // Remove the entity from the map so at the end of the proto loop we will
      // have the list of entities to remove.
      inner_entities.erase(id);
    } else {
      // We use HasEntity here in case world_proto.entities() has duplicates.
      if (HasEntity(id)) {
        return ::intrinsic::InvalidArgumentErrorBuilder()
               << "Found duplicate entity with an id: " << id;
      }

      INTR_ASSIGN_OR_RETURN(auto entity,
                            geolib != nullptr
                                ? WorldEntity::FromProto(entity_proto, *geolib)
                                : WorldEntity::FromProto(entity_proto));

      if (id != kRootEntityId.id && entity->HasLabel(labels::WorldOrigin())) {
        return DataLossErrorBuilder()
               << "Found entity with WorldOrigin that is not the root: " << id;
      }

      // Add the new entity to the world
      INTR_RETURN_IF_ERROR(inner_world_->AddEntity(id, std::move(entity)));
    }
  }

  // Delete any inner_entities that are not in the proto
  for (const auto& [entity_id, entity_ptr] : inner_entities) {
    INTR_RETURN_IF_ERROR(inner_world_->RemoveEntity(entity_id));
  }

  // Clear the collision data cache
  model_space_spatial_trees_->clear();

  // Copy the ID translation maps.
  dof_id_to_entity_id_.clear();
  for (const auto& [dof_id, ent_id] : world_proto.dof_id_to_entity_id()) {
    dof_id_to_entity_id_[DofId(dof_id)] = EntityId(ent_id);
  }

  group_ids_.clear();
  for (const auto& group_id : world_proto.group_ids()) {
    group_ids_.emplace(group_id);
  }

  robot_group_id_to_robot_collections_entity_ids_.clear();
  for (const auto& [group_id, proto_robot_ids] :
       world_proto.group_id_to_robot_ids()) {
    robot_group_id_to_robot_collections_entity_ids_[GroupId(group_id)] =
        std::make_unique<std::vector<RobotCollectionsEntityId>>(
            proto_robot_ids.robot_id().begin(),
            proto_robot_ids.robot_id().end());
  }

  return absl::OkStatus();
}

// ------------------------------------
// -- Entity based methods
// ------------------------------------

WorldHashSet<EntityId> World::GetEntityIds() const {
  return inner_world_->GetEntityIds();
}

absl::StatusOr<EntityId> World::GetEntityIdByDofId(DofId dof_id) const {
  auto iter = dof_id_to_entity_id_.find(dof_id);
  if (iter == dof_id_to_entity_id_.end()) {
    return intrinsic::NotFoundErrorBuilder()
           << "no EntityId found for DofId \"" << dof_id.value() << "\"";
  }
  return iter->second;
}

absl::StatusOr<DofId> World::GetDofIdByEntityId(EntityId entity_id) const {
  for (const auto& [dof_id, ent_id] : dof_id_to_entity_id_) {
    if (entity_id == ent_id) {
      return dof_id;
    }
  }
  return intrinsic::NotFoundErrorBuilder()
         << "no DofId found for EntityId " << entity_id.value();
}

WorldHashSet<AttachmentEntityId> World::FindByExactLocalNames(
    AttachmentEntityId root,
    const std::vector<std::string>& local_names) const {
  return FindByLocalNamesImpl(root, true, local_names);
}

WorldHashSet<AttachmentEntityId> World::FindByLocalNames(
    AttachmentEntityId root,
    const std::vector<std::string>& local_names) const {
  return FindByLocalNamesImpl(root, false, local_names);
}

WorldHashSet<AttachmentEntityId> World::FindByLocalNamesImpl(
    AttachmentEntityId root, bool is_exact,
    const std::vector<std::string>& local_names) const {
  WorldHashSet<AttachmentEntityId> results;
  if (local_names.empty()) {
    return results;
  }

  const std::string& local_name = local_names.front();
  for (const auto& child_id : GetChildrenOf(root)) {
    // If we match the next name we can progress down the list of local names.
    ASSIGN_OR_DIE(auto child_ent, GetEntityById(child_id));
    if (child_ent->GetLocalName() == local_name) {
      // If we don't have any more local names we can add the child as part of
      // the results.
      if (local_names.size() == 1) {
        results.insert(child_id);
      } else {
        // If we still have more to match we can recursively look to the child
        // entities.
        // TODO(stoyang): Avoid the copy by using an index param
        std::vector<std::string> other_local_names(local_names.begin() + 1,
                                                   local_names.end());
        auto inner_results =
            FindByLocalNamesImpl(child_id, is_exact, other_local_names);
        results.insert(inner_results.begin(), inner_results.end());
      }
      // If we did not match the child but are allowed to skip nodes we can
      // recursively look at the child nodes and continue the search there.
    }
    if (!is_exact) {
      auto inner_results =
          FindByLocalNamesImpl(child_id, is_exact, local_names);
      results.insert(inner_results.begin(), inner_results.end());
    }
  }

  return results;
}

absl::StatusOr<std::vector<std::string>> World::GetLocalNamePath(
    AttachmentEntityId id) const {
  std::vector<std::string> local_names;
  do {
    INTR_ASSIGN_OR_RETURN(auto* ent, GetEntityById(id));

    // Add the entity to the result list.
    local_names.push_back(ent->GetLocalName());

    // Grab the parent id and use it for the next iteration
    INTR_ASSIGN_OR_RETURN(auto component,
                          ent->GetComponent<AttachmentComponent>());
    id = component->GetParentId();
  } while (id != kRootEntityId);

  // Flip the order so that the given entity is at the end.
  std::reverse(local_names.begin(), local_names.end());
  return local_names;
}

absl::StatusOr<std::string> World::GetLocalNamePathString(
    AttachmentEntityId id, absl::string_view separator) const {
  INTR_ASSIGN_OR_RETURN(auto names, GetLocalNamePath(id));
  return absl::StrJoin(names, separator);
}

WorldHashSet<AttachmentEntityId> World::FindByNameAndCollectionPath(
    const std::string& local_name,
    const std::vector<std::string>& collection_path) const {
  if (collection_path.empty()) {
    return {};
  }

  // Find candidate entities with the correct local name.
  WorldHashSet<AttachmentEntityId> candidates;
  ASSIGN_OR_DIE(auto all_entities, inner_world_->GetAllEntities());
  for (const auto& [id, child_ent] : all_entities) {
    if (child_ent->GetLocalName() == local_name) {
      auto attachment_id_or = ValidateEntity<AttachmentEntityId>(id);
      if (attachment_id_or.ok()) {
        candidates.insert(attachment_id_or.value());
      }
    }
  }

  // Check each candidate's ancestry in the attachment graph.
  WorldHashSet<AttachmentEntityId> ret;
  for (AttachmentEntityId candidate : candidates) {
    // Traverse from the candidate to world root, looking for ancestors that are
    // members of the collections found in collection_path.
    int path_index = collection_path.size() - 1;
    AttachmentEntityId current_id = candidate;
    while (path_index >= 0 && current_id != kInvalidEntityId) {
      ASSIGN_OR_DIE(const auto* current_ent, GetEntityById(current_id));

      auto current_collections_member_or =
          current_ent->GetComponent<CollectionsMemberComponent>();
      // If one of this entity's collection parents has the correct name,
      // consume an element of collection_path by decrementing path_index.
      if (current_collections_member_or.ok()) {
        const auto& parent_ids =
            std::views::keys(current_collections_member_or.value()
                                 ->GetParentCollectionsIdToTypesMap());
        if (std::any_of(
                parent_ids.begin(), parent_ids.end(),
                [this, &collection_path, path_index](CollectionsEntityId id) {
                  ASSIGN_OR_DIE(const auto* ent, GetEntityById(id));
                  return ent->GetLocalName() == collection_path[path_index];
                })) {
          path_index--;
        }
      }
      ASSIGN_OR_DIE(const auto* current_attachment,
                    current_ent->GetComponent<AttachmentComponent>());
      current_id = current_attachment->GetParentId();
    }
    // If we've consumed the entire path, this candidate is a match.
    if (path_index < 0) {
      ret.emplace(candidate);
    }
  }
  return ret;
}

absl::StatusOr<EntityId> World::FindByAlias(absl::string_view alias) const {
  if (alias.empty()) {
    return absl::InvalidArgumentError("Alias must be non empty");
  }

  INTR_ASSIGN_OR_RETURN(const auto all_entities,
                        inner_world_->GetAllEntities());

  std::optional<EntityId> result;
  for (const auto& [entity_id, entity] : all_entities) {
    if (entity->GetAlias() == alias) {
      if (result.has_value()) {
        return ::intrinsic::InvalidArgumentErrorBuilder()
               << "Found two entities with the same alias: " << alias;
      }

      result = entity_id;
    }
  }

  if (!result.has_value()) {
    return intrinsic::NotFoundErrorBuilder()
           << "Could not find an entity with alias: " << alias;
  }

  return result.value();
}

absl::Status World::SetAlias(EntityId id, absl::string_view alias) {
  // If the alias is not empty we need to check for duplicates.
  if (!alias.empty()) {
    // Go through all the entities and see if any of them have the same alias.
    INTR_ASSIGN_OR_RETURN(auto all_entities, inner_world_->GetAllEntities());
    for (const auto& [entity_id, entity] : all_entities) {
      if (entity_id != id) {
        if (entity->GetAlias() == alias) {
          return ::intrinsic::InvalidArgumentErrorBuilder()
                 << "Alias already exists within the world: " << alias;
        }
      }
    }
  }

  INTR_ASSIGN_OR_RETURN(auto* entity, GetEntityById(id));
  INTR_RETURN_IF_ERROR(entity->SetAlias(alias));
  return absl::OkStatus();
}

EntityId World::CreateEntity() {
  return inner_world_->CreateEntity(kDefaultEntityIdPrefix);
}

absl::StatusOr<EntityId> World::CreateEntityWithId(EntityId entity_id) {
  INTR_RETURN_IF_ERROR(
      inner_world_->AddEntity(entity_id, WorldEntity::Create()));
  return entity_id;
}

absl::StatusOr<WorldHashMap<EntityId, EntityId>> World::IntegrateWorld(
    const AttachmentEntityId& to_attach_to, uint16_t entity_id_prefix,
    const World& other_world) {
  // This line is needed for force the const-version of the call.
  const auto* other_inner_world = other_world.inner_world_.get();
  INTR_ASSIGN_OR_RETURN(
      (WorldHashMap<EntityId, const WorldEntity*> other_entities),
      other_inner_world->GetAllEntities());
  INTR_ASSIGN_OR_RETURN(
      auto mapping,
      IntegrateEntities(to_attach_to, entity_id_prefix, other_entities));

  // Remap the rule set entity ids.
  intrinsic_proto::RuleSet processed_rule_set =
      other_inner_world->GetDefaultRuleSet();
  RemapRuleSetEntityIds(processed_rule_set, mapping);

  // The priority is with the first action for a matching pair, so we add the
  // new rules rule set after the current.
  intrinsic_proto::RuleSet merged_rule_set =
      MergeRules(inner_world_->GetDefaultRuleSet(), processed_rule_set);
  INTR_RETURN_IF_ERROR(inner_world_->SetDefaultRuleSet(merged_rule_set));

  return std::move(mapping);
}

absl::StatusOr<WorldHashMap<EntityId, EntityId>> World::IntegrateFragmentImpl(
    const AttachmentEntityId& to_attach_to, uint16_t entity_id_prefix,
    const intrinsic_proto::world::WorldFragment& input_fragment,
    const GeometryDeserializer* geolib) {
  // TODO(stoyang): We should be able to re-use these entities without having to
  // clone them again later in CreateRemapedEntity.
  WorldHashMap<EntityId, std::unique_ptr<WorldEntity>> entity_ptrs;
  WorldHashMap<EntityId, const WorldEntity*> entities;

  for (const auto& [id, entity_proto] : input_fragment.entities()) {
    INTR_ASSIGN_OR_RETURN(auto entity,
                          geolib != nullptr
                              ? WorldEntity::FromProto(entity_proto, *geolib)
                              : WorldEntity::FromProto(entity_proto));
    entities.emplace(EntityId(id), entity.get());
    entity_ptrs.emplace(EntityId(id), std::move(entity));
  }

  INTR_ASSIGN_OR_RETURN(
      auto mapping,
      IntegrateEntities(to_attach_to, entity_id_prefix, entities));

  // Remap the rule set entity ids.
  intrinsic_proto::RuleSet fragment_rule_set = input_fragment.rule_set();
  RemapRuleSetEntityIds(fragment_rule_set, mapping);

  // The priority is with the first action for a matching pair, so we add the
  // new rules rule set after the current.
  intrinsic_proto::RuleSet merged_rule_set =
      MergeRules(inner_world_->GetDefaultRuleSet(), fragment_rule_set);
  INTR_RETURN_IF_ERROR(inner_world_->SetDefaultRuleSet(merged_rule_set));

  return mapping;
}

absl::Status World::IntegrateEntitiesKeepingIds(
    const WorldHashMap<EntityId, const WorldEntity*>& other_entities) {
  WorldHashMap<EntityId, EntityId> other_to_this_id_mapping{};

  for (const auto& [other_id, other_entity] : other_entities) {
    if (other_id == kRootEntityId) continue;
    other_to_this_id_mapping[other_id] = other_id;

    // Add parent entity to mapping so that later CreateRemapedEntity() can
    // handle the CollectionsMemberComponents.
    if (other_entity->HasComponent<CollectionsMemberComponent>()) {
      INTR_ASSIGN_OR_RETURN(
          const CollectionsMemberComponent* collections_member_component,
          other_entity->GetComponent<CollectionsMemberComponent>());
      for (const auto& [parent_id, _] :
           collections_member_component->GetParentCollectionsIdToTypesMap()) {
        other_to_this_id_mapping[parent_id] = parent_id;
      }
    }
    // Add parent entity to mapping so that later CreateRemapedEntity() can
    // handle the AttachmentComponents.
    if (other_entity->HasComponent<AttachmentComponent>()) {
      INTR_ASSIGN_OR_RETURN(const AttachmentComponent* attachment_component,
                            other_entity->GetComponent<AttachmentComponent>());
      AttachmentEntityId parent_id = attachment_component->GetParentId();
      other_to_this_id_mapping[parent_id] = parent_id;
    }
    // Add the entities that are referenced in the CollisionComponents to the
    // mapping so that later CreateRemapedEntity() won't drop any collision
    // exclusions because they cannot be mapped.
    if (other_entity->HasComponent<CollisionComponent>()) {
      INTR_ASSIGN_OR_RETURN(const CollisionComponent* collision_component,
                            other_entity->GetComponent<CollisionComponent>());
      for (const PhysicalEntityId& exclusion_id :
           collision_component->GetExclusions()) {
        if (HasEntity(exclusion_id)) {
          other_to_this_id_mapping[exclusion_id] = exclusion_id;
        }
      }
    }
  }

  INTR_RETURN_IF_ERROR(
      IntegrateEntitiesImpl(other_entities, other_to_this_id_mapping));

  return absl::OkStatus();
}

absl::StatusOr<WorldHashMap<EntityId, EntityId>> World::IntegrateEntities(
    const AttachmentEntityId& to_attach_to, uint16_t entity_id_prefix,
    const WorldHashMap<EntityId, const WorldEntity*>& other_entities) {
  // Populate the mapping of ids.
  // The root maps to the entity we want the other entities to attach to.
  //
  // TODO(keegang): There might be weird behavior if an other object has a
  // id that is supposed to reference the root, but this use-case does not exist
  // because the root object is usually referred to by its constant
  // (kRootEntityId).
  WorldHashMap<EntityId, EntityId> other_to_this_id_mapping{
      {kRootEntityId, to_attach_to}};
  // Traverse the entities in a deterministic order so that reservations are
  // consistent across multiple runs.
  for (const EntityId& other_id : SortedKeys(other_entities)) {
    if (other_id == kRootEntityId) continue;
    other_to_this_id_mapping[other_id] =
        inner_world_->ReserveId(entity_id_prefix);
  }

  INTR_RETURN_IF_ERROR(
      IntegrateEntitiesImpl(other_entities, other_to_this_id_mapping));

  return other_to_this_id_mapping;
}

absl::Status World::IntegrateEntitiesImpl(
    const WorldHashMap<EntityId, const WorldEntity*>& other_entities,
    WorldHashMap<EntityId, EntityId> other_to_this_id_mapping) {
  // Check for duplicate alias as this is not a recoverable case at this time.
  absl::btree_set<std::string> other_entities_aliases;
  for (const auto& [id, entity] : other_entities) {
    const std::string alias = entity->GetAlias();
    if (alias.empty()) {
      continue;
    }

    // If we saw this alias before return an error.
    if (!other_entities_aliases.insert(alias).second) {
      return ::intrinsic::InvalidArgumentErrorBuilder()
             << "Cannot integrate entities, duplicate alias found: " << alias
             << " within the requested entities.";
    }

    auto status_or = FindByAlias(alias);
    if (status_or.ok()) {
      return ::intrinsic::InvalidArgumentErrorBuilder()
             << "Cannot integrate entities, duplicate alias found: " << alias;
    }
  }

  // Build new entities. Add them later so that we don't leave this world in an
  // uncertain state if something fails.
  // TODO(stoyang): Can this be replaced by CreateRemapedEntities?
  WorldHashMap<EntityId, std::unique_ptr<WorldEntity>> new_entities;
  for (const auto& [other_id, other_entity] : other_entities) {
    if (other_id == kRootEntityId) continue;

    INTR_ASSIGN_OR_RETURN(
        auto new_entity,
        CreateRemapedEntity(*other_entity, other_to_this_id_mapping));

    // Add this element.
    EntityId new_id = other_to_this_id_mapping.at(other_id);
    new_entities[new_id] = std::move(new_entity);
  }

  // Copy over group data.
  //
  // TODO(keegang): We are skipping this for now as it presents a number of
  // complications. This means that any code (for example, skills) that use the
  // group to identify the robot may not work correctly.
  //
  // TODO(keegang): We don't want overlapping string names so might need to
  // remap labels as well... Hard to tell if label should be deduped when used
  // as a group versus as a true "label" like "tip"

  // Add all entities now. Should succeed from this point onward...
  for (auto& [new_id, new_entity] : new_entities) {
    INTR_RETURN_IF_ERROR(
        inner_world_->AddEntity(new_id, std::move(new_entity)));
  }
  return absl::OkStatus();
}

absl::Status World::RemoveEntity(EntityId entity_id) {
  return inner_world_->RemoveEntity(entity_id);
}

absl::Status World::SafelyRemoveEntity(EntityId entity_id) {
  if (entity_id == kInvalidEntityId) {
    return absl::InvalidArgumentError("Invalid entity id.");
  }

  if (entity_id == kRootEntityId) {
    return absl::InvalidArgumentError("Cannot remove the root entity.");
  }

  INTR_ASSIGN_OR_RETURN(auto* entity, GetEntityById(entity_id));

  auto collections_component = entity->GetComponent<CollectionsComponent>();
  if (collections_component.ok()) {
    auto members = collections_component.value()->GetAllCollectionMembers();
    if (members.size() > 1) {
      return absl::InvalidArgumentError(
          "Cannot delete an entity that is a collection parent");
    } else if (members.size() == 1) {
      // We call SafelyRemoveEntity here so that we can reuse the logic of
      // removing a member of a collection as long as it is the only member
      // within a collection.
      return SafelyRemoveEntity(*members.begin());
    } else {
      // There are no members within this collection so it is safe to remove.
    }
  }

  auto att_id = ValidateEntity<AttachmentEntityId>(entity_id);
  if (!att_id.ok()) {
    return absl::InvalidArgumentError(
        "Cannot delete an entity without an attachment component");
  }

  if (!GetChildrenOf(*att_id).empty()) {
    return ::intrinsic::InvalidArgumentErrorBuilder()
           << "Cannot remove entity '" << *att_id
           << "' as it has child entities";
  }

  auto collection_member_component =
      entity->GetComponent<CollectionsMemberComponent>();

  // Remove the entity from any collections component, and remove the collection
  // if the entity is the only member
  std::set<EntityId> secondary_entities_to_delete;
  if (collection_member_component.ok()) {
    const auto& collections_map =
        collection_member_component.value()->GetParentCollectionsIdToTypesMap();

    for (const auto& [collection_id, type_set] : collections_map) {
      INTR_ASSIGN_OR_RETURN(
          auto parent_collection_component,
          GetComponentByEntityId<CollectionsComponent>(collection_id));

      const auto members =
          parent_collection_component->GetAllCollectionMembers();
      if (members.size() == 1 && (*members.begin()) == entity_id) {
        secondary_entities_to_delete.insert(collection_id);
      } else if (members.size() > 1) {
        for (auto type : type_set) {
          const std::vector<CollectionsMemberEntityId>& parent_members =
              parent_collection_component->GetCollectionMembers(type);
          std::vector<CollectionsMemberEntityId> new_parent_members;
          absl::c_copy_if(parent_members,
                          std::back_inserter(new_parent_members),
                          [entity_id](const CollectionsMemberEntityId id) {
                            return id.value() != entity_id.value();
                          });
          INTR_RETURN_IF_ERROR(
              parent_collection_component->SetCollectionMembers(
                  type, new_parent_members));
        }
      } else {
        // Members is empty or not the right id...something is wrong...
        return absl::InternalError("Inconsistent CollectionsComponent");
      }
    }
  }

  INTR_RETURN_IF_ERROR(RemoveEntity(*att_id));

  for (const auto& entity : secondary_entities_to_delete) {
    INTR_RETURN_IF_ERROR(RemoveEntity(entity));
  }

  return absl::OkStatus();
}

bool World::HasEntity(EntityId entity_id) const {
  return inner_world_->HasEntity(entity_id);
}

WorldHashSet<EntityId> World::GetEntitiesWithAllLabels(
    const WorldHashSet<LabelId>& label_ids,
    absl::string_view local_name) const {
  WorldHashSet<EntityId> results;
  ASSIGN_OR_DIE(auto all_entities, inner_world_->GetAllEntities());
  for (const auto& [entity_id, entity] : all_entities) {
    if (!local_name.empty() && entity->GetLocalName() != local_name) {
      continue;
    }
    bool missing_label = false;
    for (const auto& label : label_ids) {
      if (!entity->HasLabel(label)) {
        missing_label = true;
        break;
      }
    }

    if (!missing_label) {
      results.insert(entity_id);
    }
  }

  return results;
}

absl::StatusOr<EntityId> World::GetSingleEntityWithAllLabels(
    const WorldHashSet<LabelId>& label_ids,
    absl::string_view local_name) const {
  const auto entities = GetEntitiesWithAllLabels(label_ids, local_name);
  if (entities.empty()) {
    return intrinsic::NotFoundErrorBuilder()
           << "No entity found with given set of labels {"
           << absl::StrJoin(SortedKeys(label_ids), ", ",
                            absl::StreamFormatter())
           << "} and local name " << local_name;
  }
  if (entities.size() != 1) {
    return ::intrinsic::InvalidArgumentErrorBuilder()
           << "More than one entity found (=" << entities.size()
           << ") with given set of labels {"
           << absl::StrJoin(SortedKeys(label_ids), ", ",
                            absl::StreamFormatter())
           << "} and local name " << local_name;
  }

  return *entities.begin();
}

absl::StatusOr<const WorldEntity*> World::GetEntityById(EntityId id) const {
  return inner_world_->GetEntityById(id);
}

absl::StatusOr<WorldEntity*> World::GetEntityById(EntityId id) {
  return inner_world_->GetEntityById(id);
}

std::string World::GetLocalNameForEntityById(EntityId id) const {
  auto entity_or_status = GetEntityById(id);
  if (entity_or_status.ok()) {
    return entity_or_status.value()->GetLocalName();
  }
  return absl::StrCat("InvalidId(", id.value(), ")");
}

WorldHashSet<EntityId> World::FindByLabel(LabelId label) const {
  WorldHashSet<EntityId> results;
  ASSIGN_OR_DIE(auto all_entities, inner_world_->GetAllEntities());
  for (const auto& [entity_id, entity] : all_entities) {
    if (entity->HasLabel(label)) {
      results.insert(entity_id);
    }
  }

  return results;
}

WorldHashSet<EntityId> World::FindByLabel(AttachmentEntityId root,
                                          LabelId label) const {
  WorldHashSet<EntityId> results;
  ASSIGN_OR_DIE(auto all_entities, inner_world_->GetAllEntities());
  for (const auto& [entity_id, entity] : all_entities) {
    if (entity->HasLabel(label)) {
      auto attachment_id = ValidateEntity<AttachmentEntityId>(entity_id);

      if (attachment_id.ok()) {
        auto common_ancestor = FindCommonAncestor(root, *attachment_id);
        if (common_ancestor.ok() && *common_ancestor == root) {
          results.insert(entity_id);
        }
      }
    }
  }

  return results;
}

absl::StatusOr<RobotCollectionsEntityId> World::GetRootRobot(
    const WorldHashSet<RobotCollectionsEntityId>& robot_entities) const {
  // Since robot entities are not part of the attachment graph, we need to
  // get a link of each robot and then search the attachment graph.

  WorldHashMap<AttachmentEntityId, RobotCollectionsEntityId>
      base_link_to_robot_id;
  WorldHashSet<EntityId> robot_base_links;
  for (const auto& robot_id : robot_entities) {
    INTR_ASSIGN_OR_RETURN(auto base_link_id, GetBaseLink(robot_id),
                          _ << " while looking for base link of "
                            << GetLocalNameForEntityById(robot_id)
                            << " id=" << robot_id);
    robot_base_links.insert(base_link_id);
    base_link_to_robot_id.insert(std::make_pair(base_link_id, robot_id));
  }

  INTR_ASSIGN_OR_RETURN(AttachmentEntityId root_base_link_id,
                        GetRootEntity(robot_base_links));

  auto it = base_link_to_robot_id.find(root_base_link_id);
  if (it == base_link_to_robot_id.end()) {
    return intrinsic::NotFoundErrorBuilder()
           << "Unable to match the computed root attachment id to a base link "
              "id, got attachment id for: "
           << GetLocalNameForEntityById(root_base_link_id)
           << " id=" << root_base_link_id
           << " while looking for the root of robots: ";
  }
  return it->second;
}

// Return the root of a collection of attachment entities.
absl::StatusOr<AttachmentEntityId> World::GetRootEntity(
    const WorldHashSet<EntityId>& entities) const {
  // Go through the entities and find the root amongs them.
  std::optional<AttachmentEntityId> maybe_root;

  // TEST(WorldTest, GetRootRobotInTree) is flaky if the entities are not sorted
  // It seems like this might point to a bug in the implementation.
  // Note: gtl::flat_set used to enforce sorting of entities.
  std::vector sorted_entities(entities.begin(), entities.end());
  std::sort(sorted_entities.begin(), sorted_entities.end());
  for (const EntityId& entity_id : sorted_entities) {
    absl::StatusOr<AttachmentEntityId> current_entity =
        ValidateEntity<AttachmentEntityId>(entity_id);
    if (!current_entity.ok()) {
      // If this entity does not have an attachment component we will skip it
      // because we are looking for the root in the attachment tree and if there
      // is no component, it cannot be the root.
      continue;
    }

    if (!maybe_root.has_value()) {
      // If we don't have a root yet then update the current root.
      maybe_root = *current_entity;
      continue;
    }

    // Find the common ancestor for the current entity and the current root.
    INTR_ASSIGN_OR_RETURN(
        AttachmentEntityId common_ancestor,
        FindCommonAncestor(*current_entity, maybe_root.value()));

    if (common_ancestor == *current_entity) {
      // In the easy case, the current entity is the common ancestor and will
      // replace the current root.
      maybe_root = *current_entity;
    } else if (common_ancestor != maybe_root.value()) {
      // This is a tree case where neither entity is the common root. If the
      // common ancestor is part of the entities to be checked we will use
      // that and assign it the current root, otherwise this is an error.
      if (entities.contains(common_ancestor)) {
        INTR_RETURN_IF_ERROR(
            ValidateEntity<AttachmentEntityId>(entity_id).status());
        maybe_root = common_ancestor;
      } else {
        auto to_string = [this](AttachmentEntityId entity_id) {
          return absl::Substitute("$0 (id=$1)",
                                  GetLocalNameForEntityById(entity_id),
                                  entity_id.value());
        };

        return FailedPreconditionErrorBuilder()
               << "There is no common root for this model. Entity '"
               << to_string(*current_entity) << "' and '"
               << to_string(*maybe_root)
               << "' are both in the model but their common root '"
               << to_string(common_ancestor) << "' is not.";
      }
    }
  }

  if (!maybe_root.has_value()) {
    return intrinsic::NotFoundErrorBuilder()
           << "No attachment entities present";
  }

  return maybe_root.value();
}

absl::StatusOr<AttachmentEntityId> World::GetRootEntity(
    CollectionsEntityId collection_id) const {
  INTR_ASSIGN_OR_RETURN(
      std::vector<AttachmentEntityId> members,
      FilterCollectionMembers<AttachmentEntityId>(collection_id));

  if (members.empty()) {
    return intrinsic::NotFoundErrorBuilder()
           << "collections entity with ID " << collection_id.value()
           << " has no attachment members";
  }

  WorldHashSet<EntityId> candidates(members.begin(), members.end());
  return GetRootEntity(candidates);
}

absl::StatusOr<AttachmentEntityId> World::GetRootOfModel(
    absl::string_view model_name) const {
  // Grab all of the entities within the model.
  WorldHashSet<EntityId> entities = FindByLabel(LabelId(model_name));
  if (entities.empty()) {
    return intrinsic::NotFoundErrorBuilder()
           << "No entities present in model: " << model_name;
  }

  return GetRootEntity(entities);
}

std::vector<AttachmentEntityId> World::GetChildrenOf(
    AttachmentEntityId entity_handle) const {
  return ::intrinsic::GetChildrenOf(*inner_world_, entity_handle);
}

absl::StatusOr<AttachmentEntityId> World::CreateAttachmentComponent(
    AttachmentEntityId parent_handle, EntityId id,
    const Pose3d& parent_t_entity, std::optional<absl::Time> timestamp) {
  if (id == kInvalidEntityId) {
    return absl::InvalidArgumentError("Invalid entity id.");
  }

  if (parent_handle == kInvalidEntityId) {
    return absl::InvalidArgumentError("Invalid entity parent id.");
  }

  if (id == kRootEntityId) {
    return absl::InvalidArgumentError(
        "Cannot set a parent for the root entity.");
  }

  INTR_ASSIGN_OR_RETURN(auto entity, GetEntityById(id));
  if (!HasEntity(parent_handle)) {
    return intrinsic::NotFoundErrorBuilder()
           << "Could not find parent entity with id: " << parent_handle;
  }

  INTR_RETURN_IF_ERROR(entity->CreateComponent<AttachmentComponent>());
  INTR_ASSIGN_OR_RETURN(auto component,
                        entity->GetComponent<AttachmentComponent>());

  component->SetParentId(parent_handle);
  component->SetParentTThis(parent_t_entity, timestamp);

  return AttachmentEntityId(id);
}

absl::Status World::DeleteAttachmentComponent(
    AttachmentEntityId entity_handle) {
  if (entity_handle == kInvalidEntityId) {
    return absl::InvalidArgumentError("Invalid entity id.");
  }

  if (entity_handle == kRootEntityId) {
    return absl::InvalidArgumentError(
        "Cannot remove an attachment component from the root entity.");
  }

  INTR_ASSIGN_OR_RETURN(auto entity, GetEntityById(entity_handle));
  if (!GetChildrenOf(entity_handle).empty()) {
    return absl::InvalidArgumentError(
        "Cannot remove an attachment component from an entity with children.");
  }

  INTR_RETURN_IF_ERROR(entity->RemoveComponent<AttachmentComponent>());
  return absl::OkStatus();
}

absl::StatusOr<PhysicsEntityId> World::CreatePhysicsComponent(
    EntityId id,
    const intrinsic_proto::world::PhysicsComponent& physics_component) {
  if (id == kInvalidEntityId) {
    return absl::InvalidArgumentError("Invalid entity id.");
  }

  if (id == kRootEntityId) {
    return absl::InvalidArgumentError(
        "Cannot create a physics component for the root entity.");
  }

  INTR_ASSIGN_OR_RETURN(auto entity, GetEntityById(id));
  INTR_RETURN_IF_ERROR(
      entity->CreateComponentFromProto<PhysicsComponent>(physics_component));
  return PhysicsEntityId(id);
}

absl::StatusOr<SensorEntityId> World::CreateSensorComponent(
    EntityId id,
    const intrinsic_proto::world::SensorComponent& sensor_component) {
  if (id == kInvalidEntityId) {
    return absl::InvalidArgumentError("Invalid entity id.");
  }

  if (id == kRootEntityId) {
    return absl::InvalidArgumentError(
        "Cannot create a sensor component for the root entity.");
  }

  INTR_ASSIGN_OR_RETURN(auto entity, GetEntityById(id));
  INTR_RETURN_IF_ERROR(
      entity->CreateComponentFromProto<SensorComponent>(sensor_component));
  return SensorEntityId(id);
}

void World::UpdateAttachmentPose(AttachmentEntityId a_handle,
                                 AttachmentEntityId b_handle,
                                 const Pose3d& a_t_b) {
  CHECK_NE(a_handle, kInvalidEntityId) << "Invalid entity id.";
  CHECK_NE(b_handle, kInvalidEntityId) << "Invalid entity id.";
  ASSIGN_OR_DIE(auto attachment_a,
                GetComponentByEntityId<AttachmentComponent>(a_handle));
  ASSIGN_OR_DIE(auto attachment_b,
                GetComponentByEntityId<AttachmentComponent>(b_handle));

  if (attachment_a->GetParentId() == b_handle) {
    attachment_a->SetParentTThis(a_t_b.inverse());
  } else if (attachment_b->GetParentId() == a_handle) {
    attachment_b->SetParentTThis(a_t_b);
  } else {
    LOG(FATAL) << "UpdateAttachmentPose cannot be called on entities ('"
               << a_handle << "' and '" << b_handle
               << "') that are "
                  "not directly attached.";
  }
}

absl::Status World::ReparentEntity(AttachmentEntityId new_parent_handle,
                                   AttachmentEntityId child_handle,
                                   const Pose3d& parent_t_entity,
                                   std::optional<absl::Time> timestamp) {
  if (new_parent_handle == kInvalidEntityId) {
    return absl::InvalidArgumentError("Invalid parent entity id.");
  }

  if (child_handle == kInvalidEntityId) {
    return absl::InvalidArgumentError("Invalid child entity id.");
  }

  if (child_handle == kRootEntityId) {
    return absl::InvalidArgumentError("Unable to reparent the root object.");
  }

  if (child_handle == new_parent_handle) {
    return absl::InvalidArgumentError("Unable to reparent to self.");
  }

  if (!inner_world_->HasEntity(new_parent_handle)) {
    return absl::InvalidArgumentError(
        "Cannot call ReparentObject with an id that is not part of this "
        "world.");
  }

  INTR_ASSIGN_OR_RETURN(
      auto child_attachment,
      GetComponentByEntityId<AttachmentComponent>(child_handle));
  CHECK(child_attachment != nullptr);

  // Make sure reparenting would not create a cycle. (If the child's parent
  // hasn't been set yet, it automatically passes this check.)
  if (child_attachment->GetParentId() != kInvalidEntityId) {
    // This should really be IsAncestor(child_handle, new_parent_handle) so as
    // to make it more efficient. We really just want to know if
    // new_parent_handle is actually already a child of the given child_handle.
    INTR_ASSIGN_OR_RETURN(auto common,
                          FindCommonAncestor(new_parent_handle, child_handle));
    if (common == child_handle) {
      return absl::InvalidArgumentError(
          "Cannot reparent objects as it would create a cycle");
    }
  }

  child_attachment->SetParentId(new_parent_handle);
  child_attachment->SetParentTThis(parent_t_entity, timestamp);

  return absl::OkStatus();
}

Pose3d World::GetTransform(AttachmentEntityId a_handle,
                           AttachmentEntityId b_handle) const {
  CHECK_NE(a_handle, kInvalidEntityId) << "Invalid entity id.";
  CHECK_NE(b_handle, kInvalidEntityId) << "Invalid entity id.";
  CHECK(inner_world_->HasEntity(a_handle)) << "Invalid entity id: " << a_handle;
  CHECK(inner_world_->HasEntity(b_handle)) << "Invalid entity id: " << b_handle;

  if (a_handle == b_handle) {
    // Return identity if a and to are the same
    return {};
  }

  ASSIGN_OR_DIE(const AttachmentEntityId common_ancestor,
                FindCommonAncestor(a_handle, b_handle));

  // Computes the transform from `common_ancestor` to `entity_id`.
  auto get_ancestor_t_entity =
      [&](AttachmentEntityId entity_id) -> absl::StatusOr<Pose3d> {
    Pose3d ancestor_t_entity;
    for (AttachmentEntityId obj_id = entity_id; obj_id != common_ancestor;) {
      INTR_ASSIGN_OR_RETURN(
          auto component, GetComponentByEntityId<AttachmentComponent>(obj_id));
      ancestor_t_entity = component->GetParentTThis() * ancestor_t_entity;
      obj_id = component->GetParentId();
    }
    return ancestor_t_entity;
  };

  ASSIGN_OR_DIE(const Pose3d ancestor_t_a, get_ancestor_t_entity(a_handle));
  ASSIGN_OR_DIE(const Pose3d ancestor_t_b, get_ancestor_t_entity(b_handle));

  return ancestor_t_a.inverse() * ancestor_t_b;
}

// Fallback to old behavior if time == absl::UnixEpoch()
absl::Status World::GetTransform(AttachmentEntityId a_handle,
                                 AttachmentEntityId b_handle, absl::Time t,
                                 tf2::Stamped<Pose3d>& output) const {
  Pose3d intermediate;
  geometry_msgs::msg::TransformStamped ros_output;
  INTR_ASSIGN_OR_RETURN(std::string source_frame,
                        EntityIdToTfFrameName(a_handle));
  INTR_ASSIGN_OR_RETURN(std::string target_frame,
                        EntityIdToTfFrameName(b_handle));
  if (t == absl::UnixEpoch()) {
    intermediate = this->GetTransform(a_handle, b_handle);
    output.setData(intermediate);
    output.frame_id_ = ros_output.header.frame_id;
  } else {
    int64_t seconds = absl::ToUnixSeconds(t);
    int64_t nanos = absl::ToUnixNanos(t - absl::Seconds(seconds));
    tf2::TimePoint rt = tf2_ros::fromRclcpp(rclcpp::Time(seconds, nanos));
    try {
      ros_output = inner_world_->GetBufferCore().lookupTransform(
          source_frame, target_frame, rt);
    } catch (tf2::ConnectivityException& ex) {
      return absl::FailedPreconditionError(ex.what());
    } catch (tf2::LookupException& ex) {
      return absl::NotFoundError(ex.what());
    } catch (tf2::ExtrapolationException& ex) {
      return absl::OutOfRangeError(ex.what());
    } catch (tf2::InvalidArgumentException& ex) {
      return absl::InvalidArgumentError(ex.what());
    } catch (tf2::TimeoutException& ex) {
      return absl::DeadlineExceededError(ex.what());
    } catch (tf2::TransformException& ex) {
      return absl::InternalError(ex.what());
    }
    output.stamp_ = tf2_ros::fromMsg(ros_output.header.stamp);
    output.frame_id_ = ros_output.header.frame_id;
    // Custom conversion + setData ??
    // TODO(tullyfoote) validate Pose3d conversion below
    Pose3d output_trasform;
    tf2::fromMsg(ros_output.transform, output_trasform);
    output.setData(output_trasform);
  }
  return absl::OkStatus();
}

std::optional<std::string> World::GetResourceName(
    const WorldEntity& entity) const {
  if (absl::StatusOr<const CollectionsMemberComponent*> collections_member =
          entity.GetComponent<CollectionsMemberComponent>();
      collections_member.ok() && ((*collections_member) != nullptr)) {
    for (const auto& [parent_id, _] :
         (*collections_member)->GetParentCollectionsIdToTypesMap()) {
      if (absl::StatusOr<const PPRComponent*> parent_ppr =
              GetComponentByEntityId<PPRComponent>(parent_id);
          parent_ppr.ok() && ((*parent_ppr) != nullptr)) {
        if ((*parent_ppr)->ResourceName().has_value()) {
          return std::string((*parent_ppr)->ResourceName().value());
        }
      }
    }
  }
  return std::nullopt;
}

std::optional<std::string> World::GetTfPrefix(const WorldEntity& entity) const {
  auto member_comp_or = entity.GetComponent<CollectionsMemberComponent>();
  if (!member_comp_or.ok() || *member_comp_or == nullptr) {
    return RootObjectName().value();
  }
  const auto* member_comp = *member_comp_or;

  for (const auto& [parent_id, _] :
       member_comp->GetParentCollectionsIdToTypesMap()) {
    auto parent_or = GetEntityById(parent_id);
    if (!parent_or.ok()) continue;

    // TODO(b/493934163): Do not only use object name for tf prefix for non
    // globally uniquely named objects.
    std::string object_name =
        TryGetObjectNameFromCollection(parent_id).value_or(
            absl::StrCat(parent_id.value()));

    return object_name;
  }

  return RootObjectName().value();
}

absl::StatusOr<std::string> World::EntityIdToTfFrameName(
    EntityId entity_id) const {
  if (entity_id == kRootEntityId) {
    return std::string(kRootEntityNameLower);
  }

  INTR_ASSIGN_OR_RETURN(const WorldEntity* entity, GetEntityById(entity_id));

  // Substitutes with entity ID if the entity has no local name.
  const std::string name = entity->GetLocalName().empty()
                               ? absl::StrCat(entity_id.value())
                               : entity->GetLocalName();

  // look up the collection that this entity belongs to, if any, to try to build
  // a TF frame name like "resource_name/object_name/name"
  //
  // LocalName might not be unique, but resource name must be, and most entities
  // are associated with a resource, so most TF frame names will be unique.
  if (std::optional<std::string> prefix = GetTfPrefix(*entity);
      prefix.has_value()) {
    return absl::StrCat(*prefix, "/", name);
  }

  return name;
}

absl::StatusOr<std::string> World::EntityIdToFullyQualifiedTfFrameName(
    EntityId entity_id, WorldHashMap<EntityId, std::string>*
                            entity_id_to_fully_qualified_frame_name) const {
  // Base cases.
  if (entity_id == kRootEntityId) {  // Is root.
    return std::string(kRootEntityNameLower);
  }
  if (entity_id_to_fully_qualified_frame_name != nullptr) {  // Memoized.
    auto it = entity_id_to_fully_qualified_frame_name->find(entity_id);
    if (it != entity_id_to_fully_qualified_frame_name->end()) {
      return it->second;
    }
  }

  INTR_ASSIGN_OR_RETURN(std::string frame_name,
                        EntityIdToTfFrameName(entity_id));
  INTR_ASSIGN_OR_RETURN(const WorldEntity* entity, GetEntityById(entity_id));

  std::string parent_fully_qualified_frame_name;
  if (!entity->HasComponent<AttachmentComponent>()) {  // No parent.
    parent_fully_qualified_frame_name = "";
  } else {
    INTR_ASSIGN_OR_RETURN(const AttachmentComponent* attachment,
                          entity->GetComponent<AttachmentComponent>());
    INTR_ASSIGN_OR_RETURN(  // Recursive call for parent.
        parent_fully_qualified_frame_name,
        EntityIdToFullyQualifiedTfFrameName(
            attachment->GetParentId(),
            entity_id_to_fully_qualified_frame_name));

    // If the parent shares the resource, strip the resource from the frame_name
    // to avoid double-prepending.
    //
    // For example, if the parent is:
    //   - Parent: "resource_A/parent"
    //   - Child: "resource_A/child"
    //
    // Then fully qualified should be:
    //   - "resource_A/parent/child"
    //
    // Not:
    //   - "resource_A/parent/resource_A/child"
    if (std::optional<std::string> prefix = GetTfPrefix(*entity);
        prefix.has_value()) {
      INTR_ASSIGN_OR_RETURN(const WorldEntity* parent_entity,
                            GetEntityById(attachment->GetParentId()));
      std::optional<std::string> parent_prefix = GetTfPrefix(*parent_entity);
      if (*prefix == RootObjectName().value() ||
          (parent_prefix.has_value() && *parent_prefix == *prefix)) {
        frame_name = std::string(
            absl::StripPrefix(frame_name, absl::StrCat(*prefix, "/")));
      }
    }
  }

  std::string fully_qualified_frame_name =
      parent_fully_qualified_frame_name.empty()
          ? frame_name
          : absl::StrCat(parent_fully_qualified_frame_name, "/", frame_name);

  // Update the memoization map if we need to.
  if (entity_id_to_fully_qualified_frame_name != nullptr) {
    entity_id_to_fully_qualified_frame_name->insert(
        {entity_id, fully_qualified_frame_name});
  }

  return fully_qualified_frame_name;
}

absl::StatusOr<TFMessage> World::ConstructTfMessage() const {
  const auto now = absl::Now();

  TFMessage tf_message;
  for (const AttachmentEntityId& entity_id :
       GetTypedEntityIds<AttachmentEntityId>()) {
    // If this is the root entity_id, we don't want to create a TF transform for
    // it, because it has no parent in the TF tree (by definition)
    if (entity_id == kRootEntityId) {
      continue;
    }

    INTR_ASSIGN_OR_RETURN(
        const auto* attachment_component,
        GetComponentByEntityId<AttachmentComponent>(entity_id));

    INTR_ASSIGN_OR_RETURN(
        const auto parent_frame_id,
        EntityIdToTfFrameName(attachment_component->GetParentId()));

    INTR_ASSIGN_OR_RETURN(const auto child_frame_id,
                          EntityIdToTfFrameName(entity_id));

    intrinsic_proto::TransformStamped ts_proto;
    ts_proto.mutable_header()->set_frame_id(parent_frame_id);
    ts_proto.set_child_frame_id(child_frame_id);

    auto timestamp = now;
    INTR_ASSIGN_OR_RETURN(*ts_proto.mutable_header()->mutable_stamp(),
                          FromAbslTime(timestamp));

    auto transform = attachment_component->GetParentTThis();
    *(ts_proto.mutable_transform()->mutable_translation()) =
        ToVectorProto(transform.translation());
    *(ts_proto.mutable_transform()->mutable_rotation()) =
        ToProto(transform.quaternion());
    tf_message.mutable_transforms()->Add(std::move(ts_proto));
  }
  return tf_message;
}

absl::StatusOr<intrinsic_proto::world::TFAssociations>
World::ConstructTfAssociations(bool strip_inline_renderables) const {
  intrinsic_proto::world::TFAssociations associations;
  WorldHashMap<EntityId, std::string> frame_id_memo_map;

  for (const AttachmentEntityId& entity_id :
       GetTypedEntityIds<AttachmentEntityId>()) {
    if (entity_id == kRootEntityId) {
      continue;
    }

    INTR_ASSIGN_OR_RETURN(const std::string frame_id,
                          EntityIdToTfFrameName(entity_id));
    INTR_ASSIGN_OR_RETURN(
        const std::string fully_qualified_frame_id,
        EntityIdToFullyQualifiedTfFrameName(entity_id, &frame_id_memo_map));

    intrinsic_proto::world::FrameAssociation& association =
        (*associations.mutable_frame_associations())[frame_id];
    association.set_fully_qualified_tf_frame(fully_qualified_frame_id);

    if (auto geo_comp = GetComponentByEntityId<GeometryComponent>(entity_id);
        geo_comp.ok()) {
      INTR_ASSIGN_OR_RETURN(*association.mutable_geometry(),
                            (*geo_comp)->ToProto());

      // Strip heavy renderables from the inline geometry representation if
      // present. This publisher operates at a relatively high frequency (10Hz)
      // and is intended as a lightweight metadata stream. Including
      // multi-megabyte glTF strings here would cause extreme performance and
      // bandwidth issues. Consumers of this topic (like Rerun visualizers)
      // should fetch renderables out-of-band via CAS or the
      // VisualizationContext payload, using the fingerprint or refs.
      if (strip_inline_renderables) {
        for (auto& [name, geo_set] :
             *association.mutable_geometry()->mutable_named_geometries()) {
          for (auto& [geo_name, transformed_geo] :
               *geo_set.mutable_named_geometries()) {
            if (transformed_geo.has_geometry() &&
                transformed_geo.geometry().has_inline_geometry_data()) {
              transformed_geo.mutable_geometry()
                  ->mutable_inline_geometry_data()
                  ->clear_renderable();
              transformed_geo.mutable_geometry()
                  ->mutable_inline_geometry_data()
                  ->clear_generated_renderable();
            }
          }
        }
      }
    }
  }
  return associations;
}

absl::Status World::UpdateTransformBuffer(
    const intrinsic_proto::TFMessage& tf_message) const {
  // If an error occurs during this loop, save it to return later, but
  // continue updating the buffer with as many frames as possible.
  std::string bad_transforms;
  for (const intrinsic_proto::TransformStamped& ts_proto :
       tf_message.transforms()) {
    geometry_msgs::msg::TransformStamped tf_buffer_input = tf2::toMsg(ts_proto);
    if (!inner_world_->GetBufferCore().setTransform(tf_buffer_input,
                                                    "world_polling")) {
      absl::StrAppend(&bad_transforms, ts_proto.header().frame_id(), "->",
                      ts_proto.child_frame_id(), " ");
    }
  }
  if (!bad_transforms.empty()) {
    return absl::InternalError("Unable to set transform(s): " + bad_transforms);
  }
  return absl::OkStatus();
}

absl::StatusOr<AttachmentEntityId> World::FindCommonAncestor(
    AttachmentEntityId a_handle, AttachmentEntityId b_handle) const {
  if (a_handle == kInvalidEntityId || b_handle == kInvalidEntityId) {
    return absl::InvalidArgumentError(
        "FindCommonAncestor called with invalid handle");
  }
  if (a_handle == kRootEntityId || b_handle == kRootEntityId) {
    return kRootEntityId;
  }

  // If they are the same we can early out without doing a lot of work.
  if (a_handle == b_handle) {
    return a_handle;
  }

  WorldHashSet<AttachmentEntityId> obj_a_to_root{a_handle};
  for (AttachmentEntityId obj_id = a_handle; obj_id != kRootEntityId;) {
    if (obj_id == b_handle) {
      // Return early to skip the search up to root.
      return obj_id;
    }

    INTR_ASSIGN_OR_RETURN(auto component,
                          GetComponentByEntityId<AttachmentComponent>(obj_id));
    const AttachmentEntityId parent_id = component->GetParentId();

    obj_id = parent_id;
    obj_a_to_root.insert(parent_id);
  }

  for (AttachmentEntityId obj_id = b_handle; obj_id != kInvalidEntityId;) {
    INTR_ASSIGN_OR_RETURN(auto component,
                          GetComponentByEntityId<AttachmentComponent>(obj_id));
    if (obj_a_to_root.contains(obj_id)) {
      return obj_id;
    }
    obj_id = component->GetParentId();
  }

  // If we reach this point, one or both objects are not attached to the world
  // root.
  return intrinsic::InternalErrorBuilder()
         << "no common ancestor found between " << a_handle.value() << " and "
         << b_handle.value();
}

WorldHashMap<LabelId, WorldHashSet<EntityId>> World::GetLabelsMap() const {
  WorldHashMap<LabelId, WorldHashSet<EntityId>> labels;
  ASSIGN_OR_DIE(auto all_entities, inner_world_->GetAllEntities());
  for (const auto& [entity_id, entity] : all_entities) {
    for (const auto& label : entity->GetLabels()) {
      labels[label].insert(entity_id);
    }
  }

  return labels;
}

absl::Status World::MarkTransformInaccuracy(AttachmentEntityId a_handle,
                                            AttachmentEntityId b_handle,
                                            bool inaccurate) {
  if (a_handle == kInvalidEntityId) {
    return absl::InvalidArgumentError("Invalid entity id.");
  }

  if (b_handle == kInvalidEntityId) {
    return absl::InvalidArgumentError("Invalid entity id.");
  }

  INTR_ASSIGN_OR_RETURN(auto a_attachment,
                        GetComponentByEntityId<AttachmentComponent>(a_handle));
  INTR_ASSIGN_OR_RETURN(auto b_attachment,
                        GetComponentByEntityId<AttachmentComponent>(b_handle));

  if (a_attachment->GetParentId() == b_handle) {
    a_attachment->MarkInaccurate(inaccurate);
  } else if (b_attachment->GetParentId() == a_handle) {
    b_attachment->MarkInaccurate(inaccurate);
  } else {
    return ::intrinsic::InvalidArgumentErrorBuilder()
           << "MarkTransformInaccuracy cannot be called on entities that "
              "are not directly attached. Entity '"
           << a_handle << "' and '" << b_handle
           << "' are not directly attached";
  }

  return absl::OkStatus();
}

WorldHashSet<std::pair<AttachmentEntityId, AttachmentEntityId>>
World::GetInaccurateTransforms(AttachmentEntityId a_handle,
                               AttachmentEntityId b_handle) const {
  CHECK_NE(a_handle, kInvalidEntityId) << "Invalid entity id.";
  CHECK_NE(b_handle, kInvalidEntityId) << "Invalid entity id.";
  CHECK(inner_world_->HasEntity(a_handle))
      << "Cannot call get inaccurate with an id that is not part of this "
         "world";
  CHECK(inner_world_->HasEntity(b_handle))
      << "Cannot call get inaccurate with an id that is not part of this "
         "world";

  if (a_handle == b_handle) {
    return {};
  }

  // First find the common ancestor. This is less efficient than other
  // approaches since it will require two passes up the tree but it simplifies
  // the code. If we start to use this functionality in a performance critical
  // code path we can optimize it further by doing a single pass and accumulate
  // the result as we go.
  ASSIGN_OR_DIE(const AttachmentEntityId common_ancestor,
                FindCommonAncestor(a_handle, b_handle));

  // Start at a_handle and then b_handle and go up the tree to the common root.
  // Gathering all of the inaccurate connections.
  WorldHashSet<std::pair<AttachmentEntityId, AttachmentEntityId>> result;
  for (const AttachmentEntityId start_handle : {a_handle, b_handle}) {
    AttachmentEntityId obj_id = start_handle;
    while (obj_id != common_ancestor) {
      ASSIGN_OR_DIE(auto component,
                    GetComponentByEntityId<AttachmentComponent>(obj_id));
      const AttachmentEntityId parent_id = component->GetParentId();

      if (component->IsInaccurate()) {
        if (start_handle == a_handle) {
          result.emplace(obj_id, parent_id);
        } else {
          result.emplace(parent_id, obj_id);
        }
      }

      obj_id = parent_id;
    }
  }

  return result;
}

absl::StatusOr<AttachmentEntityId> World::UpdateIndirectTransform(
    AttachmentEntityId a_handle, AttachmentEntityId b_handle,
    const Pose3d& a_t_b, std::optional<absl::Time> timestamp) {
  if (a_handle == kInvalidEntityId) {
    return absl::InvalidArgumentError("Invalid entity id.");
  }

  if (b_handle == kInvalidEntityId) {
    return absl::InvalidArgumentError("Invalid entity id.");
  }

  const auto inaccurate_transforms =
      GetInaccurateTransforms(a_handle, b_handle);
  if (inaccurate_transforms.empty()) {
    return intrinsic::NotFoundErrorBuilder()
           << "No inaccurate links between " << a_handle << " and " << b_handle
           << " while updating an indirect transform.";
  }

  if (inaccurate_transforms.size() != 1) {
    return ::intrinsic::InvalidArgumentErrorBuilder()
           << "Too many inaccurate links(" << inaccurate_transforms.size()
           << ") between " << a_handle << " and " << b_handle
           << " while updating an indirect transform.";
  }

  const auto left_obj_id = inaccurate_transforms.begin()->first;
  const auto right_obj_id = inaccurate_transforms.begin()->second;
  CHECK_NE(left_obj_id, kInvalidEntityId) << "Invalid entity id.";
  CHECK_NE(right_obj_id, kInvalidEntityId) << "Invalid entity id.";

  const auto left_t_a = GetTransform(left_obj_id, a_handle);
  const auto b_t_right = GetTransform(b_handle, right_obj_id);

  const auto left_t_right = left_t_a * a_t_b * b_t_right;

  ASSIGN_OR_DIE(auto attachment_left,
                GetComponentByEntityId<AttachmentComponent>(left_obj_id));
  ASSIGN_OR_DIE(auto attachment_right,
                GetComponentByEntityId<AttachmentComponent>(right_obj_id));

  AttachmentComponent* updated_component = nullptr;
  AttachmentEntityId updated_entity;
  Pose3d new_parent_t_this;
  if (attachment_left->GetParentId() == right_obj_id) {
    updated_component = attachment_left;
    new_parent_t_this = left_t_right.inverse();
    updated_entity = left_obj_id;
  } else if (attachment_right->GetParentId() == left_obj_id) {
    updated_component = attachment_right;
    new_parent_t_this = left_t_right;
    updated_entity = right_obj_id;
  } else {
    return intrinsic::InternalErrorBuilder()
           << "UpdateIndirectTransform could not update inaccurate transform "
              "between entities ('"
           << left_obj_id << "' and '" << right_obj_id
           << "') that are "
              "not directly attached.";
  }
  std::optional<absl::Time> existing_timestamp =
      updated_component->GetTimestamp();
  if (timestamp.has_value() && existing_timestamp.has_value() &&
      *timestamp < *existing_timestamp) {
    return absl::InvalidArgumentError(absl::Substitute(
        "Cannot update transform for '$0' because its timestamp $1 is newer "
        "than the given timestamp $2",
        updated_entity.value(), absl::FormatTime(*existing_timestamp),
        absl::FormatTime(*timestamp)));
  }

  updated_component->SetParentTThis(new_parent_t_this, timestamp);
  updated_component->MarkInaccurate(false);

  return updated_entity;
}

absl::StatusOr<CollisionEntityId> World::CreateCollisionComponent(EntityId id) {
  if (id == kInvalidEntityId) {
    return absl::InvalidArgumentError("Invalid entity id.");
  }

  INTR_ASSIGN_OR_RETURN(auto entity, GetEntityById(id));
  if (!entity->HasComponent<CollisionComponent>()) {
    INTR_RETURN_IF_ERROR(entity->CreateComponent<CollisionComponent>());
  }

  return CollisionEntityId(id);
}

absl::Status World::AddExclusionPair(PhysicalEntityId first,
                                     PhysicalEntityId second) {
  INTR_ASSIGN_OR_RETURN(CollisionComponent * collision_a,
                        GetComponentByEntityId<CollisionComponent>(first));
  INTR_ASSIGN_OR_RETURN(CollisionComponent * collision_b,
                        GetComponentByEntityId<CollisionComponent>(second));
  collision_a->AddExclusionId(second);
  collision_b->AddExclusionId(first);
  return absl::OkStatus();
}

absl::Status World::RemoveExclusionPair(PhysicalEntityId first,
                                        PhysicalEntityId second) {
  INTR_ASSIGN_OR_RETURN(CollisionComponent * collision_a,
                        GetComponentByEntityId<CollisionComponent>(first));
  INTR_ASSIGN_OR_RETURN(CollisionComponent * collision_b,
                        GetComponentByEntityId<CollisionComponent>(second));
  collision_a->RemoveExclusionId(second);
  collision_b->RemoveExclusionId(first);
  return absl::OkStatus();
}

absl::Status World::RemoveNonmutualExclusions() {
  for (const CollisionEntityId& id : GetTypedEntityIds<CollisionEntityId>()) {
    INTR_ASSIGN_OR_RETURN(CollisionComponent * collision,
                          GetComponentByEntityId<CollisionComponent>(id));

    // Copy exclusions. We cannot iterate and erase simultaneously with only
    // const access to the set itself.
    WorldHashSet<PhysicalEntityId> exclusions = collision->GetExclusions();

    for (const PhysicalEntityId& other_id : exclusions) {
      bool mutual = false;

      absl::StatusOr<WorldEntity*> other_entity = GetEntityById(other_id);

      if (other_entity.ok() &&
          (*other_entity)->HasComponent<CollisionComponent>()) {
        INTR_ASSIGN_OR_RETURN(
            CollisionComponent * other_collision,
            (*other_entity)->GetComponent<CollisionComponent>());
        if (other_collision->GetExclusions().contains(
                PhysicalEntityId(id.value()))) {
          mutual = true;
        }
      } else if (!absl::IsNotFound(other_entity.status())) {
        INTR_RETURN_IF_ERROR(other_entity.status());
      }

      if (!mutual) {
        collision->RemoveExclusionId(other_id);
      }
    }
  }
  return absl::OkStatus();
}

void World::ClearExclusionPairs() {
  for (const auto& object_id : GetTypedEntityIds<CollisionComponentType>()) {
    ASSIGN_OR_DIE(auto collision,
                  GetComponentByEntityId<CollisionComponent>(object_id));
    collision->ClearExclusions();
  }
}

std::vector<std::pair<PhysicalEntityId, PhysicalEntityId>>
World::GetExclusionPairs(bool filter_empty_collision_geometry) const {
  std::vector<std::pair<PhysicalEntityId, PhysicalEntityId>> result;
  for (const auto& object_id : GetTypedEntityIds<CollisionComponentType>()) {
    if (filter_empty_collision_geometry &&
        HasEmptyCollisionGeometry(object_id)) {
      continue;
    }
    ASSIGN_OR_DIE(auto collision,
                  GetComponentByEntityId<CollisionComponent>(object_id));
    for (const auto& other_id : collision->GetExclusions()) {
      if (filter_empty_collision_geometry &&
          HasEmptyCollisionGeometry(other_id)) {
        continue;
      }

      result.emplace_back(object_id.value(), other_id);
    }
  }
  return result;
}

bool World::IsCollisionExcluded(PhysicalEntityId object_id_1,
                                PhysicalEntityId object_id_2) const {
  ASSIGN_OR_DIE(auto collision,
                GetComponentByEntityId<CollisionComponent>(object_id_1));
  return collision->GetExclusions().contains(object_id_2);
}

WorldHashSet<PhysicalEntityId> World::GetExcludedObjectdIds(
    const WorldHashSet<PhysicalEntityId>& object_ids) const {
  WorldHashSet<PhysicalEntityId> result;
  for (const auto& object_id : object_ids) {
    ASSIGN_OR_DIE(auto collision,
                  GetComponentByEntityId<CollisionComponent>(object_id));
    const WorldHashSet<PhysicalEntityId>& exclusions =
        collision->GetExclusions();
    result.insert(exclusions.begin(), exclusions.end());
  }
  return result;
}

absl::StatusOr<std::shared_ptr<CollisionChecker>> World::GetCollisionChecker(
    const WorldHashSet<PhysicalEntityId>& dynamic_objects,
    const intrinsic_proto::RuleSet& rule_set,
    const intrinsic_proto::world::CollisionCheckerConfig&
        collision_checker_config) const {
  return inner_world_->GetCollisionChecker(*this, dynamic_objects, rule_set,
                                           collision_checker_config);
}

absl::StatusOr<std::shared_ptr<CollisionChecker>> World::GetCollisionChecker(
    const WorldHashSet<PhysicalEntityId>& dynamic_objects,
    const intrinsic_proto::world::CollisionCheckerConfig&
        collision_checker_config) const {
  return inner_world_->GetCollisionChecker(*this, dynamic_objects,
                                           collision_checker_config);
}

NamedGeometrySet World::GetLocallyTransformedShapes(PhysicalEntityId object_id,
                                                    bool merge) const {
  auto geo_component = GetComponentByEntityId<GeometryComponent>(object_id);
  if (!geo_component.ok()) {
    ASSIGN_OR_DIE(auto object, GetEntityById(object_id));
    LOG_EVERY_N_SEC(WARNING, 1)
        << "PhysicalEntityId '" << object_id.value() << "' (local name: '"
        << object->GetLocalName() << "') does not have a Geometry component.";
    return NamedGeometrySet();
  }
  auto geo_set = (*geo_component)->GetGeometry(kKindCollisionGeometry);
  if (!geo_set.ok()) {
    return NamedGeometrySet();
  }

  if (merge) {
    ASSIGN_OR_DIE(auto geo, MaybeFuseGeometries(*geo_set));
    return geo;
  }
  return *geo_set;
}

NamedGeometrySet World::GetGlobalyTransformedShapes(PhysicalEntityId object_id,
                                                    bool merge) const {
  NamedGeometrySet object_geo;
  NamedGeometrySet merged_geo = GetLocallyTransformedShapes(object_id, merge);
  const Pose3d world_t_object = GetTransform(kRootEntityId, object_id);

  // TODO(stoyang): Memoize this.
  object_geo.reserve(merged_geo.size());
  for (auto& [name, geo_component] : merged_geo) {
    // Apply the current transform on the object.
    const auto world_t_shape = world_t_object * geo_component.ref_t_shape();
    object_geo.emplace(
        name,
        TransformedGeometry(std::move(geo_component).shape(), world_t_shape));
  }

  if (merge) {
    ASSIGN_OR_DIE(auto named_geo_set, MaybeFuseGeometries(object_geo));
    return named_geo_set;
  } else {
    return object_geo;
  }
}

NamedGeometrySet World::GetStaticSpatialTreeInWorldSpace(
    const WorldHashSet<PhysicalEntityId>& static_object_ids) const {
  NamedGeometrySet object_geos;

  // Create new TransformedGeometry with their new positions.
  for (const auto& object_id : static_object_ids) {
    const NamedGeometrySet merged_geo =
        GetGlobalyTransformedShapes(object_id, false);
    for (const auto& [name, geo] : merged_geo) {
      const std::string base_name = absl::StrCat(object_id.value(), "-", name);
      std::string object_level_name = base_name;
      for (int i = 0; object_geos.contains(object_level_name); ++i) {
        object_level_name = absl::StrCat(base_name, "-", i);
      }
      object_geos.emplace(object_level_name, geo);
    }
  }

  ASSIGN_OR_DIE(NamedGeometrySet geo_set, MaybeFuseGeometries(object_geos));
  return geo_set;
}

NamedGeometrySet World::GetSpatialTreeInModelSpace(
    PhysicalEntityId object_id) const {
  auto spatial_tree_iter = model_space_spatial_trees_->find(object_id);
  if (spatial_tree_iter != model_space_spatial_trees_->end()) {
    return spatial_tree_iter->second;
  }

  NamedGeometrySet geo_set =
      GetLocallyTransformedShapes(object_id, /*merge=*/true);

  model_space_spatial_trees_->emplace(object_id, geo_set);
  return geo_set;
}

bool World::HasEmptyCollisionGeometry(const EntityId& id) const {
  auto entity_or_status = GetEntityById(id);
  if (!entity_or_status.ok()) {
    VLOG(1) << "Couldn't get entity: " << entity_or_status.status();
    return true;
  }
  const auto* ent = entity_or_status.value();

  if (ent->HasComponent<CollisionComponent>() &&
      ent->HasComponent<GeometryComponent>()) {
    ASSIGN_OR_DIE(auto* geo_component, ent->GetComponent<GeometryComponent>());
    return !geo_component->HasGeometry(kKindCollisionGeometry);
  }

  return true;  // Not a GeometryComponent or CollisionComponent
}

absl::StatusOr<NamedGeometrySet> World::GetGeometryForEntity(
    EntityId id, absl::string_view kind) const {
  INTR_ASSIGN_OR_RETURN(auto entity, GetEntityById(id));
  INTR_ASSIGN_OR_RETURN(auto* geo_component,
                        entity->GetComponent<GeometryComponent>());
  return geo_component->GetGeometry(kind);
}

intrinsic_proto::RuleSet World::GetDefaultRuleSet() const {
  return inner_world_->GetDefaultRuleSet();
}

absl::Status World::SetDefaultRuleSet(
    const intrinsic_proto::RuleSet& rule_set) {
  return inner_world_->SetDefaultRuleSet(rule_set);
}

const WorldHashSet<GroupId>& World::GetGroupIds() const { return group_ids_; }

absl::Status World::AddGroupId(const GroupId& group_id) {
  if (group_ids_.contains(group_id)) {
    return AlreadyExistsErrorBuilder()
           << "GroupId \"" << group_id.value() << "\" already exists";
  }
  if (!FindByLabel(LabelId(group_id.value())).empty()) {
    return ::intrinsic::InvalidArgumentErrorBuilder()
           << "cannot add GroupId \"" << group_id.value()
           << "\"; equivalent LabelId already in use";
  }
  group_ids_.insert(group_id);
  return absl::OkStatus();
}

absl::Status World::RemoveGroupId(const GroupId& group_id) {
  if (!group_ids_.contains(group_id)) {
    return intrinsic::NotFoundErrorBuilder()
           << "GroupId \"" << group_id.value() << "\" does not exist";
  }
  std::set<LabelId> label_id_set;
  label_id_set.emplace(group_id.value());
  ASSIGN_OR_DIE(auto all_entities, inner_world_->GetAllEntities());
  for (const auto& [entity_id, entity] : all_entities) {
    INTR_RETURN_IF_ERROR(entity->RemoveLabels(label_id_set));
  }
  group_ids_.erase(group_id);
  return absl::OkStatus();
}

absl::Status World::RefreshDofParentTThis(JointEntityId joint_handle,
                                          std::optional<absl::Time> timestamp) {
  INTR_ASSIGN_OR_RETURN(WorldEntity * entity, GetEntityById(joint_handle));
  INTR_ASSIGN_OR_RETURN(auto* kin_component,
                        entity->GetComponent<KinematicsComponent>());
  INTR_ASSIGN_OR_RETURN(auto* attachment_comp,
                        entity->GetComponent<AttachmentComponent>());

  // Compute the DoF's derived value and inboard_t_outboard transform.
  const double dof_value = kin_component->GetRawValue();
  const eigenmath::Vector3d& axis = kin_component->GetAxis();
  Pose3d inboard_t_outboard;
  switch (kin_component->GetMotionType()) {
    case intrinsic_proto::world::KinematicsComponent::MOTION_TYPE_REVOLUTE:
      inboard_t_outboard = CreateAngleAxisPose(dof_value, axis);
      break;
    case intrinsic_proto::world::KinematicsComponent::MOTION_TYPE_PRISMATIC:
      inboard_t_outboard.translation() = axis * dof_value;
      break;
    case intrinsic_proto::world::KinematicsComponent::MOTION_TYPE_FIXED:
      break;
    default:
      return ::intrinsic::InvalidArgumentErrorBuilder()
             << "unhandled motion_type " << kin_component->GetMotionType();
  }

  // Finally set the DoF's parent_t_this.
  // Ref: go/intrinsic-choreographer-kinematic-tdd
  attachment_comp->SetParentTThis(kin_component->GetParentTInboard() *
                                      inboard_t_outboard *
                                      kin_component->GetOutboardTChild(),
                                  timestamp);
  return absl::OkStatus();
}

bool is_outside_limits_double(double raw_value, double lower_limit,
                              double upper_limit, double epsilon = 0.0001) {
  if (raw_value < lower_limit &&
      (std::abs(raw_value - lower_limit) >= epsilon)) {
    return true;
  } else if ((raw_value > upper_limit) &&
             (std::abs(raw_value - upper_limit) >= epsilon)) {
    return true;
  } else {
    return false;
  }
}

absl::Status World::CheckDofRawValue(JointEntityId joint_handle,
                                     double raw_value) const {
  INTR_ASSIGN_OR_RETURN(
      const KinematicsComponent* kin_component,
      GetComponentByEntityId<KinematicsComponent>(joint_handle));

  if (std::isnan(raw_value)) {
    return absl::InvalidArgumentError("raw_value cannot be NaN");
  }

  {  // Check that raw_value is within the system limits.
    auto [lower_limit, upper_limit] =
        kin_component->GetSystemRawValueFixedLimits();
    if (is_outside_limits_double(raw_value, lower_limit, upper_limit)) {
      return OutOfRangeErrorBuilder()
             << "new raw value " << raw_value << " for joint (ID "
             << joint_handle.value() << ") is outside system limits of ["
             << lower_limit << ", " << upper_limit << "]";
    }
  }

  {  // Check that raw_value is within the application limits.
    auto [lower_limit, upper_limit] =
        kin_component->GetApplicationRawValueFixedLimits();
    if (is_outside_limits_double(raw_value, lower_limit, upper_limit)) {
      return OutOfRangeErrorBuilder()
             << "new raw value " << raw_value << " for joint (ID "
             << joint_handle.value() << ") is outside application limits of ["
             << lower_limit << ", " << upper_limit << "]";
    }
  }

  return absl::OkStatus();
}

absl::Status World::SetDofRawValue(JointEntityId joint_handle, double raw_value,
                                   bool enforce_limits,
                                   std::optional<absl::Time> timestamp) {
  // Check that the new value is within the limits.
  if (enforce_limits) {
    INTR_RETURN_IF_ERROR(CheckDofRawValue(joint_handle, raw_value));
  }

  if (std::isnan(raw_value)) {
    return absl::InvalidArgumentError("raw_value cannot be NaN");
  }

  // Set the DoF's raw value and update its parent_t_this.
  INTR_ASSIGN_OR_RETURN(
      auto* kin_component,
      GetComponentByEntityId<KinematicsComponent>(joint_handle));
  kin_component->SetRawValue(raw_value, timestamp);
  return RefreshDofParentTThis(joint_handle, timestamp);
}

absl::StatusOr<std::vector<JointEntityId>> World::GetRobotDofs(
    RobotCollectionsEntityId robot_handle) const {
  INTR_ASSIGN_OR_RETURN(auto all_joints,
                        ValidateCollectionMembers<JointEntityId>(
                            robot_handle, CollectionsComponent::kJoints));
  std::vector<JointEntityId> ret;
  ret.reserve(all_joints.size());
  for (auto joint_handle : all_joints) {
    INTR_ASSIGN_OR_RETURN(
        const auto* kinematics_component,
        GetComponentByEntityId<KinematicsComponent>(joint_handle));
    // Skip fixed joints.
    if (kinematics_component->GetMotionType() ==
        intrinsic_proto::world::KinematicsComponent::MOTION_TYPE_FIXED) {
      continue;
    }
    ret.push_back(joint_handle);
  }
  return ret;
}

namespace {

absl::Status AppendLeavesFromRoot(
    const World* world,
    const WorldHashMap<AttachmentEntityId, WorldHashSet<AttachmentEntityId>>&
        parent_to_children,
    AttachmentEntityId root, WorldHashSet<AttachmentEntityId>* leaves) {
  WorldHashSet<AttachmentEntityId> visited_ids = {root};
  std::deque<AttachmentEntityId> nodes_to_visit = {root};
  while (!nodes_to_visit.empty()) {
    AttachmentEntityId current = nodes_to_visit.front();
    nodes_to_visit.pop_front();
    visited_ids.insert(current);

    auto iter = parent_to_children.find(current);
    if (iter == parent_to_children.end()) {
      leaves->insert(current);
      continue;
    }
    const bool has_link_or_joint_children =
        absl::c_any_of(iter->second, [world](AttachmentEntityId child) {
          return world->ValidateEntity<LinkEntityId>(child).ok() ||
                 world->ValidateEntity<JointEntityId>(child).ok();
        });
    for (const auto& child : iter->second) {
      if (visited_ids.contains(child)) {
        return intrinsic::NotFoundErrorBuilder()
               << "The kinematic system is a kinematic loop. Only kinematic "
                  "trees are supported. Loop encountered at id="
               << child;
      }

      // Only visit joint or link IDs, unless there are none, then visit all
      // children.
      if (!has_link_or_joint_children ||
          world->ValidateEntity<LinkEntityId>(child).ok() ||
          world->ValidateEntity<JointEntityId>(child).ok()) {
        nodes_to_visit.push_back(child);
      }
    }
  }
  return absl::OkStatus();
}

}  // namespace

absl::StatusOr<AttachmentEntityId> World::GetBaseLinkForRobotGroupId(
    const GroupId& group_id) const {
  INTR_ASSIGN_OR_RETURN(auto robot_ids,
                        GetRobotCollectionsEntityIdsForRobotGroupId(group_id));
  // Early out for the most common case of exactly 1 RobotCollectionsEntity.
  if (robot_ids->size() == 1) {
    return GetBaseLink(robot_ids->at(0));
  }

  // Otherwise, gather the candidate base links and find the root of those.
  WorldHashSet<EntityId> candidates;
  candidates.reserve(robot_ids->size());
  for (auto robot_id : *robot_ids) {
    INTR_ASSIGN_OR_RETURN(auto base_link_id, GetBaseLink(robot_id));
    candidates.insert(base_link_id);
  }
  return GetRootEntity(candidates);
}

absl::StatusOr<WorldHashSet<AttachmentEntityId>> World::GetFinalEntitiesOfRobot(
    CollectionsEntityId robot_id) const {
  WorldHashSet<AttachmentEntityId> part_id_set;

  // Remove the attachment frames as those are not intended to be used when
  // searching for the leafs of a robot.
  auto part_types = CollectionsComponent::RobotPartTypes();
  part_types.erase(CollectionsComponent::kAttachmentFrames);
  part_types.erase(CollectionsComponent::kCoordinateFrames);

  for (auto type : part_types) {
    INTR_ASSIGN_OR_RETURN(
        const auto ids,
        ValidateCollectionMembers<AttachmentEntityId>(robot_id, type));
    part_id_set.insert(ids.begin(), ids.end());
  }

  WorldHashMap<AttachmentEntityId, WorldHashSet<AttachmentEntityId>>
      parent_to_children;
  for (auto part_id : part_id_set) {
    INTR_ASSIGN_OR_RETURN(const auto* attachment_comp,
                          GetComponentByEntityId<AttachmentComponent>(part_id));
    AttachmentEntityId parent_id = attachment_comp->GetParentId();

    // Ignore parts that are parented to an Entity outside of the robot.
    if (!part_id_set.contains(parent_id)) {
      continue;
    }
    parent_to_children[parent_id].insert(part_id);
  }

  // Start from the root link and traverse the chain to the end.
  INTR_ASSIGN_OR_RETURN(AttachmentEntityId current_root,
                        GetRootEntity(robot_id));

  WorldHashSet<AttachmentEntityId> leaves;
  INTR_RETURN_IF_ERROR(
      AppendLeavesFromRoot(this, parent_to_children, current_root, &leaves));
  return leaves;
}

absl::StatusOr<AttachmentEntityId> World::GetFinalEntityOfRobotKinematicChain(
    CollectionsEntityId robot_id) const {
  auto tips_or_status = GetFinalEntitiesOfRobot(robot_id);
  if (!tips_or_status.ok()) {
    if (absl::StrContains(tips_or_status.status().message(),
                          "kinematic loop")) {
      return AttachmentEntityId(kInvalidEntityId);
    }
    return tips_or_status.status();
  }

  const auto& tips = tips_or_status.value();
  if (tips.size() != 1) {
    return AttachmentEntityId(kInvalidEntityId);
  }
  return *tips.begin();
}

absl::StatusOr<bool> World::IsChainKinematicChain(
    RobotCollectionsEntityId robot_id) const {
  INTR_ASSIGN_OR_RETURN(auto leaves, GetFinalEntitiesOfRobot(robot_id));
  return leaves.size() == 1;
}

absl::StatusOr<std::vector<RobotCollectionsEntityId>> World::GetRobotIdsInChain(
    AttachmentEntityId id1, AttachmentEntityId id2) const {
  std::vector<RobotCollectionsEntityId> robot_ids;

  // TODO(stoyang): The order of the two objects should not matter.
  INTR_ASSIGN_OR_RETURN(AttachmentEntityId ancestor,
                        FindCommonAncestor(id1, id2));
  if (ancestor != id1) {
    return absl::UnimplementedError(
        "GetRobotIdsInChain() requires id1 to be an ancestor of id2");
  }

  // Go through all of the robots and find if any of them can affect the
  // transform between the two given objects. If any of them are able to do this
  // we can use them as part of a view for this pair of input objects.
  for (RobotCollectionsEntityId robot_id :
       GetTypedEntityIds<RobotCollectionsEntityId>()) {
    INTR_ASSIGN_OR_RETURN(auto ee_ids, GetFinalEntitiesOfRobot(robot_id));

    auto base_id_or = GetBaseLink(robot_id);
    if (!base_id_or.ok()) {
      continue;
    }
    AttachmentEntityId base_id = base_id_or.value();

    // We filter out any objects that are not directly between the two input ids
    // and ordered in the correct way. This is a limitation of the current
    // implementation.
    if (base_id != ancestor) {
      INTR_ASSIGN_OR_RETURN(AttachmentEntityId common_ancestor,
                            FindCommonAncestor(ancestor, base_id));
      if (common_ancestor != ancestor) {
        continue;
      }
    }
    if (!ee_ids.contains(id2)) {
      bool robot_is_in_chain = false;
      for (auto ee_id : ee_ids) {
        INTR_ASSIGN_OR_RETURN(AttachmentEntityId common_ancestor,
                              FindCommonAncestor(ee_id, id2));
        if (common_ancestor == ee_id) {
          robot_is_in_chain = true;
          break;
        }
      }
      if (!robot_is_in_chain) {
        continue;
      }
    }
    robot_ids.push_back(robot_id);
  }

  // This is not the most efficient way to sort the robots but it is
  // conceptually the way you would sort the robots. This checks to see if the
  // base link of one robot is the ancestor of the other in order to determine
  // which one is 'less than' the other.
  std::sort(robot_ids.begin(), robot_ids.end(),
            [this](const RobotCollectionsEntityId& lhs,
                   const RobotCollectionsEntityId& rhs) {
              ASSIGN_OR_DIE(LinkEntityId lhs_base, GetBaseLink(lhs));
              ASSIGN_OR_DIE(LinkEntityId rhs_base, GetBaseLink(rhs));
              if (lhs_base == rhs_base) return false;
              ASSIGN_OR_DIE(AttachmentEntityId ancestor,
                            FindCommonAncestor(lhs_base, rhs_base));
              return ancestor == lhs_base;
            });

  return robot_ids;
}

namespace {

absl::StatusOr<std::vector<JointEntityId>> GetJointsBetweenEntities(
    const World& world, AttachmentEntityId id1, AttachmentEntityId id2) {
  if (id1 == id2) {
    return absl::NotFoundError("No DOF view possible id1 == id2");
  }

  // TODO(stoyang): The order of the two objects should not matter.
  INTR_ASSIGN_OR_RETURN(AttachmentEntityId ancestor,
                        world.FindCommonAncestor(id1, id2));
  if (ancestor != id1) {
    return absl::UnimplementedError(
        "GetDofKinematicView() currently requires id1 to be an ancestor of "
        "id2");
  }

  AttachmentEntityId current = id2;

  std::vector<JointEntityId> joints;
  while (current != id1) {
    INTR_ASSIGN_OR_RETURN(
        auto attachment,
        world.GetComponentByEntityId<AttachmentComponent>(current));
    current = attachment->GetParentId();
    if (current == kInvalidEntityId) {
      return absl::InvalidArgumentError(
          "Got an invalid parent along the chain");
    }

    auto maybe_joint = world.ValidateEntity<JointEntityId>(current);
    if (!maybe_joint.ok()) {
      continue;
    }

    INTR_ASSIGN_OR_RETURN(
        auto joint, world.GetComponentByEntityId<KinematicsComponent>(current));

    if (joint->GetMotionType() !=
        intrinsic_proto::world::KinematicsComponent::MOTION_TYPE_FIXED) {
      joints.emplace(joints.begin(), current.value());
    }
  }

  return std::move(joints);
}

}  // namespace

absl::StatusOr<std::vector<std::unique_ptr<const CartesianKinematicView>>>
World::GetCartesianKinematicViews(AttachmentEntityId id1,
                                  AttachmentEntityId id2) const {
  if (id1 == id2) {
    return std::vector<std::unique_ptr<const CartesianKinematicView>>();
  }

  INTR_ASSIGN_OR_RETURN(auto robot_ids, GetRobotIdsInChain(id1, id2));
  std::vector<std::unique_ptr<const CartesianKinematicView>> result;
  result.reserve(robot_ids.size());
  for (RobotCollectionsEntityId robot_id : robot_ids) {
    INTR_ASSIGN_OR_RETURN(bool is_chain, IsChainKinematicChain(robot_id));
    if (!is_chain) {
      VLOG(1) << "Ignoring non-kinematic chain robot "
              << GetLocalNameForEntityById(robot_id) << " id=" << robot_id;
      continue;
    }
    result.push_back(
        std::make_unique<
            entity_robots_details::ConstEntityRobotCartesianKinematicView>(
            static_cast<const entity_aspect_world_details::EntityWorld*>(this),
            std::make_pair(id1, id2), robot_id));
  }
  return result;
}

absl::StatusOr<std::vector<std::unique_ptr<CartesianKinematicView>>>
World::GetCartesianKinematicViews(AttachmentEntityId id1,
                                  AttachmentEntityId id2) {
  if (id1 == id2) {
    return std::vector<std::unique_ptr<CartesianKinematicView>>();
  }
  INTR_ASSIGN_OR_RETURN(auto robot_ids, GetRobotIdsInChain(id1, id2));

  std::vector<std::unique_ptr<CartesianKinematicView>> result;
  result.reserve(robot_ids.size());
  for (RobotCollectionsEntityId robot_id : robot_ids) {
    INTR_ASSIGN_OR_RETURN(bool is_chain, IsChainKinematicChain(robot_id));
    if (!is_chain) {
      VLOG(1) << "Ignoring non-kinematic chain robot "
              << GetLocalNameForEntityById(robot_id) << " id=" << robot_id;
      continue;
    }
    result.push_back(std::make_unique<
                     entity_robots_details::EntityRobotCartesianKinematicView>(
        static_cast<entity_aspect_world_details::EntityWorld*>(this),
        std::make_pair(id1, id2), robot_id));
  }
  return result;
}

absl::StatusOr<std::unique_ptr<const CartesianKinematicView>>
World::GetCartesianKinematicView(AttachmentEntityId id1,
                                 AttachmentEntityId id2) const {
  if (id1 == id2) {
    return std::unique_ptr<CartesianKinematicView>();
  }

  auto cart_view = std::make_unique<
      entity_robots_details::ConstChainedRobotCartesianKinematicView>(
      static_cast<const entity_aspect_world_details::EntityWorld*>(this),
      PhysicalEntityId(id1.value()), PhysicalEntityId(id2.value()));

  INTR_RETURN_IF_ERROR(cart_view->Initialize());
  return cart_view;
}

absl::StatusOr<std::unique_ptr<CartesianKinematicView>>
World::GetCartesianKinematicView(AttachmentEntityId id1,
                                 AttachmentEntityId id2) {
  if (id1 == id2) {
    return std::unique_ptr<CartesianKinematicView>();
  }

  auto cart_view = std::make_unique<
      entity_robots_details::ChainedRobotCartesianKinematicView>(
      static_cast<entity_aspect_world_details::EntityWorld*>(this),
      PhysicalEntityId(id1.value()), PhysicalEntityId(id2.value()));

  INTR_RETURN_IF_ERROR(cart_view->Initialize());
  return cart_view;
}

absl::StatusOr<std::unique_ptr<DofKinematicView>> World::GetDofKinematicView(
    AttachmentEntityId id1, AttachmentEntityId id2) {
  INTR_ASSIGN_OR_RETURN(std::vector<JointEntityId> joints,
                        GetJointsBetweenEntities(*this, id1, id2));
  if (joints.empty()) {
    return absl::NotFoundError("No joints found to create dof view");
  }

  return GetDofKinematicView(joints);
}

absl::StatusOr<std::unique_ptr<const DofKinematicView>>
World::GetDofKinematicView(AttachmentEntityId id1,
                           AttachmentEntityId id2) const {
  INTR_ASSIGN_OR_RETURN(std::vector<JointEntityId> joints,
                        GetJointsBetweenEntities(*this, id1, id2));
  if (joints.empty()) {
    return absl::NotFoundError("No joints found to create dof view");
  }

  return GetDofKinematicView(joints);
}

absl::StatusOr<std::unique_ptr<DofKinematicView>> World::GetDofKinematicView(
    const std::vector<JointEntityId>& joint_ids) {
  return std::make_unique<
      entity_kinematic_world_details::EntityDofKinematicView>(
      static_cast<entity_aspect_world_details::EntityWorld*>(this), joint_ids);
}

absl::StatusOr<std::unique_ptr<const DofKinematicView>>
World::GetDofKinematicView(const std::vector<JointEntityId>& joint_ids) const {
  return std::make_unique<
      entity_kinematic_world_details::ConstEntityDofKinematicView>(
      static_cast<const entity_aspect_world_details::EntityWorld*>(this),
      joint_ids);
}

absl::StatusOr<std::unique_ptr<DofKinematicView>> World::GetDofKinematicView(
    RobotCollectionsEntityId robot_id) {
  INTR_ASSIGN_OR_RETURN(auto robot_dofs, GetRobotDofs(robot_id));
  return std::make_unique<
      entity_kinematic_world_details::EntityDofKinematicView>(
      static_cast<entity_aspect_world_details::EntityWorld*>(this), robot_dofs);
}

absl::StatusOr<std::unique_ptr<const DofKinematicView>>
World::GetDofKinematicView(RobotCollectionsEntityId robot_id) const {
  INTR_ASSIGN_OR_RETURN(auto robot_dofs, GetRobotDofs(robot_id));
  return std::make_unique<
      entity_kinematic_world_details::ConstEntityDofKinematicView>(
      static_cast<const entity_aspect_world_details::EntityWorld*>(this),
      robot_dofs);
}

absl::StatusOr<std::unique_ptr<DofKinematicView>>
World::GetDofKinematicViewForRobotGroupId(GroupId robot_group_id) {
  INTR_ASSIGN_OR_RETURN(
      const auto* robot_ids,
      GetRobotCollectionsEntityIdsForRobotGroupId(robot_group_id));
  if (robot_ids == nullptr) {
    return absl::InternalError("robot_ids was null");
  }

  std::vector<JointEntityId> joint_ids;
  for (auto robot_id : *robot_ids) {
    INTR_ASSIGN_OR_RETURN(auto robot_dofs, GetRobotDofs(robot_id));
    joint_ids.insert(joint_ids.end(), robot_dofs.begin(), robot_dofs.end());
  }
  return std::make_unique<
      entity_kinematic_world_details::EntityDofKinematicView>(
      static_cast<entity_aspect_world_details::EntityWorld*>(this), joint_ids);
}

absl::StatusOr<std::unique_ptr<const DofKinematicView>>
World::GetDofKinematicViewForRobotGroupId(GroupId robot_group_id) const {
  INTR_ASSIGN_OR_RETURN(
      const auto* robot_ids,
      GetRobotCollectionsEntityIdsForRobotGroupId(robot_group_id));
  if (robot_ids == nullptr) {
    return absl::InternalError("robot_ids was null");
  }

  std::vector<JointEntityId> joint_ids;
  for (auto robot_id : *robot_ids) {
    INTR_ASSIGN_OR_RETURN(auto robot_dofs, GetRobotDofs(robot_id));
    joint_ids.insert(joint_ids.end(), robot_dofs.begin(), robot_dofs.end());
  }
  return std::make_unique<
      entity_kinematic_world_details::ConstEntityDofKinematicView>(
      static_cast<const entity_aspect_world_details::EntityWorld*>(this),
      joint_ids);
}

absl::StatusOr<std::unique_ptr<kinematics::Skeleton>> World::BuildChainSkeleton(
    AttachmentEntityId id1, AttachmentEntityId id2) const {
  return ::intrinsic::BuildChainSkeleton(*this, id1, id2);
}

absl::StatusOr<LinkEntityId> World::GetBaseLink(
    CollectionsEntityId robot_id) const {
  INTR_ASSIGN_OR_RETURN(const CollectionsComponent* robot_collections,
                        GetComponentByEntityId<CollectionsComponent>(robot_id));
  std::vector<CollectionsMemberEntityId> link_ids =
      robot_collections->GetCollectionMembers(CollectionsComponent::kLinks);
  if (link_ids.empty()) {
    return intrinsic::NotFoundErrorBuilder()
           << "collections entity with ID " << robot_id.value()
           << " has no links";
  }
  INTR_ASSIGN_OR_RETURN(const WorldEntity* base_link_ent,
                        GetEntityById(link_ids[0]));
  INTR_RETURN_IF_ERROR(base_link_ent->ValidateEntity<LinkEntityId>());
  return LinkEntityId(link_ids[0].value());
}

absl::StatusOr<std::pair<RobotCollectionsEntityId, LinkEntityId>>
World::CreateRobotAndBaseLink(AttachmentEntityId parent_entity_id) {
  if (!HasEntity(parent_entity_id)) {
    return ::intrinsic::InvalidArgumentErrorBuilder()
           << "parent entity (ID " << parent_entity_id.value()
           << ") does not exist";
  }
  auto robot_id = CreateTypedEntity<RobotCollectionsEntityId>();
  INTR_ASSIGN_OR_RETURN(WorldEntity * robot_ent, GetEntityById(robot_id));
  auto link_id = CreateTypedEntity<LinkEntityId>();
  INTR_ASSIGN_OR_RETURN(WorldEntity * link_ent, GetEntityById(link_id));
  INTR_ASSIGN_OR_RETURN(auto* link_attachment,
                        link_ent->GetComponent<AttachmentComponent>());
  link_attachment->SetParentId(parent_entity_id);

  // Set the RobotComponent and RobotPartComponent references.
  INTR_ASSIGN_OR_RETURN(auto* link_collections_member,
                        link_ent->GetComponent<CollectionsMemberComponent>());
  INTR_RETURN_IF_ERROR(link_collections_member->AddParentCollection(
      robot_id, CollectionsComponent::kLinks));
  INTR_ASSIGN_OR_RETURN(auto* robot_collections,
                        robot_ent->GetComponent<CollectionsComponent>());
  INTR_RETURN_IF_ERROR(robot_collections->SetCollectionMembers(
      CollectionsComponent::kLinks, {link_id}));
  return std::make_pair(robot_id, link_id);
}

absl::StatusOr<std::pair<JointEntityId, LinkEntityId>> World::AddJoint(
    AttachmentEntityId parent_id, const Pose3d& parent_t_inboard,
    intrinsic_proto::world::KinematicsComponent::MotionType motion_type,
    const eigenmath::Vector3d& axis, double value, double lower_limit,
    double upper_limit) {
  INTR_ASSIGN_OR_RETURN(auto* parent_ent, GetEntityById(parent_id),
                        _ << "while getting parent entity");

  // Create the joint Entity. (Note its AttachmentComponent::parent_t_this is
  // updated through RefreshDofParentTThis().)
  auto joint_id = CreateTypedEntity<JointEntityId>();
  INTR_ASSIGN_OR_RETURN(WorldEntity * joint_ent, GetEntityById(joint_id));
  INTR_ASSIGN_OR_RETURN(auto* joint_attachment,
                        joint_ent->GetComponent<AttachmentComponent>());
  joint_attachment->SetParentId(parent_id);
  INTR_ASSIGN_OR_RETURN(auto* joint_kinematics,
                        joint_ent->GetComponent<KinematicsComponent>());
  joint_kinematics->SetParentTInboard(parent_t_inboard);
  joint_kinematics->SetMotionType(motion_type);
  joint_kinematics->SetAxis(axis);
  joint_kinematics->SetRawValue(value);

  // We skip enforcing limits for the application limits because the current
  // system limits might not allow this update, however since we know that both
  // system and application limits are being set we don't need to enforce them.
  INTR_RETURN_IF_ERROR(joint_kinematics->SetApplicationRawValueFixedLimits(
      lower_limit, upper_limit, /*enforce_limits=*/false));
  INTR_RETURN_IF_ERROR(joint_kinematics->SetSystemRawValueFixedLimits(
      lower_limit, upper_limit, /*enforce_limits=*/true));

  INTR_RETURN_IF_ERROR(RefreshDofParentTThis(joint_id));

  // Create the child link Entity.
  auto child_link_id = CreateTypedEntity<LinkEntityId>();
  INTR_ASSIGN_OR_RETURN(WorldEntity * child_link_ent,
                        GetEntityById(child_link_id));
  INTR_ASSIGN_OR_RETURN(auto* child_link_attachment,
                        child_link_ent->GetComponent<AttachmentComponent>());
  child_link_attachment->SetParentId(joint_id);

  // If applicable, exclude the parent and child link from colliding with each
  // other.
  auto parent_collision_or = parent_ent->GetComponent<CollisionComponent>();
  if (parent_collision_or.ok()) {
    parent_collision_or.value()->AddExclusionId(child_link_id);
    INTR_ASSIGN_OR_RETURN(auto* child_link_collision,
                          child_link_ent->GetComponent<CollisionComponent>());
    child_link_collision->AddExclusionId(PhysicalEntityId(parent_id.value()));
  }

  // If the parent link is not part of a robot, we're done.
  auto parent_robot_or =
      ValidateCollectionParentAmongTypes<RobotCollectionsEntityId>(
          parent_id, CollectionsComponent::RobotPartTypes());
  if (!parent_robot_or.ok()) {
    return std::make_pair(joint_id, child_link_id);
  }
  RobotCollectionsEntityId robot_id = parent_robot_or.value();

  // Otherwise, add the new joint and link to the robot.
  INTR_ASSIGN_OR_RETURN(auto* joint_collections_member,
                        joint_ent->GetComponent<CollectionsMemberComponent>());
  INTR_RETURN_IF_ERROR(joint_collections_member->AddParentCollection(
      robot_id, CollectionsComponent::kJoints));
  INTR_ASSIGN_OR_RETURN(
      auto* child_link_collections_member,
      child_link_ent->GetComponent<CollectionsMemberComponent>());
  INTR_RETURN_IF_ERROR(child_link_collections_member->AddParentCollection(
      robot_id, CollectionsComponent::kLinks));

  // To support legacy APIs, if the parent link and robot appear to be part of
  // a robot GroupId, add the new link and joint to it as well.
  INTR_ASSIGN_OR_RETURN(auto* robot_ent, GetEntityById(robot_id));
  GroupId robot_group_id(robot_ent->GetLocalName());
  LabelId robot_label(robot_ent->GetLocalName());
  if (robot_group_id_to_robot_collections_entity_ids_.contains(
          robot_group_id) &&
      robot_ent->HasLabel(robot_label) && parent_ent->HasLabel(robot_label)) {
    INTR_RETURN_IF_ERROR(joint_ent->AddLabels({robot_label}));
    INTR_RETURN_IF_ERROR(child_link_ent->AddLabels({robot_label}));
  }

  // Add the joint and link to the robot component's lists and call the helper
  // function to sort them.
  INTR_ASSIGN_OR_RETURN(auto* robot_collections,
                        robot_ent->GetComponent<CollectionsComponent>(),
                        _ << "while getting CollectionsComponent");
  INTR_ASSIGN_OR_RETURN(auto links,
                        ValidateCollectionMembers<CollectionsMemberEntityId>(
                            robot_id, CollectionsComponent::kLinks));
  links.emplace_back(child_link_id);
  INTR_RETURN_IF_ERROR(robot_collections->SetCollectionMembers(
      CollectionsComponent::kLinks, links));
  INTR_ASSIGN_OR_RETURN(auto joints,
                        ValidateCollectionMembers<CollectionsMemberEntityId>(
                            robot_id, CollectionsComponent::kJoints));
  joints.emplace_back(joint_id);
  INTR_RETURN_IF_ERROR(robot_collections->SetCollectionMembers(
      CollectionsComponent::kJoints, joints));
  INTR_RETURN_IF_ERROR(SortRobotLinkAndJointLists(robot_id));
  return std::make_pair(joint_id, child_link_id);
}

absl::StatusOr<RobotCoordinateFrameEntityId> World::AddCoordinateFrameToRobot(
    LinkEntityId parent_link_id, const Pose3d& parent_link_t_coordinate_frame) {
  INTR_ASSIGN_OR_RETURN(
      RobotCollectionsEntityId robot_id,
      ValidateCollectionParentAmongTypes<RobotCollectionsEntityId>(
          parent_link_id, {CollectionsComponent::kLinks}));

  auto coord_frame_id = CreateTypedEntity<RobotCoordinateFrameEntityId>();
  INTR_ASSIGN_OR_RETURN(
      auto* coord_frame_attachment,
      GetComponentByEntityId<AttachmentComponent>(coord_frame_id));
  coord_frame_attachment->SetParentId(parent_link_id);
  coord_frame_attachment->SetParentTThis(parent_link_t_coordinate_frame);
  INTR_ASSIGN_OR_RETURN(
      auto* coord_frame_collections_member,
      GetComponentByEntityId<CollectionsMemberComponent>(coord_frame_id));
  INTR_RETURN_IF_ERROR(coord_frame_collections_member->AddParentCollection(
      robot_id, CollectionsComponent::kCoordinateFrames));

  INTR_ASSIGN_OR_RETURN(auto* robot_collections,
                        GetComponentByEntityId<CollectionsComponent>(robot_id));
  INTR_ASSIGN_OR_RETURN(auto coord_frames,
                        ValidateCollectionMembers<CollectionsMemberEntityId>(
                            robot_id, CollectionsComponent::kCoordinateFrames));
  coord_frames.emplace_back(coord_frame_id);
  INTR_RETURN_IF_ERROR(robot_collections->SetCollectionMembers(
      CollectionsComponent::kCoordinateFrames, coord_frames));

  return coord_frame_id;
}

absl::Status World::SortRobotLinkAndJointLists(
    RobotCollectionsEntityId robot_id) {
  INTR_ASSIGN_OR_RETURN(auto* robot_collections,
                        GetComponentByEntityId<CollectionsComponent>(robot_id));
  INTR_ASSIGN_OR_RETURN(auto orig_link_ids,
                        ValidateCollectionMembers<LinkEntityId>(
                            robot_id, CollectionsComponent::kLinks));
  WorldHashSet<LinkEntityId> unsorted_link_ids(orig_link_ids.begin(),
                                               orig_link_ids.end());
  INTR_ASSIGN_OR_RETURN(auto unsorted_joint_ids,
                        ValidateCollectionMembers<JointEntityId>(
                            robot_id, CollectionsComponent::kJoints));
  if (unsorted_link_ids.size() <= 1 && unsorted_joint_ids.size() <= 1) {
    return absl::OkStatus();
  }

  // Iterate over the links to determine the base link and build a parent joint
  // to child link map.
  LinkEntityId base_link_id(kInvalidEntityId);
  WorldHashMap<JointEntityId, LinkEntityId> joint_id_to_child_link;
  for (auto joint_id : unsorted_joint_ids) {
    joint_id_to_child_link[joint_id] = LinkEntityId(kInvalidEntityId);
  }
  WorldHashMap<LinkEntityId, std::vector<JointEntityId>>
      link_id_to_child_joints;
  WorldHashMap<LinkEntityId, std::vector<LinkEntityId>> link_id_to_child_links;
  for (LinkEntityId link_id : unsorted_link_ids) {
    // Add this link to link_id_to_child_joints's keys. This will be used later
    // to check that every joint has a parent link in this robot.
    link_id_to_child_joints[link_id];

    INTR_ASSIGN_OR_RETURN(const auto* link_attachment,
                          GetComponentByEntityId<AttachmentComponent>(link_id));

    const auto parent_id = link_attachment->GetParentId();
    LinkEntityId parent_link_id(parent_id.value());
    if (unsorted_link_ids.contains(parent_link_id)) {
      // If we have a parent that is a link in this robot, we don't need to
      // consider it as a base link candidate.
      link_id_to_child_links[parent_link_id].push_back(link_id);
      continue;
    }

    JointEntityId parent_joint_id(parent_id.value());

    // To be a base link candidate, the link must either be:
    // - The child of an external joint OR
    // - The child of a fixed joint that itself is the child of an external link
    //
    // The reason for the second case is that we have SDFs where robots are
    // positioned via fixed joints between the world root and base links (e.g.
    // intrinsic/world/test_data/gluer_wedge_table_cell_world.sdf;rcl=332717638;l=12)
    const auto iter = joint_id_to_child_link.find(parent_joint_id);
    bool is_base_link_candidate = (iter == joint_id_to_child_link.end());
    if (!is_base_link_candidate) {
      INTR_ASSIGN_OR_RETURN(const auto* parent_joint_ent,
                            GetEntityById(parent_joint_id));
      INTR_ASSIGN_OR_RETURN(
          const auto* parent_joint_kinematics,
          parent_joint_ent->GetComponent<KinematicsComponent>());
      INTR_ASSIGN_OR_RETURN(
          const auto* parent_joint_attachment,
          parent_joint_ent->GetComponent<AttachmentComponent>());

      const LinkEntityId joint_parent_link_id(
          parent_joint_attachment->GetParentId().value());
      const bool has_joint_parent_link_in_robot =
          unsorted_link_ids.contains(joint_parent_link_id);
      is_base_link_candidate =
          (parent_joint_kinematics->GetMotionType() ==
           intrinsic_proto::world::KinematicsComponent::MOTION_TYPE_FIXED) &&
          !has_joint_parent_link_in_robot;
    }

    if (is_base_link_candidate) {
      if (base_link_id != kInvalidEntityId) {
        return absl::InvalidArgumentError(absl::StrCat(
            "Found multiple base link candidates, '",
            GetLocalNameForEntityById(base_link_id), "'[", base_link_id.value(),
            "] and '", GetLocalNameForEntityById(link_id), "'[",
            link_id.value(), "]"));
      }
      base_link_id = link_id;
      continue;
    }
    if (iter != joint_id_to_child_link.end()) {
      if (iter->second != kInvalidEntityId) {
        return ::intrinsic::InvalidArgumentErrorBuilder()
               << "joint with ID " << parent_joint_id.value()
               << " has multiple child links";
      }
      iter->second = link_id;
    }
  }
  if (base_link_id == kInvalidEntityId) {
    return absl::NotFoundError("no base link candidates");
  }

  // Iterate over the joints to build a parent link to child joints map.
  WorldHashMap<JointEntityId, const KinematicsComponent*>
      joint_id_to_kinematics;
  std::vector<CollectionsMemberEntityId> sorted_joint_ids;
  for (JointEntityId joint_id : unsorted_joint_ids) {
    INTR_ASSIGN_OR_RETURN(const auto* joint_ent, GetEntityById(joint_id));
    INTR_ASSIGN_OR_RETURN(const auto* joint_kinematics,
                          joint_ent->GetComponent<KinematicsComponent>());
    joint_id_to_kinematics[joint_id] = joint_kinematics;
    INTR_ASSIGN_OR_RETURN(const auto* joint_attachment,
                          joint_ent->GetComponent<AttachmentComponent>());
    LinkEntityId parent_link_id(joint_attachment->GetParentId().value());

    // Add this joint to the parent link's list of children.
    auto iter = link_id_to_child_joints.find(parent_link_id);
    if (iter == link_id_to_child_joints.end()) {
      // This joint is the child of an external entity. If it's fixed, it's
      // probably a positioning "joint" (e.g.
      // intrinsic/choreographer/input/process_builder.cc;rcl=338157096;l=248),
      // so we just add them to the front of the sorted joints. Otherwise,
      // return an error.
      if (joint_kinematics->GetMotionType() ==
          intrinsic_proto::world::KinematicsComponent::MOTION_TYPE_FIXED) {
        sorted_joint_ids.emplace_back(joint_id);
        continue;
      }
      return ::intrinsic::InvalidArgumentErrorBuilder()
             << "joint with ID " << joint_id.value()
             << " is neither fixed nor a child of a link in the robot";
    }
    iter->second.push_back(joint_id);
  }

  // Build the sorted link and joint lists.
  std::vector<CollectionsMemberEntityId> sorted_link_ids = {base_link_id};
  for (int i = 0; i < sorted_link_ids.size(); i++) {
    LinkEntityId link_id(sorted_link_ids[i].value());

    // Sort the child joints of this link according to the algorithm described
    // at go/intrinsic-choreographer-kinematic-tdd#heading=h.pbpe8z6aycw4.
    auto child_joint_ids = link_id_to_child_joints.at(link_id);
    std::sort(
        child_joint_ids.begin(), child_joint_ids.end(),
        [&joint_id_to_kinematics](JointEntityId one, JointEntityId two) {
          const auto* kin1 = joint_id_to_kinematics.at(one);
          const auto* kin2 = joint_id_to_kinematics.at(two);
          const auto& pti1 = kin1->GetParentTInboard();
          const auto& pti2 = kin2->GetParentTInboard();
          const auto& axis1 = kin1->GetAxis();
          const auto& axis2 = kin2->GetAxis();

          // Note that the orientation components of parent_t_inboard are
          // inverted because they're sorted in ascending order.
          return std::make_tuple(kin1->GetMotionType(), pti1.translation().x(),
                                 pti1.translation().y(), pti1.translation().z(),
                                 -pti1.quaternion().w(), -pti1.quaternion().x(),
                                 -pti1.quaternion().y(), -pti1.quaternion().z(),
                                 axis1.x(), axis1.y(), axis1.z()) <
                 std::make_tuple(kin2->GetMotionType(), pti2.translation().x(),
                                 pti2.translation().y(), pti2.translation().z(),
                                 -pti2.quaternion().w(), -pti2.quaternion().x(),
                                 -pti2.quaternion().y(), -pti2.quaternion().z(),
                                 axis2.x(), axis2.y(), axis2.z());
        });

    for (auto child_joint_id : child_joint_ids) {
      sorted_joint_ids.emplace_back(child_joint_id);

      // Because SDF allows a <joint> to have parent or child links that belong
      // to nested <model>s (which we translate into RobotComponents), we can
      // have situations where the child link of a joint is not a part of this
      // robot. In such instances, we silently skip the missing granchild link.
      LinkEntityId grandchild_link_id =
          joint_id_to_child_link.at(child_joint_id);
      if (grandchild_link_id != kInvalidEntityId) {
        sorted_link_ids.emplace_back(grandchild_link_id);
      }
    }

    auto child_link_ids = link_id_to_child_links[link_id];
    std::sort(child_link_ids.begin(), child_link_ids.end(),
              [this](const LinkEntityId& left, const LinkEntityId& right) {
                return GetLocalNameForEntityById(left) <
                       GetLocalNameForEntityById(right);
              });
    for (auto child_link_id : child_link_ids) {
      sorted_link_ids.emplace_back(child_link_id);
    }
  }

  CHECK_EQ(unsorted_link_ids.size(), sorted_link_ids.size());
  CHECK_EQ(unsorted_joint_ids.size(), sorted_joint_ids.size());
  INTR_RETURN_IF_ERROR(robot_collections->SetCollectionMembers(
      CollectionsComponent::kLinks, sorted_link_ids));
  INTR_RETURN_IF_ERROR(robot_collections->SetCollectionMembers(
      CollectionsComponent::kJoints, sorted_joint_ids));
  return absl::OkStatus();
}

absl::Status World::RemoveRobots(
    const std::vector<RobotCollectionsEntityId>& robot_ids) {
  // Go through the robots and collect the initial set of Entities we need to
  // remove.
  std::vector<AttachmentEntityId> ids_to_remove;
  for (RobotCollectionsEntityId robot_id : robot_ids) {
    for (auto type : CollectionsComponent::RobotPartTypes()) {
      INTR_ASSIGN_OR_RETURN(
          auto ids,
          ValidateCollectionMembers<AttachmentEntityId>(robot_id, type),
          _ << "while validating members of type " << type << " of '"
            << GetLocalNameForEntityById(robot_id) << "' / ID << "
            << robot_id.value());
      ids_to_remove.insert(ids_to_remove.end(), ids.begin(), ids.end());
    }
  }

  // This replicates the original implementation's behavior of removing any
  // children that would be left dangling.
  while (!ids_to_remove.empty()) {
    AttachmentEntityId id = ids_to_remove.back();
    ids_to_remove.pop_back();
    if (!HasEntity(id)) {
      continue;
    }

    // If the current entity is a leaf, remove it. Otherwise push it back onto
    // the stack along with its children.
    std::vector<AttachmentEntityId> children = GetChildrenOf(id);
    if (children.empty()) {
      INTR_RETURN_IF_ERROR(RemoveEntity(id));
      continue;
    }
    ids_to_remove.push_back(id);
    ids_to_remove.insert(ids_to_remove.end(), children.begin(), children.end());
  }

  // Finally, remove the robot entities and group.
  for (auto robot_id : robot_ids) {
    INTR_RETURN_IF_ERROR(RemoveEntity(robot_id));
  }
  return absl::OkStatus();
}

WorldHashSet<GroupId> World::GetRobotGroupIds() const {
  const auto keys =
      std::views::keys(robot_group_id_to_robot_collections_entity_ids_);
  return WorldHashSet<GroupId>(keys.begin(), keys.end());
}

absl::StatusOr<const std::vector<RobotCollectionsEntityId>*>
World::GetRobotCollectionsEntityIdsForRobotGroupId(
    const GroupId& group_id) const {
  auto iter = robot_group_id_to_robot_collections_entity_ids_.find(group_id);
  if (iter == robot_group_id_to_robot_collections_entity_ids_.end()) {
    return intrinsic::NotFoundErrorBuilder()
           << "no RobotCollectionsEntityIds found for GroupId \""
           << group_id.value() << "\"";
  }
  return iter->second.get();
}

void World::SetRobotCollectionsEntityIdsForRobotGroupId(
    const GroupId& group_id,
    const std::vector<RobotCollectionsEntityId>& robot_ids) {
  if (robot_ids.empty()) {
    robot_group_id_to_robot_collections_entity_ids_.erase(group_id);
    return;
  }
  auto iter = robot_group_id_to_robot_collections_entity_ids_.find(group_id);
  if (iter == robot_group_id_to_robot_collections_entity_ids_.end()) {
    robot_group_id_to_robot_collections_entity_ids_[group_id] =
        std::make_unique<std::vector<RobotCollectionsEntityId>>(robot_ids);
    return;
  }
  auto& dest_robot_ids = iter->second;
  dest_robot_ids->clear();
  dest_robot_ids->reserve(robot_ids.size());
  dest_robot_ids->insert(dest_robot_ids->begin(), robot_ids.begin(),
                         robot_ids.end());
}

absl::StatusOr<AttachmentEntityId> World::GetFinalEntityForRobotGroupId(
    const GroupId& group_id) const {
  INTR_ASSIGN_OR_RETURN(const auto* robot_ids,
                        GetRobotCollectionsEntityIdsForRobotGroupId(group_id));
  AttachmentEntityId group_tip_id(kInvalidEntityId);
  for (auto robot_id : *robot_ids) {
    auto robot_tip_id_or = GetFinalEntityOfRobotKinematicChain(robot_id);
    if (!robot_tip_id_or.ok()) {
      continue;
    }
    AttachmentEntityId robot_tip_id = robot_tip_id_or.value();
    if (group_tip_id == kInvalidEntityId) {
      group_tip_id = robot_tip_id;
      continue;
    }

    INTR_ASSIGN_OR_RETURN(AttachmentEntityId common_ancestor,
                          FindCommonAncestor(robot_tip_id, group_tip_id));
    // If the common ancestor is neither the robot's tip nor the current group
    // tip, this group does not form a linear chain (e.g. has a branch or
    // unrelated robots), so the final entity cannot be determined.
    if (common_ancestor != robot_tip_id && common_ancestor != group_tip_id) {
      return intrinsic::InternalErrorBuilder()
             << "Robots of GroupId \"" << group_id.value()
             << "\" do not form a linear chain";
    }

    // Otherwise, if this robot's tip is a descendant of the current group tip,
    // update the current group tip.
    if (common_ancestor == group_tip_id) {
      group_tip_id = robot_tip_id;
    }
  }

  if (group_tip_id == kInvalidEntityId) {
    return intrinsic::NotFoundErrorBuilder()
           << "No tip found for GroupId \"" << group_id.value() << "\"";
  }
  return group_tip_id;
}

absl::Status World::SetTipIdForRobotGroupId(const GroupId& group_id,
                                            PhysicalEntityId tip_object_id) {
  // RobotsImpl::SetTipId() was only used to enable
  // RobotsImpl::GetCartesianKinematicView(), see
  // intrinsic/world/robots_impl.cc;rcl=313274102;l=340 and
  // intrinsic/world/robots_impl.cc;rcl=313274102;l=595.
  //
  // For the reasons described in GetCartesianKinematicViewForRobotGroupId(),
  // we only need to update the first RobotComponent in the group.
  INTR_ASSIGN_OR_RETURN(const auto* robot_ids,
                        GetRobotCollectionsEntityIdsForRobotGroupId(group_id));
  CHECK(!robot_ids->empty());
  RobotCollectionsEntityId robot_id = (*robot_ids)[0];
  INTR_ASSIGN_OR_RETURN(auto* robot_component,
                        GetComponentByEntityId<RobotComponent>(robot_id));

  // Check invariants.
  const auto& solvable_frames = robot_component->GetSolvableFrames();
  if (solvable_frames.size() != 1) {
    return intrinsic::InternalErrorBuilder()
           << "first robot of GroupId \"" << group_id.value() << "\" has "
           << solvable_frames.size() << " solvable frames, expected 1";
  }
  const auto [base_id, old_tip_id] = *solvable_frames.begin();
  if (base_id != kInvalidEntityId) {
    return intrinsic::InternalErrorBuilder()
           << "base ID of solvable frame of first robot of GroupId \""
           << group_id.value() << "\" was " << base_id.value() << ", expected "
           << kInvalidEntityId.value();
  }

  // Clean up the previous tip entry.
  INTR_ASSIGN_OR_RETURN(auto solver_key, robot_component->GetSolverKeyForFrames(
                                             base_id, old_tip_id));
  if (old_tip_id != kInvalidEntityId) {
    INTR_ASSIGN_OR_RETURN(auto* old_tip_entity, GetEntityById(old_tip_id));
    INTR_RETURN_IF_ERROR(old_tip_entity->RemoveLabels({labels::Tip()}));
  }
  INTR_RETURN_IF_ERROR(
      robot_component->RemoveSolvableFrames(base_id, old_tip_id));

  // Set the new tip.
  INTR_RETURN_IF_ERROR(robot_component->AddSolvableFrames(
      AttachmentEntityId(kInvalidEntityId), tip_object_id, solver_key));
  if (tip_object_id != kInvalidEntityId) {
    INTR_ASSIGN_OR_RETURN(auto* new_tip_entity, GetEntityById(tip_object_id));
    INTR_RETURN_IF_ERROR(new_tip_entity->AddLabels({labels::Tip()}));
  }
  return absl::OkStatus();
}

absl::StatusOr<std::unique_ptr<CartesianKinematicView>>
World::GetCartesianKinematicViewForRobotGroupId(const GroupId& group_id) {
  // TL;DR: The behavior of RobotsImpl::GetCartesianKinematicView() can be
  // replicated by returning a CartesianKinematicView for the first robot of the
  // group, as long as RobotsImpl::SetTipId() has been called.
  //
  // Full story: The only GroupIds in the Robots aspect that were associated
  // with more than 1 KinematicObject were the "6+1 DoF" robots. These groups
  // were composed of two robots:
  // 1. A normal 6 DoF arm.
  // 2. The attached tool in the form of a KinematicObjectWithVirtualJoints.
  //
  // KinematicObjectWithVirtualJoints::HasOneTip() always returned false
  // (intrinsic/world/kinematic_object_with_virtual_joints.h?rcl=305120896&l=84),
  // which precluded KinematicWorldImpl::GetCartesianKinematicViewsImpl() from
  // generating a view for the tools
  // (intrinsic/world/kinematic_world_impl.cc?rcl=305777517&l=101).
  // Thus RobotsImpl::GetCartesianKinematicView() only ever returned the view
  // for the first robot
  // (intrinsic/world/robots_impl.cc?rcl=308667636&l=650).
  INTR_ASSIGN_OR_RETURN(const auto* robot_ids,
                        GetRobotCollectionsEntityIdsForRobotGroupId(group_id));
  CHECK(!robot_ids->empty());
  return GetCartesianKinematicView((*robot_ids)[0]);
}

absl::StatusOr<std::unique_ptr<const CartesianKinematicView>>
World::GetCartesianKinematicView(RobotCollectionsEntityId robot_id) const {
  INTR_ASSIGN_OR_RETURN(const auto* robot_component,
                        GetComponentByEntityId<RobotComponent>(robot_id));

  // Check invariants.
  const auto& solvable_frames = robot_component->GetSolvableFrames();
  if (solvable_frames.size() != 1) {
    return intrinsic::InternalErrorBuilder()
           << "first robot \"" << GetLocalNameForEntityById(robot_id)
           << "\" has " << solvable_frames.size()
           << " solvable frames, expected 1";
  }
  auto [base_id, tip_id] = *solvable_frames.begin();

  if (tip_id == kInvalidEntityId) {
    return intrinsic::InternalErrorBuilder()
           << "A CartesianKinematicView couldn't be created because the tip id "
              "for robot: "
           << robot_id.value()
           << " has not been set. Did you forget to call SetRobotTip or "
              "SetRobotIkSolverKey?";
  }

  return std::make_unique<
      entity_robots_details::ConstEntityRobotCartesianKinematicView>(
      static_cast<const entity_aspect_world_details::EntityWorld*>(this),
      std::make_pair(kRootEntityId, tip_id), robot_id);
}

absl::StatusOr<std::unique_ptr<CartesianKinematicView>>
World::GetCartesianKinematicView(RobotCollectionsEntityId robot_id) {
  INTR_ASSIGN_OR_RETURN(const auto* robot_component,
                        GetComponentByEntityId<RobotComponent>(robot_id));

  // Check invariants.
  const auto& solvable_frames = robot_component->GetSolvableFrames();
  if (solvable_frames.size() != 1) {
    return intrinsic::InternalErrorBuilder()
           << "first robot \"" << GetLocalNameForEntityById(robot_id)
           << "\" has " << solvable_frames.size()
           << " solvable frames, expected 1";
  }
  auto [base_id, tip_id] = *solvable_frames.begin();

  if (tip_id == kInvalidEntityId) {
    return intrinsic::InternalErrorBuilder()
           << "A CartesianKinematicView couldn't be created because the tip id "
              "for robot: "
           << robot_id.value()
           << " has not been set. Did you forget to call SetRobotTip or "
              "SetRobotIkSolverKey?";
  }

  return std::make_unique<
      entity_robots_details::EntityRobotCartesianKinematicView>(
      static_cast<entity_aspect_world_details::EntityWorld*>(this),
      std::make_pair(kRootEntityId, tip_id), robot_id);
}

absl::StatusOr<std::unique_ptr<const CartesianKinematicView>>
World::GetCartesianKinematicViewForRobotGroupId(const GroupId& group_id) const {
  // TL;DR: The behavior of RobotsImpl::GetCartesianKinematicView() can be
  // replicated by returning a CartesianKinematicView for the first robot of the
  // group, as long as RobotsImpl::SetTipId() has been called.
  //
  // Full story: The only GroupIds in the Robots aspect that were associated
  // with more than 1 KinematicObject were the "6+1 DoF" robots. These groups
  // were composed of two robots:
  // 1. A normal 6 DoF arm.
  // 2. The attached tool in the form of a KinematicObjectWithVirtualJoints.
  //
  // KinematicObjectWithVirtualJoints::HasOneTip() always returned false
  // (intrinsic/world/kinematic_object_with_virtual_joints.h?rcl=305120896&l=84),
  // which precluded KinematicWorldImpl::GetCartesianKinematicViewsImpl() from
  // generating a view for the tools
  // (intrinsic/world/kinematic_world_impl.cc?rcl=305777517&l=101).
  // Thus RobotsImpl::GetCartesianKinematicView() only ever returned the view
  // for the first robot
  // (intrinsic/world/robots_impl.cc?rcl=308667636&l=650).
  INTR_ASSIGN_OR_RETURN(const auto* robot_ids,
                        GetRobotCollectionsEntityIdsForRobotGroupId(group_id));
  CHECK(robot_ids != nullptr);
  CHECK(!robot_ids->empty());
  return GetCartesianKinematicView((*robot_ids)[0]);
}

void World::SetDofIdForEntityId(DofId dof_id, EntityId entity_id) {
  dof_id_to_entity_id_[dof_id] = entity_id;
}

absl::StatusOr<std::vector<DofId>> World::GetDofIdsForRobotGroupId(
    const GroupId& group_id) const {
  INTR_ASSIGN_OR_RETURN(const auto* robot_ids,
                        GetRobotCollectionsEntityIdsForRobotGroupId(group_id));
  CHECK_NE(nullptr, robot_ids);

  std::vector<DofId> ret;
  for (auto robot_id : *robot_ids) {
    INTR_ASSIGN_OR_RETURN(auto joint_ids, GetRobotDofs(robot_id));
    for (auto joint_id : joint_ids) {
      INTR_ASSIGN_OR_RETURN(auto dof_id, GetDofIdByEntityId(joint_id));
      ret.push_back(dof_id);
    }
  }
  return ret;
}

absl::StatusOr<std::set<CollectionsMemberEntityId>> World::GetCollectionMembers(
    CollectionsEntityId collections_id) const {
  INTR_ASSIGN_OR_RETURN(
      const CollectionsComponent* collections,
      GetComponentByEntityId<CollectionsComponent>(collections_id));
  return collections->GetAllCollectionMembers();
}

}  // namespace intrinsic
