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

#include "intrinsic/scene/processing/util/transform_util.h"

#include <ranges>

#include "absl/status/status.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/geometry/api/io.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/proto_conversion.h"
#include "intrinsic/scene/conversion/scene_object_model_utils.h"
#include "intrinsic/scene/proto/v1/entity.pb.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/proto/geometry_component.pb.h"
#include "intrinsic/world/proto/kinematics_component.pb.h"

namespace intrinsic {
namespace scene_object {

using ::intrinsic_proto::scene_object::v1::Entity;
using ::intrinsic_proto::world::KinematicsComponent;

absl::Status TransformEntityByPose(Entity& entity,
                                   const Pose3d& new_parent_t_old_parent) {
  if (entity.has_joint()) {
    INTR_ASSIGN_OR_RETURN(const Pose3d old_parent_t_this,
                          ResolveJointEntityPose(entity));
    KinematicsComponent* const kinematics =
        entity.mutable_joint()->mutable_kinematics_component();
    Pose3d parent_t_inboard;
    if (kinematics->has_parent_t_inboard()) {
      INTR_ASSIGN_OR_RETURN(const Pose3d parsed_parent_t_inboard,
                            FromProto(kinematics->parent_t_inboard()));
      parent_t_inboard = parsed_parent_t_inboard;
    }
    const Pose3d new_parent_t_inboard =
        new_parent_t_old_parent * parent_t_inboard;
    *kinematics->mutable_parent_t_inboard() = ToProto(new_parent_t_inboard);
    *entity.mutable_parent_t_this() =
        ToProto(new_parent_t_old_parent * old_parent_t_this);
    return absl::OkStatus();
  }

  Pose3d parent_t_this;
  if (entity.has_parent_t_this()) {
    INTR_ASSIGN_OR_RETURN(parent_t_this, FromProto(entity.parent_t_this()));
  }

  Pose3d new_parent_t_this = new_parent_t_old_parent * parent_t_this;
  *entity.mutable_parent_t_this() = ToProto(new_parent_t_this);

  return absl::OkStatus();
}

absl::Status TransformLinkGeometry(Entity& entity,
                                   const Pose3d& new_parent_t_old_parent) {
  if (!entity.has_link()) {
    return absl::OkStatus();
  }

  auto* geometry_component =
      entity.mutable_link()->mutable_geometry_component();

  for (auto& geometries :
       std::views::values(*geometry_component->mutable_named_geometries())) {
    for (auto& geometry : *geometries.mutable_geometries()) {
      eigenmath::Matrix4d ref_t_shape = eigenmath::Matrix4d::Identity();
      if (geometry.has_ref_t_shape_aff()) {
        INTR_ASSIGN_OR_RETURN(
            eigenmath::MatrixXd ref_t_shape_xd,
            intrinsic_proto::FromProto(geometry.ref_t_shape_aff()));

        if (ref_t_shape_xd.rows() != 4 || ref_t_shape_xd.cols() != 4) {
          return absl::InvalidArgumentError(
              "ref_t_shape_aff is not a 4x4 matrix.");
        }
        ref_t_shape = ref_t_shape_xd;
      }

      eigenmath::Matrix4d transformed_ref_t_shape =
          new_parent_t_old_parent.matrix() * ref_t_shape;

      *geometry.mutable_ref_t_shape_aff() = ToProto(transformed_ref_t_shape);
    }

    // Transform named geometries using value_view for the inner map as well.
    for (auto& geometry :
         std::views::values(*geometries.mutable_named_geometries())) {
      eigenmath::Matrix4d ref_t_shape = eigenmath::Matrix4d::Identity();
      if (geometry.has_ref_t_shape()) {
        INTR_ASSIGN_OR_RETURN(ref_t_shape,
                              ToAffineTransform(geometry.ref_t_shape()));
      }

      eigenmath::Matrix4d transformed_ref_t_shape =
          new_parent_t_old_parent.matrix() * ref_t_shape;

      INTR_ASSIGN_OR_RETURN(*geometry.mutable_ref_t_shape(),
                            ToGeometricTransform(transformed_ref_t_shape));
    }
  }
  return absl::OkStatus();
}
}  // namespace scene_object
}  // namespace intrinsic
