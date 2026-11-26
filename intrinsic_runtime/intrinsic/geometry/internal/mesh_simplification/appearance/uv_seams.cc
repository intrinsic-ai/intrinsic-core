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

#include "intrinsic/geometry/internal/mesh_simplification/appearance/uv_seams.h"

#include <cstddef>
#include <cstdint>
#include <vector>

#include "absl/types/span.h"
#include "intrinsic/geometry/internal/mesh_simplification/appearance/remapped_triangles.h"
#include "meshoptimizer.h"

namespace intrinsic::geo {
namespace {

// UV winding of a triangle, and the union of its triangles' windings for a
// vertex.
enum UvWinding : uint8_t {
  kNoWinding = 0,  // Zero UV area.
  kCounterClockwise = 1,
  kClockwise = 2,
  kBothWindings = kCounterClockwise | kClockwise,
};

}  // namespace

std::vector<unsigned int> SplitMirroredUvSeams(absl::Span<const float> uvs,
                                               RemappedTriangles& mesh) {
  if (uvs.empty()) {
    return {};
  }
  const size_t vertex_count = mesh.vertex_count();
  const size_t triangle_count = mesh.indices.size() / kTriangleSize;
  std::vector<uint8_t> triangle_winding(triangle_count, kNoWinding);
  std::vector<uint8_t> vertex_windings(vertex_count, kNoWinding);
  for (size_t t = 0; t < triangle_count; ++t) {
    const unsigned int* tri = &mesh.indices[t * kTriangleSize];
    const float* a = &uvs[mesh.source[tri[0]] * kUvSize];
    const float* b = &uvs[mesh.source[tri[1]] * kUvSize];
    const float* c = &uvs[mesh.source[tri[2]] * kUvSize];
    const float area =
        (b[0] - a[0]) * (c[1] - a[1]) - (c[0] - a[0]) * (b[1] - a[1]);
    triangle_winding[t] =
        area > 0 ? kCounterClockwise : (area < 0 ? kClockwise : kNoWinding);
    for (size_t k = 0; k < kTriangleSize; ++k) {
      vertex_windings[tri[k]] |= triangle_winding[t];
    }
  }
  std::vector<unsigned int> copy_of(vertex_count);
  std::vector<unsigned int> split(vertex_count);
  for (unsigned int v = 0; v < vertex_count; ++v) {
    copy_of[v] = v;
    if (vertex_windings[v] == kBothWindings) {
      split[v] = static_cast<unsigned int>(mesh.source.size());
      mesh.source.push_back(mesh.source[v]);
      copy_of.push_back(v);
    }
  }
  for (size_t t = 0; t < triangle_count; ++t) {
    if (triangle_winding[t] != kClockwise) {
      continue;
    }
    for (size_t k = 0; k < kTriangleSize; ++k) {
      unsigned int& v = mesh.indices[t * kTriangleSize + k];
      if (vertex_windings[v] == kBothWindings) {
        v = split[v];
      }
    }
  }
  return copy_of;
}

std::vector<unsigned char> ProtectUvSeams(absl::Span<const float> positions,
                                          absl::Span<const float> uvs,
                                          size_t presplit_vertex_count) {
  const size_t vertex_count = positions.size() / kPositionSize;
  std::vector<unsigned char> locks(vertex_count, 0);
  if (uvs.empty()) {
    return locks;
  }
  std::vector<unsigned int> position_remap(vertex_count);
  meshopt_generatePositionRemap(position_remap.data(), positions.data(),
                                vertex_count, kPositionSize * sizeof(float));
  for (size_t i = 0; i < vertex_count; ++i) {
    const unsigned int r = position_remap[i];
    if (r != i && (uvs[i * kUvSize] != uvs[r * kUvSize] ||
                   uvs[i * kUvSize + 1] != uvs[r * kUvSize + 1])) {
      locks[i] |= meshopt_SimplifyVertex_Protect;
    }
  }
  for (size_t i = presplit_vertex_count; i < vertex_count; ++i) {
    locks[i] |= meshopt_SimplifyVertex_Protect;
  }
  return locks;
}

}  // namespace intrinsic::geo
