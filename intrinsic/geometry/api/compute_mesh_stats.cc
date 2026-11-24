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

#include "intrinsic/geometry/api/compute_mesh_stats.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>

#include "CGAL/Kernel/global_functions_3.h"
#include "CGAL/Origin.h"
#include "CGAL/Polygon_mesh_processing/measure.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "intrinsic/geometry/api/axis_aligned_bounding_box_3d.h"
#include "intrinsic/geometry/api/exact_geometry.h"
#include "intrinsic/geometry/api/geometry.h"
#include "intrinsic/geometry/api/mesh_stats.h"
#include "intrinsic/geometry/internal/conversion_utils/mesh_conversion.h"
#include "intrinsic/geometry/internal/kernel_3/exact_predicates_inexact_constructions_kernel.h"
#include "intrinsic/geometry/internal/mesh/mesh.h"
#include "intrinsic/geometry/internal/surface_mesh_3/surface_mesh_3d.h"
#include "intrinsic/geometry/internal/surface_mesh_3/unary_predicates.h"
#include "intrinsic/geometry/internal/util/cgal_utils.h"
#include "intrinsic/geometry/shapes/point_cloud.h"
#include "intrinsic/util/object_store/memoize.h"
#include "intrinsic/util/object_store/object_ref.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic::geo {
namespace {

absl::StatusOr<EpicPoint3> ComputeVertexCentroid(
    const EpicSurfaceMesh3& surface_mesh) {
  size_t num_vertices = surface_mesh.vertices().size();
  if (num_vertices == 0) {
    return EpicPoint3(0.0, 0.0, 0.0);
  }
  EpicKernel::Vector_3 centroid(0.0, 0.0, 0.0);
  for (const auto& vertex : surface_mesh.vertices()) {
    if (!vertex.is_valid()) {
      return absl::InvalidArgumentError(
          "Vertex in mesh is invalid so cannot compute vertex center. ");
    }
    // CGAL convert point to vector
    centroid += surface_mesh.point(vertex) - CGAL::ORIGIN;
  }
  return CGAL::ORIGIN + (centroid / num_vertices);
}

double ComputeMinCircumradius(const EpicSurfaceMesh3& surface_mesh) {
  if (surface_mesh.is_empty()) {
    return 0.0;
  }
  double min_sq_circumradius = std::numeric_limits<double>::max();
  for (const auto& face : surface_mesh.faces()) {
    auto half_edge = surface_mesh.halfedge(face);
    auto p1 = surface_mesh.point(surface_mesh.source(half_edge));
    auto p2 = surface_mesh.point(surface_mesh.target(half_edge));
    half_edge = surface_mesh.next(half_edge);
    auto p3 = surface_mesh.point(surface_mesh.target(half_edge));

    if (CGAL::collinear(p1, p2, p3)) {
      continue;
    }

    const auto center = CGAL::circumcenter(p1, p2, p3);
    const double sq_radius = (p1 - center).squared_length();
    min_sq_circumradius = std::min(min_sq_circumradius, sq_radius);
  }

  return (min_sq_circumradius == std::numeric_limits<double>::max())
             ? 0.0
             : std::sqrt(min_sq_circumradius);
}

absl::StatusOr<MeshStats> ComputeMeshStatsImpl(const Mesh& mesh,
                                               AxisAlignedBoundingBox3d&& aabb,
                                               bool contains_primitives) {
  if (mesh.empty()) {
    return MeshStats();
  }

  EpicSurfaceMesh3 surface_mesh = ToEpicSurfaceMesh3(mesh);
  if (surface_mesh.is_empty()) {
    return MeshStats();
  }

  bool closed = IsClosed(surface_mesh);
  EpicPoint3 volumetric_centroid = EpicPoint3(0.0, 0.0, 0.0);
  EpicPoint3 vertex_centroid = EpicPoint3(0.0, 0.0, 0.0);
  double volume = 0.0;
  if (closed) {
    INTR_ASSIGN_OR_RETURN(
        volumetric_centroid, ProtectedCGALCall([&]() {
          return CGAL::Polygon_mesh_processing::centroid(surface_mesh);
        }));
    INTR_ASSIGN_OR_RETURN(
        volume, ProtectedCGALCall([&]() {
          return CGAL::Polygon_mesh_processing::volume(surface_mesh);
        }));
  }
  INTR_ASSIGN_OR_RETURN(vertex_centroid, ComputeVertexCentroid(surface_mesh));

  int num_vertices = mesh.vertices().size();
  int num_triangles = mesh.faces().size();
  return MeshStats{
      .vertex_centroid = std::move(vertex_centroid),
      .volumetric_centroid = std::move(volumetric_centroid),
      .num_vertices = num_vertices,
      .num_triangles = num_triangles,
      .closed = closed,
      .contains_primitives = contains_primitives,
      .volume = volume,
      .aabb = std::move(aabb),
      .min_circumradius = ComputeMinCircumradius(surface_mesh),
  };
}

class ComputeMeshStatsFunctor {
 public:
  explicit ComputeMeshStatsFunctor(bool contains_primitives)
      : contains_primitives_(contains_primitives) {}

  template <typename Geo>
  absl::StatusOr<MeshStats> operator()(const Geo& geo) const {
    LOG(WARNING) << "WARNING: intrinsic::ComputeMeshStatsFunctor "
                    "does not support\n"
                 << "operator()(const Geo&)) const, with \n"
                 << "Geo = " << Demangle<Geo>();
    return absl::InvalidArgumentError(
        "ComputeMeshStats does not support argument type.");
  }

  template <>
  absl::StatusOr<MeshStats> operator()(const ObjectRef<Mesh>& geo) const {
    INTR_ASSIGN_OR_RETURN(
        const auto stats,
        Memoize(
            MemoizeOptions("ComputeMeshStats-Mesh"),
            [](const ObjectRef<Mesh>& geo,
               bool contains_primitives) -> absl::StatusOr<MeshStats> {
              AxisAlignedBoundingBox3d aabb;
              for (const auto& v : geo.Value().vertices()) {
                aabb.ExtendBy(v);
              }

              return ComputeMeshStatsImpl(geo.Value(), std::move(aabb),
                                          contains_primitives);
            },
            geo, contains_primitives_));
    return stats.Value();
  }

  template <>
  absl::StatusOr<MeshStats> operator()(const ObjectRef<PointCloud>& geo) const {
    return MeshStats();
  }

 private:
  bool contains_primitives_;
};

}  // namespace

absl::StatusOr<MeshStats> ComputeMeshStats(const Geometry& geo) {
  const bool has_primitives =
      !geo.GetExactGeometry().GetPrimitiveShapes().empty();

  INTR_ASSIGN_OR_RETURN(
      auto stats,
      geo.GetExactGeometry().visit(ComputeMeshStatsFunctor(has_primitives)));

  return stats;
}

}  // namespace intrinsic::geo
