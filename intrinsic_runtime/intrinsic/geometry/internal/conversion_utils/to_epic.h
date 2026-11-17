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

#ifndef INTRINSIC_GEOMETRY_INTERNAL_CONVERSION_UTILS_TO_EPIC_H_
#define INTRINSIC_GEOMETRY_INTERNAL_CONVERSION_UTILS_TO_EPIC_H_

#include "CGAL/Aff_transformation_3.h"
#include "CGAL/Bbox_3.h"
#include "CGAL/Polygon_mesh_processing/orient_polygon_soup.h"
#include "CGAL/Polygon_mesh_processing/polygon_soup_to_polygon_mesh.h"
#include "CGAL/Polygon_mesh_processing/repair_degeneracies.h"
#include "CGAL/Polygon_mesh_processing/repair_polygon_soup.h"
#include "CGAL/utils.h"
#include "absl/log/check.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/geometry/api/axis_aligned_bounding_box_3d.h"
#include "intrinsic/geometry/api/triangle.h"
#include "intrinsic/geometry/internal/indexed_triangle_set_3/indexed_triangle_set_3.h"
#include "intrinsic/geometry/internal/kernel_3/exact_predicates_inexact_constructions_kernel.h"
#include "intrinsic/geometry/internal/surface_mesh_3/surface_mesh_3d.h"
#include "intrinsic/math/pose3.h"

// This file provides conversions to primitives of the Epic Kernel.

namespace intrinsic::geo {
inline CGAL::Bbox_3 ToEpic(const AxisAlignedBoundingBox3d& b) {
  return CGAL::Bbox_3(b.GetMin(0), b.GetMin(1), b.GetMin(2), b.GetMax(0),
                      b.GetMax(1), b.GetMax(2));
}

inline const EpicPlane3& ToEpic(const EpicPlane3& p) { return p; }

inline const EpicPoint3& ToEpic(const EpicPoint3& p) { return p; }

inline EpicPoint3 ToEpic(const eigenmath::Vector3d& v) {
  return EpicPoint3(v[0], v[1], v[2]);
}

inline EpicTriangle3 ToEpic(const Triangle& t) {
  return EpicTriangle3(ToEpic(t.v0), ToEpic(t.v1), ToEpic(t.v2));
}

template <int Options>
inline CGAL::Aff_transformation_3<EpicKernel> ToEpic(
    const eigenmath::Matrix4<double, Options>& transformation_matrix) {
  // The CGAL constructor takes the 3x3 rotational matrix, the translational
  // component in the last column and the homogenous coefficient in the lower
  // right corner.
  return CGAL::Aff_transformation_3<EpicKernel>(
      transformation_matrix(0, 0), transformation_matrix(0, 1),
      transformation_matrix(0, 2), transformation_matrix(0, 3),
      transformation_matrix(1, 0), transformation_matrix(1, 1),
      transformation_matrix(1, 2), transformation_matrix(1, 3),
      transformation_matrix(2, 0), transformation_matrix(2, 1),
      transformation_matrix(2, 2), transformation_matrix(2, 3),
      transformation_matrix(3, 3));
}

inline CGAL::Aff_transformation_3<EpicKernel> ToEpic(const Pose3d& t) {
  return ToEpic(t.matrix());
}

inline eigenmath::Vector3d FromEpic3(const EpicPoint3& p) {
  return eigenmath::Vector3d(p.x(), p.y(), p.z());
}

inline eigenmath::Vector3d FromEpic3(const EpicVector3& p) {
  return eigenmath::Vector3d(p.x(), p.y(), p.z());
}

inline EpicSurfaceMesh3 ToEpicSurfaceMesh3(
    IndexedTriangleSet3<CGAL::Epick> indexed_triangle_set) {
  namespace PMP = CGAL::Polygon_mesh_processing;

  auto& faces = indexed_triangle_set.triples();
  auto& points = indexed_triangle_set.points();
  // Repair the polygon soup. This includes simplifying faces with identical
  // vertices, removing isolated points, merging duplicate points, merging
  // duplicate faces, etc.
  PMP::repair_polygon_soup(points, faces,
                           CGAL::parameters::require_same_orientation(true));
  // This syncs the orientation of all triangles so they become patches.
  // It also adds vertex duplicates in case of non manifolds.
  // While this is only necessary for surface mesh, it should also help
  // the swept volume computation as it improves connectivity.
  PMP::orient_polygon_soup(points, faces);
  CHECK(PMP::is_polygon_soup_a_polygon_mesh(faces));
  EpicSurfaceMesh3 surface_mesh;
  PMP::polygon_soup_to_polygon_mesh(points, faces, surface_mesh);
  CHECK(CGAL::is_valid(surface_mesh));
  // Repairing the polygon soup fixes any topology problems but does not
  // remove degenerate faces (faces with 3 colinear points). We do that now,
  // to avoid failing CGAL preconditions later.
  // TODO(b/482359928): We should cleanup this mesh-repair code. It is repeated
  // in many places.
  PMP::remove_degenerate_faces(surface_mesh);
  return surface_mesh;
}

}  // namespace intrinsic::geo
namespace intrinsic {
using ::intrinsic::geo::FromEpic3;
}  // namespace intrinsic

#endif  // INTRINSIC_GEOMETRY_INTERNAL_CONVERSION_UTILS_TO_EPIC_H_
