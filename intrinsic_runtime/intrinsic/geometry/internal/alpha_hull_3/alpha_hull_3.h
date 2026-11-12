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

#ifndef INTRINSIC_GEOMETRY_INTERNAL_ALPHA_HULL_3_ALPHA_HULL_3_H_
#define INTRINSIC_GEOMETRY_INTERNAL_ALPHA_HULL_3_ALPHA_HULL_3_H_

#include "CGAL/Exact_predicates_inexact_constructions_kernel.h"
#include "CGAL/Filtered_kernel/internal/Static_filters/Do_intersect_3.h"
#include "CGAL/Intersections_3/internal/Bbox_3_Triangle_3_do_intersect.h"
#include "CGAL/Kernel/function_objects.h"
#include "CGAL/Surface_mesh/Surface_mesh.h"
#include "CGAL/property_map.h"
#include "absl/status/statusor.h"

namespace gsan {

using Kernel = CGAL::Exact_predicates_inexact_constructions_kernel;
using Point_3 = Kernel::Point_3;
using Vector_3 = Kernel::Vector_3;
using Epic_surface_mesh_3 = CGAL::Surface_mesh<Point_3>;

absl::StatusOr<Epic_surface_mesh_3> AlphaHull3(
    const Epic_surface_mesh_3& surface_mesh, double max_error);

}  // namespace gsan

#endif  // INTRINSIC_GEOMETRY_INTERNAL_ALPHA_HULL_3_ALPHA_HULL_3_H_
