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

#include "intrinsic/scene/processing/transform_scene_object.h"

#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/scene/processing/util/transform_util.h"
#include "intrinsic/scene/proto/v1/entity.pb.h"
#include "intrinsic/scene/proto/v1/scene_object.pb.h"
#include "intrinsic/scene/scene_object/root_entity.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic {
namespace scene_object {

using ::intrinsic_proto::scene_object::v1::Entity;
using ::intrinsic_proto::scene_object::v1::SceneObject;

absl::StatusOr<SceneObject> TransformSceneObject(
    const SceneObject& scene_object, const Pose3d& new_parent_t_old_parent) {
  if (new_parent_t_old_parent.isApprox(Pose3d::Identity())) {
    return scene_object;
  }

  INTR_ASSIGN_OR_RETURN(const Entity* root_entity,
                        FindRootEntity(scene_object));
  const std::string& root_entity_name = root_entity->name();

  SceneObject transformed_scene_object = scene_object;
  for (auto& entity : *transformed_scene_object.mutable_entities()) {
    if (entity.parent_name() == root_entity_name) {
      INTR_RETURN_IF_ERROR(
          TransformEntityByPose(entity, new_parent_t_old_parent));
    }
    if (entity.name() == root_entity_name) {
      INTR_RETURN_IF_ERROR(
          TransformLinkGeometry(entity, new_parent_t_old_parent));
    }
  }

  return transformed_scene_object;
}

}  // namespace scene_object
}  // namespace intrinsic
