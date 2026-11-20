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

#ifndef INTRINSIC_GEOMETRY_GLTF_GLTF_BUFFER_READER_H_
#define INTRINSIC_GEOMETRY_GLTF_GLTF_BUFFER_READER_H_

// Reads the vertex and index data of a tinygltf::Model as flat arrays.
//
// Validate, then process: call ValidateGltfBuffers() (gltf_validation.h) on a
// parsed model before using these functions, and again before serializing an
// edited model. The functions assume a model that passed it. If one returns an
// error, the model is left in an unspecified state and should be discarded.

#include <vector>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "tiny_gltf.h"

namespace intrinsic::geo {

// Reads vertex attribute `attr_name` from `prim` as `out_components` (1 to 4)
// floats per vertex. Returns an empty vector if `attr_name` is absent. Supports
// `FLOAT` and 8/16-bit integers (normalized or not); missing components are
// padded with `0.0f`, or `1.0f` for the fourth. Returns InvalidArgumentError
// for sparse accessors, other component types, normalized `FLOAT`s, and
// non-finite values.
absl::StatusOr<std::vector<float>> ReadGltfAttributeFloats(
    const tinygltf::Model& model, const tinygltf::Primitive& prim,
    absl::string_view attr_name, int out_components);

// Reads the indices of `prim`, a triangle list, or returns 0..n-1 for its n
// POSITION vertices if it has none. Returns InvalidArgumentError if `prim`'s
// mode is not TRIANGLES (or unset), its indices are not unsigned integers, the
// index count is not a multiple of 3, or it has neither indices nor readable
// POSITION data.
absl::StatusOr<std::vector<unsigned int>> ReadGltfTriangleIndices(
    const tinygltf::Model& model, const tinygltf::Primitive& prim);

}  // namespace intrinsic::geo

#endif  // INTRINSIC_GEOMETRY_GLTF_GLTF_BUFFER_READER_H_
