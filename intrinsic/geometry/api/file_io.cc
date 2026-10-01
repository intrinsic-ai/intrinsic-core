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

#include "intrinsic/geometry/api/file_io.h"

#include <assimp/Importer.hpp>
// Note that we have to include assimp in this way since this is how it is
// included in blue/shared. Using the google version of the build results in
// a redefinition of the assimp importer class.
// TODO(b/73746994): Reconcile this with blue/shared and decide which version to
// use.
#include <assimp/cimport.h>
#include <assimp/postprocess.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/container/flat_hash_set.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "assimp/Exporter.hpp"
#include "assimp/cexport.h"
#include "assimp/cimport.h"
#include "assimp/color4.h"
#include "assimp/config.h"
#include "assimp/material.h"
#include "assimp/mesh.h"
#include "assimp/scene.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/geometry/api/exact_geometry.h"
#include "intrinsic/geometry/api/geometry.h"
#include "intrinsic/geometry/api/renderable.h"
#include "intrinsic/geometry/internal/legacy/point_cloud/ai_scene_to_point_cloud.h"
#include "intrinsic/geometry/internal/legacy/point_cloud/load_point_cloud_from_buffer.h"
#include "intrinsic/geometry/internal/mesh/io/ai_scene_to_mesh.h"
#include "intrinsic/geometry/internal/mesh/io/load_ai_scene_from_buffer.h"
#include "intrinsic/geometry/internal/mesh/io/mesh_to_ai_scene.h"
#include "intrinsic/geometry/internal/mesh/io/restrict_importer.h"
#include "intrinsic/geometry/internal/mesh/mesh.h"
#include "intrinsic/geometry/internal/point_cloud/pts_to_ai_scene.h"
#include "intrinsic/geometry/internal/point_cloud/pts_to_point_cloud.h"
#include "intrinsic/geometry/internal/util/export_as_gltf.h"
#include "intrinsic/geometry/proto/v1/exact_geometry.pb.h"
#include "intrinsic/geometry/proto/v1/geometry.pb.h"
#include "intrinsic/geometry/proto/v1/geometry_options.pb.h"
#include "intrinsic/geometry/proto/v1/geometry_storage_refs.pb.h"
#include "intrinsic/geometry/proto/v1/inline_geometry.pb.h"
#include "intrinsic/geometry/proto/v1/octree.pb.h"
#include "intrinsic/geometry/proto/v1/octree_wrapping.pb.h"
#include "intrinsic/geometry/proto/v1/point_cloud.pb.h"
#include "intrinsic/geometry/proto/v1/primitive_shape.pb.h"
#include "intrinsic/geometry/proto/v1/primitives.pb.h"
#include "intrinsic/geometry/proto/v1/renderable.pb.h"
#include "intrinsic/geometry/proto/v1/transformed_geometry.pb.h"
#include "intrinsic/geometry/proto/v1/transformed_primitive_shape.pb.h"
#include "intrinsic/geometry/proto/v1/triangle_mesh.pb.h"
#include "intrinsic/geometry/shapes/point_cloud.h"
#include "intrinsic/geometry/shapes/shapes.h"
#include "intrinsic/math/proto/vector3.pb.h"
#include "intrinsic/util/object_store/object_ref.h"
#include "intrinsic/util/status/status_builder.h"
#include "intrinsic/util/status/status_macros.h"
#include "ortools/base/helpers.h"
#include "ortools/base/options.h"
#include "ortools/base/path.h"

namespace intrinsic::geo {
using ::intrinsic::geo::legacy::AiSceneIsPointCloud;
using ::intrinsic::geo::legacy::AiSceneToPointCloud;
namespace {

absl::StatusOr<std::string> ExportAsGltfFromMesh(
    const aiScene* scene, absl::string_view extension,
    const eigenmath::Vector3d& scale) {
  if (scene == nullptr) {
    return absl::InvalidArgumentError("Input geometry can't be loaded!");
  }

  aiScene* scene_copy = nullptr;
  aiCopyScene(scene, &scene_copy);
  if (scene_copy == nullptr) {
    return absl::InternalError("Failed to copy aiScene!");
  }
  std::unique_ptr<aiScene, void (*)(const aiScene*)> scene_guard(
      scene_copy, [](const aiScene* s) { aiFreeScene(s); });

  if (extension == "stl" && scene_copy->HasMaterials()) {
    // Override any material in the scene to be opaque white. Assimp parses
    // non standard materialise materials for STL files. These materials are
    // not recognised by other softwares causing confusion when loading the
    // same model.
    for (int i = 0; i < scene_copy->mNumMaterials; ++i) {
      aiColor4D opaque_white(ai_real(1.0), ai_real(1.0), ai_real(1.0),
                             ai_real(1.0));
      aiMaterial* original_mat = scene_copy->mMaterials[i];
      original_mat->AddProperty(&opaque_white, 1, AI_MATKEY_COLOR_DIFFUSE);
      original_mat->AddProperty(&opaque_white, 1, AI_MATKEY_COLOR_SPECULAR);
    }
  }

  // Note that the renderable can have a non-uniform scale.
  Eigen::Affine3d scale_transform(Eigen::Scaling(scale));

  // Finally export the scene with the scale transform.
  return ExportAiSceneAsGltf(*scene_copy, scale_transform.matrix());
}

absl::StatusOr<std::string> ExportAsGltfFromPoints(
    const std::string& content, const eigenmath::Vector3d& scale) {
  // The importer must have the same lifetime as the scene. Otherwise we will
  // get into trouble when trying to access the scene later.
  Assimp::Importer importer;

  // When we create the scene from scratch we must manage the memory ourselves,
  // but when we create it using the importer we defer the management to the
  // importer as we don't own the resulting aiScene*. This complicated duality
  // is why we have both a unique_ptr and a regular raw pointer.
  INTR_ASSIGN_OR_RETURN(std::unique_ptr<const aiScene> scene_ptr,
                        PtsFileToAiScene(content));
  const aiScene* scene = scene_ptr.get();

  if (scene == nullptr) {
    return absl::InvalidArgumentError("Input geometry can't be loaded!");
  }

  // Note that the renderableInfo can have a non-uniform scale.
  Eigen::Affine3d scaleTransform(Eigen::Scaling(scale));

  // Post apply the scaling matrix.
  eigenmath::Matrix4d rend_trans = scaleTransform.matrix();

  // Finally export the scene with the scale transform.
  return ExportAiSceneAsGltf(*scene, rend_trans);
}

}  // namespace

absl::StatusOr<Geometry> LoadPointsFileToGeometry(
    absl::string_view filename, const eigenmath::Vector3d& scale) {
  const std::string extension(file::Extension(filename));
  if (!SupportedPointsExtensions().contains(absl::AsciiStrToLower(extension))) {
    return absl::InvalidArgumentError(absl::Substitute(
        "Extension '$0' is not supported. Please specify one of "
        "the supported extensions for "
        "loading point cloud from buffer. Supported extensions are: $1",
        extension, absl::StrJoin(SupportedPointsExtensions(), ", ")));
  }

  std::string file_content;
  INTR_RETURN_IF_ERROR(
      file::GetContents(filename, &file_content, file::Defaults()))
      << "while trying to load point cloud from file: " << filename;

  INTR_ASSIGN_OR_RETURN(
      auto geo, LoadPointsBufferToGeometry(file_content, extension, scale),
      _ << "Failed to load point cloud from file content of : " << filename);

  return geo;
}

absl::StatusOr<Geometry> LoadPointsBufferToGeometry(
    const std::string& file_content, const std::string& extension,
    const eigenmath::Vector3d& scale) {
  if (!SupportedPointsExtensions().contains(absl::AsciiStrToLower(extension))) {
    return absl::InvalidArgumentError(absl::Substitute(
        "Extension '$0' is not supported. Please specify one of "
        "the supported extensions for "
        "loading point cloud from buffer. Supported extensions are: $1",
        extension, absl::StrJoin(SupportedPointsExtensions(), ", ")));
  }

  if (extension == "pts") {
    INTR_ASSIGN_OR_RETURN(auto points,
                          PtsFileToPointCloud(file_content, scale));

    INTR_ASSIGN_OR_RETURN(std::string glb_string,
                          ExportAsGltfFromPoints(file_content, scale));

    auto renderable = std::make_shared<const Renderable>(std::move(glb_string));
    return Geometry(ExactGeometry(std::move(points)), std::move(renderable),
                    /*provenance=*/std::nullopt);
  } else if (extension == "ply") {
    INTR_ASSIGN_OR_RETURN(auto scene,
                          LoadAiSceneFromBuffer(file_content, extension));
    INTR_ASSIGN_OR_RETURN(
        PointCloud point_cloud,
        AiSceneToPointCloud(*scene, scale, /*compute_missing_normals=*/false));

    INTR_ASSIGN_OR_RETURN(std::string glb_string,
                          ExportAsGltfFromMesh(scene.get(), extension, scale));

    auto renderable = std::make_shared<const Renderable>(std::move(glb_string));
    return Geometry(ExactGeometry(std::move(point_cloud)),
                    std::move(renderable),
                    /*provenance=*/std::nullopt);

  } else {
    return absl::UnimplementedError(
        absl::StrCat("Unsupported extension: ", extension));
  }
}

absl::StatusOr<Geometry> LoadMeshFileToGeometry(
    absl::string_view filename, const eigenmath::Vector3d& scale) {
  const std::string extension(file::Extension(filename));

  std::string file_content;
  INTR_RETURN_IF_ERROR(
      file::GetContents(filename, &file_content, file::Defaults()));

  INTR_ASSIGN_OR_RETURN(
      auto geo, LoadMeshBufferToGeometry(file_content, extension, scale),
      _ << "Failed to load mesh " << filename);
  return geo;
}

absl::StatusOr<Geometry> LoadMeshBufferToGeometry(
    const std::string& file_content, const std::string& extension,
    const eigenmath::Vector3d& scale) {
  if (!SupportedMeshExtensions().contains(absl::AsciiStrToLower(extension))) {
    return absl::InvalidArgumentError(absl::Substitute(
        "Extension '$0' is not supported. Please specify one of "
        "the supported extensions for "
        "loading mesh from buffer. Supported extensions are: $1",
        extension, absl::StrJoin(SupportedMeshExtensions(), ", ")));
  }
  Assimp::Importer importer;
  // Make sure no extra transform is added by assimp.
  importer.SetPropertyInteger(AI_CONFIG_IMPORT_COLLADA_IGNORE_UP_DIRECTION, 1);
  importer.SetPropertyInteger(AI_CONFIG_PP_SBP_REMOVE, aiPrimitiveType_LINE);

  RestrictImporterToExtension(importer, extension);

  unsigned int pFlags = aiProcess_CalcTangentSpace | aiProcess_Triangulate |
                        aiProcess_JoinIdenticalVertices |
                        aiProcess_SortByPType | aiProcess_PreTransformVertices |
                        aiProcess_FindDegenerates | aiProcess_EmbedTextures;

  const aiScene* scene = importer.ReadFileFromMemory(
      file_content.c_str(), file_content.size(), pFlags, extension.c_str());

  if (scene == nullptr) {
    return intrinsic::InternalErrorBuilder()
           << "Failed to parse buffer: " << importer.GetErrorString();
  }

  INTR_ASSIGN_OR_RETURN(std::string glb_string,
                        ExportAsGltfFromMesh(scene, extension, scale));

  auto renderable = std::make_shared<const Renderable>(std::move(glb_string));

  if (AiSceneIsPointCloud(*scene)) {
    INTR_ASSIGN_OR_RETURN(
        auto points, AiSceneToPointCloud(*scene, scale,
                                         /*compute_missing_normals=*/true));

    return Geometry(ExactGeometry(std::move(points)), std::move(renderable),
                    /*provenance=*/std::nullopt);
  }

  // TODO(b/180961517): Note that applying the scale through the root
  // transformation of aiScene would not work because AiSceneToMesh does not use
  // transformation information right now.
  INTR_ASSIGN_OR_RETURN(auto mesh, AiSceneToMesh(*scene));
  mesh.Scale(scale);

  return Geometry(ExactGeometry(std::move(mesh)), std::move(renderable),
                  /*provenance=*/std::nullopt);
}

const absl::flat_hash_set<std::string>& SupportedMeshExtensions() {
  static const auto* kMeshExtensions = new absl::flat_hash_set<std::string>(
      {"obj", "stl", "gltf", "glb", "dae", "ply"});
  return *kMeshExtensions;
}

const absl::flat_hash_set<std::string>& SupportedPointsExtensions() {
  static const auto* kPointCloudExtensions =
      new absl::flat_hash_set<std::string>({"pts", "ply"});
  return *kPointCloudExtensions;
}

}  // namespace intrinsic::geo
