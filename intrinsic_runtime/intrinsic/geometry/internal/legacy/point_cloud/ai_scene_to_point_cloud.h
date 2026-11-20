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

#ifndef INTRINSIC_GEOMETRY_INTERNAL_LEGACY_POINT_CLOUD_AI_SCENE_TO_POINT_CLOUD_H_
#define INTRINSIC_GEOMETRY_INTERNAL_LEGACY_POINT_CLOUD_AI_SCENE_TO_POINT_CLOUD_H_

#include "absl/status/statusor.h"
#include "assimp/scene.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/geometry/shapes/point_cloud.h"

namespace intrinsic::geo::legacy {

// Converts the given aiScene to a point cloud primitive. Returns an error if
// the given scene contains more than a point cloud. Computes normals if
// compute_missing_normals is set to true and there are missing normals in the
// input scene.
absl::StatusOr<geo::PointCloud> AiSceneToPointCloud(
    const aiScene& scene, const eigenmath::Vector3d& scale,
    bool compute_missing_normals);

// Returns true if the given scene represents a point cloud.
bool AiSceneIsPointCloud(const aiScene& scene);

}  // namespace intrinsic::geo::legacy
#endif  // INTRINSIC_GEOMETRY_INTERNAL_LEGACY_POINT_CLOUD_AI_SCENE_TO_POINT_CLOUD_H_
