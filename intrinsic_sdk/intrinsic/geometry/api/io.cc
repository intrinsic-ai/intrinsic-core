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

#include "intrinsic/geometry/api/io.h"

#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "intrinsic/eigenmath/rotation_utils.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/geometry/api/affine_transform_of_geometry.h"
#include "intrinsic/geometry/api/exact_geometry.h"
#include "intrinsic/geometry/api/geometry.h"
#include "intrinsic/geometry/api/geometry_options.h"
#include "intrinsic/geometry/api/renderable.h"
#include "intrinsic/geometry/internal/mesh/mesh.h"
#include "intrinsic/geometry/internal/point_cloud/point_cloud_riegeli_coder.h"
#include "intrinsic/geometry/proto/v1/exact_geometry.pb.h"
#include "intrinsic/geometry/proto/v1/geometry.pb.h"
#include "intrinsic/geometry/proto/v1/geometry_options.pb.h"
#include "intrinsic/geometry/proto/v1/geometry_storage_refs.pb.h"
#include "intrinsic/geometry/proto/v1/inline_geometry.pb.h"
#include "intrinsic/geometry/proto/v1/point_cloud.pb.h"
#include "intrinsic/geometry/proto/v1/primitive_shape.pb.h"
#include "intrinsic/geometry/proto/v1/primitives.pb.h"
#include "intrinsic/geometry/proto/v1/renderable.pb.h"
#include "intrinsic/geometry/proto/v1/transformed_geometry.pb.h"
#include "intrinsic/geometry/proto/v1/transformed_primitive_shape.pb.h"
#include "intrinsic/geometry/proto/v1/triangle_mesh.pb.h"
#include "intrinsic/geometry/shapes/box.h"
#include "intrinsic/geometry/shapes/capsule.h"
#include "intrinsic/geometry/shapes/cylinder.h"
#include "intrinsic/geometry/shapes/ellipsoid.h"
#include "intrinsic/geometry/shapes/frustum.h"
#include "intrinsic/geometry/shapes/point_cloud.h"
#include "intrinsic/geometry/shapes/shape_base.h"
#include "intrinsic/geometry/shapes/shapes.h"
#include "intrinsic/geometry/shapes/sphere.h"
#include "intrinsic/geometry/storage/geometry_deserializer.h"
#include "intrinsic/geometry/storage/geometry_serializer.h"
#include "intrinsic/math/proto/vector3.pb.h"
#include "intrinsic/math/proto_conversion.h"
#include "intrinsic/util/object_store/object_ref.h"
#include "intrinsic/util/object_store/object_store.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic::geo {

namespace {

absl::StatusOr<Box> ToShape(const intrinsic_proto::geometry::v1::Box& proto) {
  eigenmath::Vector3d size(proto.size().x(), proto.size().y(),
                           proto.size().z());
  if (!size.allFinite()) {
    return absl::InvalidArgumentError("Box dimensions must be finite");
  }
  if (size.minCoeff() <= 0) {
    return absl::InvalidArgumentError("Box dimensions must be positive");
  }
  return Box(size);
}

absl::StatusOr<Cylinder> ToShape(
    const intrinsic_proto::geometry::v1::Cylinder& proto) {
  if (!std::isfinite(proto.length())) {
    return absl::InvalidArgumentError("Cylinder length must be finite");
  }
  if (!std::isfinite(proto.radius())) {
    return absl::InvalidArgumentError("Cylinder radius must be finite");
  }

  if (proto.length() <= 0) {
    return absl::InvalidArgumentError("Cylinder length must be positive");
  }
  if (proto.radius() <= 0) {
    return absl::InvalidArgumentError("Cylinder radius must be positive");
  }
  return Cylinder(/*length=*/proto.length(),
                  /*radius=*/proto.radius());
}

absl::StatusOr<Sphere> ToShape(
    const intrinsic_proto::geometry::v1::Sphere& proto) {
  if (!std::isfinite(proto.radius())) {
    return absl::InvalidArgumentError("Sphere radius must be finite");
  }
  if (proto.radius() <= 0) {
    return absl::InvalidArgumentError("Sphere radius must be positive");
  }
  return Sphere(proto.radius());
}

absl::StatusOr<Ellipsoid> ToShape(
    const intrinsic_proto::geometry::v1::Ellipsoid& proto) {
  eigenmath::Vector3d radii(proto.radii().x(), proto.radii().y(),
                            proto.radii().z());
  if (!radii.allFinite()) {
    return absl::InvalidArgumentError("Ellipsoid radii must be finite");
  }
  if (radii.minCoeff() <= 0) {
    return absl::InvalidArgumentError("Ellipsoid radii must be positive");
  }
  return Ellipsoid(radii);
}

absl::StatusOr<Capsule> ToShape(
    const intrinsic_proto::geometry::v1::Capsule& proto) {
  if (!std::isfinite(proto.length())) {
    return absl::InvalidArgumentError("Capsule length must be finite");
  }
  if (!std::isfinite(proto.radius())) {
    return absl::InvalidArgumentError("Capsule radius must be finite");
  }

  if (proto.length() <= 0) {
    return absl::InvalidArgumentError("Capsule length must be positive");
  }
  if (proto.radius() <= 0) {
    return absl::InvalidArgumentError("Capsule radius must be positive");
  }
  return Capsule(/*length=*/proto.length(),
                 /*radius=*/proto.radius());
}

absl::StatusOr<Frustum> ToShape(
    const intrinsic_proto::geometry::v1::Frustum& proto) {
  return Frustum::Create(
      /*x_angle=*/proto.x_angle(),
      /*y_angle=*/proto.y_angle(),
      /*min_z_distance=*/proto.min_z_distance(),
      /*max_z_distance=*/proto.max_z_distance());
}

struct GeometryTuple {
  ExactGeometry exact_geometry;
  std::shared_ptr<const Renderable> renderable;
  bool keep_renderable;
};

absl::StatusOr<GeometryTuple> ToGeometryTuple(
    const intrinsic_proto::geometry::v1::InlineGeometry& proto) {
  INTR_ASSIGN_OR_RETURN(ExactGeometry exact_geometry,
                        ToGeometry(proto.exact_geometry()));
  bool keep_renderable = false;
  std::shared_ptr<const Renderable> renderable;
  if (proto.has_renderable()) {
    keep_renderable = true;
    INTR_ASSIGN_OR_RETURN(auto base_renderable,
                          ToRenderable(proto.renderable()));
    renderable = std::make_shared<Renderable>(std::move(base_renderable));
  } else if (proto.has_generated_renderable()) {
    keep_renderable = false;
    INTR_ASSIGN_OR_RETURN(auto base_renderable,
                          ToRenderable(proto.generated_renderable()));
    renderable = std::make_shared<Renderable>(std::move(base_renderable));
  }

  return GeometryTuple{
      .exact_geometry = std::move(exact_geometry),
      .renderable = std::move(renderable),
      .keep_renderable = keep_renderable,
  };
}

absl::StatusOr<intrinsic_proto::geometry::v1::Geometry> ShrinkToSize(
    intrinsic_proto::geometry::v1::Geometry&& geo_proto, int max_size) {
  if (geo_proto.ByteSizeLong() <= max_size || !geo_proto.has_provenance()) {
    return std::move(geo_proto);
  }

  if (!geo_proto.provenance().has_geometry_data()) {
    return std::move(geo_proto);
  }

  // Move the previous geometry data out of the provenance and into a local
  // variable.
  intrinsic_proto::geometry::v1::Geometry previous_geometry =
      std::move(*geo_proto.mutable_provenance()->mutable_geometry_data());
  geo_proto.mutable_provenance()->clear_geometry_data();

  int left_over_size = max_size - geo_proto.ByteSizeLong();
  if (left_over_size <= 0) {
    geo_proto.clear_provenance();
    return std::move(geo_proto);
  }

  INTR_ASSIGN_OR_RETURN(
      *geo_proto.mutable_provenance()->mutable_geometry_data(),
      ShrinkToSize(std::move(previous_geometry), left_over_size));

  return std::move(geo_proto);
}

}  // namespace

absl::StatusOr<intrinsic_proto::geometry::v1::Geometry> ToProto(
    const Geometry& geo, GeometrySerializer* serializer) {
  intrinsic_proto::geometry::v1::Geometry result;
  if (geo.GetExactGeometry().HasPrimitiveShapes() || serializer == nullptr) {
    INTR_ASSIGN_OR_RETURN(*result.mutable_inline_geometry_data(),
                          geometry_details::ToInlineGeometryProto(geo));
  } else {
    INTR_ASSIGN_OR_RETURN(auto geo_storage_refs,
                          serializer->SaveGeometryV1(geo, {}));
    *result.mutable_geo_ref() = geo_storage_refs;
  }

  const auto material_overrides = geo.material_properties();
  if (material_overrides.has_value()) {
    *result.mutable_material_overrides() = *material_overrides;
  }

  if (const auto provenance = geo.provenance(); provenance.has_value()) {
    INTR_ASSIGN_OR_RETURN(
        *result.mutable_provenance(),
        geometry_details::ToProto(provenance.value(), serializer));
  }

  return result;
}

absl::StatusOr<intrinsic_proto::geometry::v1::Geometry> ToInlinedProto(
    const Geometry& geo) {
  return ToProto(geo, nullptr);
}

absl::StatusOr<Geometry> ToGeometry(
    const intrinsic_proto::geometry::v1::Geometry& proto,
    const GeometryDeserializer* deserializer) {
  std::optional<intrinsic_proto::geometry::v1::MaterialProperties>
      material_properties;
  if (proto.has_material_overrides()) {
    material_properties = proto.material_overrides();
  }

  INTR_ASSIGN_OR_RETURN(
      GeometryTuple tuple, ([&]() -> absl::StatusOr<GeometryTuple> {
        if (!proto.has_inline_geometry_data()) {
          if (deserializer == nullptr) {
            return absl::InvalidArgumentError(
                "Geometry proto has refs but no deserializer is provided");
          }

          INTR_ASSIGN_OR_RETURN(
              Geometry geo,
              deserializer->GetGeometry(proto.geo_ref(), std::nullopt));
          return GeometryTuple{
              .exact_geometry = geo.GetExactGeometry(),
              .renderable = geo.GetRenderable(),
              .keep_renderable = geo.KeepRenderableForSerialization()};
        } else {
          return ToGeometryTuple(proto.inline_geometry_data());
        }
      }()));

  std::optional<Geometry::Provenance> provenance;
  if (proto.has_provenance()) {
    auto maybe_provenance = geometry_details::ToGeometryProvenance(
        proto.provenance(), deserializer);
    if (maybe_provenance.ok()) {
      provenance = std::move(maybe_provenance).value();
    } else {
      LOG(ERROR) << "Failed to parse provenance data: "
                 << maybe_provenance.status();
    }
  }

  return Geometry(std::move(tuple.exact_geometry), std::move(tuple.renderable),
                  tuple.keep_renderable, std::move(material_properties),
                  std::move(provenance));
}

absl::StatusOr<Geometry> ToGeometry(
    const intrinsic_proto::geometry::v1::InlineGeometry& proto) {
  return ToGeometry(proto, std::nullopt);
}

absl::StatusOr<Geometry> ToGeometry(
    const intrinsic_proto::geometry::v1::InlineGeometry& proto,
    std::optional<intrinsic_proto::geometry::v1::MaterialProperties>
        material_properties) {
  INTR_ASSIGN_OR_RETURN(auto tuple, ToGeometryTuple(proto));
  auto [exact_geometry, renderable, keep_renderable] = std::move(tuple);
  return Geometry(std::move(exact_geometry), std::move(renderable),
                  keep_renderable, std::move(material_properties),
                  std::nullopt);
}

absl::StatusOr<intrinsic_proto::geometry::v1::TransformedGeometry> ToProto(
    const TransformedGeometry& geo, GeometrySerializer* serializer) {
  intrinsic_proto::geometry::v1::TransformedGeometry result;
  INTR_ASSIGN_OR_RETURN(*result.mutable_geometry(),
                        ToProto(geo.shape(), serializer));
  INTR_ASSIGN_OR_RETURN(*result.mutable_ref_t_shape(),
                        ToGeometricTransform(geo.ref_t_shape()));
  return result;
}

absl::StatusOr<TransformedGeometry> ToGeometry(
    const intrinsic_proto::geometry::v1::TransformedGeometry& proto,
    const GeometryDeserializer* deserializer) {
  INTR_ASSIGN_OR_RETURN(Geometry shape,
                        ToGeometry(proto.geometry(), deserializer));
  eigenmath::Matrix4d ref_t_shape = eigenmath::Matrix4d::Identity();
  if (proto.has_ref_t_shape()) {
    INTR_ASSIGN_OR_RETURN(ref_t_shape, ToAffineTransform(proto.ref_t_shape()));
  }
  return TransformedGeometry(std::move(shape), std::move(ref_t_shape));
}

absl::StatusOr<intrinsic_proto::geometry::v1::Renderable> ToProto(
    const Renderable& geo) {
  intrinsic_proto::geometry::v1::Renderable result;
  result.set_glb_bytes(geo.GetGLBString());
  return result;
}

absl::StatusOr<Renderable> ToRenderable(
    const intrinsic_proto::geometry::v1::Renderable& proto) {
  return Renderable(proto.glb_bytes());
}

absl::StatusOr<intrinsic_proto::geometry::v1::GeometricTransform>
ToGeometricTransform(const eigenmath::Matrix4d& matrix) {
  intrinsic_proto::geometry::v1::GeometricTransform result;
  *result.mutable_matrix4d() = intrinsic::ToProto(matrix);
  return result;
}

absl::StatusOr<eigenmath::Matrix4d> ToAffineTransform(
    const intrinsic_proto::geometry::v1::GeometricTransform& proto) {
  switch (proto.data_case()) {
    case intrinsic_proto::geometry::v1::GeometricTransform::kMatrix4D: {
      INTR_ASSIGN_OR_RETURN(eigenmath::MatrixXd result,
                            intrinsic_proto::FromProto(proto.matrix4d()));
      if (result.rows() != 4 || result.cols() != 4) {
        return absl::InvalidArgumentError(
            "GeometricTransform.matrix4d is not a 4x4 matrix");
      }
      return eigenmath::Matrix4d(result);
    }
    case intrinsic_proto::geometry::v1::GeometricTransform::kTrs: {
      const auto& trs = proto.trs();
      eigenmath::AffineTransform3d transform =
          eigenmath::AffineTransform3d::Identity();
      // Compose T * R * S with right apply functions.
      if (trs.has_translation()) {
        eigenmath::Vector3d translation = FromProto(trs.translation());
        transform.translate(translation);
      }
      if (trs.has_rotation_rpy()) {
        const auto& rpy = trs.rotation_rpy();
        eigenmath::Quaterniond quat =
            eigenmath::QuaternionFromRPY(rpy.r(), rpy.p(), rpy.y());
        transform.rotate(quat);
      }
      if (trs.has_scale()) {
        eigenmath::Vector3d scale = FromProto(trs.scale());
        transform.scale(scale);
      }

      return transform.matrix();
    }
    case intrinsic_proto::geometry::v1::GeometricTransform::DATA_NOT_SET:
      return absl::InvalidArgumentError("GeometricTransform.data not set");
    default:
      return absl::InvalidArgumentError("GeometricTransform.data unknown case");
  }
}

absl::StatusOr<intrinsic_proto::geometry::v1::ExactGeometry> ToProto(
    const ExactGeometry& exact_geo) {
  intrinsic_proto::geometry::v1::ExactGeometry result;
  const auto& primitive_shapes = exact_geo.GetPrimitiveShapes();
  if (!primitive_shapes.empty()) {
    INTR_ASSIGN_OR_RETURN(*result.mutable_primitive_set(),
                          geometry_details::ToProto(primitive_shapes));
  } else {
    if (exact_geo.HasMesh()) {
      INTR_ASSIGN_OR_RETURN(
          *result.mutable_triangle_mesh(),
          ToTriangleMeshProtoV1(exact_geo.GetMesh()->Value()));
    }

    if (exact_geo.HasPointCloud()) {
      INTR_ASSIGN_OR_RETURN(*result.mutable_point_cloud(),
                            ToProto(exact_geo.GetPointCloud()->Value()));
    }
  }

  const auto& options = exact_geo.options();

  if (options.simulation_convex_decomposition_resolution.has_value()) {
    result.mutable_options()->set_simulation_convex_decomposition_resolution(
        options.simulation_convex_decomposition_resolution.value());
  }


  return std::move(result);
}

absl::StatusOr<ExactGeometry> ToGeometry(
    const intrinsic_proto::geometry::v1::ExactGeometry& proto) {
  ExactGeometry::Shape primary_shape;

  switch (proto.data_case()) {
    case intrinsic_proto::geometry::v1::ExactGeometry::kPrimitiveSet: {
      INTR_ASSIGN_OR_RETURN(primary_shape, geometry_details::ToPrimitiveSet(
                                               proto.primitive_set()));

    } break;
    case intrinsic_proto::geometry::v1::ExactGeometry::kTriangleMesh: {
      INTR_ASSIGN_OR_RETURN(auto mesh, FromProto(proto.triangle_mesh()));
      primary_shape = DeDuplicate(std::move(mesh));
    } break;
    case intrinsic_proto::geometry::v1::ExactGeometry::kPointCloud: {
      INTR_ASSIGN_OR_RETURN(auto point_cloud, ToShape(proto.point_cloud()));
      primary_shape = DeDuplicate(std::move(point_cloud));
    } break;
    case intrinsic_proto::geometry::v1::ExactGeometry::DATA_NOT_SET:
      return absl::InvalidArgumentError("ExactGeometry.data not set");
    default:
      return absl::InvalidArgumentError("ExactGeometry.data unknown case");
  }

  GeometryOptions options;

  if (proto.options().has_simulation_convex_decomposition_resolution()) {
    options.simulation_convex_decomposition_resolution =
        proto.options().simulation_convex_decomposition_resolution();
  }


  return ExactGeometry::Create(std::move(primary_shape), std::move(options));
}

namespace geometry_details {

absl::StatusOr<intrinsic_proto::geometry::v1::PrimitiveShape> ToProto(
    const PrimitiveShapePtr& shape) {
  intrinsic_proto::geometry::v1::PrimitiveShape primitive;

  switch (shape->getType()) {
    case ShapeType::BOX: {
      const auto& shape_type = shape->get<Box>();
      *primitive.mutable_box()->mutable_size() =
          ToVectorProto(shape_type.getSize());
      break;
    }
    case ShapeType::CAPSULE: {
      const auto& shape_type = shape->get<Capsule>();
      primitive.mutable_capsule()->set_length(shape_type.getLength());
      primitive.mutable_capsule()->set_radius(shape_type.getRadius());
      break;
    }
    case ShapeType::CYLINDER: {
      const auto& shape_type = shape->get<Cylinder>();
      primitive.mutable_cylinder()->set_length(shape_type.getLength());
      primitive.mutable_cylinder()->set_radius(shape_type.getRadius());
      break;
    }
    case ShapeType::ELLIPSOID: {
      const auto& shape_type = shape->get<Ellipsoid>();
      *primitive.mutable_ellipsoid()->mutable_radii() =
          ToVectorProto(shape_type.getRadii());

      break;
    }
    case ShapeType::SPHERE: {
      const auto& shape_type = shape->get<Sphere>();
      primitive.mutable_sphere()->set_radius(shape_type.getRadius());
      break;
    }
    case ShapeType::FRUSTUM: {
      const auto& shape_type = shape->get<Frustum>();
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

absl::StatusOr<PrimitiveShapePtr> ToPrimitive(
    const intrinsic_proto::geometry::v1::PrimitiveShape& proto) {
  switch (proto.shape_case()) {
    case intrinsic_proto::geometry::v1::PrimitiveShape::kBox: {
      INTR_ASSIGN_OR_RETURN(Box box, ToShape(proto.box()));
      return std::make_shared<Box>(box);
    }
    case intrinsic_proto::geometry::v1::PrimitiveShape::kCylinder: {
      INTR_ASSIGN_OR_RETURN(Cylinder cylinder, ToShape(proto.cylinder()));
      return std::make_shared<Cylinder>(cylinder);
    }
    case intrinsic_proto::geometry::v1::PrimitiveShape::kSphere: {
      INTR_ASSIGN_OR_RETURN(Sphere sphere, ToShape(proto.sphere()));
      return std::make_shared<Sphere>(sphere);
    }
    case intrinsic_proto::geometry::v1::PrimitiveShape::kEllipsoid: {
      INTR_ASSIGN_OR_RETURN(Ellipsoid ellipsoid, ToShape(proto.ellipsoid()));
      return std::make_shared<Ellipsoid>(ellipsoid);
    }
    case intrinsic_proto::geometry::v1::PrimitiveShape::kCapsule: {
      INTR_ASSIGN_OR_RETURN(Capsule capsule, ToShape(proto.capsule()));
      return std::make_shared<Capsule>(capsule);
    }
    case intrinsic_proto::geometry::v1::PrimitiveShape::kFrustum: {
      INTR_ASSIGN_OR_RETURN(Frustum frustum, ToShape(proto.frustum()));
      return std::make_shared<Frustum>(frustum);
    }
    case intrinsic_proto::geometry::v1::PrimitiveShape::SHAPE_NOT_SET: {
      return absl::InvalidArgumentError("Unset primitive shape type");
    }
    default:
      return absl::InvalidArgumentError(
          "PrimitiveShape.shape_case unknown case");
  }
}

absl::StatusOr<intrinsic_proto::geometry::v1::TransformedPrimitiveShape>
ToProto(const TransformedPrimitiveShapePtr& shape) {
  intrinsic_proto::geometry::v1::TransformedPrimitiveShape result;
  INTR_ASSIGN_OR_RETURN(*result.mutable_shape(), ToProto(shape.shape()));
  INTR_ASSIGN_OR_RETURN(*result.mutable_ref_t_shape(),
                        ToGeometricTransform(shape.ref_t_shape()));
  return result;
}

absl::StatusOr<TransformedPrimitiveShapePtr> ToPrimitive(
    const intrinsic_proto::geometry::v1::TransformedPrimitiveShape& proto) {
  INTR_ASSIGN_OR_RETURN(auto shape, ToPrimitive(proto.shape()));
  eigenmath::Matrix4d ref_t_shape = eigenmath::Matrix4d::Identity();
  if (proto.has_ref_t_shape()) {
    INTR_ASSIGN_OR_RETURN(ref_t_shape, ToAffineTransform(proto.ref_t_shape()));
  }
  return TransformedPrimitiveShapePtr(std::move(shape), std::move(ref_t_shape));
}

absl::StatusOr<intrinsic_proto::geometry::v1::TransformedPrimitiveShapeSet>
ToProto(const std::vector<TransformedPrimitiveShapePtr>& shape) {
  intrinsic_proto::geometry::v1::TransformedPrimitiveShapeSet result;
  for (const auto& shape : shape) {
    INTR_ASSIGN_OR_RETURN(*result.add_primitives(), ToProto(shape));
  }
  return result;
}

absl::StatusOr<std::vector<TransformedPrimitiveShapePtr>> ToPrimitiveSet(
    const intrinsic_proto::geometry::v1::TransformedPrimitiveShapeSet&
        proto_set) {
  std::vector<TransformedPrimitiveShapePtr> primitive_shapes;
  for (const auto& proto : proto_set.primitives()) {
    INTR_ASSIGN_OR_RETURN(auto shape, ToPrimitive(proto));
    primitive_shapes.emplace_back(shape);
  }
  return primitive_shapes;
}

absl::StatusOr<intrinsic_proto::geometry::v1::InlineGeometry>
ToInlineGeometryProto(const Geometry& geo) {
  intrinsic_proto::geometry::v1::InlineGeometry result;
  INTR_ASSIGN_OR_RETURN(*result.mutable_exact_geometry(),
                        ToProto(geo.GetExactGeometry()));

  auto renderable = geo.GetRenderable();
  if (renderable != nullptr) {
    if (geo.KeepRenderableForSerialization()) {
      INTR_ASSIGN_OR_RETURN(*result.mutable_renderable(), ToProto(*renderable));

    } else {
      INTR_ASSIGN_OR_RETURN(*result.mutable_generated_renderable(),
                            ToProto(*renderable));
    }
  }

  return result;
}

absl::StatusOr<Geometry::Provenance> ToGeometryProvenance(
    const intrinsic_proto::geometry::v1::GeometryProvenance& proto,
    const GeometryDeserializer* deserializer) {
  std::variant<std::string, std::shared_ptr<const Geometry>> previous_geometry;

  switch (proto.data_case()) {
    case intrinsic_proto::geometry::v1::GeometryProvenance::kGeometryDataUri: {
      previous_geometry = proto.geometry_data_uri();
      break;
    }
    case intrinsic_proto::geometry::v1::GeometryProvenance::kGeometryData: {
      INTR_ASSIGN_OR_RETURN(Geometry parsed_geo,
                            ToGeometry(proto.geometry_data(), deserializer));
      previous_geometry =
          std::make_shared<const Geometry>(std::move(parsed_geo));
      break;
    }
    case intrinsic_proto::geometry::v1::GeometryProvenance::DATA_NOT_SET: {
      return absl::InvalidArgumentError("GeometryProvenance.data not set");
    }
  }

  return Geometry::Provenance{
      .human_readable_update_reason = proto.human_readable_update_reason(),
      .previous_geometry = std::move(previous_geometry),
  };
}

absl::StatusOr<intrinsic_proto::geometry::v1::GeometryProvenance> ToProto(
    const Geometry::Provenance& geo_provenance,
    GeometrySerializer* serializer) {
  intrinsic_proto::geometry::v1::GeometryProvenance result;
  result.set_human_readable_update_reason(
      geo_provenance.human_readable_update_reason);

  if (std::holds_alternative<std::string>(geo_provenance.previous_geometry)) {
    result.set_geometry_data_uri(
        std::get<std::string>(geo_provenance.previous_geometry));
  } else if (std::holds_alternative<std::shared_ptr<const Geometry>>(
                 geo_provenance.previous_geometry)) {
    auto geo_ptr = std::get<std::shared_ptr<const Geometry>>(
        geo_provenance.previous_geometry);
    INTR_ASSIGN_OR_RETURN(auto geo_proto, ToProto(*geo_ptr, serializer));

    // If we don't have primitive shapes or the proto is too large, we store the
    // geometry data in CAS and then put the URI into the provenance data.
    if (geo_ptr->GetExactGeometry().GetPrimitiveShapes().empty() ||
        geo_proto.ByteSizeLong() > kMaxGeometryProtoSize) {
      // TODO(b/427564183): Use CAS to store the geometry data instead of
      // attempting to shrink it by removing old references.

      // We use 80% of the max size to account for the fact that our algorithm
      // is not exact.
      INTR_ASSIGN_OR_RETURN(
          geo_proto,
          ShrinkToSize(std::move(geo_proto), 0.8 * kMaxGeometryProtoSize));
    }

    *result.mutable_geometry_data() = std::move(geo_proto);
  } else {
    return absl::InvalidArgumentError(
        "GeometryProvenance.previous_geometry is not a string or shared_ptr "
        "to Geometry");
  }

  return result;
}

}  // namespace geometry_details

}  // namespace intrinsic::geo
