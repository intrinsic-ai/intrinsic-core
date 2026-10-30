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

#include "intrinsic/math/numopt/incremental_convex_hull_3d.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/types/span.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic {
namespace {

// A point lies outside a facet only once its signed distance exceeds this
// fraction of the magnitudes that produced it, which absorbs the rounding of
// the cross products building the facet planes. The scale is local to the facet
// and to the point rather than global: polar duals put points at radius `1 /
// distance_to_facet`, spanning many orders of magnitude, and a global bound
// would swallow the facets near the origin.
constexpr double kRelativeTolerance = 1.0e-13;

// Cap on the facets reserved up front. Larger hulls still work, they just grow
// their buffers as usual.
constexpr int kMaxReservedFacets = 1024;

// Slack allowed when checking that the finished hull contains every input
// point. It sits far above `kRelativeTolerance` so that the accumulated
// rounding of a long construction never trips it: the failure it looks for is
// a point left stranded well outside the hull by a misjudged visibility
// decision, which shows up at a relative excess of order one, not of order
// `1e-9`.
constexpr double kCompletenessTolerance = 1.0e-9;

// Facet of the hull under construction. Half-edge `k` runs from `vertices[k]`
// to `vertices[(k + 1) % 3]`, and `neighbors[k]` is the facet on the other side
// of that edge.
//
//                       e0 (0 -> 1)
//        vertices[0] ______________ vertices[1]
//                    \            /
//         e2          \          /          e1
//      (2 -> 0)        \        /        (1 -> 2)
//                       \      /
//                        \    /
//                         \  /
//                          \/
//                      vertices[2]
//
// The vertex order is the combinatorial orientation used to stitch new facets
// together; the plane is oriented separately against a known interior point,
// which keeps visibility tests correct even when a triangle is nearly
// degenerate.
struct Facet {
  std::array<int, 3> vertices;
  std::array<int, 3> neighbors;
  eigenmath::Vector3d normal;
  double offset;

  // The input point farthest outside this facet and its margin, maintained as
  // points are assigned so that expanding the hull never rescans the list.
  double farthest_margin;
  int farthest_point;

  // Head of the list of input points still strictly outside this facet, and
  // hence still candidates for becoming hull vertices; -1 when empty. The list
  // is threaded through `next_outside_point_` rather than held in a per-facet
  // container, which keeps `Facet` trivially copyable and allocation free.
  int outside_head;

  // Stamp left by the most recent visibility search that reached this facet,
  // encoding both that it was reached and what the outcome was.
  int visit_mark;

  bool deleted;
};

// A boundary edge of the visible region, directed as it appeared in the
// visible facet it came from.
struct HorizonEdge {
  int from_vertex;
  int to_vertex;

  // The facet on the far (non-visible) side, and the index of the half-edge
  // within it that borders the visible region.
  int outer_facet;
  int outer_edge;
};

// Builds the convex hull of a point set incrementally.
class ConvexHullBuilder {
 public:
  // Builds the convex hull of `points`.
  absl::Status Build(absl::Span<const eigenmath::Vector3d> points);

  // Returns the facets of the convex hull.
  const std::vector<Facet>& facets() const { return facets_; }

 private:
  absl::Status BuildInitialTetrahedron();
  absl::Status ExpandHull();

  // Returns by how much `point` clears the plane of `facet` beyond the rounding
  // of the terms measuring it: positive exactly when the point counts as
  // outside, and comparable across facets to pick the "most outside" one.
  double OutsideMargin(const Facet& facet,
                       const eigenmath::Vector3d& point) const {
    const double projection = facet.normal.dot(point);
    const double scale = std::abs(projection) + std::abs(facet.offset);
    return projection - facet.offset - kRelativeTolerance * scale;
  }

  // A facet reached by the current visibility search is stamped with `2 *
  // visit_id_`, plus one when it is visible. `visit_id_` is pre-incremented
  // before each search, so stamps only ever grow and the 0 of a freshly
  // allocated slot always reads as "not reached this round".
  bool WasVisited(const Facet& facet) const {
    return facet.visit_mark >= 2 * visit_id_;
  }
  bool IsVisible(const Facet& facet) const {
    return facet.visit_mark == 2 * visit_id_ + 1;
  }
  void MarkVisited(Facet& facet, const bool visible) const {
    facet.visit_mark = 2 * visit_id_ + (visible ? 1 : 0);
  }

  // Records `point_index` as still outside `facet_index`, with `margin` the
  // value `OutsideMargin()` returned for that pair, and keeps the facet's
  // cached farthest point up to date.
  void AddOutsidePoint(int facet_index, int point_index, double margin);

  // Allocates a facet on `vertices`, orienting its plane away from
  // `interior_point_`. Returns the facet index, or -1 if the triangle is
  // degenerate.
  int AddFacet(int vertex_a, int vertex_b, int vertex_c);

  // Connects the half-edge `edge_index` of `facet_index` to the matching
  // half-edge of `other_facet_index`, which must traverse the same two
  // vertices in the opposite direction. Returns false if no such half-edge
  // exists.
  bool LinkFacets(int facet_index, int edge_index, int other_facet_index);

  // Collects the facets visible from `points_[point_index]`, starting the
  // search at `seed_facet`, into `visible_facets_`.
  void CollectVisibleFacets(int seed_facet, int point_index);

  // Derives `horizon_` from `visible_facets_`. Returns false if the boundary
  // of the visible region is not a single closed loop, which only happens when
  // rounding has made the facet planes mutually inconsistent.
  bool CollectHorizon();

  // Replaces the visible facets by a cone of new facets joining `point_index`
  // to the horizon.
  absl::Status ReplaceVisibleFacetsWithCone(int point_index);

  absl::Span<const eigenmath::Vector3d> points_;
  double tolerance_ = 0.0;

  // A point strictly inside the initial tetrahedron, and therefore inside
  // every later hull, used to orient facet planes outward.
  eigenmath::Vector3d interior_point_ = eigenmath::Vector3d::Zero();

  // Facets are addressed by slot index throughout, because appending to
  // `facets_` can reallocate. `AddFacet()` takes a slot from `free_facets_`
  // when one is available and appends otherwise:
  //
  //   slot:        0       1       2       3       4
  //            +-------+-------+-------+-------+-------+
  //   facets_  |  F0   |  F1   |  F2   |  F3   |  F4   |
  //            +-------+-------+-------+-------+-------+
  //                                        ^
  //   free_facets_ = [ 3 ] ----------------+
  //
  // Each facet owns the points still outside it as a list threaded through
  // `next_outside_point_`, so that no facet ever allocates:
  //
  //   F0.outside_head = 5 --> next[5] = 2 --> next[2] = -1
  //   F1.outside_head = 7 --> next[7] = -1
  std::vector<Facet> facets_;
  std::vector<int> free_facets_;

  // Threads the per-facet outside lists: `next_outside_point_[p]` is the point
  // after `p` in the list holding it, or -1 at the end. A point belongs to at
  // most one facet, so one entry per input point is enough.
  std::vector<int> next_outside_point_;

  // `horizon_edge_leaving_vertex_[v]` is the index within `horizon_` of the
  // edge that leaves vertex `v`, or -1 when no edge of the current horizon
  // does. Entries are cleared again after each expansion step.
  std::vector<int> horizon_edge_leaving_vertex_;

  // Facets whose outside set may be non-empty, i.e. the remaining work.
  std::vector<int> pending_facets_;

  // Counter identifying the current visibility search, compared against
  // `Facet::visit_mark`.
  int visit_id_ = 0;

  std::vector<int> visible_facets_;
  std::vector<HorizonEdge> horizon_;
  std::vector<int> new_facets_;
  std::vector<int> orphaned_points_;
};

void ConvexHullBuilder::AddOutsidePoint(const int facet_index,
                                        const int point_index,
                                        const double margin) {
  Facet& facet = facets_[facet_index];
  next_outside_point_[point_index] = facet.outside_head;
  facet.outside_head = point_index;
  if (margin > facet.farthest_margin) {
    facet.farthest_margin = margin;
    facet.farthest_point = point_index;
  }
}

int ConvexHullBuilder::AddFacet(const int vertex_a, const int vertex_b,
                                const int vertex_c) {
  const eigenmath::Vector3d& a = points_[vertex_a];
  const eigenmath::Vector3d& b = points_[vertex_b];
  const eigenmath::Vector3d& c = points_[vertex_c];

  eigenmath::Vector3d normal = (b - a).cross(c - a);
  const double norm = normal.norm();
  if (!(norm > 0.0) || !std::isfinite(norm)) {
    return -1;
  }
  normal /= norm;
  double offset = normal.dot(a);

  // Orient against the interior point rather than against the vertex winding.
  // The winding is still what stitches neighbours together, but it is the
  // unreliable one for slivers.
  if (normal.dot(interior_point_) > offset) {
    normal = -normal;
    offset = -offset;
  }

  int facet_index;
  if (free_facets_.empty()) {
    facet_index = facets_.size();
    facets_.emplace_back();
  } else {
    facet_index = free_facets_.back();
    free_facets_.pop_back();
  }

  Facet& facet = facets_[facet_index];
  facet.vertices = {vertex_a, vertex_b, vertex_c};
  facet.neighbors = {-1, -1, -1};
  facet.normal = normal;
  facet.offset = offset;
  facet.outside_head = -1;
  facet.farthest_point = -1;
  facet.farthest_margin = 0.0;
  facet.visit_mark = 0;
  facet.deleted = false;
  return facet_index;
}

// The two facets sharing an edge traverse it in opposite directions, which is
// what identifies the matching half-edge:
//
//        facet.vertices[k]          facet.vertices[k + 1]
//                  o --------------------> o
//                  ^         shared        |   facet, half-edge `edge_index`
//                  |          edge         v
//                  o <-------------------- o   other, half-edge `k`
//        other.vertices[k + 1]      other.vertices[k]
bool ConvexHullBuilder::LinkFacets(const int facet_index, const int edge_index,
                                   const int other_facet_index) {
  Facet& facet = facets_[facet_index];
  Facet& other = facets_[other_facet_index];
  const int from = facet.vertices[edge_index];
  const int to = facet.vertices[(edge_index + 1) % 3];
  for (int k = 0; k < 3; ++k) {
    if (other.vertices[k] == to && other.vertices[(k + 1) % 3] == from) {
      facet.neighbors[edge_index] = other_facet_index;
      other.neighbors[k] = facet_index;
      return true;
    }
  }
  return false;
}

absl::Status ConvexHullBuilder::BuildInitialTetrahedron() {
  const int num_points = points_.size();

  // Start from the extremes along each axis: they are cheap to find and give a
  // well separated pair to base the tetrahedron on.
  std::array<int, 6> extreme_points = {0, 0, 0, 0, 0, 0};
  for (int i = 1; i < num_points; ++i) {
    for (int axis = 0; axis < 3; ++axis) {
      if (points_[i][axis] < points_[extreme_points[2 * axis]][axis]) {
        extreme_points[2 * axis] = i;
      }
      if (points_[i][axis] > points_[extreme_points[2 * axis + 1]][axis]) {
        extreme_points[2 * axis + 1] = i;
      }
    }
  }

  int vertex_0 = -1;
  int vertex_1 = -1;
  double best_squared_distance = 0.0;
  for (int i = 0; i < 6; ++i) {
    for (int j = i + 1; j < 6; ++j) {
      const double squared_distance =
          (points_[extreme_points[i]] - points_[extreme_points[j]])
              .squaredNorm();
      if (squared_distance > best_squared_distance) {
        best_squared_distance = squared_distance;
        vertex_0 = extreme_points[i];
        vertex_1 = extreme_points[j];
      }
    }
  }
  if (vertex_0 < 0 || std::sqrt(best_squared_distance) <= tolerance_) {
    return absl::FailedPreconditionError(
        "All points of the convex hull are coincident.");
  }

  // Farthest point from the line through the first two.
  const eigenmath::Vector3d line_direction =
      (points_[vertex_1] - points_[vertex_0]).normalized();
  int vertex_2 = -1;
  double best_line_distance = 0.0;
  for (int i = 0; i < num_points; ++i) {
    const eigenmath::Vector3d offset_from_line = points_[i] - points_[vertex_0];
    const double distance =
        (offset_from_line -
         line_direction.dot(offset_from_line) * line_direction)
            .norm();
    if (distance > best_line_distance) {
      best_line_distance = distance;
      vertex_2 = i;
    }
  }
  if (vertex_2 < 0 || best_line_distance <= tolerance_) {
    return absl::FailedPreconditionError(
        "All points of the convex hull are collinear.");
  }

  // Farthest point from the plane through the first three.
  eigenmath::Vector3d plane_normal =
      (points_[vertex_1] - points_[vertex_0])
          .cross(points_[vertex_2] - points_[vertex_0]);
  const double plane_normal_norm = plane_normal.norm();
  if (!(plane_normal_norm > 0.0)) {
    return absl::FailedPreconditionError(
        "All points of the convex hull are collinear.");
  }
  plane_normal /= plane_normal_norm;
  int vertex_3 = -1;
  double best_plane_distance = 0.0;
  for (int i = 0; i < num_points; ++i) {
    const double distance =
        std::abs(plane_normal.dot(points_[i] - points_[vertex_0]));
    if (distance > best_plane_distance) {
      best_plane_distance = distance;
      vertex_3 = i;
    }
  }
  if (vertex_3 < 0 || best_plane_distance <= tolerance_) {
    return absl::FailedPreconditionError(
        "All points of the convex hull are coplanar.");
  }

  interior_point_ = 0.25 * (points_[vertex_0] + points_[vertex_1] +
                            points_[vertex_2] + points_[vertex_3]);

  // The fourth entry of each quadruple is the vertex opposite to the facet.
  // The facets must all be wound counter-clockwise as seen from outside so
  // that every shared edge is traversed in opposite directions by the two
  // facets holding it, which is what the neighbor linking and the horizon
  // traversal rely on.
  std::array<std::array<int, 4>, 4> tetrahedron_facets = {{
      {vertex_0, vertex_1, vertex_2, vertex_3},
      {vertex_0, vertex_1, vertex_3, vertex_2},
      {vertex_0, vertex_2, vertex_3, vertex_1},
      {vertex_1, vertex_2, vertex_3, vertex_0},
  }};
  for (std::array<int, 4>& facet_vertices : tetrahedron_facets) {
    const eigenmath::Vector3d& a = points_[facet_vertices[0]];
    const eigenmath::Vector3d& b = points_[facet_vertices[1]];
    const eigenmath::Vector3d& c = points_[facet_vertices[2]];
    const eigenmath::Vector3d& opposite = points_[facet_vertices[3]];
    // A normal pointing towards the fourth vertex points into the tetrahedron.
    if ((b - a).cross(c - a).dot(opposite - a) > 0.0) {
      std::swap(facet_vertices[1], facet_vertices[2]);
    }
  }
  std::array<int, 4> facet_indices = {-1, -1, -1, -1};
  for (int i = 0; i < 4; ++i) {
    facet_indices[i] =
        AddFacet(tetrahedron_facets[i][0], tetrahedron_facets[i][1],
                 tetrahedron_facets[i][2]);
    if (facet_indices[i] < 0) {
      return absl::FailedPreconditionError(
          "The initial tetrahedron of the convex hull is degenerate.");
    }
  }

  // Each pair of tetrahedron facets shares exactly one edge, which the
  // consistent winding guarantees to appear in opposite directions.
  for (int i = 0; i < 4; ++i) {
    for (int k = 0; k < 3; ++k) {
      if (facets_[facet_indices[i]].neighbors[k] >= 0) continue;
      for (int j = 0; j < 4; ++j) {
        if (j != i && LinkFacets(facet_indices[i], k, facet_indices[j])) {
          break;
        }
      }
    }
  }
  for (int i = 0; i < 4; ++i) {
    const Facet& facet = facets_[facet_indices[i]];
    for (int k = 0; k < 3; ++k) {
      if (facet.neighbors[k] < 0) {
        return absl::FailedPreconditionError(
            "The initial tetrahedron of the convex hull is not closed.");
      }
    }
  }

  // Seed the outside sets, assigning each point to the facet it is farthest
  // outside of, so that the first expansion removes as many candidates as
  // possible.
  for (int i = 0; i < num_points; ++i) {
    if (i == vertex_0 || i == vertex_1 || i == vertex_2 || i == vertex_3) {
      continue;
    }
    int best_facet = -1;
    double best_margin = 0.0;
    for (int j = 0; j < 4; ++j) {
      const double margin =
          OutsideMargin(facets_[facet_indices[j]], points_[i]);
      if (margin > best_margin) {
        best_margin = margin;
        best_facet = facet_indices[j];
      }
    }
    if (best_facet >= 0) {
      AddOutsidePoint(best_facet, i, best_margin);
    }
  }
  for (int i = 0; i < 4; ++i) {
    if (facets_[facet_indices[i]].outside_head >= 0) {
      pending_facets_.push_back(facet_indices[i]);
    }
  }

  return absl::OkStatus();
}

void ConvexHullBuilder::CollectVisibleFacets(const int seed_facet,
                                             const int point_index) {
  const eigenmath::Vector3d& point = points_[point_index];
  ++visit_id_;
  visible_facets_.clear();

  MarkVisited(facets_[seed_facet], true);
  visible_facets_.push_back(seed_facet);

  // `visible_facets_` doubles as the traversal stack: everything appended is
  // visible and still has to have its neighbours examined.
  for (size_t head = 0; head < visible_facets_.size(); ++head) {
    const Facet& facet = facets_[visible_facets_[head]];
    for (int k = 0; k < 3; ++k) {
      const int neighbor = facet.neighbors[k];
      if (neighbor < 0 || WasVisited(facets_[neighbor])) {
        continue;
      }
      const bool visible = OutsideMargin(facets_[neighbor], point) > 0.0;
      MarkVisited(facets_[neighbor], visible);
      if (visible) {
        visible_facets_.push_back(neighbor);
      }
    }
  }
}

bool ConvexHullBuilder::CollectHorizon() {
  horizon_.clear();
  for (const int facet_index : visible_facets_) {
    const Facet& facet = facets_[facet_index];
    for (int k = 0; k < 3; ++k) {
      const int neighbor = facet.neighbors[k];
      if (neighbor < 0) {
        return false;
      }
      if (IsVisible(facets_[neighbor])) {
        continue;
      }
      const int from = facet.vertices[k];
      const int to = facet.vertices[(k + 1) % 3];
      int outer_edge = -1;
      for (int l = 0; l < 3; ++l) {
        if (facets_[neighbor].vertices[l] == to &&
            facets_[neighbor].vertices[(l + 1) % 3] == from) {
          outer_edge = l;
          break;
        }
      }
      if (outer_edge < 0) {
        return false;
      }
      horizon_.push_back({from, to, neighbor, outer_edge});
    }
  }
  return horizon_.size() >= 3;
}

absl::Status ConvexHullBuilder::ReplaceVisibleFacetsWithCone(
    const int point_index) {
  // Salvage the candidate points of the facets that are about to disappear.
  orphaned_points_.clear();
  for (const int facet_index : visible_facets_) {
    for (int candidate = facets_[facet_index].outside_head; candidate >= 0;
         candidate = next_outside_point_[candidate]) {
      if (candidate != point_index) {
        orphaned_points_.push_back(candidate);
      }
    }
  }

  for (const int facet_index : visible_facets_) {
    facets_[facet_index].deleted = true;
    facets_[facet_index].outside_head = -1;
    free_facets_.push_back(facet_index);
  }

  // One new facet per horizon edge, keeping the edge's direction so that the
  // cone is wound consistently with the facets that survive.
  new_facets_.clear();
  for (const HorizonEdge& edge : horizon_) {
    const int facet_index =
        AddFacet(edge.from_vertex, edge.to_vertex, point_index);
    if (facet_index < 0) {
      return absl::FailedPreconditionError(
          "Degenerate facet while expanding the convex hull.");
    }
    new_facets_.push_back(facet_index);
  }

  // Index the horizon by the vertex each edge leaves, so that finding the
  // neighbour of a new facet is a lookup rather than a scan of the whole loop.
  // Two edges leaving the same vertex mean the visible region pinched. Every
  // error path below abandons the build, so the index only has to be unwound
  // on the success path.
  for (size_t i = 0; i < horizon_.size(); ++i) {
    int& leaving_edge = horizon_edge_leaving_vertex_[horizon_[i].from_vertex];
    if (leaving_edge >= 0) {
      return absl::FailedPreconditionError(
          "The horizon of the convex hull is not a closed loop.");
    }
    leaving_edge = static_cast<int>(i);
  }

  // Stitch each new facet to the surviving facet across the horizon, and to
  // the new facet built from the following horizon edge. Linking edge 1 of one
  // facet fills edge 2 of the other in the same call, because the two traverse
  // the shared apex edge in opposite directions, so one call per horizon edge
  // wires up the whole cone.
  //
  //   horizon loop:  ... --> from --------> to --------> next --> ...
  //                           |              |            |
  //                        previous        THIS          next
  //                          facet         facet         facet
  //
  //   THIS.neighbors[0] = edge.outer_facet, across the horizon
  //   THIS.neighbors[1] = facet built from the edge LEAVING `to`
  //   THIS.neighbors[2] = filled in when `previous` links its own edge 1
  for (size_t i = 0; i < horizon_.size(); ++i) {
    const HorizonEdge& edge = horizon_[i];
    const int facet_index = new_facets_[i];
    facets_[facet_index].neighbors[0] = edge.outer_facet;
    facets_[edge.outer_facet].neighbors[edge.outer_edge] = facet_index;

    const int next_edge = horizon_edge_leaving_vertex_[edge.to_vertex];
    if (next_edge < 0) {
      return absl::FailedPreconditionError(
          "The horizon of the convex hull is not a closed loop.");
    }
    if (!LinkFacets(facet_index, 1, new_facets_[next_edge])) {
      return absl::FailedPreconditionError(
          "Could not connect the new facets of the convex hull.");
    }
  }

  for (const HorizonEdge& edge : horizon_) {
    horizon_edge_leaving_vertex_[edge.from_vertex] = -1;
  }

  // A facet whose edge 2 is still open was nobody's successor, which is what a
  // horizon that reaches the same vertex along two different edges looks like.
  for (const int facet_index : new_facets_) {
    if (facets_[facet_index].neighbors[2] < 0) {
      return absl::FailedPreconditionError(
          "The horizon of the convex hull is not a closed loop.");
    }
  }
  // Verify that the horizon forms a single connected loop rather than multiple
  // disjoint cycles.
  size_t connected_facets = 0;
  int current_facet = new_facets_[0];
  for (size_t step = 0; step < new_facets_.size(); ++step) {
    ++connected_facets;
    current_facet = facets_[current_facet].neighbors[1];
    if (current_facet == new_facets_[0]) break;
  }
  if (connected_facets != new_facets_.size()) {
    return absl::FailedPreconditionError(
        "The horizon of the convex hull is not a closed loop.");
  }

  // Redistribute the salvaged candidates over the new facets.
  for (const int candidate : orphaned_points_) {
    int best_facet = -1;
    double best_margin = 0.0;
    for (const int facet_index : new_facets_) {
      const double margin =
          OutsideMargin(facets_[facet_index], points_[candidate]);
      if (margin > best_margin) {
        best_margin = margin;
        best_facet = facet_index;
      }
    }

    // A point sits in at most one outside list, but it can be outside several
    // facets at once, and it is recorded only against the one it is furthest
    // from. So when that facet is deleted here, the point may still be outside
    // a facet that survives, and the cone is then the wrong place to look for
    // its new owner: the surviving facet never learns about the point, its own
    // outside list stays empty, it is never queued for expansion, and the
    // vertex the point would have contributed is lost. Dropping a point is
    // only correct once it is inside every facet, so fall back to a scan of
    // the live facets before giving up on it.
    //
    // This runs at most once per point over the whole build, because a point
    // that reaches this scan without an owner is inside the hull and is never
    // salvaged again.
    if (best_facet < 0) {
      for (size_t facet_index = 0; facet_index < facets_.size();
           ++facet_index) {
        if (facets_[facet_index].deleted) continue;
        const double margin =
            OutsideMargin(facets_[facet_index], points_[candidate]);
        if (margin > best_margin) {
          best_margin = margin;
          best_facet = static_cast<int>(facet_index);
        }
      }
      if (best_facet >= 0) {
        AddOutsidePoint(best_facet, candidate, best_margin);
        // The owner is not part of the cone, so it is not covered by the loop
        // below that queues the new facets.
        pending_facets_.push_back(best_facet);
      }
      continue;
    }

    AddOutsidePoint(best_facet, candidate, best_margin);
  }
  for (const int facet_index : new_facets_) {
    if (facets_[facet_index].outside_head >= 0) {
      pending_facets_.push_back(facet_index);
    }
  }

  return absl::OkStatus();
}

absl::Status ConvexHullBuilder::ExpandHull() {
  // Every round consumes one point, so the hull cannot need more rounds than
  // there are points. The counter only guards against a rounding-induced cycle.
  const int max_rounds = points_.size() + 1;
  int round = 0;

  while (!pending_facets_.empty()) {
    const int facet_index = pending_facets_.back();
    pending_facets_.pop_back();
    const Facet& facet = facets_[facet_index];
    if (facet.deleted || facet.outside_head < 0) {
      continue;
    }
    if (++round > max_rounds) {
      return absl::FailedPreconditionError(
          "The convex hull construction did not converge.");
    }

    // Points only ever enter an outside list with a positive margin, so a
    // non-empty list always has a valid farthest point recorded.
    const int farthest_point = facet.farthest_point;

    CollectVisibleFacets(facet_index, farthest_point);
    if (!CollectHorizon()) {
      return absl::FailedPreconditionError(
          "The visible region of the convex hull is not simply connected.");
    }
    INTR_RETURN_IF_ERROR(ReplaceVisibleFacetsWithCone(farthest_point));
  }

  return absl::OkStatus();
}

absl::Status ConvexHullBuilder::Build(
    const absl::Span<const eigenmath::Vector3d> points) {
  points_ = points;

  if (points.size() < 4) {
    return absl::InvalidArgumentError(
        absl::StrCat("A convex hull in 3D needs at least 4 points. Got ",
                     points.size(), "."));
  }

  double largest_magnitude = 0.0;
  for (const eigenmath::Vector3d& point : points) {
    if (!point.allFinite()) {
      return absl::InvalidArgumentError(
          "The points of the convex hull must all be finite.");
    }
    largest_magnitude =
        std::max(largest_magnitude, point.cwiseAbs().maxCoeff());
  }
  tolerance_ = kRelativeTolerance * largest_magnitude;

  // A simplicial hull on `v` vertices has `2 * v - 4` facets, and at worst
  // every point is a vertex, so sizing `facets_` here avoids reallocating.
  facets_.reserve(static_cast<int>(
      std::min<size_t>(2 * points.size() - 4, kMaxReservedFacets)));
  next_outside_point_.assign(points.size(), -1);
  horizon_edge_leaving_vertex_.assign(points.size(), -1);

  INTR_RETURN_IF_ERROR(BuildInitialTetrahedron());
  return ExpandHull();
}

}  // namespace

absl::StatusOr<std::vector<ConvexHullFacetPlane3d>>
ComputeConvexHullFacetPlanes3d(
    const absl::Span<const eigenmath::Vector3d> points) {
  ConvexHullBuilder builder;
  INTR_RETURN_IF_ERROR(builder.Build(points));

  std::vector<ConvexHullFacetPlane3d> facet_planes;
  facet_planes.reserve(builder.facets().size());
  for (const Facet& facet : builder.facets()) {
    if (facet.deleted) continue;
    facet_planes.push_back({facet.normal, facet.offset, facet.vertices});
  }
  if (facet_planes.size() < 4) {
    return absl::FailedPreconditionError(
        "The convex hull has fewer than 4 facets.");
  }

  // Verify the defining property of the hull: no input point lies outside any
  // facet. The construction is a sequence of local visibility decisions, and a
  // single misjudged one on near degenerate input leaves points stranded
  // outside with no facet left queued to reach them, which ends the loop early
  // and returns a hull that is not the hull of `points`.
  //
  // The error matters beyond this function. Callers interpret a facet as a
  // supporting plane of the input, so a facet that is not one is not merely
  // imprecise, it is a plane the data never supported; in the polar dual it
  // maps to a point that is not a vertex of the original polyhedron, and it
  // can sit arbitrarily far from it. Reporting the failure lets the caller
  // fall back to another solver, whereas returning the hull silently spreads
  // the damage.
  //
  // `projection - offset` is a difference of two dot products of a unit normal
  // with points of the input, so the rounding it carries is set by the size of
  // those points. Scaling the tolerance by `|projection| + |offset|` instead
  // measures how far the plane passes from the origin, which is a different
  // quantity and a much smaller one here: the dual points are formed about an
  // interior point of the polytope, so the origin lies inside their hull and
  // the supporting planes pass close to it. On those facets the tolerance
  // collapses and the test demands an exactness the arithmetic cannot deliver,
  // rejecting hulls that are correct to the last few digits.
  //
  // The scale is taken per facet, from the vertices that define it and the
  // point being tested, rather than from the largest point in the cloud. A
  // single global magnitude would be far too generous for the facets that sit
  // in the small-scale part of an anisotropic input.
  // Precompute the infinity-norm of each input point once so facet and
  // per-point completeness scales can be assembled in O(1) inside the loop.
  std::vector<double> point_magnitudes(points.size());
  for (size_t j = 0; j < points.size(); ++j) {
    point_magnitudes[j] = points[j].cwiseAbs().maxCoeff();
  }
  std::vector<double> facet_magnitudes(facet_planes.size());
  for (size_t i = 0; i < facet_planes.size(); ++i) {
    const ConvexHullFacetPlane3d& facet_plane = facet_planes[i];
    double magnitude = 0.0;
    for (const int point_index : facet_plane.point_indices) {
      if (point_index < 0 ||
          point_index >= static_cast<int>(point_magnitudes.size())) {
        continue;
      }
      magnitude = std::max(magnitude, point_magnitudes[point_index]);
    }
    facet_magnitudes[i] = magnitude;
  }
  for (size_t j = 0; j < points.size(); ++j) {
    const eigenmath::Vector3d& point = points[j];
    const double point_magnitude = point_magnitudes[j];
    for (size_t i = 0; i < facet_planes.size(); ++i) {
      const ConvexHullFacetPlane3d& facet_plane = facet_planes[i];
      const double projection = facet_plane.normal.dot(point);
      const double scale = point_magnitude + facet_magnitudes[i];
      if (projection - facet_plane.offset > kCompletenessTolerance * scale) {
        return absl::FailedPreconditionError(
            "The convex hull does not contain all of the input points.");
      }
    }
  }
  return facet_planes;
}

}  // namespace intrinsic
