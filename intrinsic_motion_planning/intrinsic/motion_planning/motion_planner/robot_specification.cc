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

#include "intrinsic/motion_planning/motion_planner/robot_specification.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/proto/cart_space.pb.h"
#include "intrinsic/icon/proto/joint_space.pb.h"
#include "intrinsic/kinematics/types/cartesian_limits.h"
#include "intrinsic/kinematics/types/joint_limits.h"
#include "intrinsic/kinematics/types/joint_limits_xd.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/motion_planning/path_planning/planners/linear_cartesian_motion_path_planner_configs.pb.h"
#include "intrinsic/motion_planning/path_planning/planners/pipeline_configs.pb.h"
#include "intrinsic/motion_planning/path_planning/planners/rrt_configs.pb.h"
#include "intrinsic/motion_planning/path_planning/planners/shortcutter_configs.pb.h"
#include "intrinsic/motion_planning/proto/motion_planner_service_proto_utils.h"
#include "intrinsic/motion_planning/proto/motion_target.pb.h"
#include "intrinsic/motion_planning/proto/v1/geometric_constraints.pb.h"
#include "intrinsic/motion_planning/proto/v1/motion_planner_service.pb.h"
#include "intrinsic/motion_planning/proto/v1/motion_specification.pb.h"
#include "intrinsic/motion_planning/proto/v1/robot_specification.pb.h"
#include "intrinsic/util/eigen.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/collision/collision_context.pb.h"
#include "intrinsic/world/component/attachment_component.h"
#include "intrinsic/world/entity.h"
#include "intrinsic/world/entity_id.h"
#include "intrinsic/world/objects/kinematic_object_internal.h"
#include "intrinsic/world/objects/object_world.h"
#include "intrinsic/world/objects/object_world_ids.h"
#include "intrinsic/world/objects/world_object_internal.h"
#include "intrinsic/world/world.h"

namespace intrinsic {
namespace {

// Returns true if the 3D vector limit `field` is non-empty and differs from
// the default unlimited bound (`[-inf, -inf, -inf]` when
// `expect_negative_infinity` is true, or `[+inf, +inf, +inf]` otherwise).
bool IsCartesianLimitSet(absl::Span<const double> field,
                         const bool expect_negative_infinity) {
  if (field.empty()) {
    return false;
  }
  if (field.size() != 3) {
    return true;
  }
  for (const double val : field) {
    if (!std::isinf(val) || (std::signbit(val) != expect_negative_infinity)) {
      return true;
    }
  }
  return false;
}

// Returns true if the scalar upper limit `val` is set (i.e., neither the Proto
// default `0.0` nor `+inf`).
bool IsCartesianLimitSet(const double val) {
  return val != 0.0 && (!std::isinf(val) || std::signbit(val));
}

// Validates and converts `limits_proto` into `CartesianLimits`. Only strictly
// positive, isotropic, and symmetric translational velocity limits (finite or
// `+inf`) are supported, because downstream trajectory parameterization (TOPP)
// bounds the Euclidean norm of the frame's translational velocity (a ball, not
// a per-axis box).
//
// Field semantics:
// - `max_translational_velocity` must have 3 elements that are all equal and
//   either strictly positive finite or `+inf`.
// - `min_translational_velocity` defaults to `-max_translational_velocity` if
//   empty; if populated, it must have 3 elements equal to
//   `-max_translational_velocity`.
// - All other vector limit fields treat empty or `[-inf, -inf, -inf]` (for
//   `min_*`) / `[+inf, +inf, +inf]` (for `max_*`) as unset, and scalar
//   rotational limit fields treat `0.0` (the Proto3 default) or `+inf` as
//   unset; any other values are rejected.
//
// Note: This does not reuse `icon::FromProto(CartesianLimits)` because the
// user-data proto only specifies `max_translational_velocity` (leaving
// `min_translational_velocity` to default to `-max_translational_velocity` and
// all other fields to `CartesianLimits::Unlimited()`), whereas
// `icon::FromProto` requires all 8 vector fields to be populated and does not
// reject unsupported limit types or enforce isotropic/symmetric velocities.
// Once full `CartesianLimits` are supported for additional frames, this can be
// replaced by `icon::FromProto`.
absl::StatusOr<CartesianLimits> ParseFrameCartesianLimitsProto(
    const intrinsic_proto::icon::CartesianLimits& limits_proto) {
  if (IsCartesianLimitSet(limits_proto.min_translational_position(),
                          /*expect_negative_infinity=*/true) ||
      IsCartesianLimitSet(limits_proto.max_translational_position(),
                          /*expect_negative_infinity=*/false) ||
      IsCartesianLimitSet(limits_proto.min_translational_acceleration(),
                          /*expect_negative_infinity=*/true) ||
      IsCartesianLimitSet(limits_proto.max_translational_acceleration(),
                          /*expect_negative_infinity=*/false) ||
      IsCartesianLimitSet(limits_proto.min_translational_jerk(),
                          /*expect_negative_infinity=*/true) ||
      IsCartesianLimitSet(limits_proto.max_translational_jerk(),
                          /*expect_negative_infinity=*/false) ||
      IsCartesianLimitSet(limits_proto.max_rotational_velocity()) ||
      IsCartesianLimitSet(limits_proto.max_rotational_acceleration()) ||
      IsCartesianLimitSet(limits_proto.max_rotational_jerk())) {
    return absl::InvalidArgumentError(
        "Unsupported Cartesian limit type in user data. Only translational "
        "velocity limits are supported.");
  }

  if (limits_proto.max_translational_velocity_size() != 3) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Must specify 3 values for max_translational_velocity in user data "
        "(got ",
        limits_proto.max_translational_velocity_size(), ")."));
  }
  if (!limits_proto.min_translational_velocity().empty() &&
      limits_proto.min_translational_velocity_size() != 3) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Must specify 3 values for min_translational_velocity in user data "
        "(got ",
        limits_proto.min_translational_velocity_size(), ")."));
  }
  if (!IsCartesianLimitSet(limits_proto.max_translational_velocity(),
                           /*expect_negative_infinity=*/false)) {
    if (IsCartesianLimitSet(limits_proto.min_translational_velocity(),
                            /*expect_negative_infinity=*/true)) {
      return absl::InvalidArgumentError(
          "Asymmetric min_translational_velocity in user data. Symmetric "
          "limits (min == -max) are required.");
    }
    return CartesianLimits::Unlimited();
  }

  const eigenmath::Vector3d v_max(limits_proto.max_translational_velocity(0),
                                  limits_proto.max_translational_velocity(1),
                                  limits_proto.max_translational_velocity(2));
  if (!(v_max.array() > 0.0).all()) {
    return absl::InvalidArgumentError(
        "Must have strictly positive max_translational_velocity in user data.");
  }
  if (!v_max.allFinite()) {
    return absl::InvalidArgumentError(
        "max_translational_velocity in user data must be either all finite or "
        "all +inf.");
  }
  if (!v_max.isApprox(eigenmath::Vector3d::Constant(v_max.x()))) {
    return absl::InvalidArgumentError(
        "Anisotropic max_translational_velocity in user data. Isotropic limits "
        "(vx == vy == vz) are required.");
  }

  eigenmath::Vector3d v_min = -v_max;
  if (!limits_proto.min_translational_velocity().empty()) {
    v_min = eigenmath::Vector3d(limits_proto.min_translational_velocity(0),
                                limits_proto.min_translational_velocity(1),
                                limits_proto.min_translational_velocity(2));
    if (!v_min.isApprox(-v_max)) {
      return absl::InvalidArgumentError(
          "Asymmetric min_translational_velocity in user data. Symmetric "
          "limits (min == -max) are required.");
    }
  }

  CartesianLimits limits = CartesianLimits::Unlimited();
  limits.max_translational_velocity = v_max;
  limits.min_translational_velocity = v_min;
  return limits;
}

// Resolves `frame_name` to a frame or link entity on `robot`: Checks named
// frames first, then falls back to matching robot links by local name if no
// named frame is found. Returns `absl::InvalidArgumentError` if multiple links
// on `robot` share `frame_name`, or `absl::NotFoundError` if not found on
// `robot`.
absl::StatusOr<AttachmentEntityId> FindFrameOrLinkEntityId(
    const World& entity_world, const object_world::KinematicObject& robot,
    absl::string_view frame_name) {
  // Check if `frame_name` refers to a named frame attached to the robot.
  const absl::StatusOr<const object_world::Frame*> frame_or =
      robot.GetFrame(FrameName(frame_name));
  if (frame_or.ok()) {
    return frame_or.value()->GetEntityId();
  }
  if (!absl::IsNotFound(frame_or.status())) {
    return frame_or.status();
  }
  // Robot links are represented as entities in `robot.GetEntityIds()` rather
  // than `Frame` objects in `ObjectWorld`, so fall back to matching link
  // entities by local name.
  AttachmentEntityId matched_link_id(kInvalidEntityId);
  for (const AttachmentEntityId candidate_id : robot.GetEntityIds()) {
    INTR_ASSIGN_OR_RETURN(const WorldEntity* entity,
                          entity_world.GetEntityById(candidate_id));
    if (entity->IsEntityValid<LinkEntityId>() &&
        entity->GetLocalName() == frame_name) {
      if (matched_link_id != AttachmentEntityId(kInvalidEntityId)) {
        return absl::InvalidArgumentError(
            absl::StrCat("Multiple links named '", frame_name,
                         "' found on robot '", robot.GetName().value(), "'."));
      }
      matched_link_id = candidate_id;
    }
  }
  if (matched_link_id != AttachmentEntityId(kInvalidEntityId)) {
    return matched_link_id;
  }
  return absl::NotFoundError(
      absl::StrCat("Frame or link '", frame_name, "' specified in user data '",
                   kFrameCartesianLimitsUserDataKey, "' not found on robot '",
                   robot.GetName().value(), "'."));
}

// Traverses up the attachment hierarchy within `robot` from `frame_entity_id`
// to find the nearest parent link on `robot` (which may be `frame_entity_id`
// itself if it refers directly to a link). Returns `absl::InvalidArgumentError`
// if a joint entity is encountered before reaching a parent link (since the
// transform from the link to the frame must be static and independent of joint
// positions), or `absl::NotFoundError` if no parent link on `robot` is found.
absl::StatusOr<std::pair<AttachmentEntityId, std::string>>
FindParentLinkOnKinematicChain(const World& entity_world,
                               const object_world::KinematicObject& robot,
                               AttachmentEntityId frame_entity_id,
                               absl::string_view frame_name) {
  const auto not_found_error = [frame_name, &robot]() {
    return absl::NotFoundError(absl::StrCat(
        "Could not find a parent link on kinematic chain for frame '",
        frame_name, "' on robot '", robot.GetName().value(), "'."));
  };
  AttachmentEntityId curr_id = frame_entity_id;
  while (curr_id != AttachmentEntityId(kInvalidEntityId) &&
         robot.GetEntityIds().contains(curr_id)) {
    INTR_ASSIGN_OR_RETURN(const WorldEntity* curr_entity,
                          entity_world.GetEntityById(curr_id));
    if (curr_entity->IsEntityValid<JointEntityId>()) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Frame '", frame_name, "' on robot '", robot.GetName().value(),
          "' is attached via joint '", curr_entity->GetLocalName(),
          "' before reaching a parent link; frame must be rigidly attached to "
          "a link."));
    }
    // Note: This matches any link on `robot` (including side-branch links);
    // membership in the base-to-tip kinematic chain (`GetSkeletonForRobot()`)
    // is validated later during trajectory parameterization in TOPP.
    if (curr_entity->IsEntityValid<LinkEntityId>()) {
      return std::make_pair(curr_id, curr_entity->GetLocalName());
    }
    INTR_ASSIGN_OR_RETURN(const AttachmentComponent* attachment,
                          curr_entity->GetComponent<AttachmentComponent>(),
                          not_found_error());
    curr_id = attachment->GetParentId();
  }
  return not_found_error();
}

}  // namespace

absl::StatusOr<std::vector<FrameCartesianPathLimits>>
ResolveFrameCartesianPathLimits(const object_world::ObjectWorld& object_world,
                                const object_world::KinematicObject& robot) {
  // Look up the optional frame Cartesian limits proto in the robot's user data.
  absl::StatusOr<const object_world::WorldObject::UserDataProtos*>
      user_data_protos_or = robot.GetUserDataProtos();
  if (absl::IsNotFound(user_data_protos_or.status())) {
    return std::vector<FrameCartesianPathLimits>{};
  }
  INTR_RETURN_IF_ERROR(user_data_protos_or.status());

  const auto& user_data_protos = *user_data_protos_or.value();
  auto it = user_data_protos.find(kFrameCartesianLimitsUserDataKey);
  if (it == user_data_protos.end()) {
    return std::vector<FrameCartesianPathLimits>{};
  }

  intrinsic_proto::icon::FrameCartesianLimits frame_limits_proto;
  if (!it->second.UnpackTo(&frame_limits_proto)) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Failed to unpack FrameCartesianLimits from user_data key '",
        kFrameCartesianLimitsUserDataKey, "' on robot '",
        robot.GetName().value(), "'."));
  }
  if (frame_limits_proto.limits_by_frame().empty()) {
    return std::vector<FrameCartesianPathLimits>{};
  }

  const World& entity_world = object_world.GetEntityWorld();
  std::vector<FrameCartesianPathLimits> resolved_limits;
  resolved_limits.reserve(frame_limits_proto.limits_by_frame().size());

  for (const auto& [frame_name, limits_proto] :
       frame_limits_proto.limits_by_frame()) {
    if (frame_name.size() > FrameCartesianPathLimits::kMaxFrameNameLength) {
      return absl::InvalidArgumentError(
          absl::StrCat("Frame or link name '", frame_name, "' on robot '",
                       robot.GetName().value(), "' exceeds maximum length of ",
                       FrameCartesianPathLimits::kMaxFrameNameLength, "."));
    }

    INTR_ASSIGN_OR_RETURN(
        const AttachmentEntityId frame_entity_id,
        FindFrameOrLinkEntityId(entity_world, robot, frame_name));

    INTR_ASSIGN_OR_RETURN(
        (const auto [parent_link_id, parent_link_name]),
        FindParentLinkOnKinematicChain(entity_world, robot, frame_entity_id,
                                       frame_name));

    if (parent_link_name.size() >
        FrameCartesianPathLimits::kMaxFrameNameLength) {
      return absl::InvalidArgumentError(
          absl::StrCat("Parent link name '", parent_link_name, "' for frame '",
                       frame_name, "' on robot '", robot.GetName().value(),
                       "' exceeds maximum length of ",
                       FrameCartesianPathLimits::kMaxFrameNameLength, "."));
    }

    // Validate and convert the proto limits for this frame.
    const absl::StatusOr<CartesianLimits> parsed_limits =
        ParseFrameCartesianLimitsProto(limits_proto);
    if (!parsed_limits.ok()) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Invalid Cartesian limits for frame '", frame_name, "' on robot '",
          robot.GetName().value(), "': ", parsed_limits.status().message()));
    }
    // Skip frames whose resolved limits are completely unlimited so downstream
    // trajectory parameterization (TOPP) does not evaluate frame Jacobians for
    // constraints that can never be active.
    if (*parsed_limits == CartesianLimits::Unlimited()) {
      continue;
    }

    // Compute the static pose of the target frame in the parent link frame
    // (`base_t_frame = base_t_parent_link * parent_link_t_frame`).
    const Pose3d parent_link_t_frame =
        entity_world.GetTransform(parent_link_id, frame_entity_id);
    resolved_limits.push_back(FrameCartesianPathLimits{
        .frame_name = absl::string_view(frame_name),
        .parent_link_name = absl::string_view(parent_link_name),
        .parent_link_t_frame = parent_link_t_frame,
        .limits = *parsed_limits,
    });
  }

  // Sort by frame name to ensure a deterministic output order, as Protobuf map
  // iteration order is non-deterministic.
  std::sort(
      resolved_limits.begin(), resolved_limits.end(),
      [](const FrameCartesianPathLimits& a, const FrameCartesianPathLimits& b) {
        return absl::string_view(a.frame_name) <
               absl::string_view(b.frame_name);
      });

  return resolved_limits;
}

absl::StatusOr<RobotSpecification> RobotSpecification::Create(
    const object_world::ObjectWorld& object_world,
    const intrinsic_proto::motion_planning::v1::RobotSpecification&
        robot_specification) {
  // Parse robot from Proto.
  INTR_ASSIGN_OR_RETURN(
      const object_world::KinematicObject* robot,
      GetRobot(robot_specification.robot_reference(), &object_world));

  // Parse default Cartesian limits.
  INTR_ASSIGN_OR_RETURN(CartesianLimits default_cart_limits,
                        robot->GetCartesianLimits());

  // Parse frame Cartesian limits.
  INTR_ASSIGN_OR_RETURN(
      std::vector<FrameCartesianPathLimits> frame_cartesian_limits,
      ResolveFrameCartesianPathLimits(object_world, *robot));

  // Parse planning and system robot joint limits.
  INTR_ASSIGN_OR_RETURN(const JointLimitsXd application_limits_xd,
                        robot->GetJointApplicationLimits());
  // Convert JointLimitsXd to JointLimits.
  INTR_ASSIGN_OR_RETURN(const JointLimits application_limits,
                        ToJointLimits(application_limits_xd));

  // Start configuration. Default from world if not provided by user.
  INTR_ASSIGN_OR_RETURN(eigenmath::VectorXd start_configuration,
                        robot->GetJointPositions());
  if (robot_specification.has_start_configuration()) {
    start_configuration = RepeatedDoubleToVectorXd(
        robot_specification.start_configuration().joints());
  }
  return RobotSpecification(robot, default_cart_limits, application_limits,
                            start_configuration,
                            std::move(frame_cartesian_limits));
}
}  // namespace intrinsic
