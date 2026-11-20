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

#include "intrinsic/geometry/gltf/gltf_validation.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "intrinsic/geometry/gltf/gltf_accessor.h"
#include "intrinsic/util/status/status_macros.h"
#include "tiny_gltf.h"

namespace intrinsic::geo {
namespace {

absl::Status InvalidGltfError(absl::string_view message) {
  return absl::InvalidArgumentError(absl::StrCat("Invalid glTF: ", message));
}

// Whether `index` is a valid index into a list of `size` elements.
bool IsInRange(int index, size_t size) {
  return index >= 0 && static_cast<size_t>(index) < size;
}

bool IsAccessor(const tinygltf::Model& model, int index) {
  return IsInRange(index, model.accessors.size());
}

bool IsBufferView(const tinygltf::Model& model, int index) {
  return IsInRange(index, model.bufferViews.size());
}

// Returns the indices of sparse accessor `acc` as an accessor.
tinygltf::Accessor SparseIndices(const tinygltf::Accessor& acc) {
  tinygltf::Accessor indices;
  indices.bufferView = acc.sparse.indices.bufferView;
  indices.byteOffset = acc.sparse.indices.byteOffset;
  indices.componentType = acc.sparse.indices.componentType;
  indices.type = TINYGLTF_TYPE_SCALAR;
  indices.count = static_cast<size_t>(acc.sparse.count);
  return indices;
}

// Returns the values of sparse accessor `acc` as an accessor.
tinygltf::Accessor SparseValues(const tinygltf::Accessor& acc) {
  tinygltf::Accessor values;
  values.bufferView = acc.sparse.values.bufferView;
  values.byteOffset = acc.sparse.values.byteOffset;
  values.componentType = acc.componentType;
  values.normalized = acc.normalized;
  values.type = acc.type;
  values.count = static_cast<size_t>(acc.sparse.count);
  return values;
}

// Returns the largest value of unsigned integer `component_type`, which glTF
// forbids as an index (renderers may read it as "primitive restart").
uint32_t PrimitiveRestartIndex(int component_type) {
  switch (component_type) {
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
      return std::numeric_limits<uint8_t>::max();
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
      return std::numeric_limits<uint16_t>::max();
    default:
      return std::numeric_limits<uint32_t>::max();
  }
}

// Buffers and bufferViews are non-empty, and every bufferView lies inside its
// buffer.
absl::Status CheckBuffersAndViews(const tinygltf::Model& model) {
  for (const tinygltf::Buffer& buffer : model.buffers) {
    if (buffer.data.empty()) {
      return InvalidGltfError("empty buffer");
    }
  }
  for (const tinygltf::BufferView& view : model.bufferViews) {
    if (view.byteLength == 0 || !IsInRange(view.buffer, model.buffers.size())) {
      return InvalidGltfError("empty bufferView or missing buffer");
    }
    const size_t size = model.buffers[view.buffer].data.size();
    if (view.byteOffset > size || view.byteLength > size - view.byteOffset) {
      return InvalidGltfError("bufferView out of bounds");
    }
  }
  for (const tinygltf::Image& image : model.images) {
    if (image.bufferView >= 0 && !IsBufferView(model, image.bufferView)) {
      return InvalidGltfError("missing image bufferView");
    }
  }
  return absl::OkStatus();
}

// A sparse accessor has 1 to `count` replaced elements; its base data (if
// any), indices and values lie inside their bufferViews; and its indices are
// unsigned integers below `count`.
absl::Status CheckSparseAccessor(const tinygltf::Model& model,
                                 const tinygltf::Accessor& acc) {
  if (acc.sparse.count <= 0 ||
      static_cast<size_t>(acc.sparse.count) > acc.count) {
    return InvalidGltfError("sparse accessor count");
  }
  if (acc.bufferView >= 0) {
    INTR_RETURN_IF_ERROR(GetGltfAccessorSlice(model, acc).status());
  }
  const tinygltf::Accessor indices = SparseIndices(acc);
  if (!IsGltfIndexAccessor(indices)) {
    return InvalidGltfError("unsupported sparse index type");
  }
  INTR_ASSIGN_OR_RETURN(const GltfAccessorSlice index_slice,
                        GetGltfAccessorSlice(model, indices));
  for (size_t i = 0; i < index_slice.count; ++i) {
    if (ReadGltfIndex(index_slice, indices.componentType, i) >= acc.count) {
      return InvalidGltfError("sparse index out of range");
    }
  }
  return GetGltfAccessorSlice(model, SparseValues(acc)).status();
}

// Accessors are non-empty and lie inside their bufferView.
absl::Status CheckAccessors(const tinygltf::Model& model) {
  for (size_t a = 0; a < model.accessors.size(); ++a) {
    const tinygltf::Accessor& acc = model.accessors[a];
    if (acc.count == 0) {
      return InvalidGltfError("empty accessor");
    }
    if (acc.sparse.isSparse) {
      INTR_RETURN_IF_ERROR(CheckSparseAccessor(model, acc));
    } else {
      INTR_RETURN_IF_ERROR(
          GetGltfAccessorSlice(model, static_cast<int>(a)).status());
    }
  }
  return absl::OkStatus();
}

// The float VEC3s in `s` are finite.
absl::Status CheckFinitePositions(const GltfAccessorSlice& s) {
  for (size_t v = 0; v < s.count; ++v) {
    for (int k = 0; k < kGltfPositionComponents; ++k) {
      if (!std::isfinite(ReadGltfRawComponent(
              s.data + v * s.stride, TINYGLTF_COMPONENT_TYPE_FLOAT, k))) {
        return InvalidGltfError("non-finite POSITION value");
      }
    }
  }
  return absl::OkStatus();
}

// POSITION is a float or 8/16-bit integer VEC3 with finite bounds and values.
absl::Status CheckPositions(const tinygltf::Model& model, int accessor) {
  const tinygltf::Accessor& pos = model.accessors[accessor];
  if (pos.type != TINYGLTF_TYPE_VEC3) {
    return InvalidGltfError("POSITION is not a VEC3");
  }
  // Bounds may be missing (exporters often omit them and the writer
  // recomputes them), but if present they must have one value per component.
  for (const std::vector<double>* bounds : {&pos.minValues, &pos.maxValues}) {
    if (!bounds->empty() && bounds->size() != kGltfPositionComponents) {
      return InvalidGltfError(
          "POSITION bounds must have one value per component");
    }
    if (!std::ranges::all_of(*bounds,
                             [](double v) { return std::isfinite(v); })) {
      return InvalidGltfError("non-finite POSITION bounds");
    }
  }
  // glTF allows 8/16-bit integer positions with KHR_mesh_quantization; they
  // are always finite.
  switch (pos.componentType) {
    case TINYGLTF_COMPONENT_TYPE_FLOAT:
      break;
    case TINYGLTF_COMPONENT_TYPE_BYTE:
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
    case TINYGLTF_COMPONENT_TYPE_SHORT:
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
      return absl::OkStatus();
    default:
      return InvalidGltfError("unsupported POSITION component type");
  }
  // Checks the base data and, for a sparse accessor, the replaced values.
  if (!pos.sparse.isSparse || pos.bufferView >= 0) {
    INTR_ASSIGN_OR_RETURN(const GltfAccessorSlice base,
                          GetGltfAccessorSlice(model, pos));
    INTR_RETURN_IF_ERROR(CheckFinitePositions(base));
  }
  if (pos.sparse.isSparse) {
    INTR_ASSIGN_OR_RETURN(const GltfAccessorSlice values,
                          GetGltfAccessorSlice(model, SparseValues(pos)));
    INTR_RETURN_IF_ERROR(CheckFinitePositions(values));
  }
  return absl::OkStatus();
}

// The primitive has a POSITION; its attributes and morph targets exist and
// have the same vertex count; POSITION is finite; and its indices are unsigned
// integers below the vertex count and the largest value of their type.
absl::Status CheckPrimitive(const tinygltf::Model& model,
                            const tinygltf::Primitive& prim) {
  size_t vertex_count = 0;
  for (const auto& [name, accessor] : prim.attributes) {
    if (!IsAccessor(model, accessor)) {
      return InvalidGltfError(absl::StrCat("missing ", name, " accessor"));
    }
    const size_t count = model.accessors[accessor].count;
    if (vertex_count != 0 && count != vertex_count) {
      return InvalidGltfError("attributes have different vertex counts");
    }
    vertex_count = count;
  }
  const auto position = prim.attributes.find("POSITION");
  if (position == prim.attributes.end()) {
    return InvalidGltfError("primitive has no POSITION");
  }
  INTR_RETURN_IF_ERROR(CheckPositions(model, position->second));
  for (const auto& target : prim.targets) {
    for (const auto& [name, accessor] : target) {
      if (!IsAccessor(model, accessor)) {
        return InvalidGltfError(
            absl::StrCat("missing morph target ", name, " accessor"));
      }
      if (model.accessors[accessor].count != vertex_count) {
        return InvalidGltfError("morph target has a different vertex count");
      }
    }
  }
  if (prim.indices < 0) {
    return absl::OkStatus();
  }
  if (!IsAccessor(model, prim.indices)) {
    return InvalidGltfError("missing indices accessor");
  }
  const tinygltf::Accessor& indices = model.accessors[prim.indices];
  if (!IsGltfIndexAccessor(indices) || indices.sparse.isSparse) {
    return InvalidGltfError("unsupported index type");
  }
  INTR_ASSIGN_OR_RETURN(const GltfAccessorSlice s,
                        GetGltfAccessorSlice(model, prim.indices));
  const uint32_t restart = PrimitiveRestartIndex(indices.componentType);
  for (size_t i = 0; i < s.count; ++i) {
    const uint32_t index = ReadGltfIndex(s, indices.componentType, i);
    if (index >= vertex_count) {
      return InvalidGltfError("index out of range");
    }
    if (index == restart) {
      return InvalidGltfError("index is the largest value of its type");
    }
  }
  return absl::OkStatus();
}

// Nodes refer to existing meshes and skins, and a skin or morph weights
// require a mesh.
absl::Status CheckNodes(const tinygltf::Model& model) {
  for (const tinygltf::Node& node : model.nodes) {
    if (node.mesh != kGltfNoIndex &&
        !IsInRange(node.mesh, model.meshes.size())) {
      return InvalidGltfError("missing node mesh");
    }
    if (node.skin != kGltfNoIndex &&
        !IsInRange(node.skin, model.skins.size())) {
      return InvalidGltfError("missing node skin");
    }
    if ((node.skin >= 0 || !node.weights.empty()) && node.mesh < 0) {
      return InvalidGltfError("skin or weights on a node without a mesh");
    }
  }
  return absl::OkStatus();
}

// Skins, animation samplers and EXT_mesh_gpu_instancing nodes refer to
// existing accessors.
absl::Status CheckOtherAccessorReferences(const tinygltf::Model& model) {
  for (const tinygltf::Skin& skin : model.skins) {
    if (skin.inverseBindMatrices != kGltfNoIndex &&
        !IsAccessor(model, skin.inverseBindMatrices)) {
      return InvalidGltfError("missing skin inverseBindMatrices accessor");
    }
  }
  for (const tinygltf::Animation& animation : model.animations) {
    for (const tinygltf::AnimationSampler& sampler : animation.samplers) {
      if (!IsAccessor(model, sampler.input) ||
          !IsAccessor(model, sampler.output)) {
        return InvalidGltfError("missing animation sampler accessor");
      }
    }
  }
  for (const tinygltf::Node& node : model.nodes) {
    const auto ext = node.extensions.find("EXT_mesh_gpu_instancing");
    if (ext == node.extensions.end()) {
      continue;
    }
    if (!ext->second.IsObject() || !ext->second.Get("attributes").IsObject()) {
      return InvalidGltfError("EXT_mesh_gpu_instancing without attributes");
    }
    for (const auto& [name, value] :
         ext->second.Get("attributes").Get<tinygltf::Value::Object>()) {
      if (!value.IsInt() || !IsAccessor(model, value.GetNumberAsInt())) {
        return InvalidGltfError(absl::StrCat("missing EXT_mesh_gpu_instancing ",
                                             name, " accessor"));
      }
    }
  }
  return absl::OkStatus();
}

}  // namespace

absl::Status ValidateGltfBuffers(const tinygltf::Model& model) {
  INTR_RETURN_IF_ERROR(CheckBuffersAndViews(model));
  INTR_RETURN_IF_ERROR(CheckAccessors(model));
  for (const tinygltf::Mesh& mesh : model.meshes) {
    for (const tinygltf::Primitive& prim : mesh.primitives) {
      INTR_RETURN_IF_ERROR(CheckPrimitive(model, prim));
    }
  }
  INTR_RETURN_IF_ERROR(CheckNodes(model));
  return CheckOtherAccessorReferences(model);
}

}  // namespace intrinsic::geo
