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

#ifndef INTRINSIC_WORLD_WORLD_H_
#define INTRINSIC_WORLD_WORLD_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "google/protobuf/timestamp.pb.h"
#include "gtest/gtest_prod.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/geometry/storage/geometry_deserializer.h"
#include "intrinsic/geometry/storage/geometry_serializer.h"
#include "intrinsic/kinematics/skeleton.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/proto/tf_message.pb.h"
#include "intrinsic/util/macros.h"
#include "intrinsic/util/status/status_builder.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/aspects/entity_grouping.h"
#include "intrinsic/world/aspects/entity_world_interface.h"
#include "intrinsic/world/cartesian_kinematic_view.h"
#include "intrinsic/world/collision/collision_check_cache.h"
#include "intrinsic/world/collision/collision_checker.h"
#include "intrinsic/world/collision/collision_checker_utils.h"
#include "intrinsic/world/collision/collision_checker_world.h"
#include "intrinsic/world/collision/collision_context.pb.h"
#include "intrinsic/world/component/collections_component.h"
#include "intrinsic/world/component/collections_member_component.h"
#include "intrinsic/world/component/geometry_component.h"
#include "intrinsic/world/dof_kinematic_view.h"
#include "intrinsic/world/entity.h"
#include "intrinsic/world/entity_id.h"
#include "intrinsic/world/grouping.h"
#include "intrinsic/world/gzfile/gzfile.h"
#include "intrinsic/world/hashing/hashing.h"
#include "intrinsic/world/internal/inner_world.h"
#include "intrinsic/world/labels.h"
#include "intrinsic/world/proto/collections_component.pb.h"
#include "intrinsic/world/proto/kinematics_component.pb.h"
#include "intrinsic/world/proto/physics_component.pb.h"
#include "intrinsic/world/proto/sensor_component.pb.h"
#include "intrinsic/world/proto/tf_associations.pb.h"
#include "intrinsic/world/proto/world_fragment.pb.h"
#include "intrinsic/world/world.pb.h"
#include "tf2/transform_datatypes.hpp"

namespace intrinsic {

// The World is a container of data associated with our understanding of the
// real world.
//
// The World is a collection of Entity objects. The main interaction with the
// World happens through these Entities. Each Entity has data stored in
// components. Not all Entities have all components.
//
// Each component is responsible for holding some information about the
// associated entity and potentially about the relationships with other
// entities.
//
// The World class itself relies on the data held by the components and the
// connections described between Entities in those components to perform
// operations on them.
//
// For more details about the different components please look at the interfaces
// for those components.
class World : public CollisionCheckerWorld,
              protected entity_aspect_world_details::EntityWorld {
 public:
  World(World&& other) noexcept;

  // We added this operator temporarely for the purpose of transitioning to the
  // World Service for skills. Widespread use of it could result in strange
  // behaviors as we may get dangling references to entities that no longer
  // exist, or worse, point to the wrong entity. An example of these cases is
  // having a kinematic view pointing to a set of dofs that are references by
  // id, then swapping the world using the move operator but continuing to use
  // the view. The view may point to the same logical dofs or it could point to
  // entities that are not related or are not even joints to begin with.
  ABSL_DEPRECATED("Please use World::Clone() instead")
  World& operator=(World&& other) noexcept;

  // Use the Clone function instead.
  World(const World& other) = delete;
  World& operator=(const World& other) = delete;

  // Makes a copy of the world and all of the entities within.
  World Clone() const;

  // Makes a copy of the world and the given entities within it.
  World CloneSubset(const WorldHashSet<EntityId>& subset_ids) const;

  // Returns a new world created without any serialized data in an empty state.
  static World CreateEmptyWorld();

  // ------------------------------------
  // -- Aspect methods
  // -- DEPRECATED Please use other alternatives where available.
  // ------------------------------------

  // Returns the Aspect of the world matching the specified type.
  template <typename AspectType>
  ABSL_DEPRECATED("Avoid aspect related functionality in the world")
  AspectType& As();

  // Returns the Aspect of the world matching the specified type.
  template <typename AspectType>
  ABSL_DEPRECATED("Avoid aspect related functionality in the world")
  const AspectType& As() const;

  // ------------------------------------
  // -- Serialization methods
  // ------------------------------------

  // Serialize all of the parts of the World. Use the given geometry library for
  // storing the geometry referenced by this world.
  ABSL_DEPRECATED("Do not serialize geometry: it should already be serialized.")
  absl::StatusOr<intrinsic_proto::world::internal::World> Serialize(
      GeometrySerializer& geolib) const {
    return SerializeImpl(&geolib);
  }

  // Serialize all of the parts of the World. Use the given geometry library for
  // storing the geometry referenced by this world.
  absl::StatusOr<intrinsic_proto::world::internal::World> Serialize() const {
    return SerializeImpl(nullptr);
  }

  // Deserializes the given proto into a world object. Use the given geometry
  // reader for fetching the geometry references by the world proto.
  ABSL_DEPRECATED("Deserialize geometry when requested not during construction")
  static absl::StatusOr<World> Deserialize(
      const intrinsic_proto::world::internal::World& data,
      const GeometryDeserializer& geolib);

  // Deserializes the given proto into a world object. Use the given geometry
  // reader for fetching the geometry references by the world proto.
  static absl::StatusOr<World> Deserialize(
      const intrinsic_proto::world::internal::World& data);

  // Load the world from a gz file.
  static absl::StatusOr<intrinsic::World> FromFile(const GZFile& gzfile);

  // Save the world to the given gz file.
  ABSL_DEPRECATED("Pass a deserializer")
  absl::Status ToFile(GZFile* gzfile) const;

  // Save the world to the given gz file. Deserialize geometries in order to
  // properly write them out to disk.
  absl::Status ToFile(GZFile* gzfile, const GeometryDeserializer& deserializer);

  // Updates the world based on the given proto. This will do a full
  // override, if something is missing from this proto it will override any
  // existing values with the defaults.
  ABSL_DEPRECATED("Deserialize geometry when requested not during update")
  absl::Status UpdateFromProto(
      const intrinsic_proto::world::internal::World& world_proto,
      const GeometryDeserializer& geolib) {
    return UpdateFromProtoImpl(world_proto, &geolib);
  }

  // Updates the world based on the given proto. This will do a full
  // override, if something is missing from this proto it will override any
  // existing values with the defaults.
  absl::Status UpdateFromProto(
      const intrinsic_proto::world::internal::World& world_proto) {
    return UpdateFromProtoImpl(world_proto, nullptr);
  }

  // ------------------------------------
  // -- General Entity methods
  // ------------------------------------

  // Returns all of the entity ids in this World.
  WorldHashSet<EntityId> GetEntityIds() const override;

  // Attempts to find the EntityId that corresponds to the provided DofId.
  absl::StatusOr<EntityId> GetEntityIdByDofId(DofId dof_id) const override;

  // Attempts to find the DofId that corresponds to the provided EntityId.
  absl::StatusOr<DofId> GetDofIdByEntityId(EntityId entity_id) const override;

  // Search for entities by name. This will search the tree down from the
  // given root to find a set of entities that have the names in their parentage
  // chain. There names specify the exact parentage and
  // there cannot be any missing items. For example if the tree looks like:
  // A -> B -> C -> D
  // and the call is FindByExactLocalNames(A, {"B", "D"}) the result will be
  // empty. While FindByExactLocalNames(A, {"B", "C"}) will return C.
  // Because local names are not necessarily unique, the resulting set may
  // contain more than one entity.
  WorldHashSet<AttachmentEntityId> FindByExactLocalNames(
      AttachmentEntityId root, const std::vector<std::string>& names) const;

  // Search for entities by name. This will search the tree down from the
  // given root to find a set of entities that have the names in their parentage
  // chain. There can be other entities that do not have a
  // match between the given names. For example if the tree looks like:
  // A -> B -> C -> D
  // and the call is FindByLocalNames(A, {"B", "D"}) the result will be D.
  WorldHashSet<AttachmentEntityId> FindByLocalNames(
      AttachmentEntityId root, const std::vector<std::string>& names) const;

  // Returns the set of local names in the path from root to the given node. The
  // given node will be at the end of the list and the root will not be part of
  // the list as it is implied.
  absl::StatusOr<std::vector<std::string>> GetLocalNamePath(
      AttachmentEntityId id) const;

  // Returns the set of local names in the path from root to the given node
  // combined into a single string. The given node will be at the end of the
  // string and the root will not be part of the string as it is implied.
  absl::StatusOr<std::string> GetLocalNamePathString(
      AttachmentEntityId id, absl::string_view separator) const override;

  // Returns the set of entities with both of these properties:
  // 1) Have the specified local name
  // 2) For each name in collection_path, has an ancestor in the attachment tree
  //    that is a member of a collection with that name.
  //
  // Note that the names in collection_path must be encountered in the correct
  // order (going from world root to target entity), but they need not be
  // encountered contiguously. In other words, there's something like a regex .*
  // wildcard before and after each item in collection_path.
  WorldHashSet<AttachmentEntityId> FindByNameAndCollectionPath(
      const std::string& local_name,
      const std::vector<std::string>& collection_path) const;

  // Returns the entity id, if found for an entity with the given alias, or an
  // error if not found.
  absl::StatusOr<EntityId> FindByAlias(absl::string_view alias) const;

  // Sets the alias for the given entity id. If the alias already exists this
  // will return an error.
  absl::Status SetAlias(EntityId id, absl::string_view alias);

  // Returns all of the entity handles in this World that have all of the
  // given ComponentTypes.
  //
  // The set template pack ComponentTypes can be one of two alternatives:
  // - A pack of component types like AttachmentComponentType or
  //   CollisionComponentType.
  // - A single parameter of type TypedEntityId<T...> where T is a set of
  //   component types.
  //
  // This facilitates being able to do things like:
  // INTR_ASSIGN_OR_RETURN(std::vector<TypedEntityId<CollisionComponentType>>
  // result, world->GetTypedEntityIds<CollisionComponentType>());
  //
  // INTR_ASSIGN_OR_RETURN(std::vector<PhysicalEntityId> result,
  // world->GetTypedEntityIds<PhysicalEntityId>());
  template <typename... ComponentTypes>
  std::vector<world_entity_details::TypedResult<ComponentTypes...>>
  GetTypedEntityIds() const;

  // Creates a new Entity in the World and returns its id.
  EntityId CreateEntity() override;

  // Creates a new Entity in the World with the specified id. Returns an error
  // if the id is already in use.
  absl::StatusOr<EntityId> CreateEntityWithId(EntityId entity_id);

  // Copies all entities from other_world into this one. Note that new ids
  // will be created for all entities. The default rule set from the given world
  // will be merged into this world instance. Returns a mapping from old id to
  // new id.
  //
  // to_attach_to references an object in THIS world under which all objects in
  // other_world will be attached to in place of the root object.
  // TODO(stoyang): Consider allowing some edits like alias assignments.
  absl::StatusOr<WorldHashMap<EntityId, EntityId>> IntegrateWorld(
      const AttachmentEntityId& to_attach_to, uint16_t entity_id_prefix,
      const World& other_world);

  // Same as above but operates on a world fragment. It will also merge the
  // default rule set present in the fragment into this world.
  //
  // It is still expected that the entities in the fragment form correct
  // attachment chains that are attached to a root entity.
  ABSL_DEPRECATED("Deserialize geometry when requested not during construction")
  absl::StatusOr<WorldHashMap<EntityId, EntityId>> IntegrateFragment(
      const AttachmentEntityId& to_attach_to, uint16_t entity_id_prefix,
      const intrinsic_proto::world::WorldFragment& input_fragment,
      const GeometryDeserializer& geolib) {
    return IntegrateFragmentImpl(to_attach_to, entity_id_prefix, input_fragment,
                                 &geolib);
  }

  // Same as above but operates on a world fragment. It will also merge the
  // default rule set present in the fragment into this world.
  //
  // It is still expected that the entities in the fragment form correct
  // attachment chains that are attached to a root entity.
  absl::StatusOr<WorldHashMap<EntityId, EntityId>> IntegrateFragment(
      const AttachmentEntityId& to_attach_to, uint16_t entity_id_prefix,
      const intrinsic_proto::world::WorldFragment& input_fragment) {
    return IntegrateFragmentImpl(to_attach_to, entity_id_prefix, input_fragment,
                                 nullptr);
  }

  // Same as above but operates on the entities directly. Does not have a rule
  // set to merge so it will not have this functionality.
  //
  // It is still expected that the other entities form correct attachment chains
  // that are attached to a root entity (though the root entity need not be
  // present in the map).
  absl::StatusOr<WorldHashMap<EntityId, EntityId>> IntegrateEntities(
      const AttachmentEntityId& to_attach_to, uint16_t entity_id_prefix,
      const WorldHashMap<EntityId, const WorldEntity*>& other_entities);

  // Copies other_entities into this world. Does NOT create new ids for the
  // entities. It fails if the ids are already used.
  absl::Status IntegrateEntitiesKeepingIds(
      const WorldHashMap<EntityId, const WorldEntity*>& other_entities);

  // Creates a new Entity with the components required by the template argument
  // (a TypedEntityId such as JointEntityId or LinkEntityId).
  template <typename EntityType>
  EntityType CreateTypedEntity();

  // Creates a new Entity with the specified components and returns its id.
  template <typename... ComponentTypes>
  world_entity_details::TypedResult<ComponentTypes...>
  CreateEntityWithComponentTypes();

  // Removes the given entity. If there are any other entities in the World that
  // point to this entity or are parented by this entity this operation could
  // cause problems in the future as this method does not do any checks. Use
  // carefully and only after removing the other references to this entity.
  absl::Status RemoveEntity(EntityId entity_id) override;

  // Removes the given entity. This enforces that we do not have other entities
  // referencing the given entity. The enforcement limits the entities that can
  // be deleted, for example we can only delete leaf entities in the attachment
  // graph.
  absl::Status SafelyRemoveEntity(EntityId entity_id) override;

  // Returns true if this entity id is part of the entities managed by this
  // world.
  bool HasEntity(EntityId entity_id) const override;

  // Validates and returns a typed entity handle based on the given templates
  // and id. Returns an error if the entity does not contain all of the required
  // templated components.
  //
  // The set template pack ComponentTypes can be one of two alternatives:
  // - A pack of component types like AttachmentComponentType or
  //   CollisionComponentType.
  // - A single parameter of type TypedEntityId<T...> where T is a set of
  //   component types.
  //
  // This facilitates being able to do things like:
  // INTR_ASSIGN_OR_RETURN(TypedEntityId<CollisionComponentType> result,
  // world->ValidateEntity<CollisionComponentType>(id));
  //
  // INTR_ASSIGN_OR_RETURN(PhysicalEntityId result,
  // world->ValidateEntity<PhysicalEntityId>(id));
  template <typename... ComponentTypes>
  absl::StatusOr<world_entity_details::TypedResult<ComponentTypes...>>
  ValidateEntity(EntityId id) const;

  // Retrieves a collection and validates all of its members conform to the
  // templated type(s). Returns an error if any error occurs while retrieving
  // the collection or if any member does not contain all of the required
  // templated components.
  template <typename... ComponentTypes>
  absl::StatusOr<
      std::vector<world_entity_details::TypedResult<ComponentTypes...>>>
  ValidateCollectionMembers(
      CollectionsEntityId collections_id,
      intrinsic_proto::world::CollectionsComponent::CollectionType type) const;

  // Retrieves a collection and validates all of its members conform to the
  // templated type(s). Returns an error if any error occurs while retrieving
  // the collection. If any member does not contain all of the required
  // templated components it will not be included in the returned vector.
  template <typename... ComponentTypes>
  absl::StatusOr<
      std::vector<world_entity_details::TypedResult<ComponentTypes...>>>
  FilterCollectionMembers(CollectionsEntityId collections_id) const;

  // Shorthand function to:
  // 1) Get member_id's CollectionMemberComponent
  // 2) Call FindParentCollectionAmongTypes()
  // 3) Validate the parent conforms to the templated type(s)
  //
  // Returns any errors that occur during the process.
  template <typename... ComponentTypes>
  absl::StatusOr<world_entity_details::TypedResult<ComponentTypes...>>
  ValidateCollectionParentAmongTypes(
      EntityId member_id,
      const WorldHashSet<
          intrinsic_proto::world::CollectionsComponent::CollectionType>& types)
      const;

  // Returns the entities associated with the given labels and optionally the
  // given local name (if non-empty). All returned entity ids contain all of the
  // given label ids. If no entities are found that have all of the labels then
  // the set will be empty.
  WorldHashSet<EntityId> GetEntitiesWithAllLabels(
      const WorldHashSet<LabelId>& label_ids,
      absl::string_view local_name = absl::string_view()) const;

  // Returns the unique entity id that is associated with all of the given
  // labels and optionally the given local name (if non-empty). Returns an error
  // if there is not exactly one one entity that matches the criteria.
  absl::StatusOr<EntityId> GetSingleEntityWithAllLabels(
      const WorldHashSet<LabelId>& label_ids,
      absl::string_view local_name = absl::string_view()) const;

  // Shorthand for GetEntityById() followed by WorldEntity::GetComponent<>().
  // Given an EntityId, returns a component of the corresponding entity.
  template <typename Component>
  absl::StatusOr<const Component*> GetComponentByEntityId(EntityId id) const;
  template <typename Component>
  absl::StatusOr<Component*> GetComponentByEntityId(EntityId id);

  // Shorthand for GetEntityById() followed by
  // WorldEntity::GetOrCreateComponent<>(). Given an EntityId, returns a
  // component of the corresponding entity.
  template <typename Component>
  absl::StatusOr<const Component*> GetOrCreateComponentByEntityId(
      EntityId id) const;
  template <typename Component>
  absl::StatusOr<Component*> GetOrCreateComponentByEntityId(EntityId id);

  // Return the local name of a given EntityId. If the EntityId is invalid, an
  // error string is return.
  std::string GetLocalNameForEntityById(EntityId id) const override;

  // Return the root robot of a tree of robots.
  absl::StatusOr<RobotCollectionsEntityId> GetRootRobot(
      const WorldHashSet<RobotCollectionsEntityId>& robot_entities) const;

  // ------------------------------------
  // -- Model methods
  // ------------------------------------

  // Returns the root entity for the model in the world. If a model with the
  // given name is not found this will return a not found error. If there is no
  // clear root a failed precondition error will be returned. The root must be
  // part of the model.
  absl::StatusOr<AttachmentEntityId> GetRootOfModel(
      absl::string_view model_name) const;

  // Attempts to get the base entity of a collection.
  absl::StatusOr<AttachmentEntityId> GetRootEntity(
      CollectionsEntityId collection_id) const;

  // Return the root of a collection of attachment entities.
  // If there is no clear root (i.e. the common ancestor is not in entities),
  // returns a failed precondition error.
  absl::StatusOr<AttachmentEntityId> GetRootEntity(
      const WorldHashSet<EntityId>& entities) const;

  // ------------------------------------
  // -- PhysicalWorld methods
  // ------------------------------------

  // Get a pointer to the entity based on the given id.
  absl::StatusOr<const WorldEntity*> GetEntityById(EntityId id) const override;

  // Get a pointer to the entity based on the given id.
  absl::StatusOr<WorldEntity*> GetEntityById(EntityId id) override;

  // Returns the set of entities that contain the given label.
  WorldHashSet<EntityId> FindByLabel(LabelId label) const override;

  // Returns the set of entities that contain the given label and are
  // descendants of the given root in the attachment graph.
  WorldHashSet<EntityId> FindByLabel(AttachmentEntityId root,
                                     LabelId label) const;

  // Shorthand function to:
  // 1) Call FindByLabel()
  // 2) Call ValidateEntity<>() on each found entity.
  template <typename... ComponentTypes>
  WorldHashSet<world_entity_details::TypedResult<ComponentTypes...>>
  ValidateByLabel(const LabelId& label) const;

  // Returns all of the direct child objects of the given AttachmentEntityId.
  // The handle must be a valid entity.
  std::vector<AttachmentEntityId> GetChildrenOf(
      AttachmentEntityId entity_handle) const override;

  // Adds an attachment component to the given entity. It then populates the
  // attachment component with the data that is has a parent with the given id
  // and that it has the given transform to that parent. Returns the handle to
  // the entity as a AttachmentEntityId. If the component already exists it will
  // return an error.
  absl::StatusOr<AttachmentEntityId> CreateAttachmentComponent(
      AttachmentEntityId parent_handle, EntityId id,
      const Pose3d& parent_t_entity,
      std::optional<absl::Time> time = std::nullopt) override;

  // Removes the attachment component from the entity. The given entity must not
  // have any children. If it has children they must be removed or reparented
  // first before removing this component.
  absl::Status DeleteAttachmentComponent(
      AttachmentEntityId entity_handle) override;

  // Creates a physics component for the given entity with the given proto.
  absl::StatusOr<PhysicsEntityId> CreatePhysicsComponent(
      EntityId id,
      const intrinsic_proto::world::PhysicsComponent& physics_component)
      override;

  // Creates a sensor component for the given entity with the given proto.
  absl::StatusOr<SensorEntityId> CreateSensorComponent(
      EntityId id,
      const intrinsic_proto::world::SensorComponent& sensor_component) override;

  // Updates a transform between two physical entities. These entities must be
  // directly connected. Either A or B can be the parent entity.
  void UpdateAttachmentPose(AttachmentEntityId a_handle,
                            AttachmentEntityId b_handle,
                            const Pose3d& a_t_b) override;

  // Adds the given entity as a child to parent entity. This will only succeed
  // if the two entities are not already connected. This moves any child
  // entities with the given entity.
  absl::Status ReparentEntity(
      AttachmentEntityId new_parent_handle, AttachmentEntityId child_handle,
      const Pose3d& parent_t_entity,
      std::optional<absl::Time> timestamp = std::nullopt) override;

  // Returns a transform between the two given entities. These entities need not
  // be directly connected. This method inspects the connections between the two
  // entities to calculate a transform that describes the pose between the two
  // given entities. Returns the transform in the form of a_t_b.
  Pose3d GetTransform(AttachmentEntityId a_handle,
                      AttachmentEntityId b_handle) const override;

  // Returns a transform between the two given entities at the given time.
  // These entities need not
  // be directly connected. This method inspects the connections between the two
  // entities to calculate a transform that describes the pose between the two
  // given entities. Returns the transform in the form of a_t_b.
  absl::Status GetTransform(AttachmentEntityId a_handle,
                            AttachmentEntityId b_handle, absl::Time t,
                            tf2::Stamped<Pose3d>& output) const;

  // Returns the common ancestor of the two given handles, the handles must be
  // present within the world.
  absl::StatusOr<AttachmentEntityId> FindCommonAncestor(
      AttachmentEntityId a_handle, AttachmentEntityId b_handle) const override;

  // Returns a map of labels to the entity ids that match with the key label.
  WorldHashMap<LabelId, WorldHashSet<EntityId>> GetLabelsMap() const override;

  // Marks the given connection between a_handle and b_handle as inaccurate or
  // not inaccurate based on the given input. NOTE: There must be a child/parent
  // relationship between from and to. Either from is the parent and to is the
  // child or vice versa.
  absl::Status MarkTransformInaccuracy(AttachmentEntityId a_handle,
                                       AttachmentEntityId b_handle,
                                       bool inaccurate) override;

  // Returns any connections that are marked inaccurate between a_handle and
  // b_handle. a_handle and b_handle do not have to be directly connected.
  // The returned pairs are ordered so that the first element of each pair is
  // closer to a_handle in the attachment graph and the second element is closer
  // to b_handle.
  WorldHashSet<std::pair<AttachmentEntityId, AttachmentEntityId>>
  GetInaccurateTransforms(AttachmentEntityId a_handle,
                          AttachmentEntityId b_handle) const override;

  // Updates exactly one inaccurate transform between two physical objects,
  // clearing the inaccurate flag in the process. Returns an error if there are
  // 0 or more than 1 inaccurate transform between the two entities. If there is
  // only one inaccurate transform we return the entity id of the child entity
  // of the transform. That is if we have a chain entities of A -> C -> D -> B,
  // and we have the inaccuare transform between C and D, then the entity id for
  // D would be returned.
  absl::StatusOr<AttachmentEntityId> UpdateIndirectTransform(
      AttachmentEntityId a_handle, AttachmentEntityId b_handle,
      const Pose3d& a_t_b,
      std::optional<absl::Time> timestamp = std::nullopt) override;

  // ------------------------------------
  // -- CollisionWorld methods
  // ------------------------------------

  // Adds a collision component to the given entity.
  absl::StatusOr<CollisionEntityId> CreateCollisionComponent(
      EntityId id) override;

  // Add the input pair to the list of pairwise ids that should not be
  // collision checked against each other.
  absl::Status AddExclusionPair(PhysicalEntityId first,
                                PhysicalEntityId second) override;

  // Remove the input pair from the list of pairwise ids that should not be
  // collision checked against each other. The order of the object_ids in the
  // pair is not importnat.
  absl::Status RemoveExclusionPair(PhysicalEntityId first,
                                   PhysicalEntityId second) override;

  // Cleans up the collision exclusions in the world by removing any non-mutual
  // exclusions (including "dangling" exclusions with non-existing entity ids).
  // I.e., for any pair of entities A and B we remove the collision exclusion
  // A->B if the collision exclusion B->A is not present or if B does not exist.
  absl::Status RemoveNonmutualExclusions() override;

  // Wipe the list of pairwise ids that should not be collision checked against
  // each other.
  void ClearExclusionPairs() override;

  // Return the list of pairwise object ids that are not collision checked
  // against each other.
  std::vector<std::pair<PhysicalEntityId, PhysicalEntityId>> GetExclusionPairs(
      bool filter_empty_collision_geometry) const override;

  // Build and return a CollisionChecker object from the input dynamic object
  // list with the given rule set applied over the default rule set. If
  // `collision_checker_config` is unconfigured or omitted (CONFIG_NOT_SET),
  // defaults to `kDefaultCollisionCheckerConfigCase`.
  absl::StatusOr<std::shared_ptr<CollisionChecker>> GetCollisionChecker(
      const WorldHashSet<PhysicalEntityId>& dynamic_objects,
      const intrinsic_proto::RuleSet& rule_set,
      const intrinsic_proto::world::CollisionCheckerConfig&
          collision_checker_config = {}) const override;

  // Build and return a CollisionChecker object from the input dynamic object
  // list with the default rule set. If `collision_checker_config` is
  // unconfigured or omitted (CONFIG_NOT_SET), defaults to
  // `kDefaultCollisionCheckerConfigCase`.
  absl::StatusOr<std::shared_ptr<CollisionChecker>> GetCollisionChecker(
      const WorldHashSet<PhysicalEntityId>& dynamic_objects,
      const intrinsic_proto::world::CollisionCheckerConfig&
          collision_checker_config = {}) const override;

  // Return true if the two object ids appear in the exclusion map.
  bool IsCollisionExcluded(PhysicalEntityId object_id_1,
                           PhysicalEntityId object_id_2) const override;

  // Get the mesh, belonging to this object_id in model space. Merging the
  // geometry before returning.
  NamedGeometrySet GetLocallyTransformedShapes(
      PhysicalEntityId object_id) const override {
    return GetLocallyTransformedShapes(object_id, true);
  }

  // Get the mesh, belonging to this object_id in model space. Optionally
  // merging the geometry before returning.
  NamedGeometrySet GetLocallyTransformedShapes(PhysicalEntityId object_id,
                                               bool merge) const;

  // Get the combined shapes of the input object_id in their current transform
  // relative to world frame. Note this does not change the representations from
  // models space to world space or vice versa. Merging the geometry before
  // returning.
  NamedGeometrySet GetGlobalyTransformedShapes(
      PhysicalEntityId object_id) const override {
    return GetGlobalyTransformedShapes(object_id, true);
  }

  // Get the combined shapes of the inpput object_id in their current transform
  // relative to world frame. Note this does not change the representations from
  // models space to world space or vice versa. Optionally merging the geometry
  // before returning.
  NamedGeometrySet GetGlobalyTransformedShapes(PhysicalEntityId object_id,
                                               bool merge) const;

  // Get the object ids that are excluded from the input list.
  WorldHashSet<PhysicalEntityId> GetExcludedObjectdIds(
      const WorldHashSet<PhysicalEntityId>& object_ids) const override;

  // Return true if no collision geometry is defined. This can be due to not
  // being a CollisionComponent or a GeometryComponent.
  bool HasEmptyCollisionGeometry(const EntityId& id) const override;

  // Return the geometry named `kind` associated with an entity of the given
  // id. Returns an error if the id or the geometry is not found.
  absl::StatusOr<NamedGeometrySet> GetGeometryForEntity(
      EntityId id, absl::string_view kind) const override;

  // Create a single static spatial tree representative of the input static
  // physical objects in world space.
  NamedGeometrySet GetStaticSpatialTreeInWorldSpace(
      const WorldHashSet<PhysicalEntityId>& static_object_ids) const override;

  // Given object_id find its memoized SpatialTree representation in model space
  // and return its key.
  NamedGeometrySet GetSpatialTreeInModelSpace(
      PhysicalEntityId object_id) const override;

  // Returns the default rule set for the world.
  //
  // The default rule set is used in the event that a rule set is not provided
  // when constructing a collision checker. If a rule set is provided during
  // collision checker construction, we will use this one as a default and the
  // provided one as an override.
  intrinsic_proto::RuleSet GetDefaultRuleSet() const override;

  // Sets the default rule set for this world, please refer to documentation of
  // GetDefaultRuleSet() for more details about the default rule set.
  absl::Status SetDefaultRuleSet(
      const intrinsic_proto::RuleSet& rule_set) override;

  // ------------------------------------
  // -- Grouping methods
  // ------------------------------------

  // Returns the set of valid GroupIds (which is a subset of the LabelIds found
  // on all Entities).
  ABSL_DEPRECATED(
      "GroupIds are deprecated, and this function should only be called by "
      "EntityGrouping. Use labels on Entities instead.")
  const WorldHashSet<GroupId>& GetGroupIds() const override;

  // Attempts to add a GroupId to the set of valid GroupIds. Returns an error if
  // the group already exists or an equivalent LabelId exists on an Entity.
  ABSL_DEPRECATED(
      "GroupIds are deprecated; use LabelIds on Entities instead. This "
      "function should only be called by EntityGrouping.")
  absl::Status AddGroupId(const GroupId& group_id) override;

  // Attempts to remove a GroupId to the set of valid GroupIds. Returns an error
  // if the group does not exist.
  ABSL_DEPRECATED(
      "GroupIds are deprecated; use LabelIds on Entities instead. This "
      "function should only be called by EntityGrouping.")
  absl::Status RemoveGroupId(const GroupId& group_id) override;

  // ------------------------------------
  // -- KinematicWorld methods
  // ------------------------------------

  // Checks if the DoF associated with joint_handle can be set to raw_value.
  absl::Status CheckDofRawValue(JointEntityId joint_handle,
                                double raw_value) const override;

  // Attempts to sets the raw value of the specified DoF. If successful, updates
  // the DoF's raw value and affected transforms (some of which may be
  // associated with dependent DoFs).
  absl::Status SetDofRawValue(
      JointEntityId joint_handle, double raw_value, bool enforce_limits,
      std::optional<absl::Time> timestamp = std::nullopt) override;

  // Given a RobotCollectionsEntityId, returns the ordered list of DoFs in that
  // robot. (Note that unlike getting the joints directly from
  // CollectionsComponent, this function will filter out fixed and fully
  // dependent joints.)
  absl::StatusOr<std::vector<JointEntityId>> GetRobotDofs(
      RobotCollectionsEntityId robot_handle) const override;

  // Returns all entities that are leaves of the robot's kinematic tree.
  absl::StatusOr<WorldHashSet<AttachmentEntityId>> GetFinalEntitiesOfRobot(
      CollectionsEntityId robot_id) const override;

  // Checks if robot_id represents a linear kinematic chain. If it does, returns
  // the ID of the final entity of that chain (usually a link). If it does not,
  // returns kInvalidEntityId.
  absl::StatusOr<AttachmentEntityId> GetFinalEntityOfRobotKinematicChain(
      CollectionsEntityId robot_id) const override;

  // Check whether a robot is a kinematic chain or not.
  absl::StatusOr<bool> IsChainKinematicChain(
      RobotCollectionsEntityId robot_id) const override;

  // Returns an ordered set of RobotCollectionsEntityIds corresponding to the
  // set of Robots (in order of appearance) in the chain of entities from id1
  // to id2. id1 must be an ancestor of id2 in the attachment tree.
  absl::StatusOr<std::vector<RobotCollectionsEntityId>> GetRobotIdsInChain(
      AttachmentEntityId id1, AttachmentEntityId id2) const override;

  // Refreshes a DoF's parent_t_this (in its AttachmentComponent) from data in
  // its (and possibly other DoFs') KinematicsComponent(s).
  absl::Status RefreshDofParentTThis(
      JointEntityId joint_handle,
      std::optional<absl::Time> timestamp = std::nullopt);

  // Returns a set of CartesianKinematicView. These views correspond to DOFs
  // that can be used to position id2's Entity with respect to id1's Entity. A
  // set is returned because there can be multiple independent ways of moving
  // the given objects. If no DOFs can modify the pose between the objects then
  // an empty set is returned.
  //
  // An example usage might be to pass in the gripper pickup point as one of the
  // object ids and the object being picked up as the second object id. Then
  // later when trying to pick up the object you would specify that you want the
  // pose between the two objects to be identity. This will solve IK and find
  // the correct Dofs to position the objects with respect to each other.
  // Another example might be to specify a glue dispenser attached to a robot
  // and an object that needs to have glue deposited on it attached to a
  // different robot. The returned kinematic views may control one or both arms
  // to ensure that when a pose is specified relative to the two objects it is
  // achieved.
  absl::StatusOr<std::vector<std::unique_ptr<CartesianKinematicView>>>
  GetCartesianKinematicViews(AttachmentEntityId id1,
                             AttachmentEntityId id2) override;

  absl::StatusOr<std::vector<std::unique_ptr<const CartesianKinematicView>>>
  GetCartesianKinematicViews(AttachmentEntityId id1,
                             AttachmentEntityId id2) const override;

  // Returns a cartesian view that correspond to the chain of DOFs between id1
  // and id2. As opposed to GetCartesianKinematicViews, this cartesian view can
  // span multiple robots connected in a chain.
  absl::StatusOr<std::unique_ptr<const CartesianKinematicView>>
  GetCartesianKinematicView(AttachmentEntityId id1,
                            AttachmentEntityId id2) const override;

  absl::StatusOr<std::unique_ptr<CartesianKinematicView>>
  GetCartesianKinematicView(AttachmentEntityId id1,
                            AttachmentEntityId id2) override;

  // Gets the DofKinematicView representing any dofs in the chain between the
  // two given entities. The resulting view will allow you to indirectly control
  // the pose between the two given entities. Any DOF on the entities themselves
  // that does not change the pose between them will not be part of the
  // resulting view.
  //
  // NOTE: This may cross robot boundaries or may be a subset of a robot.
  absl::StatusOr<std::unique_ptr<DofKinematicView>> GetDofKinematicView(
      AttachmentEntityId id1, AttachmentEntityId id2) override;

  // Gets the DofKinematicView representing any dofs in the chain between the
  // two given entities. The resulting view will allow you to indirectly control
  // the pose between the two given entities. Any DOF on the entities themselves
  // that does not change the pose between them will not be part of the
  // resulting view.
  //
  // NOTE: This may cross robot boundaries or may be a subset of a robot.
  absl::StatusOr<std::unique_ptr<const DofKinematicView>> GetDofKinematicView(
      AttachmentEntityId id1, AttachmentEntityId id2) const override;

  // Gets the DofKinematicView associated with the given joint ids.
  absl::StatusOr<std::unique_ptr<DofKinematicView>> GetDofKinematicView(
      const std::vector<JointEntityId>& joint_ids) override;
  absl::StatusOr<std::unique_ptr<const DofKinematicView>> GetDofKinematicView(
      const std::vector<JointEntityId>& joint_ids) const override;

  // Gets the DofKinematicView for a given RobotCollectionsEntity.
  absl::StatusOr<std::unique_ptr<DofKinematicView>> GetDofKinematicView(
      RobotCollectionsEntityId robot_id) override;
  absl::StatusOr<std::unique_ptr<const DofKinematicView>> GetDofKinematicView(
      RobotCollectionsEntityId robot_id) const override;

  // Returns a DofKinematicView for the specified robot GroupId. This function
  // is intended to match the behavior of Robots::GetDofKinematicView(), meaning
  // the returned view does not necessarily represent all of the DoFs in the
  // robot group.
  ABSL_DEPRECATED(
      "GroupIds are deprecated. Prefer to use World functions that take "
      "RobotCollectionsEntityId arguments.")
  absl::StatusOr<std::unique_ptr<DofKinematicView>>
  GetDofKinematicViewForRobotGroupId(GroupId robot_group_id) override;
  ABSL_DEPRECATED(
      "GroupIds are deprecated. Prefer to use World functions that take "
      "RobotCollectionsEntityId arguments.")
  absl::StatusOr<std::unique_ptr<const DofKinematicView>>
  GetDofKinematicViewForRobotGroupId(GroupId robot_group_id) const override;

  // Builds a skeleton for the kinematic chain between id1 and id2.
  //
  // Joints in the chain are ordered to match their order in the robots in this
  // world, so the root of the chain may be either `id1` or `id2`. When `id1` is
  // an ancestor of `id2`, the root of the chain will be `id1`.
  absl::StatusOr<std::unique_ptr<kinematics::Skeleton>> BuildChainSkeleton(
      AttachmentEntityId id1, AttachmentEntityId id2) const;

  // ------------------------------------
  // -- Robots methods
  // ------------------------------------

  // Attempts to get the base link of a robot collections entity.
  absl::StatusOr<LinkEntityId> GetBaseLink(
      CollectionsEntityId robot_id) const override;

  // Creates and returns new robot and link entities, with the link as a child
  // of the specified parent entity.
  absl::StatusOr<std::pair<RobotCollectionsEntityId, LinkEntityId>>
  CreateRobotAndBaseLink(AttachmentEntityId parent_entity_id) override;

  // Creates and returns new joint and link entities as children of the
  // specified parent. If the parent is part of a robot, the new joint and link
  // entities will be added to that robot.
  absl::StatusOr<std::pair<JointEntityId, LinkEntityId>> AddJoint(
      AttachmentEntityId parent_id, const Pose3d& parent_t_inboard,
      intrinsic_proto::world::KinematicsComponent::MotionType motion_type,
      const eigenmath::Vector3d& axis, double value, double lower_limit,
      double upper_limit) override;

  // Creates and returns a new entity as a child of the specified parent link.
  // The parent link must be part of a robot, whose data will be updated by this
  // function.
  absl::StatusOr<RobotCoordinateFrameEntityId> AddCoordinateFrameToRobot(
      LinkEntityId parent_link_id,
      const Pose3d& parent_link_t_coordinate_frame) override;

  // Sorts the specified robot's link and joint lists according to the rules
  // described in intrinsic/world/proto/collections_component.proto.
  absl::Status SortRobotLinkAndJointLists(
      RobotCollectionsEntityId robot_id) override;

  // Removes RobotCollectionsEntities, their member Entities, and any children
  // of those Entities.
  absl::Status RemoveRobots(
      const std::vector<RobotCollectionsEntityId>& robot_ids) override;

  // Returns the set of GroupIds that have RobotCollectionsEntityIds associated
  // with them (i.e. the GroupIds that are valid arguments to
  // GetRobotCollectionsEntityIdsForRobotGroupId).
  ABSL_DEPRECATED(
      "This function should only be called by EntityRobots. Instead, use "
      "World-level functions to interact directly with robots.")
  WorldHashSet<GroupId> GetRobotGroupIds() const override;

  // Returns an ordered list of RobotCollectionsEntityIds associated with the
  // provided GroupId.
  ABSL_DEPRECATED(
      "This function should only be called by EntityRobots. Instead, use "
      "World-level functions to interact directly with robots.")
  absl::StatusOr<const std::vector<RobotCollectionsEntityId>*>
  GetRobotCollectionsEntityIdsForRobotGroupId(
      const GroupId& group_id) const override;

  // Sets the ordered list of RobotCollectionsEntityIds associated with the
  // specified GroupId, overwriting any previous value. An empty list will
  // disable the GroupId.
  ABSL_DEPRECATED(
      "This function should only be called by EntityRobots. Instead, use "
      "World-level functions to interact directly with robots.")
  void SetRobotCollectionsEntityIdsForRobotGroupId(
      const GroupId& group_id,
      const std::vector<RobotCollectionsEntityId>& robot_ids) override;

  // Returns the base link that is the ancestor of all other robots' base links
  // in the group. Returns an error if the robots in the group do not share a
  // common base link.
  ABSL_DEPRECATED(
      "Prefer to use GetBaseLink() which operates on "
      "RobotCollectionsEntityIds.")
  absl::StatusOr<AttachmentEntityId> GetBaseLinkForRobotGroupId(
      const GroupId& group_id) const override;

  // Returns the most distal entity (from the world root) of the robots in the
  // specified group. Returns an error if the robots in the group are not
  // attached to each other.
  ABSL_DEPRECATED(
      "This function should only be called by EntityRobots. Instead, use "
      "World-level functions to interact directly with robots.")
  absl::StatusOr<AttachmentEntityId> GetFinalEntityForRobotGroupId(
      const GroupId& group_id) const override;

  // Sets a tip for a robot GroupId, which enables
  // GetCartesianKinematicViewForRobotGroupId().
  ABSL_DEPRECATED(
      "This function should only be called by EntityRobots. Instead, use "
      "World-level functions to interact directly with robots.")
  absl::Status SetTipIdForRobotGroupId(const GroupId& group_id,
                                       PhysicalEntityId tip_object_id) override;

  // Returns a CartesianKinematicView for the specified robot. The returned view
  // does not necessarily represent all of the DoFs in the robot.
  absl::StatusOr<std::unique_ptr<CartesianKinematicView>>
  GetCartesianKinematicView(RobotCollectionsEntityId robot_id);

  // Returns a CartesianKinematicView for the specified robot. The returned view
  // does not necessarily represent all of the DoFs in the robot.
  absl::StatusOr<std::unique_ptr<const CartesianKinematicView>>
  GetCartesianKinematicView(RobotCollectionsEntityId robot_id) const;

  // Returns a CartesianKinematicView for the specified robot GroupId. This
  // function is intended to match the behavior of
  // Robots::GetCartesianKinematicView(), meaning the returned view does not
  // necessarily represent all of the DoFs in the robot group.
  ABSL_DEPRECATED(
      "GroupIds are deprecated. Prefer to use World functions that take "
      "RobotCollectionsEntityId arguments.")
  absl::StatusOr<std::unique_ptr<CartesianKinematicView>>
  GetCartesianKinematicViewForRobotGroupId(const GroupId& group_id) override;
  ABSL_DEPRECATED(
      "GroupIds are deprecated. Prefer to use World functions that take "
      "RobotCollectionsEntityId arguments.")
  absl::StatusOr<std::unique_ptr<const CartesianKinematicView>>
  GetCartesianKinematicViewForRobotGroupId(
      const GroupId& group_id) const override;

  // Sets the DofId for an EntityId.
  ABSL_DEPRECATED(
      "This function should only be called by EntityRobots. Use JointEntityIds "
      "instead of DofIds.")
  void SetDofIdForEntityId(DofId dof_id, EntityId entity_id) override;

  // Returns an ordered list of DofIds for the specified robot GroupId.
  ABSL_DEPRECATED(
      "This function should only be called by EntityRobots. Instead, use "
      "World-level functions to interact directly with robots.")
  absl::StatusOr<std::vector<DofId>> GetDofIdsForRobotGroupId(
      const GroupId& group_id) const override;

  // ------------------------------------
  // -- Collection methods
  // ------------------------------------

  absl::StatusOr<std::set<CollectionsMemberEntityId>> GetCollectionMembers(
      CollectionsEntityId collections_id) const;

  // Returns the resource name of the entity, if any.
  //
  // Returns std::nullopt if the entity does have a CollectionsMemberComponent.
  std::optional<std::string> GetResourceName(const WorldEntity& entity) const;

  // Tries to get an object name for a collection based on alias or local name.
  // It checks the collection entity's alias, then the root entity's alias,
  // then the collection entity's local name.
  std::optional<std::string> TryGetObjectNameFromCollection(
      CollectionsEntityId collection_entity_id) const;
  // Tries to get the name of the object to which this entity belongs, or the
  // name of the entity itself if it is the object.
  //
  // If the name of an entity representing an object is requested, then it is
  // returned directly. For entities that are part of objects (links, joints,
  // etc.), we return "<object_name>.<local_name>" which should be a unique id.
  //
  // The naming patterns are as follows:
  //   * "object_name.local_name" for any entity which is part of an object
  //   * "object_name" if the entity itself is the object collection
  //   * "<alias.local_name,eid=XYZ>" for other objects
  //
  // The last case is meant to catch entities that aren't objects. It should
  // almost never occur, so we've opted for a more detailed (and less
  // user-friendly) name to make sure we can quickly identify these cases.
  absl::StatusOr<std::string> TryGetObjectNameForEntity(
      EntityId entity_id) const override;

  // Try to create a simple "tf-style" frame name for this entity, by using the
  // parent resource name (if any) and the local_name
  //
  // - If the entity is the root, returns "root"
  // - If the local_name is empty, substitutes it with the entity id
  //
  // - If the entity belongs to a resource, prefixes with the resource name.
  //   - For example: "resource_name/local_name"
  //
  // - If the entity does not belong to a resource, but the entity has a single
  //   label, prefixes with the label instead
  //   - For example: "label/local_name"
  absl::StatusOr<std::string> EntityIdToTfFrameName(EntityId entity_id) const;

  // Try to create a fully qualified "tf-style" frame name for this entity, by
  // using the parent resource name (if any) and the local_name
  //
  // Similar to EntityIdToTfFrameName(), but the frame name is fully qualified,
  // which means that it includes the names of all ancestors.
  //
  // For example, if the following transforms exist in the attachment graph:
  //   - RootEntity -> EntityA
  //   - EntityB -> EntityC
  //
  // Then the fully qualified frame name for EntityC is "{W}/{X}/{Y}/{Z}",
  // where the each of the substituted frame name elements are:
  //   - W: EntityIdToFrameName(RootEntity)
  //   - X: EntityIdToFrameName(EntityA)
  //   - Y: EntityIdToFrameName(EntityB)
  //   - Z: EntityIdToFrameName(EntityC)
  //
  // Additionally, will only apply resource name prefixes to entities that are
  // the root of their resource subtree.
  //
  // For example, for the following graph:
  //   - root -> resource_A/x -> resource_A/y -> z
  //
  // The fully qualified frame name will be:
  //   - root/resource_A/x/y/z
  // Instead of:
  //   - root/resource_A/x/resource_A/y/z
  //
  // You may also pass in a hash map of frame_id_to_fully_qualified_frame_id,
  // for memoization, to avoid walking up the attachment tree multiple times.
  absl::StatusOr<std::string> EntityIdToFullyQualifiedTfFrameName(
      EntityId entity_id,
      WorldHashMap<EntityId, std::string>*
          entity_id_to_fully_qualified_frame_name = nullptr) const;

  // Create a TF message of the transforms present in this world
  absl::StatusOr<intrinsic_proto::TFMessage> ConstructTfMessage() const;

  // Update the transform buffer from a TFMessage. This allows the re-use of
  // the work completed in PopulateTfMessage(), which is virtually identical
  absl::Status UpdateTransformBuffer(
      const intrinsic_proto::TFMessage& tf_message) const;

  // Create a TF associations message containing fully qualified TF frames and
  // their geometries. If strip_inline_renderables is true, then any inline
  // renderables in the geometries will be stripped out to reduce payload size.
  absl::StatusOr<intrinsic_proto::world::TFAssociations>
  ConstructTfAssociations(bool strip_inline_renderables = true) const;

  std::shared_ptr<CollisionCheckCache> GetCachedChecks() const override {
    return inner_world_->GetCachedChecks();
  }

 private:
  friend class EntityCollisionWorldTest;

  explicit World(
      std::shared_ptr<world_internal::InnerWorld> inner_world) noexcept;

  // Helper function for implementing clone method. This assumes that the
  // correct inner_world has already been set/cloned before calling the method.
  void CloneFrom(const World& other, const WorldHashSet<EntityId>& subset_ids);

  WorldHashSet<AttachmentEntityId> FindByLocalNamesImpl(
      AttachmentEntityId root, bool is_exact,
      const std::vector<std::string>& names) const;

  // ------------------------------------
  // -- Parsing related helper functions
  // ------------------------------------

  std::optional<std::string> GetTfPrefix(const WorldEntity& entity) const;

  absl::Status ParseWorldData(
      const intrinsic_proto::world::internal::World& world_data,
      const GeometryDeserializer* geolib);
  absl::Status ParseWorldData(const GZFile& world_data);

  absl::Status ParseWorldDataImpl(
      const intrinsic_proto::world::internal::World& world_proto,
      const GeometryDeserializer* geolib);

  absl::Status UpdateFromProtoImpl(
      const intrinsic_proto::world::internal::World& world_proto,
      const GeometryDeserializer* geolib);

  absl::StatusOr<intrinsic_proto::world::internal::World> SerializeImpl(
      GeometrySerializer* geolib) const;

  absl::StatusOr<WorldHashMap<EntityId, EntityId>> IntegrateFragmentImpl(
      const AttachmentEntityId& to_attach_to, uint16_t entity_id_prefix,
      const intrinsic_proto::world::WorldFragment& input_fragment,
      const GeometryDeserializer* geolib);

  // ------------------------------------
  // -- KinematicWorld-related helper functions
  // ------------------------------------

  // InnerWorld representing the entities and the core functionality of this
  // instance of World.
  std::shared_ptr<world_internal::InnerWorld> inner_world_;

  using SpatialTreesMap = WorldHashMap<PhysicalEntityId, NamedGeometrySet>;
  // Cache for the keys to object store for SpatialTrees.
  // Note: It is important to keep this cache for performance purposes.
  // An attempt to remove it resulted in 110% performance reduction.
  // TODO(stoyang): Move this into the geometry component of each entity
  std::shared_ptr<SpatialTreesMap> model_space_spatial_trees_;

  // TODO(stoyang): Remove aspects and support data!
  std::unique_ptr<entity_grouping_details::EntityGrouping> grouping_aspect_;
  WorldHashMap<DofId, EntityId> dof_id_to_entity_id_;
  WorldHashSet<GroupId> group_ids_;
  WorldHashMap<GroupId, std::unique_ptr<std::vector<RobotCollectionsEntityId>>>
      robot_group_id_to_robot_collections_entity_ids_;

  // Integrates the given entities in the world.
  // The entities are mapped to a new ID following the other_to_this_id_mapping.
  // In addition checks on the parent and alias are done to ensure that the
  // entities can be included.
  absl::Status IntegrateEntitiesImpl(
      const WorldHashMap<EntityId, const WorldEntity*>& other_entities,
      WorldHashMap<EntityId, EntityId> other_to_this_id_mapping);
};

template <typename... ComponentTypes>
std::vector<world_entity_details::TypedResult<ComponentTypes...>>
World::GetTypedEntityIds() const {
  std::vector<world_entity_details::TypedResult<ComponentTypes...>> results;
  ASSIGN_OR_DIE(auto all_entities, inner_world_->GetAllEntities());
  for (const auto& [id, entity] : all_entities) {
    if (entity->IsEntityValid<ComponentTypes...>()) {
      results.emplace_back(id);
    }
  }

  return results;
}

template <typename EntityType>
EntityType World::CreateTypedEntity() {
  EntityId ret = CreateEntity();
  ASSIGN_OR_DIE(WorldEntity * entity, GetEntityById(ret));
  CHECK_OK(entity->CreateComponentsByType<EntityType>());
  return EntityType(ret);
}

template <typename... ComponentTypes>
world_entity_details::TypedResult<ComponentTypes...>
World::CreateEntityWithComponentTypes() {
  EntityId ret = CreateEntity();
  ASSIGN_OR_DIE(WorldEntity * entity, GetEntityById(ret));
  CHECK_OK(entity->CreateComponentsByType<ComponentTypes...>());
  return world_entity_details::TypedResult<ComponentTypes...>{ret};
}

template <typename... ComponentTypes>
absl::StatusOr<world_entity_details::TypedResult<ComponentTypes...>>
World::ValidateEntity(EntityId id) const {
  INTR_ASSIGN_OR_RETURN(const WorldEntity* entity, GetEntityById(id));
  INTR_RETURN_IF_ERROR(entity->ValidateEntity<ComponentTypes...>());
  return world_entity_details::TypedResult<ComponentTypes...>{id};
}

template <typename... ComponentTypes>
WorldHashSet<world_entity_details::TypedResult<ComponentTypes...>>
World::ValidateByLabel(const LabelId& label) const {
  WorldHashSet<world_entity_details::TypedResult<ComponentTypes...>> ret;
  for (auto candidate : FindByLabel(label)) {
    auto typed_id_or = ValidateEntity<ComponentTypes...>(candidate);
    if (typed_id_or.ok()) {
      ret.emplace(typed_id_or.value());
    }
  }
  return ret;
}

template <typename... ComponentTypes>
absl::StatusOr<
    std::vector<world_entity_details::TypedResult<ComponentTypes...>>>
World::ValidateCollectionMembers(
    CollectionsEntityId collections_id,
    intrinsic_proto::world::CollectionsComponent::CollectionType type) const {
  INTR_ASSIGN_OR_RETURN(
      const CollectionsComponent* collections,
      GetComponentByEntityId<CollectionsComponent>(collections_id));
  const std::vector<CollectionsMemberEntityId>& members =
      collections->GetCollectionMembers(type);
  std::vector<world_entity_details::TypedResult<ComponentTypes...>> ret;
  ret.reserve(members.size());
  for (CollectionsMemberEntityId member : members) {
    INTR_ASSIGN_OR_RETURN(const WorldEntity* member_ent, GetEntityById(member));
    if (!member_ent->IsEntityValid<ComponentTypes...>()) {
      return intrinsic::InternalErrorBuilder()
             << "could not validate member of collection type "
             << intrinsic_proto::world::CollectionsComponent::
                    CollectionType_Name(type);
    }
    ret.emplace_back(member.value());
  }
  return ret;
}

template <typename... ComponentTypes>
absl::StatusOr<
    std::vector<world_entity_details::TypedResult<ComponentTypes...>>>
World::FilterCollectionMembers(CollectionsEntityId collections_id) const {
  INTR_ASSIGN_OR_RETURN(
      const CollectionsComponent* collections,
      GetComponentByEntityId<CollectionsComponent>(collections_id));
  std::set<CollectionsMemberEntityId> members =
      collections->GetAllCollectionMembers();
  std::vector<world_entity_details::TypedResult<ComponentTypes...>> ret;
  ret.reserve(members.size());
  for (CollectionsMemberEntityId member : members) {
    INTR_ASSIGN_OR_RETURN(const WorldEntity* member_ent, GetEntityById(member));
    if (member_ent->IsEntityValid<ComponentTypes...>()) {
      ret.emplace_back(member.value());
    }
  }
  return ret;
}

template <typename... ComponentTypes>
absl::StatusOr<world_entity_details::TypedResult<ComponentTypes...>>
World::ValidateCollectionParentAmongTypes(
    EntityId member_id,
    const WorldHashSet<
        intrinsic_proto::world::CollectionsComponent::CollectionType>& types)
    const {
  INTR_ASSIGN_OR_RETURN(
      const auto* collections_member,
      GetComponentByEntityId<CollectionsMemberComponent>(member_id));
  INTR_ASSIGN_OR_RETURN(
      CollectionsEntityId parent_candidate,
      collections_member->FindParentCollectionAmongTypes(types));
  INTR_ASSIGN_OR_RETURN(const WorldEntity* parent_candidate_ent,
                        GetEntityById(parent_candidate));
  INTR_RETURN_IF_ERROR(
      parent_candidate_ent->ValidateEntity<ComponentTypes...>())
      << "while trying to validate collection parent of entity with ID "
      << member_id.value();
  return world_entity_details::TypedResult<ComponentTypes...>(
      parent_candidate.value());
}

template <typename Component>
absl::StatusOr<const Component*> World::GetComponentByEntityId(
    EntityId id) const {
  INTR_ASSIGN_OR_RETURN(const WorldEntity* entity, GetEntityById(id));
  INTR_ASSIGN_OR_RETURN(const auto* component,
                        entity->GetComponent<Component>(),
                        _ << "with id: " << id);
  return component;
}

template <typename Component>
absl::StatusOr<Component*> World::GetComponentByEntityId(EntityId id) {
  INTR_ASSIGN_OR_RETURN(WorldEntity * entity, GetEntityById(id));
  INTR_ASSIGN_OR_RETURN(auto* component, entity->GetComponent<Component>(),
                        _ << "with id: " << id);
  return component;
}

template <typename Component>
absl::StatusOr<const Component*> World::GetOrCreateComponentByEntityId(
    EntityId id) const {
  INTR_ASSIGN_OR_RETURN(const WorldEntity* entity, GetEntityById(id));
  INTR_ASSIGN_OR_RETURN(const auto* component,
                        entity->GetOrCreateComponent<Component>(),
                        _ << "with id: " << id);
  return component;
}

template <typename Component>
absl::StatusOr<Component*> World::GetOrCreateComponentByEntityId(EntityId id) {
  INTR_ASSIGN_OR_RETURN(WorldEntity * entity, GetEntityById(id));
  INTR_ASSIGN_OR_RETURN(auto* component,
                        entity->GetOrCreateComponent<Component>(),
                        _ << "with id: " << id);
  return component;
}

#define INTRINSIC_WORLD_AS_ASPECT_HELPER(Type, Variable) \
  template <>                                            \
  inline Type& World::As<Type>() {                       \
    return *Variable;                                    \
  }                                                      \
  template <>                                            \
  inline const Type& World::As<Type>() const {           \
    return *Variable;                                    \
  }

INTRINSIC_WORLD_AS_ASPECT_HELPER(Grouping, grouping_aspect_)
#undef INTRINSIC_WORLD_AS_ASPECT_HELPER

}  // namespace intrinsic

#endif  // INTRINSIC_WORLD_WORLD_H_
