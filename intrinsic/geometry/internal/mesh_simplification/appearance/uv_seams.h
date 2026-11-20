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

#ifndef INTRINSIC_GEOMETRY_INTERNAL_MESH_SIMPLIFICATION_APPEARANCE_UV_SEAMS_H_
#define INTRINSIC_GEOMETRY_INTERNAL_MESH_SIMPLIFICATION_APPEARANCE_UV_SEAMS_H_

#include <cstddef>
#include <vector>

#include "absl/types/span.h"
#include "intrinsic/geometry/internal/mesh_simplification/appearance/remapped_triangles.h"

namespace intrinsic::geo {

// UV seams are where a mesh's texture coordinates jump: vertices that share a
// position have different UVs, one for each side of the seam. If the
// simplifier moved them, the texture would tear, so they are locked
// (ProtectUvSeams), as gltfpack does.
//
// Mirrored textures hide some seams. To save texture space, two symmetric
// parts of a model (e.g. the left and right halves of a part) often use the
// same region of the texture, one flipped. Along the mirror line, the triangles
// of both halves share vertices with the same UVs, so the data has no seam
// there, but the UV orientation (winding) flips from one side to the other. If
// the simplifier merged vertices across that line, a triangle could get UVs
// from both sides and the texture would fold or smear.

// Turns mirror lines into seams, like gltfpack's simplifyUvSplit, so that
// ProtectUvSeams locks them.
//
// A vertex is on a mirror line if its triangles have both UV windings. For
// each such vertex, this modifies `mesh`: it appends a copy of the vertex to
// `mesh.source`, and points the clockwise (flipped) triangles in
// `mesh.indices` at the copy. `uvs` has kUvSize floats per input vertex.
//
// Returns, for every vertex of the modified `mesh`, the vertex it is a copy of
// (itself for the original vertices), to merge the copies back after
// simplifying. If `uvs` is empty, returns an empty vector and leaves `mesh`
// unchanged.
std::vector<unsigned int> SplitMirroredUvSeams(absl::Span<const float> uvs,
                                               RemappedTriangles& mesh);

// Returns per-vertex meshopt_SimplifyVertex_Protect locks, like gltfpack's
// simplifyProtect: a vertex is locked if an earlier vertex has the same
// position but different UVs (a UV seam), or if it is one of the copies
// SplitMirroredUvSeams added after the first `presplit_vertex_count` vertices.
// `positions` and `uvs` have kPositionSize and kUvSize floats per vertex; if
// `uvs` is empty, nothing is locked.
std::vector<unsigned char> ProtectUvSeams(absl::Span<const float> positions,
                                          absl::Span<const float> uvs,
                                          size_t presplit_vertex_count);

}  // namespace intrinsic::geo

#endif  // INTRINSIC_GEOMETRY_INTERNAL_MESH_SIMPLIFICATION_APPEARANCE_UV_SEAMS_H_
