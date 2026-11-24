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

#include "intrinsic/geometry/internal/mesh_simplification/appearance/glb_appearance_simplifier.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "intrinsic/geometry/gltf/gltf_accessor.h"
#include "intrinsic/geometry/gltf/gltf_buffer_reader.h"
#include "intrinsic/geometry/gltf/gltf_buffer_writer.h"
#include "intrinsic/geometry/gltf/gltf_constants.h"
#include "intrinsic/geometry/gltf/gltf_io.h"
#include "intrinsic/geometry/gltf/gltf_validation.h"
#include "intrinsic/geometry/gltf/unpack_renderable.h"
#include "intrinsic/geometry/internal/mesh_simplification/appearance/remapped_triangles.h"
#include "intrinsic/geometry/internal/mesh_simplification/appearance/uv_seams.h"
#include "intrinsic/geometry/internal/mesh_simplification/appearance/weld_vertices.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/thread/stop_token.h"
#include "meshoptimizer.h"
#include "tiny_gltf.h"

namespace intrinsic::geo {
namespace {

// A glTF primitive is one draw call: triangles that share a material and a
// vertex layout. Each primitive is simplified on its own:
// 1. Read it and weld vertices that match (PreparePrimitiveMesh, WeldVertices).
// 2. Give vertices on mirrored UV seams a copy (SplitMirroredUvSeams) and lock
//    UV seams (ProtectUvSeams).
// 3. Simplify to the target triangle count, then merge the seam copies back
//    (SimplifyIndices).
// 4. Reorder for rendering and write the new buffers (WriteOptimizedPrimitive).
//
// This follows how meshoptimizer's gltfpack simplifies a triangle mesh
// (processMesh and simplifyMesh in
// https://github.com/zeux/meshoptimizer/blob/master/gltf/mesh.cpp), run with
// -slb (lock borders) and -sp (permissive, UV seams protected).

// Returns every attribute of `primitive` but POSITION, to compare when
// welding, so no data is lost by welding. Returns std::nullopt (weld nothing)
// if one can't be read as floats, e.g. a custom UNSIGNED_INT attribute.
std::optional<std::vector<WeldAttribute>> ReadWeldAttributes(
    const tinygltf::Model& model, const tinygltf::Primitive& primitive) {
  std::vector<WeldAttribute> attributes;
  for (const std::string& name : std::views::keys(primitive.attributes)) {
    if (name == kGltfPositionAttribute) {
      continue;
    }
    absl::StatusOr<std::vector<float>> values =
        ReadGltfAttributeFloats(model, primitive, name, kWeldAttributeSize);
    if (!values.ok()) {
      return std::nullopt;
    }
    attributes.push_back({*std::move(values), GltfWeldTolerance(name)});
  }
  return attributes;
}

// Attributes for meshopt_simplifyWithAttributes, with gltfpack's weights
// (simplifyAttributes): the normal, and the color's RGB premultiplied by alpha
// so color changes on transparent vertices count less. UVs are left out, like
// gltfpack's UV weight of 0 without -sv: the seam locks protect the texture
// instead.
constexpr size_t kNormalOffset = 0;
constexpr size_t kColorOffset = kNormalOffset + kNormalSize;
constexpr size_t kColorRgbSize = 3;
constexpr size_t kAlphaIndex = 3;  // In an RGBA color.
constexpr size_t kAttrComponents = kColorOffset + kColorRgbSize;
constexpr float kNormalWeight = 0.5f;
constexpr float kColorWeight = 1.0f;
static_assert(kNormalOffset + kNormalSize <= kAttrComponents);
static_assert(kColorOffset + kColorRgbSize <= kAttrComponents);
static_assert(kAlphaIndex < kColorSize);

struct SimplificationAttributes {
  float weights[kAttrComponents] = {};
  std::vector<float> values;
};

// `normals` and `colors` have kNormalSize and kColorSize floats per vertex, or
// are empty.
SimplificationAttributes BuildSimplificationAttributes(
    size_t vertex_count, absl::Span<const float> normals,
    absl::Span<const float> colors) {
  SimplificationAttributes attributes;
  attributes.values.assign(vertex_count * kAttrComponents, 0.0f);
  if (!normals.empty()) {
    for (size_t i = 0; i < vertex_count; ++i) {
      for (size_t c = 0; c < kNormalSize; ++c) {
        attributes.values[i * kAttrComponents + kNormalOffset + c] =
            normals[i * kNormalSize + c];
      }
    }
    std::fill_n(attributes.weights + kNormalOffset, kNormalSize, kNormalWeight);
  }
  if (!colors.empty()) {
    for (size_t i = 0; i < vertex_count; ++i) {
      const float* color = &colors[i * kColorSize];
      for (size_t c = 0; c < kColorRgbSize; ++c) {
        attributes.values[i * kAttrComponents + kColorOffset + c] =
            color[c] * color[kAlphaIndex];
      }
    }
    std::fill_n(attributes.weights + kColorOffset, kColorRgbSize, kColorWeight);
  }
  return attributes;
}

// A primitive's welded mesh, ready for meshopt.
struct PreparedMesh {
  RemappedTriangles mesh;
  // kPositionSize floats per vertex of `mesh`.
  std::vector<float> positions;
  // Vertices before SplitMirroredUvSeams added copies; the output vertices
  // only come from these.
  size_t presplit_vertex_count = 0;
  // See SplitMirroredUvSeams.
  std::vector<unsigned int> copy_of;
  // Per-vertex meshopt_SimplifyVertex_* flags.
  std::vector<unsigned char> locks;
  SimplificationAttributes attributes;
};

// Reads and welds `primitive`'s triangles and sets up the UV seam locks and
// attributes for meshopt. The mesh only holds this primitive's vertices, even
// when several primitives share one vertex buffer. `model` must have passed
// ValidateGltfBuffers, so the primitive has triangles.
absl::StatusOr<PreparedMesh> PreparePrimitiveMesh(
    const tinygltf::Model& model, const tinygltf::Primitive& primitive) {
  INTR_ASSIGN_OR_RETURN(
      const std::vector<float> positions,
      ReadGltfAttributeFloats(model, primitive, kGltfPositionAttribute,
                              kPositionSize));
  INTR_ASSIGN_OR_RETURN(const std::vector<unsigned int> indices,
                        ReadGltfTriangleIndices(model, primitive));
  PreparedMesh prepared;
  prepared.mesh =
      WeldVertices(positions, indices, ReadWeldAttributes(model, primitive));
  INTR_ASSIGN_OR_RETURN(
      const std::vector<float> normals,
      ReadGltfAttributeFloats(model, primitive, kGltfNormalAttribute,
                              kNormalSize));
  INTR_ASSIGN_OR_RETURN(const std::vector<float> colors,
                        ReadGltfAttributeFloats(
                            model, primitive, kGltfColorAttribute, kColorSize));
  INTR_ASSIGN_OR_RETURN(const std::vector<float> uvs,
                        ReadGltfAttributeFloats(
                            model, primitive, kGltfTexcoordAttribute, kUvSize));

  prepared.presplit_vertex_count = prepared.mesh.vertex_count();
  prepared.copy_of = SplitMirroredUvSeams(uvs, prepared.mesh);
  const std::vector<unsigned int>& source = prepared.mesh.source;
  prepared.positions = GatherVertices(positions, kPositionSize, source);
  prepared.locks =
      ProtectUvSeams(prepared.positions, GatherVertices(uvs, kUvSize, source),
                     prepared.presplit_vertex_count);
  prepared.attributes = BuildSimplificationAttributes(
      prepared.mesh.vertex_count(),
      GatherVertices(normals, kNormalSize, source),
      GatherVertices(colors, kColorSize, source));
  return prepared;
}

// Runs meshopt on `prepared` until it has `target_triangle_ratio` of its
// triangles, and returns the simplified triangle indices into the pre-split
// vertices. `regularize` is for skinned meshes.
std::vector<unsigned int> SimplifyIndices(const PreparedMesh& prepared,
                                          double target_triangle_ratio,
                                          bool regularize) {
  const RemappedTriangles& mesh = prepared.mesh;
  // Only the triangle count decides when to stop, so meshopt gets the largest
  // relative error (100% of the mesh's extent).
  constexpr float kMaxRelativeError = 1.0f;
  // Every primitive keeps a few triangles: CAD imports have one small
  // primitive per surface, and with no floor a small ratio collapses all of
  // them and nothing is left.
  constexpr size_t kMinTriangles = 4;
  const size_t triangles = mesh.indices.size() / kTriangleSize;
  const size_t target_triangles = std::max(
      kMinTriangles, static_cast<size_t>(static_cast<double>(triangles) *
                                         target_triangle_ratio));
  const size_t target_index_count =
      std::min(mesh.indices.size(), target_triangles * kTriangleSize);
  // Flags:
  // - LockBorder (gltfpack -slb): open edges stay fixed, so no gaps open
  //   between primitives, which are simplified one at a time.
  // - Permissive (gltfpack -sp): allows collapsing across normal or color
  //   seams when the values are close; UV seams stay protected by `locks`.
  // - ErrorClamped: keeps the normal and color error on the same scale as the
  //   position error, so a few vertices with large attribute changes don't
  //   dominate the order in which triangles are removed.
  // - Regularize: for skinned meshes, more even triangles deform better.
  unsigned int options = meshopt_SimplifyLockBorder |
                         meshopt_SimplifyPermissive |
                         meshopt_SimplifyErrorClamped;
  if (regularize) {
    options |= meshopt_SimplifyRegularize;
  }
  std::vector<unsigned int> simplified_indices(mesh.indices.size());
  simplified_indices.resize(meshopt_simplifyWithAttributes(
      simplified_indices.data(), mesh.indices.data(), mesh.indices.size(),
      prepared.positions.data(), mesh.vertex_count(),
      kPositionSize * sizeof(float), prepared.attributes.values.data(),
      kAttrComponents * sizeof(float), prepared.attributes.weights,
      kAttrComponents, prepared.locks.data(), target_index_count,
      kMaxRelativeError, options, /*result_error=*/nullptr));

  // Merge the mirror-seam copies back into their originals.
  if (!prepared.copy_of.empty()) {
    meshopt_remapIndexBuffer(
        simplified_indices.data(), simplified_indices.data(),
        simplified_indices.size(), prepared.copy_of.data());
  }
  return simplified_indices;
}

// Reorders `indices` and their vertices for faster rendering, like gltfpack's
// optimizeMesh, and writes them to `primitive`. Vertices no triangle uses
// anymore are dropped.
absl::Status WriteOptimizedPrimitive(tinygltf::Model& model,
                                     tinygltf::Primitive& primitive,
                                     int scratch_buffer_index,
                                     const PreparedMesh& prepared,
                                     std::vector<unsigned int> indices) {
  const size_t vertex_count = prepared.presplit_vertex_count;
  meshopt_optimizeVertexCache(indices.data(), indices.data(), indices.size(),
                              vertex_count);
  // Maps each old vertex to its new index, or kNoVertex if no triangle uses it.
  std::vector<unsigned int> fetch_remap(vertex_count);
  const size_t new_vertex_count = meshopt_optimizeVertexFetchRemap(
      fetch_remap.data(), indices.data(), indices.size(), vertex_count);
  meshopt_remapIndexBuffer(indices.data(), indices.data(), indices.size(),
                           fetch_remap.data());
  // For each output vertex, the glTF vertex to copy its data from.
  std::vector<unsigned int> source_vertices(new_vertex_count);
  for (size_t v = 0; v < vertex_count; ++v) {
    if (fetch_remap[v] != kNoVertex) {
      source_vertices[fetch_remap[v]] = prepared.mesh.source[v];
    }
  }
  return WriteRemappedPrimitiveBuffers(model, primitive, scratch_buffer_index,
                                       source_vertices, indices);
}

// Simplifies `primitive` in place to `target_triangle_ratio` of its
// triangles. Returns false if the primitive collapsed to nothing
// and should be dropped. Primitives whose data can't be read (e.g. a sparse
// accessor) are left as they are.
absl::StatusOr<bool> SimplifyTrianglePrimitive(tinygltf::Model& model,
                                               tinygltf::Primitive& primitive,
                                               int scratch_buffer_index,
                                               double target_triangle_ratio) {
  const absl::StatusOr<PreparedMesh> prepared =
      PreparePrimitiveMesh(model, primitive);
  if (!prepared.ok()) {
    return true;
  }
  std::vector<unsigned int> indices = SimplifyIndices(
      *prepared, target_triangle_ratio,
      /*regularize=*/
      primitive.attributes.contains(std::string(kGltfWeightsAttribute)));
  if (indices.empty()) {
    return false;
  }
  INTR_RETURN_IF_ERROR(WriteOptimizedPrimitive(
      model, primitive, scratch_buffer_index, *prepared, std::move(indices)));
  return true;
}

// Points, lines and morph-target primitives are left as is (removing vertices
// would break the morph data).
bool IsSimplifiableTrianglePrimitive(const tinygltf::Primitive& primitive) {
  return IsGltfTriangleList(primitive) && primitive.targets.empty() &&
         primitive.attributes.contains(std::string(kGltfPositionAttribute));
}

// Returns the number of triangles in `model`'s simplifiable primitives.
size_t CountSimplifiableTriangles(const tinygltf::Model& model) {
  size_t triangles = 0;
  for (const tinygltf::Mesh& mesh : model.meshes) {
    for (const tinygltf::Primitive& primitive : mesh.primitives) {
      if (!IsSimplifiableTrianglePrimitive(primitive)) {
        continue;
      }
      const int counted =
          primitive.indices >= 0
              ? primitive.indices
              : primitive.attributes.at(std::string(kGltfPositionAttribute));
      triangles += model.accessors[counted].count / kTriangleSize;
    }
  }
  return triangles;
}

// Simplifies every simplifiable primitive of `model` in place, writing the new
// data to `scratch_buffer_index`, and removes those that end up with no
// triangles. Other primitives are kept as they are.
absl::Status SimplifyAllPrimitives(tinygltf::Model& model,
                                   int scratch_buffer_index,
                                   double target_triangle_ratio,
                                   const StopToken& stop_token) {
  for (tinygltf::Mesh& mesh : model.meshes) {
    std::vector<tinygltf::Primitive> kept_primitives;
    kept_primitives.reserve(mesh.primitives.size());
    for (tinygltf::Primitive& primitive : mesh.primitives) {
      if (stop_token.stop_requested()) {
        return absl::CancelledError("SimplifyGlbPreservingAppearance stopped.");
      }
      bool kept = true;
      if (IsSimplifiableTrianglePrimitive(primitive)) {
        INTR_ASSIGN_OR_RETURN(kept, SimplifyTrianglePrimitive(
                                        model, primitive, scratch_buffer_index,
                                        target_triangle_ratio));
      }
      if (kept) {
        kept_primitives.push_back(std::move(primitive));
      }
    }
    mesh.primitives = std::move(kept_primitives);
  }
  return absl::OkStatus();
}

}  // namespace

absl::StatusOr<std::optional<std::string>> SimplifyGlbPreservingAppearance(
    absl::string_view glb_bytes, double target_triangle_ratio,
    const StopToken& stop_token) {
  if (!(target_triangle_ratio > 0.0 && target_triangle_ratio <= 1.0)) {
    return absl::InvalidArgumentError(
        "target_triangle_ratio must be in (0, 1].");
  }
  // std::nullopt means "keep the original": the GLB can't be unpacked or
  // parsed, it uses glTF data the buffer code doesn't support (e.g. accessors
  // without a bufferView), there is nothing to simplify, the result is not
  // smaller, or nothing would be left.
  //
  // tinygltf cannot read meshopt-compressed GLBs, and the buffer code reads
  // dequantized data, so unpack first.
  const absl::StatusOr<std::string> unpacked_glb =
      UnpackRenderableGlbData(glb_bytes);
  if (!unpacked_glb.ok()) {
    return std::nullopt;
  }
  absl::StatusOr<tinygltf::Model> parsed = ParseGlb(*unpacked_glb);
  if (!parsed.ok() || !ValidateGltfBuffers(*parsed).ok()) {
    return std::nullopt;
  }
  tinygltf::Model model = *std::move(parsed);

  const size_t triangles_before = CountSimplifiableTriangles(model);
  if (triangles_before == 0) {
    return std::nullopt;
  }

  // New vertex and index data goes into a separate buffer; CompactGlbBuffers
  // later merges everything still used into one buffer and drops the rest.
  const int scratch_buffer_index = static_cast<int>(model.buffers.size());
  model.buffers.emplace_back();
  INTR_RETURN_IF_ERROR(SimplifyAllPrimitives(
      model, scratch_buffer_index, target_triangle_ratio, stop_token));
  // Keep the original if every triangle collapsed: other meshes (e.g. points)
  // would otherwise be all that's left.
  const size_t triangles_after = CountSimplifiableTriangles(model);
  if (triangles_after == 0 || triangles_after >= triangles_before) {
    return std::nullopt;
  }

  // The input was validated, so failures from here on are bugs: return an
  // InternalError rather than a corrupt GLB.
  INTR_RETURN_IF_ERROR(CompactGlbBuffers(model))
      .SetCode(absl::StatusCode::kInternal);
  if (model.meshes.empty()) {
    return std::nullopt;
  }
  INTR_RETURN_IF_ERROR(ValidateGltfBuffers(model))
      .SetCode(absl::StatusCode::kInternal);
  INTR_ASSIGN_OR_RETURN(std::string out_glb, SerializeGltfToGlb(model),
                        _.SetCode(absl::StatusCode::kInternal));
  return out_glb;
}

}  // namespace intrinsic::geo
