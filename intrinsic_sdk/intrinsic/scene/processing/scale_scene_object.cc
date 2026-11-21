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

#include "intrinsic/scene/processing/scale_scene_object.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/geometry/api/io.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/proto/vector3.pb.h"
#include "intrinsic/math/proto_conversion.h"
#include "intrinsic/scene/proto/v1/entity.pb.h"
#include "intrinsic/scene/proto/v1/scene_object.pb.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/proto/geometry_component.pb.h"

namespace intrinsic {
namespace scene_object {

using SceneObject = intrinsic_proto::scene_object::v1::SceneObject;
using SceneObjectEntity = intrinsic_proto::scene_object::v1::Entity;

namespace {

template <typename Vector3Type>
bool IsOnes(const Vector3Type& v) {
  return v.x() == 1.0 && v.y() == 1.0 && v.z() == 1.0;
}

template <typename Vector3Type>
bool IsUniform(const Vector3Type& v) {
  return v.x() == v.y() && v.x() == v.z();
}

// Scales `geometry_component` by `scale`. Scales each transformed geometry by
// `scaled_ref_t_shape_aff = scale * ref_t_shape_aff`. Returns an error if any
// existing `ref_t_shape_aff` cannot be converted to a 4x4 Matrix.
absl::StatusOr<intrinsic_proto::world::GeometryComponent>
ScaleGeometryComponent(
    const intrinsic_proto::world::GeometryComponent& geometry_component,
    const intrinsic_proto::Vector3& scale) {
  const eigenmath::Matrix4d scale_matrix =
      eigenmath::Vector4d(scale.x(), scale.y(), scale.z(), 1.0).asDiagonal();

  auto scaled_geometry_component = geometry_component;
  for (auto& [_, geometry_set] :
       *scaled_geometry_component.mutable_named_geometries()) {
    for (auto& geometry : *geometry_set.mutable_geometries()) {
      eigenmath::Matrix4d ref_t_shape_aff = eigenmath::Matrix4d::Identity();
      if (geometry.has_ref_t_shape_aff()) {
        if (geometry.ref_t_shape_aff().rows() != 4 ||
            geometry.ref_t_shape_aff().cols() != 4) {
          return absl::InvalidArgumentError(
              "ref_t_shape_aff is not a 4x4 matrix.");
        }
        INTR_ASSIGN_OR_RETURN(ref_t_shape_aff,
                              FromProto(geometry.ref_t_shape_aff()));
      }
      const eigenmath::Matrix4d scaled_ref_t_shape_aff =
          scale_matrix * ref_t_shape_aff;
      *geometry.mutable_ref_t_shape_aff() = ToProto(scaled_ref_t_shape_aff);
    }
    for (auto& [name, geometry] : *geometry_set.mutable_named_geometries()) {
      eigenmath::Matrix4d ref_t_shape = eigenmath::Matrix4d::Identity();
      if (geometry.has_ref_t_shape()) {
        INTR_ASSIGN_OR_RETURN(ref_t_shape,
                              ToAffineTransform(geometry.ref_t_shape()));
      }
      const eigenmath::Matrix4d scaled_ref_t_shape = scale_matrix * ref_t_shape;
      INTR_ASSIGN_OR_RETURN(*geometry.mutable_ref_t_shape(),
                            ToGeometricTransform(scaled_ref_t_shape));
    }
  }

  return scaled_geometry_component;
}

// Attempts to scale `parent_to_this` pose by applying the scale to the
// translation component. If the scale is non-uniform, returns an error when a
// rotation is present in `parent_t_this`.
absl::StatusOr<Pose3d> ScalePose3d(const Pose3d& parent_t_this,
                                   const eigenmath::Vector3d& scale) {
  if (!IsUniform(scale)) {
    if (!parent_t_this.quaternion().isApprox(
            eigenmath::Quaterniond::Identity())) {
      return absl::InvalidArgumentError(
          "Scaling an entity non-uniformly with rotation is not "
          "supported");
    }
  }

  return Pose3d(parent_t_this.so3(),
                parent_t_this.translation().cwiseProduct(scale));
}

absl::StatusOr<SceneObjectEntity> ScaleSceneObjectEntity(
    const SceneObjectEntity& entity, const intrinsic_proto::Vector3& scale) {
  if (!entity.has_link() && !entity.has_frame()) {
    return absl::InvalidArgumentError(
        "Only scaling a scene object entity with link or frame is supported.");
  }
  // TODO(b/341316097): Implement scaling of physics properties for scene
  // objects.

  Pose3d parent_t_this;
  if (entity.has_parent_t_this()) {
    INTR_ASSIGN_OR_RETURN(parent_t_this, FromProto(entity.parent_t_this()));
  }
  INTR_ASSIGN_OR_RETURN(const Pose3d scaled_parent_t_this,
                        ScalePose3d(parent_t_this, FromProto(scale)),
                        _ << "Scaling a scene object entity pose fails.");

  SceneObjectEntity scaled_entity = entity;
  *scaled_entity.mutable_parent_t_this() = ToProto(scaled_parent_t_this);

  if (entity.has_link()) {
    INTR_ASSIGN_OR_RETURN(
        *scaled_entity.mutable_link()->mutable_geometry_component(),
        ScaleGeometryComponent(entity.link().geometry_component(), scale));
  }

  return scaled_entity;
}

}  // namespace

absl::StatusOr<SceneObject> ScaleSceneObject(
    const SceneObject& scene_object, const intrinsic_proto::Vector3& scale) {
  if (IsOnes(scale)) {
    return scene_object;
  }

  SceneObject scaled_scene_object = scene_object;
  for (auto& entity : *scaled_scene_object.mutable_entities()) {
    INTR_ASSIGN_OR_RETURN(entity, ScaleSceneObjectEntity(entity, scale));
  }
  return scaled_scene_object;
}

}  // namespace scene_object
}  // namespace intrinsic
