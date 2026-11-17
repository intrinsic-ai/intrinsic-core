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

#include "intrinsic/scene/util/scene_object_creation.h"

#include <memory>
#include <string>

#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "intrinsic/geometry/api/affine_transform_of_geometry.h"
#include "intrinsic/geometry/api/geometry.h"
#include "intrinsic/geometry/storage/geometry_serializer.h"
#include "intrinsic/kinematics/types/joint_limits.h"
#include "intrinsic/kinematics/types/joint_limits.pb.h"
#include "intrinsic/scene/proto/v1/entity.pb.h"
#include "intrinsic/scene/proto/v1/scene_object.pb.h"
#include "intrinsic/scene/proto/v1/scene_object_updates.pb.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/component/geometry_component.h"
#include "intrinsic/world/geometry_types.h"
#include "intrinsic/world/proto/geometry_component.pb.h"

namespace intrinsic {
namespace scene_object {

constexpr absl::string_view kJointNamePrefix = "joint_a";
using ::intrinsic_proto::scene_object::v1::UpdateJointsRequest;

using ::intrinsic_proto::scene_object::v1::SceneObject;

absl::StatusOr<SceneObject> SingleLinkSceneObjectFromGeometry(
    const Geometry& geo, GeometrySerializer* geo_serializer) {
  SceneObject scene_object;
  auto* link = scene_object.mutable_entities()->Add();
  link->set_name("link");
  link->mutable_link()->mutable_physics_component();

  std::unique_ptr<GeometryComponent> geo_comp = GeometryComponent::Create();
  geo_comp->SetGeometry(kKindVisualGeometry,
                        {{"0", {TransformedGeometry(geo)}}});
  geo_comp->SetGeometry(kKindCollisionGeometry,
                        {{"0", {TransformedGeometry(geo)}}});
  INTR_ASSIGN_OR_RETURN(*link->mutable_link()->mutable_geometry_component(),
                        geo_comp->ToProto(geo_serializer));

  return scene_object;
}

intrinsic_proto::scene_object::v1::UpdateJointsRequest ToSceneObjectUpdate(
    const intrinsic::JointLimits& system_limits,
    const intrinsic::JointLimits& application_limits) {
  UpdateJointsRequest update;
  for (int i = 0; i < system_limits.size(); ++i) {
    intrinsic_proto::JointLimitUpdate joint_system_limits,
        joint_application_limits;
    joint_system_limits.set_min_position(system_limits.min_position(i));
    joint_system_limits.set_max_position(system_limits.max_position(i));
    joint_system_limits.set_max_velocity(system_limits.max_velocity(i));
    joint_system_limits.set_max_acceleration(system_limits.max_acceleration(i));
    joint_system_limits.set_max_jerk(system_limits.max_jerk(i));
    joint_system_limits.set_max_effort(system_limits.max_torque(i));
    joint_application_limits.set_min_position(
        application_limits.min_position(i));
    joint_application_limits.set_max_position(
        application_limits.max_position(i));
    joint_application_limits.set_max_velocity(
        application_limits.max_velocity(i));
    joint_application_limits.set_max_acceleration(
        application_limits.max_acceleration(i));
    joint_application_limits.set_max_jerk(application_limits.max_jerk(i));
    joint_application_limits.set_max_effort(application_limits.max_torque(i));
    const std::string joint_name = absl::StrCat(kJointNamePrefix, i + 1);
    update.mutable_joint_system_limits()->insert(
        {joint_name, joint_system_limits});
    update.mutable_joint_application_limits()->insert(
        {joint_name, joint_application_limits});
  }
  return update;
}

}  // namespace scene_object
}  // namespace intrinsic
