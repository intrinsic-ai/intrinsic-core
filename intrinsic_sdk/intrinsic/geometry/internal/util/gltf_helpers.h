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

#ifndef INTRINSIC_GEOMETRY_INTERNAL_UTIL_GLTF_HELPERS_H_
#define INTRINSIC_GEOMETRY_INTERNAL_UTIL_GLTF_HELPERS_H_

#include <optional>
#include <string>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "tiny_gltf.h"

namespace intrinsic::geo {

// Name of the glTF extension carrying the material transmission factor.
inline constexpr absl::string_view kKhrMaterialsTransmission =
    "KHR_materials_transmission";
// Property key of the transmission factor in KHR_materials_transmission.
inline constexpr absl::string_view kTransmissionFactorKey =
    "transmissionFactor";

// Returns the numeric value of `value` if it holds a JSON number (integer or
// real), or std::nullopt otherwise. JSON does not distinguish integers from
// reals, and tinygltf serializes integral doubles such as 1.0 as "1", which
// re-parse as an integer Value.
std::optional<double> GetGltfNumber(const tinygltf::Value& value);

// Returns the extension named `name` in `ext`, or nullptr if absent.
const tinygltf::Value* FindGltfExtension(const tinygltf::ExtensionMap& ext,
                                         absl::string_view name);

// Returns the value for `key` if `object` is a JSON object containing it, or
// nullptr otherwise.
const tinygltf::Value* FindGltfObjectValue(const tinygltf::Value& object,
                                           absl::string_view key);

// Messages reported by tinygltf while parsing. tinygltf does not guarantee
// that `errors` is empty when parsing succeeds, so callers that require a
// clean parse (e.g. tests) can check it explicitly.
struct GltfParseDiagnostics {
  std::string errors;
  std::string warnings;
};

// Parses GLB or glTF JSON bytes (auto-detected) into a tinygltf::Model.
//
// Images are not decoded: `tinygltf::Image::image` stays empty while the
// image metadata (`bufferView`, `mimeType`, `uri`) and the underlying buffer
// bytes are preserved. Re-serializing the model with SerializeGltfToGlb()
// therefore keeps the original encoded image bytes.
//
// If `diagnostics` is non-null, it receives the tinygltf errors and warnings,
// on both success and failure. Otherwise, warnings are logged at WARNING
// severity. Warnings never cause failure.
absl::StatusOr<tinygltf::Model> ParseGltf(
    absl::string_view bytes, GltfParseDiagnostics* diagnostics = nullptr);

// Parses binary GLB bytes into a tinygltf::Model, rejecting glTF JSON input.
//
// Images and `diagnostics` are handled as in ParseGltf().
absl::StatusOr<tinygltf::Model> ParseGlb(
    absl::string_view glb_bytes, GltfParseDiagnostics* diagnostics = nullptr);

// Serializes `model` to a binary GLB string.
absl::StatusOr<std::string> SerializeGltfToGlb(const tinygltf::Model& model);

// Extracts the transmissionFactor from the KHR_materials_transmission extension
// on `mat`, returning 0.0 if the extension or a numeric property is not
// present.
double GetGltfTransmissionFactor(const tinygltf::Material& mat);

}  // namespace intrinsic::geo

#endif  // INTRINSIC_GEOMETRY_INTERNAL_UTIL_GLTF_HELPERS_H_
