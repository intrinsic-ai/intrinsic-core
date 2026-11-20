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

#include "intrinsic/geometry/gltf/gltf_buffer_reader.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "intrinsic/geometry/gltf/gltf_accessor.h"
#include "intrinsic/util/status/status_macros.h"
#include "tiny_gltf.h"

namespace intrinsic::geo {
namespace {

// Whether ReadComponentAsFloat supports `acc`: FLOAT (not normalized, which
// glTF forbids) and 8/16-bit integers.
bool IsSupportedAttributeType(const tinygltf::Accessor& acc) {
  switch (acc.componentType) {
    case TINYGLTF_COMPONENT_TYPE_FLOAT:
      return !acc.normalized;
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
    case TINYGLTF_COMPONENT_TYPE_BYTE:
    case TINYGLTF_COMPONENT_TYPE_SHORT:
      return true;
    default:
      return false;
  }
}

// Returns the component at `src` as a float, applying `normalized` as glTF
// defines it. `component_type` must pass IsSupportedAttributeType.
float ReadComponentAsFloat(int component_type, bool normalized,
                           const unsigned char* src) {
  // Every supported type is exact in a float, so this matches reading it
  // directly.
  const float v =
      static_cast<float>(ReadGltfRawComponent(src, component_type, 0));
  switch (component_type) {
    // glTF maps normalized values to [0, 1] by dividing by the type's maximum,
    // and signed ones to [-1, 1] with max(v / max_value, -1).
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
      return normalized ? v / std::numeric_limits<uint8_t>::max() : v;
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
      return normalized ? v / std::numeric_limits<uint16_t>::max() : v;
    case TINYGLTF_COMPONENT_TYPE_BYTE:
      return normalized
                 ? std::max(v / std::numeric_limits<int8_t>::max(), -1.0f)
                 : v;
    case TINYGLTF_COMPONENT_TYPE_SHORT:
      return normalized
                 ? std::max(v / std::numeric_limits<int16_t>::max(), -1.0f)
                 : v;
    default:  // FLOAT
      return v;
  }
}

}  // namespace

absl::StatusOr<std::vector<float>> ReadGltfAttributeFloats(
    const tinygltf::Model& model, const tinygltf::Primitive& prim,
    absl::string_view attr_name, int out_components) {
  // glTF attributes have at most 4 components (VEC4).
  constexpr int kMaxComponents = 4;
  if (out_components < 1 || out_components > kMaxComponents) {
    return absl::InvalidArgumentError("out_components must be 1 to 4.");
  }
  const auto it = prim.attributes.find(std::string(attr_name));
  if (it == prim.attributes.end()) {
    return std::vector<float>{};
  }
  INTR_ASSIGN_OR_RETURN(const GltfAccessorSlice s,
                        GetGltfAccessorSlice(model, it->second));
  const tinygltf::Accessor& acc = model.accessors[it->second];
  if (!IsSupportedAttributeType(acc)) {
    return absl::InvalidArgumentError("Unsupported attribute type.");
  }
  const int comps = tinygltf::GetNumComponentsInType(acc.type);
  const int csize = tinygltf::GetComponentSizeInBytes(acc.componentType);
  std::vector<float> out(s.count * out_components);
  for (size_t i = 0; i < s.count; ++i) {
    for (int c = 0; c < out_components; ++c) {
      float& dst = out[i * out_components + c];
      if (c >= comps) {
        // Missing components are 0, except the fourth (w, or alpha), which
        // is 1.
        constexpr int kFourthComponent = 3;
        dst = (c == kFourthComponent) ? 1.0f : 0.0f;
        continue;
      }
      dst = ReadComponentAsFloat(acc.componentType, acc.normalized,
                                 s.data + i * s.stride + c * csize);
      if (!std::isfinite(dst)) {
        return absl::InvalidArgumentError(
            "glTF vertex attribute contains non-finite values.");
      }
    }
  }
  return out;
}

absl::StatusOr<std::vector<unsigned int>> ReadGltfTriangleIndices(
    const tinygltf::Model& model, const tinygltf::Primitive& prim) {
  if (!IsGltfTriangleList(prim)) {
    return absl::InvalidArgumentError("Primitive is not a triangle list.");
  }
  std::vector<unsigned int> indices;
  if (prim.indices < 0) {
    const auto position = prim.attributes.find("POSITION");
    if (position == prim.attributes.end()) {
      return absl::InvalidArgumentError(
          "Primitive has neither indices nor POSITION.");
    }
    // The slice checks that the POSITION data exists, which caps the count.
    INTR_ASSIGN_OR_RETURN(const GltfAccessorSlice positions,
                          GetGltfAccessorSlice(model, position->second));
    indices.resize(positions.count);
    std::iota(indices.begin(), indices.end(), 0u);
  } else {
    INTR_ASSIGN_OR_RETURN(const GltfAccessorSlice s,
                          GetGltfAccessorSlice(model, prim.indices));
    if (!IsGltfIndexAccessor(model.accessors[prim.indices])) {
      return absl::InvalidArgumentError("Unsupported index type.");
    }
    const int component_type = model.accessors[prim.indices].componentType;
    indices.resize(s.count);
    for (size_t i = 0; i < s.count; ++i) {
      indices[i] = ReadGltfIndex(s, component_type, i);
    }
  }
  if (indices.size() % kGltfTriangleSize != 0) {
    return absl::InvalidArgumentError(
        "Triangle index count must be a multiple of 3.");
  }
  return indices;
}

}  // namespace intrinsic::geo
