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

#ifndef INTRINSIC_WORLD_INTERNAL_INNER_WORLD_H_
#define INTRINSIC_WORLD_INTERNAL_INNER_WORLD_H_

#include <cstdint>
#include <memory>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "intrinsic/world/collision/collision_check_cache.h"
#include "intrinsic/world/collision/collision_checker.h"
#include "intrinsic/world/collision/collision_checker_utils.h"
#include "intrinsic/world/collision/collision_checker_world.h"
#include "intrinsic/world/collision/collision_context.pb.h"
#include "intrinsic/world/entity.h"
#include "intrinsic/world/entity_id.h"
#include "intrinsic/world/hashing/hashing.h"
#include "intrinsic/world/internal/world_acl.h"
#include "intrinsic/world/proto/collision_checker_config.pb.h"
#include "intrinsic/world/world_acl_spec.h"
#include "tf2/buffer_core.hpp"

namespace intrinsic {
namespace world_internal {

// An InnerWorld implements the core components of a intrinsic::World. It is
// responsible for keeping track of the entities held by the World, as well as a
// few extra pieces of detail.
class InnerWorld {
 public:
  InnerWorld();
  ~InnerWorld() = default;

  InnerWorld(InnerWorld&& other) noexcept;
  InnerWorld& operator=(InnerWorld&& other) noexcept;

  InnerWorld(const InnerWorld& other) = delete;
  InnerWorld& operator=(const InnerWorld& other) = delete;

  // Returns all of the entity ids in this World.
  WorldHashSet<EntityId> GetEntityIds() const;

  // Creates a new Entity in the World and returns its id.
  EntityId CreateEntity(uint16_t prefix);

  // Reserves an id, but defers the creation of the Entity so it can be
  // AddEntity'ed later when data is available. The prefix is used in
  // conjunction with the internal counter to ensure consistency and uniqueness.
  EntityId ReserveId(uint16_t prefix);

  // Removes the given entity. If there are any other entities in the World that
  // point to this entity or are parented by this entity this operation could
  // cause problems in the future as this method does not do any checks. Use
  // carefully and only after removing the other references to this entity.
  absl::Status RemoveEntity(EntityId entity_id);

  // Returns true if this entity id is part of the entities managed by this
  // world.
  bool HasEntity(EntityId entity_id) const;

  // Gets a pointer to the entity based on the given id.
  absl::StatusOr<const WorldEntity*> GetEntityById(EntityId id) const;

  // Returns a pointer for each of the entities in this world along with its id.
  absl::StatusOr<WorldHashMap<EntityId, const WorldEntity*>> GetAllEntities()
      const;

  // Gets a pointer to the entity based on the given id.
  absl::StatusOr<WorldEntity*> GetEntityById(EntityId id);

  // Returns a pointer for each of the entities in this world along with its id.
  absl::StatusOr<WorldHashMap<EntityId, WorldEntity*>> GetAllEntities();

  // Builds and returns a CollisionChecker object from the input dynamic object.
  absl::StatusOr<std::shared_ptr<CollisionChecker>> GetCollisionChecker(
      const CollisionCheckerWorld& world,
      const WorldHashSet<PhysicalEntityId>& dynamic_objects,
      const intrinsic_proto::RuleSet& rule_set,
      const intrinsic_proto::world::CollisionCheckerConfig&
          collision_checker_config = {}) const;

  // Builds and returns a CollisionChecker object from the input dynamic object.
  absl::StatusOr<std::shared_ptr<CollisionChecker>> GetCollisionChecker(
      const CollisionCheckerWorld& world,
      const WorldHashSet<PhysicalEntityId>& dynamic_objects,
      const intrinsic_proto::world::CollisionCheckerConfig&
          collision_checker_config = {}) const;
  // Adds the given entity to the list of managed entities, this should be used
  // only during deserialization as it creates the entity externally.
  absl::Status AddEntity(EntityId entity_id,
                         std::unique_ptr<WorldEntity> entity);
  // Attempts to lock a subset of the permissions available in this instance. If
  // the given spec can be delegated the resulting WorldACL will lock the given
  // spec for its lifetime. Once the returned object is deallocated, the
  // permissions will be unlocked and a new delegation can happen through this
  // instance.
  absl::StatusOr<WorldACL> TryLock(const WorldACLSpec& narrow_spec);

  // Returns the current set of ACLs observed by this inner_world.
  WorldACLSpec GetACLSpec() const;

  // Returns the current default rule set used with this world.
  intrinsic_proto::RuleSet GetDefaultRuleSet() const;
  // Sets the current default rule set used with this world.
  absl::Status SetDefaultRuleSet(const intrinsic_proto::RuleSet& rule_set);

  // Clone the given InnerWorld by making a copy of the subset of entities and
  // representing them within this InnerWorld.
  absl::Status CloneFrom(const InnerWorld& other,
                         const WorldHashSet<EntityId>& subset);

  // Get the tf2 Buffer associated with this world
  tf2::BufferCore& GetBufferCore();

  // Returns the cached collision check results.
  std::shared_ptr<CollisionCheckCache> GetCachedChecks() const {
    return cached_checks_;
  }

 private:
  absl::StatusOr<std::shared_ptr<CollisionChecker>> GetCollisionCheckerImpl(
      const CollisionCheckerWorld& world,
      const WorldHashSet<PhysicalEntityId>& dynamic_objects,
      const intrinsic_proto::RuleSet& rule_set,
      const intrinsic_proto::world::CollisionCheckerConfig&
          collision_checker_config) const;

  friend class TestInnerWorld;
  WorldHashMap<uint16_t, uint16_t> next_entity_id_map_ = {
      {kDefaultEntityIdPrefix, static_cast<uint16_t>(kFirstEntityId.value())},
  };

  // These are all of the entities contained within this world.
  WorldHashMap<EntityId, std::unique_ptr<WorldEntity>> entities_;

  // The default rule set to use when creating a collision checker instance.
  intrinsic_proto::RuleSet default_rule_set_;

  // For book keeping of whether an outstanding collision checker exists.
  mutable std::weak_ptr<CollisionChecker> collision_checker_;

  // Map holding the result of computing the collicion between two objects and a
  // relative pose between them. The key is [a_id, b_id, a_t_b], while the value
  // is a set of margin/result pairs.
  mutable std::shared_ptr<CollisionCheckCache> cached_checks_;

  // The specs associated with this inner world, it manages the delegation of
  // acls for this world.
  WorldACL spec_ = WorldACL(WorldACLSpec());

  // Storage for tf2 transform data
  ::tf2::BufferCore tf2_buffer_;
};

}  // namespace world_internal
}  // namespace intrinsic

#endif  // INTRINSIC_WORLD_INTERNAL_INNER_WORLD_H_
