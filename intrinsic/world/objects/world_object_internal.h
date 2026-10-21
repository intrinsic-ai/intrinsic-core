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

#ifndef INTRINSIC_WORLD_OBJECTS_WORLD_OBJECT_INTERNAL_H_
#define INTRINSIC_WORLD_OBJECTS_WORLD_OBJECT_INTERNAL_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/base/nullability.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/any.pb.h"
#include "intrinsic/geometry/api/geometry_options.h"
#include "intrinsic/geometry/storage/geometry_library.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/resources/proto/geometric_resource_data.pb.h"
#include "intrinsic/scene/proto/v1/scene_object.pb.h"
#include "intrinsic/scene/util/scene_object_updates.h"
#include "intrinsic/world/component/geometry_component.h"
#include "intrinsic/world/component/simulation_component.h"
#include "intrinsic/world/entity_id.h"
#include "intrinsic/world/hashing/hashing.h"
#include "intrinsic/world/objects/frame_internal.h"
#include "intrinsic/world/objects/object_entity_filter.h"
#include "intrinsic/world/objects/object_world_data.h"
#include "intrinsic/world/objects/object_world_ids.h"
#include "intrinsic/world/objects/transform_node_internal.h"
#include "intrinsic/world/proto/geometry_component.pb.h"
#include "intrinsic/world/proto/object_world_updates.pb.h"
#include "intrinsic/world/proto/simulation_component.pb.h"
#include "intrinsic/world/proto/user_data.pb.h"
#include "intrinsic/world/util/walk_attachment_tree.h"

namespace intrinsic {
namespace object_world {

class KinematicObject;
class PhysicalObject;
class RootObject;

// Interface for the visitor pattern on WorldObjects. Implementations can be
// used with WorldObject::Accept().
class WorldObjectVisitor {
 public:
  virtual ~WorldObjectVisitor() = default;
  virtual absl::Status Visit(RootObject& root_object) = 0;
  virtual absl::Status Visit(PhysicalObject& physical_object) = 0;
  virtual absl::Status Visit(KinematicObject& kinematic_object) = 0;
};

// Const variant of WorldObjectVisitor.
class WorldObjectConstVisitor {
 public:
  virtual ~WorldObjectConstVisitor() = default;
  virtual absl::Status Visit(const RootObject& root_object) = 0;
  virtual absl::Status Visit(const PhysicalObject& physical_object) = 0;
  virtual absl::Status Visit(const KinematicObject& kinematic_object) = 0;
};

// Abstract base class for objects in the object-based view onto a world (see
// ObjectWorld).
class WorldObject : public TransformNode {
 public:
  WorldObject(ObjectWorldResourceId id, WorldObjectName name,
              WorldHashSet<AttachmentEntityId> entity_ids,
              ObjectWorldData& data);

  // Returns the name of the object. The name is guaranteed to be unique amongst
  // all objects in a world if NameIsGlobalAlias() is true. The name is
  // guaranteed to be unique among its siblings and any other global names.
  const WorldObjectName& GetName() const { return name_; }

  virtual absl::StatusOr<bool> NameIsGlobalAlias() const = 0;

  absl::StatusOr<WorldObjectNameType> GetNameType() const;

  // Returns an available object name that can be used on a child object. The
  // returned name will not conflict with other objects and global frames based
  // on the suggested_name and name_type provided. If suggested_name is
  // available, it will be returned. If suggested name is not available, a name
  // in the form of suggested_name + a suffix will be returned.
  absl::StatusOr<WorldObjectName> GetAvailableChildObjectName(
      WorldObjectName suggested_name, WorldObjectNameType name_type) const;

  // Returns the object names from the root object(excluded) to this
  // object(included) in order.
  std::vector<WorldObjectName> GetFullPathName() const;

  // Sets the name of this object.
  // If name_is_global_alias is true, the name is expected to be globally
  // unique. If name_is_global_alias is false, the name is expected to be
  // locally unique among its sibling objects and frames. Returns an error if
  // the new name is not valid or if called on the root object.
  virtual absl::Status SetName(const WorldObjectName& name,
                               bool name_is_global_alias) = 0;

  // Reparents this object to the given object. The global pose of this object
  // remains unaffected, i.e., "parent_t_this" might change but "root_t_this"
  // will not change.
  //
  // Method variants:
  // - "ReparentTo" will attach to the base/origin of 'new_parent'. If
  //   'new_parent' is a kinematic object and thus has more than one object
  //   entity we will attach to the first/base object entity.
  // - "ReparentTo" with filter will attach to the specified entity of
  //   'new_parent'.
  // - "ReparentToFinalEntityOf" will attach to the final object entity of
  //   'new_parent'. If 'new_parent' is not a kinematic object and thus consists
  //   of a single entity, this method is equivalent to "ReparentTo". If
  //   'new_parent' is a kinematic object and the final entity cannot be
  //   determined uniquely, an error will be returned.
  virtual absl::Status ReparentTo(WorldObject& new_parent) = 0;
  virtual absl::Status ReparentTo(WorldObject& new_parent,
                                  const world::ObjectEntityFilter& filter) = 0;
  virtual absl::Status ReparentToFinalEntityOf(WorldObject& new_parent) = 0;

  // Deletes this object and all frames grouped under it, or returns an error if
  // this object has attached child objects which would be left dangling by this
  // operation.
  //
  // All references to this object and its frames will become invalid after this
  // operation.
  absl::Status DeleteIfNoChildObjects();

  // Recursively deletes this object and all (directly or indirectly) attached
  // child objects, including all frames grouped under the deleted objects.
  //
  // All references to the deleted objects and frames will become invalid after
  // this operation.
  absl::Status DeleteIncludingChildObjects();

  // Reparents all child objects to the parent of this object, then deletes
  // this object and all of its frames.
  // Returns an error if called on the root object.
  absl::Status ReparentChildObjectsAndDelete();

  // Disables collision detection between this object and the given 'other'
  // object. Changes apply only to the subset of entities of this and the other
  // object as selected by the given ObjectEntityFilter's.
  //
  // Collisions will be disabled for all pairs (a,b) of entities where a is a
  // entity of this object and selected by 'this_entity_filter' and b is a
  // entity of the 'other' object and selected by 'other_entity_filter'. If
  // multiple entities are selected for one or both objects, collisions will
  // never be disabled between two entities that belong to the same object.
  //
  // Succeeds and has no effect if collisions were already disabled.
  absl::Status DisableCollisionsWith(
      WorldObject& other, const world::ObjectEntityFilter& this_entity_filter,
      const world::ObjectEntityFilter& other_entity_filter);

  // Enables collision detection between this object and the given 'other'
  // object. This is the counterpart to DisableCollisionsWith().
  //
  // Succeeds and has no effect if collisions were already enabled.
  absl::Status EnableCollisionsWith(
      WorldObject& other, const world::ObjectEntityFilter& this_entity_filter,
      const world::ObjectEntityFilter& other_entity_filter);

  // Returns the child objects. Can be empty if this object has no children.
  std::vector<WorldObject*> GetChildren();
  std::vector<const WorldObject*> GetChildren() const;

  // Returns all entities represented by this object, excluding any collection
  // entities (see GetCollectionEntity()). Most of the returned entities will be
  // part of the entity collection but this is not guaranteed.
  const WorldHashSet<AttachmentEntityId>& GetEntityIds() const {
    return entity_ids_;
  }

  // Returns the child frame with the given id. Returns an error if no matching
  // frame can be found.
  absl::StatusOr<Frame* absl_nonnull> GetFrame(
      const ObjectWorldResourceId& frame_id);
  absl::StatusOr<const Frame* absl_nonnull> GetFrame(
      const ObjectWorldResourceId& frame_id) const;

  // Returns the child frame with the given name. Returns an error if no
  // matching frame can be found.
  absl::StatusOr<Frame* absl_nonnull> GetFrame(const FrameName& frame_name);
  absl::StatusOr<const Frame* absl_nonnull> GetFrame(
      const FrameName& frame_name) const;

  // Returns all frames under this object, including ones that are attached
  // indirectly to this object via another child frame. The ordering is
  // arbitrary and may vary with each call to this method.
  std::vector<Frame*> GetFrames();
  std::vector<const Frame*> GetFrames() const;

  // Returns all frames under this object, including ones that are attached
  // indirectly to this object via another child frame. The returned frames are
  // sorted according to their names in lexicograhical order.
  std::vector<const Frame*> GetFramesSorted() const;

  // Returns all frames under this object, including ones that are attached
  // indirectly to this object via another child frame. The returned frames are
  // sorted partially according to their hierarchical relationship: parent
  // frames before child frames.
  std::vector<const Frame*> GetFramesPartiallySorted() const;

  // Returns all immediate child frames of this object, excluding ones that are
  // attached indirectly to this object via another child frame. The ordering is
  // arbitrary and may vary with each call to this method.
  std::vector<Frame*> GetChildFrames();
  std::vector<const Frame*> GetChildFrames() const;

  // Returns all immediate child frames of this object, excluding ones that are
  // attached indirectly to this object via another child frame. The returned
  // frames are sorted according to their names in lexicographical order.
  std::vector<const Frame*> GetChildFramesSorted() const;

  // Returns the name of the associated resource instance or nullopt if no
  // resource is associated with this object.
  absl::StatusOr<std::optional<std::string>> GetResourceName() const;

  // Sets the name of the associated resource instance. Only valid if this
  // object is neither a product part nor another resource.
  absl::Status SetResourceName(absl::string_view resource_name);

  // Applies the geometry option overrides for all the geometries of this
  // object. If a field is not set in the overrides, it will not modify the
  // equivalent field in the existing (or default) options for the geometry.
  absl::Status ApplyGeometryOptionOverrides(
      const intrinsic_proto::world::GeometryOptions& options,
      GeometryLibrary& geolib);

  // Applies the user_data to the object.
  absl::Status ApplyUserData(
      const intrinsic_proto::world::UpdateUserData& user_data_update);

  // Sets the frames of this object, overriting any existing ones. The caller is
  // responsible for ensuring that all given frames have unique names.
  absl::Status SetFrames(
      WorldHashMap<ObjectWorldResourceId, std::unique_ptr<Frame>> frames);

  // Returns the simulation component for this object or a not-found error if it
  // has none.
  absl::StatusOr<SimulationComponent* absl_nonnull> GetSimulationComponent();
  absl::StatusOr<const SimulationComponent* absl_nonnull>
  GetSimulationComponent() const;

  // Updates the object's simulation component to the given one. Creates
  // a new simulation component if none exists or otherwise overrides the
  // existing simulation component.
  absl::Status SetSimulationComponent(
      const intrinsic_proto::world::SimulationComponent& proto);

  // Returns the userdata for this object or a not-found error if it
  // has none.
  absl::StatusOr<const WorldHashMap<std::string, std::string>* absl_nonnull>
  GetUserDataMap() const;

  // Returns the mutable userdata for this object. If there is no userdata set
  // for this object, an empty user data component will be created and returned.
  absl::StatusOr<WorldHashMap<std::string, std::string>* absl_nonnull>
  GetMutableUserDataMap();

  using UserDataProtos = WorldHashMap<std::string, google::protobuf::Any>;
  // Returns the user data protos for this object or a not-found error if it has
  // none.
  absl::StatusOr<const UserDataProtos* absl_nonnull> GetUserDataProtos() const;

  // Returns the mutable user data protos for this object. If there is no user
  // data protos set for this object, an empty user data component will be
  // created and returned.
  absl::StatusOr<UserDataProtos* absl_nonnull> GetMutableUserDataProtos();

  // Creates a simple child object consisting of a single entity. The created
  // object:
  // - Is attached to the base entity of this object with the given relative
  // pose.
  // - Is created with the given geometry.
  // - Is created with some default physics parameters.
  absl::StatusOr<WorldObject* absl_nonnull> CreateChildObject(
      const WorldObjectName& name, WorldObjectNameType name_type,
      const Pose3d& object_t_new_object,
      std::optional<std::string> entity_local_name,
      std::unique_ptr<GeometryComponent> geometry_component);

  // Creates a child object consisting of the given entities. The created
  // object gets attached to the base entity of this object with the given
  // relative pose.
  // The given entities need to have a common root entity which itself is
  // attached to the world's root entity without any offset. They should also
  // contain exactly one entity collection (i.e., one collection entity) from
  // which the new object will be created. There may also be additional entities
  // outside of this collection that represent frames attached to the object.
  //
  // CAUTION: The support for frames is currently limited. It will work, but the
  // caller will have to re-create their ObjectWorld view to be able to "see"
  // them.
  absl::StatusOr<WorldObject* absl_nonnull> CreateChildObject(
      const WorldObjectName& name, WorldObjectNameType name_type,
      const Pose3d& object_t_new_object, GeometryLibrary* absl_nullable geolib,
      const intrinsic_proto::world::WorldFragment& fragment);

  // Creates a child object consisting of the given scene object. The created
  // object gets attached to the base entity of this object with the given
  // relative pose. The geometry library is needed to reserialize v0
  // references and will throw an error if it's not provided but needed.
  absl::StatusOr<WorldObject* absl_nonnull> CreateChildObject(
      const WorldObjectName& name, WorldObjectNameType name_type,
      const Pose3d& object_t_new_object, GeometryLibrary* absl_nullable geolib,
      const intrinsic_proto::scene_object::v1::SceneObject& scene_object);

  // Creates a child object by cloning the given 'existing_object'. The created
  // object gets attached to the base entity of this object with the given
  // relative pose 'object_t_new_object'. If no pose is given, the new object
  // will have the same global pose as the existing one (but potentially a
  // different parent).
  // Only simple objects can be cloned at the moment. If the given object is not
  // supported, an error will be returned. Frames are not cloned together with
  // the object.
  absl::StatusOr<WorldObject* absl_nonnull> CloneExistingObjectAndAttach(
      const WorldObject& existing_object, const WorldObjectName& name,
      WorldObjectNameType name_type, bool clone_frames,
      std::optional<Pose3d> object_t_new_object = std::nullopt);

  // Creates a child resource consisting of the given entities. Same as
  // `CreateChildObject` but additionally marks the object as a resource in the
  // PPR model and handles resource-specific setup logic for resources like
  // cameras or grippers (e.g., setting a sensor component for a camera). The
  // geometry library is needed to reserialize v0 references and will throw an
  // error if it's not provided but needed. Note: The resulting world object's
  // name will be the same as the resource name.
  absl::StatusOr<WorldObject* absl_nonnull> CreateChildResource(
      const intrinsic_proto::resources::GeometricResourceInstanceData&
          resource_instance_data,
      WorldObjectNameType name_type, const Pose3d& object_t_new_object,
      GeometryLibrary* absl_nullable geolib,
      scene_object::UpdatePolicy update_policy =
          scene_object::UpdatePolicy::kDefault);

  // Variants of CreateChildObject() which attach the created object to
  // the final entity of this object. If this object is not a kinematic object
  // and thus consists of a single entity, this method is equivalent to the
  // corresponding overload of CreateChildObject(). If this object is a
  // kinematic object and the final entity cannot be determined uniquely, an
  // error will be returned. The geometry library is needed to reserialize v0
  // references and will throw an error if it's not provided but needed.
  absl::StatusOr<WorldObject* absl_nonnull> CreateChildObjectOfFinalEntity(
      const WorldObjectName& name, WorldObjectNameType name_type,
      const Pose3d& object_t_new_object,
      std::optional<std::string> entity_local_name,
      std::unique_ptr<GeometryComponent> geometry_component);
  absl::StatusOr<WorldObject* absl_nonnull> CreateChildObjectOfFinalEntity(
      const WorldObjectName& name, WorldObjectNameType name_type,
      const Pose3d& object_t_new_object, GeometryLibrary* absl_nullable geolib,
      const intrinsic_proto::world::WorldFragment& fragment);
  absl::StatusOr<WorldObject* absl_nonnull> CreateChildObjectOfFinalEntity(
      const WorldObjectName& name, WorldObjectNameType name_type,
      const Pose3d& object_t_new_object, GeometryLibrary* absl_nullable geolib,
      const intrinsic_proto::scene_object::v1::SceneObject& scene_object);
  absl::StatusOr<WorldObject* absl_nonnull>
  CloneExistingObjectAndAttachToFinalEntity(
      const WorldObject& existing_object, const WorldObjectName& name,
      WorldObjectNameType name_type, bool clone_frames,
      std::optional<Pose3d> object_t_new_object = std::nullopt);

  // Creates a new child frame attached to and grouped under this object. If
  // this object is a kinematic object and has more than one object entity, the
  // new frame will get attached to the objects origin/base entity.
  absl::StatusOr<Frame* absl_nonnull> CreateChildFrame(
      const FrameName& new_frame_name, const Pose3d& object_t_new_frame);

  // Creates a new child frame attached to and grouped under this object. For
  // kinematic objects with more than one object entity, the new frame will
  // get attached to the provided entity filter.
  absl::StatusOr<Frame* absl_nonnull> CreateChildFrame(
      const FrameName& new_frame_name, const world::ObjectEntityFilter& filter,
      const Pose3d& entity_t_new_frame);

  // Creates a new child frame attached to and grouped under this object. If
  // this object is a kinematic object and has more than one object entity, the
  // new frame will get attached to the objects final entity. If a final entity
  // cannot be determined uniquely, an error will be returned.
  absl::StatusOr<Frame* absl_nonnull> CreateChildFrameOfFinalEntity(
      const FrameName& new_frame_name, const Pose3d& object_t_new_frame);

  // Returns the root entity of all entities represented by this object (see
  // GetEntityIds()).
  absl::StatusOr<AttachmentEntityId> GetRootEntityId() const;

  // Returns the collections entity corresponding to this object or std::nullopt
  // if there is no collection (e.g. for the root object).
  virtual std::optional<CollectionsEntityId> GetCollectionEntity() const {
    return std::nullopt;
  }

  // Accepts the given visitor and calls exactly one of its Visit() methods
  // appropriate to the type of this WorldObject.
  // Note that only this instance is visited and there is no implicit
  // tree-traversal. For a tree-traversal, the visitor implementation explicitly
  // needs to invoke Accept() on the children or parents of a visited
  // WorldObject.
  virtual absl::Status Accept(WorldObjectVisitor& visitor) = 0;
  virtual absl::Status Accept(WorldObjectConstVisitor& visitor) const = 0;

  absl::Status Accept(TransformNodeVisitor& visitor) override {
    return visitor.Visit(*this);
  }
  absl::Status Accept(TransformNodeConstVisitor& visitor) const override {
    return visitor.Visit(*this);
  }

  absl::StatusOr<AttachmentEntityId> GetTransformOriginEntityId()
      const override;

  absl::StatusOr<AttachmentEntityId> GetTransformEntityId(
      const world::ObjectEntityFilter& filter) const override;

  // Returns the final entity of this object if it is a linear-chain kinematic
  // object, else returns the root attachment entity.
  virtual absl::StatusOr<AttachmentEntityId>
  FinalEntityIfKinematicObjectOrElseRootEntity() const = 0;

  // Returns the set of leaf entities of this object.
  virtual absl::StatusOr<WorldHashSet<AttachmentEntityId>> FinalEntities()
      const;

  // Returns whether the entity is collision excluded from this entire object.
  absl::StatusOr<bool> IsCollisionExcluded(const EntityId& entity_id) const;

  // Returns whether the entire other object is collision excluded from this
  // entire object.
  absl::StatusOr<bool> IsCollisionExcluded(
      const WorldObject& other_object) const;

  // Checks if new_child_name conflicts with any of this object's child objects
  // and child frames. Returns an error if there is a conflict and returns Ok if
  // the name can be used.
  absl::Status CheckObjectNameAgainstChildrenObjectAndFrameNames(
      const WorldObjectName& new_child_name,
      const WorldObject* absl_nullable exclude_object = nullptr) const;

  // Returns OK if movable, otherwise returns some error stating why.
  absl::Status CheckIsMovable(
      const std::optional<world::ObjectEntityFilter>& filter) const override;

 protected:
  // Friend declarations required for updating references to children.
  friend class ObjectWorldCreationProcess;
  friend class PhysicalObject;
  friend class Frame;

  // Add the given object to this objects' children. CAUTION: Does not update
  // parent references in the child object and can lead to an inconsistent
  // state.
  void AddChildAsymmetric(WorldObject* absl_nonnull child);

  // Removes given object from this objects' children. Returns a NotFoundError
  // if the given object is not registered as a child. CAUTION: Does not update
  // parent references in the child object and can lead to an inconsistent
  // state.
  absl::Status RemoveChildAsymmetric(const WorldObject& child);

  // Check whether the given name would be valid as a new frame name under this
  // object.
  absl::Status CheckFrameNameIsAvailable(const FrameName& name) const;

  absl::StatusOr<WorldObject* absl_nonnull> CreateSingleEntityObject(
      const WorldObjectName& name, WorldObjectNameType name_type,
      AttachmentEntityId parent_entity_id,
      const Pose3d& parent_entity_t_new_object,
      std::optional<std::string> entity_local_name,
      std::unique_ptr<GeometryComponent> geometry_component);

  absl::StatusOr<WorldObject* absl_nonnull> CreateObjectFromSceneObject(
      const WorldObjectName& name, WorldObjectNameType name_type,
      AttachmentEntityId parent_entity_id,
      const Pose3d& parent_entity_t_new_object,
      GeometryLibrary* absl_nullable geolib,
      const intrinsic_proto::scene_object::v1::SceneObject& scene_object);

  absl::StatusOr<WorldObject* absl_nonnull> CreateObjectFromFragment(
      const WorldObjectName& name, WorldObjectNameType name_type,
      AttachmentEntityId parent_entity_id,
      const Pose3d& parent_entity_t_new_object,
      GeometryLibrary* absl_nullable geolib,
      const intrinsic_proto::world::WorldFragment& fragment);

  absl::StatusOr<WorldObject* absl_nonnull> CloneObject(
      const WorldObject& existing_object, const WorldObjectName& name,
      WorldObjectNameType name_type, AttachmentEntityId parent_entity_id,
      bool clone_frames, std::optional<Pose3d> parent_entity_t_new_object);

  absl::StatusOr<Frame* absl_nonnull> CreateEntityAndFrame(
      const FrameName& name, AttachmentEntityId parent_entity_id,
      Frame* absl_nullable parent_frame,
      const Pose3d& parent_entity_t_new_frame);

  // Adds the given frame to this object. CAUTION: Does not update any parent
  // references in the frame and can lead to an inconsistent state.
  absl::Status AddFrameAsymmetric(std::unique_ptr<Frame> frame);

  // Removes the given frame from this object and returns ownership to the
  // caller. CAUTION: Does not update any parent references in the frame and can
  // lead to an inconsistent state.
  absl::StatusOr<std::unique_ptr<Frame>> RemoveFrameAsymmetric(
      const Frame& frame);

  virtual absl::Status Delete(bool delete_child_objects) = 0;

  virtual absl::Status ToggleCollisionsWith(
      WorldObject& other, bool enable_collisions,
      const world::ObjectEntityFilter& this_entity_filter,
      const world::ObjectEntityFilter& other_entity_filter) = 0;

  // Creates a raw sensor frame if the object has a sensor entity.
  // Note: This frame is not added to the object, Remember to call
  // AddFrameAsymmetric to move ownership of the frame to the object.
  absl::StatusOr<std::unique_ptr<Frame>>
  CreateSensorFrameIfObjectHasSensorEntity();

  // Creates a raw frame from the given entity.
  // Note: This frame is not added to the object, Remember to call
  // AddFrameAsymmetric to move ownership of the frame to the object. And
  // AddChildFrameAsymmetric to setup frame-frame parentage after all frames are
  // created for this object..
  absl::StatusOr<std::unique_ptr<Frame>> CreateFrameFromEntity(
      AttachmentEntityId candidate_id,
      const WorldHashMap<ObjectWorldResourceId, std::unique_ptr<Frame>>& frames,
      const WorldHashMap<FrameName, AttachmentEntityId>& found_frame_entities,
      bool candidate_is_collection_member,
      std::vector<std::string>* absl_nullable non_critical_errors = nullptr);

  // Finds and adds all frames to this object based on the underlying entity
  // world. Expects the frameless object to be fully constructed but frames not
  // created yet.
  absl::Status FindAndAddFrames(
      std::vector<std::string>* absl_nullable non_critical_errors,
      const AttachmentGraph* absl_nullable attachment_graph);

  WorldObjectName name_;

 private:
  // Registers a frame entity to belong to this object and syncs with
  // ObjectWorldData.
  void RegisterFrameEntity(AttachmentEntityId frame_id);
  // Unregisters a frame entity and syncs with ObjectWorldData.
  void UnregisterFrameEntity(AttachmentEntityId frame_id);
  // Updates the local-name index when a frame entity is renamed.
  void UpdateFrameEntityLocalName(AttachmentEntityId frame_id,
                                  absl::string_view old_name,
                                  absl::string_view new_name);
  // Returns the single entity of this object that matches `filter`. Resolves
  // base-entity filters with GetRootEntityId() and single-name filters with
  // `entity_ids_by_local_name_`. All other filters, and names that are not in
  // the index, go through GetObjectEntitiesMatchingEntityFilter(). Returns a
  // NotFoundError if no entity matches and an InvalidArgumentError if more
  // than one entity matches.
  absl::StatusOr<AttachmentEntityId> ResolveSingleEntityIdFromFilter(
      const world::ObjectEntityFilter& filter) const;

  std::vector<WorldObject*> children_;
  // The set of entities that belong to this object.
  // CAUTION: Do not modify this set directly! Use RegisterFrameEntity() and
  // UnregisterFrameEntity() instead to keep the ObjectWorldData in sync.
  const WorldHashSet<AttachmentEntityId> entity_ids_;
  WorldHashMap<ObjectWorldResourceId, std::unique_ptr<Frame>> frames_by_id_;
  AttachmentEntityId root_entity_id_{kInvalidEntityId};
  // Entities of this object grouped by their local name.
  // CAUTION: Do not modify this map directly! Use RegisterFrameEntity(),
  // UnregisterFrameEntity(), and UpdateFrameEntityLocalName() instead to keep
  // this index in sync.
  const WorldHashMap<std::string, std::vector<AttachmentEntityId>>
      entity_ids_by_local_name_;
};

namespace object_world_object_entity_filter_details {

// Returns the set of entities that correspond to the object and filter. If
// expanded list is set we will return the full set of links instead of only the
// last/first entities.
absl::StatusOr<WorldHashSet<AttachmentEntityId>>
GetObjectEntitiesMatchingEntityFilter(
    const WorldObject& object, const world::ObjectEntityFilter& entity_filter,
    bool expanded_list);

// Similar to GetObjectEntitiesMatchingEntityFilter but will return only valid
// CollisionEntityIds that have a CollisionComponent.
absl::StatusOr<WorldHashSet<CollisionEntityId>>
GetObjectCollisionEntitiesMatchingEntityFilter(
    const WorldObject& object, const world::ObjectEntityFilter& entity_filter);

}  // namespace object_world_object_entity_filter_details

}  // namespace object_world
}  // namespace intrinsic

#endif  // INTRINSIC_WORLD_OBJECTS_WORLD_OBJECT_INTERNAL_H_
