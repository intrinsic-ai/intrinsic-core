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

#ifndef INTRINSIC_SCENE_PROCESSING_SCALE_SCENE_OBJECT_H_
#define INTRINSIC_SCENE_PROCESSING_SCALE_SCENE_OBJECT_H_

#include "absl/status/statusor.h"
#include "intrinsic/math/proto/vector3.pb.h"
#include "intrinsic/scene/proto/v1/scene_object.pb.h"

namespace intrinsic {
namespace scene_object {

// Scales `scene_object` by `scale` and returns the scaled SceneObject. Applies
// `scale` to all geometries and poses between entities. Supports only
// SceneObject with link and frame Entity. For non-uniform scaling, only link
// entities without rotation are supported. Returns an error for:
// - Entity with unsupported types
// - Unsupported non-uniform scaling cases.
absl::StatusOr<intrinsic_proto::scene_object::v1::SceneObject> ScaleSceneObject(
    const intrinsic_proto::scene_object::v1::SceneObject& scene_object,
    const intrinsic_proto::Vector3& scale);

}  // namespace scene_object
}  // namespace intrinsic

#endif  // INTRINSIC_SCENE_PROCESSING_SCALE_SCENE_OBJECT_H_
