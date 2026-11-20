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

#ifndef INTRINSIC_GEOMETRY_INTERNAL_MESH_SIMPLIFICATION_APPEARANCE_WELD_VERTICES_H_
#define INTRINSIC_GEOMETRY_INTERNAL_MESH_SIMPLIFICATION_APPEARANCE_WELD_VERTICES_H_

#include <cstddef>
#include <optional>
#include <vector>

#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "intrinsic/geometry/internal/mesh_simplification/appearance/remapped_triangles.h"

namespace intrinsic::geo {

// Floats per vertex in a WeldAttribute (glTF attributes have at most 4).
inline constexpr size_t kWeldAttributeSize = 4;

// A vertex attribute compared when welding, as kWeldAttributeSize floats per
// vertex.
struct WeldAttribute {
  std::vector<float> values;
  // How far apart two vertices' values can be and still be welded.
  float tolerance = 0.0f;
};

// Returns the welding tolerance for glTF attribute `name`, from gltfpack's
// reindexTolerance (without quantization). Attributes it doesn't know, like
// JOINTS_0, must match exactly.
float GltfWeldTolerance(absl::string_view name);

// Merges the vertices of a triangle mesh that have the same position and
// attributes within tolerance, like gltfpack's reindexMesh. Imported meshes
// are often unwelded (e.g. OBJ files converted in the browser have 3 vertices
// per triangle), and the simplifier can't collapse edges between unwelded
// triangles. `positions` has kPositionSize floats per vertex; -0.0 and 0.0
// count as equal.
//
// If `attributes` is std::nullopt, no vertices are merged. Either way, the
// result only has the vertices `indices` use; each one is a copy of the first
// input vertex merged into it. Returns an empty mesh if there are no
// positions or triangles.
RemappedTriangles WeldVertices(
    absl::Span<const float> positions, absl::Span<const unsigned int> indices,
    const std::optional<std::vector<WeldAttribute>>& attributes);

}  // namespace intrinsic::geo

#endif  // INTRINSIC_GEOMETRY_INTERNAL_MESH_SIMPLIFICATION_APPEARANCE_WELD_VERTICES_H_
