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

#include "intrinsic/geometry/internal/point_cloud/pts_to_ai_scene.h"

#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "assimp/color4.h"
#include "assimp/color4.inl"
#include "assimp/defs.h"
#include "assimp/material.h"
#include "assimp/mesh.h"
#include "assimp/metadata.h"
#include "assimp/scene.h"
#include "assimp/vector3.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/geometry/api/material.h"
#include "intrinsic/geometry/internal/point_cloud/validate_pts.h"
#include "intrinsic/geometry/shapes/point_cloud.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic::geo {
namespace {

template <typename PointFunc, typename ColorFunc>
absl::StatusOr<std::unique_ptr<aiScene>> ToAiScene(
    const size_t num_vertices, const PointFunc& point_func,
    const ColorFunc& color_func,
    const std::optional<Material>& material = std::nullopt) {
  // `aisScene` destructor takes care of deallocating the resources.
  // https://github.com/assimp/assimp/blob/17ec36b247b3e59f83ce952afc47f9b362f78a14/code/Common/scene.cpp#L68
  std::unique_ptr<aiScene> scene = std::make_unique<aiScene>();
  scene->mNumMeshes = 1;
  scene->mMeshes = new aiMesh* [] { new aiMesh() };

  auto mesh = scene->mMeshes[0];
  mesh->mNumVertices = num_vertices;
  mesh->mVertices = new aiVector3D[num_vertices];
  mesh->mNumFaces = num_vertices;
  mesh->mFaces = new aiFace[num_vertices];
  for (size_t i = 0; i < num_vertices; ++i) {
    INTR_ASSIGN_OR_RETURN(const auto& p, point_func(i));

    mesh->mVertices[i] = {static_cast<ai_real>(p[0]),
                          static_cast<ai_real>(p[1]),
                          static_cast<ai_real>(p[2])};
    mesh->mFaces[i].mNumIndices = 1;
    mesh->mFaces[i].mIndices = new unsigned[]{static_cast<unsigned int>(i)};
    INTR_ASSIGN_OR_RETURN(std::optional<aiColor4D> color, color_func(i));
    if (color.has_value()) {
      if (mesh->mColors[0] == nullptr) {
        mesh->mColors[0] = new aiColor4D[num_vertices];
      }
      mesh->mColors[0][i] = std::move(color).value();
    }
  }

  // workaround, https://github.com/assimp/assimp/issues/3778
  mesh->mPrimitiveTypes = aiPrimitiveType_POINT;

  if (material.has_value()) {
    scene->mNumMaterials = 1;
    scene->mMaterials = new aiMaterial* [] { new aiMaterial() };

    aiColor4D ambient(material->ambient[0], material->ambient[1],
                      material->ambient[2], material->ambient[3]);
    scene->mMaterials[0]->AddProperty(&ambient, 1, AI_MATKEY_COLOR_AMBIENT);
    aiColor4D diffuse(material->diffuse[0], material->diffuse[1],
                      material->diffuse[2], material->diffuse[3]);
    scene->mMaterials[0]->AddProperty(&diffuse, 1, AI_MATKEY_COLOR_DIFFUSE);
    aiColor4D specular(material->specular[0], material->specular[1],
                       material->specular[2], material->specular[3]);
    scene->mMaterials[0]->AddProperty(&specular, 1, AI_MATKEY_COLOR_SPECULAR);
    aiColor4D emission(material->emission[0], material->emission[1],
                       material->emission[2], material->emission[3]);
    scene->mMaterials[0]->AddProperty(&emission, 1, AI_MATKEY_COLOR_EMISSIVE);
    scene->mMaterials[0]->AddProperty(&material->shininess, 1,
                                      AI_MATKEY_SHININESS);
  }

  scene->mRootNode = new aiNode();
  scene->mRootNode->mNumMeshes = 1;
  scene->mRootNode->mMeshes = new unsigned[]{0};
  // workaround, https://github.com/assimp/assimp/issues/3781
  scene->mMetaData = new aiMetadata();
  return scene;
}

}  // namespace

absl::StatusOr<std::unique_ptr<aiScene>> PtsFileToAiScene(
    const std::string& file_content, eigenmath::Vector3d scale) {
  std::stringstream stream(file_content);
  INTR_ASSIGN_OR_RETURN(
      const int num_vertices,
      ParseAndValidatePtsPointCount(stream, file_content.size()));
  return ToAiScene(
      num_vertices,
      [&stream, &scale](size_t i) -> absl::StatusOr<std::array<double, 3>> {
        double x, y, z;
        int intensity;
        stream >> x >> y >> z;
        stream >> intensity;
        if (stream.bad() || stream.fail()) {
          return absl::InvalidArgumentError(
              absl::StrCat("Bad point cloud data on line ", i + 1));
        }
        return std::array<double, 3>{x * scale[0], y * scale[1], z * scale[2]};
      },
      [&stream](size_t i) -> absl::StatusOr<std::optional<aiColor4D>> {
        // We have the implicit assumption that we call the other lambda first
        // per line and so this one expects to see r,g,b and not anything else.
        int r, g, b;
        stream >> r >> g >> b;
        if (stream.bad() || stream.fail()) {
          return absl::InvalidArgumentError(
              absl::StrCat("Bad point cloud data on line ", i + 1));
        }

        return aiColor4D(r, g, b, 255) / aiColor4D(255);
      });
}

absl::StatusOr<std::unique_ptr<aiScene>> PointCloudToAiScene(
    const PointCloud& point_cloud, eigenmath::Vector3d scale,
    std::optional<Material> material,
    std::optional<std::span<const uint8_t>> colors) {
  const size_t num_points = point_cloud.getPoints().size();
  std::optional<Material> effective_material = material;

  if (colors.has_value()) {
    if (colors->size() != 3 && colors->size() != 3 * num_points) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Point cloud colors size (", colors->size(),
          ") is invalid. Expected 3 for uniform RGB or 3 * points size (",
          3 * num_points, ") for per-point RGB."));
    }
    if (colors->size() == 3) {
      if (!effective_material.has_value()) {
        effective_material = Material();
      }
      effective_material->diffuse = {
          static_cast<float>((*colors)[0]) / 255.0f,
          static_cast<float>((*colors)[1]) / 255.0f,
          static_cast<float>((*colors)[2]) / 255.0f,
          1.0f,
      };
    }
  }

  const bool has_per_point_colors =
      colors.has_value() && colors->size() == 3 * num_points && num_points > 0;

  return ToAiScene(
      num_points,
      [&point_cloud, &scale](size_t i) -> absl::StatusOr<eigenmath::Vector3d> {
        return point_cloud.getPoints()[i].cwiseProduct(scale);
      },
      [&colors, has_per_point_colors](
          size_t i) -> absl::StatusOr<std::optional<aiColor4D>> {
        if (!has_per_point_colors) {
          return std::nullopt;
        }
        return aiColor4D(static_cast<float>((*colors)[3 * i]) / 255.0f,
                         static_cast<float>((*colors)[3 * i + 1]) / 255.0f,
                         static_cast<float>((*colors)[3 * i + 2]) / 255.0f,
                         1.0f);
      },
      effective_material);
}

}  // namespace intrinsic::geo
