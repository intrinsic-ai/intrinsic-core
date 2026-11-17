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

#ifndef INTRINSIC_SCENE_PROCESSING_TRANSFORM_IMPORTED_SCENE_H_
#define INTRINSIC_SCENE_PROCESSING_TRANSFORM_IMPORTED_SCENE_H_

#include "absl/status/statusor.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/math/proto/quaternion.pb.h"
#include "intrinsic/math/proto/vector3.pb.h"
#include "intrinsic/math/proto_conversion.h"
#include "intrinsic/scene/proto/v1/imported_scene.pb.h"

namespace intrinsic {
namespace scene_object {

using ::intrinsic_proto::scene_object::v1::ImportedScene;

// Applies rotation followed by translation
absl::StatusOr<ImportedScene> TransformImportedScene(
    const ImportedScene& scene,
    const intrinsic_proto::Quaternion& rotation_proto,
    const intrinsic_proto::Vector3& translation_proto);

// Rotation only.
inline absl::StatusOr<ImportedScene> TransformImportedScene(
    const ImportedScene& scene,
    const intrinsic_proto::Quaternion& rotation_proto) {
  return TransformImportedScene(scene, rotation_proto,
                                ToVectorProto(eigenmath::Vector3d::Zero()));
}

// Translation only.
inline absl::StatusOr<ImportedScene> TransformImportedScene(
    const ImportedScene& scene,
    const intrinsic_proto::Vector3& translation_proto) {
  return TransformImportedScene(
      scene, ToProto(eigenmath::Quaterniond::Identity()), translation_proto);
}

}  // namespace scene_object
}  // namespace intrinsic

#endif  // INTRINSIC_SCENE_PROCESSING_TRANSFORM_IMPORTED_SCENE_H_
