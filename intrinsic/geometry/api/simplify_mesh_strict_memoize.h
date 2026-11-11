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

#ifndef INTRINSIC_GEOMETRY_API_SIMPLIFY_MESH_STRICT_MEMOIZE_H_
#define INTRINSIC_GEOMETRY_API_SIMPLIFY_MESH_STRICT_MEMOIZE_H_

#include "absl/status/statusor.h"
#include "intrinsic/geometry/api/geometry.h"
#include "intrinsic/geometry/internal/util/timeout_predicate.h"
#include "intrinsic/util/thread/stop_token.h"

namespace intrinsic::geo {

// Returns a simplified copy of `geo` where every point of the original mesh is
// within `max_hausdorff_distance` (meters) of the result, up to float rounding
// of the vertices (about 1e-7 times the coordinate size).
//
// Border vertices are not moved, so open meshes simplify less than closed ones.
//
// Returns `geo` unchanged if `max_hausdorff_distance` is 0 or the mesh is
// empty. If the timeout expires part way through, returns the partly simplified
// mesh, which is still within the distance. If it has already expired, returns
// `geo` unchanged. Returns CancelledError if `stop_token` is triggered, and
// InvalidArgumentError if `max_hausdorff_distance` is negative or not finite
// or `geo` contains primitive shapes.
//
// `stop_token` and `custom_early_stop_predicate` mean different things: a stop
// request cancels the operation and returns CancelledError with no result,
// while the timeout is a time budget after which the partly simplified mesh is
// returned as a successful result.
absl::StatusOr<Geometry> SimplifyMeshStrictMemoize(
    const Geometry& geo, double max_hausdorff_distance,
    StopToken stop_token = StopToken(),
    const TimeoutPredicate& custom_early_stop_predicate = TimeoutPredicate());

}  // namespace intrinsic::geo

#endif  // INTRINSIC_GEOMETRY_API_SIMPLIFY_MESH_STRICT_MEMOIZE_H_
