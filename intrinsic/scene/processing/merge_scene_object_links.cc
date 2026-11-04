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

#include "intrinsic/scene/processing/merge_scene_object_links.h"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/substitute.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/proto_conversion.h"
#include "intrinsic/scene/processing/util/transform_util.h"
#include "intrinsic/scene/proto/v1/entity.pb.h"
#include "intrinsic/scene/proto/v1/scene_object.pb.h"
#include "intrinsic/scene/validate/scene_object_validation.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/proto/geometry_component.pb.h"

namespace intrinsic {
namespace scene_object {
namespace {

using ::intrinsic_proto::scene_object::v1::Entity;
using ::intrinsic_proto::scene_object::v1::SceneObject;

// Computes the pose (`root_t_entity`) of every entity in a validated
// `scene_object` relative to the root entity using a top-down traversal.
absl::StatusOr<absl::flat_hash_map<std::string, Pose3d>>
ResolveRootRelativePoses(const SceneObject& scene_object) {
  absl::flat_hash_map<std::string, std::vector<const Entity*>>
      children_by_parent;
  absl::flat_hash_map<std::string, Pose3d> absolute_poses;
  std::vector<const Entity*> entities_to_visit;

  for (const Entity& entity : scene_object.entities()) {
    if (entity.parent_name().empty()) {
      absolute_poses[entity.name()] = Pose3d::Identity();
      entities_to_visit.push_back(&entity);
      continue;
    }
    children_by_parent[entity.parent_name()].push_back(&entity);
  }

  while (!entities_to_visit.empty()) {
    const Entity* parent = entities_to_visit.back();
    entities_to_visit.pop_back();
    const Pose3d& root_t_parent = absolute_poses.at(parent->name());

    const auto children_it = children_by_parent.find(parent->name());
    if (children_it == children_by_parent.end()) {
      continue;
    }

    for (const Entity* child : children_it->second) {
      Pose3d parent_t_child = Pose3d::Identity();
      if (child->has_parent_t_this()) {
        INTR_ASSIGN_OR_RETURN(parent_t_child,
                              FromProtoNormalized(child->parent_t_this()));
      }
      absolute_poses.try_emplace(child->name(), root_t_parent * parent_t_child);
      entities_to_visit.push_back(child);
    }
  }

  if (absolute_poses.size() !=
      static_cast<size_t>(scene_object.entities_size())) {
    return absl::InvalidArgumentError(
        "Failed to resolve poses for all entities in scene object.");
  }

  return absolute_poses;
}

}  // namespace

absl::StatusOr<SceneObject> MergeSceneObjectLinks(
    const SceneObject& scene_object) {
  INTR_RETURN_IF_ERROR(ValidateSceneObject(scene_object));

  for (const auto& entity : scene_object.entities()) {
    if (!entity.has_link()) {
      return absl::InvalidArgumentError(absl::Substitute(
          "MergeSceneObjectLinks only supports link-only SceneObjects. Found "
          "non-link entity '$0'.",
          entity.name()));
    }
  }

  if (scene_object.entities_size() == 1) {
    return scene_object;
  }

  INTR_ASSIGN_OR_RETURN(const std::string root_entity_name,
                        GetRootEntityName(scene_object));
  const Entity* root_entity_ptr = nullptr;
  for (const auto& entity : scene_object.entities()) {
    if (entity.name() == root_entity_name) {
      root_entity_ptr = &entity;
      break;
    }
  }
  if (root_entity_ptr == nullptr) {
    return absl::InternalError(absl::Substitute(
        "Root entity '$0' not found in scene object.", root_entity_name));
  }

  INTR_ASSIGN_OR_RETURN(const auto root_t_entities,
                        ResolveRootRelativePoses(scene_object));

  Entity merged_root_entity = *root_entity_ptr;

  for (const auto& entity : scene_object.entities()) {
    // Geometries attached directly to the root entity are already in the root
    // link's coordinate frame and included in `merged_root_entity`.
    if (entity.name() == root_entity_name) {
      continue;
    }
    if (!entity.link().has_geometry_component()) {
      continue;
    }

    // Transform the child link's local geometries into the root link's
    // coordinate frame using its resolved `root_t_entity` pose.
    Entity transformed_entity = entity;
    INTR_RETURN_IF_ERROR(TransformLinkGeometry(
        transformed_entity, root_t_entities.at(entity.name())));

    // Merge each geometry type (e.g. `Intrinsic_Visual`, `Intrinsic_Collision`)
    // from the transformed child link into the root link's GeometryComponent.
    for (auto& [geometry_type, geometry_set] :
         *transformed_entity.mutable_link()
              ->mutable_geometry_component()
              ->mutable_named_geometries()) {
      if (geometry_set.named_geometries().empty()) {
        continue;
      }

      auto& root_named_geometries =
          *(*merged_root_entity.mutable_link()
                 ->mutable_geometry_component()
                 ->mutable_named_geometries())[geometry_type]
               .mutable_named_geometries();
      for (auto& [geom_name, transformed_geometry] :
           *geometry_set.mutable_named_geometries()) {
        const std::string base_name =
            absl::StrCat(entity.name(), "_", geom_name);
        std::string merged_name = base_name;
        int suffix = 1;
        while (root_named_geometries.contains(merged_name)) {
          merged_name = absl::StrCat(base_name, "_", suffix++);
        }
        root_named_geometries[merged_name] = std::move(transformed_geometry);
      }
    }
  }

  SceneObject merged_scene_object = scene_object;
  merged_scene_object.clear_entities();
  *merged_scene_object.add_entities() = std::move(merged_root_entity);

  return merged_scene_object;
}

}  // namespace scene_object
}  // namespace intrinsic
