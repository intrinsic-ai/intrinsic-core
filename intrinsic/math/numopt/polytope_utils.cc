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

#include "intrinsic/math/numopt/polytope_utils.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <memory>
#include <vector>

#include "CGAL/Convex_hull_3/dual/halfspace_intersection_3.h"
#include "CGAL/Exact_predicates_inexact_constructions_kernel.h"
#include "CGAL/Polyhedron_3.h"
#include "Eigen/LU"
#include "Eigen/QR"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/math/almost_equals.h"
#include "intrinsic/math/numopt/incremental_convex_hull_3d.h"
#include "intrinsic/math/numopt/linear_constraint_utils.h"
#include "intrinsic/util/status/status_macros.h"
#include "libqhullcpp/Qhull.h"
#include "libqhullcpp/QhullFacetList.h"
#include "libqhullcpp/QhullPoint.h"

namespace intrinsic {
namespace {

// The geometric kernel for geometric computations based on CGAL. We use the
// Exact_predicates_inexact_constructions_kernel (EPICK) because:
// - Predicates are computed exactly to avoid topological inconsistencies,
// - Constructions (e.g., computing an intersection point) use `double` for
//   high performance, accepting minor floating-point drift.
using CGALKernel = CGAL::Exact_predicates_inexact_constructions_kernel;

// Minimum distance of an edge (e.g. from the origin in dual space to a
// hyperplane). Values below this represent vertices at infinity, which are
// skipped to handle unbounded regions.
constexpr double kMinimumEdgeLength = 1.0e-12;

// How far a vertex reported by `kIncrementalDualHull` may lie outside a
// halfspace, relative to the magnitudes entering the residual, before the
// result is rejected.
constexpr double kFeasibilityTolerance = 1.0e-9;

// Minimum absolute determinant of the column-equilibrated 3x3 facet normal
// matrix required to solve the system directly via the closed-form 3x3
// adjugate/cross-product formula instead of falling back to `Eigen::FullPivLU`.
// After column equilibration each column has infinity-norm 1, so by Hadamard's
// inequality `|det| <= 3 * sqrt(3)` and this threshold is scale-free.
constexpr double kMinDeterminantForDirectInverse = 1.0e-9;

// Validates that
// - The `matrix_a` has non-zero dimensions.
// - Input dimensions of `matrix_a`, `vector_b`, and `interior_point` are
//   consistent.
// - The `interior_point` is strictly interior.
template <typename DerivedA, typename DerivedB, typename DerivedI>
absl::Status ValidateHalfspaceRepresentationData(
    const Eigen::MatrixBase<DerivedA>& matrix_a,
    const Eigen::MatrixBase<DerivedB>& vector_b,
    const Eigen::MatrixBase<DerivedI>& interior_point) {
  const Eigen::Index point_dimension = matrix_a.cols();
  const Eigen::Index num_constraints = matrix_a.rows();
  if (point_dimension < 1 || num_constraints < 1) {
    return absl::InvalidArgumentError(absl::StrCat(
        "The `matrix_a` cannot have zero size. Got ", num_constraints,
        " constraints and ", point_dimension, " point dimension."));
  }
  if (num_constraints != vector_b.size()) {
    return absl::InvalidArgumentError(absl::StrCat(
        "The number of rows of `matrix_a` and `vector_b` must match. Got ",
        num_constraints, " vs. ", vector_b.size(), "."));
  }
  if (num_constraints <= point_dimension) {
    return absl::InvalidArgumentError(absl::StrCat(
        "A bounded intersection in ", point_dimension,
        "-dimensional space requires at least ", point_dimension + 1,
        " constraints. Got ", num_constraints, "."));
  }
  if (point_dimension != interior_point.size()) {
    return absl::InvalidArgumentError(
        absl::StrCat("The columns of `matrix_a` and the size of "
                     "`interior_point` must match. Got ",
                     point_dimension, " vs. ", interior_point.size(), "."));
  }
  constexpr double kTolerance = -std::numeric_limits<double>::epsilon();
  for (Eigen::Index i = 0; i < num_constraints; ++i) {
    const double violation = matrix_a.row(i).dot(interior_point) - vector_b(i);
    if (violation >= kTolerance) {
      return absl::InvalidArgumentError(
          absl::StrCat("The `interior_point` is not strictly interior. It "
                       "violates constraint at row ",
                       i, " by ", violation, "."));
    }
  }
  // Check if the normal vectors span the N-dimensional space.
  // If rank(A) < N, the intersection has no vertices (it's an infinite
  // cylinder/corridor) and Qhull will crash attempting to process perfectly
  // collinear dual points.
  // Fast check: if constraints have opposing axis-aligned bounds in all
  // dimensions, rank is guaranteed to be full, and the expensive QR rank
  // decomposition can be skipped.
  if (!HasOpposingBoundsOnAllAxes(matrix_a)) {
    Eigen::ColPivHouseholderQR<typename DerivedA::PlainObject> qr(matrix_a);
    if (qr.rank() < point_dimension) {
      return absl::InternalError(
          "Halfspace intersection is empty or unbounded.");
    }
  }
  return absl::OkStatus();
}

// Computes the H-representation (`Ax <= b`) from a counter-clockwise ordered
// list of 2D convex hull vertices and populates the result object.
absl::Status PopulateHRepresentationFromCCWVertices(
    ConvexHullResult2d& result) {
  const int num_hull_vertices = result.vertices.size();
  if (num_hull_vertices <= 2) {
    return absl::InternalError(
        absl::StrCat("The number of hull vertices is ", num_hull_vertices,
                     ", which implies a degenerate geometry."));
  }

  // Prune consecutive vertices that are closer than `kMinimumEdgeLength`.
  HalfspaceIntersection2d valid_vertices;
  valid_vertices.reserve(num_hull_vertices);
  for (int i = 0; i < num_hull_vertices; ++i) {
    const eigenmath::Vector2d& p1 = result.vertices[i];
    const eigenmath::Vector2d& p2 =
        result.vertices[(i + 1) % num_hull_vertices];
    if ((p2 - p1).norm() >= kMinimumEdgeLength) {
      valid_vertices.push_back(p1);
    }
  }

  // Check wrap-around if the first and last remaining vertices became adjacent.
  while (valid_vertices.size() > 2 &&
         (valid_vertices.front() - valid_vertices.back()).norm() <
             kMinimumEdgeLength) {
    valid_vertices.pop_back();
  }

  const int num_valid_vertices = valid_vertices.size();
  if (num_valid_vertices <= 2) {
    return absl::InternalError(
        absl::StrCat("The number of valid facets is ", num_valid_vertices,
                     ", which implies a degenerate geometry."));
  }

  // Compute H-representation from `valid_vertices`.
  result.A.resize(num_valid_vertices, 2);
  result.b.resize(num_valid_vertices);

  for (int i = 0; i < num_valid_vertices; ++i) {
    const eigenmath::Vector2d& p1 = valid_vertices[i];
    const eigenmath::Vector2d& p2 =
        valid_vertices[(i + 1) % num_valid_vertices];
    const eigenmath::Vector2d delta = p2 - p1;
    const double length = delta.norm();

    // Outward normal for counter-clockwise points.
    const double inv_length = 1.0 / length;
    const eigenmath::Vector2d normal(delta.y() * inv_length,
                                     -delta.x() * inv_length);
    result.A.row(i) = normal;
    result.b(i) = normal.dot(p1);
  }

  result.vertices = std::move(valid_vertices);
  return absl::OkStatus();
}

// Computes the 2D cross product of vectors OA and OB.
//
// This is mathematically equivalent to the determinant of the 2x2 matrix formed
// by vectors (A - O) and (B - O). It is primarily used to determine the
// relative orientation of the points (O, A, B):
//  * > 0: Counter-clockwise (left) turn from OA to OB.
//  * < 0: Clockwise (right) turn from OA to OB.
//  * = 0: The points O, A, and B are collinear.
inline double CrossProduct(const eigenmath::Vector2d& o,
                           const eigenmath::Vector2d& a,
                           const eigenmath::Vector2d& b) {
  return (a.x() - o.x()) * (b.y() - o.y()) - (a.y() - o.y()) * (b.x() - o.x());
}

// Computes the vertices of a convex polyhedron defined by a set of linear
// inequalities `A*x <= b` (defined by `matrix_a` and `vector_b`). It leverages
// the Qhull library and the mathematical principle of polar duality to
// transform the intersection problem into a convex hull problem.
template <typename OutputVectorType, typename DerivedA, typename DerivedB,
          typename DerivedI>
absl::StatusOr<std::vector<OutputVectorType>> ComputeHalfspaceIntersectionQHull(
    const Eigen::MatrixBase<DerivedA>& matrix_a,
    const Eigen::MatrixBase<DerivedB>& vector_b,
    const Eigen::MatrixBase<DerivedI>& interior_point) {
  INTR_RETURN_IF_ERROR(
      ValidateHalfspaceRepresentationData(matrix_a, vector_b, interior_point));

  // Flatten the constraints into a format [a_{i1}, a_{i2}, ..., a_{iN}, -b_i]
  const int point_dimension = matrix_a.cols();
  const int num_constraints = matrix_a.rows();
  constexpr int kColsAtCompileTime =
      (DerivedA::ColsAtCompileTime == Eigen::Dynamic)
          ? Eigen::Dynamic
          : DerivedA::ColsAtCompileTime + 1;
  eigenmath::Matrix<double, Eigen::Dynamic, kColsAtCompileTime, Eigen::RowMajor>
      halfspace_data(num_constraints, point_dimension + 1);
  halfspace_data.leftCols(point_dimension) = matrix_a;
  halfspace_data.rightCols(1) = -vector_b;

  // Command "H" with the interior point coordinates.
  std::string h_cmd;
  if constexpr (DerivedI::RowsAtCompileTime == 2) {
    h_cmd = absl::StrFormat("H%.17g,%.17g Qx", interior_point[0],
                            interior_point[1]);
  } else if constexpr (DerivedI::RowsAtCompileTime == 3) {
    h_cmd = absl::StrFormat("H%.17g,%.17g,%.17g Qx", interior_point[0],
                            interior_point[1], interior_point[2]);
  } else {
    // Fallback for dynamic N-dimensional spaces.
    h_cmd =
        absl::StrCat("H",
                     absl::StrJoin(interior_point, ",",
                                   [](std::string* out, double value) {
                                     absl::StrAppendFormat(out, "%.17g", value);
                                   }),
                     " Qx");
  }

  orgQhull::Qhull qhull;
  std::stringstream qhull_error_stream;
  try {
    // Redirect error output to inspect detailed geometric errors on failure.
    qhull.setErrorStream(&qhull_error_stream);

    // Compute the facets of the dual shape, which correspond to the
    // vertices of the original intersection.
    qhull.runQhull("", point_dimension + 1, num_constraints,
                   halfspace_data.data(), h_cmd.c_str());
  }
  // Capture orgQhull::QhullError and convert them into absl::Status messages,
  // providing detailed error stream logs if the geometric calculation fails.
  catch (const orgQhull::QhullError& e) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Qhull error: ", e.what(), " | Details: ", qhull_error_stream.str()));
  } catch (const std::exception& e) {
    return absl::InternalError(
        absl::StrCat("Standard exception in Qhull: ", e.what()));
  }

  // Vertex recovery: Iterates through the resulting facets. It filters out
  // "Upper Delaunay" facets (intersections at infinity) and recovers the
  // vertex coordinates by un-projecting the dual facets using the formula:
  // `vertex = interior_point - normal / offset`.
  std::vector<OutputVectorType> intersection_points;
  intersection_points.reserve(qhull.facetCount());
  for (const orgQhull::QhullFacet& facet : qhull.facetList()) {
    if (facet.isUpperDelaunay()) {
      continue;
    }

    // Check that the signed perpendicular distance from the origin to the
    // hyperplane is above a threshold. If this is below the threshold, the
    // dual point is at infinity and thus not relevant.
    const orgQhull::QhullHyperplane& hyperplane = facet.hyperplane();
    const double offset = hyperplane.offset();
    if (std::abs(offset) < kMinimumEdgeLength) continue;

    const double inv_offset = 1.0 / offset;
    Eigen::Map<const OutputVectorType> normal(
        hyperplane.coordinates(),
        OutputVectorType::RowsAtCompileTime == Eigen::Dynamic
            ? point_dimension
            : OutputVectorType::RowsAtCompileTime);
    intersection_points.push_back(interior_point - normal * inv_offset);
  }
  if (intersection_points.empty()) {
    // It should not happen, but it is included as an extra safety layer.
    return absl::InternalError("Halfspace intersection is empty or unbounded.");
  }

  return intersection_points;
}

// Computes the vertices of the polyhedron `A * x <= b` using polar duality
// around the `interior_point`, with an incremental 3D solver.
//
// Internally, it maps halfspace `i` to the dual point `a_i / (b_i - a_i *
// interior_point)`, and every facet of the convex hull of those dual points
// maps back to one vertex of the original polyhedron.
absl::StatusOr<HalfspaceIntersection3d> ComputeHalfspaceIntersectionDualHull(
    const Eigen::Ref<const eigenmath::MatrixX3d>& matrix_a,
    const Eigen::Ref<const eigenmath::VectorXd>& vector_b,
    const Eigen::Ref<const eigenmath::Vector3d>& interior_point) {
  INTR_RETURN_IF_ERROR(
      ValidateHalfspaceRepresentationData(matrix_a, vector_b, interior_point));

  const int num_constraints = matrix_a.rows();
  std::vector<eigenmath::Vector3d> dual_points;
  dual_points.reserve(num_constraints);
  for (int i = 0; i < num_constraints; ++i) {
    // The `margin` is strictly positive because `interior_point` was validated
    // to be strictly interior, so the dual point is always well defined.
    const eigenmath::Vector3d& normal = matrix_a.row(i).transpose();
    const double margin = vector_b(i) - normal.dot(interior_point);
    dual_points.push_back(normal / margin);
  }

  INTR_ASSIGN_OR_RETURN(const std::vector<ConvexHullFacetPlane3d> facet_planes,
                        ComputeConvexHullFacetPlanes3d(dual_points));

  // Each dual facet is spanned by three dual points, which correspond to three
  // halfspaces of the original problem, and the primal vertex is where those
  // three planes meet. Recover it by solving that 3x3 system on the original
  // `matrix_a` and `vector_b` rather than by inverting the dual plane.
  //
  // Going back through the dual plane as `interior_point + normal / offset`
  // amplifies any error in the plane by `1 / offset^2`, and the plane itself is
  // a normalized cross product of three dual points. The hull is simplicial, so
  // a group of coplanar dual points gets triangulated into slivers whose cross
  // product cancels catastrophically. Constraint sets here routinely produce
  // such groups (opposing box bounds, octagon rows for the Cartesian limits),
  // so that path loses many digits exactly where the polytope is most
  // anisotropic. The 3x3 solve instead consumes the input data unmodified.
  HalfspaceIntersection3d intersection_points;
  intersection_points.reserve(facet_planes.size());
  eigenmath::Matrix3d facet_normals;
  eigenmath::Vector3d facet_offsets;
  // Row data for the feasibility filter below, hoisted out of the loop.
  const eigenmath::VectorXd row_norms = matrix_a.rowwise().norm();
  const eigenmath::VectorXd absolute_offsets = vector_b.cwiseAbs();
  for (const ConvexHullFacetPlane3d& facet_plane : facet_planes) {
    for (int k = 0; k < 3; ++k) {
      const int constraint_index = facet_plane.point_indices[k];
      facet_normals.row(k) = matrix_a.row(constraint_index);
      facet_offsets(k) = vector_b(constraint_index);
    }

    // Equilibrate the columns before factorizing. The three unknowns are
    // `(b, bp, bpp)`, whose coefficients scale as `O(1)`, `O(ds)` and
    // `O(ds^2)`, so at the step sizes used here the raw system carries a
    // condition number of `1e6` or more purely from that spread, before any
    // geometry is taken into account. Solving for the rescaled unknowns
    // removes that artificial part of the conditioning, and it also makes the
    // rank test below scale free, since otherwise the pivot threshold is
    // dominated by the largest column.
    eigenmath::Vector3d column_scales;
    for (int j = 0; j < 3; ++j) {
      const double column_magnitude =
          facet_normals.col(j).cwiseAbs().maxCoeff();
      column_scales(j) = column_magnitude > 0.0 ? column_magnitude : 1.0;
    }
    const eigenmath::Matrix3d scaled_normals =
        facet_normals * column_scales.cwiseInverse().asDiagonal();

    // Direct 3x3 adjugate / Cramer inversion on the equilibrated 3x3 matrix:
    // for `M = [c0, c1, c2]`, `adj(M)` has rows `(c1 x c2)^T`, `(c2 x c0)^T`,
    // and `(c0 x c1)^T`, satisfying `adj(M) * M = det(M) * I`, followed by one
    // step of iterative refinement in the equilibrated system.
    const eigenmath::Vector3d c0 = scaled_normals.col(0);
    const eigenmath::Vector3d c1 = scaled_normals.col(1);
    const eigenmath::Vector3d c2 = scaled_normals.col(2);
    const eigenmath::Vector3d c1_cross_c2 = c1.cross(c2);
    const double det = c0.dot(c1_cross_c2);
    eigenmath::Vector3d scaled_vertex;
    if (std::abs(det) > kMinDeterminantForDirectInverse) {
      const double inv_det = 1.0 / det;
      eigenmath::Matrix3d adjugate;
      adjugate.row(0) = c1_cross_c2.transpose();
      adjugate.row(1) = c2.cross(c0).transpose();
      adjugate.row(2) = c0.cross(c1).transpose();
      scaled_vertex = (adjugate * facet_offsets) * inv_det;
      const eigenmath::Vector3d scaled_residual =
          facet_offsets - scaled_normals * scaled_vertex;
      scaled_vertex += (adjugate * scaled_residual) * inv_det;
    } else {
      const Eigen::FullPivLU<eigenmath::Matrix3d> plane_intersection(
          scaled_normals);
      // A singular triple means the three halfspaces have no single common
      // point. That is the unbounded case the offset test used to catch, except
      // that rank is scale-free, whereas comparing the dual offset against an
      // absolute threshold was not.
      if (!plane_intersection.isInvertible()) continue;
      scaled_vertex = plane_intersection.solve(facet_offsets);
      // One step of iterative refinement. The residual is formed in the scaled
      // system, where the entries are comparable in magnitude and the
      // subtraction therefore keeps its significant digits.
      scaled_vertex += plane_intersection.solve(facet_offsets -
                                                scaled_normals * scaled_vertex);
    }
    const eigenmath::Vector3d vertex =
        scaled_vertex.cwiseQuotient(column_scales);
    if (!vertex.allFinite()) continue;

    // Keep the point only if it satisfies the defining property of a vertex,
    // namely that it lies in every halfspace. The hull is simplicial, so a set
    // of coplanar dual points is triangulated rather than merged into one
    // facet, and the slivers in that triangulation correspond to triples of
    // primal planes that share a common line instead of a common point. Those
    // are only borderline singular, so they survive the rank test above and
    // then solve to a point far outside the polyhedron.
    //
    // The bound scales with `|b_i| + |a_i| * |v|`, which is the magnitude of
    // the terms that cancel when forming the residual, so it tracks the
    // rounding error actually available on that row. Deciding per vertex
    // matters: a shared bound driven by the largest vertex is exactly the
    // bound that the spurious far away vertices inflate themselves, so it
    // stops rejecting the points it exists to reject.
    const double v_norm = vertex.norm();
    bool lies_in_every_halfspace = true;
    for (int i = 0; i < num_constraints; ++i) {
      if (!(matrix_a.row(i).dot(vertex) - vector_b(i) <=
            kFeasibilityTolerance *
                (absolute_offsets(i) + row_norms(i) * v_norm))) {
        lies_in_every_halfspace = false;
        break;
      }
    }
    if (!lies_in_every_halfspace) continue;

    intersection_points.push_back(vertex);
  }
  if (intersection_points.empty()) {
    return absl::InternalError("Halfspace intersection is empty or unbounded.");
  }

  return intersection_points;
}

// Computes both the half-space representation (H-rep: Ax <= b) and the vertex
// representation (V-rep) of the convex hull for the provided
// `candidate_vertices` using the Qhull library.
template <typename VectorType>
absl::StatusOr<ConvexHullResult<VectorType::RowsAtCompileTime>>
ComputeConvexHullQHull(const std::vector<VectorType>& candidate_vertices) {
  constexpr int Dim = VectorType::RowsAtCompileTime;
  const int num_points = candidate_vertices.size();
  if (num_points < 1) {
    return absl::InvalidArgumentError(
        "The `candidate_vertices` cannot be empty.");
  }
  const int point_dimension =
      (Dim == Eigen::Dynamic) ? candidate_vertices.front().size() : Dim;
  if constexpr (Dim == Eigen::Dynamic) {
    for (int id = 1; id < num_points; ++id) {
      if (candidate_vertices[id].size() != point_dimension) {
        return absl::InvalidArgumentError(absl::StrCat(
            "The `candidate_vertex`[", id,
            "] has incorrect point dimension. Expected: ", point_dimension,
            ", got: ", candidate_vertices[id].size(), "."));
      }
    }
  }

  // Determine the memory layout to pass to Qhull. Use unique_ptr to avoid the
  // O(N) zero-initialization overhead of std::vector::resize().
  const double* qhull_input_data = nullptr;
  std::unique_ptr<double[]> flat_data_buffer;

  // Check if the std::vector memory is strictly contiguous without padding.
  if constexpr (Dim != Eigen::Dynamic &&
                sizeof(VectorType) == Dim * sizeof(double)) {
    // Zero copy: The memory is already a perfectly flat array of coordinates.
    qhull_input_data = candidate_vertices.front().data();
  } else {
    // Fallback for dynamic vectors (VectorXd) or padded structs.
    flat_data_buffer.reset(new double[num_points * point_dimension]);
    double* data_ptr = flat_data_buffer.get();
    for (const auto& vertex : candidate_vertices) {
      std::copy_n(vertex.data(), point_dimension, data_ptr);
      data_ptr += point_dimension;
    }
    qhull_input_data = flat_data_buffer.get();
  }

  orgQhull::Qhull qhull;
  std::stringstream qhull_error_stream;
  try {
    // Redirect error output to inspect detailed geometric errors on failure.
    qhull.setErrorStream(&qhull_error_stream);

    // Initialize Qhull with the coordinates using "Qx" for exact pre-merging
    // to ensure minimal bounding facets are generated.
    qhull.runQhull("", point_dimension, num_points, qhull_input_data, "Qx");
  }
  // Capture orgQhull::QhullError and convert them into absl::Status messages,
  // providing detailed error stream logs if the geometric calculation fails.
  catch (const orgQhull::QhullError& e) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Qhull error: ", e.what(), " | Details: ", qhull_error_stream.str()));
  } catch (const std::exception& e) {
    return absl::InternalError(
        absl::StrCat("Standard exception in Qhull: ", e.what()));
  }

  // Extract the equations of the facets (Ax <= b). In Qhull, the equation is:
  // Ax + offset = 0. To obtain the form Ax <= b, we set b = -offset.
  ConvexHullResult<Dim> result;
  result.A.resize(qhull.facetCount(), point_dimension);
  result.b.resize(qhull.facetCount());
  int num_used_facets = 0;
  for (const orgQhull::QhullFacet& facet : qhull.facetList()) {
    if (facet.isUpperDelaunay()) {
      continue;
    }

    orgQhull::QhullHyperplane hyperplane = facet.hyperplane();
    // Map as RowVectorXd to match the row() assignment.
    result.A.row(num_used_facets) =
        Eigen::Map<const Eigen::RowVector<double, Dim>>(
            hyperplane.coordinates(),
            Dim == Eigen::Dynamic ? point_dimension : Dim);
    result.b(num_used_facets) = -hyperplane.offset();
    num_used_facets++;
  }
  result.A.conservativeResize(num_used_facets, Eigen::NoChange);
  result.b.conservativeResize(num_used_facets);

  // Extract also the vertices of the envelope.
  result.vertices.reserve(qhull.vertexCount());
  for (const orgQhull::QhullVertex& vertex : qhull.vertexList()) {
    const orgQhull::QhullPoint& point = vertex.point();
    // Map as VectorXd to match the col() assignment.
    result.vertices.emplace_back(
        Eigen::Map<const eigenmath::Vector<double, Dim>>(point.coordinates(),
                                                         point_dimension));
  }

  return result;
}

// Computes the vertices of a convex polyhedron defined by a set of linear
// inequalities `A*x <= b` (defined by `matrix_a` and `vector_b`) in 3D.
absl::StatusOr<HalfspaceIntersection3d> ComputeHalfspaceIntersectionCGAL(
    const Eigen::Ref<const eigenmath::MatrixX3d>& matrix_a,
    const Eigen::Ref<const eigenmath::VectorXd>& vector_b,
    const Eigen::Ref<const eigenmath::Vector3d>& interior_point) {
  INTR_RETURN_IF_ERROR(
      ValidateHalfspaceRepresentationData(matrix_a, vector_b, interior_point));

  // Map halfspace information `A * x <= b` to CGAL Planes. In CGAL, a plane
  // is defined as `ax + by + cz + d = 0`. The halfspace is defined as `ax +
  // by + cz + d <= 0`. Thus: `A(i,0) * x + A(i,1) * y + A(i,2) * z - b(i) <=
  // 0`.
  std::vector<CGALKernel::Plane_3> planes;
  planes.reserve(matrix_a.rows());
  for (int row_id = 0; row_id < matrix_a.rows(); ++row_id) {
    planes.emplace_back(matrix_a(row_id, 0), matrix_a(row_id, 1),
                        matrix_a(row_id, 2), -vector_b(row_id));
  }
  const CGALKernel::Point_3 center(interior_point(0), interior_point(1),
                                   interior_point(2));

  // Compute the intersection and store the result in a Polyhedron.
  CGAL::Polyhedron_3<CGALKernel> polyhedron;
  try {
    CGAL::halfspace_intersection_3(planes.begin(), planes.end(), polyhedron,
                                   center);
  } catch (const std::exception& e) {
    return absl::InternalError(
        absl::StrCat("CGAL 3D halfspace intersection failed: ", e.what()));
  }
  if (polyhedron.empty()) {
    // It should not happen, but it is included as an extra safety layer.
    return absl::InternalError("Halfspace intersection is empty or unbounded.");
  }

  HalfspaceIntersection3d intersection_points;
  intersection_points.reserve(polyhedron.size_of_vertices());
  for (auto vertex_iter = polyhedron.vertices_begin();
       vertex_iter != polyhedron.vertices_end(); ++vertex_iter) {
    const CGALKernel::Point_3& vertex_3d = vertex_iter->point();
    intersection_points.emplace_back(CGAL::to_double(vertex_3d.x()),
                                     CGAL::to_double(vertex_3d.y()),
                                     CGAL::to_double(vertex_3d.z()));
  }
  return intersection_points;
}

absl::StatusOr<ConvexHullResult2d> ComputeConvexHullCGAL(
    const HalfspaceIntersection2d& candidate_vertices) {
  if (candidate_vertices.empty()) {
    return absl::InvalidArgumentError(
        "The `candidate_vertices` cannot be empty.");
  }

  // Prepare the data in the form of CGAL Point_2 objects.
  std::vector<CGALKernel::Point_2> points;
  points.reserve(candidate_vertices.size());
  for (const eigenmath::Vector2d& candidate_vertex_2d : candidate_vertices) {
    points.emplace_back(candidate_vertex_2d[0], candidate_vertex_2d[1]);
  }

  // Compute the convex hull: the vertices follow counter-clockwise order.
  std::vector<CGALKernel::Point_2> hull_vertices;
  hull_vertices.reserve(candidate_vertices.size());
  CGAL::convex_hull_2(points.begin(), points.end(),
                      std::back_inserter(hull_vertices));

  // Extract the vertices (V-representation).
  ConvexHullResult2d result;
  result.vertices.reserve(hull_vertices.size());
  for (const CGALKernel::Point_2& vertex_2d : hull_vertices) {
    result.vertices.emplace_back(CGAL::to_double(vertex_2d.x()),
                                 CGAL::to_double(vertex_2d.y()));
  }

  INTR_RETURN_IF_ERROR(PopulateHRepresentationFromCCWVertices(result));
  return result;
}

// Fast and direct computation of 2D convex hulls for `candidate_vertices` using
// Andrew's Monotone Chain algorithm (`O(N log N)`).
absl::StatusOr<ConvexHullResult2d> ComputeConvexHullAndrewMonotoneChain(
    const HalfspaceIntersection2d& candidate_vertices) {
  const int num_candidate_vertices = candidate_vertices.size();
  if (num_candidate_vertices < 1) {
    return absl::InvalidArgumentError(
        "The `candidate_vertices` cannot be empty.");
  }
  if (num_candidate_vertices <= 2) {
    return absl::InternalError(absl::StrCat(
        "The number of candidate vertices is ", num_candidate_vertices,
        ", which implies a degenerate geometry."));
  }

  // Sort lexicographically. Points are sorted primarily by their x-coordinate.
  // If the x-coordinates are equal, they are sorted by their y-coordinate
  // (bottom-to-top).
  HalfspaceIntersection2d points_2d = candidate_vertices;
  std::sort(points_2d.begin(), points_2d.end(),
            [](const eigenmath::Vector2d& a, const eigenmath::Vector2d& b) {
              if (a.x() < b.x()) return true;
              if (b.x() < a.x()) return false;
              return a.y() < b.y();
            });

  // Deduplicate points.
  points_2d.erase(
      std::unique(
          points_2d.begin(), points_2d.end(),
          [](const eigenmath::Vector2d& a, const eigenmath::Vector2d& b) {
            return AlmostEquals(a.x(), b.x()) && AlmostEquals(a.y(), b.y());
          }),
      points_2d.end());

  const int num_points_2d = points_2d.size();
  if (num_points_2d <= 2) {
    return absl::InternalError(
        absl::StrCat("The number of unique vertices is ", num_points_2d,
                     ", which implies a degenerate geometry."));
  }

  HalfspaceIntersection2d hull_vertices(2 * num_points_2d);
  int k = 0;

  // Build the lower half of the convex hull. By processing the sorted points
  // from left to right, we construct the bottom boundary. We enforce that every
  // sequence of three consecutive points makes a strictly counter-clockwise
  // (left) turn. If the cross product is <= 0, the points form a right turn (a
  // concave indentation) or a flat line, so we pop the previous point from the
  // hull.
  for (int i = 0; i < num_points_2d; ++i) {
    while (k >= 2 && CrossProduct(hull_vertices[k - 2], hull_vertices[k - 1],
                                  points_2d[i]) <= 0.0) {
      k--;
    }
    hull_vertices[k] = points_2d[i];
    k += 1;
  }

  // Build the upper half of the convex hull. We process the same points in
  // reverse order (right to left) to construct the top boundary. Just like the
  // lower hull, moving right-to-left while enforcing counter-clockwise turns
  // creates a convex outer shell. The boundary variable `t = k + 1` prevents
  // the algorithm from popping vertices that belong to the already-completed
  // lower hull.
  for (int i = num_points_2d - 2, t = k + 1; i >= 0; --i) {
    while (k >= t && CrossProduct(hull_vertices[k - 2], hull_vertices[k - 1],
                                  points_2d[i]) <= 0.0) {
      k--;
    }
    hull_vertices[k] = points_2d[i];
    k += 1;
  }

  hull_vertices.resize(k - 1);

  ConvexHullResult2d result;
  result.vertices = std::move(hull_vertices);
  INTR_RETURN_IF_ERROR(PopulateHRepresentationFromCCWVertices(result));
  return result;
}

}  // namespace

absl::StatusOr<HalfspaceIntersection2d> ComputeHalfspaceIntersection2d(
    const Eigen::Ref<const eigenmath::MatrixX2d>& matrix_a,
    const Eigen::Ref<const eigenmath::VectorXd>& vector_b,
    const Eigen::Ref<const eigenmath::Vector2d>& interior_point) {
  return ComputeHalfspaceIntersectionQHull<eigenmath::Vector2d>(
      matrix_a, vector_b, interior_point);
}

absl::StatusOr<HalfspaceIntersection3d> ComputeHalfspaceIntersection3d(
    const Eigen::Ref<const eigenmath::MatrixX3d>& matrix_a,
    const Eigen::Ref<const eigenmath::VectorXd>& vector_b,
    const Eigen::Ref<const eigenmath::Vector3d>& interior_point,
    const GeometrySolverType geometry_solver_type) {
  switch (geometry_solver_type) {
    case GeometrySolverType::kQHull: {
      return ComputeHalfspaceIntersectionQHull<eigenmath::Vector3d>(
          matrix_a, vector_b, interior_point);
    }
    case GeometrySolverType::kCGAL: {
      return ComputeHalfspaceIntersectionCGAL(matrix_a, vector_b,
                                              interior_point);
    }
    case GeometrySolverType::kIncrementalDualHull: {
      return ComputeHalfspaceIntersectionDualHull(matrix_a, vector_b,
                                                  interior_point);
    }
    default: {
      return absl::UnimplementedError("Geometry solver type not available.");
    }
  }
}

absl::StatusOr<HalfspaceIntersectionXd> ComputeHalfspaceIntersectionXd(
    const Eigen::Ref<const eigenmath::MatrixXd>& matrix_a,
    const Eigen::Ref<const eigenmath::VectorXd>& vector_b,
    const Eigen::Ref<const eigenmath::VectorXd>& interior_point) {
  return ComputeHalfspaceIntersectionQHull<eigenmath::VectorXd>(
      matrix_a, vector_b, interior_point);
}

absl::StatusOr<ConvexHullResult2d> ComputeConvexHull2d(
    const HalfspaceIntersection2d& candidate_vertices,
    const GeometrySolverType geometry_solver_type) {
  switch (geometry_solver_type) {
    case GeometrySolverType::kQHull: {
      return ComputeConvexHullQHull<eigenmath::Vector2d>(candidate_vertices);
    }
    case GeometrySolverType::kCGAL: {
      return ComputeConvexHullCGAL(candidate_vertices);
    }
    case GeometrySolverType::kAndrewMonotoneChain: {
      return ComputeConvexHullAndrewMonotoneChain(candidate_vertices);
    }
    default: {
      return absl::UnimplementedError("Geometry solver type not available.");
    }
  }
}

absl::StatusOr<ConvexHullResult3d> ComputeConvexHull3d(
    const HalfspaceIntersection3d& candidate_vertices) {
  return ComputeConvexHullQHull<eigenmath::Vector3d>(candidate_vertices);
}

absl::StatusOr<ConvexHullResultXd> ComputeConvexHullXd(
    const HalfspaceIntersectionXd& candidate_vertices) {
  return ComputeConvexHullQHull<eigenmath::VectorXd>(candidate_vertices);
}

}  // namespace intrinsic
