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

#ifndef INTRINSIC_GEOMETRY_GLTF_GLTF_MATERIALS_H_
#define INTRINSIC_GEOMETRY_GLTF_GLTF_MATERIALS_H_

// Helpers for the glTF material extensions this codebase writes.

#include "absl/strings/string_view.h"
#include "tiny_gltf.h"

namespace intrinsic::geo {

// Name of the glTF extension carrying the material transmission factor.
inline constexpr absl::string_view kKhrMaterialsTransmission =
    "KHR_materials_transmission";

// Extracts the transmissionFactor from the KHR_materials_transmission extension
// on `mat`, returning 0.0 if the extension or a numeric property is not
// present.
double GetGltfTransmissionFactor(const tinygltf::Material& mat);

// Sets the transmissionFactor on `mat` via the KHR_materials_transmission
// extension. Any existing KHR_materials_transmission extension on `mat` is
// replaced entirely, so other properties it carried (e.g.
// `transmissionTexture`) are discarded. Callers attaching `mat` to a
// tinygltf::Model are responsible for declaring kKhrMaterialsTransmission in
// `model.extensionsUsed` (e.g. via AddGltfExtensionUsed()).
void SetGltfTransmissionFactor(tinygltf::Material& mat, double factor);

// Appends `extension` to `model.extensionsUsed` if it is not already present.
void AddGltfExtensionUsed(tinygltf::Model& model, absl::string_view extension);

}  // namespace intrinsic::geo

#endif  // INTRINSIC_GEOMETRY_GLTF_GLTF_MATERIALS_H_
