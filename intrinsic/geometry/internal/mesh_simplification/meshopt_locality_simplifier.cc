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

#include "intrinsic/geometry/internal/mesh_simplification/meshopt_locality_simplifier.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "absl/functional/function_ref.h"
#include "absl/hash/hash.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/geometry/internal/conversion_utils/mesh_conversion.h"
#include "intrinsic/geometry/internal/mesh_simplification/meshopt_simplification_helpers.h"
#include "intrinsic/geometry/internal/util/timeout_predicate.h"
#include "intrinsic/util/thread/stop_token.h"
#include "meshoptimizer.h"
#include "third_party/meshoptimizer/simplifier.h"

namespace intrinsic::geo {
namespace {

using ::intrinsic::eigenmath::Vector3d;
using ::intrinsic::geo::meshopt::ComputeRingCoverage;
using ::intrinsic::geo::meshopt::RingCoverage;
using ::intrinsic::geo::meshopt::Triangle3d;
using ::intrinsic::geo::meshopt::WeldAndCleanTriangleMesh;

constexpr size_t kFloatsPerVertex = 3;
constexpr size_t kVertexStride = sizeof(float) * kFloatsPerVertex;
constexpr size_t kIndicesPerTriangle = 3;
constexpr uint32_t kInvalidVertex = ~0u;
// Loose QEM error limit, so that our check (not QEM) decides when to stop.
constexpr float kPermissiveQemTargetError = 1.0f;
// Target triangle count (a tetrahedron), so that meshoptimizer keeps proposing
// collapses until it finds none it can accept.
constexpr size_t kMinTargetTriangleCount = 4;

// The last refused collapse of a vertex, so the same collapse with the same
// surrounding triangles can be refused again without redoing the check.
struct RefusedCollapse {
  uint32_t kept_vertex = kInvalidVertex;
  uint32_t failing_triangle = kInvalidVertex;
  size_t ring_triangle_count = 0;
  size_t ring_hash = 0;
};

// Hashes the corner indices of the triangles around a vertex (see
// `ring_corners` in SimplifyWithCancellation).
size_t HashRing(absl::Span<const unsigned int> ring_corners) {
  return absl::HashOf(ring_corners);
}

// Returns triangle `triangle` of the flat index list `indices` (three corners
// per triangle), with corners looked up in the flat float `positions` and
// converted to double for the distance checks. Indices come from the welded
// mesh or from meshoptimizer's ring of the same mesh, so they are in range.
Triangle3d TriangleAt(absl::Span<const float> positions,
                      absl::Span<const unsigned int> indices, size_t triangle) {
  const absl::Span<const unsigned int> corners =
      indices.subspan(triangle * kIndicesPerTriangle, kIndicesPerTriangle);
  auto corner = [positions, corners](size_t i) {
    const float* p = &positions[corners[i] * kFloatsPerVertex];
    return Vector3d(p[0], p[1], p[2]);
  };
  return {corner(0), corner(1), corner(2)};
}

}  // namespace

absl::StatusOr<MeshoptLocalityResult> SimplifyWithLocalityBound(
    const MeshoptBuffers& buffers, double max_hausdorff_distance,
    const StopToken& stop_token,
    const TimeoutPredicate& custom_early_stop_predicate, bool regularize) {
  return SimplifyWithLocalityBound(
      buffers, max_hausdorff_distance, stop_token,
      [&custom_early_stop_predicate]() {
        return custom_early_stop_predicate.HasTimedOut();
      },
      regularize);
}

absl::StatusOr<MeshoptLocalityResult> SimplifyWithLocalityBound(
    const MeshoptBuffers& buffers, double max_hausdorff_distance,
    const StopToken& stop_token, absl::FunctionRef<bool()> has_timed_out,
    bool regularize) {
  // Merge duplicate vertices so each index has its own position.
  const MeshoptBuffers welded = WeldAndCleanTriangleMesh(buffers);
  const size_t welded_vertex_count =
      welded.vertex_positions.size() / kFloatsPerVertex;
  MeshoptLocalityResult result;
  if (welded_vertex_count == 0 || welded.indices.empty()) {
    return result;
  }

  if (stop_token.stop_requested()) {
    return absl::CancelledError("SimplifyWithLocalityBound cancelled.");
  }
  if (has_timed_out()) {
    return absl::DeadlineExceededError("SimplifyWithLocalityBound timed out.");
  }

  const absl::Span<const unsigned int> original_indices = welded.indices;
  const size_t index_count = original_indices.size();
  const size_t original_triangle_count = index_count / kIndicesPerTriangle;

  // Each original triangle is watched by the vertices whose moves could change
  // the surface near it. A collapse that removes a vertex must re-check every
  // triangle that vertex watches, then hand them to new watchers.
  //
  // `watched_triangles[v]`: the original triangles that vertex `v` watches.
  // `watchers[t]`: the vertices that watch original triangle `t`.
  std::vector<std::vector<uint32_t>> watched_triangles(welded_vertex_count);
  std::vector<std::vector<uint32_t>> watchers(original_triangle_count);
  for (uint32_t triangle = 0; triangle < original_triangle_count; ++triangle) {
    const unsigned int* corners =
        &original_indices[triangle * kIndicesPerTriangle];
    for (size_t corner = 0; corner < kIndicesPerTriangle; ++corner) {
      watched_triangles[corners[corner]].push_back(triangle);
    }
    watchers[triangle] = {corners[0], corners[1], corners[2]};
  }

  double max_accepted_error = 0.0;
  size_t rejected_collapses = 0;
  std::vector<RefusedCollapse> last_refusal(welded_vertex_count);
  bool stopped_early = false;

  // Reused between calls to avoid allocations.
  std::vector<Triangle3d> ring;
  // (original triangle, index into the ring of a triangle that covers it).
  std::vector<std::pair<uint32_t, size_t>> pending_handoffs;

  // Removes `original_triangle` from the lists of all its watchers except
  // `skip`. Clears a vertex's last refusal if it was caused by that triangle,
  // since the same collapse might now pass.
  auto stop_watching = [&watchers, &watched_triangles, &last_refusal](
                           uint32_t original_triangle, uint32_t skip) {
    for (const uint32_t watcher : watchers[original_triangle]) {
      if (watcher == skip) {
        continue;
      }
      if (std::erase(watched_triangles[watcher], original_triangle) > 0 &&
          last_refusal[watcher].failing_triangle == original_triangle) {
        last_refusal[watcher].kept_vertex = kInvalidVertex;
      }
    }
    watchers[original_triangle].clear();
  };

  // Makes the corners of ring triangle `covering_triangle` watch
  // `original_triangle`.
  auto start_watching = [&watchers, &watched_triangles](
                            uint32_t original_triangle,
                            absl::Span<const unsigned int> ring_corners,
                            size_t covering_triangle) {
    auto& triangle_watchers = watchers[original_triangle];
    for (size_t corner = 0; corner < kIndicesPerTriangle; ++corner) {
      const uint32_t vertex =
          ring_corners[covering_triangle * kIndicesPerTriangle + corner];
      if (std::find(triangle_watchers.begin(), triangle_watchers.end(),
                    vertex) == triangle_watchers.end()) {
        triangle_watchers.push_back(vertex);
        watched_triangles[vertex].push_back(original_triangle);
      }
    }
  };

  // Called by meshoptimizer before each collapse of `removed_vertex` onto
  // `kept_vertex`. `ring_indices` points to a temporary array meshoptimizer
  // builds for this call: the vertex indices (three per triangle) of the
  // `ring_triangle_count` triangles that would surround `kept_vertex` after
  // the collapse. It is only valid during the call. Returns whether the
  // collapse is allowed.
  auto can_collapse = [&welded, &stopped_early, &stop_token, &has_timed_out,
                       &rejected_collapses, &last_refusal, &ring,
                       &pending_handoffs, &watched_triangles, original_indices,
                       max_hausdorff_distance, &max_accepted_error,
                       &stop_watching, &start_watching](
                          unsigned int removed_vertex, unsigned int kept_vertex,
                          const unsigned int* ring_indices,
                          size_t ring_triangle_count) -> bool {
    const absl::Span<const unsigned int> ring_corners = absl::MakeConstSpan(
        ring_indices, ring_triangle_count * kIndicesPerTriangle);
    // Once stopped, refuse everything so meshoptimizer finishes quickly with
    // the collapses done so far.
    if (stopped_early) {
      return false;
    }
    if (stop_token.stop_requested() || has_timed_out()) {
      stopped_early = true;
      return false;
    }

    // Refuse collapses that would delete the last triangle of a piece.
    if (ring_triangle_count == 0) {
      ++rejected_collapses;
      return false;
    }

    // Refuse right away if this exact collapse already failed with the same
    // surrounding triangles. The ring is only hashed when the rest matches.
    const RefusedCollapse& refusal = last_refusal[removed_vertex];
    if (refusal.kept_vertex == kept_vertex &&
        refusal.ring_triangle_count == ring_triangle_count &&
        refusal.ring_hash == HashRing(ring_corners)) {
      ++rejected_collapses;
      return false;
    }

    ring.resize(ring_triangle_count);
    for (size_t i = 0; i < ring_triangle_count; ++i) {
      ring[i] = TriangleAt(welded.vertex_positions, ring_corners, i);
    }

    // Every original triangle that `removed_vertex` watches must stay close
    // enough to the ring. Handoffs are only staged here, so a refused collapse
    // changes nothing.
    double collapse_error = 0.0;
    pending_handoffs.clear();
    auto& removed_vertex_triangles = watched_triangles[removed_vertex];
    for (size_t i = 0; i < removed_vertex_triangles.size(); ++i) {
      const uint32_t original_triangle = removed_vertex_triangles[i];
      const std::optional<RingCoverage> coverage =
          ComputeRingCoverage(TriangleAt(welded.vertex_positions,
                                         original_indices, original_triangle),
                              ring, max_hausdorff_distance);
      if (!coverage.has_value()) {
        // Move the failing triangle to the front so the next attempt on
        // `removed_vertex` checks it first and can be refused quickly.
        std::swap(removed_vertex_triangles[0], removed_vertex_triangles[i]);
        last_refusal[removed_vertex] = RefusedCollapse{
            .kept_vertex = kept_vertex,
            .failing_triangle = original_triangle,
            .ring_triangle_count = ring_triangle_count,
            .ring_hash = HashRing(ring_corners),
        };
        ++rejected_collapses;
        return false;
      }
      collapse_error = std::max(collapse_error, coverage->max_distance_bound);
      for (const size_t covering_triangle : coverage->covering_triangles) {
        pending_handoffs.emplace_back(original_triangle, covering_triangle);
      }
    }

    // All passed. Hand each of `removed_vertex`'s triangles to the corners of
    // the ring triangles that covered it.
    max_accepted_error = std::max(max_accepted_error, collapse_error);
    for (const uint32_t original_triangle : removed_vertex_triangles) {
      stop_watching(original_triangle, /*skip=*/removed_vertex);
    }
    removed_vertex_triangles.clear();
    last_refusal[removed_vertex].kept_vertex = kInvalidVertex;
    for (const auto& [original_triangle, covering_triangle] :
         pending_handoffs) {
      start_watching(original_triangle, ring_corners, covering_triangle);
    }
    return true;
  };

  unsigned int options = meshopt_SimplifyLockBorder;
  if (regularize) {
    options |= meshopt_SimplifyRegularizeLight;
  }

  // Keep proposing collapses until none is accepted.
  const size_t min_target_index_count = std::min<size_t>(
      index_count, kMinTargetTriangleCount * kIndicesPerTriangle);
  std::vector<uint32_t> simplified_indices(index_count);
  float meshopt_error = 0.0f;
  const size_t simplified_index_count =
      intrinsic_thirdparty::meshopt_simplifyCustom(
          simplified_indices.data(), original_indices.data(), index_count,
          welded.vertex_positions.data(), welded_vertex_count, kVertexStride,
          min_target_index_count, kPermissiveQemTargetError, options,
          &meshopt_error, can_collapse);

  if (stop_token.stop_requested()) {
    return absl::CancelledError("SimplifyWithLocalityBound cancelled.");
  }
  // On timeout, keep the partly simplified mesh. Every collapse done so far
  // passed the check.
  result.timed_out = stopped_early;

  // Reorder triangles for faster rendering and remove unused vertices.
  simplified_indices.resize(simplified_index_count);
  meshopt_optimizeVertexCache(simplified_indices.data(),
                              simplified_indices.data(), simplified_index_count,
                              welded_vertex_count);

  result.buffers.vertex_positions.resize(welded_vertex_count *
                                         kFloatsPerVertex);
  const size_t compact_vertex_count = meshopt_optimizeVertexFetch(
      result.buffers.vertex_positions.data(), simplified_indices.data(),
      simplified_index_count, welded.vertex_positions.data(),
      welded_vertex_count, kVertexStride);
  result.buffers.vertex_positions.resize(compact_vertex_count *
                                         kFloatsPerVertex);
  result.buffers.indices = std::move(simplified_indices);
  result.resulting_error = max_accepted_error;
  result.rejected_collapses = rejected_collapses;
  return result;
}

}  // namespace intrinsic::geo
