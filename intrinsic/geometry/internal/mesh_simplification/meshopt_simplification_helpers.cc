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

#include "intrinsic/geometry/internal/mesh_simplification/meshopt_simplification_helpers.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

#include "absl/types/span.h"
#include "intrinsic/eigenmath/types.h"
#include "meshoptimizer.h"

namespace intrinsic::geo::meshopt {
namespace {

using ::intrinsic::eigenmath::Vector3d;

static_assert(sizeof(unsigned int) == sizeof(uint32_t),
              "meshoptimizer requires 32-bit unsigned int indices.");

constexpr size_t kFloatsPerVertex = 3;
constexpr size_t kIndicesPerTriangle = 3;
constexpr double kInfinity = std::numeric_limits<double>::infinity();
// Below this value the triangle is treated as degenerate. The fallback can only
// overestimate the distance, which is safe.
constexpr double kDegenerateBarycentricSumEpsilon = 1e-30;

}  // namespace

double SquaredDistanceToTriangle(const Triangle3d& triangle,
                                 const Vector3d& query_point) {
  const Vector3d& v1 = triangle.v1;
  const Vector3d& v2 = triangle.v2;
  const Vector3d& v3 = triangle.v3;
  auto squared_distance_to = [&query_point](const Vector3d& closest_point) {
    return (query_point - closest_point).squaredNorm();
  };

  // Find which part of the triangle is closest to `query_point` (a corner, an
  // edge, or the face) and measure to it (Ericson, Real-Time Collision
  // Detection, Section 5.1.5).
  //
  // Closest to corner v1.
  const Vector3d edge_12 = v2 - v1;
  const Vector3d edge_13 = v3 - v1;
  const Vector3d v1_to_p = query_point - v1;
  const double d1 = edge_12.dot(v1_to_p);
  const double d2 = edge_13.dot(v1_to_p);
  if (d1 <= 0.0 && d2 <= 0.0) {
    return squared_distance_to(v1);
  }

  // Closest to corner v2.
  const Vector3d v2_to_p = query_point - v2;
  const double d3 = edge_12.dot(v2_to_p);
  const double d4 = edge_13.dot(v2_to_p);
  if (d3 >= 0.0 && d4 <= d3) {
    return squared_distance_to(v2);
  }

  // Closest to edge v1-v2.
  const double vc = d1 * d4 - d3 * d2;
  if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0 && (d1 - d3) > 0.0) {
    const double v = d1 / (d1 - d3);
    return squared_distance_to(v1 + v * edge_12);
  }

  // Closest to corner v3.
  const Vector3d v3_to_p = query_point - v3;
  const double d5 = edge_12.dot(v3_to_p);
  const double d6 = edge_13.dot(v3_to_p);
  if (d6 >= 0.0 && d5 <= d6) {
    return squared_distance_to(v3);
  }

  // Closest to edge v1-v3.
  const double vb = d5 * d2 - d1 * d6;
  if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0 && (d2 - d6) > 0.0) {
    const double w = d2 / (d2 - d6);
    return squared_distance_to(v1 + w * edge_13);
  }

  // Closest to edge v2-v3.
  const double va = d3 * d6 - d5 * d4;
  if (va <= 0.0 && (d4 - d3) >= 0.0 && (d5 - d6) >= 0.0 &&
      ((d4 - d3) + (d5 - d6)) > 0.0) {
    const double w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
    return squared_distance_to(v2 + w * (v3 - v2));
  }

  // Closest to a point inside the face. A degenerate (zero-area) triangle falls
  // back to v1.
  const double barycentric_sum = va + vb + vc;
  if (barycentric_sum <= kDegenerateBarycentricSumEpsilon) {
    return squared_distance_to(v1);
  }
  const double inv_barycentric_sum = 1.0 / barycentric_sum;
  const double v = vb * inv_barycentric_sum;
  const double w = vc * inv_barycentric_sum;
  return squared_distance_to(v1 + v * edge_12 + w * edge_13);
}

MeshoptBuffers WeldAndCleanTriangleMesh(const MeshoptBuffers& input_buffers) {
  MeshoptBuffers welded_buffers;
  const size_t input_vertex_count =
      input_buffers.vertex_positions.size() / kFloatsPerVertex;
  // Ignore a trailing incomplete triangle.
  const size_t input_index_count =
      input_buffers.indices.size() -
      (input_buffers.indices.size() % kIndicesPerTriangle);
  if (input_vertex_count == 0 || input_index_count < kIndicesPerTriangle) {
    return welded_buffers;
  }

  // Turn -0.0 into +0.0 so that both are welded together. meshoptimizer treats
  // them as the same position internally.
  std::vector<float> canonical_positions(input_vertex_count * kFloatsPerVertex);
  for (size_t i = 0; i < canonical_positions.size(); ++i) {
    canonical_positions[i] = input_buffers.vertex_positions[i] + 0.0f;
  }

  std::vector<uint32_t> vertex_remap(input_vertex_count);
  const size_t welded_vertex_count = meshopt_generateVertexRemap(
      vertex_remap.data(), input_buffers.indices.data(), input_index_count,
      canonical_positions.data(), input_vertex_count,
      sizeof(float) * kFloatsPerVertex);

  welded_buffers.indices.resize(input_index_count);
  meshopt_remapIndexBuffer(welded_buffers.indices.data(),
                           input_buffers.indices.data(), input_index_count,
                           vertex_remap.data());

  // Drop triangles that now have a repeated corner. Thin triangles with three
  // different corners are kept.
  size_t write_index = 0;
  for (size_t i = 0; i < input_index_count; i += kIndicesPerTriangle) {
    const uint32_t v0 = welded_buffers.indices[i + 0];
    const uint32_t v1 = welded_buffers.indices[i + 1];
    const uint32_t v2 = welded_buffers.indices[i + 2];
    if (v0 != v1 && v1 != v2 && v0 != v2) {
      welded_buffers.indices[write_index + 0] = v0;
      welded_buffers.indices[write_index + 1] = v1;
      welded_buffers.indices[write_index + 2] = v2;
      write_index += kIndicesPerTriangle;
    }
  }
  welded_buffers.indices.resize(write_index);
  if (write_index == 0) {
    return welded_buffers;
  }

  // Renumber the vertices that the remaining triangles use, so vertices that
  // were only used by dropped triangles are not copied.
  std::vector<uint32_t> used_remap(welded_vertex_count);
  const size_t used_vertex_count = meshopt_optimizeVertexFetchRemap(
      used_remap.data(), welded_buffers.indices.data(), write_index,
      welded_vertex_count);
  for (uint32_t& index : welded_buffers.indices) {
    index = used_remap[index];
  }
  for (uint32_t& index : vertex_remap) {
    if (index != ~0u) {
      index = used_remap[index];
    }
  }

  welded_buffers.vertex_positions.resize(used_vertex_count * kFloatsPerVertex);
  meshopt_remapVertexBuffer(welded_buffers.vertex_positions.data(),
                            canonical_positions.data(), input_vertex_count,
                            sizeof(float) * kFloatsPerVertex,
                            vertex_remap.data());
  return welded_buffers;
}

namespace {

// Result of checking one triangle without splitting it.
enum class TriangleCheck {
  kCovered,    // Covered by a single candidate triangle.
  kRejected,   // A corner is too far from every candidate triangle.
  kUndecided,  // Neither; needs subdivision.
};

// A piece of the input triangle waiting to be checked, and how many times it
// has been split.
struct PendingTriangle {
  Triangle3d triangle;
  int depth;
};

// Checks one triangle without splitting it. On kCovered, also raises
// `max_sq_dist` and adds the covering triangle to `covering_triangles`.
TriangleCheck CheckTriangle(const Triangle3d& triangle,
                            absl::Span<const Triangle3d> ring,
                            double max_distance_sq, double& max_sq_dist,
                            std::vector<size_t>& covering_triangles) {
  const Vector3d& v1 = triangle.v1;
  const Vector3d& v2 = triangle.v2;
  const Vector3d& v3 = triangle.v3;
  double min_sq_dist_1 = kInfinity;
  double min_sq_dist_2 = kInfinity;
  double min_sq_dist_3 = kInfinity;
  double min_candidate_max_sq_dist = kInfinity;
  size_t best_candidate_idx = 0;

  // Accept if a single candidate triangle is close enough to all three corners.
  // The distance to a flat triangle is largest at one of the corners, so this
  // covers the whole triangle.
  for (size_t candidate_idx = 0; candidate_idx < ring.size(); ++candidate_idx) {
    const Triangle3d& candidate = ring[candidate_idx];
    const double sq_dist_1 = SquaredDistanceToTriangle(candidate, v1);
    const double sq_dist_2 = SquaredDistanceToTriangle(candidate, v2);
    const double sq_dist_3 = SquaredDistanceToTriangle(candidate, v3);

    min_sq_dist_1 = std::min(min_sq_dist_1, sq_dist_1);
    min_sq_dist_2 = std::min(min_sq_dist_2, sq_dist_2);
    min_sq_dist_3 = std::min(min_sq_dist_3, sq_dist_3);

    const double candidate_max_sq_dist =
        std::max({sq_dist_1, sq_dist_2, sq_dist_3});
    if (candidate_max_sq_dist < min_candidate_max_sq_dist) {
      min_candidate_max_sq_dist = candidate_max_sq_dist;
      best_candidate_idx = candidate_idx;
    }
  }
  if (min_candidate_max_sq_dist <= max_distance_sq) {
    max_sq_dist = std::max(max_sq_dist, min_candidate_max_sq_dist);
    if (std::find(covering_triangles.begin(), covering_triangles.end(),
                  best_candidate_idx) == covering_triangles.end()) {
      covering_triangles.push_back(best_candidate_idx);
    }
    return TriangleCheck::kCovered;
  }

  // Reject if any corner is too far from every candidate triangle. Splitting
  // cannot fix that.
  const double hausdorff_lower_sq =
      std::max({min_sq_dist_1, min_sq_dist_2, min_sq_dist_3});
  if (hausdorff_lower_sq > max_distance_sq) {
    return TriangleCheck::kRejected;
  }

  return TriangleCheck::kUndecided;
}

}  // namespace

std::optional<RingCoverage> ComputeRingCoverage(
    const Triangle3d& triangle, absl::Span<const Triangle3d> ring,
    double max_distance, int max_depth) {
  if (ring.empty()) {
    return std::nullopt;
  }

  // Check the triangle, and split any piece that can't be decided into 4 at
  // its edge midpoints. Uses a stack that never holds more than
  // 3 * max_depth + 1 pieces.
  const double max_distance_sq = max_distance * max_distance;
  double max_sq_dist = 0.0;
  std::vector<size_t> covering_triangles;
  std::vector<PendingTriangle> pending = {{triangle, 0}};
  while (!pending.empty()) {
    const PendingTriangle piece = pending.back();
    pending.pop_back();
    switch (CheckTriangle(piece.triangle, ring, max_distance_sq, max_sq_dist,
                          covering_triangles)) {
      case TriangleCheck::kCovered:
        continue;
      case TriangleCheck::kRejected:
        return std::nullopt;
      case TriangleCheck::kUndecided:
        break;
    }
    if (piece.depth >= max_depth) {
      return std::nullopt;
    }
    const Triangle3d& t = piece.triangle;
    const Vector3d midpoint_12 = 0.5 * (t.v1 + t.v2);
    const Vector3d midpoint_13 = 0.5 * (t.v1 + t.v3);
    const Vector3d midpoint_23 = 0.5 * (t.v2 + t.v3);
    const int child_depth = piece.depth + 1;
    pending.push_back({{t.v3, midpoint_13, midpoint_23}, child_depth});
    pending.push_back({{t.v2, midpoint_12, midpoint_23}, child_depth});
    pending.push_back({{t.v1, midpoint_12, midpoint_13}, child_depth});
    // Pushed last so it is checked first: it is the quickest to reject if a
    // midpoint is too far away.
    pending.push_back({{midpoint_12, midpoint_13, midpoint_23}, child_depth});
  }
  return RingCoverage{.max_distance_bound = std::sqrt(max_sq_dist),
                      .covering_triangles = std::move(covering_triangles)};
}

}  // namespace intrinsic::geo::meshopt
