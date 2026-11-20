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

#include "intrinsic/geometry/gltf/gltf_buffer_writer.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "intrinsic/geometry/gltf/gltf_accessor.h"
#include "intrinsic/util/status/status_macros.h"
#include "tiny_gltf.h"

// The output layout follows meshoptimizer's gltfpack writer
// (https://github.com/zeux/meshoptimizer/tree/master/gltf, write.cpp and
// stream.cpp): 4-byte aligned buffer views, vertex strides padded to 4 bytes,
// 16-bit indices when they fit, and recomputed POSITION min/max.

namespace intrinsic::geo {
namespace {

// glTF starts bufferViews and vertex elements on 4-byte boundaries.
constexpr size_t kGltfAlignment = 4;

size_t AlignToGltf(size_t n) {
  return (n + kGltfAlignment - 1) / kGltfAlignment * kGltfAlignment;
}

// Sets `acc`'s min/max to the bounds of the first 3 components of the `count`
// elements at `data` with stride `stride`. Returns InvalidArgumentError for
// non-finite values, which std::min/std::max would silently skip.
absl::Status SetPositionBounds(tinygltf::Accessor& acc,
                               const unsigned char* data, size_t stride,
                               size_t count) {
  acc.minValues.assign(kGltfPositionComponents,
                       std::numeric_limits<double>::infinity());
  acc.maxValues.assign(kGltfPositionComponents,
                       -std::numeric_limits<double>::infinity());
  for (size_t v = 0; v < count; ++v) {
    for (int k = 0; k < kGltfPositionComponents; ++k) {
      const double value =
          ReadGltfRawComponent(data + v * stride, acc.componentType, k);
      if (!std::isfinite(value)) {
        return absl::InvalidArgumentError("Non-finite POSITION value.");
      }
      acc.minValues[k] = std::min(acc.minValues[k], value);
      acc.maxValues[k] = std::max(acc.maxValues[k], value);
    }
  }
  return absl::OkStatus();
}

// Returns InvalidArgumentError unless attribute `name` (accessor `acc_idx`)
// can be read and has a vertex `max_source_vertex`.
absl::Status CheckVertexAttribute(const tinygltf::Model& model,
                                  absl::string_view name, int acc_idx,
                                  unsigned int max_source_vertex) {
  INTR_ASSIGN_OR_RETURN(const GltfAccessorSlice s,
                        GetGltfAccessorSlice(model, acc_idx));
  if (max_source_vertex >= s.count) {
    return absl::InvalidArgumentError("Source vertex out of range.");
  }
  if (name == "POSITION" &&
      model.accessors[acc_idx].type != TINYGLTF_TYPE_VEC3) {
    return absl::InvalidArgumentError("POSITION must be VEC3.");
  }
  return absl::OkStatus();
}

// Appends a copy of attribute `name` (accessor `acc_idx`, which passed
// CheckVertexAttribute) holding only `source_vertices`, keeping its component
// type, and returns the new accessor's index.
absl::StatusOr<int> CopyVertexAttribute(
    tinygltf::Model& model, absl::string_view name, int acc_idx,
    int buffer_index, absl::Span<const unsigned int> source_vertices) {
  INTR_ASSIGN_OR_RETURN(const GltfAccessorSlice s,
                        GetGltfAccessorSlice(model, acc_idx));
  // Vertex strides must be multiples of 4, so e.g. 3-byte RGB colors get one
  // padding byte (gltfpack writeVertexStream).
  const size_t stride = AlignToGltf(s.elem_size);
  std::vector<unsigned char> bytes(source_vertices.size() * stride, 0);
  for (size_t i = 0; i < source_vertices.size(); ++i) {
    std::memcpy(&bytes[i * stride], s.data + source_vertices[i] * s.stride,
                s.elem_size);
  }
  tinygltf::Accessor acc = model.accessors[acc_idx];
  acc.minValues.clear();
  acc.maxValues.clear();
  if (name == "POSITION" && !source_vertices.empty()) {
    INTR_RETURN_IF_ERROR(
        SetPositionBounds(acc, bytes.data(), stride, source_vertices.size()));
  }
  INTR_ASSIGN_OR_RETURN(
      acc.bufferView,
      AppendGltfBufferView(model, buffer_index, bytes.data(), bytes.size(),
                           TINYGLTF_TARGET_ARRAY_BUFFER,
                           stride == s.elem_size ? 0 : stride));
  acc.byteOffset = 0;
  acc.count = source_vertices.size();
  model.accessors.push_back(std::move(acc));
  return static_cast<int>(model.accessors.size() - 1);
}

// Appends `indices` as an index accessor and returns its index, as 16-bit
// indices when they fit (gltfpack writeIndexStream).
absl::StatusOr<int> WriteIndices(tinygltf::Model& model, int buffer_index,
                                 absl::Span<const unsigned int> indices) {
  tinygltf::Accessor acc;
  acc.type = TINYGLTF_TYPE_SCALAR;
  acc.count = indices.size();
  const unsigned int max_index =
      indices.empty() ? 0 : *std::ranges::max_element(indices);
  // 0xFFFF is the 16-bit "primitive restart" value, so it can't be an index.
  constexpr unsigned int kPrimitiveRestart16 =
      std::numeric_limits<uint16_t>::max();
  if (max_index < kPrimitiveRestart16) {
    const std::vector<uint16_t> indices16(indices.begin(), indices.end());
    acc.componentType = TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT;
    INTR_ASSIGN_OR_RETURN(
        acc.bufferView,
        AppendGltfBufferView(model, buffer_index, indices16.data(),
                             indices16.size() * sizeof(uint16_t),
                             TINYGLTF_TARGET_ELEMENT_ARRAY_BUFFER));
  } else {
    acc.componentType = TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT;
    INTR_ASSIGN_OR_RETURN(
        acc.bufferView,
        AppendGltfBufferView(model, buffer_index, indices.data(),
                             indices.size() * sizeof(unsigned int),
                             TINYGLTF_TARGET_ELEMENT_ARRAY_BUFFER));
  }
  model.accessors.push_back(std::move(acc));
  return static_cast<int>(model.accessors.size() - 1);
}

// Removes meshes without primitives and updates the nodes that used them. A
// node that loses its mesh also loses its skin and morph weights, which glTF
// only allows with a mesh.
void RemoveEmptyMeshes(tinygltf::Model& model) {
  std::vector<tinygltf::Mesh> kept;
  std::vector<int> new_index(model.meshes.size(), kGltfNoIndex);
  for (size_t m = 0; m < model.meshes.size(); ++m) {
    if (!model.meshes[m].primitives.empty()) {
      new_index[m] = static_cast<int>(kept.size());
      kept.push_back(std::move(model.meshes[m]));
    }
  }
  model.meshes = std::move(kept);
  for (tinygltf::Node& node : model.nodes) {
    if (node.mesh < 0) {
      continue;
    }
    node.mesh = static_cast<size_t>(node.mesh) < new_index.size()
                    ? new_index[node.mesh]
                    : kGltfNoIndex;
    if (node.mesh < 0) {
      node.skin = kGltfNoIndex;
      node.weights.clear();
    }
  }
}

// Keeps only the accessors used by a mesh primitive (including morph
// targets), skin, animation or EXT_mesh_gpu_instancing node, and renumbers the
// references to them.
void RemoveUnusedAccessors(tinygltf::Model& model) {
  std::vector<tinygltf::Accessor> kept;
  std::vector<int> new_index(model.accessors.size(), kGltfNoIndex);
  const auto keep = [&kept, &new_index, &model](int& idx) {
    if (idx < 0 || static_cast<size_t>(idx) >= new_index.size()) {
      return;
    }
    if (new_index[idx] < 0) {
      new_index[idx] = static_cast<int>(kept.size());
      kept.push_back(std::move(model.accessors[idx]));
    }
    idx = new_index[idx];
  };
  for (tinygltf::Mesh& mesh : model.meshes) {
    for (tinygltf::Primitive& prim : mesh.primitives) {
      keep(prim.indices);
      for (auto& [_, accessor] : prim.attributes) {
        keep(accessor);
      }
      for (auto& target : prim.targets) {
        for (auto& [_, accessor] : target) {
          keep(accessor);
        }
      }
    }
  }
  for (tinygltf::Skin& skin : model.skins) {
    keep(skin.inverseBindMatrices);
  }
  for (tinygltf::Animation& animation : model.animations) {
    for (tinygltf::AnimationSampler& sampler : animation.samplers) {
      keep(sampler.input);
      keep(sampler.output);
    }
  }
  for (tinygltf::Node& node : model.nodes) {
    const auto ext = node.extensions.find("EXT_mesh_gpu_instancing");
    if (ext == node.extensions.end() || !ext->second.IsObject()) {
      continue;
    }
    auto& ext_obj = ext->second.Get<tinygltf::Value::Object>();
    const auto attrs = ext_obj.find("attributes");
    if (attrs == ext_obj.end() || !attrs->second.IsObject()) {
      continue;
    }
    for (auto& [_, value] : attrs->second.Get<tinygltf::Value::Object>()) {
      if (value.IsInt()) {
        int accessor = value.GetNumberAsInt();
        keep(accessor);
        value = tinygltf::Value(accessor);
      }
    }
  }
  model.accessors = std::move(kept);
}

// Copies the bufferViews used by accessors (including sparse data) and images
// into a new buffer 0, and renumbers the references to them.
absl::Status RepackBufferViews(tinygltf::Model& model) {
  std::vector<bool> used(model.bufferViews.size(), false);
  const auto mark = [&used](int view) {
    if (view >= 0 && static_cast<size_t>(view) < used.size()) {
      used[view] = true;
    }
  };
  for (const tinygltf::Accessor& acc : model.accessors) {
    mark(acc.bufferView);
    if (acc.sparse.isSparse) {
      mark(acc.sparse.indices.bufferView);
      mark(acc.sparse.values.bufferView);
    }
  }
  for (const tinygltf::Image& image : model.images) {
    mark(image.bufferView);
  }
  // Check the kept views before moving anything, so the copies below stay
  // inside their buffers.
  for (size_t i = 0; i < model.bufferViews.size(); ++i) {
    if (!used[i]) {
      continue;
    }
    const tinygltf::BufferView& view = model.bufferViews[i];
    if (view.buffer < 0 ||
        static_cast<size_t>(view.buffer) >= model.buffers.size()) {
      return absl::InvalidArgumentError("bufferView has an invalid buffer.");
    }
    const size_t size = model.buffers[view.buffer].data.size();
    if (view.byteOffset > size || view.byteLength > size - view.byteOffset) {
      return absl::InvalidArgumentError("bufferView out of bounds.");
    }
  }

  const std::vector<tinygltf::Buffer> old_buffers = std::move(model.buffers);
  const std::vector<tinygltf::BufferView> old_views =
      std::move(model.bufferViews);
  model.buffers.assign(1, tinygltf::Buffer{});
  model.bufferViews.clear();
  std::vector<int> new_index(old_views.size(), kGltfNoIndex);
  for (size_t i = 0; i < old_views.size(); ++i) {
    if (!used[i]) {
      continue;
    }
    const tinygltf::BufferView& view = old_views[i];
    INTR_ASSIGN_OR_RETURN(
        new_index[i],
        AppendGltfBufferView(
            model, 0, old_buffers[view.buffer].data.data() + view.byteOffset,
            view.byteLength, view.target, view.byteStride));
    model.bufferViews.back().name = view.name;
  }
  const auto renumber = [&new_index](int& view) {
    view = view >= 0 && static_cast<size_t>(view) < new_index.size()
               ? new_index[view]
               : kGltfNoIndex;
  };
  for (tinygltf::Accessor& acc : model.accessors) {
    renumber(acc.bufferView);
    if (acc.sparse.isSparse) {
      renumber(acc.sparse.indices.bufferView);
      renumber(acc.sparse.values.bufferView);
    }
  }
  for (tinygltf::Image& image : model.images) {
    renumber(image.bufferView);
  }
  return absl::OkStatus();
}

}  // namespace

absl::StatusOr<int> AppendGltfBufferView(tinygltf::Model& model,
                                         int buffer_index, const void* data,
                                         size_t byte_length, int target,
                                         size_t byte_stride) {
  if (buffer_index < 0 ||
      static_cast<size_t>(buffer_index) >= model.buffers.size()) {
    return absl::InvalidArgumentError("Invalid buffer index.");
  }
  // Views start and end on 4-byte boundaries, as glTF requires for accessors.
  std::vector<unsigned char>& bin = model.buffers[buffer_index].data;
  // Growing `bin` may move it, so `data` must not point into it.
  const auto* bytes = static_cast<const unsigned char*>(data);
  if (byte_length > 0 && !bin.empty() &&
      std::less<>()(bytes, bin.data() + bin.size()) &&
      std::less<>()(bin.data(), bytes + byte_length)) {
    return absl::InvalidArgumentError(
        "data must not point into the buffer it is appended to.");
  }
  // Reject sizes whose padded total doesn't fit, so the math below can't wrap.
  const size_t max_size = bin.max_size();
  if (bin.size() > max_size - kGltfAlignment ||
      byte_length > max_size - kGltfAlignment) {
    return absl::InvalidArgumentError("glTF buffer too large.");
  }
  const size_t offset = AlignToGltf(bin.size());
  const size_t padded_length = AlignToGltf(byte_length);
  if (padded_length > max_size - offset) {
    return absl::InvalidArgumentError("glTF buffer too large.");
  }
  bin.resize(offset + padded_length, 0);
  if (byte_length > 0) {
    std::memcpy(bin.data() + offset, data, byte_length);
  }
  tinygltf::BufferView view;
  view.buffer = buffer_index;
  view.byteOffset = offset;
  view.byteLength = byte_length;
  view.byteStride = byte_stride;
  view.target = target;
  model.bufferViews.push_back(std::move(view));
  return static_cast<int>(model.bufferViews.size() - 1);
}

absl::Status WriteRemappedPrimitiveBuffers(
    tinygltf::Model& model, tinygltf::Primitive& prim, int buffer_index,
    absl::Span<const unsigned int> source_vertices,
    absl::Span<const unsigned int> indices) {
  if (buffer_index < 0 ||
      static_cast<size_t>(buffer_index) >= model.buffers.size()) {
    return absl::InvalidArgumentError("Invalid buffer index.");
  }
  if (!IsGltfTriangleList(prim)) {
    return absl::InvalidArgumentError("Primitive is not a triangle list.");
  }
  // Morph targets would keep the old vertex count.
  if (!prim.targets.empty()) {
    return absl::InvalidArgumentError("Primitive has morph targets.");
  }
  if (indices.empty() || indices.size() % kGltfTriangleSize != 0) {
    return absl::InvalidArgumentError(
        "Triangle index count must be a positive multiple of 3.");
  }
  if (*std::ranges::max_element(indices) >= source_vertices.size()) {
    return absl::InvalidArgumentError("Index out of range.");
  }
  // `source_vertices` is non-empty, since it has an element for every index.
  const unsigned int max_source_vertex =
      *std::ranges::max_element(source_vertices);
  // Check every attribute before changing `model` or `prim`.
  for (const auto& [name, accessor] : prim.attributes) {
    INTR_RETURN_IF_ERROR(
        CheckVertexAttribute(model, name, accessor, max_source_vertex));
  }
  // The old accessors are left in place; CompactGlbBuffers removes them.
  // `prim` is only updated once all the new data is written.
  std::map<std::string, int> attributes;
  for (const auto& [name, accessor] : prim.attributes) {
    INTR_ASSIGN_OR_RETURN(attributes[name],
                          CopyVertexAttribute(model, name, accessor,
                                              buffer_index, source_vertices));
  }
  INTR_ASSIGN_OR_RETURN(const int new_indices,
                        WriteIndices(model, buffer_index, indices));
  prim.attributes = std::move(attributes);
  prim.indices = new_indices;
  prim.mode = TINYGLTF_MODE_TRIANGLES;
  return absl::OkStatus();
}

absl::Status CompactGlbBuffers(tinygltf::Model& model) {
  RemoveEmptyMeshes(model);
  RemoveUnusedAccessors(model);
  return RepackBufferViews(model);
}
}  // namespace intrinsic::geo
