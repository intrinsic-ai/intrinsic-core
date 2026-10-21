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

#ifndef INTRINSIC_WORLD_OBJECTS_OBJECT_WORLD_CREATION_UTILS_H_
#define INTRINSIC_WORLD_OBJECTS_OBJECT_WORLD_CREATION_UTILS_H_

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "absl/base/nullability.h"
#include "absl/flags/declare.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "intrinsic/icon/release/source_location.h"
#include "intrinsic/world/entity.h"
#include "intrinsic/world/entity_id.h"
#include "intrinsic/world/hashing/hashing.h"
#include "intrinsic/world/objects/object_world_ids.h"
#include "intrinsic/world/util/walk_attachment_tree.h"
#include "intrinsic/world/world.h"
#include "re2/re2.h"

namespace intrinsic {
namespace object_world {

// LINT.IfChange(object_name_pattern)
static constexpr char kStrictObjectWorldNameRegexpPattern[] =
    "^[a-zA-Z_][a-zA-Z_0-9]*$";
// LINT.ThenChange(
//   //intrinsic/frontend/world_viewer/core/model/world_ids.ts
// )
static constexpr LazyRE2 kStrictObjectWorlNameRegexp = {
    kStrictObjectWorldNameRegexpPattern};

// Returns an error if the name is not compatible with the object-view. This
// ensures, e.g., that the name can be used as a Python variable name without
// the need for character replacements.
// If 'strict' is set to false, we allow some additional characters for
// compatibility with some old worlds but log a warning if we encounter
// additionally allowed cases.
// TODO(ferstl): Remove non-strict mode once all old and test worlds comply.
absl::Status CheckNameIsCompatibleWithObjectView(absl::string_view name,
                                                 bool strict = true);

// Returns a name that is compatible with the object-view based on `name.
std::string GetObjectViewCompatibleName(absl::string_view name);

// Returns for the given id of a collections entity representing an object the
// corresponding ObjectWorldResourceId to be used for the object as a whole.
ObjectWorldResourceId ObjectWorldResourceIdForObject(
    CollectionsEntityId object_collections_id);

// Returns for the given id of an attachment entity representing a frame the
// corresponding ObjectWorldResourceId to be used for the frame as a whole.
// DO NOT PASS kRootEntityId to this method.
ObjectWorldResourceId ObjectWorldResourceIdForFrame(
    AttachmentEntityId frame_entity_id);

// Returns for the given id of an internal attachment entity* that is part of an
// object the corresponding ObjectWorldResourceId to be used for the public
// version of the internal entity which is served through the object API.
//
// *Note that all member entities of an object are attachment entities and the
//  collection entity representing an object does not get exposed directly as
//  an object world resource.
ObjectWorldResourceId ObjectWorldResourceIdForEntity(
    AttachmentEntityId entity_id);

// Returns for the given object world resource id the id of the corresponding
// entity in the internal entity world. Returns an error if the given id does
// not represent an entity.
//
// Inverse of ObjectWorldResourceIdForEntity() above.
absl::StatusOr<EntityId> ObjectWorldResourceIdToEntityId(
    ObjectWorldResourceId object_world_resource_id);

// Returns whether the given entity represents a frame in the sense of the
// object-view.
// If the returned value is false, 'explain_not_a_frame' will be invoked with an
// explanation of why the entity was not considered as a frame.
absl::StatusOr<bool> IsFrameEntity(
    const WorldEntity* entity,
    std::function<void(absl::string_view)> explain_not_a_frame =
        [](absl::string_view) {});

// Returns all child frame entities, transitively, of 'entity_ids' in partially
// sorted order (parents before children). The optional attachment graph can be
// used to speed up the search significantly.
absl::StatusOr<std::vector<AttachmentEntityId>>
GetChildFrameEntitiesRecursively(
    const World& world, const WorldHashSet<AttachmentEntityId>& entity_ids,
    const AttachmentGraph* absl_nullable attachment_graph);

// Returns a list of the entity IDs of the entities with the sensor component.
absl::StatusOr<std::vector<AttachmentEntityId>> FindSensorEntities(
    const World& world, const WorldHashSet<AttachmentEntityId>& object_entities,
    std::optional<CollectionsEntityId> collection_entity);

// Returns a string description of an entity intended to be used in error
// messages.
absl::StatusOr<std::string> DescribeEntityForError(
    const World& world, std::optional<EntityId> entity_id);

}  // namespace object_world
}  // namespace intrinsic

#endif  // INTRINSIC_WORLD_OBJECTS_OBJECT_WORLD_CREATION_UTILS_H_
