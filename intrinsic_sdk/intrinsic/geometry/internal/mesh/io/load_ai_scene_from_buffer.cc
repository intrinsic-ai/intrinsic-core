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

#include "intrinsic/geometry/internal/mesh/io/load_ai_scene_from_buffer.h"

#include <assimp/Importer.hpp>
// Note that we have to include assimp in this way since this is how it is
// included in blue/shared. Using the google version of the build results in
// a redefinition of the assimp importer class.
// TODO(b/73746994): Reconcile this with blue/shared and decide which version to
// use.
#include <assimp/postprocess.h>

#include <memory>
#include <string>

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "assimp/IOSystem.hpp"
#include "assimp/SceneCombiner.h"
#include "assimp/config.h"
#include "assimp/mesh.h"
#include "assimp/scene.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/geometry/internal/mesh/io/restrict_importer.h"
#include "intrinsic/geometry/internal/point_cloud/pts_to_ai_scene.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic::geo {
namespace {

// Note that the returned `scene` is owned by `importer`.
absl::StatusOr<const aiScene*> LoadAiSceneFromBuffer(
    const std::string& file_content, const std::string& extension,
    Assimp::Importer& importer, bool remove_point_clouds) {
  // Make sure no extra transform is added by assimp.
  importer.SetPropertyInteger(AI_CONFIG_IMPORT_COLLADA_IGNORE_UP_DIRECTION, 1);
  if (remove_point_clouds) {
    importer.SetPropertyInteger(AI_CONFIG_PP_SBP_REMOVE,
                                aiPrimitiveType_POINT | aiPrimitiveType_LINE);
  } else {
    importer.SetPropertyInteger(AI_CONFIG_PP_SBP_REMOVE, aiPrimitiveType_LINE);
  }

  RestrictImporterToExtension(importer, extension);

  auto read_options = aiProcess_CalcTangentSpace | aiProcess_Triangulate |
                      aiProcess_JoinIdenticalVertices | aiProcess_SortByPType |
                      aiProcess_FindDegenerates | aiProcess_EmbedTextures;
  if (remove_point_clouds) {
    read_options |= aiProcess_PreTransformVertices;
  }

  const aiScene* scene =
      importer.ReadFileFromMemory(file_content.c_str(), file_content.size(),
                                  read_options, extension.c_str());
  if (scene == nullptr) {
    return absl::InternalError(
        absl::StrCat("Failed to load point cloud buffer. Error is\n",
                     importer.GetErrorString()));
  }
  return scene;
}

}  // namespace

absl::StatusOr<std::unique_ptr<aiScene>> LoadAiSceneFromBuffer(
    const std::string& file_content, const std::string& extension,
    const eigenmath::Vector3d& scale) {
  bool remove_point_clouds = true;
  if (extension == "pts") {
    return PtsFileToAiScene(file_content, scale);
  } else if (extension == "ply") {
    remove_point_clouds = false;
  }

  aiScene* scene = nullptr;
  Assimp::Importer importer;
  INTR_ASSIGN_OR_RETURN(const aiScene* tmp_scene,
                        LoadAiSceneFromBuffer(file_content, extension, importer,
                                              remove_point_clouds));
  Assimp::SceneCombiner::CopyScene(&scene, tmp_scene);
  scene->mRootNode->mTransformation[0][0] *= scale[0];
  scene->mRootNode->mTransformation[1][1] *= scale[1];
  scene->mRootNode->mTransformation[2][2] *= scale[2];
  // TODO(stoyang): Confirm if we have to call aiFreeScene on destruction
  return std::unique_ptr<aiScene>(scene);
}

}  // namespace intrinsic::geo
