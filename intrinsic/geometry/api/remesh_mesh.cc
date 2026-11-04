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

#include "intrinsic/geometry/api/remesh_mesh.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/geometry/api/axis_aligned_bounding_box_3d.h"
#include "intrinsic/geometry/internal/conversion_utils/mesh_conversion.h"
#include "intrinsic/geometry/internal/mesh/mesh.h"
#include "intrinsic/util/status/status_macros.h"
#include "meshoptimizer.h"

namespace intrinsic::geo {
namespace {

static_assert(std::is_same_v<uint32_t, unsigned int>,
              "meshoptimizer index buffers require uint32_t == unsigned int.");

// Returns the transform that maps `mesh` into a frame centered on its bounding
// box with unit half-extent along its largest axis.
absl::StatusOr<eigenmath::AffineTransform3d> ComputeNormalizingTransform(
    const Mesh& mesh) {
  AxisAlignedBoundingBox3d bounds;
  for (size_t i = 0; i < mesh.vertex_count(); ++i) {
    bounds.ExtendBy(mesh.vertex(i));
  }
  if (bounds.IsEmpty()) {
    return absl::InvalidArgumentError("Input mesh has faces but no vertices.");
  }
  const eigenmath::Vector3d diagonal = bounds.GetDiagonal();
  if (!diagonal.allFinite()) {
    return absl::InvalidArgumentError(
        "Input mesh contains non-finite coordinates or an extent that "
        "overflows double precision.");
  }
  const double half_extent = 0.5 * diagonal.maxCoeff();
  const double scale = half_extent > 0.0 ? half_extent : 1.0;
  eigenmath::AffineTransform3d normalized_t_mesh =
      eigenmath::AffineTransform3d::Identity();
  normalized_t_mesh.scale(1.0 / scale).translate(-bounds.GetCenter());
  return normalized_t_mesh;
}

}  // namespace

absl::StatusOr<RemeshMeshResult> RemeshMesh(
    const Mesh& mesh, const MeshoptRemeshOptions& options) {
  if (options.resolution < kMinVoxelRemeshResolution ||
      options.resolution > kMaxVoxelRemeshResolution) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "Voxel remesh resolution must be in [%d, %d], but got %d.",
        kMinVoxelRemeshResolution, kMaxVoxelRemeshResolution,
        options.resolution));
  }

  if (mesh.empty()) {
    return RemeshMeshResult{
        .mesh = Mesh(),
        .result =
            RemeshResult{
                .original_vertex_count = mesh.vertex_count(),
                .original_face_count = mesh.face_count(),
                .remeshed_vertex_count = 0,
                .remeshed_face_count = 0,
            },
    };
  }

  // `meshopt_remesh` builds its voxel grid from the float bounding box, which
  // overflows for meshes spanning most of the float range and loses precision
  // far from the origin. Since the voxelization is relative to the bounding
  // box, remesh in a normalized frame and map the result back.
  INTR_ASSIGN_OR_RETURN(const eigenmath::AffineTransform3d normalized_t_mesh,
                        ComputeNormalizingTransform(mesh));
  Mesh normalized_mesh = mesh.Clone();
  normalized_mesh.Transform(normalized_t_mesh);

  // Rejects non-finite coordinates and invalid face indices.
  INTR_ASSIGN_OR_RETURN(const MeshoptBuffers input_buffers,
                        ToMeshoptBuffers(normalized_mesh));

  const size_t original_vertex_count = mesh.vertex_count();
  const size_t original_face_count = mesh.face_count();
  const size_t index_count = input_buffers.indices.size();

  unsigned int flags = 0;
  if (options.shell) {
    flags |= meshopt_RemeshShell;
  }
  if (options.solve) {
    flags |= meshopt_RemeshSolve;
  }

  // First pass counts the upper bound on generated triangles without allocating
  // per-voxel solve state.
  const size_t triangle_capacity = meshopt_remesh(
      /*destination=*/nullptr, /*max_triangle_count=*/0,
      input_buffers.indices.data(), index_count,
      input_buffers.vertex_positions.data(), original_vertex_count,
      sizeof(float) * 3, options.resolution, flags);
  if (triangle_capacity == 0) {
    return RemeshMeshResult{
        .mesh = Mesh(),
        .result =
            RemeshResult{
                .original_vertex_count = original_vertex_count,
                .original_face_count = original_face_count,
                .remeshed_vertex_count = 0,
                .remeshed_face_count = 0,
            },
    };
  }

  // Second pass emits unindexed triangle corner positions (3 vertices * 3
  // floats = 9 floats per triangle).
  std::vector<float> unindexed_triangles(triangle_capacity * 9);
  const size_t triangle_count = meshopt_remesh(
      unindexed_triangles.data(), triangle_capacity,
      input_buffers.indices.data(), index_count,
      input_buffers.vertex_positions.data(), original_vertex_count,
      sizeof(float) * 3, options.resolution, flags);
  if (triangle_count == 0) {
    return RemeshMeshResult{
        .mesh = Mesh(),
        .result =
            RemeshResult{
                .original_vertex_count = original_vertex_count,
                .original_face_count = original_face_count,
                .remeshed_vertex_count = 0,
                .remeshed_face_count = 0,
            },
    };
  }
  unindexed_triangles.resize(triangle_count * 9);

  // Deduplicate shared voxel corner vertices into an indexed triangle mesh.
  const size_t unindexed_vertex_count = triangle_count * 3;
  std::vector<uint32_t> remap(unindexed_vertex_count);
  const size_t unique_vertex_count = meshopt_generateVertexRemap(
      remap.data(), /*indices=*/nullptr, unindexed_vertex_count,
      unindexed_triangles.data(), unindexed_vertex_count, sizeof(float) * 3);

  std::vector<uint32_t> remapped_indices(unindexed_vertex_count);
  meshopt_remapIndexBuffer(remapped_indices.data(), /*indices=*/nullptr,
                           unindexed_vertex_count, remap.data());

  std::vector<float> remapped_vertices(unique_vertex_count * 3);
  meshopt_remapVertexBuffer(remapped_vertices.data(),
                            unindexed_triangles.data(), unindexed_vertex_count,
                            sizeof(float) * 3, remap.data());

  // Filter out any degenerate triangles where solved corners coincided.
  size_t valid_index_count = 0;
  for (size_t i = 0; i < remapped_indices.size(); i += 3) {
    const uint32_t a = remapped_indices[i + 0];
    const uint32_t b = remapped_indices[i + 1];
    const uint32_t c = remapped_indices[i + 2];
    if (a != b && b != c && a != c) {
      remapped_indices[valid_index_count + 0] = a;
      remapped_indices[valid_index_count + 1] = b;
      remapped_indices[valid_index_count + 2] = c;
      valid_index_count += 3;
    }
  }
  remapped_indices.resize(valid_index_count);
  if (remapped_indices.empty()) {
    return RemeshMeshResult{
        .mesh = Mesh(),
        .result =
            RemeshResult{
                .original_vertex_count = original_vertex_count,
                .original_face_count = original_face_count,
                .remeshed_vertex_count = 0,
                .remeshed_face_count = 0,
            },
    };
  }

  meshopt_optimizeVertexCache(remapped_indices.data(), remapped_indices.data(),
                              remapped_indices.size(), unique_vertex_count);

  std::vector<float> compact_vertices(unique_vertex_count * 3);
  const size_t compact_vertex_count = meshopt_optimizeVertexFetch(
      compact_vertices.data(), remapped_indices.data(), remapped_indices.size(),
      remapped_vertices.data(), unique_vertex_count, sizeof(float) * 3);
  compact_vertices.resize(compact_vertex_count * 3);

  const size_t remeshed_face_count = remapped_indices.size() / 3;
  Mesh result_mesh = FromMeshoptBuffers(MeshoptBuffers{
      .vertex_positions = std::move(compact_vertices),
      .indices = std::move(remapped_indices),
  });
  result_mesh.Transform(normalized_t_mesh.inverse());

  return RemeshMeshResult{
      .mesh = std::move(result_mesh),
      .result =
          RemeshResult{
              .original_vertex_count = original_vertex_count,
              .original_face_count = original_face_count,
              .remeshed_vertex_count = compact_vertex_count,
              .remeshed_face_count = remeshed_face_count,
          },
  };
}

}  // namespace intrinsic::geo
