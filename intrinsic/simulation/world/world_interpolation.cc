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

#include "intrinsic/simulation/world/world_interpolation.h"

#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "intrinsic/eigenmath/interpolation.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/proto_conversion.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/objects/object_entity_filter.h"
#include "intrinsic/world/objects/object_world_client.h"
#include "intrinsic/world/proto/object_world_service.pb.h"
#include "intrinsic/world/proto/object_world_updates.pb.h"

namespace intrinsic::simulation {
namespace {

using intrinsic_proto::world::ObjectWorldUpdate;
using intrinsic_proto::world::ObjectWorldUpdates;
using intrinsic_proto::world::UpdateObjectJointRequest;
using intrinsic_proto::world::UpdateObjectJointsRequest;
using intrinsic_proto::world::UpdateTransformRequest;

absl::StatusOr<UpdateObjectJointsRequest> HandleUpdateObjectJointsRequest(
    const world::ObjectWorldClient& world,
    const UpdateObjectJointsRequest& update_object_joints, double t) {
  UpdateObjectJointsRequest result = update_object_joints;

  if (update_object_joints.has_joint_system_limits()) {
    return absl::InvalidArgumentError(
        "Unsupported update: UpdateObjectJointsRequest::joint_system_limits.");
  }
  if (update_object_joints.has_joint_application_limits()) {
    return absl::InvalidArgumentError(
        "Unsupported update: "
        "UpdateObjectJointsRequest::joint_application_limits.");
  }

  if (update_object_joints.joint_positions_size() != 0) {
    INTR_ASSIGN_OR_RETURN(
        const auto kin_obj,
        world.GetKinematicObject(update_object_joints.object()));
    eigenmath::VectorXd theta = kin_obj.JointPositions();
    if (theta.size() != update_object_joints.joint_positions_size()) {
      return ::intrinsic::InvalidArgumentErrorBuilder()
             << "Attempting to interpolate robot with " << theta.size()
             << " dofs when update contains "
             << update_object_joints.joint_positions_size() << " dofs.";
    }
    for (int i = 0; i < update_object_joints.joint_positions_size(); ++i) {
      result.set_joint_positions(
          i, eigenmath::InterpolateLinear(
                 t, theta[i], update_object_joints.joint_positions(i)));
    }
  }

  return result;
}

absl::StatusOr<UpdateObjectJointRequest> HandleUpdateObjectJointRequest(
    const world::ObjectWorldClient& world,
    const UpdateObjectJointRequest& update_object_joint, double t) {
  UpdateObjectJointRequest result = update_object_joint;
  INTR_ASSIGN_OR_RETURN(const auto kin_obj,
                        world.GetKinematicObject(update_object_joint.object()));
  eigenmath::VectorXd theta = kin_obj.JointPositions();

  // in DOF order
  auto joint_entity_ids =
      kin_obj.Proto().kinematic_object_component().joint_entity_ids();
  std::optional<size_t> target_joint_idx;

  for (size_t i = 0; i < joint_entity_ids.size(); ++i) {
    const auto& joint_entity_id = joint_entity_ids[i];
    if (!kin_obj.Proto().entities().contains(joint_entity_id)) {
      return absl::InternalError("Joint entity is not found in object!");
    }

    absl::string_view joint_local_name =
        kin_obj.Proto().entities().at(joint_entity_id).name();

    if (joint_local_name == update_object_joint.joint_name()) {
      target_joint_idx = i;
      break;
    }
  }

  if (!target_joint_idx.has_value()) {
    return absl::NotFoundError(absl::StrCat("Joint with name `",
                                            update_object_joint.joint_name(),
                                            "` was not found."));
  }

  eigenmath::VectorXd theta_new = theta;
  result.set_joint_position(
      eigenmath::InterpolateLinear(t, theta[target_joint_idx.value()],
                                   update_object_joint.joint_position()));
  return result;
}

absl::StatusOr<UpdateTransformRequest> HandleUpdateTransformRequest(
    const world::ObjectWorldClient& world,
    const UpdateTransformRequest& update_transform, double t) {
  UpdateTransformRequest result = update_transform;
  INTR_ASSIGN_OR_RETURN(auto node_a,
                        world.GetTransformNode(update_transform.node_a()));
  INTR_ASSIGN_OR_RETURN(auto node_b,
                        world.GetTransformNode(update_transform.node_b()));

  std::optional<world::ObjectEntityFilter> node_a_filter = std::nullopt;
  if (update_transform.has_node_a_filter()) {
    node_a_filter =
        world::ObjectEntityFilter::FromProto(update_transform.node_a_filter());
  }
  std::optional<world::ObjectEntityFilter> node_b_filter = std::nullopt;
  if (update_transform.has_node_b_filter()) {
    node_b_filter =
        world::ObjectEntityFilter::FromProto(update_transform.node_b_filter());
  }

  INTR_ASSIGN_OR_RETURN(
      const Pose pose0,
      world.GetTransform(node_a, node_a_filter, node_b, node_b_filter));
  INTR_ASSIGN_OR_RETURN(const Pose pose1, FromProto(update_transform.a_t_b()));

  const Pose current_a_t_b = eigenmath::Interpolate(t, pose0, pose1);
  *result.mutable_a_t_b() = ToProto(current_a_t_b);
  return result;
}

}  // namespace

// t must be inside the closed interval of [0, 1].
absl::StatusOr<intrinsic_proto::world::ObjectWorldUpdate>
InterpolateObjectWorldUpdate(
    const world::ObjectWorldClient& world,
    const intrinsic_proto::world::ObjectWorldUpdate& world_update, double t) {
  if (t < 0 || t > 1) {
    return absl::InvalidArgumentError("t is outside [0-1]");
  }

  if (t == 1) {
    return world_update;
  }

  ObjectWorldUpdate result;
  if (t == 0) {
    return result;
  }

  switch (world_update.update_case()) {
    case intrinsic_proto::world::ObjectWorldUpdate::kUpdateTransform: {
      INTR_ASSIGN_OR_RETURN(auto update_transform,
                            HandleUpdateTransformRequest(
                                world, world_update.update_transform(), t));
      *result.mutable_update_transform() = update_transform;
      break;
    }
    case intrinsic_proto::world::ObjectWorldUpdate::kUpdateObjectJoints: {
      INTR_ASSIGN_OR_RETURN(auto update_object_joints,
                            HandleUpdateObjectJointsRequest(
                                world, world_update.update_object_joints(), t));
      *result.mutable_update_object_joints() = update_object_joints;
      break;
    }
    case intrinsic_proto::world::ObjectWorldUpdate::kUpdateObjectJoint: {
      INTR_ASSIGN_OR_RETURN(auto update_object_joint,
                            HandleUpdateObjectJointRequest(
                                world, world_update.update_object_joint(), t));
      *result.mutable_update_object_joint() = std::move(update_object_joint);
      break;
    }
    case intrinsic_proto::world::ObjectWorldUpdate::kUpdateCollisionSettings:
      return absl::InvalidArgumentError(
          "Unsupported update: UpdateCollisionSettingsRequest.");
    case intrinsic_proto::world::ObjectWorldUpdate::kUpdateObjectName:
      return absl::InvalidArgumentError(
          "Unsupported update: UpdateObjectNameRequest.");
    case intrinsic_proto::world::ObjectWorldUpdate::kUpdateFrameName:
      return absl::InvalidArgumentError(
          "Unsupported update: UpdateFrameNameRequest.");
    case intrinsic_proto::world::ObjectWorldUpdate::
        kUpdateKinematicObjectProperties:
      return absl::InvalidArgumentError(
          "Unsupported update: UpdateKinematicObjectPropertiesRequest.");
    case intrinsic_proto::world::ObjectWorldUpdate::kUpdateObjectProperties:
      return absl::InvalidArgumentError(
          "Unsupported update: UpdateObjectPropertiesRequest.");
    case intrinsic_proto::world::ObjectWorldUpdate::kUpdateEntityProperties:
      return absl::InvalidArgumentError(
          "Unsupported update: UpdateEntityPropertiesRequest.");
    case intrinsic_proto::world::ObjectWorldUpdate::kUpdateFrameProperties:
      return absl::InvalidArgumentError(
          "Unsupported update: kUpdateFramePropertiesRequest.");
    case intrinsic_proto::world::ObjectWorldUpdate::kToggleCollisions:
    case intrinsic_proto::world::ObjectWorldUpdate::kCreateObject:
    case intrinsic_proto::world::ObjectWorldUpdate::kDeleteObject:
    case intrinsic_proto::world::ObjectWorldUpdate::kCreateFrame:
    case intrinsic_proto::world::ObjectWorldUpdate::kDeleteFrame:
    case intrinsic_proto::world::ObjectWorldUpdate::kReparentObject:
    case intrinsic_proto::world::ObjectWorldUpdate::kReparentFrame:
      LOG(WARNING) << "Interpolating update with tag "
                   << world_update.update_case() << " is a no-op.";
      result = world_update;
      break;
    case intrinsic_proto::world::ObjectWorldUpdate::UPDATE_NOT_SET: {
      break;
    }
  }

  return result;
}

absl::StatusOr<ObjectWorldUpdates> InterpolateObjectWorldUpdates(
    const world::ObjectWorldClient& world,
    const ObjectWorldUpdates& world_updates, double t) {
  ObjectWorldUpdates result;
  if (!world_updates.entity_updates().empty()) {
    return absl::InvalidArgumentError(
        "Interpolation of ObjectWorldUpdates only accepts object updates, not "
        "entity updates. The entity_updates field must be empty.");
  }

  for (const auto& update : world_updates.updates()) {
    INTR_ASSIGN_OR_RETURN(auto interpolated_update,
                          InterpolateObjectWorldUpdate(world, update, t));
    if (interpolated_update.update_case() !=
        intrinsic_proto::world::ObjectWorldUpdate::UPDATE_NOT_SET) {
      *(result.add_updates()) = interpolated_update;
    }
  }
  return result;
}

absl::StatusOr<std::vector<intrinsic_proto::world::ObjectWorldUpdate>>
InterpolateObjectWorldUpdate(
    const world::ObjectWorldClient& world,
    const intrinsic_proto::world::ObjectWorldUpdate& world_update,
    const std::vector<double>& times) {
  std::vector<intrinsic_proto::world::ObjectWorldUpdate> result;
  result.reserve(times.size());

  if (times.empty()) {
    return result;
  }
  double prev_t = times[0];

  for (double t : times) {
    if (t < prev_t) {
      return absl::InvalidArgumentError(
          absl::StrCat("times should be monotonically increasing. Found time ",
                       t, " after ", prev_t));
    }
    INTR_ASSIGN_OR_RETURN(
        intrinsic_proto::world::ObjectWorldUpdate interim_update,
        InterpolateObjectWorldUpdate(world, world_update, t));
    result.push_back(std::move(interim_update));
    prev_t = t;
  }
  return result;
}

}  // namespace intrinsic::simulation
