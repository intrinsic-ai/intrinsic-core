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

#include "intrinsic/geometry/internal/mesh/mesh.h"

#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

#include "intrinsic/eigenmath/types.h"
#include "intrinsic/geometry/proto/mesh.pb.h"
#include "intrinsic/geometry/proto/triangle_mesh.pb.h"
#include "intrinsic/geometry/proto/v1/triangle_mesh.pb.h"
#include "intrinsic/math/pose3.h"

namespace intrinsic::geo {

Mesh::Mesh(Mesh&& other) noexcept
    : vertices_(std::move(other.vertices_)), faces_(std::move(other.faces_)) {}

Mesh& Mesh::operator=(Mesh&& other) noexcept {
  vertices_ = std::move(other.vertices_);
  faces_ = std::move(other.faces_);
  return *this;
}

Mesh Mesh::Clone() const { return Mesh(vertices_, faces_); }

Mesh& Mesh::Append(const Mesh& other_mesh) {
  size_t offset = vertices_.size();
  vertices_.insert(vertices_.end(), other_mesh.vertices_.begin(),
                   other_mesh.vertices_.end());
  for (const auto& face : other_mesh.faces_) {
    faces_.push_back(
        Face(face[0] + offset, face[1] + offset, face[2] + offset));
  }
  return *this;
}

Mesh& Mesh::Transform(const eigenmath::AffineTransform3d& ref_t_geo) {
  if (ref_t_geo.matrix().isIdentity()) {
    return *this;
  }
  for (auto& v : vertices_) {
    v = ref_t_geo * v;
  }
  return *this;
}

Mesh& Mesh::Transform(const Pose3d& ref_t_geo) {
  return Transform(eigenmath::AffineTransform3d(ref_t_geo.matrix()));
}

Mesh& Mesh::Transform(const eigenmath::Matrix4d& ref_t_geo) {
  return Transform(eigenmath::AffineTransform3d(ref_t_geo));
}

Mesh& Mesh::Translate(const eigenmath::Vector3d& translation) {
  for (auto& v : vertices_) {
    v += translation;
  }
  return *this;
}

Mesh& Mesh::Scale(const eigenmath::Vector3d& scaling) {
  if (scaling == eigenmath::Vector3d::Ones()) {
    return *this;
  }
  for (auto& v : vertices_) {
    v = v.cwiseProduct(scaling);
  }
  return *this;
}

absl::StatusOr<intrinsic_proto::geometry::Mesh> ToProto(const Mesh& mesh) {
  intrinsic_proto::geometry::Mesh result;

  result.set_vertices_stored_row_major(true);
  result.mutable_vertices()->Reserve(mesh.vertex_count() * 3);
  for (int i = 0; i < mesh.vertex_count(); i++) {
    result.mutable_vertices()->AddAlreadyReserved(mesh.vertex(i)[0]);
    result.mutable_vertices()->AddAlreadyReserved(mesh.vertex(i)[1]);
    result.mutable_vertices()->AddAlreadyReserved(mesh.vertex(i)[2]);
  }
  result.set_faces_stored_row_major(true);
  result.mutable_faces()->Reserve(mesh.face_count() * 3);
  for (int i = 0; i < mesh.face_count(); i++) {
    result.mutable_faces()->AddAlreadyReserved(mesh.face(i)[0]);
    result.mutable_faces()->AddAlreadyReserved(mesh.face(i)[1]);
    result.mutable_faces()->AddAlreadyReserved(mesh.face(i)[2]);
  }
  return std::move(result);
}

absl::StatusOr<Mesh> FromProto(const intrinsic_proto::geometry::Mesh& proto) {
  using Mesh = Mesh;
  if (proto.vertices().empty() || proto.faces().empty()) {
    return Mesh();
  }
  // A couple sanity checks specific to our mesh

  if (proto.vertices_size() % 3) {
    return absl::InvalidArgumentError("Vertex Dimension must be 3.");
  }
  if (proto.faces_size() % 3) {
    return absl::InvalidArgumentError("Mesh is not a triangular mesh");
  }

  const int rows_v = proto.vertices_size() / 3;
  const int cols_v = 3;

  Eigen::MatrixXd vertex_matrix;
  if (proto.vertices_stored_row_major()) {
    vertex_matrix =
        Eigen::Map<const Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic,
                                       Eigen::RowMajor>>(
            proto.vertices().data(), rows_v, cols_v);
  } else {
    vertex_matrix =
        Eigen::Map<const Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic,
                                       Eigen::ColMajor>>(
            proto.vertices().data(), rows_v, cols_v);
  }
  Mesh::VertexCollection vertices;
  vertices.reserve(proto.vertices_size());
  for (int i = 0; i < proto.vertices_size() / 3; i++) {
    vertices.emplace_back(vertex_matrix.row(i)[0], vertex_matrix.row(i)[1],
                          vertex_matrix.row(i)[2]);
  }

  Eigen::MatrixXi face_matrix;

  const int rows_f = proto.faces_size() / 3;
  const int cols_f = 3;
  if (proto.faces_stored_row_major()) {
    face_matrix =
        Eigen::Map<const Eigen::Matrix<int, Eigen::Dynamic, Eigen::Dynamic,
                                       Eigen::RowMajor>>(proto.faces().data(),
                                                         rows_f, cols_f);
  } else {
    face_matrix =
        Eigen::Map<const Eigen::Matrix<int, Eigen::Dynamic, Eigen::Dynamic,
                                       Eigen::ColMajor>>(proto.faces().data(),
                                                         rows_f, cols_f);
  }
  Mesh::FaceCollection faces;
  faces.reserve(proto.faces_size());
  for (int i = 0; i < proto.faces_size() / 3; i++) {
    faces.emplace_back(face_matrix.row(i));
  }

  return Mesh(std::move(vertices), std::move(faces));
}

namespace {

template <typename ProtoT>
absl::StatusOr<ProtoT> ToTriangleMeshProtoImpl(const Mesh& mesh) {
  ProtoT result;

  result.mutable_vertices()->Reserve(mesh.vertex_count() * 3);
  for (int i = 0; i < mesh.vertex_count(); i++) {
    result.mutable_vertices()->AddAlreadyReserved(mesh.vertex(i)[0]);
    result.mutable_vertices()->AddAlreadyReserved(mesh.vertex(i)[1]);
    result.mutable_vertices()->AddAlreadyReserved(mesh.vertex(i)[2]);
  }

  result.mutable_faces()->Reserve(mesh.face_count() * 3);
  for (int i = 0; i < mesh.face_count(); i++) {
    result.mutable_faces()->AddAlreadyReserved(mesh.face(i)[0]);
    result.mutable_faces()->AddAlreadyReserved(mesh.face(i)[1]);
    result.mutable_faces()->AddAlreadyReserved(mesh.face(i)[2]);
  }

  return std::move(result);
}

template <typename ProtoT>
absl::StatusOr<Mesh> FromProtoImpl(const ProtoT& proto) {
  using Mesh = Mesh;
  if (proto.vertices().empty() || proto.faces().empty()) {
    return Mesh();
  }

  // A couple of sanity checks.
  if (proto.vertices_size() % 3) {
    return absl::InvalidArgumentError("Vertex Dimension must be 3.");
  }
  if (proto.faces_size() % 3) {
    return absl::InvalidArgumentError(
        "Mesh is not a triangular mesh, must have 3 indices per face.");
  }

  Mesh::VertexCollection vertices;
  vertices.reserve(proto.vertices_size());
  for (int i = 0; i < proto.vertices_size(); i += 3) {
    vertices.emplace_back(proto.vertices(i + 0), proto.vertices(i + 1),
                          proto.vertices(i + 2));
  }

  Mesh::FaceCollection faces;
  faces.reserve(proto.faces_size());
  for (int i = 0; i < proto.faces_size(); i += 3) {
    faces.emplace_back(proto.faces(i + 0), proto.faces(i + 1),
                       proto.faces(i + 2));
  }

  return Mesh(std::move(vertices), std::move(faces));
}

}  // namespace

absl::StatusOr<intrinsic_proto::geometry::TriangleMesh> ToTriangleMeshProto(
    const Mesh& mesh) {
  return ToTriangleMeshProtoImpl<intrinsic_proto::geometry::TriangleMesh>(mesh);
}

absl::StatusOr<Mesh> FromProto(
    const intrinsic_proto::geometry::TriangleMesh& proto) {
  return FromProtoImpl<intrinsic_proto::geometry::TriangleMesh>(proto);
}

absl::StatusOr<intrinsic_proto::geometry::v1::TriangleMesh>
ToTriangleMeshProtoV1(const Mesh& mesh) {
  return ToTriangleMeshProtoImpl<intrinsic_proto::geometry::v1::TriangleMesh>(
      mesh);
}

absl::StatusOr<Mesh> FromProto(
    const intrinsic_proto::geometry::v1::TriangleMesh& proto) {
  return FromProtoImpl<intrinsic_proto::geometry::v1::TriangleMesh>(proto);
}

}  // namespace intrinsic::geo
