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

#ifndef INTRINSIC_GEOMETRY_API_REMESH_MESH_H_
#define INTRINSIC_GEOMETRY_API_REMESH_MESH_H_

#include <cstddef>

#include "absl/status/statusor.h"
#include "intrinsic/geometry/internal/mesh/mesh.h"

namespace intrinsic::geo {

// Minimum supported voxel grid dimension for `meshopt_remesh`.
inline constexpr int kMinVoxelRemeshResolution = 4;

// Maximum supported voxel grid dimension for `meshopt_remesh`.
inline constexpr int kMaxVoxelRemeshResolution = 256;

// Default voxel grid dimension for `RemeshMesh`.
inline constexpr int kDefaultVoxelRemeshResolution = 64;

// Options configuring meshoptimizer voxel remeshing (`meshopt_remesh`).
struct MeshoptRemeshOptions {
  // Dimension of the internal voxel grid in
  // `[kMinVoxelRemeshResolution, kMaxVoxelRemeshResolution]` (`[4, 256]`).
  // Higher values preserve finer geometric detail at the cost of higher output
  // triangle counts and runtime.
  int resolution = kDefaultVoxelRemeshResolution;

  // Computes optimal output vertex positions that approximate the original
  // surface as closely as possible (`meshopt_RemeshSolve`). Disable only when a
  // more regular grid-aligned triangulation is preferred.
  bool solve = true;

  // Produces a two-sided shell that wraps around surfaces of the original mesh
  // instead of filling closed interiors into a solid volume
  // (`meshopt_RemeshShell`).
  bool shell = false;
};

// Summary statistics returned by voxel remeshing.
struct RemeshResult {
  size_t original_vertex_count = 0;
  size_t original_face_count = 0;
  size_t remeshed_vertex_count = 0;
  size_t remeshed_face_count = 0;
};

// Result containing the remeshed mesh and remeshing statistics.
struct RemeshMeshResult {
  Mesh mesh;
  RemeshResult result;
};

// Reconstructs a closed, uniformly tessellated mesh approximating `mesh` at the
// configured voxel grid resolution using `meshopt_remesh`, followed by exact
// vertex deduplication and vertex cache/fetch compaction.
//
// Original mesh topology is not preserved: gaps smaller than a voxel may be
// closed, nearby features may be merged, and interior geometry is discarded by
// default (`options.shell == false`).
//
// Precision Note:
//   `meshopt_remesh` operates internally on single-precision `float` positions.
//   Input `Mesh` coordinates (`double`) are cast to `float` during voxelization
//   and converted back to `double` in the output `Mesh`.
absl::StatusOr<RemeshMeshResult> RemeshMesh(
    const Mesh& mesh, const MeshoptRemeshOptions& options = {});

}  // namespace intrinsic::geo

#endif  // INTRINSIC_GEOMETRY_API_REMESH_MESH_H_
