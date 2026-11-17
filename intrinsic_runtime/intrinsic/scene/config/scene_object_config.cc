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

#include "intrinsic/scene/config/scene_object_config.h"

#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "intrinsic/scene/proto/v1/collision_rules.pb.h"
#include "intrinsic/scene/proto/v1/scene_object.pb.h"
#include "intrinsic/scene/proto/v1/scene_object_config.pb.h"
#include "intrinsic/scene/proto/v1/scene_object_updates.pb.h"
#include "intrinsic/scene/util/scene_object_updates.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic {
namespace scene_object {

using ::intrinsic_proto::scene_object::v1::GeometryUpdate;
using ::intrinsic_proto::scene_object::v1::SceneObject;
using ::intrinsic_proto::scene_object::v1::SceneObjectConfig;
using ::intrinsic_proto::scene_object::v1::SceneObjectInstanceUpdates;
using ::intrinsic_proto::scene_object::v1::UpdateCollisionRules;

absl::StatusOr<SceneObjectUpdateResult> ProcessSceneObjectConfig(
    SceneObject object, const SceneObjectConfig& config,
    UpdatePolicy update_policy) {
  SceneObjectInstanceUpdates all_updates;

  for (const auto& update : config.entity_pose_updates()) {
    *all_updates.add_updates()->mutable_entity_pose() = update;
  }

  for (const auto& update : config.new_frames()) {
    *all_updates.add_updates()->mutable_create_frame() = update;
  }

  if (config.has_named_joint_configs()) {
    *all_updates.add_updates()->mutable_set_named_configurations() =
        config.named_joint_configs();
  }

  if (config.has_initial_joint_settings()) {
    *all_updates.add_updates()->mutable_update_joints() =
        config.initial_joint_settings();
  }

  if (config.has_initial_cartesian_limits()) {
    *all_updates.add_updates()->mutable_cartesian_limits() =
        config.initial_cartesian_limits();
  }

  if (config.has_initial_ik_solvers()) {
    *all_updates.add_updates()->mutable_set_ik_solvers() =
        config.initial_ik_solvers();
  }

  if (config.has_simulation_properties()) {
    *all_updates.add_updates()->mutable_update_simulation_properties() =
        config.simulation_properties();
  }

  for (const auto& [entity_name, update] : config.geometry_overrides()) {
    GeometryUpdate update_with_name = update;
    update_with_name.set_entity_name(entity_name);
    *all_updates.add_updates()->mutable_update_geometry() = update_with_name;
  }

  if (config.has_updates()) {
    for (const auto& update : config.updates().updates()) {
      *all_updates.add_updates() = update;
    }
  }

  return ProcessSceneObjectUpdates(std::move(object), all_updates,
                                   update_policy);
}

}  // namespace scene_object
}  // namespace intrinsic
