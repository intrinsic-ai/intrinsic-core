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

#include "intrinsic/geometry/internal/alpha_hull_3/alpha_hull_3.h"

#include <utility>

#include "CGAL/Alpha_wrap_3/internal/Alpha_wrap_AABB_geom_traits.h"
#include "CGAL/Cartesian/function_objects.h"
#include "CGAL/Kernel/function_objects.h"
#include "CGAL/Surface_mesh/Surface_mesh.h"
#include "CGAL/alpha_wrap_3.h"
#include "CGAL/boost/graph/helpers.h"
#include "CGAL/property_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace gsan {

absl::StatusOr<Epic_surface_mesh_3> AlphaHull3(
    const Epic_surface_mesh_3& surface_mesh, double max_error) {
  if (max_error < 0) {
    return absl::InvalidArgumentError("AlphaHull3: alpha must be positive");
  }

  // Early exit if solid or empty.
  if (CGAL::is_empty(surface_mesh)) return surface_mesh;

  if (max_error == 0) {
    return absl::InvalidArgumentError(
        "AlphaHull3: Given alpha is zero, but input was not solid "
        "in the first place.");
  }

  Epic_surface_mesh_3 alpha_hull_mesh;
  CGAL::alpha_wrap_3(surface_mesh, max_error, max_error / 30, alpha_hull_mesh);
  return std::move(alpha_hull_mesh);
}

}  // namespace gsan
