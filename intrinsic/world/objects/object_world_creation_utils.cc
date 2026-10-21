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

#include "intrinsic/world/objects/object_world_creation_utils.h"

#include <algorithm>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "absl/base/nullability.h"
#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "absl/strings/substitute.h"
#include "intrinsic/icon/release/source_location.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/component/attachment_component.h"
#include "intrinsic/world/component/collections_member_component.h"
#include "intrinsic/world/component/geometry_component.h"
#include "intrinsic/world/component/kinematics_component.h"
#include "intrinsic/world/component/physics_component.h"
#include "intrinsic/world/component/projector_component.h"
#include "intrinsic/world/component/sensor_component.h"
#include "intrinsic/world/entity.h"
#include "intrinsic/world/entity_id.h"
#include "intrinsic/world/hashing/hashing.h"
#include "intrinsic/world/objects/object_world_ids.h"
#include "intrinsic/world/util/walk_attachment_tree.h"
#include "intrinsic/world/world.h"
#include "re2/re2.h"

namespace intrinsic {
namespace object_world {
namespace {

// The prefix for objects and frames must be the same since for legacy reasons
// the frontend still uses number ids internally and must be able to convert
// string id -> int -> string id (for objects and frames).
// See intrinsic/frontend/world_viewer/core/utils/utils.ts.
// TODO(b/238716879): Switch to different prefixes once the frontend uses string
// ids.
constexpr absl::string_view kObjectWorldResourceIdObjectPrefix = "ofid_";
constexpr absl::string_view kObjectWorldResourceIdFramePrefix = "ofid_";
constexpr absl::string_view kObjectWorldResourceIdEntityPrefix = "eid_";

}  // namespace

absl::Status CheckNameIsCompatibleWithObjectView(absl::string_view name,
                                                 bool strict) {
  static const LazyRE2 kExtendedRegexp = {"^[a-zA-Z_0-9][a-zA-Z_0-9-:]*$"};
  const bool strict_match = RE2::FullMatch(name, *kStrictObjectWorlNameRegexp);
  const bool extended_match = RE2::FullMatch(name, *kExtendedRegexp);

  if ((strict && !strict_match) || (!strict && !extended_match)) {
    return absl::InvalidArgumentError(absl::Substitute(
        "The name \"$0\" is not compatible with the object-world view. It may "
        "only contain letters, digits and underscores and must not start with "
        "a digit.",
        name));
  }
  if (!strict && !strict_match) {
    LOG(WARNING) << absl::Substitute(
        "The name \"$0\" is not compatible with the object-world view but was "
        "accepted for compatibility reasons with older worlds. Some features, "
        "in particular in Python, might not work as expected. The name may "
        "only contain letters, digits and underscores and must not start with "
        "a digit.",
        name);
  }

  return absl::OkStatus();
}

std::string GetObjectViewCompatibleName(absl::string_view name) {
  if (name.empty()) {
    return "_";
  }
  if (CheckNameIsCompatibleWithObjectView(name, /*strict=*/true).ok()) {
    return std::string(name);
  }
  std::string compatible_name(name);

  RE2::GlobalReplace(&compatible_name, "[[:^word:]]", "_");
  RE2::GlobalReplace(&compatible_name, "_+", "_");
  if (!RE2::FullMatch(compatible_name.substr(0, 1), "[a-zA-Z_]")) {
    compatible_name = absl::StrCat("_", compatible_name);
  }
  return compatible_name;
}

ObjectWorldResourceId ObjectWorldResourceIdForObject(
    CollectionsEntityId object_collections_id) {
  return ObjectWorldResourceId(absl::StrCat(kObjectWorldResourceIdObjectPrefix,
                                            object_collections_id.value()));
}

ObjectWorldResourceId ObjectWorldResourceIdForFrame(
    AttachmentEntityId frame_entity_id) {
  CHECK(frame_entity_id != kRootEntityId);
  return ObjectWorldResourceId(
      absl::StrCat(kObjectWorldResourceIdFramePrefix, frame_entity_id.value()));
}

ObjectWorldResourceId ObjectWorldResourceIdForEntity(
    AttachmentEntityId entity_id) {
  return entity_id == kRootEntityId
             ? RootEntityId()
             : ObjectWorldResourceId(absl::StrCat(
                   kObjectWorldResourceIdEntityPrefix, entity_id.value()));
}

absl::StatusOr<EntityId> ObjectWorldResourceIdToEntityId(
    ObjectWorldResourceId object_world_resource_id) {
  if (object_world_resource_id == RootEntityId() ||
      object_world_resource_id == RootObjectId()) {
    return kRootEntityId;
  }

  for (absl::string_view prefix :
       {kObjectWorldResourceIdEntityPrefix, kObjectWorldResourceIdObjectPrefix,
        kObjectWorldResourceIdFramePrefix}) {
    if (absl::StartsWith(object_world_resource_id.value(), prefix)) {
      absl::string_view stripped =
          absl::StripPrefix(object_world_resource_id.value(), prefix);
      EntityId::ValueType id;
      if (!absl::SimpleAtoi(stripped, &id)) {
        return absl::InvalidArgumentError(absl::Substitute(
            "Could not convert the given object world resource id \"$0\" to an "
            "entity id. Expected \"$1\" to be a number.",
            object_world_resource_id.value(), stripped));
      }
      return EntityId(id);
    }
  }

  return absl::InvalidArgumentError(absl::Substitute(
      "The given object world resource id \"$0\" does not correspond to an "
      "entity resource.",
      object_world_resource_id.value()));
}

absl::StatusOr<bool> IsFrameEntity(
    const WorldEntity* entity,
    std::function<void(absl::string_view)> explain_not_a_frame) {
  // We expect a frame entity not to have a GeometryComponent, but if it has
  // one and it is empty it should also be accepted.
  bool has_geometry = entity->HasComponent<GeometryComponent>();
  if (has_geometry) {
    INTR_ASSIGN_OR_RETURN(const GeometryComponent* geometry,
                          entity->GetComponent<GeometryComponent>());
    has_geometry = false;
    for (const auto& name : geometry->GetGeometryNames()) {
      has_geometry = has_geometry || geometry->HasGeometry(name);
    }
  }

  bool has_attachment = entity->HasComponent<AttachmentComponent>();
  bool is_root = false;
  if (has_attachment) {
    INTR_ASSIGN_OR_RETURN(const AttachmentComponent* attachment,
                          entity->GetComponent<AttachmentComponent>());
    if (attachment->GetParentId() == kInvalidEntityId) {
      is_root = true;
    }
  }

  const bool is_frame_entity = has_attachment && !is_root && !has_geometry &&
                               !entity->HasComponent<KinematicsComponent>() &&
                               !entity->HasComponent<SensorComponent>() &&
                               !entity->HasComponent<PhysicsComponent>() &&
                               !entity->HasComponent<ProjectorComponent>();

  if (!is_frame_entity) {
    std::vector<std::string> not_a_frame_explanations;
    if (!has_attachment) {
      not_a_frame_explanations.push_back(
          "does not have an AttachmentComponent");
    }
    if (is_root) {
      not_a_frame_explanations.push_back("is root entity");
    }
    if (has_geometry) {
      not_a_frame_explanations.push_back("has a non-empty GeometryComponent");
    }
    if (entity->HasComponent<KinematicsComponent>()) {
      not_a_frame_explanations.push_back("has a KinematicsComponent");
    }
    if (entity->HasComponent<SensorComponent>()) {
      not_a_frame_explanations.push_back("has a SensorComponent");
    }
    if (entity->HasComponent<PhysicsComponent>()) {
      not_a_frame_explanations.push_back("has a PhysicsComponent");
    }
    explain_not_a_frame(absl::StrJoin(not_a_frame_explanations, "; "));
  }

  return is_frame_entity;
}

absl::StatusOr<std::vector<AttachmentEntityId>>
GetChildFrameEntitiesRecursively(
    const World& world, const WorldHashSet<AttachmentEntityId>& entity_ids,
    const AttachmentGraph* absl_nullable attachment_graph) {
  std::deque<AttachmentEntityId> unvisited(entity_ids.begin(),
                                           entity_ids.end());

  // Sort unvisited list so that parents are before children. This is O(N^3).
  // We can do better by doing a breadth-first traversal of an actual tree.
  for (int i = 0; i < unvisited.size(); ++i) {
    for (int j = i + 1; j < unvisited.size(); ++j) {
      INTR_ASSIGN_OR_RETURN(
          auto attachment_i,
          world.GetComponentByEntityId<AttachmentComponent>(unvisited[i]));

      if (attachment_i->GetParentId() == unvisited[j]) {
        std::swap(unvisited[i], unvisited[j]);
        j = i;
      }
    }
  }

  std::vector<AttachmentEntityId> result;
  while (!unvisited.empty()) {
    AttachmentEntityId id = unvisited.front();
    unvisited.pop_front();

    INTR_ASSIGN_OR_RETURN(const WorldEntity* entity, world.GetEntityById(id));
    INTR_ASSIGN_OR_RETURN(bool is_frame_entity, IsFrameEntity(entity));
    if (is_frame_entity) {
      result.push_back(id);
    }

    CollectionsEntityId parent_collection_id(kInvalidEntityId);
    if (entity->HasComponent<CollectionsMemberComponent>()) {
      INTR_ASSIGN_OR_RETURN(auto collection_member_component,
                            entity->GetComponent<CollectionsMemberComponent>());
      auto parent_member_map =
          collection_member_component->GetParentCollectionsIdToTypesMap();
      if (parent_member_map.size() != 1) {
        return absl::InvalidArgumentError(
            "Object world only supports entities that belong to at most one "
            "collection.");
      }
      parent_collection_id = parent_member_map.begin()->first;
    }

    std::vector<AttachmentEntityId> children;
    if (attachment_graph != nullptr) {
      children = attachment_graph->at(id);
    } else {
      // This is a slow call that iterates over ALL entities.
      children = world.GetChildrenOf(id);
    }

    for (AttachmentEntityId child_id : children) {
      INTR_ASSIGN_OR_RETURN(const WorldEntity* child_entity,
                            world.GetEntityById(child_id));

      if (child_entity->HasComponent<CollectionsMemberComponent>()) {
        INTR_ASSIGN_OR_RETURN(
            auto member_component,
            child_entity->GetComponent<CollectionsMemberComponent>());
        const auto member_map =
            member_component->GetParentCollectionsIdToTypesMap();
        // If this is part of more than one collection it's an error.
        if (member_map.size() != 1) {
          continue;
        }

        // Ignore entities that are part of other objects.
        if (member_map.begin()->first != parent_collection_id) {
          continue;
        }

        // If the candidate entity is part of more than one type within the
        // collection that is considered an error.
        if (member_map.begin()->second.size() != 1) {
          continue;
        }

        // If we got here then we can try to use this entity as a frame.
      }

      if (std::find(unvisited.begin(), unvisited.end(), child_id) ==
          unvisited.end()) {
        unvisited.push_back(child_id);
      }
    }
  }
  return result;
}

absl::StatusOr<std::vector<AttachmentEntityId>> FindSensorEntities(
    const World& world, const WorldHashSet<AttachmentEntityId>& object_entities,
    std::optional<CollectionsEntityId> collection_entity) {
  std::vector<AttachmentEntityId> found_sensor_ids;
  for (AttachmentEntityId entity_id : object_entities) {
    if (!world.ValidateEntity<SensorComponentType>(entity_id).status().ok()) {
      continue;
    }
    found_sensor_ids.push_back(entity_id);
  }
  return found_sensor_ids;
}

absl::StatusOr<std::string> DescribeEntityForError(
    const World& world, std::optional<EntityId> entity_id) {
  if (!entity_id.has_value()) {
    return "unknown entity";
  }
  INTR_ASSIGN_OR_RETURN(const WorldEntity* entity,
                        world.GetEntityById(*entity_id));
  return absl::Substitute(
      "id=$0, local_name=\"$1\", labels={$2}, alias=\"$3\"", entity_id->value(),
      entity->GetLocalName(),
      absl::StrJoin(entity->GetLabels(), ", ", absl::StreamFormatter()),
      entity->GetAlias());
}

}  // namespace object_world
}  // namespace intrinsic
