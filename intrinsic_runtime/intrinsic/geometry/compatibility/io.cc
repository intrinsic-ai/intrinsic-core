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

#include "intrinsic/geometry/compatibility/io.h"

#include <memory>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/repeated_ptr_field.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/geometry/api/exact_geometry.h"
#include "intrinsic/geometry/api/geometry.h"
#include "intrinsic/geometry/api/geometry_options.h"
#include "intrinsic/geometry/api/renderable.h"
#include "intrinsic/geometry/api/renderable_generation.h"
#include "intrinsic/geometry/internal/mesh/mesh.h"
#include "intrinsic/geometry/internal/point_cloud/point_cloud_riegeli_coder.h"
#include "intrinsic/geometry/proto/geometry.pb.h"
#include "intrinsic/geometry/proto/geometry_storage_refs.pb.h"
#include "intrinsic/geometry/proto/lazy_exact_geometry.pb.h"
#include "intrinsic/geometry/proto/point_cloud.pb.h"
#include "intrinsic/geometry/proto/primitives.pb.h"
#include "intrinsic/geometry/proto/renderable.pb.h"
#include "intrinsic/geometry/proto/triangle_mesh.pb.h"
#include "intrinsic/geometry/proto/v1/material.pb.h"
#include "intrinsic/geometry/shapes/box.h"
#include "intrinsic/geometry/shapes/capsule.h"
#include "intrinsic/geometry/shapes/cylinder.h"
#include "intrinsic/geometry/shapes/ellipsoid.h"
#include "intrinsic/geometry/shapes/frustum.h"
#include "intrinsic/geometry/shapes/point_cloud.h"
#include "intrinsic/geometry/shapes/shape_base.h"
#include "intrinsic/geometry/shapes/sphere.h"
#include "intrinsic/math/proto/vector3.pb.h"
#include "intrinsic/util/object_store/object_ref.h"
#include "intrinsic/util/object_store/object_store.h"
#include "intrinsic/util/status/status_builder.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic::geometry_compatibility {

namespace {

geo::Box ToShape(const intrinsic_proto::geometry::Box& proto) {
  eigenmath::Vector3d size(proto.size().x(), proto.size().y(),
                           proto.size().z());
  return geo::Box(size);
}

geo::Cylinder ToShape(const intrinsic_proto::geometry::Cylinder& proto) {
  return geo::Cylinder(/*length=*/proto.length(),
                       /*radius=*/proto.radius());
}

geo::Sphere ToShape(const intrinsic_proto::geometry::Sphere& proto) {
  return geo::Sphere(proto.radius());
}

geo::Ellipsoid ToShape(const intrinsic_proto::geometry::Ellipsoid& proto) {
  eigenmath::Vector3d radii(proto.radii().x(), proto.radii().y(),
                            proto.radii().z());
  return geo::Ellipsoid(radii);
}

geo::Capsule ToShape(const intrinsic_proto::geometry::Capsule& proto) {
  return geo::Capsule(/*length=*/proto.length(),
                      /*radius=*/proto.radius());
}

absl::StatusOr<geo::Frustum> ToShape(
    const intrinsic_proto::geometry::Frustum& proto) {
  return geo::Frustum::Create(
      /*x_angle=*/proto.x_angle(),
      /*y_angle=*/proto.y_angle(),
      /*min_z_distance=*/proto.min_z_distance(),
      /*max_z_distance=*/proto.max_z_distance());
}

template <typename Container, typename... T>
absl::StatusOr<ExactGeometry> ToExactGeometryHelper(const Container& protos,
                                                    GeometryOptions options,
                                                    T... shape) {
  std::vector<geo::TransformedPrimitiveShapePtr> primitive_shapes;
  for (const intrinsic_proto::geometry::PrimitiveShape& proto : protos) {
    switch (proto.shape_case()) {
      case intrinsic_proto::geometry::PrimitiveShape::kBox: {
        primitive_shapes.emplace_back(
            std::make_shared<geo::Box>(ToShape(proto.box())));
        break;
      }
      case intrinsic_proto::geometry::PrimitiveShape::kCylinder: {
        primitive_shapes.emplace_back(
            std::make_shared<geo::Cylinder>(ToShape(proto.cylinder())));
        break;
      }
      case intrinsic_proto::geometry::PrimitiveShape::kSphere: {
        primitive_shapes.emplace_back(
            std::make_shared<geo::Sphere>(ToShape(proto.sphere())));
        break;
      }
      case intrinsic_proto::geometry::PrimitiveShape::kEllipsoid: {
        primitive_shapes.emplace_back(
            std::make_shared<geo::Ellipsoid>(ToShape(proto.ellipsoid())));
        break;
      }
      case intrinsic_proto::geometry::PrimitiveShape::kCapsule: {
        primitive_shapes.emplace_back(
            std::make_shared<geo::Capsule>(ToShape(proto.capsule())));
        break;
      }
      case intrinsic_proto::geometry::PrimitiveShape::kFrustum: {
        INTR_ASSIGN_OR_RETURN(geo::Frustum frustum, ToShape(proto.frustum()));
        primitive_shapes.emplace_back(std::make_shared<geo::Frustum>(frustum));
        break;
      }
      case intrinsic_proto::geometry::PrimitiveShape::SHAPE_NOT_SET: {
        return absl::InvalidArgumentError("Unset primitive shape type");
      }
    }
  }

  return ExactGeometry::Create(std::move(primitive_shapes), shape...,
                               std::move(options));
}

intrinsic_proto::Vector3 ToVectorProto(const eigenmath::Vector3d& v) {
  intrinsic_proto::Vector3 result;
  result.set_x(v[0]);
  result.set_y(v[1]);
  result.set_z(v[2]);
  return result;
}

absl::StatusOr<intrinsic_proto::geometry::PrimitiveShape> ToPrimitiveProto(
    const geo::ShapeBase& shape) {
  intrinsic_proto::geometry::PrimitiveShape primitive;

  switch (shape.getType()) {
    case geo::ShapeType::BOX: {
      const auto& shape_type = shape.get<geo::Box>();
      *primitive.mutable_box()->mutable_size() =
          ToVectorProto(shape_type.getSize());
      break;
    }
    case geo::ShapeType::CAPSULE: {
      const auto& shape_type = shape.get<geo::Capsule>();
      primitive.mutable_capsule()->set_length(shape_type.getLength());
      primitive.mutable_capsule()->set_radius(shape_type.getRadius());
      break;
    }
    case geo::ShapeType::CYLINDER: {
      const auto& shape_type = shape.get<geo::Cylinder>();
      primitive.mutable_cylinder()->set_length(shape_type.getLength());
      primitive.mutable_cylinder()->set_radius(shape_type.getRadius());
      break;
    }
    case geo::ShapeType::ELLIPSOID: {
      const auto& shape_type = shape.get<geo::Ellipsoid>();
      *primitive.mutable_ellipsoid()->mutable_radii() =
          ToVectorProto(shape_type.getRadii());

      break;
    }
    case geo::ShapeType::SPHERE: {
      const auto& shape_type = shape.get<geo::Sphere>();
      primitive.mutable_sphere()->set_radius(shape_type.getRadius());
      break;
    }
    case geo::ShapeType::FRUSTUM: {
      const auto& shape_type = shape.get<geo::Frustum>();
      primitive.mutable_frustum()->set_x_angle(shape_type.getXAngle());
      primitive.mutable_frustum()->set_y_angle(shape_type.getYAngle());
      primitive.mutable_frustum()->set_min_z_distance(
          shape_type.getMinZDistance());
      primitive.mutable_frustum()->set_max_z_distance(
          shape_type.getMaxZDistance());
      break;
    }
    default: {
      return absl::InvalidArgumentError("Unsupported shape type");
    }
  }

  return primitive;
}

absl::StatusOr<intrinsic_proto::geometry::PointCloud> ToPointCloudProto(
    const geo::PointCloud& point_cloud) {
  intrinsic_proto::geometry::PointCloud result;

  const std::vector<eigenmath::Vector3d>& points = point_cloud.getPoints();
  const std::vector<eigenmath::Vector3d>& normals = point_cloud.getNormals();

  if (!normals.empty() && points.size() != normals.size()) {
    return absl::InvalidArgumentError("Points and normals length mismatch");
  }

  result.mutable_points()->Reserve(points.size() * 3);
  for (const eigenmath::Vector3d& p : points) {
    result.add_points(p.x());
    result.add_points(p.y());
    result.add_points(p.z());
  }

  result.mutable_normals()->Reserve(normals.size() * 3);
  for (const eigenmath::Vector3d& n : normals) {
    result.add_normals(n.x());
    result.add_normals(n.y());
    result.add_normals(n.z());
  }

  return result;
}

absl::StatusOr<geo::PointCloud> FromPointCloudProto(
    const intrinsic_proto::geometry::PointCloud& point_cloud) {
  if (point_cloud.points_size() % 3 != 0) {
    return absl::DataLossError(
        "Point cloud does not have the right number of points values");
  }

  if (point_cloud.normals_size() % 3 != 0) {
    return absl::DataLossError(
        "Point cloud does not have the right number of normals values");
  }

  if (point_cloud.normals_size() != 0 &&
      point_cloud.points_size() != point_cloud.normals_size()) {
    return absl::InvalidArgumentError("Points and normals length mismatch");
  }

  std::vector<eigenmath::Vector3d> points;
  points.reserve(point_cloud.points_size() / 3);
  for (int i = 0; i < point_cloud.points_size(); i += 3) {
    eigenmath::Vector3d point = {point_cloud.points(i),
                                 point_cloud.points(i + 1),
                                 point_cloud.points(i + 2)};
    points.push_back(std::move(point));
  }

  std::vector<eigenmath::Vector3d> normals;
  normals.reserve(point_cloud.normals_size() / 3);
  for (int i = 0; i < point_cloud.normals_size(); i += 3) {
    eigenmath::Vector3d normal = {point_cloud.normals(i),
                                  point_cloud.normals(i + 1),
                                  point_cloud.normals(i + 2)};
    normals.push_back(std::move(normal));
  }

  return geo::PointCloud(std::move(points), std::move(normals));
}

absl::StatusOr<intrinsic_proto::geometry::PrimitiveShapeSet>
ToPrimitiveSetProto(
    const std::vector<geo::TransformedPrimitiveShapePtr>& shapes) {
  intrinsic_proto::geometry::PrimitiveShapeSet result;
  for (const auto& shape : shapes) {
    if (!shape.ref_t_shape().isApprox(eigenmath::Matrix4d::Identity())) {
      return absl::InvalidArgumentError("Shape has a transform");
    }
    INTR_ASSIGN_OR_RETURN(*result.add_primitives(),
                          ToPrimitiveProto(*shape.shape()));
  }

  return result;
}

}  // namespace

absl::StatusOr<intrinsic_proto::geometry::LazyExactGeometry> ToProto(
    const ExactGeometry& shape) {
  intrinsic_proto::geometry::LazyExactGeometry result;
  if (shape.HasMesh()) {
    INTR_ASSIGN_OR_RETURN(*result.mutable_triangle_mesh(),
                          ToTriangleMeshProto(shape.GetMesh()->Value()));
  }

  if (shape.HasPointCloud()) {
    INTR_ASSIGN_OR_RETURN(*result.mutable_point_cloud(),
                          ToPointCloudProto(shape.GetPointCloud()->Value()));
  }

  const auto primitive_shapes = shape.GetPrimitiveShapes();
  INTR_ASSIGN_OR_RETURN(*result.mutable_primitive_set(),
                        ToPrimitiveSetProto(primitive_shapes));
  return std::move(result);
}

absl::StatusOr<ExactGeometry> ToExactGeometry(
    const intrinsic_proto::geometry::LazyExactGeometry& proto,
    GeometryOptions options) {
  const auto* primitives = &proto.primitives();
  if (proto.has_primitive_set()) {
    primitives = &proto.primitive_set().primitives();
  }

  ExactGeometry::ComputedShape primary_shape = DeDuplicate(Mesh());

  if (proto.has_point_cloud()) {
    if (proto.mesh_type_case() !=
        intrinsic_proto::geometry::LazyExactGeometry::MESH_TYPE_NOT_SET) {
      return absl::DataLossError("Got conflicting data, point cloud and mesh");
    }
    INTR_ASSIGN_OR_RETURN(auto point_cloud,
                          FromPointCloudProto(proto.point_cloud()));
    primary_shape = DeDuplicate(std::move(point_cloud));
  } else {
    Mesh mesh;
    if (proto.has_triangle_mesh()) {
      INTR_ASSIGN_OR_RETURN(mesh, FromProto(proto.triangle_mesh()));
    } else {  // TODO(b/240608966): Use triangle_mesh only
      INTR_ASSIGN_OR_RETURN(mesh, FromProto(proto.mesh()));
    }
    primary_shape = DeDuplicate(std::move(mesh));
  }

  return ToExactGeometryHelper(*primitives, std::move(options), primary_shape);
}

absl::StatusOr<intrinsic_proto::geometry::Geometry> ToProto(
    const Geometry& geo) {
  intrinsic_proto::geometry::Geometry proto;
  INTR_ASSIGN_OR_RETURN(*proto.mutable_exact_geometry(),
                        ToProto(geo.GetExactGeometry()));
  // Always generate a renderable with material overrides because the Geometry
  // proto doesn't have a field for material overrides.
  INTR_ASSIGN_OR_RETURN(auto renderable,
                        GenerateRenderableWithMaterialOverrides(geo));
  if (renderable != nullptr) {
    if (geo.KeepRenderableForSerialization()) {
      INTR_ASSIGN_OR_RETURN(*proto.mutable_renderable(), ToProto(*renderable));
    } else {
      INTR_ASSIGN_OR_RETURN(*proto.mutable_generated_renderable(),
                            ToProto(*renderable));
    }
  }

  return proto;
}

absl::StatusOr<Geometry> ToGeometry(
    const intrinsic_proto::geometry::Geometry& proto,
    const intrinsic::geo::GeometryOptions& options,
    const std::optional<intrinsic_proto::geometry::GeometryStorageRefs>& refs) {
  ExactGeometry exact_geo = ExactGeometry::CreateEmpty();
  if (proto.has_exact_geometry()) {
    INTR_ASSIGN_OR_RETURN(
        exact_geo, ToExactGeometry(proto.exact_geometry(), std::move(options)));
  } else {
    return absl::DataLossError("Geometry proto is missing shape data");
  }

  std::shared_ptr<const Renderable> renderable_ptr;
  bool keep_renderable = false;
  if (proto.has_renderable()) {
    renderable_ptr =
        std::make_shared<Renderable>(proto.renderable().gltf_string());
    keep_renderable = true;
  } else if (proto.has_generated_renderable()) {
    renderable_ptr = std::make_shared<Renderable>(
        proto.generated_renderable().gltf_string());
  }

  return Geometry(std::move(exact_geo), std::move(renderable_ptr),
                  keep_renderable, /*material_properties=*/std::nullopt,
                  /*provenance=*/std::nullopt);
}

absl::StatusOr<intrinsic_proto::geometry::Renderable> ToProto(
    const Renderable& geo) {
  intrinsic_proto::geometry::Renderable proto;
  proto.set_gltf_string(geo.GetGLBString());
  return proto;
}

absl::StatusOr<Geometry> ToGeometry(
    const ::google::protobuf::RepeatedPtrField<
        intrinsic_proto::geometry::PrimitiveShape>& protos) {
  INTR_ASSIGN_OR_RETURN(
      auto rep, ToExactGeometryHelper(protos, GeometryOptions::Default()));
  return Geometry(std::move(rep), /*provenance=*/std::nullopt);
}

// Serialize the shapes into the proto.
absl::StatusOr<intrinsic_proto::geometry::PrimitiveShapeSet>
ToPrimitiveSetProto(const Geometry& geo) {
  return ToPrimitiveSetProto(geo.GetExactGeometry().GetPrimitiveShapes());
}

}  // namespace intrinsic::geometry_compatibility
