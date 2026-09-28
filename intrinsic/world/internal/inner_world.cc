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

#include "intrinsic/world/internal/inner_world.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <ranges>
#include <type_traits>
#include <utility>

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "intrinsic/util/macros.h"
#include "intrinsic/util/status/ret_check.h"
#include "intrinsic/util/status/status_builder.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/collision/coal_collision_checker.h"
#include "intrinsic/world/collision/collision_check_cache.h"
#include "intrinsic/world/collision/collision_checker.h"
#include "intrinsic/world/collision/collision_checker_utils.h"
#include "intrinsic/world/collision/collision_checker_world.h"
#include "intrinsic/world/collision/collision_context.pb.h"
#include "intrinsic/world/entity.h"
#include "intrinsic/world/entity_id.h"
#include "intrinsic/world/hashing/hashing.h"
#include "intrinsic/world/internal/world_acl.h"
#include "intrinsic/world/world_acl_spec.h"
#include "intrinsic/world/world_entity_acl_spec.h"
#include "tf2/buffer_core.hpp"

namespace intrinsic {
namespace world_internal {

namespace {

constexpr std::pair<uint16_t, uint16_t> SplitEntityId(EntityId id) {
  static_assert(std::is_same_v<EntityId::ValueType, uint32_t>,
                "This calculation relies on EntityId being uint32_t");
  return {id.value() >> 16, static_cast<uint16_t>(id.value())};
}

constexpr EntityId CombineEntityId(uint16_t prefix, uint16_t counter) {
  static_assert(std::is_same_v<EntityId::ValueType, uint32_t>,
                "This calculation relies on EntityId being uint32_t");
  return EntityId((static_cast<uint32_t>(prefix) << 16) + counter);
}

}  // namespace

InnerWorld::InnerWorld() {
  constexpr size_t kMaxNumEntriesInCollisionCache = 1 << 20;
  cached_checks_ =
      std::make_shared<CollisionCheckCache>(kMaxNumEntriesInCollisionCache);
}

InnerWorld::InnerWorld(InnerWorld&& other) noexcept
    : next_entity_id_map_(std::move(other.next_entity_id_map_)),
      entities_(std::move(other.entities_)),
      default_rule_set_(std::move(other.default_rule_set_)),
      cached_checks_(std::move(other.cached_checks_)),
      spec_(std::move(other.spec_)) {
  collision_checker_ = std::move(other.collision_checker_);
  // tf2_buffer_ is not copied
}

InnerWorld& InnerWorld::operator=(InnerWorld&& other) noexcept {
  next_entity_id_map_ = other.next_entity_id_map_;
  collision_checker_ = std::move(other.collision_checker_);
  entities_ = std::move(other.entities_);
  default_rule_set_ = std::move(other.default_rule_set_);
  cached_checks_ = std::move(other.cached_checks_);
  spec_ = std::move(other.spec_);
  return *this;
}

absl::Status InnerWorld::CloneFrom(const InnerWorld& other,
                                   const WorldHashSet<EntityId>& subset) {
  // Reset the spec to empty so we can fill it in the for loop.
  spec_.ClearAccessFor(spec_.GetAllSpecifiedEntities());

  for (const auto& id : subset) {
    INTR_ASSIGN_OR_RETURN(auto* entity, other.GetEntityById(id));
    entities_.emplace(id, entity->Clone());

    // Ensure we have full access
    spec_.SetAccessFor(id, WorldEntityACLSpec::FullAccess());

    const auto [prefix, counter] = SplitEntityId(id);

    // Update the next entity id if the current entity has a higher value.
    next_entity_id_map_[prefix] = std::max(
        {static_cast<uint16_t>(kFirstEntityId.value()),
         next_entity_id_map_[prefix], static_cast<uint16_t>(counter + 1)});
  }

  // TODO(stoyang): Trim the rule set based on the entities?
  default_rule_set_ = other.GetDefaultRuleSet();
  // tf2_buffer_ Is not copied
  // cached_checks_ Is not copied, maybe we should?
  return absl::OkStatus();
}

absl::Status InnerWorld::AddEntity(EntityId entity_id,
                                   std::unique_ptr<WorldEntity> entity) {
  if (entity == nullptr) {
    return absl::InvalidArgumentError("Cannot add a null entity");
  }

  if (entities_.contains(entity_id)) {
    return ::intrinsic::InvalidArgumentErrorBuilder()
           << "Cannot add an entity with an id " << entity_id
           << " it already exists";
  }

  entities_.emplace(entity_id, std::move(entity));
  spec_.SetAccessFor(entity_id, WorldEntityACLSpec::FullAccess());

  const auto [prefix, counter] = SplitEntityId(entity_id);

  // Update the next entity id based on the max value we got from the input.
  next_entity_id_map_[prefix] = std::max(
      {static_cast<uint16_t>(kFirstEntityId.value()),
       next_entity_id_map_[prefix], static_cast<uint16_t>(counter + 1)});

  return absl::OkStatus();
}

WorldHashSet<EntityId> InnerWorld::GetEntityIds() const {
  const auto keys = std::views::keys(entities_);
  WorldHashSet<EntityId> results;
  results.insert(keys.begin(), keys.end());
  return results;
}

EntityId InnerWorld::CreateEntity(uint16_t prefix) {
  EntityId id = ReserveId(prefix);
  CHECK(entities_.emplace(id, WorldEntity::Create()).second) << id;
  spec_.SetAccessFor(id, WorldEntityACLSpec::FullAccess());
  return id;
}

EntityId InnerWorld::ReserveId(uint16_t prefix) {
  const uint16_t counter =
      std::max(next_entity_id_map_[prefix],
               static_cast<uint16_t>(kFirstEntityId.value()));
  next_entity_id_map_[prefix] = counter + 1;
  EntityId id = CombineEntityId(prefix, counter);
  CHECK(!entities_.contains(id)) << id;
  return id;
}

absl::Status InnerWorld::RemoveEntity(EntityId entity_id) {
  if (entity_id == kInvalidEntityId) {
    return absl::InvalidArgumentError("Invalid entity id.");
  }

  if (entity_id == kRootEntityId) {
    return absl::InvalidArgumentError("Cannot remove the root entity.");
  }

  entities_.erase(entity_id);
  spec_.ClearAccessFor({entity_id});
  return absl::OkStatus();
}

bool InnerWorld::HasEntity(EntityId entity_id) const {
  return entities_.contains(entity_id);
}

absl::StatusOr<const WorldEntity*> InnerWorld::GetEntityById(
    EntityId id) const {
  auto itr = entities_.find(id);
  if (itr == entities_.end()) {
    return intrinsic::NotFoundErrorBuilder()
           << "could not find Entity for ID " << id.value();
  }

  return itr->second.get();
}

absl::StatusOr<WorldEntity*> InnerWorld::GetEntityById(EntityId id) {
  auto itr = entities_.find(id);
  if (itr == entities_.end()) {
    return intrinsic::NotFoundErrorBuilder()
           << "could not find Entity for ID " << id.value();
  }

  return itr->second.get();
}

absl::StatusOr<WorldHashMap<EntityId, const WorldEntity*>>
InnerWorld::GetAllEntities() const {
  WorldHashMap<EntityId, const WorldEntity*> results;
  results.reserve(entities_.size());
  for (const auto& [k, v] : entities_) {
    results.emplace(k, v.get());
  }

  return std::move(results);
}

absl::StatusOr<WorldHashMap<EntityId, WorldEntity*>>
InnerWorld::GetAllEntities() {
  WorldHashMap<EntityId, WorldEntity*> results;
  results.reserve(entities_.size());
  for (auto& [k, v] : entities_) {
    results.emplace(k, v.get());
  }

  return std::move(results);
}

absl::StatusOr<std::shared_ptr<CollisionChecker>>
InnerWorld::GetCollisionChecker(
    const CollisionCheckerWorld& world,
    const WorldHashSet<PhysicalEntityId>& dynamic_objects,
    const intrinsic_proto::RuleSet& rule_set,
    const intrinsic_proto::world::CollisionCheckerConfig&
        collision_checker_config) const {
  // The priority is with the first action for a matching pair, so we add the
  // default rule set after.
  intrinsic_proto::RuleSet merged_rule_set = rule_set;
  merged_rule_set.MergeFrom(default_rule_set_);
  return GetCollisionCheckerImpl(world, dynamic_objects, merged_rule_set,
                                 collision_checker_config);
}

absl::StatusOr<std::shared_ptr<CollisionChecker>>
InnerWorld::GetCollisionChecker(
    const CollisionCheckerWorld& world,
    const WorldHashSet<PhysicalEntityId>& dynamic_objects,
    const intrinsic_proto::world::CollisionCheckerConfig&
        collision_checker_config) const {
  return GetCollisionCheckerImpl(world, dynamic_objects, default_rule_set_,
                                 collision_checker_config);
}

absl::StatusOr<std::shared_ptr<CollisionChecker>>
InnerWorld::GetCollisionCheckerImpl(
    const CollisionCheckerWorld& world,
    const WorldHashSet<PhysicalEntityId>& dynamic_objects,
    const intrinsic_proto::RuleSet& rule_set,
    const intrinsic_proto::world::CollisionCheckerConfig&
        collision_checker_config) const {
  INTR_RET_CHECK(collision_checker_.expired())
      << "Another object already owns a collision_checker of this world.";

  // While migrating at-rest World V1 data to World V2, all PhysicalWorld IDs
  // were translated into PhysicalEntityIds, even though some PhysicalWorld
  // objects do not have all of the PhysicalEntityId components (i.e. joints
  // usually do not have collision or geometry).
  //
  // As a result, we need to filter out objects that don't have both Collision
  // and GeometryComponents.
  WorldHashSet<PhysicalEntityId> filtered_objects;
  for (PhysicalEntityId phys_ent_id : dynamic_objects) {
    INTR_ASSIGN_OR_RETURN(const WorldEntity* entity,
                          GetEntityById(phys_ent_id));
    if (entity->IsEntityValid<PhysicalEntityId>()) {
      filtered_objects.insert(phys_ent_id);
    }
  }

  if (collision_checker_config.config_case() !=
          intrinsic_proto::world::CollisionCheckerConfig::kCoal &&
      collision_checker_config.config_case() !=
          intrinsic_proto::world::CollisionCheckerConfig::CONFIG_NOT_SET) {
    return ::intrinsic::InvalidArgumentErrorBuilder()
           << "CollisionCheckerConfig must be set to 'coal', but was set to "
              "case "
           << collision_checker_config.config_case();
  }

  // TODO(stoyang): We should also pass in the cached_check_ to the checker.
  INTR_ASSIGN_OR_RETURN(
      std::shared_ptr<CollisionChecker> collision_checker,
      CoalCollisionChecker::Create(&world, filtered_objects, rule_set,
                                   collision_checker_config.coal()));
  collision_checker_ = collision_checker;
  return collision_checker;
}

absl::StatusOr<WorldACL> InnerWorld::TryLock(const WorldACLSpec& narrow_spec) {
  // Validate that we do not have any invalid entities or permissions.
  for (const auto& entity_id : narrow_spec.GetAllSpecifiedEntities()) {
    if (!HasEntity(entity_id)) {
      return ::intrinsic::InvalidArgumentErrorBuilder()
             << "Unknown entity id in subset spec: " << entity_id;
    }

    // Ensure our spec can be narrowed to the given spec.
    const auto& our_spec = spec_.GetSpecForEntityId(entity_id);
    const auto& entity_spec = narrow_spec.GetSpecForEntityId(entity_id);

    INTR_RETURN_IF_ERROR(our_spec.CanNarrowTo(entity_spec))
        << "Incompatible subset spec for entity id: " << entity_id
        << " narrow_spec: " << narrow_spec << " our_spec: " << our_spec;

    if (!entity_spec.CanReadEntityData()) {
      return ::intrinsic::InvalidArgumentErrorBuilder()
             << "Invalid spec, entity must have read permission: " << entity_id;
    }
  }

  return spec_.TryLock(narrow_spec);
}

WorldACLSpec InnerWorld::GetACLSpec() const { return spec_.GetACLSpec(); }

intrinsic_proto::RuleSet InnerWorld::GetDefaultRuleSet() const {
  return default_rule_set_;
}

absl::Status InnerWorld::SetDefaultRuleSet(
    const intrinsic_proto::RuleSet& rule_set) {
  default_rule_set_ = rule_set;
  return absl::OkStatus();
}

tf2::BufferCore& InnerWorld::GetBufferCore() { return tf2_buffer_; }

}  // namespace world_internal
}  // namespace intrinsic
