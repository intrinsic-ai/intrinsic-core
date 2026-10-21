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

#ifndef INTRINSIC_SCENE_MESH_SCENE_OBJECT_TO_GLB_H_
#define INTRINSIC_SCENE_MESH_SCENE_OBJECT_TO_GLB_H_

#include <string>

#include "absl/status/statusor.h"
#include "intrinsic/geometry/storage/geometry_library.h"
#include "intrinsic/scene/proto/v1/scene_object.pb.h"
#include "tiny_gltf.h"

namespace intrinsic::scene_object {

// Converts a SceneObject proto to an intermediate C++ glTF 2.0 Model struct
// containing the scene graph, entity transforms, visual meshes, and materials.
// Only "Intrinsic_Visual" geometries and their renderables are exported.
absl::StatusOr<tinygltf::Model> SceneObjectToGltf(
    const intrinsic_proto::scene_object::v1::SceneObject& so,
    const GeometryLibrary& geo_lib);

// Converts a SceneObject proto to a self-contained GLB binary byte buffer in
// C++. Only "Intrinsic_Visual" geometries and their renderables are exported.
absl::StatusOr<std::string> SceneObjectToGlb(
    const intrinsic_proto::scene_object::v1::SceneObject& so,
    const GeometryLibrary& geo_lib);

}  // namespace intrinsic::scene_object

#endif  // INTRINSIC_SCENE_MESH_SCENE_OBJECT_TO_GLB_H_
