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

#include <cmath>
#include <memory>
#include <string>
#include <utility>

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
#include "intrinsic/geometry/api/material_properties.h"
#include "intrinsic/geometry/api/renderable.h"
#include "intrinsic/geometry/internal/util/gltf_helpers.h"
#include "intrinsic/geometry/proto/v1/material.pb.h"
#include "intrinsic/util/status/status_macros.h"
#include "tiny_gltf.h"

namespace intrinsic::geo {

// In this file, `MaterialProperties` refers to the C++ struct from
// material_properties.h and `MaterialPropertiesProto` to the proto message
// used in the public API.
using MaterialPropertiesProto =
    ::intrinsic_proto::geometry::v1::MaterialProperties;

namespace {

// Checks finiteness explicitly because IEEE 754 NaN comparisons return false.
bool IsInUnitRange(double v) {
  return std::isfinite(v) && v >= 0.0 && v <= 1.0;
}

// Builds a PBR material from validated `props`. Base color alpha is fixed to
// 1.0 because transparency is represented via KHR_materials_transmission.
tinygltf::Material MakeGltfMaterial(const MaterialProperties& props) {
  tinygltf::Material material;
  material.name = "user_specified_material";
  material.pbrMetallicRoughness.baseColorFactor = {
      props.color.x(), props.color.y(), props.color.z(), 1.0};
  material.pbrMetallicRoughness.metallicFactor = props.metalness;
  material.pbrMetallicRoughness.roughnessFactor = props.roughness;
  SetGltfTransmissionFactor(material, props.transmission);
  return material;
}

// Replaces all materials in `model` with `material` and points every mesh
// primitive at it.
//
// Unused textures, samplers, images, bufferViews, buffer bytes, and extension
// declarations (notably `extensionsRequired` entries) referenced only by the
// replaced materials are intentionally retained; pruning them can be done in a
// follow-up.
void ReplaceMaterials(tinygltf::Model& model, tinygltf::Material material) {
  model.materials = {std::move(material)};
  for (tinygltf::Mesh& mesh : model.meshes) {
    for (tinygltf::Primitive& primitive : mesh.primitives) {
      primitive.material = 0;
    }
  }

  // glTF 2.0 specification section 3.12 requires every extension used in the
  // asset to be listed in `extensionsUsed`, which `MakeGltfMaterial` needs for
  // KHR_materials_transmission.
  AddGltfExtensionUsed(model, kKhrMaterialsTransmission);
}

}  // namespace

absl::Status ValidateMaterialProperties(
    const MaterialPropertiesProto& material_properties) {
  if (material_properties.has_base_color()) {
    const auto& base_color = material_properties.base_color();
    if (!IsInUnitRange(base_color.red()) ||
        !IsInUnitRange(base_color.green()) ||
        !IsInUnitRange(base_color.blue())) {
      return absl::InvalidArgumentError(absl::Substitute(
          "Base color must be in the range of [0, 1]. Got [r=$0, g=$1, b=$2]",
          base_color.red(), base_color.green(), base_color.blue()));
    }
    // NaN compares unequal to 1.0 and is rejected as well.
    if (base_color.has_alpha() && base_color.alpha().value() != 1.0) {
      return absl::InvalidArgumentError(
          "Base color alpha is not supported. Please use transmission in "
          "material properties instead.");
    }
  }
  if (material_properties.has_metalness()) {
    const double metalness = material_properties.metalness();
    if (!IsInUnitRange(metalness)) {
      return absl::InvalidArgumentError(absl::Substitute(
          "Metalness must be in the range of [0, 1]. Got $0", metalness));
    }
  }
  if (material_properties.has_transmission()) {
    const double transmission = material_properties.transmission();
    if (!IsInUnitRange(transmission)) {
      return absl::InvalidArgumentError(absl::Substitute(
          "Transmission must be in the range of [0, 1]. Got $0", transmission));
    }
  }
  if (material_properties.has_roughness()) {
    const double roughness = material_properties.roughness();
    if (!IsInUnitRange(roughness)) {
      return absl::InvalidArgumentError(absl::Substitute(
          "Roughness must be in the range of [0, 1]. Got $0", roughness));
    }
  }
  return absl::OkStatus();
}

absl::StatusOr<tinygltf::Material> ToGltfMaterial(
    const MaterialPropertiesProto& material_properties) {
  INTR_RETURN_IF_ERROR(ValidateMaterialProperties(material_properties));
  return MakeGltfMaterial(MaterialProperties::FromProto(material_properties));
}

absl::StatusOr<std::shared_ptr<const Renderable>> ApplyMaterialProperties(
    std::shared_ptr<const Renderable> geo,
    const MaterialPropertiesProto& material_properties) {
  if (geo == nullptr) {
    return absl::InvalidArgumentError("Renderable is null.");
  }
  std::string glb_bytes = geo->GetGLBString();
  INTR_ASSIGN_OR_RETURN(
      glb_bytes, ApplyMaterialPropertiesToGlb(glb_bytes, material_properties));
  return std::make_shared<Renderable>(std::move(glb_bytes));
}

absl::StatusOr<std::string> ApplyMaterialPropertiesToGlb(
    absl::string_view glb_bytes,
    const MaterialPropertiesProto& material_properties) {
  INTR_ASSIGN_OR_RETURN(tinygltf::Material material,
                        ToGltfMaterial(material_properties));
  INTR_ASSIGN_OR_RETURN(tinygltf::Model model, ParseGlb(glb_bytes));
  ReplaceMaterials(model, std::move(material));
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
  for (unsigned int i = 0; i < scene.mNumMaterials; ++i) {
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
