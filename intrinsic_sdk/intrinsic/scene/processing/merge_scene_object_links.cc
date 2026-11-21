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

#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/substitute.h"
#include "absl/types/span.h"
#include "intrinsic/scene/processing/util/transform_util.h"
#include "intrinsic/scene/proto/v1/entity.pb.h"
#include "intrinsic/scene/proto/v1/scene_object.pb.h"
#include "intrinsic/scene/scene_object/scene_object_view.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/proto/geometry_component.pb.h"

namespace intrinsic {
namespace scene_object {
namespace {

using ::intrinsic_proto::scene_object::v1::Entity;
using ::intrinsic_proto::scene_object::v1::SceneObject;

}  // namespace

absl::StatusOr<SceneObject> MergeSceneObjectLinks(
    const SceneObject& scene_object) {
  // Validates `scene_object`.
  INTR_ASSIGN_OR_RETURN(const SceneObjectView view,
                        SceneObjectView::Create(scene_object));

  for (const Entity* entity : view.GetEntitiesInTreeOrder()) {
    if (!entity->has_link()) {
      return absl::InvalidArgumentError(absl::Substitute(
          "MergeSceneObjectLinks only supports link-only SceneObjects. Found "
          "non-link entity '$0'.",
          entity->name()));
    }
  }

  if (view.GetNumEntities() == 1) {
    return scene_object;
  }

  const Entity& root_entity = view.GetRootEntity();
  INTR_ASSIGN_OR_RETURN(const auto root_t_entities, view.GetRootPoses());

  Entity merged_root_entity = root_entity;

  // Geometries attached directly to the root entity (at index 0 in tree order)
  // are already in the root link's coordinate frame and included in
  // `merged_root_entity`.
  for (const Entity* entity : view.GetEntitiesInTreeOrder().subspan(1)) {
    if (!entity->link().has_geometry_component()) {
      continue;
    }

    // Transform the child link's local geometries into the root link's
    // coordinate frame using its resolved `root_t_entity` pose.
    Entity transformed_entity = *entity;
    INTR_RETURN_IF_ERROR(TransformLinkGeometry(
        transformed_entity, root_t_entities.at(entity->name())));

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
            absl::StrCat(entity->name(), "_", geom_name);
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
