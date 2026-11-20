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

#include "intrinsic/geometry/gltf/gltf_accessor.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "tiny_gltf.h"

namespace intrinsic::geo {
namespace {

// glTF starts each matrix column on a 4-byte boundary, so MAT2/MAT3 with 1- or
// 2-byte components have padding, which the slice math doesn't handle.
constexpr int kGltfMatrixColumnAlignment = 4;

// Reads a `T` from possibly unaligned `src`.
template <typename T>
T ReadAs(const unsigned char* src) {
  T v{};
  std::memcpy(&v, src, sizeof(v));
  return v;
}

}  // namespace

absl::StatusOr<GltfAccessorSlice> GetGltfAccessorSlice(
    const tinygltf::Model& model, int accessor_index) {
  if (accessor_index < 0 ||
      static_cast<size_t>(accessor_index) >= model.accessors.size() ||
      model.accessors[accessor_index].sparse.isSparse) {
    return absl::InvalidArgumentError("Invalid or sparse glTF accessor.");
  }
  return GetGltfAccessorSlice(model, model.accessors[accessor_index]);
}

// glTF data is reached in three hops: accessor -> bufferView -> buffer. This
// checks each hop, writing the range checks as subtractions so large values
// can't overflow.
absl::StatusOr<GltfAccessorSlice> GetGltfAccessorSlice(
    const tinygltf::Model& model, const tinygltf::Accessor& acc) {
  const int comps = tinygltf::GetNumComponentsInType(acc.type);
  const int csize = tinygltf::GetComponentSizeInBytes(acc.componentType);
  const bool padded_matrix =
      (acc.type == TINYGLTF_TYPE_MAT2 || acc.type == TINYGLTF_TYPE_MAT3) &&
      csize < kGltfMatrixColumnAlignment;
  if (comps <= 0 || csize <= 0 || padded_matrix || acc.bufferView < 0 ||
      static_cast<size_t>(acc.bufferView) >= model.bufferViews.size()) {
    return absl::InvalidArgumentError("Invalid glTF accessor metadata.");
  }
  const tinygltf::BufferView& bv = model.bufferViews[acc.bufferView];
  if (bv.buffer < 0 || static_cast<size_t>(bv.buffer) >= model.buffers.size()) {
    return absl::InvalidArgumentError("Invalid glTF buffer.");
  }
  const std::vector<unsigned char>& buffer = model.buffers[bv.buffer].data;
  const size_t elem_size = static_cast<size_t>(comps * csize);
  // byteStride 0 means the elements are tightly packed.
  const size_t stride = bv.byteStride > 0 ? bv.byteStride : elem_size;
  // Elements don't overlap, the view is inside the buffer, the accessor starts
  // inside the view, and its last element ends inside it.
  if (stride < elem_size || bv.byteOffset > buffer.size() ||
      bv.byteLength > buffer.size() - bv.byteOffset ||
      acc.byteOffset > bv.byteLength ||
      (acc.count > 0 &&
       (bv.byteLength - acc.byteOffset < elem_size ||
        acc.count - 1 >
            (bv.byteLength - acc.byteOffset - elem_size) / stride))) {
    return absl::InvalidArgumentError("glTF accessor out of bounds.");
  }
  return GltfAccessorSlice{
      .data = buffer.data() + bv.byteOffset + acc.byteOffset,
      .stride = stride,
      .elem_size = elem_size,
      .count = acc.count,
  };
}

bool IsGltfIndexAccessor(const tinygltf::Accessor& acc) {
  return acc.type == TINYGLTF_TYPE_SCALAR &&
         (acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE ||
          acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT ||
          acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT);
}

uint32_t ReadGltfIndex(const GltfAccessorSlice& s, int component_type,
                       size_t i) {
  if (i >= s.count) {
    return 0;
  }
  const unsigned char* p = s.data + i * s.stride;
  switch (component_type) {
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
      return *p;
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
      return ReadAs<uint16_t>(p);
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
      return ReadAs<uint32_t>(p);
    default:
      return 0;
  }
}

double ReadGltfRawComponent(const unsigned char* element, int component_type,
                            size_t c) {
  const int csize = tinygltf::GetComponentSizeInBytes(component_type);
  if (csize <= 0) {
    return std::numeric_limits<double>::quiet_NaN();
  }
  const unsigned char* src = element + c * csize;
  switch (component_type) {
    case TINYGLTF_COMPONENT_TYPE_FLOAT:
      return ReadAs<float>(src);
    case TINYGLTF_COMPONENT_TYPE_DOUBLE:
      return ReadAs<double>(src);
    case TINYGLTF_COMPONENT_TYPE_BYTE:
      return ReadAs<int8_t>(src);
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
      return ReadAs<uint8_t>(src);
    case TINYGLTF_COMPONENT_TYPE_SHORT:
      return ReadAs<int16_t>(src);
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
      return ReadAs<uint16_t>(src);
    case TINYGLTF_COMPONENT_TYPE_INT:
      return ReadAs<int32_t>(src);
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
      return ReadAs<uint32_t>(src);
    default:
      return std::numeric_limits<double>::quiet_NaN();
  }
}

}  // namespace intrinsic::geo
