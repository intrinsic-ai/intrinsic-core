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

#ifndef INTRINSIC_GEOMETRY_API_APPLY_MATERIAL_PROPERTIES_H_
#define INTRINSIC_GEOMETRY_API_APPLY_MATERIAL_PROPERTIES_H_

#include <memory>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "assimp/scene.h"
#include "intrinsic/geometry/api/material.h"
#include "intrinsic/geometry/api/renderable.h"
#include "intrinsic/geometry/proto/v1/material.pb.h"
#include "tiny_gltf.h"

namespace intrinsic::geo {

// Returns InvalidArgument if any set field of `material_properties` is not
// finite or outside [0, 1], or if `base_color.alpha` is set to anything other
// than 1, since alpha is not supported; transmission is used instead.
absl::Status ValidateMaterialProperties(
    const intrinsic_proto::geometry::v1::MaterialProperties&
        material_properties);

// Validates `material_properties` and converts it into a tinygltf::Material,
// including the KHR_materials_transmission extension. Unset fields use the
// defaults of MaterialProperties.
absl::StatusOr<tinygltf::Material> ToGltfMaterial(
    const intrinsic_proto::geometry::v1::MaterialProperties&
        material_properties);

// Applies `material_properties` to `geo`, replacing all existing materials and
// returning a new Renderable.
absl::StatusOr<std::shared_ptr<const Renderable>> ApplyMaterialProperties(
    std::shared_ptr<const Renderable> geo,
    const intrinsic_proto::geometry::v1::MaterialProperties&
        material_properties);

// Applies `material_properties` to `glb_bytes`, replacing all existing
// materials and returning the updated GLB bytes.
//
// Textures, samplers, images, buffer data and extension declarations are kept,
// even if only the replaced materials used them. In particular, an extension
// listed in `extensionsRequired` stays required, so loaders that do not
// support it still reject the result.
absl::StatusOr<std::string> ApplyMaterialPropertiesToGlb(
    absl::string_view glb_bytes,
    const intrinsic_proto::geometry::v1::MaterialProperties&
        material_properties);

// Applies `material` to all materials in `scene`.
absl::Status ApplyMaterialToAiScene(aiScene& scene, const Material& material);

}  // namespace intrinsic::geo
namespace intrinsic {
using ::intrinsic::geo::ApplyMaterialPropertiesToGlb;
using ::intrinsic::geo::ToGltfMaterial;
using ::intrinsic::geo::ValidateMaterialProperties;
}  // namespace intrinsic

#endif  // INTRINSIC_GEOMETRY_API_APPLY_MATERIAL_PROPERTIES_H_
