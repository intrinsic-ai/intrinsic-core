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

#ifndef INTRINSIC_GEOMETRY_GLTF_GLTF_IO_H_
#define INTRINSIC_GEOMETRY_GLTF_GLTF_IO_H_

// Reads glTF/GLB bytes into a tinygltf::Model and writes a model as GLB.

#include <string>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "tiny_gltf.h"

namespace intrinsic::geo {

// Messages reported by tinygltf while parsing. tinygltf does not guarantee
// that `errors` is empty when parsing succeeds, so callers that require a
// clean parse (e.g. tests) can check it explicitly.
struct GltfParseDiagnostics {
  std::string errors;
  std::string warnings;
};

// Parses binary GLB bytes into a tinygltf::Model, rejecting glTF JSON input.
//
// Images are not decoded: `tinygltf::Image::image` stays empty while the
// image metadata (`bufferView`, `mimeType`, `uri`) and the underlying buffer
// bytes are preserved. Re-serializing the model with SerializeGltfToGlb()
// therefore keeps the original encoded image bytes.
//
// If `diagnostics` is non-null, it receives the tinygltf errors and warnings,
// on both success and failure. Otherwise, warnings are logged at WARNING
// severity. Warnings never cause failure.
absl::StatusOr<tinygltf::Model> ParseGlb(
    absl::string_view glb_bytes, GltfParseDiagnostics* diagnostics = nullptr);

// Serializes `model` to a binary GLB string.
absl::StatusOr<std::string> SerializeGltfToGlb(const tinygltf::Model& model);

}  // namespace intrinsic::geo

#endif  // INTRINSIC_GEOMETRY_GLTF_GLTF_IO_H_
