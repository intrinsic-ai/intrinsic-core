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

#ifndef INTRINSIC_GEOMETRY_GLTF_GLTF_ACCESSOR_H_
#define INTRINSIC_GEOMETRY_GLTF_GLTF_ACCESSOR_H_

// Low-level, bounds-checked access to the raw bytes of a tinygltf accessor.
// Shared by gltf_buffer_reader, gltf_buffer_writer and gltf_validation.

#include <cstddef>
#include <cstdint>

#include "absl/status/statusor.h"
#include "tiny_gltf.h"

namespace intrinsic::geo {

// Components of a POSITION, which glTF requires to be a VEC3.
inline constexpr int kGltfPositionComponents = 3;
// Indices per triangle in a TRIANGLES primitive.
inline constexpr size_t kGltfTriangleSize = 3;
// glTF's value for "no reference" in an index field (e.g. `node.mesh`).
inline constexpr int kGltfNoIndex = -1;
// tinygltf's `Primitive::mode` when it isn't set, which glTF reads as
// TRIANGLES.
inline constexpr int kGltfModeNotSet = -1;

// Returns true if `prim` is a triangle list (mode TRIANGLES or unset).
inline bool IsGltfTriangleList(const tinygltf::Primitive& prim) {
  return prim.mode == kGltfModeNotSet || prim.mode == TINYGLTF_MODE_TRIANGLES;
}

// The raw bytes of an accessor: element `i` starts at `data + i * stride` and
// is `elem_size` bytes long (e.g. 12 for a float VEC3).
struct GltfAccessorSlice {
  const unsigned char* data = nullptr;
  size_t stride = 0;
  size_t elem_size = 0;
  size_t count = 0;
};

// Returns the raw bytes of `model.accessors[accessor_index]`, after checking
// that the accessor, its bufferView and its buffer exist and that every
// element is in bounds. Returns InvalidArgumentError otherwise, for sparse
// accessors, and for matrices with 8/16-bit components (padded columns).
absl::StatusOr<GltfAccessorSlice> GetGltfAccessorSlice(
    const tinygltf::Model& model, int accessor_index);

// Like above, for `acc`'s dense data, ignoring `acc.sparse`. Used to check the
// base data, indices and values of sparse accessors.
absl::StatusOr<GltfAccessorSlice> GetGltfAccessorSlice(
    const tinygltf::Model& model, const tinygltf::Accessor& acc);

// Returns true if `acc` can hold indices: an unsigned 8/16/32-bit scalar.
bool IsGltfIndexAccessor(const tinygltf::Accessor& acc);

// Returns index `i` of `s`, the slice of an index accessor of
// `component_type`, or 0 if `component_type` is not an unsigned integer type
// or `i` is not below `s.count`.
uint32_t ReadGltfIndex(const GltfAccessorSlice& s, int component_type,
                       size_t i);

// Returns component `c` of the element at `element` as stored, ignoring
// `normalized` (glTF min/max hold stored values). Returns NaN for an unknown
// `component_type`.
double ReadGltfRawComponent(const unsigned char* element, int component_type,
                            size_t c);

}  // namespace intrinsic::geo

#endif  // INTRINSIC_GEOMETRY_GLTF_GLTF_ACCESSOR_H_
