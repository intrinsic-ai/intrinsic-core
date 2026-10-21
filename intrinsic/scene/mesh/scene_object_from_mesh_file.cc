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

#include "intrinsic/scene/mesh/scene_object_from_mesh_file.h"

#include <optional>
#include <string>
#include <utility>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/geometry/api/affine_transform_of_geometry.h"
#include "intrinsic/geometry/api/compute_axis_aligned_bounding_box_3d.h"
#include "intrinsic/geometry/api/file_io.h"
#include "intrinsic/geometry/api/geometry.h"
#include "intrinsic/geometry/processing/pipeline_config.pb.h"
#include "intrinsic/geometry/proto/geometry_service.pb.h"
#include "intrinsic/geometry/proto/geometry_storage_refs.pb.h"
#include "intrinsic/geometry/proto/renderable.pb.h"
#include "intrinsic/geometry/proto/v1/material.pb.h"
#include "intrinsic/geometry/storage/geometry_serializer.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/proto/vector3.pb.h"
#include "intrinsic/math/proto_conversion.h"
#include "intrinsic/scene/proto/v1/entity.pb.h"
#include "intrinsic/scene/proto/v1/scene_object.pb.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/component/geometry_component.h"
#include "intrinsic/world/component/physics_component.h"
#include "intrinsic/world/geometry_types.h"
#include "intrinsic/world/objects/object_world_creation_utils.h"
#include "intrinsic/world/proto/physics_component.pb.h"
#include "ortools/base/path.h"

namespace intrinsic::scene_object {

// Create a physics component with mass and center of mass with our best
// guess based on the transformed geometry.
absl::StatusOr<intrinsic_proto::world::PhysicsComponent>
PhysicsComponentFromGeometry(const TransformedGeometry& geometry) {
  // Default physics component is initialized with sane values for physics, e.g.
  // for friction.
  auto default_physics_component = PhysicsComponent::Create();
  INTR_ASSIGN_OR_RETURN(
      intrinsic_proto::world::PhysicsComponent physics_component,
      default_physics_component->ToProto());
  INTR_ASSIGN_OR_RETURN(auto aabb,
                        ComputeAxisAlignedBoundingBox3d(geometry.shape()));

  const eigenmath::Vector3d transformed_diagonal =
      (geometry.ref_t_shape() *
       eigenmath::Vector4d(aabb.GetDiagonal().x(), aabb.GetDiagonal().y(),
                           aabb.GetDiagonal().z(), 1.0))
          .head(3);
  const double volume = transformed_diagonal.x() * transformed_diagonal.y() *
                        transformed_diagonal.z();
  // Assume 1000kg/m^3 density for the bounding box.
  // http://sdformat.org/spec?ver=1.11&elem=collision#collision_density
  physics_component.set_mass_kg(volume * 1000);

  eigenmath::Vector3d center_of_mass =
      (geometry.ref_t_shape() * eigenmath::Vector4d(aabb.GetCenter().x(),
                                                    aabb.GetCenter().y(),
                                                    aabb.GetCenter().z(), 1.0))
          .head(3);

  // Assume center of mass is at the center of the bounding box.
  *physics_component.mutable_this_t_center_of_mass() =
      ToProto(Pose3d(center_of_mass));

  return physics_component;
}

// Creates a SceneObject from `geometry`. The new scene object will be named
// `name` and contain a link entity named `{name}_link` with `geometry` set to
// both the visual and collision geometry.
absl::StatusOr<intrinsic_proto::scene_object::v1::SceneObject>
SceneObjectFromGeometry(TransformedGeometry geometry, absl::string_view name,
                        GeometrySerializer& geo_serializer) {
  intrinsic_proto::scene_object::v1::SceneObject scene_object;
  const std::string ow_compatible_name =
      object_world::GetObjectViewCompatibleName(name);
  scene_object.set_name(ow_compatible_name);

  NamedGeometrySet geometry_set{{"0", std::move(geometry)}};
  auto geometry_component = GeometryComponent::Create();
  geometry_component->SetGeometry(kKindCollisionGeometry, geometry_set);
  geometry_component->SetGeometry(kKindVisualGeometry, geometry_set);

  intrinsic_proto::scene_object::v1::Entity link_entity;
  link_entity.set_name(absl::StrCat(ow_compatible_name, "_link"));
  INTR_ASSIGN_OR_RETURN(
      *link_entity.mutable_link()->mutable_geometry_component(),
      geometry_component->ToProto(&geo_serializer));

  INTR_ASSIGN_OR_RETURN(
      *link_entity.mutable_link()->mutable_physics_component(),
      PhysicsComponentFromGeometry(geometry_set.begin()->second));

  *scene_object.add_entities() = std::move(link_entity);
  return scene_object;
}

absl::StatusOr<intrinsic_proto::scene_object::v1::SceneObject>
SceneObjectFromMeshFile(absl::string_view filename,
                        std::optional<double> scale_factor,
                        GeometrySerializer& geo_serializer) {
  eigenmath::Vector3d scale = eigenmath::Vector3d::Ones();
  if (scale_factor.has_value()) {
    if (scale_factor.value() <= 0) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Scale factor must be positive, got ", scale_factor.value(), "."));
    }
    scale = eigenmath::Vector3d::Ones() * scale_factor.value();
  }

  INTR_ASSIGN_OR_RETURN(auto geometry, LoadMeshFileToGeometry(filename, scale));
  return SceneObjectFromGeometry(TransformedGeometry(std::move(geometry)),
                                 file::Stem(filename), geo_serializer);
}

}  // namespace intrinsic::scene_object
