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

#ifndef INTRINSIC_GEOMETRY_INTERNAL_CONVERSION_UTILS_MESH_CONVERSION_H_
#define INTRINSIC_GEOMETRY_INTERNAL_CONVERSION_UTILS_MESH_CONVERSION_H_

#include <cstdint>
#include <vector>

#include "absl/status/statusor.h"
#include "coacd.h"
#include "intrinsic/geometry/internal/indexed_triangle_set_3/indexed_triangle_set_3.h"
#include "intrinsic/geometry/internal/mesh/mesh.h"
#include "intrinsic/geometry/internal/surface_mesh_3/surface_mesh_3d.h"

namespace intrinsic::geo {
inline const EpicSurfaceMesh3& ToEpicSurfaceMesh3(
    const EpicSurfaceMesh3& mesh) {
  return mesh;
}

// Converts an EpicSurfaceMesh3 to EpicIndexedTriangleSet3.
EpicIndexedTriangleSet3 ToEpicIndexedTriangleSet3(
    const EpicSurfaceMesh3& surface_mesh);
// Converts a Mesh to EpicIndexedTriangleSet3.
EpicIndexedTriangleSet3 ToEpicIndexedTriangleSet3(const Mesh& mesh);

// Converts an EpicSurfaceMesh3 to Mesh.
Mesh ToMesh(const EpicSurfaceMesh3& surface_mesh);
// Converts an EpicIndexedTriangleSet3 to Mesh.
Mesh ToMesh(const EpicIndexedTriangleSet3& indexed_triangle_set);

// Converts a Mesh to EpicSurfaceMesh3.
EpicSurfaceMesh3 ToEpicSurfaceMesh3(const Mesh& mesh);

// Converts an EpicSurfaceMesh3 to Mesh. It cleans the mesh by removing
// isolated vertices, removing small connected components, duplicating
// non-manifold vertices, and removing degenerate edges and faces.
absl::StatusOr<Mesh> ToMeshWithCleaning(const EpicSurfaceMesh3& surface_mesh);

// Options for governing polygon soup mesh cleaning and repair.
struct CleanMeshOptions {
  // If true, validates that the resulting cleaned mesh forms a valid,
  // orientable 2-manifold polygon mesh. If false, residual non-manifold
  // geometry is permitted.
  //
  // Behavior:
  // - When true: CleanMesh returns absl::InvalidArgumentError if the mesh
  //   cannot be repaired into a manifold surface.
  // - When false: CleanMesh returns absl::OkStatus() even if non-manifold
  //   elements remain. Suitable for algorithms with internal remeshing
  //   fallbacks (e.g. CoACD).
  //
  // Example: Three faces meeting at a single shared edge. When true, cleaning
  // fails with an error; when false, cleaning succeeds and retains the valid
  // faces.
  bool require_polygon_mesh = true;

  // Prunes zero-area degenerate triangular faces where vertex indices collapse
  // or distinct vertices are collinear.
  //
  // Behavior:
  // - Removes triangles with repeated indices (e.g. collapsed edge or vertex).
  // - Removes triangles whose 3D coordinates lie along a single line.
  //
  // Example:
  // - Repeated index: Triangle {0, 0, 1} where two vertices are identical.
  // - Collinear points: Triangle with vertices at (0, 0, 0), (1, 0, 0), and
  //   (2, 0, 0) lying on the x-axis.
  bool prune_degenerate_faces = true;

  // Prunes unreferenced isolated vertices from the point cloud.
  //
  // Behavior:
  // - Vertex coordinates that appear in zero face triples are removed.
  // - All remaining face vertex index references are updated accordingly.
  //
  // Example: A mesh with 4 points {p0, p1, p2, p3} but only one triangle
  // {0, 1, 2}. Point p3 is removed, leaving 3 points.
  bool prune_isolated_points = true;

  // Merges coincident vertices that share the exact same 3D coordinates.
  //
  // Behavior:
  // - Duplicate coordinates are unified into a single vertex in points.
  // - Face index references are rewritten to point to the shared vertex,
  //   effectively stitching seams and cracks along shared boundaries.
  //
  // Example: Two adjacent triangles define their shared edge with separate
  // points: Triangle A uses (0, 0, 0) and (1, 0, 0); Triangle B also specifies
  // (0, 0, 0) and (1, 0, 0) at distinct point indices. Merging unifies them
  // into shared vertices, connecting the two triangles.
  bool merge_duplicate_points = true;

  // Merges and prunes duplicate faces sharing the same set of vertices.
  //
  // Behavior:
  // - Removes redundant triangles that reference identical vertices,
  //   regardless of cyclic index permutation.
  //
  // Example: A mesh contains two triangles referencing the same vertices:
  // {0, 1, 2} and {1, 2, 0}. The duplicate face is removed, leaving 1 face.
  bool merge_duplicate_faces = true;

  // Unifies face winding order across connected components so adjacent faces
  // have consistent normal directions.
  //
  // Behavior:
  // - Reverses face vertex index orders where adjacent faces traverse a shared
  //   edge in the same direction.
  //
  // Example: Two triangles meet along edge (1, 2): Triangle A is {0, 1, 2} and
  // Triangle B is {3, 1, 2}. Both traverse edge (1, 2) in the same direction,
  // pointing their surface normals in opposite directions. Orientation flips
  // Triangle B to {3, 2, 1} so the traversal directions oppose and normals
  // align.
  bool orient_normals = true;

  // Strict options requiring 2-manifold polygon mesh topology and pruning
  // collinear/degenerate faces.
  static const CleanMeshOptions& Strict();

  // Best-effort options allowing residual non-manifold topology for algorithms
  // with internal remeshing fallbacks (e.g. CoACD).
  static const CleanMeshOptions& BestEffort();
};

// Cleans an EpicIndexedTriangleSet3 in-place according to the given options.
// Returns absl::OkStatus() if cleaning succeeds and the mesh satisfies the
// options (e.g. is a valid 2-manifold polygon mesh when require_polygon_mesh is
// true). Returns an error status if the cleaned mesh violates the options.
absl::Status CleanMesh(EpicIndexedTriangleSet3& indexed_triangle_set,
                       const CleanMeshOptions& options);

// Cleans an EpicIndexedTriangleSet3 in-place using CleanMeshOptions::Strict().
absl::Status CleanMesh(EpicIndexedTriangleSet3& indexed_triangle_set);

// Converts a Mesh to EpicSurfaceMesh3. It cleans the mesh according to options
// (defaults to CleanMeshOptions::Strict()).
absl::StatusOr<EpicSurfaceMesh3> ToEpicSurfaceMesh3WithCleaning(
    const Mesh& mesh,
    const CleanMeshOptions& options = CleanMeshOptions::Strict());

// Converts an EpicIndexedTriangleSet3 to a clean coacd::Mesh.
absl::StatusOr<coacd::Mesh> ToCoacdMesh(
    EpicIndexedTriangleSet3 indexed_triangle_set);

// Converts a vector of decomposed coacd::Mesh elements to a vector of
// EpicIndexedTriangleSet3 elements.
std::vector<EpicIndexedTriangleSet3> FromCoacdMeshes(
    const std::vector<coacd::Mesh>& decomposed_meshes);

// Flat single-precision vertex and 32-bit triangle index buffers used by
// meshoptimizer.
struct MeshoptBuffers {
  std::vector<float> vertex_positions;
  std::vector<uint32_t> indices;
};

// Converts a Mesh to flat float vertex coordinates [x0, y0, z0, ...] and
// uint32_t triangle indices for meshoptimizer.
MeshoptBuffers ToMeshoptBuffers(const Mesh& mesh);

// Converts flat float vertex coordinates and uint32_t triangle indices from
// meshoptimizer back to a double-precision Mesh.
Mesh FromMeshoptBuffers(const MeshoptBuffers& buffers);

}  // namespace intrinsic::geo
#endif  // INTRINSIC_GEOMETRY_INTERNAL_CONVERSION_UTILS_MESH_CONVERSION_H_
