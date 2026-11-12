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

#ifndef INTRINSIC_GEOMETRY_INTERNAL_SURFACE_MESH_3_UNARY_PREDICATES_H_
#define INTRINSIC_GEOMETRY_INTERNAL_SURFACE_MESH_3_UNARY_PREDICATES_H_

#include "intrinsic/geometry/internal/surface_mesh_3/surface_mesh_3d.h"

namespace intrinsic::geo {
// If the surface has self intersections, this includes non-manifold vertices.
bool HasSelfIntersections(const EpicSurfaceMesh3& surface_mesh);
// A manifold surface is closed if there are no open boundary edges.
bool IsClosed(const EpicSurfaceMesh3& surface_mesh);
// TODO(b/164237152): Revisit definition of solid with regard to volume vs
// surface. E.g. a surface mesh with several connected components may still
// bound one volume. However in terms of collision checking only the outer CC is
// relevant. A solid is a closed manifold without self intersections.
bool IsSolid(const EpicSurfaceMesh3& surface_mesh);

bool IsEmpty(const EpicSurfaceMesh3& surface_mesh);
bool IsManifold(const EpicSurfaceMesh3& surface_mesh);
bool IsQuadMesh(const EpicSurfaceMesh3& surface_mesh);
bool IsTriangleMesh(const EpicSurfaceMesh3& surface_mesh);

// A surface mesh is either invalid, or a combinatorial manifold.
inline bool IsCombinatorialManifold(const EpicSurfaceMesh3& surface_mesh) {
  return true;
}

}  // namespace intrinsic::geo

namespace intrinsic {
using ::intrinsic::geo::IsEmpty;
}  // namespace intrinsic
#endif  // INTRINSIC_GEOMETRY_INTERNAL_SURFACE_MESH_3_UNARY_PREDICATES_H_
