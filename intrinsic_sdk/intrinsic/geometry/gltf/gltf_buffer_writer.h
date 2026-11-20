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

#ifndef INTRINSIC_GEOMETRY_GLTF_GLTF_BUFFER_WRITER_H_
#define INTRINSIC_GEOMETRY_GLTF_GLTF_BUFFER_WRITER_H_

// Rewrites the vertex and index data of a tinygltf::Model.
//
// Validate, then process: call ValidateGltfBuffers() (gltf_validation.h) on a
// parsed model before using these functions, and again before serializing an
// edited model. The functions assume a model that passed it. If one returns an
// error, the model is left in an unspecified state and should be discarded.

#include <cstddef>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "tiny_gltf.h"

namespace intrinsic::geo {

// Appends `byte_length` bytes of `data` (padded to 4-byte alignment) to
// `model.buffers[buffer_index]` as a new BufferView and returns its index.
// Returns InvalidArgumentError if `buffer_index` is invalid, `data` points into
// that buffer, or the buffer would grow too large.
absl::StatusOr<int> AppendGltfBufferView(tinygltf::Model& model,
                                         int buffer_index, const void* data,
                                         size_t byte_length, int target,
                                         size_t byte_stride = 0);

// Replaces the vertex attributes and indices of `prim`, a triangle list
// without morph targets, with new BufferViews in `model.buffers[buffer_index]`.
// Output vertex `i` copies input vertex `source_vertices[i]`, and `indices`
// (a non-empty multiple of 3) index the new vertices. Returns
// InvalidArgumentError for out-of-range arguments and primitives that aren't
// triangle lists or have morph targets.
absl::Status WriteRemappedPrimitiveBuffers(
    tinygltf::Model& model, tinygltf::Primitive& prim, int buffer_index,
    absl::Span<const unsigned int> source_vertices,
    absl::Span<const unsigned int> indices);

// Removes empty meshes and the accessors and bufferViews nothing references,
// and packs the rest into `model.buffers[0]`. BufferViews referenced only by
// extensions (such as `KHR_draco_mesh_compression`) are not kept, so such
// models are not supported. Returns InvalidArgumentError if a kept bufferView
// lies outside its buffer.
absl::Status CompactGlbBuffers(tinygltf::Model& model);

}  // namespace intrinsic::geo

#endif  // INTRINSIC_GEOMETRY_GLTF_GLTF_BUFFER_WRITER_H_
