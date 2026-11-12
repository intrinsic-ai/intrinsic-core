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

#ifndef INTRINSIC_GEOMETRY_INTERNAL_SURFACE_MESH_3_SURFACE_MESH_3D_H_
#define INTRINSIC_GEOMETRY_INTERNAL_SURFACE_MESH_3_SURFACE_MESH_3D_H_

#include "CGAL/Surface_mesh/Surface_mesh.h"
#include "intrinsic/geometry/internal/kernel_3/exact_predicates_inexact_constructions_kernel.h"

// Namespace encapsulating the geometry processing package.
namespace intrinsic::geo {

using EpicSurfaceMesh3 = CGAL::Surface_mesh<EpicPoint3>;

namespace sm3d {

using VertexIndex = EpicSurfaceMesh3::Vertex_index;
using FaceIndex = EpicSurfaceMesh3::Face_index;
using HalfedgeIndex = EpicSurfaceMesh3::Halfedge_index;

}  // namespace sm3d

}  // namespace intrinsic::geo

#endif  // INTRINSIC_GEOMETRY_INTERNAL_SURFACE_MESH_3_SURFACE_MESH_3D_H_
