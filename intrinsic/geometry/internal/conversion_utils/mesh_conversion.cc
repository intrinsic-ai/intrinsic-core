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

#include "intrinsic/geometry/internal/conversion_utils/mesh_conversion.h"

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

#include "CGAL/Polygon_mesh_processing/manifoldness.h"
#include "CGAL/Polygon_mesh_processing/orient_polygon_soup.h"
#include "CGAL/Polygon_mesh_processing/polygon_soup_to_polygon_mesh.h"
#include "CGAL/Polygon_mesh_processing/repair.h"
#include "CGAL/Polygon_mesh_processing/repair_degeneracies.h"
#include "CGAL/Polygon_mesh_processing/repair_polygon_soup.h"
#include "CGAL/Polygon_mesh_processing/self_intersections.h"
#include "CGAL/Polygon_mesh_processing/shape_predicates.h"
#include "CGAL/Surface_mesh/Surface_mesh.h"
#include "CGAL/number_utils.h"
#include "CGAL/tags.h"
#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "intrinsic/geometry/internal/conversion_utils/to_epic.h"
#include "intrinsic/geometry/internal/indexed_triangle_set_3/indexed_triangle_set_3.h"
#include "intrinsic/geometry/internal/mesh/mesh.h"
#include "intrinsic/geometry/internal/surface_mesh_3/surface_mesh_3d.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic::geo {
namespace {

constexpr size_t kMaxCleanAttempts = 10;

struct SurfaceMeshCleaningState {
  bool does_self_intersect = false;
  bool has_degenerate_edges = false;
  bool has_degenerate_faces = false;
  bool non_manifold = false;
  // TODO(b/458420439): Self intersection does not currently contribute to mesh
  // cleanliness as auto repair is not used and requires CGAL 6.0+.
  bool IsClean() const {
    return !non_manifold && !has_degenerate_edges && !has_degenerate_faces;
  }
};

struct PointHash {
  size_t operator()(const EpicPoint3& p) const {
    return absl::HashOf(p.x(), p.y(), p.z());
  }
};

struct PointEq {
  bool operator()(const EpicPoint3& p, const EpicPoint3& q) const {
    return p.x() == q.x() && p.y() == q.y() && p.z() == q.z();
  }
};

void PruneDegenerateFaces(const std::vector<EpicPoint3>& points,
                          std::vector<EpicIndexedTriangleSet3::Triple>& faces) {
  std::erase_if(faces, [&](const auto& t) {
    if (t.size() < 3) {
      return true;
    }
    return EpicTriangle3(points[t[0]], points[t[1]], points[t[2]])
        .is_degenerate();
  });
}

// Converts a CGAL EpicPoint3 to a standard double array of size 3.
std::array<double, 3> ToStdArray3(const EpicPoint3& point) {
  return {CGAL::to_double(point.x()), CGAL::to_double(point.y()),
          CGAL::to_double(point.z())};
}

// Converts a dynamically-sized index triple vector to a fixed-size int array.
std::array<int, 3> ToStdArray3(const std::vector<std::size_t>& triple) {
  CHECK_EQ(triple.size(), 3);
  return {static_cast<int>(triple[0]), static_cast<int>(triple[1]),
          static_cast<int>(triple[2])};
}
}  // namespace

EpicIndexedTriangleSet3 ToEpicIndexedTriangleSet3(const Mesh& mesh) {
  EpicIndexedTriangleSet3 indexed_triangle_set;
  using Pidx = EpicIndexedTriangleSet3::PointIndex;
  using Triple = EpicIndexedTriangleSet3::Triple;

  indexed_triangle_set.points().reserve(mesh.vertex_count());
  indexed_triangle_set.triples().reserve(mesh.face_count());

  // Make all points unique and remapping of vertices to points.
  absl::flat_hash_map<EpicPoint3, Pidx, PointHash, PointEq> point_to_index_map;
  for (int i = 0; i < mesh.vertex_count(); ++i) {
    EpicPoint3 epic_point = EpicPoint3(
        mesh.vertices()[i][0], mesh.vertices()[i][1], mesh.vertices()[i][2]);
    if (point_to_index_map.count(epic_point) == 0) {
      Pidx idx = indexed_triangle_set.points().size();
      indexed_triangle_set.points().push_back(epic_point);
      point_to_index_map.insert_or_assign(point_to_index_map.end(), epic_point,
                                          idx);
    }
  }

  // Update vertex indices in faces according to map generated above.
  Triple triple = EpicIndexedTriangleSet3::MakeTriple(0, 0, 0);
  for (int i = 0; i < mesh.face_count(); ++i) {
    for (int j = 0; j < 3; ++j) {
      auto vdx = mesh.faces()[i][j];
      EpicPoint3 epic_point =
          EpicPoint3(mesh.vertices()[vdx][0], mesh.vertices()[vdx][1],
                     mesh.vertices()[vdx][2]);
      triple[j] = point_to_index_map.at(epic_point);
    }
    indexed_triangle_set.triples().push_back(triple);
  }

  return indexed_triangle_set;
}

EpicIndexedTriangleSet3 ToEpicIndexedTriangleSet3(
    const EpicSurfaceMesh3& surface_mesh) {
  return ToEpicIndexedTriangleSet3(ToMesh(surface_mesh));
}

const CleanMeshOptions& CleanMeshOptions::Strict() {
  static constexpr CleanMeshOptions options;
  return options;
}

const CleanMeshOptions& CleanMeshOptions::BestEffort() {
  static constexpr CleanMeshOptions options{
      .require_polygon_mesh = false,
  };
  return options;
}

absl::Status CleanMesh(EpicIndexedTriangleSet3& indexed_triangle_set,
                       const CleanMeshOptions& options) {
  namespace PMP = CGAL::Polygon_mesh_processing;
  auto& points = indexed_triangle_set.points();
  auto& faces = indexed_triangle_set.triples();

  for (const auto& face : faces) {
    for (const auto& idx : face) {
      if (idx >= points.size()) {
        return absl::InvalidArgumentError(
            "Indexed triangle set contains out-of-bounds face vertex indices.");
      }
    }
  }

  if (options.prune_degenerate_faces) {
    PruneDegenerateFaces(points, faces);
  }
  if (options.prune_isolated_points) {
    PMP::remove_isolated_points_in_polygon_soup(points, faces);
  }
  if (options.merge_duplicate_points) {
    PMP::merge_duplicate_points_in_polygon_soup(points, faces);
  }
  if (options.merge_duplicate_faces) {
    PMP::merge_duplicate_polygons_in_polygon_soup(
        points, faces, CGAL::parameters::require_same_orientation(true));
  }
  PMP::repair_polygon_soup(points, faces);
  if (options.orient_normals) {
    PMP::orient_polygon_soup(points, faces);
  }

  bool is_polygon_mesh = PMP::is_polygon_soup_a_polygon_mesh(faces);
  if (options.require_polygon_mesh && !is_polygon_mesh) {
    return absl::InvalidArgumentError(
        "Mesh contains non-manifold or unorientable geometry and could not be "
        "repaired into a valid 2-manifold surface mesh.");
  }
  return absl::OkStatus();
}

absl::Status CleanMesh(EpicIndexedTriangleSet3& indexed_triangle_set) {
  return CleanMesh(indexed_triangle_set, CleanMeshOptions::Strict());
}

absl::Status ProcessMeshUntilClean(
    EpicIndexedTriangleSet3& indexed_triangle_set,
    const CleanMeshOptions& options) {
  size_t clean_attempts = 0;
  absl::Status status = CleanMesh(indexed_triangle_set, options);
  while (clean_attempts < kMaxCleanAttempts && !status.ok()) {
    status = CleanMesh(indexed_triangle_set, options);
    clean_attempts++;
  }
  return status;
}

absl::StatusOr<EpicSurfaceMesh3> ToEpicSurfaceMesh3WithCleaning(
    const Mesh& mesh, const CleanMeshOptions& options) {
  if (!options.require_polygon_mesh) {
    return absl::InvalidArgumentError(
        "ToEpicSurfaceMesh3WithCleaning requires require_polygon_mesh to be "
        "true because EpicSurfaceMesh3 strictly requires a 2-manifold "
        "structure.");
  }
  EpicIndexedTriangleSet3 indexed_triangle_set =
      ToEpicIndexedTriangleSet3(mesh);
  INTR_RETURN_IF_ERROR(ProcessMeshUntilClean(indexed_triangle_set, options));
  return ToEpicSurfaceMesh3(indexed_triangle_set);
}

void CleanSurfaceMesh(EpicSurfaceMesh3& surface_mesh,
                      SurfaceMeshCleaningState& mesh_cleaning_state) {
  namespace PMP = CGAL::Polygon_mesh_processing;
  CHECK(surface_mesh.is_valid());
  if (surface_mesh.is_empty()) {
    return;
  }
  VLOG(1) << "Attempting to remove isolated vertices";
  PMP::remove_isolated_vertices(surface_mesh);
  VLOG(1) << "Attempting to remove connected components of negligible size";
  PMP::remove_connected_components_of_negligible_size(surface_mesh);

  if (mesh_cleaning_state.non_manifold) {
    VLOG(1) << "Attempting to duplicate non-manifold vertices";
    PMP::duplicate_non_manifold_vertices(surface_mesh);
  }

  if (mesh_cleaning_state.has_degenerate_edges) {
    VLOG(1) << "Attempting to repair degenerate edges";
    PMP::remove_degenerate_edges(surface_mesh);
  }

  if (mesh_cleaning_state.has_degenerate_faces) {
    VLOG(1) << "Attempting to repair degenerate faces";
    PMP::remove_degenerate_faces(surface_mesh);
  }
}

SurfaceMeshCleaningState CheckIfSurfaceMeshIsClean(
    EpicSurfaceMesh3& surface_mesh) {
  SurfaceMeshCleaningState mesh_cleaning_state;
  CHECK(surface_mesh.is_valid());
  if (surface_mesh.is_empty()) {
    return mesh_cleaning_state;
  }
  namespace PMP = CGAL::Polygon_mesh_processing;

  mesh_cleaning_state.does_self_intersect =
      PMP::does_self_intersect<CGAL::Parallel_tag>(surface_mesh);

  mesh_cleaning_state.has_degenerate_edges = false;
  for (const auto& edge : surface_mesh.edges()) {
    if (PMP::is_degenerate_edge(edge, surface_mesh)) {
      mesh_cleaning_state.has_degenerate_edges = true;
      break;
    }
  }

  mesh_cleaning_state.has_degenerate_faces = false;
  for (const auto& face : surface_mesh.faces()) {
    if (PMP::is_degenerate_triangle_face(face, surface_mesh)) {
      mesh_cleaning_state.has_degenerate_faces = true;
      break;
    }
  }

  mesh_cleaning_state.non_manifold = false;
  for (const auto& vertex : surface_mesh.vertices()) {
    if (PMP::is_non_manifold_vertex(vertex, surface_mesh)) {
      mesh_cleaning_state.non_manifold = true;
      break;
    }
  }
  return mesh_cleaning_state;
}

bool ProcessSurfaceMeshUntilClean(EpicSurfaceMesh3& surface_mesh) {
  size_t clean_attempts = 0;
  SurfaceMeshCleaningState mesh_cleaning_state =
      CheckIfSurfaceMeshIsClean(surface_mesh);
  while (clean_attempts < kMaxCleanAttempts && !mesh_cleaning_state.IsClean()) {
    CleanSurfaceMesh(surface_mesh, mesh_cleaning_state);
    mesh_cleaning_state = CheckIfSurfaceMeshIsClean(surface_mesh);
    clean_attempts++;
  }
  return mesh_cleaning_state.IsClean();
}

absl::StatusOr<Mesh> ToMeshWithCleaning(const EpicSurfaceMesh3& surface_mesh) {
  EpicSurfaceMesh3 clean_mesh = surface_mesh;
  bool is_clean = ProcessSurfaceMeshUntilClean(clean_mesh);
  if (!is_clean) {
    return absl::InternalError(
        "Mesh is not clean after repair attempts in ToMeshWithCleaning.");
  }

  return ToMesh(clean_mesh);
}

EpicSurfaceMesh3 ToEpicSurfaceMesh3(const Mesh& mesh) {
  return ToEpicSurfaceMesh3(ToEpicIndexedTriangleSet3(mesh));
}

Mesh ToMesh(const EpicSurfaceMesh3& surface_mesh) {
  CHECK(surface_mesh.is_valid());

  Mesh::VertexCollection vertices;
  vertices.reserve(surface_mesh.number_of_vertices());

  // While in most cases the identity map for vertex and face indices would be
  // sufficient, this is not the case for surface_meshes with deleted vertices
  // and faces, e.g. after a deduplication step.
  // Map from surface mesh vertex index to new vertex index.
  absl::flat_hash_map<EpicSurfaceMesh3::Vertex_index, int> map_vdx;
  for (auto v : surface_mesh.vertices()) {
    map_vdx[v] = vertices.size();
    vertices.emplace_back(surface_mesh.point(v)[0], surface_mesh.point(v)[1],
                          surface_mesh.point(v)[2]);
  }

  Mesh::FaceCollection faces;
  faces.reserve(surface_mesh.number_of_faces());
  for (auto f : surface_mesh.faces()) {
    auto he = surface_mesh.halfedge(f);
    auto i = map_vdx[surface_mesh.target(he)];
    he = surface_mesh.next(he);
    auto j = map_vdx[surface_mesh.target(he)];
    he = surface_mesh.next(he);
    auto k = map_vdx[surface_mesh.target(he)];
    faces.emplace_back(i, j, k);
  }
  return Mesh(std::move(vertices), std::move(faces));
}

Mesh ToMesh(const EpicIndexedTriangleSet3& set) {
  Mesh::VertexCollection vertices;
  vertices.reserve(set.points().size());
  for (const auto& p : set.points()) {
    vertices.emplace_back(p[0], p[1], p[2]);
  }
  Mesh::FaceCollection faces;
  faces.reserve(set.triples().size());
  for (const auto& triple : set.triples()) {
    faces.emplace_back(triple[0], triple[1], triple[2]);
  }
  return Mesh(std::move(vertices), std::move(faces));
}

absl::StatusOr<coacd::Mesh> ToCoacdMesh(
    EpicIndexedTriangleSet3 indexed_triangle_set) {
  INTR_RETURN_IF_ERROR(
      CleanMesh(indexed_triangle_set, CleanMeshOptions::BestEffort()));

  coacd::Mesh coacd_mesh;
  coacd_mesh.vertices.resize(indexed_triangle_set.points().size());
  std::transform(indexed_triangle_set.points().begin(),
                 indexed_triangle_set.points().end(),
                 coacd_mesh.vertices.begin(),
                 [](const auto& p) { return ToStdArray3(p); });

  coacd_mesh.indices.resize(indexed_triangle_set.triples().size());
  std::transform(indexed_triangle_set.triples().begin(),
                 indexed_triangle_set.triples().end(),
                 coacd_mesh.indices.begin(),
                 [](const auto& t) { return ToStdArray3(t); });
  return coacd_mesh;
}

std::vector<EpicIndexedTriangleSet3> FromCoacdMeshes(
    const std::vector<coacd::Mesh>& decomposed_meshes) {
  std::vector<EpicIndexedTriangleSet3> output;
  output.reserve(decomposed_meshes.size());
  for (const auto& coacd_mesh : decomposed_meshes) {
    EpicIndexedTriangleSet3 hull_indexed_triangle_set;
    hull_indexed_triangle_set.points().reserve(coacd_mesh.vertices.size());
    for (const auto& vertex : coacd_mesh.vertices) {
      hull_indexed_triangle_set.AddPoint(
          EpicPoint3(vertex[0], vertex[1], vertex[2]));
    }
    hull_indexed_triangle_set.triples().reserve(coacd_mesh.indices.size());
    for (const auto& triple : coacd_mesh.indices) {
      hull_indexed_triangle_set.AddTriple(triple[0], triple[1], triple[2]);
    }
    output.push_back(std::move(hull_indexed_triangle_set));
  }
  return output;
}

MeshoptBuffers ToMeshoptBuffers(const Mesh& mesh) {
  const size_t vertex_count = mesh.vertex_count();
  const size_t face_count = mesh.face_count();

  MeshoptBuffers buffers;
  buffers.vertex_positions.resize(vertex_count * 3);
  for (size_t i = 0; i < vertex_count; ++i) {
    const auto& v = mesh.vertex(i);
    buffers.vertex_positions[i * 3] = static_cast<float>(v.x());
    buffers.vertex_positions[i * 3 + 1] = static_cast<float>(v.y());
    buffers.vertex_positions[i * 3 + 2] = static_cast<float>(v.z());
  }

  buffers.indices.resize(face_count * 3);
  for (size_t i = 0; i < face_count; ++i) {
    const auto& f = mesh.face(i);
    buffers.indices[i * 3] = static_cast<uint32_t>(f[0]);
    buffers.indices[i * 3 + 1] = static_cast<uint32_t>(f[1]);
    buffers.indices[i * 3 + 2] = static_cast<uint32_t>(f[2]);
  }

  return buffers;
}

Mesh FromMeshoptBuffers(const MeshoptBuffers& buffers) {
  const size_t vertex_count = buffers.vertex_positions.size() / 3;
  const size_t index_count = buffers.indices.size();

  Mesh::VertexCollection vertices;
  vertices.reserve(vertex_count);
  for (size_t i = 0; i < vertex_count; ++i) {
    vertices.emplace_back(
        static_cast<double>(buffers.vertex_positions[i * 3]),
        static_cast<double>(buffers.vertex_positions[i * 3 + 1]),
        static_cast<double>(buffers.vertex_positions[i * 3 + 2]));
  }

  Mesh::FaceCollection faces;
  faces.reserve(index_count / 3);
  for (size_t i = 0; i + 2 < index_count; i += 3) {
    faces.emplace_back(static_cast<Mesh::IndexType>(buffers.indices[i + 0]),
                       static_cast<Mesh::IndexType>(buffers.indices[i + 1]),
                       static_cast<Mesh::IndexType>(buffers.indices[i + 2]));
  }

  return Mesh(std::move(vertices), std::move(faces));
}

}  // namespace intrinsic::geo
