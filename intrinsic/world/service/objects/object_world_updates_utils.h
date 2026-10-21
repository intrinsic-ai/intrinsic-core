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

#ifndef INTRINSIC_WORLD_SERVICE_OBJECTS_OBJECT_WORLD_UPDATES_UTILS_H_
#define INTRINSIC_WORLD_SERVICE_OBJECTS_OBJECT_WORLD_UPDATES_UTILS_H_

#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "intrinsic/geometry/storage/geometry_deserializer.h"
#include "intrinsic/world/entity_id.h"
#include "intrinsic/world/objects/frame_internal.h"
#include "intrinsic/world/objects/object_world.h"
#include "intrinsic/world/objects/transform_node_internal.h"
#include "intrinsic/world/objects/world_object_internal.h"
#include "intrinsic/world/proto/object_world_updates.pb.h"

namespace intrinsic {
namespace object_world {

class KinematicObject;

// The optional GeomtryLibrary is only required for creating objects that only
// contain v0 geometry references. An error will be returned if it is actually
// required but none was given.
absl::StatusOr<const WorldObject*> HandleCreateObjectRequest(
    ObjectWorld& world,
    const intrinsic_proto::world::CreateObjectRequest& request,
    GeometryLibrary* geolib = nullptr);

absl::Status HandleDeleteObjectRequest(
    ObjectWorld& world,
    const intrinsic_proto::world::DeleteObjectRequest& request);

// The optional GeomtryLibrary is only required for updating objects that only
// contain v0 geometry references. An error will be returned if it is actually
// required but none was given.
absl::StatusOr<const WorldObject*> HandleUpdateObjectRequest(
    ObjectWorld& world,
    const intrinsic_proto::world::UpdateObjectRequest& request,
    GeometryLibrary* geolib = nullptr);

absl::StatusOr<const WorldObject*> HandleUpdateObjectNameRequest(
    ObjectWorld& world,
    const intrinsic_proto::world::UpdateObjectNameRequest& request);

absl::StatusOr<const KinematicObject*> HandleUpdateObjectJointsRequest(
    ObjectWorld& world,
    const intrinsic_proto::world::UpdateObjectJointsRequest& request);

absl::StatusOr<const KinematicObject*> HandleUpdateObjectJointRequest(
    ObjectWorld& world,
    const intrinsic_proto::world::UpdateObjectJointRequest& request);

absl::StatusOr<const KinematicObject*>
HandleUpdateKinematicObjectPropertiesRequest(
    ObjectWorld& world,
    const intrinsic_proto::world::UpdateKinematicObjectPropertiesRequest&
        request);

// The optional GeomtryDeserializer is only required for updates related to
// geometry options and otherwise can be safely omitted. An error will be
// returned if it is actually required but was not given.
absl::StatusOr<const WorldObject*> HandleUpdateObjectPropertiesRequest(
    ObjectWorld& world,
    const intrinsic_proto::world::UpdateObjectPropertiesRequest& request,
    GeometryLibrary* geolib = nullptr);

absl::StatusOr<AttachmentEntityId> HandleUpdateEntityPropertiesRequest(
    ObjectWorld& world,
    const intrinsic_proto::world::UpdateEntityPropertiesRequest& request,
    GeometryLibrary* geolib = nullptr);

absl::StatusOr<const Frame*> HandleCreateFrameRequest(
    ObjectWorld& world,
    const intrinsic_proto::world::CreateFrameRequest& request);

absl::Status HandleDeleteFrameRequest(
    ObjectWorld& world,
    const intrinsic_proto::world::DeleteFrameRequest& request,
    bool disable_asset_frame_edits = true);

absl::StatusOr<const Frame*> HandleUpdateFrameNameRequest(
    ObjectWorld& world,
    const intrinsic_proto::world::UpdateFrameNameRequest& request,
    bool disable_asset_frame_edits = true);

absl::StatusOr<const Frame*> HandleReparentFrameRequest(
    ObjectWorld& world,
    const intrinsic_proto::world::ReparentFrameRequest& request,
    bool disable_asset_frame_edits = true);

absl::StatusOr<const Frame*> HandleUpdateFramePropertiesRequest(
    ObjectWorld& world,
    const intrinsic_proto::world::UpdateFramePropertiesRequest& request);

absl::StatusOr<const TransformNode*> HandleUpdateTransformRequest(
    ObjectWorld& world,
    const intrinsic_proto::world::UpdateTransformRequest& request);

absl::StatusOr<const WorldObject*> HandleReparentObjectRequest(
    ObjectWorld& world,
    const intrinsic_proto::world::ReparentObjectRequest& request);

absl::StatusOr<std::pair<const WorldObject*, const WorldObject*>>
HandleToggleCollisionsRequest(
    ObjectWorld& world,
    const intrinsic_proto::world::ToggleCollisionsRequest& request);

absl::Status HandleUpdateCollisionSettingsRequest(
    ObjectWorld& world,
    const intrinsic_proto::world::UpdateCollisionSettingsRequest& request);

absl::Status HandleObjectWorldUpdate(
    ObjectWorld& world, const intrinsic_proto::world::ObjectWorldUpdate& update,
    GeometryLibrary* geolib = nullptr, bool disable_asset_frame_edits = true);

absl::Status HandleObjectWorldUpdates(
    ObjectWorld& world,
    const intrinsic_proto::world::ObjectWorldUpdates& updates,
    GeometryLibrary* geolib = nullptr, bool disable_asset_frame_edits = true);

// Returns true if any of the `world_id` fields in any of the world updates is
// non empty.
bool AnyWorldIdsSet(
    const intrinsic_proto::world::ObjectWorldUpdates& world_updates);

// Returns true if the object world updates contain only changes to the state of
// the world and not its structure. State in this case can mean things like
// parent_t_this and dof values.
bool IsOnlyStateChange(
    const intrinsic_proto::world::ObjectWorldUpdates& world_updates);

// Returns true if the object world update contain only changes to the state of
// the world and not its structure. State in this case can mean things like
// parent_t_this and dof values.
bool IsOnlyStateChange(
    const intrinsic_proto::world::ObjectWorldUpdate& world_update);

// Name of the counter of object creations and renames with
// name_is_global_alias set to false.
inline constexpr char kNameIsGlobalAliasFalseMeasureName[] =
    "intrinsic/object_world/name_is_global_alias_false";
// Attribute key of that counter: the type of the object world update.
inline constexpr char kUpdateTypeKey[] = "object_world_update_type";

// Creates the object world update metric instruments. Call it after the global
// MeterProvider is initialized (e.g. after OpenCensusPlugin in main()), or the
// instruments bind to the Noop provider.
void RegisterObjectWorldUpdatesMetrics();

}  // namespace object_world
}  // namespace intrinsic

#endif  // INTRINSIC_WORLD_SERVICE_OBJECTS_OBJECT_WORLD_UPDATES_UTILS_H_
