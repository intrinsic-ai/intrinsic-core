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

#ifndef INTRINSIC_SCENE_PROCESSING_SCALE_IMPORTED_SCENE_H_
#define INTRINSIC_SCENE_PROCESSING_SCALE_IMPORTED_SCENE_H_

#include "absl/status/statusor.h"
#include "intrinsic/math/proto/vector3.pb.h"
#include "intrinsic/scene/proto/v1/imported_scene.pb.h"

namespace intrinsic {
namespace scene_object {

// TODO(b/335447975): Re-evaluate if scale needs to be Vector3.
// Scales `scene` by `scale` and returns the scaled ImportedScene. Applies
// `scale` to all scene objects and poses in reparent updates. For non-uniform
// scaling, reparent with rotation is not supported. Scene object
// scaling limitations applies. See
// intrinsic/scene/processing/scale_scene_object.h for more
// details.
// Returns an error for:
// - Any scene object that doesn't support scaling.
// - Unsupported non-uniform scaling cases in reparent updates.
absl::StatusOr<intrinsic_proto::scene_object::v1::ImportedScene>
ScaleImportedScene(
    const intrinsic_proto::scene_object::v1::ImportedScene& scene,
    const intrinsic_proto::Vector3& scale);

}  // namespace scene_object
}  // namespace intrinsic

#endif  // INTRINSIC_SCENE_PROCESSING_SCALE_IMPORTED_SCENE_H_
