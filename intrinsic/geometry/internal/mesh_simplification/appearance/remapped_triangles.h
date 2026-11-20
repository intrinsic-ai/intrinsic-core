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

#ifndef INTRINSIC_GEOMETRY_INTERNAL_MESH_SIMPLIFICATION_APPEARANCE_REMAPPED_TRIANGLES_H_
#define INTRINSIC_GEOMETRY_INTERNAL_MESH_SIMPLIFICATION_APPEARANCE_REMAPPED_TRIANGLES_H_

#include <cstddef>
#include <vector>

#include "absl/types/span.h"

namespace intrinsic::geo {

// Floats per vertex in the vertex arrays.
inline constexpr size_t kPositionSize = 3;
inline constexpr size_t kNormalSize = 3;
inline constexpr size_t kColorSize = 4;  // RGBA.
inline constexpr size_t kUvSize = 2;
// Indices per triangle.
inline constexpr size_t kTriangleSize = 3;
// Marks "no vertex", as in meshopt remaps for vertices that no triangle uses.
inline constexpr unsigned int kNoVertex = ~0u;

// Triangles over copies of input vertices: vertex `v` is a copy of input
// vertex `source[v]`, so its data is the input data at `source[v]` (see
// GatherVertices).
struct RemappedTriangles {
  std::vector<unsigned int> source;
  std::vector<unsigned int> indices;

  size_t vertex_count() const { return source.size(); }
};

// Returns `values` (`n` floats per input vertex) for the vertices in `source`,
// in order. Returns an empty vector if `values` is empty.
inline std::vector<float> GatherVertices(
    absl::Span<const float> values, size_t n,
    absl::Span<const unsigned int> source) {
  std::vector<float> out;
  if (values.empty()) {
    return out;
  }
  out.reserve(source.size() * n);
  for (const unsigned int s : source) {
    for (size_t c = 0; c < n; ++c) {
      out.push_back(values[s * n + c]);
    }
  }
  return out;
}

}  // namespace intrinsic::geo

#endif  // INTRINSIC_GEOMETRY_INTERNAL_MESH_SIMPLIFICATION_APPEARANCE_REMAPPED_TRIANGLES_H_
