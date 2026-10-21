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

#include "intrinsic/motion_planning/skills/motion_planning_error_util.h"

#include <cstdint>
#include <string>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/cord.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/proto/eigen_conversion.h"
#include "intrinsic/icon/proto/joint_space.pb.h"
#include "intrinsic/math/proto/point.pb.h"
#include "intrinsic/math/proto/pose.pb.h"
#include "intrinsic/math/proto/quaternion.pb.h"
#include "intrinsic/motion_planning/proto/v1/geometric_constraints.pb.h"
#include "intrinsic/motion_planning/proto/v1/motion_planning_error.pb.h"
#include "intrinsic/util/eigen.h"
#include "intrinsic/util/status/get_extended_status.h"
#include "intrinsic/util/status/status_builder.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/proto/object_world_refs.pb.h"

namespace intrinsic::skills {

namespace {

// Helper function to create a string for a given frame.
// Based on availability it will create a string in the format
// object_name.frame_name or frame id.
std::string GetFrameString(
    intrinsic_proto::world::TransformNodeReference frame) {
  std::string frame_str;
  if (frame.has_by_name()) {
    return absl::StrFormat("%s.%s", frame.by_name().frame().object_name(),
                           frame.by_name().frame().frame_name());
  }
  if (frame.has_id()) {
    return absl::StrFormat("%s", frame.id());
  }
  LOG(WARNING) << "Frame does not have a name or id";
  return "";
}

// Helper function to get the target frame and moving frame names for
// position equality, pose equality, rotation equality, position bounding box
// and rotation cone constraint.
// (b/327651535)We currently do not handle constraint intersections.
std::string GetTargetAndMovingFrameFromConstraint(
    const ::intrinsic_proto::motion_planning::v1::GeometricConstraint&
        error_constraint) {
  std::string target_constraint_str, moving_constraint_str, constraint_str;
  switch (error_constraint.constraint_case()) {
    case intrinsic_proto::motion_planning::v1::GeometricConstraint::
        ConstraintCase::kCartesianPose: {
      target_constraint_str =
          GetFrameString(error_constraint.cartesian_pose().target_frame());
      moving_constraint_str =
          GetFrameString(error_constraint.cartesian_pose().moving_frame());
    } break;
    case intrinsic_proto::motion_planning::v1::GeometricConstraint::
        ConstraintCase::kJointPositionLimits: {
      break;
    }
    case intrinsic_proto::motion_planning::v1::GeometricConstraint::
        ConstraintCase::kPositionEquality: {
      target_constraint_str =
          GetFrameString(error_constraint.position_equality().target_frame());
      moving_constraint_str =
          GetFrameString(error_constraint.position_equality().moving_frame());
    } break;
    case intrinsic_proto::motion_planning::v1::GeometricConstraint::
        ConstraintCase::kJointPosition:
      break;
    case intrinsic_proto::motion_planning::v1::GeometricConstraint::
        ConstraintCase::kRotationEquality: {
      target_constraint_str =
          GetFrameString(error_constraint.rotation_equality().target_frame());
      moving_constraint_str =
          GetFrameString(error_constraint.rotation_equality().moving_frame());
    } break;
    case intrinsic_proto::motion_planning::v1::GeometricConstraint::
        ConstraintCase::kRelativePositionEquality: {
      target_constraint_str =
          GetFrameString(error_constraint.rotation_equality().target_frame());
      moving_constraint_str =
          GetFrameString(error_constraint.rotation_equality().moving_frame());
    } break;
    case intrinsic_proto::motion_planning::v1::GeometricConstraint::
        ConstraintCase::kRelativeRotationEquality: {
      moving_constraint_str =
          GetFrameString(error_constraint.rotation_equality().moving_frame());
    } break;
    case intrinsic_proto::motion_planning::v1::GeometricConstraint::
        ConstraintCase::kRelativeCartesianPose: {
      target_constraint_str =
          GetFrameString(error_constraint.rotation_equality().target_frame());
      moving_constraint_str =
          GetFrameString(error_constraint.rotation_equality().moving_frame());
    } break;
    case intrinsic_proto::motion_planning::v1::GeometricConstraint::
        ConstraintCase::kRotationCone: {
      target_constraint_str =
          GetFrameString(error_constraint.rotation_cone().target_frame());
      moving_constraint_str =
          GetFrameString(error_constraint.rotation_cone().moving_frame());
    } break;
    case intrinsic_proto::motion_planning::v1::GeometricConstraint::
        ConstraintCase::kPositionBoundingBox:
      target_constraint_str =
          GetFrameString(error_constraint.rotation_cone().target_frame());
      moving_constraint_str =
          GetFrameString(error_constraint.rotation_cone().moving_frame());
      break;
    case intrinsic_proto::motion_planning::v1::GeometricConstraint::
        ConstraintCase::kJointPositionSumLimit: {
      break;
    }
    case intrinsic_proto::motion_planning::v1::GeometricConstraint::
        ConstraintCase::kConstraintIntersection:
      break;
    case intrinsic_proto::motion_planning::v1::GeometricConstraint::
        ConstraintCase::kPointAt: {
      target_constraint_str =
          GetFrameString(error_constraint.point_at().target_frame());
      moving_constraint_str =
          GetFrameString(error_constraint.point_at().moving_frame());
    } break;
    case intrinsic_proto::motion_planning::v1::GeometricConstraint::
        ConstraintCase::CONSTRAINT_NOT_SET:
      break;
  }
  if (!target_constraint_str.empty()) {
    constraint_str = absl::StrCat("\n\tTarget frame: ", target_constraint_str);
  }
  if (!moving_constraint_str.empty()) {
    absl::StrAppend(&constraint_str,
                    "\n\tMoving frame: ", moving_constraint_str);
  }
  return constraint_str;
}

std::string GetPositionString(const ::intrinsic_proto::Point& position) {
  std::string position_str = absl::StrFormat(
      "x: %.2f, y: %.2f, z: %.2f", position.x(), position.y(), position.z());
  return position_str;
}

std::string GetOrientationString(
    const ::intrinsic_proto::Quaternion& orientation) {
  float q_w, q_x, q_y, q_z = 0.0;
  std::string orientation_str = "Quaternion (xyzw): ";
  if (orientation.w()) {
    q_w = orientation.w();
  }
  if (orientation.x()) {
    q_x = orientation.x();
  }
  if (orientation.y()) {
    q_y = orientation.y();
  }
  if (orientation.z()) {
    q_z = orientation.z();
  }
  absl::StrAppend(&orientation_str,
                  absl::StrFormat("x: %.2f, y: %.2f, z: %.2f, w: %.2f", q_x,
                                  q_y, q_z, q_w));
  return orientation_str;
}

std::string GetSegmentIDString(
    intrinsic_proto::motion_planning::v1::MotionPlanningError
        motion_planning_error) {
  // Handle segment ids for the error message.
  std::string prepend_segment_id, segment_id;
  if (motion_planning_error.segment_id().empty()) {
    LOG(INFO) << "Segment id is empty";
  } else {
    if (motion_planning_error.segment_id().size() > 1) {
      segment_id =
          absl::StrFormat("%d to %d", motion_planning_error.segment_id().at(0),
                          motion_planning_error.segment_id().at(1));
    }
    if (motion_planning_error.segment_id().size() == 1) {
      segment_id =
          absl::StrFormat("%d", motion_planning_error.segment_id().at(0));
    }
    prepend_segment_id = absl::StrFormat(
        "Error reported when planning for motion segment %s. \n", segment_id);
  }
  return prepend_segment_id;
}

// Creates a user friendly error message from a Motion Planning Collision Error.
absl::StatusOr<std::string> GetUserFriendlyCollisionErrorMessage(
    const intrinsic_proto::motion_planning::v1::CollisionError& collision_error,
    bool use_rad) {
  std::string error_message;

  if (collision_error.error_context() ==
      intrinsic_proto::motion_planning::v1::ErrorContext::
          INITIAL_STATE_COLLISION) {
    error_message =
        "\nInvalid initial joint configuration. Collision reported:";
  } else if (collision_error.error_context() ==
             intrinsic_proto::motion_planning::v1::ErrorContext::
                 GOAL_STATE_COLLISION) {
    error_message = "\nInvalid goal joint configuration. Collision reported:";
  }
  if (collision_error.error_context() ==
      intrinsic_proto::motion_planning::v1::ErrorContext::IK_COLLISION) {
    absl::StrAppend(&error_message,
                    "\nIK could not find a collision free configuration.\n"
                    "Collision reported for the following solutions: \n");
  }
  if (collision_error.error_context() ==
      intrinsic_proto::motion_planning::v1::ErrorContext::
          TRAJECTORY_PLANNING_INITIAL_STATE_COLLISION) {
    error_message =
        "Could not generate a Cartesian linear path because the initial state "
        "of the resulting trajectory violates "
        "path constraints. Detailed report:\n";
  }
  if (collision_error.error_context() ==
      intrinsic_proto::motion_planning::v1::ErrorContext::
          TRAJECTORY_PLANNING_GOAL_STATE_COLLISION) {
    error_message =
        "Could not generate a Cartesian linear path because the end state of "
        "the resulting trajectory violates "
        "path constraints. Detailed report:\n";
  }
  if (collision_error.error_context() ==
      intrinsic_proto::motion_planning::v1::ErrorContext::
          TRAJECTORY_PLANNING_STATE_COLLISION) {
    absl::StrAppend(&error_message,
                    "Could not generate a Cartesian linear path because a "
                    "state along the planned"
                    "trajectory violates "
                    "path constraints. Detailed report:\n");
  }
  if (collision_error.error_context() ==
      intrinsic_proto::motion_planning::v1::ErrorContext::
          REFINEMENT_VALIDATION) {
    error_message =
        "Trajectory failed validation: a collision was detected in the "
        "trajectory. Please try increasing collision margins and try again. "
        "Detailed collision report:\n";
  }

  std::string unit = use_rad ? "rad" : "deg";
  for (const auto& collision_debug : collision_error.collision_debug()) {
    INTR_ASSIGN_OR_RETURN(
        const eigenmath::VectorNd joint_positions_in_rad,
        intrinsic::icon::FromProto(collision_debug.joint_positions()));
    std::string joint_positions =
        use_rad ? toString(joint_positions_in_rad)
                : toString(ConvertRadiansToDegrees(joint_positions_in_rad));

    // TODO(b/508634962): In Flowstate, the newlines in this string are being
    // ignored, making the error message hard to read.
    const std::string collision_debug_str = absl::StrFormat(
        "\n Config: [%s] in %s,\n Left entity: %s \n Right entities: %s \n",
        joint_positions, unit, collision_debug.left_entity(),
        collision_debug.right_entities());
    absl::StrAppend(&error_message, collision_debug_str);
  }
  return error_message;
}

// Creates a user friendly error message from a Motion Planning IK Error.
absl::StatusOr<std::string> GetUserFriendlyIKErrorMessage(
    const intrinsic_proto::motion_planning::v1::IKError& ik_error) {
  std::string error_message;
  if (ik_error.error_context() == intrinsic_proto::motion_planning::v1::
                                      ErrorContext::IK_NO_SOLUTIONS_FOUND) {
    error_message =
        "\nIK solver couldn't find any solutions for the given constraints. "
        "Typically this means the Cartesian constraint region was"
        " either outside the robot's reachable envelope or that"
        " it led to limit violations.";
  } else if (ik_error.error_context() ==
             intrinsic_proto::motion_planning::v1::ErrorContext::
                 IK_NO_SOLUTIONS_FOUND_W_COLLISION_CHECKING) {
    error_message =
        "\nIK solver returned no solutions. The motion target is either "
        "outside the reachable workspace, overconstrained, or in collision.";
  }
  std::string target_constraint;
  if (ik_error.constraint().has_cartesian_pose()) {
    if (ik_error.constraint().cartesian_pose().has_target_frame_offset()) {
      const std::string position_str =
          GetPositionString(ik_error.constraint()
                                .cartesian_pose()
                                .target_frame_offset()
                                .position());
      const std::string orientation_str =
          GetOrientationString(ik_error.constraint()
                                   .cartesian_pose()
                                   .target_frame_offset()
                                   .orientation());
      target_constraint = absl::StrCat(
          "\n\n\tTarget frame offset : \n\t\t Position : ", position_str,
          "\n\t\t Orientation : ", orientation_str);
    }
  }
  if (ik_error.constraint().has_position_equality()) {
    if (ik_error.constraint().position_equality().has_target_frame_offset()) {
      const std::string position_str = GetPositionString(
          ik_error.constraint().position_equality().target_frame_offset());
      target_constraint = absl::StrCat(
          "\n\n\tTarget frame offset : \n\t\t Position : ", position_str);
    }
  }

  std::string constraint_str =
      intrinsic::skills::GetTargetAndMovingFrameFromConstraint(
          ik_error.constraint());
  absl::StrAppend(&error_message,
                  "\n\nRecommendation : Ensure the target frame is reachable "
                  "by the moving frame.",
                  constraint_str, target_constraint, "\n");
  return error_message;
}

absl::StatusOr<std::string> GetUserFriendlyFinePathIKErrorMessage(
    const intrinsic_proto::motion_planning::v1::FinePathIKError&
        fine_path_ik_error) {
  std::string error_message;
  if (fine_path_ik_error.error_context() ==
      intrinsic_proto::motion_planning::v1::ErrorContext::
          LINEAR_CARTESIAN_PATH_PLANNER_ERROR) {
    error_message =
        "\nCould not generate a continuous Cartesian linear path. This might "
        "happen when planning close to joint limits or singular "
        "configurations.\n";
  }
  if (fine_path_ik_error.error_context() ==
      intrinsic_proto::motion_planning::v1::ErrorContext::FINE_PATH_IK_ERROR) {
    error_message =
        "\nCould not solve IK without excessive change in joint config. This "
        "might happen when planning close to joint limits or singular "
        "configurations.\n";
  }
  return error_message;
}

absl::StatusOr<std::string>
GetUserFriendlyLinearCartesianPathPlanningErrorMessage(
    const intrinsic_proto::motion_planning::v1::
        LinearCartesianMotionPathPlannerError
            linear_cartesian_path_planning_error,
    bool use_rad) {
  std::string error_message =
      "Could not generate Cartesian linear path to the desired motion target "
      "joint configuration";
  if (linear_cartesian_path_planning_error.error_context() ==
      intrinsic_proto::motion_planning::v1::ErrorContext::
          LINEAR_CARTESIAN_PATH_PLANNER_ERROR) {
    const std::string unit = use_rad ? "rad" : "deg";
    if (linear_cartesian_path_planning_error.has_target_joint_positions()) {
      INTR_ASSIGN_OR_RETURN(
          eigenmath::VectorNd target_joint_positions,
          icon::FromProto(
              linear_cartesian_path_planning_error.target_joint_positions()));
      if (!use_rad) {
        target_joint_positions =
            ConvertRadiansToDegrees(target_joint_positions);
      }

      absl::StrAppend(&error_message,
                      absl::StrFormat(" [%s] in %s",
                                      toString(target_joint_positions), unit));
    }
    absl::StrAppend(&error_message, ".\n");
    if (linear_cartesian_path_planning_error.has_final_joint_positions()) {
      INTR_ASSIGN_OR_RETURN(
          eigenmath::VectorNd final_joint_positions,
          icon::FromProto(
              linear_cartesian_path_planning_error.final_joint_positions()));
      if (!use_rad) {
        final_joint_positions = ConvertRadiansToDegrees(final_joint_positions);
      }
      absl::StrAppend(
          &error_message,
          absl::StrFormat(
              "Planner reached final joint configuration [%s] in %s.",
              toString(final_joint_positions), unit));
    }
    absl::StrAppend(
        &error_message,
        "\nYou can try to either use the reached final joint "
        "configuration as motion target or restrict the joint position limits "
        "of the motion target to favor IK solutions closer to the reached "
        "configuration.");
  }
  return error_message;
}

absl::StatusOr<std::string> GetUserFriendlyJointLimitErrorMessage(
    const intrinsic_proto::motion_planning::v1::JointLimitError&
        joint_limit_error,
    bool use_rad) {
  if (!joint_limit_error.has_joint_positions()) {
    return "Joint limits violated. Please check start and target state.";
  }
  INTR_ASSIGN_OR_RETURN(eigenmath::VectorNd joint_positions,
                        icon::FromProto(joint_limit_error.joint_positions()));
  const std::string unit = use_rad ? "rad" : "deg";
  if (!use_rad) {
    joint_positions = ConvertRadiansToDegrees(joint_positions);
  }
  if (joint_limit_error.error_context() ==
      intrinsic_proto::motion_planning::v1::ErrorContext::
          INITIAL_CONF_VALIDATION) {
    return absl::StrFormat(
        "The initial joint configuration [%s] in %s is not "
        "within joint limits.",
        toString(joint_positions), unit);
  }
  if (joint_limit_error.error_context() ==
      intrinsic_proto::motion_planning::v1::ErrorContext::
          GOAL_CONF_VALIDATION) {
    return absl::StrFormat(
        "The goal joint configuration [%s] in %s is not "
        "within joint limits.",
        toString(joint_positions), unit);
  }
  if (joint_limit_error.error_context() ==
      intrinsic_proto::motion_planning::v1::ErrorContext::CONF_VALIDATION) {
    return absl::StrFormat(
        "Joint configuration [%s] in %s is not within joint limits.",
        toString(joint_positions), unit);
  }
  if (joint_limit_error.error_context() ==
      intrinsic_proto::motion_planning::v1::ErrorContext::
          REFINEMENT_VALIDATION) {
    return absl::StrFormat(
        "Trajectory failed validation due to joint limit violation. Joint "
        "configuration [%s] in %s is not within joint limits.",
        toString(joint_positions), unit);
  }
  return absl::StrFormat("Joint limits violated for configuration [%s] in %s.",
                         toString(joint_positions), unit);
}

}  // namespace

absl::Status GetMotionPlanningExtendedStatusErrorMessage(
    absl::Status status, bool use_rad, absl::Time timestamp,
    absl::string_view component) {
  uint32_t error_code = 0;

  std::string user_message;
  intrinsic_proto::motion_planning::v1::MotionPlanningError
      motion_planning_error;
  intrinsic_proto::motion_planning::v1::MotionPipelineError
      motion_pipeline_error;
  status.ForEachPayload([&](absl::string_view type_url,
                            const absl::Cord& payload) {
    if (absl::StrContains(type_url, "MotionPipelineError")) {
      if (!motion_pipeline_error.ParseFromString(payload)) {
        return;
      }
      motion_planning_error =
          motion_pipeline_error.motion_planning_error().at(0);
    } else if (absl::StrContains(type_url, "MotionPlanningError")) {
      if (!motion_planning_error.ParseFromString(payload)) {
        return;
      }
    } else {
      return;
    }

    absl::StatusOr<std::string> status_or_error_message;
    switch (motion_planning_error.error_case()) {
      case intrinsic_proto::motion_planning::v1::MotionPlanningError::
          kCollisionError: {
        error_code = kCollisionErrorCode;
        status_or_error_message = GetUserFriendlyCollisionErrorMessage(
            motion_planning_error.collision_error(), use_rad);
        break;
      }
      case intrinsic_proto::motion_planning::v1::MotionPlanningError::
          kIkError: {
        error_code = kIKErrorCode;
        status_or_error_message =
            GetUserFriendlyIKErrorMessage(motion_planning_error.ik_error());
        break;
      }
      case intrinsic_proto::motion_planning::v1::MotionPlanningError::
          kFinePathIkError: {
        error_code = kFinePathIKErrorCode;
        status_or_error_message = GetUserFriendlyFinePathIKErrorMessage(
            motion_planning_error.fine_path_ik_error());
        break;
      }
      case intrinsic_proto::motion_planning::v1::MotionPlanningError::
          kLinearCartesianPathPlanningError: {
        error_code = kLinearCartesianPathPlanningErrorCode;
        status_or_error_message =
            GetUserFriendlyLinearCartesianPathPlanningErrorMessage(
                motion_planning_error.linear_cartesian_path_planning_error(),
                use_rad);
        break;
      }
      case intrinsic_proto::motion_planning::v1::MotionPlanningError::
          kJointLimitError: {
        error_code = kJointLimitErrorCode;
        status_or_error_message = GetUserFriendlyJointLimitErrorMessage(
            motion_planning_error.joint_limit_error(), use_rad);
        break;
      }
      default:
        return;
    }

    const std::string prepend_segment_id =
        GetSegmentIDString(motion_planning_error);
    if (!status_or_error_message.status().ok() ||
        status_or_error_message.value().empty()) {
      // We do not raise an error, but instead just return the
      // original status
      LOG(INFO) << "Unable to create pretty message, returning to original "
                   "status";
    } else {
      user_message = absl::StrCat(
          prepend_segment_id,
          status_or_error_message.value()
      );
    }
  });

  if (user_message.empty()) {
    LOG(INFO)
        << "No `MotionPlanningError` found or unable to create a pretty "
           "error. Converting original error message to `ExtendedStatus`.";
    user_message = status.message();
    error_code = kMotionPlanningErrorCode;
  }

  return GetMotionPlanningExtendedStatusErrorMessage(
      status, error_code, user_message, /*debug_message=*/"", timestamp,
      component);
}

absl::Status GetMotionPlanningExtendedStatusErrorMessage(
    const absl::Status& status, uint32_t error_code,
    absl::string_view user_message, absl::string_view debug_message,
    absl::Time timestamp, absl::string_view component) {
  StatusBuilder::ExtendedStatusOptions extended_status_options = {
      .timestamp = timestamp,
      .user_message = std::string(user_message),
      .generic_code = status.code()};
  if (!debug_message.empty()) {
    extended_status_options.debug_message = std::string(debug_message);
  }

  std::optional<intrinsic_proto::status::ExtendedStatus> received_es =
      intrinsic::GetExtendedStatus(status);
  if (received_es.has_value()) {
    extended_status_options.context.push_back(*received_es);
  } else {
    intrinsic_proto::status::ExtendedStatus context_es;
    context_es.mutable_status_code()->set_code(
        static_cast<uint32_t>(status.code()));
    context_es.set_title(absl::StrFormat(
        "Generic failure (code %s)", absl::StatusCodeToString(status.code())));
    context_es.mutable_user_report()->set_message(status.message());
    extended_status_options.context.push_back(std::move(context_es));
  }

  return StatusBuilder(component, error_code, extended_status_options);
}

// Helper function to convert joint positions from radians to degrees.
eigenmath::VectorNd ConvertRadiansToDegrees(
    const eigenmath::VectorNd& joint_positions) {
  if (joint_positions.size() == 0) {
    // Return an empty vector if the input is empty.
    return joint_positions;
  }
  eigenmath::VectorNd degrees_joint_positions(joint_positions.size());
  for (int i = 0; i < joint_positions.size(); ++i) {
    degrees_joint_positions(i) = joint_positions(i) * 180.0 / M_PI;
  }
  return degrees_joint_positions;
}

}  // namespace intrinsic::skills
