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

#include "intrinsic/geometry/api/apply_material_properties.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "assimp/color4.h"
#include "assimp/material.h"
#include "assimp/scene.h"
#include "google/protobuf/wrappers.pb.h"
#include "google/type/color.pb.h"
#include "intrinsic/geometry/api/material.h"
#include "intrinsic/geometry/api/renderable.h"
#include "intrinsic/geometry/internal/util/gltf_helpers.h"
#include "intrinsic/geometry/proto/v1/material.pb.h"
#include "intrinsic/util/status/status_macros.h"
#include "tiny_gltf.h"

namespace intrinsic::geo {

using ::intrinsic_proto::geometry::v1::MaterialProperties;

namespace {
constexpr double kDefaultColor[4]{0.5, 0.5, 0.5, 1.0};
constexpr double kDefaultMetalness = 1.0;
constexpr double kDefaultRoughness = 1.0;
constexpr double kDefaultTransmission = 0.0;
}  // namespace

absl::Status ValidateMaterialProperties(
    const MaterialProperties& material_properties) {
  if (material_properties.has_base_color()) {
    const auto& base_color = material_properties.base_color();
    if (base_color.red() < 0.0 || base_color.red() > 1.0 ||
        base_color.green() < 0.0 || base_color.green() > 1.0 ||
        base_color.blue() < 0.0 || base_color.blue() > 1.0) {
      return absl::InvalidArgumentError(absl::Substitute(
          "Base color must be in the range of [0, 1]. Got [r=$0, g=$1, b=$2]",
          base_color.red(), base_color.green(), base_color.blue()));
    }
    if (base_color.has_alpha() && base_color.alpha().value() != 1.0) {
      return absl::InvalidArgumentError(
          "Base color alpha is not supported. Please use transmission in "
          "material properties instead.");
    }
  }
  if (material_properties.has_metalness()) {
    const double metalness = material_properties.metalness();
    if (metalness < 0.0 || metalness > 1.0) {
      return absl::InvalidArgumentError(absl::Substitute(
          "Metalness must be in the range of [0, 1]. Got $0", metalness));
    }
  }
  if (material_properties.has_transmission()) {
    const double transmission = material_properties.transmission();
    if (transmission < 0.0 || transmission > 1.0) {
      return absl::InvalidArgumentError(absl::Substitute(
          "Transmission must be in the range of [0, 1]. Got $0", transmission));
    }
  }
  if (material_properties.has_roughness()) {
    const double roughness = material_properties.roughness();
    if (roughness < 0.0 || roughness > 1.0) {
      return absl::InvalidArgumentError(absl::Substitute(
          "Roughness must be in the range of [0, 1]. Got $0", roughness));
    }
  }
  return absl::OkStatus();
}

absl::StatusOr<tinygltf::Material> ToGltfMaterial(
    const MaterialProperties& material_properties) {
  INTR_RETURN_IF_ERROR(ValidateMaterialProperties(material_properties));

  std::vector<double> base_color_factor;
  if (material_properties.has_base_color()) {
    const auto& base_color = material_properties.base_color();
    base_color_factor = {base_color.red(), base_color.green(),
                         base_color.blue(), 1.0};
  } else {
    base_color_factor = std::vector<double>(kDefaultColor, kDefaultColor + 4);
  }
  const double metalness = material_properties.has_metalness()
                               ? material_properties.metalness()
                               : kDefaultMetalness;
  const double transmission = material_properties.has_transmission()
                                  ? material_properties.transmission()
                                  : kDefaultTransmission;
  const double roughness = material_properties.has_roughness()
                               ? material_properties.roughness()
                               : kDefaultRoughness;

  tinygltf::Material material;
  material.name = "user_specified_material";
  material.pbrMetallicRoughness.baseColorFactor = std::move(base_color_factor);
  material.pbrMetallicRoughness.metallicFactor = metalness;
  material.pbrMetallicRoughness.roughnessFactor = roughness;
  material.extensions[std::string(kKhrMaterialsTransmission)] = tinygltf::Value(
      tinygltf::Value::Object{{std::string(kTransmissionFactorKey),
                               tinygltf::Value(transmission)}});
  return material;
}

absl::StatusOr<std::shared_ptr<const Renderable>> ApplyMaterialProperties(
    std::shared_ptr<const Renderable> geo,
    const MaterialProperties& material_properties) {
  std::string glb_bytes = geo->GetGLBString();
  INTR_ASSIGN_OR_RETURN(
      glb_bytes, ApplyMaterialPropertiesToGlb(glb_bytes, material_properties));
  return std::make_shared<Renderable>(std::move(glb_bytes));
}

absl::StatusOr<std::string> ApplyMaterialPropertiesToGlb(
    absl::string_view glb_bytes,
    const MaterialProperties& material_properties) {
  INTR_ASSIGN_OR_RETURN(tinygltf::Material material,
                        ToGltfMaterial(material_properties));

  INTR_ASSIGN_OR_RETURN(tinygltf::Model model, ParseGlb(glb_bytes));

  // Clears existing materials and creates a new material. Replace all mesh
  // primitives to use this new material.
  model.materials = {std::move(material)};

  for (auto& mesh : model.meshes) {
    for (auto& primitive : mesh.primitives) {
      primitive.material = 0;
    }
  }

  return SerializeGltfToGlb(model);
}

absl::Status ApplyMaterialToAiScene(aiScene& scene, const Material& material) {
  aiColor4D ambient(material.ambient[0], material.ambient[1],
                    material.ambient[2], material.ambient[3]);
  aiColor4D diffuse(material.diffuse[0], material.diffuse[1],
                    material.diffuse[2], material.diffuse[3]);
  aiColor4D specular(material.specular[0], material.specular[1],
                     material.specular[2], material.specular[3]);
  aiColor4D emission(material.emission[0], material.emission[1],
                     material.emission[2], material.emission[3]);

  // If we didn't have any materials, create a new one.
  if (scene.mNumMaterials == 0) {
    scene.mMaterials = new aiMaterial*[1];
    scene.mMaterials[0] = new aiMaterial();
    scene.mNumMaterials = 1;
  }

  // Apply the overrides to all materials in the scene.
  for (int i = 0; i < scene.mNumMaterials; ++i) {
    scene.mMaterials[i]->Clear();
    scene.mMaterials[i]->AddProperty(&ambient, 1, AI_MATKEY_COLOR_AMBIENT);

    scene.mMaterials[i]->AddProperty(&diffuse, 1, AI_MATKEY_COLOR_DIFFUSE);

    scene.mMaterials[i]->AddProperty(&specular, 1, AI_MATKEY_COLOR_SPECULAR);

    scene.mMaterials[i]->AddProperty(&emission, 1, AI_MATKEY_COLOR_EMISSIVE);
    scene.mMaterials[i]->AddProperty(&material.shininess, 1,
                                     AI_MATKEY_SHININESS);
  }

  return absl::OkStatus();
}

}  // namespace intrinsic::geo
