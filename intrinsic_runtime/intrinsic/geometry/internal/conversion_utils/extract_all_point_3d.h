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

#ifndef INTRINSIC_GEOMETRY_INTERNAL_CONVERSION_UTILS_EXTRACT_ALL_POINT_3D_H_
#define INTRINSIC_GEOMETRY_INTERNAL_CONVERSION_UTILS_EXTRACT_ALL_POINT_3D_H_

#include <algorithm>
#include <iterator>
#include <vector>

#include "CGAL/Surface_mesh/Surface_mesh.h"
#include "intrinsic/geometry/internal/indexed_triangle_set_3/indexed_triangle_set_3.h"
#include "intrinsic/geometry/internal/kernel_3/exact_predicates_inexact_constructions_kernel.h"
#include "intrinsic/geometry/internal/mesh/mesh.h"
#include "intrinsic/geometry/internal/surface_mesh_3/surface_mesh_3d.h"

namespace intrinsic::geo {
template <class OutputIterator>
OutputIterator EpicExtractAllPoint3(const EpicSurfaceMesh3& sm,
                                    OutputIterator oit) {
  return std::copy(sm.points().begin(), sm.points().end(), oit);
}

template <class OutputIterator>
OutputIterator EpicExtractAllPoint3(const EpicPoint3& p, OutputIterator oit) {
  return *oit++ = p;
}

template <class OutputIterator>
OutputIterator EpicExtractAllPoint3(const EpicTriangle3& t,
                                    OutputIterator oit) {
  *oit++ = t.vertex(0);
  *oit++ = t.vertex(1);
  *oit++ = t.vertex(2);
  return oit;
}

template <class Geo, class OutputIterator>
OutputIterator EpicExtractAllPoint3(const std::vector<Geo>& geos,
                                    OutputIterator oit) {
  for (const auto& geo : geos) {
    oit = EpicExtractAllPoint3(geo, oit);
  }
  return oit;
}

template <class OutputIterator>
OutputIterator EpicExtractAllPoint3(const EpicIndexedTriangleSet3& set,
                                    OutputIterator oit) {
  return std::copy(set.points().begin(), set.points().end(), oit);
}

template <class OutputIterator>
OutputIterator EpicExtractAllPoint3(const Mesh& mesh, OutputIterator oit) {
  for (int i = 0; i < mesh.vertex_count(); ++i) {
    *oit = EpicPoint3(mesh.vertex(i)[0], mesh.vertex(i)[1], mesh.vertex(i)[2]);
    oit++;
  }
  return oit;
}

// Extracts the sequence of all points in the order as it is solely induced
// by the corresponding Geo type, which includes duplicates.
template <class Geo>
std::vector<EpicPoint3> EpicExtractAllPoint3(const Geo& geo) {
  std::vector<EpicPoint3> points;
  EpicExtractAllPoint3(geo, std::back_inserter(points));
  return points;
}

}  // namespace intrinsic::geo
#endif  // INTRINSIC_GEOMETRY_INTERNAL_CONVERSION_UTILS_EXTRACT_ALL_POINT_3D_H_
