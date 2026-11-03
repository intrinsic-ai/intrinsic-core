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

#include "intrinsic/geometry/api/is_convex.h"

#include <cmath>
#include <vector>

#include "CGAL/Polygon_mesh_processing/measure.h"
#include "CGAL/convex_hull_3.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "intrinsic/geometry/api/exact_geometry.h"
#include "intrinsic/geometry/api/geometry.h"
#include "intrinsic/geometry/internal/conversion_utils/extract_all_point_3d.h"
#include "intrinsic/geometry/internal/conversion_utils/mesh_conversion.h"
#include "intrinsic/geometry/internal/kernel_3/exact_predicates_inexact_constructions_kernel.h"
#include "intrinsic/geometry/internal/mesh/mesh.h"
#include "intrinsic/geometry/internal/surface_mesh_3/surface_mesh_3d.h"
#include "intrinsic/geometry/internal/surface_mesh_3/unary_predicates.h"
#include "intrinsic/geometry/internal/util/cgal_utils.h"
#include "intrinsic/geometry/shapes/point_cloud.h"
#include "intrinsic/util/object_store/object_ref.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic::geo {
namespace {

absl::StatusOr<bool> IsConvexMesh(const Mesh& mesh,
                                  double relative_volume_tolerance) {
  if (mesh.empty()) {
    return false;
  }
  const EpicSurfaceMesh3 surface_mesh = ToEpicSurfaceMesh3(mesh);
  if (surface_mesh.is_empty() || !IsClosed(surface_mesh)) {
    return false;
  }

  // The sign of the volume depends on the orientation of the faces.
  INTR_ASSIGN_OR_RETURN(
      const double mesh_volume, ProtectedCGALCall([&]() {
        return std::abs(CGAL::to_double(
            CGAL::Polygon_mesh_processing::volume(surface_mesh)));
      }));
  if (mesh_volume <= 0.0) {
    return false;
  }

  const std::vector<EpicPoint3> points = EpicExtractAllPoint3(mesh);
  INTR_ASSIGN_OR_RETURN(
      const double hull_volume, ProtectedCGALCall([&]() {
        EpicSurfaceMesh3 hull;
        CGAL::convex_hull_3(points.begin(), points.end(), hull);
        return std::abs(
            CGAL::to_double(CGAL::Polygon_mesh_processing::volume(hull)));
      }));
  if (hull_volume <= 0.0) {
    return false;
  }

  return hull_volume - mesh_volume <= relative_volume_tolerance * hull_volume;
}

class IsConvexFunctor {
 public:
  explicit IsConvexFunctor(double relative_volume_tolerance)
      : relative_volume_tolerance_(relative_volume_tolerance) {}

  template <typename Geo>
  absl::StatusOr<bool> operator()(const Geo& geo) const {
    LOG(WARNING) << "WARNING: intrinsic::IsConvexFunctor does not support\n"
                 << "operator()(const Geo&)) const, with \n"
                 << "Geo = " << Demangle<Geo>();
    return absl::InvalidArgumentError(
        "IsConvex does not support argument type.");
  }

  template <>
  absl::StatusOr<bool> operator()(const ObjectRef<Mesh>& geo) const {
    return IsConvexMesh(geo.Value(), relative_volume_tolerance_);
  }

  template <>
  absl::StatusOr<bool> operator()(const ObjectRef<PointCloud>& geo) const {
    return false;
  }

 private:
  double relative_volume_tolerance_;
};

}  // namespace

absl::StatusOr<bool> IsConvex(const Geometry& geo,
                              double relative_volume_tolerance) {
  if (relative_volume_tolerance < 0.0) {
    return absl::InvalidArgumentError(
        "relative_volume_tolerance must not be negative.");
  }
  return geo.GetExactGeometry().visit(
      IsConvexFunctor(relative_volume_tolerance));
}

}  // namespace intrinsic::geo
