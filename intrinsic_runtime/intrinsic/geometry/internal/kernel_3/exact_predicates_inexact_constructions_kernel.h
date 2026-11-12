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

#ifndef INTRINSIC_GEOMETRY_INTERNAL_KERNEL_3_EXACT_PREDICATES_INEXACT_CONSTRUCTIONS_KERNEL_H_
#define INTRINSIC_GEOMETRY_INTERNAL_KERNEL_3_EXACT_PREDICATES_INEXACT_CONSTRUCTIONS_KERNEL_H_

#include "CGAL/Aff_transformation_3.h"
#include "CGAL/Exact_predicates_inexact_constructions_kernel.h"  // IWYU pragma: export

namespace intrinsic::geo {
// The CGAL kernel used throughout the geometry processing package. go/cgal-epic
using EpicKernel = CGAL::Exact_predicates_inexact_constructions_kernel;

constexpr EpicKernel kEpicKernel;

// The fundamental number type used throughout the geometry processing package.
// go/cgal-algebraic-foundations
using EpicFieldType = EpicKernel::FT;  // double for EPIC

// Geometric Primitives go/cgal-kernel-primitives
using EpicCircle3 = EpicKernel::Circle_3;
using EpicDirection3 = EpicKernel::Direction_3;
using EpicIsoCuboid3 = EpicKernel::Iso_cuboid_3;
using EpicLine3 = EpicKernel::Line_3;
using EpicPlane3 = EpicKernel::Plane_3;
using EpicPoint3 = EpicKernel::Point_3;
using EpicRay3 = EpicKernel::Ray_3;
using EpicSegment3 = EpicKernel::Segment_3;
using EpicSphere3 = EpicKernel::Sphere_3;
using EpicTetrahedron3 = EpicKernel::Tetrahedron_3;
using EpicTriangle3 = EpicKernel::Triangle_3;
using EpicVector3 = EpicKernel::Vector_3;
using EpicWeightedPoint3 = EpicKernel::Weighted_point_3;

using EpicAffTransformation3 = CGAL::Aff_transformation_3<EpicKernel>;

}  // namespace intrinsic::geo
#endif  // INTRINSIC_GEOMETRY_INTERNAL_KERNEL_3_EXACT_PREDICATES_INEXACT_CONSTRUCTIONS_KERNEL_H_
