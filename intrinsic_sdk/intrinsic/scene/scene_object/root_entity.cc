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

#include "intrinsic/scene/scene_object/root_entity.h"

#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/substitute.h"
#include "intrinsic/scene/proto/v1/entity.pb.h"
#include "intrinsic/scene/proto/v1/scene_object.pb.h"

namespace intrinsic {
namespace scene_object {

using ::intrinsic_proto::scene_object::v1::Entity;
using ::intrinsic_proto::scene_object::v1::SceneObject;

absl::StatusOr<const Entity*> FindRootEntity(const SceneObject& scene_object) {
  const Entity* root = nullptr;
  for (const Entity& entity : scene_object.entities()) {
    if (!entity.parent_name().empty()) {
      continue;
    }
    if (root != nullptr) {
      return absl::InvalidArgumentError(absl::Substitute(
          "Multiple root entities found in SceneObject '$0': '$1', '$2'",
          scene_object.name(), root->name(), entity.name()));
    }
    root = &entity;
  }
  if (root == nullptr) {
    return absl::InvalidArgumentError(absl::Substitute(
        "No root entity found in SceneObject '$0'", scene_object.name()));
  }
  if (root->name().empty()) {
    return absl::InvalidArgumentError(
        absl::Substitute("Root entity in SceneObject '$0' has an empty name",
                         scene_object.name()));
  }
  return root;
}

absl::StatusOr<Entity*> FindRootEntity(SceneObject& scene_object) {
  absl::StatusOr<const Entity*> root =
      FindRootEntity(std::as_const(scene_object));
  if (!root.ok()) {
    return root.status();
  }
  return const_cast<Entity*>(*root);
}

}  // namespace scene_object
}  // namespace intrinsic
