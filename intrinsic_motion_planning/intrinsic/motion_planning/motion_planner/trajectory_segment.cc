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

#include "intrinsic/motion_planning/motion_planner/trajectory_segment.h"

#include <optional>
#include <queue>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "intrinsic/icon/utils/realtime_status_macro.h"
#include "intrinsic/kinematics/types/cartesian_limits.h"
#include "intrinsic/kinematics/types/check_joint_limits.h"
#include "intrinsic/kinematics/types/joint_limits.h"
#include "intrinsic/kinematics/types/to_fixed_string.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/motion_planning/motion_planner/robot_specification.h"
#include "intrinsic/motion_planning/proto/motion_specification_proto_utils.h"
#include "intrinsic/motion_planning/proto/v1/geometric_constraints.pb.h"
#include "intrinsic/motion_planning/proto/v1/motion_specification.pb.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/entity_id.h"
#include "intrinsic/world/objects/kinematic_object_internal.h"
#include "intrinsic/world/objects/object_world.h"
#include "intrinsic/world/objects/object_world_proto_utils.h"
#include "intrinsic/world/objects/transform_node_internal.h"
#include "intrinsic/world/proto/object_world_refs.pb.h"
#include "intrinsic/world/world.h"

namespace intrinsic {

using intrinsic_proto::motion_planning::v1::MotionSegment;

namespace {

using intrinsic_proto::motion_planning::v1::GeometricConstraint;

// Function returns either the tip the tool is connected to or the tool itself
// if it is a part of the robot. Returns an error if the tool_id is not
// connected to the robot.
absl::StatusOr<AttachmentEntityId> GetConnectedRobotElement(
    const World& world, RobotCollectionsEntityId robot_id,
    AttachmentEntityId tool_id) {
  INTR_ASSIGN_OR_RETURN(auto tip_ids, world.GetFinalEntitiesOfRobot(robot_id));
  for (const auto tip_id : tip_ids) {
    INTR_ASSIGN_OR_RETURN(const auto ancestor_id,
                          world.FindCommonAncestor(tip_id, tool_id));
    if (ancestor_id == tip_id) {
      return tip_id;
    }
  }

  // The tool id is connected to the robot, but not through a tip element, this
  // is the case if tool_id is a part of the robot.
  INTR_ASSIGN_OR_RETURN(const auto base_id, world.GetBaseLink(robot_id));
  INTR_ASSIGN_OR_RETURN(const auto ancestor_id,
                        world.FindCommonAncestor(base_id, tool_id));
  if (ancestor_id == base_id) {
    return tool_id;
  }

  return ::intrinsic::InvalidArgumentErrorBuilder()
         << "Tool id " << tool_id.value() << tool_id.value()
         << " does not seem to be connected to the robot " << robot_id.value();
}

absl::StatusOr<AttachmentEntityId> GetMovingFrame(
    const object_world::ObjectWorld& object_world,
    const object_world::KinematicObject& robot,
    const intrinsic_proto::world::TransformNodeReference& reference,
    const intrinsic_proto::world::TransformNodeReference& moving) {
  INTR_ASSIGN_OR_RETURN(
      const object_world::TransformNode* moving_node,
      object_world::GetTransformNodeByReference(object_world, moving));
  INTR_ASSIGN_OR_RETURN(auto constraint_moving_id,
                        moving_node->GetTransformOriginEntityId());
  INTR_ASSIGN_OR_RETURN(
      const object_world::TransformNode* reference_node,
      object_world::GetTransformNodeByReference(object_world, reference));
  INTR_ASSIGN_OR_RETURN(auto constraint_reference_id,
                        reference_node->GetTransformOriginEntityId());
  INTR_ASSIGN_OR_RETURN(auto ancestor_ref_moving_id,
                        object_world.GetEntityWorld().FindCommonAncestor(
                            constraint_reference_id, constraint_moving_id));
  INTR_ASSIGN_OR_RETURN(auto base_id, robot.GetRootEntityId());
  INTR_ASSIGN_OR_RETURN(auto ancestor_base_moving_id,
                        object_world.GetEntityWorld().FindCommonAncestor(
                            base_id, constraint_moving_id));
  // Swap ids to make sure the order is right
  if (ancestor_ref_moving_id == constraint_moving_id) {
    // moving is an ancestor of ref or the same.
    return constraint_reference_id;
  }
  if (ancestor_ref_moving_id != constraint_reference_id &&
      ancestor_base_moving_id != base_id) {
    // If ref is not an ancestor of moving and moving is not an ancestor of ref
    // then we need to switch based on who is connected to the robot base.
    return constraint_reference_id;
  }
  return constraint_moving_id;
}

absl::StatusOr<std::optional<AttachmentEntityId>> GetToolMovingAttachmentId(
    const object_world::ObjectWorld& object_world,
    const object_world::KinematicObject& robot,
    const GeometricConstraint& geometric_constraint) {
  std::queue<GeometricConstraint> queue;
  queue.push(geometric_constraint);
  std::optional<AttachmentEntityId> moving_frame;
  while (!queue.empty()) {
    const auto constraint = queue.front();
    queue.pop();

    switch (constraint.constraint_case()) {
      case GeometricConstraint::ConstraintCase::kConstraintIntersection: {
        // For an intersection we can add all constraints on the queue.
        for (const auto& element :
             constraint.constraint_intersection().constraints()) {
          queue.push(element);
        }
        break;
      }
      case GeometricConstraint::ConstraintCase::kJointPositionLimits: {
        // Nothing to do.
        break;
      }
      case GeometricConstraint::ConstraintCase::kJointPosition: {
        return std::nullopt;
      }
      case GeometricConstraint::ConstraintCase::kCartesianPose: {
        INTR_ASSIGN_OR_RETURN(
            auto constraint_moving_id,
            GetMovingFrame(object_world, robot,
                           constraint.cartesian_pose().target_frame(),
                           constraint.cartesian_pose().moving_frame()));
        // Check if compatible
        if (moving_frame.has_value() && *moving_frame != constraint_moving_id) {
          return std::nullopt;
        }
        moving_frame = constraint_moving_id;
        break;
      }
      case GeometricConstraint::ConstraintCase::kPositionEquality: {
        INTR_ASSIGN_OR_RETURN(
            auto constraint_moving_id,
            GetMovingFrame(object_world, robot,
                           constraint.position_equality().target_frame(),
                           constraint.position_equality().moving_frame()));
        // Check if compatible
        if (moving_frame.has_value() && *moving_frame != constraint_moving_id) {
          return std::nullopt;
        }
        moving_frame = constraint_moving_id;
        break;
      }
      case GeometricConstraint::ConstraintCase::kRotationCone: {
        INTR_ASSIGN_OR_RETURN(
            auto constraint_moving_id,
            GetMovingFrame(object_world, robot,
                           constraint.rotation_cone().target_frame(),
                           constraint.rotation_cone().moving_frame()));
        // Check if compatible
        if (moving_frame.has_value() && *moving_frame != constraint_moving_id) {
          return std::nullopt;
        }
        moving_frame = constraint_moving_id;
        break;
      }
      case GeometricConstraint::ConstraintCase::kRotationEquality: {
        INTR_ASSIGN_OR_RETURN(
            auto constraint_moving_id,
            GetMovingFrame(object_world, robot,
                           constraint.rotation_equality().target_frame(),
                           constraint.rotation_equality().moving_frame()));
        // Check if compatible
        if (moving_frame.has_value() && *moving_frame != constraint_moving_id) {
          return std::nullopt;
        }
        moving_frame = constraint_moving_id;
        break;
      }
      case GeometricConstraint::ConstraintCase::kPositionBoundingBox: {
        INTR_ASSIGN_OR_RETURN(
            auto constraint_moving_id,
            GetMovingFrame(object_world, robot,
                           constraint.position_bounding_box().target_frame(),
                           constraint.position_bounding_box().moving_frame()));
        // Check if compatible
        if (moving_frame.has_value() && *moving_frame != constraint_moving_id) {
          return std::nullopt;
        }
        moving_frame = constraint_moving_id;
        break;
      }
      case GeometricConstraint::ConstraintCase::kJointPositionSumLimit: {
        // Nothing to do.
        break;
      }
      case GeometricConstraint::ConstraintCase::kPointAt: {
        INTR_ASSIGN_OR_RETURN(
            auto constraint_moving_id,
            GetMovingFrame(object_world, robot,
                           constraint.point_at().target_frame(),
                           constraint.point_at().moving_frame()));
        // Check if compatible
        if (moving_frame.has_value() && *moving_frame != constraint_moving_id) {
          return std::nullopt;
        }
        moving_frame = constraint_moving_id;
        break;
      }
      case GeometricConstraint::ConstraintCase::kRelativePositionEquality: {
        INTR_ASSIGN_OR_RETURN(
            auto constraint_moving_id,
            GetMovingFrame(
                object_world, robot,
                constraint.relative_position_equality().moving_frame(),
                constraint.relative_position_equality().moving_frame()));
        // Check if compatible
        if (moving_frame.has_value() && *moving_frame != constraint_moving_id) {
          return std::nullopt;
        }
        moving_frame = constraint_moving_id;
        break;
      }
      case GeometricConstraint::ConstraintCase::kRelativeRotationEquality: {
        INTR_ASSIGN_OR_RETURN(
            auto constraint_moving_id,
            GetMovingFrame(
                object_world, robot,
                constraint.relative_rotation_equality().moving_frame(),
                constraint.relative_rotation_equality().moving_frame()));
        // Check if compatible
        if (moving_frame.has_value() && *moving_frame != constraint_moving_id) {
          return std::nullopt;
        }
        moving_frame = constraint_moving_id;
        break;
      }
      case GeometricConstraint::ConstraintCase::kRelativeCartesianPose: {
        INTR_ASSIGN_OR_RETURN(
            auto constraint_moving_id,
            GetMovingFrame(
                object_world, robot,
                constraint.relative_cartesian_pose().moving_frame(),
                constraint.relative_cartesian_pose().moving_frame()));
        // Check if compatible
        if (moving_frame.has_value() && *moving_frame != constraint_moving_id) {
          return std::nullopt;
        }
        moving_frame = constraint_moving_id;
        break;
      }
      case GeometricConstraint::CONSTRAINT_NOT_SET: {
        return absl::InvalidArgumentError(
            "Constraint type unknown or unspecified.");
      }
    }
  }
  return moving_frame;
}

absl::StatusOr<TrajectorySegment::PlanningFrames>
CreatePlanningFramesForMotionSegment(
    const object_world::ObjectWorld& object_world,
    const object_world::KinematicObject& robot,
    const intrinsic_proto::motion_planning::v1::MotionSegment& motion_segment,
    const TrajectorySegment::Type& type) {
  // By default the planning frames are robot base to flange.
  TrajectorySegment::PlanningFrames planning_frames;
  INTR_ASSIGN_OR_RETURN(planning_frames.reference_id, robot.GetRootEntityId());
  INTR_ASSIGN_OR_RETURN(const auto robot_flange_frame,
                        robot.GetSingleIsoFlangeFrame());
  INTR_ASSIGN_OR_RETURN(planning_frames.moving_id,
                        robot_flange_frame->GetTransformOriginEntityId());

  // Check if linear path constraint specifies the frames.
  if (type == TrajectorySegment::Type::kBlendedCartesian) {
    // Take it from the pose moving frame.
    if (motion_segment.has_target()) {
      INTR_ASSIGN_OR_RETURN(auto moving_id,
                            GetToolMovingAttachmentId(object_world, robot,
                                                      motion_segment.target()));
      // Set moving id to the moving id from the constraint. Default set to
      // use the robot tip.
      if (moving_id.has_value()) {
        planning_frames.moving_id = moving_id.value();
      }
    }
    // Check that the set moving_id is connected to the robot
    INTR_ASSIGN_OR_RETURN(const AttachmentEntityId base_id,
                          robot.GetRootEntityId());
    INTR_ASSIGN_OR_RETURN(const AttachmentEntityId ancestor_id,
                          object_world.GetEntityWorld().FindCommonAncestor(
                              base_id, planning_frames.moving_id));

    if (ancestor_id != base_id) {
      return absl::InvalidArgumentError(
          "Movement is defined for a frame that does not seem to be "
          "connected to the frame. Please choose a frame that is connected "
          "to the robot as the `move` frame.");
    }
  }

  return planning_frames;
}

absl::StatusOr<Pose3d> GetTipTMovingFrame(
    const object_world::ObjectWorld& object_world,
    const RobotSpecification& robot_specification,
    const AttachmentEntityId& moving_id) {
  INTR_ASSIGN_OR_RETURN(
      const AttachmentEntityId robot_tip_id,
      GetConnectedRobotElement(object_world.GetEntityWorld(),
                               robot_specification.robot->GetRobotEntityId(),
                               moving_id));
  return object_world.GetEntityWorld().GetTransform(robot_tip_id, moving_id);
}

}  // namespace

absl::StatusOr<TrajectorySegment> TrajectorySegment::Create(
    const object_world::ObjectWorld& object_world,
    const RobotSpecification& robot_specification,
    const MotionSegment& motion_segment,
    const std::optional<
        intrinsic_proto::motion_planning::v1::BlendingParameters>&
        blending_parameters) {
  INTR_ASSIGN_OR_RETURN(
      const TrajectorySegment::Type trajectory_type,
      ValidatePathConstraintsAndReturnTrajectoryType(motion_segment));

  INTR_ASSIGN_OR_RETURN(const TrajectorySegment::PlanningFrames planning_frames,
                        CreatePlanningFramesForMotionSegment(
                            object_world, *robot_specification.robot,
                            motion_segment, trajectory_type));
  INTR_ASSIGN_OR_RETURN(const Pose3d tip_t_moving_frame,
                        GetTipTMovingFrame(object_world, robot_specification,
                                           planning_frames.moving_id));
  CartesianLimits cart_limits = ParseCartesianLimitsFromProto(
      motion_segment, /*default_limits=*/robot_specification.cart_limits);

  INTR_ASSIGN_OR_RETURN(
      const JointLimits joint_limits,
      ParseJointLimitsFromProto(motion_segment,
                                robot_specification.application_limits));
  INTRINSIC_RT_ASSIGN_OR_RETURN(
      LimitCheckResult limit_check_result,
      IsWithinLimits(joint_limits, robot_specification.application_limits));
  if (!limit_check_result) {
    return absl::InvalidArgumentError(
        absl::StrFormat("Provided joint limits violate the application limits. "
                        "Provided limits %v, application limits %v",
                        ToFixedString(joint_limits),
                        ToFixedString(robot_specification.application_limits)));
  }

  return TrajectorySegment{
      {motion_segment},
      /*joint_samples=*/{}, cart_limits,
      joint_limits,         blending_parameters,
      trajectory_type,      planning_frames,
      tip_t_moving_frame,   robot_specification.frame_cartesian_limits};
}

absl::StatusOr<TrajectorySegment::Type>
TrajectorySegment::ValidatePathConstraintsAndReturnTrajectoryType(
    const intrinsic_proto::motion_planning::v1::MotionSegment& motion_segment) {
  TrajectorySegment::Type new_segment_type =
      TrajectorySegment::Type::kBlendedJoint;
  if (motion_segment.motion_type() == MotionSegment::LINEAR) {
    new_segment_type = TrajectorySegment::Type::kBlendedCartesian;
  }
  return new_segment_type;
}

}  // namespace intrinsic
