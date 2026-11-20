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

#include "intrinsic/geometry/internal/mesh_simplification/appearance/weld_vertices.h"

#include <cmath>
#include <cstddef>
#include <optional>
#include <vector>

#include "absl/strings/match.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "intrinsic/geometry/internal/mesh_simplification/appearance/remapped_triangles.h"
#include "meshoptimizer.h"

namespace intrinsic::geo {
namespace {

// gltfpack's reindexTolerance values.
constexpr float kDirectionTolerance = 2e-4f;  // Normals and tangents.
constexpr float kUvTolerance = 5e-5f;
// Half a step of an 8-bit value, for colors and skin weights.
constexpr float kUnitValueTolerance = 0.5f / 256.0f;

}  // namespace

float GltfWeldTolerance(absl::string_view name) {
  if (absl::StartsWith(name, "NORMAL") || absl::StartsWith(name, "TANGENT")) {
    return kDirectionTolerance;
  }
  if (absl::StartsWith(name, "TEXCOORD_")) {
    return kUvTolerance;
  }
  if (absl::StartsWith(name, "COLOR_") || absl::StartsWith(name, "WEIGHTS_")) {
    return kUnitValueTolerance;
  }
  return 0.0f;
}

RemappedTriangles WeldVertices(
    absl::Span<const float> positions, absl::Span<const unsigned int> indices,
    const std::optional<std::vector<WeldAttribute>>& attributes) {
  // meshopt asserts on an empty index buffer.
  if (positions.empty() || indices.size() < kTriangleSize) {
    return {};
  }
  const size_t vertex_count = positions.size() / kPositionSize;
  // meshopt calls this for vertices with equal positions.
  const auto attributes_match = [&](unsigned int a, unsigned int b) {
    if (!attributes.has_value()) {
      return false;
    }
    for (const WeldAttribute& attribute : *attributes) {
      for (size_t c = 0; c < kWeldAttributeSize; ++c) {
        if (std::fabs(attribute.values[a * kWeldAttributeSize + c] -
                      attribute.values[b * kWeldAttributeSize + c]) >
            attribute.tolerance) {
          return false;
        }
      }
    }
    return true;
  };
  std::vector<unsigned int> remap(vertex_count);
  const size_t welded_count = meshopt_generateVertexRemapCustom(
      remap.data(), indices.data(), indices.size(), positions.data(),
      vertex_count, kPositionSize * sizeof(float), attributes_match);

  RemappedTriangles mesh;
  mesh.source.assign(welded_count, kNoVertex);
  for (size_t i = 0; i < vertex_count; ++i) {
    if (remap[i] != kNoVertex && mesh.source[remap[i]] == kNoVertex) {
      mesh.source[remap[i]] = static_cast<unsigned int>(i);
    }
  }
  mesh.indices.resize(indices.size());
  meshopt_remapIndexBuffer(mesh.indices.data(), indices.data(), indices.size(),
                           remap.data());
  return mesh;
}

}  // namespace intrinsic::geo
