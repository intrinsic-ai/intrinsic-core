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

#include "intrinsic/geometry/internal/surface_mesh_3/unary_predicates.h"

#include "CGAL/Polygon_mesh_processing/self_intersections.h"
#include "CGAL/Surface_mesh/Surface_mesh.h"
#include "CGAL/boost/graph/helpers.h"
#include "intrinsic/geometry/internal/surface_mesh_3/surface_mesh_3d.h"

namespace intrinsic::geo {
// Checks if `surface_mesh` is a closed 2-manifold.
// Note that the notion of manifold is defined with respect to the connectivity
// graph. For instance, it still allows for self intersections, thus it is only
// a necessary condition for `surface_mesh` being a solid.
bool IsSolid(const EpicSurfaceMesh3& surface_mesh) {
  if (!IsClosed(surface_mesh)) return false;

  for (auto vertex : surface_mesh.vertices()) {
    if (surface_mesh.is_isolated(vertex)) return false;
  }
  if (HasSelfIntersections(surface_mesh)) return false;
  return true;
}

bool IsClosed(const EpicSurfaceMesh3& surface_mesh) {
  return CGAL::is_closed(surface_mesh);
}

bool HasSelfIntersections(const EpicSurfaceMesh3& surface_mesh) {
  namespace PMP = CGAL::Polygon_mesh_processing;
  return PMP::does_self_intersect(surface_mesh);
}

bool IsManifold(const EpicSurfaceMesh3& surface_mesh) {
  return !HasSelfIntersections(surface_mesh);
}

bool IsTriangleMesh(const EpicSurfaceMesh3& surface_mesh) {
  return CGAL::is_triangle_mesh(surface_mesh);
}

bool IsQuadMesh(const EpicSurfaceMesh3& surface_mesh) {
  return CGAL::is_quad_mesh(surface_mesh);
}

bool IsEmpty(const EpicSurfaceMesh3& surface_mesh) {
  return CGAL::is_empty(surface_mesh);
}

}  // namespace intrinsic::geo
