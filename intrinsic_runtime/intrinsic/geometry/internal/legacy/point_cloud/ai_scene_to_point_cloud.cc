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

#include "intrinsic/geometry/internal/legacy/point_cloud/ai_scene_to_point_cloud.h"

#include <vector>

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "assimp/mesh.h"
#include "assimp/scene.h"
#include "assimp/vector3.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/geometry/shapes/point_cloud.h"

namespace intrinsic::geo::legacy {

using ::intrinsic::eigenmath::Vector3d;
using ::intrinsic::geo::PointCloud;

absl::StatusOr<PointCloud> AiSceneToPointCloud(const aiScene& scene,
                                               const Vector3d& scale,
                                               bool compute_missing_normals) {
  if (!scene.mRootNode) {
    return absl::InvalidArgumentError("AiScene has no root node");
  }
  if (!scene.mRootNode->mTransformation.IsIdentity()) {
    return absl::InvalidArgumentError(
        "We do not support transformations on the scene. b/180961517");
  }

  std::vector<Vector3d> points;
  std::vector<Vector3d> normals;

  const aiVector3D aiScale(scale.x(), scale.y(), scale.z());

  for (int mesh_index = 0; mesh_index < scene.mNumMeshes; mesh_index++) {
    aiMesh* ai_scene_mesh = scene.mMeshes[mesh_index];
    // If we encounter a single face with more than one index, then return
    // an error as it's not considered a point cloud.
    for (int fdx = 0; fdx < ai_scene_mesh->mNumFaces; fdx++) {
      if (ai_scene_mesh->mFaces[fdx].mNumIndices != 1) {
        return absl::InvalidArgumentError("AiScene is not a point cloud");
      }
    }

    points.reserve(points.size() + ai_scene_mesh->mNumVertices);
    if (compute_missing_normals || ai_scene_mesh->mNormals != nullptr) {
      normals.reserve(normals.size() + ai_scene_mesh->mNumVertices);
    }

    for (int vdx = 0; vdx < ai_scene_mesh->mNumVertices; vdx++) {
      const aiVector3D& p = ai_scene_mesh->mVertices[vdx];
      points.push_back(
          Vector3d(p.x * scale.x(), p.y * scale.y(), p.z * scale.z()));

      // Normals aren't required by Assimp, but we may require them for creating
      // the PointCloud blue shape, so we fake them if requested.
      aiVector3D normal;
      if (ai_scene_mesh->mNormals != nullptr) {
        normal = ai_scene_mesh->mNormals[vdx];
      } else if (compute_missing_normals) {
        normal = p;
      } else {
        continue;
      }

      normal = aiVector3D(normal.x * aiScale.x, normal.y * aiScale.y,
                          normal.z * aiScale.z)
                   .Normalize();
      normals.push_back(Vector3d(normal.x, normal.y, normal.z));
    }
  }

  // If we had a mixture of normal containing mesh and non containing mesh then
  // we need to clear all the normals. This should only occur if we have
  // compute_missing_normals as false and the input is missing some normals but
  // not all.
  if (!normals.empty() && normals.size() != points.size()) {
    CHECK(!compute_missing_normals);
    normals.clear();
  }

  return PointCloud(points, normals);
}

bool AiSceneIsPointCloud(const aiScene& scene) {
  for (int mesh_index = 0; mesh_index < scene.mNumMeshes; mesh_index++) {
    aiMesh* ai_scene_mesh = scene.mMeshes[mesh_index];
    // If we encounter a single face with more than one index, then return
    // false.
    for (int fdx = 0; fdx < ai_scene_mesh->mNumFaces; fdx++) {
      if (ai_scene_mesh->mFaces[fdx].mNumIndices != 1) {
        return false;
      }
    }
  }

  return true;
}

}  // namespace intrinsic::geo::legacy
