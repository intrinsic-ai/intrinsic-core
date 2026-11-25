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

#ifndef INTRINSIC_GEOMETRY_INTERNAL_LEGACY_MESH_MESH_H_
#define INTRINSIC_GEOMETRY_INTERNAL_LEGACY_MESH_MESH_H_

#include <cstddef>
#include <iterator>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/status/statusor.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/geometry/api/triangle.h"
#include "intrinsic/geometry/internal/util/eigen_serialization.h"  // IWYU pragma: keep
#include "intrinsic/geometry/proto/mesh.pb.h"
#include "intrinsic/geometry/proto/triangle_mesh.pb.h"
#include "intrinsic/geometry/proto/v1/exact_geometry.pb.h"
#include "intrinsic/geometry/proto/v1/triangle_mesh.pb.h"
#include "intrinsic/marshal/riegeli_proto_coder.h"
#include "intrinsic/math/pose3.h"

namespace intrinsic::geo {

class Mesh {
 public:
  using IndexType = Eigen::MatrixXi::Scalar;
  using Face = Eigen::Matrix<IndexType, 1, 3>;
  using Vertex = eigenmath::Vector3d;
  using FaceCollection = std::vector<Face>;
  using VertexCollection = std::vector<Vertex>;

  Mesh() = default;

  Mesh(const VertexCollection& vertices, const FaceCollection& faces)
      : vertices_(vertices), faces_(faces) {}
  Mesh(VertexCollection&& vertices, FaceCollection&& faces)
      : vertices_(std::move(vertices)), faces_(std::move(faces)) {}

  // Allow move construct/assign
  Mesh(Mesh&& other) noexcept;
  Mesh& operator=(Mesh&& other) noexcept;

  // Disallow implicit copy construct/assign
  Mesh(const Mesh& other) = delete;
  Mesh& operator=(const Mesh&) = delete;

  // Make a copy of this mesh
  Mesh Clone() const;

  inline bool operator==(const Mesh& rhs) const {
    return vertices_ == rhs.vertices_ && faces_ == rhs.faces_;
  }

  bool empty() const { return face_count() == 0; }

  size_t vertex_count() const { return vertices_.size(); }
  size_t face_count() const { return faces_.size(); }

  const VertexCollection& vertices() const& { return vertices_; }
  VertexCollection&& vertices() && { return std::move(vertices_); }
  Vertex vertex(size_t index) const {
    DCHECK_GT(vertices_.size(), index);
    return vertices_[index];
  }

  const FaceCollection& faces() const& { return faces_; }
  FaceCollection&& faces() && { return std::move(faces_); }
  Face face(size_t index) const {
    DCHECK_GT(faces_.size(), index);
    return faces_[index];
  }

  // Returns all faces as triangles.
  inline std::vector<Triangle> ExtractTriangles() const {
    std::vector<Triangle> result;
    result.reserve(face_count());
    for (const auto& face : faces()) {
      result.push_back(
          Triangle{vertex(face[0]), vertex(face[1]), vertex(face[2])});
    }
    return result;
  }

  // Appends other_mesh's faces and vertices to this mesh
  // Returns a reference to this mesh to facilitate chaining calls
  Mesh& Append(const Mesh& other_mesh);

  // Transforms this mesh's vertices by the given pose
  // Returns a reference to this mesh to facilitate chaining calls
  Mesh& Transform(const eigenmath::AffineTransform3d& ref_t_geo);
  Mesh& Transform(const Pose3d& ref_t_geo);
  Mesh& Transform(const eigenmath::Matrix4d& ref_t_geo);

  // Translates this mesh's position by the given translation vector
  // Returns a reference to this mesh to facilitate chaining calls
  Mesh& Translate(const eigenmath::Vector3d& translation);

  // Scales this mesh by the given scaling vector.
  // Returns a reference to this mesh to facilitate chaining calls
  Mesh& Scale(const eigenmath::Vector3d& scaling);

 private:
  // Vertices
  VertexCollection vertices_;

  // Faces
  FaceCollection faces_;
};

// To and from proto for Mesh.
absl::StatusOr<intrinsic_proto::geometry::Mesh> ToProto(const Mesh& mesh);
absl::StatusOr<Mesh> FromProto(const intrinsic_proto::geometry::Mesh& proto);

// To and from proto for TriangleMesh.
absl::StatusOr<intrinsic_proto::geometry::TriangleMesh> ToTriangleMeshProto(
    const Mesh& mesh);
absl::StatusOr<Mesh> FromProto(
    const intrinsic_proto::geometry::TriangleMesh& proto);

// TODO(b/417296237): Attempt to use only v1 protos

// To and from proto for v1 TriangleMesh.
absl::StatusOr<intrinsic_proto::geometry::v1::TriangleMesh>
ToTriangleMeshProtoV1(const Mesh& mesh);
absl::StatusOr<Mesh> FromProto(
    const intrinsic_proto::geometry::v1::TriangleMesh& proto);

}  // namespace intrinsic::geo

REGISTER_RIEGELI_PROTO_CODER_EXPLICIT(intrinsic::geo::Mesh,
                                      intrinsic_proto::geometry::TriangleMesh,
                                      intrinsic::geo::ToTriangleMeshProto,
                                      intrinsic::geo::FromProto)

namespace intrinsic {
using ::intrinsic::geo::FromProto;
using ::intrinsic::geo::Mesh;
using ::intrinsic::geo::ToProto;
}  // namespace intrinsic
#endif  // INTRINSIC_GEOMETRY_INTERNAL_LEGACY_MESH_MESH_H_
