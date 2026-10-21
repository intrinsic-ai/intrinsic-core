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

#ifndef INTRINSIC_WORLD_OBJECTS_OBJECT_WORLD_CLIENT_H_
#define INTRINSIC_WORLD_OBJECTS_OBJECT_WORLD_CLIENT_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/struct.pb.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/kinematics/types/cartesian_limits.h"
#include "intrinsic/kinematics/types/joint_limits_xd.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/resources/proto/resource_handle.pb.h"
#include "intrinsic/skills/proto/equipment.pb.h"
#include "intrinsic/world/hashing/hashing.h"
#include "intrinsic/world/objects/frame.h"
#include "intrinsic/world/objects/kinematic_object.h"
#include "intrinsic/world/objects/object_entity_filter.h"
#include "intrinsic/world/objects/object_world_ids.h"
#include "intrinsic/world/objects/transform_node.h"
#include "intrinsic/world/objects/world_object.h"
#include "intrinsic/world/proto/collision_settings.pb.h"
#include "intrinsic/world/proto/geometry_component.pb.h"
#include "intrinsic/world/proto/object_world_refs.pb.h"
#include "intrinsic/world/proto/object_world_service.grpc.pb.h"
#include "intrinsic/world/proto/object_world_updates.pb.h"
#include "intrinsic/world/robot_payload/robot_payload.h"

namespace intrinsic {
namespace world {

// Provides access to a remote world in the world service.
//
// Uses the object-based-view onto a world, i.e., a world is exposed in the
// form of objects and frames.
class ObjectWorldClient {
  using ObjectWorldService = ::intrinsic_proto::world::ObjectWorldService;

 public:
  // Creates a client for the world with the given id.
  ObjectWorldClient(
      absl::string_view world_id,
      std::shared_ptr<ObjectWorldService::StubInterface> object_world_service);

  ~ObjectWorldClient();

  ObjectWorldClient(const ObjectWorldClient&) = delete;
  ObjectWorldClient& operator=(const ObjectWorldClient&) = delete;
  ObjectWorldClient(ObjectWorldClient&& other) noexcept;
  ObjectWorldClient& operator=(ObjectWorldClient&& other) noexcept;

  // Returns the ID of the world.
  absl::string_view GetWorldID() const { return world_id_; }

  // Clones the world in the world service and returns a client for the cloned
  // world. The cloned world is automatically deleted from the world service
  // when the returned client is destroyed.
  absl::StatusOr<ObjectWorldClient> Clone() const;

  // Returns a local copy of the transform node identified by the given
  // reference.
  absl::StatusOr<TransformNode> GetTransformNode(
      const intrinsic_proto::world::TransformNodeReference& reference) const;

  // Returns a local copy of the root object.
  absl::StatusOr<WorldObject> GetRootObject() const;

  // Returns a local copy of the object identified by the given reference.
  absl::StatusOr<WorldObject> GetObject(
      const intrinsic_proto::world::ObjectReference& reference) const;

  // Returns a local copy of the object identified by the given resource id.
  absl::StatusOr<WorldObject> GetObject(const ObjectWorldResourceId& id) const;

  // Returns a local copy of the object identified by the given name.
  absl::StatusOr<WorldObject> GetObject(const WorldObjectName& name) const;

  // Returns a local copy of the object associated with the name of the given
  // resource handle.
  absl::StatusOr<WorldObject> GetObject(
      const intrinsic_proto::resources::ResourceHandle& resource_handle) const;

  // Returns a fresh local copy of the remote object with the same id as the
  // given local object copy.
  //
  // This provides a quick way to "update" a local copy:
  //   INTR_ASSIGN_OR_RETURN(object, world->GetObject(object));
  absl::StatusOr<WorldObject> GetObject(const WorldObject& object) const;

  // Returns all objects in the world.
  // If query_objects is not empty, only objects that match the query will be
  // returned in the result.
  absl::StatusOr<std::vector<WorldObject>> ListObjects(
      std::vector<intrinsic_proto::world::ObjectReference> query_objects = {})
      const;

  // Returns all object names in the world.
  absl::StatusOr<std::vector<WorldObjectName>> ListObjectNames() const;

  // Option on how to handle child objects when deleting an object.
  enum class DeleteObjectWithChildrenOption {
    kErrorIfChildren = 0,
    kDeleteChildren,
    kReparentChildren
  };

  // Deletes object from world. Set `option` to specify what should happen if
  // the deleted object has child objects:
  //
  // kErrorIfChildren: Return an error if the object has any child objects.
  //
  // kDeleteChildren: Delete child objects along with the object.
  //
  // kReparentChildren: Reparent child objects to the deleted object's parent.
  //   Relative pose of the child object with repect to the new parent object
  //   will be retained.
  //
  // Frames grouped under the deleted object will be deleted regardless of the
  // selected option.
  absl::Status DeleteObject(
      const WorldObject& object,
      const DeleteObjectWithChildrenOption& option =
          DeleteObjectWithChildrenOption::kErrorIfChildren);

  // Returns a local copy of the robot part object identified by the given
  // reference. Returns an error if the referenced object is not a robot part.
  absl::StatusOr<KinematicObject> GetKinematicObject(
      const intrinsic_proto::world::ObjectReference& reference) const;

  // Returns a local copy of the robot part object identified by the given
  // resource id. Returns an error if the referenced object is not a robot part.
  absl::StatusOr<KinematicObject> GetKinematicObject(
      const ObjectWorldResourceId& id) const;

  // Returns a local copy of the robot part object identified by the given
  // reference. Returns an error if the referenced object is not a robot part.
  absl::StatusOr<KinematicObject> GetKinematicObject(
      const WorldObjectName& name) const;

  // Returns a local copy of the robot part object associated with the name of
  // the given resource handle. Returns an error if the referenced object is
  // not a robot part.
  absl::StatusOr<KinematicObject> GetKinematicObject(
      const intrinsic_proto::resources::ResourceHandle& resource_handle) const;

  // Returns a fresh local copy of the remote kinematic object with the same id
  // as the given local object copy.
  //
  // This provides a quick way to "update" a local copy:
  //   INTR_ASSIGN_OR_RETURN(object, world->GetKinematicObject(object));
  absl::StatusOr<KinematicObject> GetKinematicObject(
      const KinematicObject& object) const;

  // Returns a local copy of the frame identified by the given reference.
  absl::StatusOr<Frame> GetFrame(
      const intrinsic_proto::world::FrameReference& reference) const;

  // Returns a local copy of the frame with the given resource id.
  absl::StatusOr<Frame> GetFrame(const ObjectWorldResourceId& id) const;

  // Returns a local copy of the frame with the given name under the object with
  // the given name.
  absl::StatusOr<Frame> GetFrame(const WorldObjectName& object_name,
                                 const FrameName& frame_name) const;

  // Returns a local copy of the frame with the given name under the object
  // associated with the name of the given resource handle.
  absl::StatusOr<Frame> GetFrame(
      const intrinsic_proto::resources::ResourceHandle& object_resource_handle,
      const FrameName& frame_name) const;

  // Returns a fresh local copy of the remote frame with the same id as the
  // given local frame copy.
  //
  // This provides a quick way to "update" a local copy:
  //   INTR_ASSIGN_OR_RETURN(frame, world->GetFrame(frame));
  absl::StatusOr<Frame> GetFrame(const Frame& frame) const;

  // Creates a new frame with the given 'new_frame_name' which is attached to
  // the base entity of the given 'parent_object' with the given offset
  // 'parent_t_new_frame'.
  absl::Status CreateFrame(const FrameName& new_frame_name,
                           const WorldObject& parent_object,
                           const Pose3d& parent_t_new_frame = Pose3d());

  // Creates a new frame with the given 'new_frame_name' which is attached to
  // the given 'parent_frame' with the given offset 'parent_t_new_frame'.
  absl::Status CreateFrame(const FrameName& new_frame_name,
                           const Frame& parent_frame,
                           const Pose3d& parent_t_new_frame = Pose3d());

  // Changes the name of the given object to the given name. Returns an error if
  // the new name is already used for another object.
  absl::Status UpdateObjectName(const WorldObject& object,
                                const WorldObjectName& new_name);

  // Deprecated: All object names are global aliases. Use
  // UpdateObjectName(object, new_name) instead.
  ABSL_DEPRECATED(
      "The name_type parameter is deprecated and ignored. All object names are "
      "global aliases; omit name_type.")
  absl::Status UpdateObjectName(const WorldObject& object,
                                const WorldObjectName& new_name,
                                WorldObjectNameType name_type);

  // Changes the name of the given frame to the given name. Returns an error if
  // the new name is already used for another frame under the same object.
  absl::Status UpdateFrameName(const Frame& frame, const FrameName& new_name);

  // Returns the transform 'a_t_b', i.e., the pose of 'node_b' in the space of
  // 'node_a'. 'node_a' and 'node_b' can be arbitrary nodes in the transform
  // tree of the world and don't have to be parent and child.
  absl::StatusOr<Pose3d> GetTransform(const TransformNode& node_a,
                                      const TransformNode& node_b) const;

  // Returns the transform 'a_t_b', i.e., the pose of 'node' in the space of
  // the root object. Equivalent to calling something like
  // GetTransform(GetRootObject().value(), node).
  absl::StatusOr<Pose3d> GetTransform(const TransformNode& node) const;

  // Returns the transform 'a_t_b', i.e., the pose of the entity within 'node_b'
  // described by the given filter in the space of the entity within 'node_a'
  // described by the given filter. 'node_a' and 'node_b' can be arbitrary nodes
  // in the transform tree of the world and don't have to be parent and child.
  absl::StatusOr<Pose3d> GetTransform(
      const TransformNode& node_a,
      std::optional<ObjectEntityFilter> node_a_filter,
      const TransformNode& node_b,
      std::optional<ObjectEntityFilter> node_b_filter) const;

  // Similar to the one above, but takes the ids of the nodes instead of the
  // nodes themselves.s
  absl::StatusOr<Pose3d> GetTransform(
      const ObjectWorldResourceId& node_a_id,
      std::optional<ObjectEntityFilter> node_a_filter,
      const ObjectWorldResourceId& node_b_id,
      std::optional<ObjectEntityFilter> node_b_filter) const;

  // Updates the pose between two neighboring nodes 'node_a' and 'node_b' such
  // that the transform between the two becomes 'a_t_b'. If 'node_b' is the
  // direct child of 'node_a', 'node_b.parent_t_this' is updated; if 'node_a' is
  // the direct child of 'node_b', 'node_a.parent_t_this' is updated; otherwise,
  // an error will be returned.
  absl::Status UpdateTransform(const TransformNode& node_a,
                               const TransformNode& node_b,
                               const Pose3d& a_t_b);

  // Updates the pose of the given 'node_to_update' in the space of its parent
  // (i.e., 'node_to_update.parent_t_this') such that the transform between
  // 'node_a' and 'node_b' becomes 'a_t_b'. Returns an error if 'node_to_update'
  // is not located on the path from 'node_a' to 'node_b'. It is valid to set
  // 'node_a'=='node_to_update' or 'node_b'=='node_to_update'.
  absl::Status UpdateTransform(const TransformNode& node_a,
                               const TransformNode& node_b,
                               const TransformNode& node_to_update,
                               const Pose3d& a_t_b);

  // Updates the pose of the entity described by the 'node_to_update_filter'
  // within the given 'node_to_update' in the space of its parent (i.e.,
  // 'node_to_update.entity.parent_t_this') such that the transform between
  // the entity in 'node_a' described by the node_a_filter and the entity within
  // 'node_b' described by the node_b_filter becomes 'a_t_b'. Returns an error
  // if the entity within 'node_to_update' is not located on the path from
  // the entity within 'node_a' to the entity within 'node_b'. It is valid to
  // set 'node_a'=='node_to_update' or 'node_b'=='node_to_update'.
  absl::Status UpdateTransform(const TransformNode& node_a,
                               const ObjectEntityFilter& node_a_filter,
                               const TransformNode& node_b,
                               const ObjectEntityFilter& node_b_filter,
                               const TransformNode& node_to_update,
                               const Pose3d& a_t_b);

  // Sets the joint positions of the given kinematic object to the given values.
  // Expects radians (for revolute joints) or meters (for prismatic joints).
  absl::Status UpdateJointPositions(const KinematicObject& kinematic_object,
                                    const eigenmath::VectorXd& joint_positions);

  // Sets the joint application limits of the given kinematic object to the
  // given values.
  absl::Status UpdateJointApplicationLimits(
      const KinematicObject& kinematic_object,
      const JointLimitsXd& joint_limits);

  // Sets the joint system limits of the given kinematic object to the given
  // values.
  absl::Status UpdateJointSystemLimits(const KinematicObject& kinematic_object,
                                       const JointLimitsXd& joint_limits);

  // Sets the joint application and system limits of the given kinematic object
  // to the given values.
  absl::Status UpdateJointLimits(const KinematicObject& kinematic_object,
                                 const JointLimitsXd& joint_application_limits,
                                 const JointLimitsXd& joint_system_limits);

  // Sets the cartesian limits of the given kinematic object to the given
  // values.
  absl::Status UpdateCartesianLimits(const KinematicObject& kinematic_object,
                                     const CartesianLimits& cartesian_limits);

  // Sets the mounted payload of the given kinematic object to the given
  // value.
  absl::Status UpdateMountedPayload(const KinematicObject& kinematic_object,
                                    const RobotPayload& payload);

  // Performs a sequence of update operations on various resources in a single
  // world.
  //
  // The update is atomic. Either all or, in case of an error, none of the given
  // updates will be applied.
  absl::Status BatchUpdate(
      const ::intrinsic_proto::world::ObjectWorldUpdates& updates);

  // Reparent 'object' to 'new_parent', leaving the global pose of the
  // reparented object unaffected (i.e., "parent_t_object" might change but
  // "root_t_object" will not change).
  //
  // If 'new_parent' is a kinematic object, this method attaches 'object' to the
  // parent's base object entity (also see ReparentObjectToFinalEntities()).
  absl::Status ReparentObject(const WorldObject& object,
                              const WorldObject& new_parent);

  // Reparent 'object' to 'new_parent', leaving the global pose of the
  // reparented object unaffected (i.e., "parent_t_object" might change but
  // "root_t_object" will not change).
  //
  // This method attaches 'object' to the parent's entity that matches the given
  // object entity filter.
  absl::Status ReparentObject(const WorldObject& object,
                              const WorldObject& new_parent,
                              const ObjectEntityFilter& filter);

  // Reparent 'object' to 'new_parent', leaving the global pose of the
  // reparented object unaffected (i.e., "parent_t_object" might change but
  // "root_t_object" will not change).
  //
  // This method attaches 'object' to the parent's final object entity. If a
  // final entity cannot be determined uniquely, an error will be returned.
  absl::Status ReparentObjectToFinalEntity(const WorldObject& object,
                                           const KinematicObject& new_parent);

  // Returns the current collision settings for the world.
  absl::StatusOr<intrinsic_proto::world::CollisionSettings>
  GetCollisionSettings() const;

  // Disables collision detection between all pairs (a, b) of entities where a
  // is a entity of 'object_a' and b is a entity of 'object_b'.
  //
  // Succeeds and has no effect if collisions were already disabled.
  //
  // This is equivalent to calling:
  //     DisableCollisions(object_a, ObjectEntityFilter().IncludeAllEntities(),
  //                       object_b, ObjectEntityFilter().IncludeAllEntities())
  absl::Status DisableCollisions(const WorldObject& object_a,
                                 const WorldObject& object_b);

  // Disables collision detection between all pairs (a, b) of entities where a
  // is a entity of 'object_a' and selected by 'entity_filter_a' and b is a
  // entity of 'object_b' and selected by 'entity_filter_b'.
  //
  // Succeeds and has no effect if collisions were already disabled.
  absl::Status DisableCollisions(const WorldObject& object_a,
                                 const ObjectEntityFilter& entity_filter_a,
                                 const WorldObject& object_b,
                                 const ObjectEntityFilter& entity_filter_b);

  // Enables collision detection between all pairs (a, b) of entities where a is
  // a entity of 'object_a' and b is a entity of 'object_b'.
  //
  // Succeeds and has no effect if collisions were already enabled.
  //
  // This is equivalent to calling:
  //     DisableCollisions(object_a, ObjectEntityFilter().IncludeAllEntities(),
  //                       object_b, ObjectEntityFilter().IncludeAllEntities())
  absl::Status EnableCollisions(const WorldObject& object_a,
                                const WorldObject& object_b);

  // Enables collision detection between all pairs (a, b) of entities where a is
  // a entity of 'object_a' and selected by 'entity_filter_a' and b is a entity
  // of 'object_b' and selected by 'entity_filter_b'.
  //
  // Succeeds and has no effect if collisions were already enabled.
  absl::Status EnableCollisions(const WorldObject& object_a,
                                const ObjectEntityFilter& entity_filter_a,
                                const WorldObject& object_b,
                                const ObjectEntityFilter& entity_filter_b);



  // Options for CreateObjectFromProductPart method below.
  struct CreateObjectOptions {
    // By default the newly created object will be attached to the root. If a
    // parent is provided it will be attached to the provided parent. Note that
    // root level frames are not supported as object parents. If this is a
    // WorldObject, the created object will be attached to the tip entity of the
    // parent object. If this is a Frame, the created object will be attached to
    // the parent frame.
    std::optional<TransformNode> parent;

    // If provided, sets the relative transform from the newly created object to
    // the object's parent (either 'parent' or root).
    std::optional<Pose3d> parent_t_created_object;
    // User data to be associated with the created object.
    WorldHashMap<std::string, google::protobuf::Any> user_data;
  };

  // Adds a scene object to the world. See options struct for more details.
  absl::Status CreateObjectFromSceneObject(
      const intrinsic_proto::scene_object::v1::SceneObject& scene_object,
      const WorldObjectName& name, const CreateObjectOptions& options = {});

  // Adds a single entity object to the world.
  // See options struct for more details.
  absl::Status CreateSingleEntityObject(
      const WorldObjectName& name,
      const intrinsic_proto::world::GeometryComponent& geometry_component,
      const CreateObjectOptions& options = {});



 private:
  ObjectWorldClient(
      absl::string_view world_id,
      std::shared_ptr<ObjectWorldService::StubInterface> object_world_service,
      bool owns_world);

  void DeleteOwnedWorld();

  std::string world_id_;
  std::shared_ptr<ObjectWorldService::StubInterface> object_world_service_;
  // If true, this client owns the remote world identified by `world_id_` and
  // will delete it from the world service when destroyed.
  bool owns_world_ = false;
};

}  // namespace world
}  // namespace intrinsic

#endif  // INTRINSIC_WORLD_OBJECTS_OBJECT_WORLD_CLIENT_H_
