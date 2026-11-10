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

#ifndef INTRINSIC_SCENE_SCENE_OBJECT_ROOT_ENTITY_H_
#define INTRINSIC_SCENE_SCENE_OBJECT_ROOT_ENTITY_H_

#include "absl/base/attributes.h"
#include "absl/status/statusor.h"
#include "intrinsic/scene/proto/v1/entity.pb.h"
#include "intrinsic/scene/proto/v1/scene_object.pb.h"

namespace intrinsic {
namespace scene_object {

// Returns the root entity of `scene_object`, i.e., the only entity without a
// parent.
//
// This is a lightweight alternative to `SceneObjectView` for code that only
// needs the root entity: it does not validate `scene_object`. Returns
// InvalidArgumentError if `scene_object` has no root entity, more than one root
// entity, or if the root entity has an empty name.
absl::StatusOr<const ::intrinsic_proto::scene_object::v1::Entity*>
FindRootEntity(const ::intrinsic_proto::scene_object::v1::SceneObject&
                   scene_object ABSL_ATTRIBUTE_LIFETIME_BOUND);

absl::StatusOr<::intrinsic_proto::scene_object::v1::Entity*> FindRootEntity(
    ::intrinsic_proto::scene_object::v1::SceneObject& scene_object
        ABSL_ATTRIBUTE_LIFETIME_BOUND);

}  // namespace scene_object
}  // namespace intrinsic

#endif  // INTRINSIC_SCENE_SCENE_OBJECT_ROOT_ENTITY_H_
