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

#ifndef INTRINSIC_GEOMETRY_INTERNAL_MESH_SIMPLIFICATION_MESHOPT_LOCALITY_SIMPLIFIER_H_
#define INTRINSIC_GEOMETRY_INTERNAL_MESH_SIMPLIFICATION_MESHOPT_LOCALITY_SIMPLIFIER_H_

#include <cstddef>

#include "absl/functional/function_ref.h"
#include "absl/status/statusor.h"
#include "intrinsic/geometry/internal/conversion_utils/mesh_conversion.h"
#include "intrinsic/geometry/internal/util/timeout_predicate.h"
#include "intrinsic/util/thread/stop_token.h"

namespace intrinsic::geo {

struct MeshoptLocalityResult {
  // The simplified mesh.
  MeshoptBuffers buffers;
  // Upper bound (in meters) on the distance from the original mesh to the
  // simplified one, from the checks of the accepted collapses.
  double resulting_error = 0.0;
  // Number of proposed collapses refused by the distance check.
  size_t rejected_collapses = 0;
  // True if the timeout stopped simplification early. The mesh is then only
  // partly simplified, but still within the distance limit.
  bool timed_out = false;
};

// Simplifies a mesh with meshoptimizer while keeping every point of the
// original mesh within `max_hausdorff_distance` of the result. Each collapse
// proposed by meshoptimizer is only allowed if the original triangles it
// affects stay close enough to the triangles around the merged vertex.
//
// Duplicate vertices of `buffers` are merged and triangles with a repeated
// corner dropped first; if nothing is left, returns an empty result. Returns
// CancelledError if `stop_token` is triggered, and DeadlineExceededError if the
// timeout expired before starting. If the timeout expires part way through,
// returns the partly simplified mesh with `timed_out` set.
absl::StatusOr<MeshoptLocalityResult> SimplifyWithLocalityBound(
    const MeshoptBuffers& buffers, double max_hausdorff_distance,
    const StopToken& stop_token,
    const TimeoutPredicate& custom_early_stop_predicate,
    bool regularize = true);

// Same as above, but stops once `has_timed_out` returns true. Lets tests
// trigger a timeout at an exact point.
absl::StatusOr<MeshoptLocalityResult> SimplifyWithLocalityBound(
    const MeshoptBuffers& buffers, double max_hausdorff_distance,
    const StopToken& stop_token, absl::FunctionRef<bool()> has_timed_out,
    bool regularize = true);

}  // namespace intrinsic::geo

#endif  // INTRINSIC_GEOMETRY_INTERNAL_MESH_SIMPLIFICATION_MESHOPT_LOCALITY_SIMPLIFIER_H_
