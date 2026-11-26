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

#ifndef INTRINSIC_GEOMETRY_INTERNAL_MESH_SIMPLIFICATION_MESHOPT_SIMPLIFICATION_HELPERS_H_
#define INTRINSIC_GEOMETRY_INTERNAL_MESH_SIMPLIFICATION_MESHOPT_SIMPLIFICATION_HELPERS_H_

#include <cstddef>
#include <optional>
#include <vector>

#include "absl/types/span.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/geometry/internal/conversion_utils/mesh_conversion.h"

namespace intrinsic::geo::meshopt {

// Default number of times `ComputeRingCoverage` may split a
// triangle into 4 smaller ones.
constexpr int kDefaultMaxSubdivisionDepth = 4;

// A triangle with double-precision corners.
struct Triangle3d {
  eigenmath::Vector3d v1;
  eigenmath::Vector3d v2;
  eigenmath::Vector3d v3;
};

// Returns the squared distance from `query_point` to `triangle`.
double SquaredDistanceToTriangle(const Triangle3d& triangle,
                                 const eigenmath::Vector3d& query_point);

// Returns `input_buffers` with duplicate vertices merged and with triangles
// that end up with a repeated corner removed. Returns empty buffers if no
// triangles remain. Vertices that no remaining triangle uses are removed.
//
// Every index must refer to an existing vertex, as guaranteed by
// `ToMeshoptBuffers`. A trailing incomplete triangle is ignored.
MeshoptBuffers WeldAndCleanTriangleMesh(const MeshoptBuffers& input_buffers);

// Result of a successful `ComputeRingCoverage` call.
struct RingCoverage {
  // Upper bound on the distance from the triangle to the ring.
  double max_distance_bound = 0.0;
  // Indices into the ring of the triangles that covered the triangle, without
  // duplicates.
  std::vector<size_t> covering_triangles;
};

// Returns the coverage if every point of `triangle` is within `max_distance`
// of at least one triangle in `ring`, and nullopt otherwise.
//
// The check is conservative: it may return nullopt for a triangle that is close
// enough, but never returns coverage for one that is not. It splits the
// triangle into 4 smaller ones at most `max_depth` times before giving up.
// Returns nullopt if `ring` is empty.
std::optional<RingCoverage> ComputeRingCoverage(
    const Triangle3d& triangle, absl::Span<const Triangle3d> ring,
    double max_distance, int max_depth = kDefaultMaxSubdivisionDepth);

}  // namespace intrinsic::geo::meshopt

#endif  // INTRINSIC_GEOMETRY_INTERNAL_MESH_SIMPLIFICATION_MESHOPT_SIMPLIFICATION_HELPERS_H_
