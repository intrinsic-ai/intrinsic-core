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

#ifndef INTRINSIC_GEOMETRY_API_IS_CONVEX_H_
#define INTRINSIC_GEOMETRY_API_IS_CONVEX_H_

#include "absl/status/statusor.h"
#include "intrinsic/geometry/api/geometry.h"

namespace intrinsic::geo {

// Default relative tolerance for `IsConvex`.
inline constexpr double kDefaultConvexityRelativeVolumeTolerance = 1e-6;

// Returns whether the given Geometry is a convex solid, i.e., a closed mesh
// whose enclosed volume equals the volume of its convex hull within
// `relative_volume_tolerance` (relative to the convex hull volume).
//
// Returns false for empty or open meshes, meshes enclosing no volume (e.g., a
// single flat surface) and point clouds.
absl::StatusOr<bool> IsConvex(const Geometry& geo,
                              double relative_volume_tolerance =
                                  kDefaultConvexityRelativeVolumeTolerance);

}  // namespace intrinsic::geo

namespace intrinsic {
using ::intrinsic::geo::IsConvex;
}  // namespace intrinsic

#endif  // INTRINSIC_GEOMETRY_API_IS_CONVEX_H_
