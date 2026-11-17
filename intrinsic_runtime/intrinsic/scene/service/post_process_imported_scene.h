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

#ifndef INTRINSIC_SCENE_SERVICE_POST_PROCESS_IMPORTED_SCENE_H_
#define INTRINSIC_SCENE_SERVICE_POST_PROCESS_IMPORTED_SCENE_H_

#include "intrinsic/longrunning/cc/operation_context.h"
#include "intrinsic/scene/proto/v1/imported_scene.pb.h"
#include "intrinsic/scene/proto/v1/scene_object_internal.pb.h"
#include "intrinsic/scene/service/geometry_client_with_cache.h"

namespace intrinsic {

// Used during SceneObject import. Performs a number of post-processing
// operations on the imported scene, based on options in the given config.
absl::Status PostProcessImportedScene(
    longrunning::OperationContext& operation_context,
    intrinsic_proto::scene_object::v1::ImportedScene& imported_scene,
    const intrinsic_proto::scene_object::v1::ImportSceneConfig& config,
    GeometryClientWithProcessGeometryCache& geometry_client);

}  // namespace intrinsic

#endif  // INTRINSIC_SCENE_SERVICE_POST_PROCESS_IMPORTED_SCENE_H_
