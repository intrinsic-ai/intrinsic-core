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

#include "incode/motion_planning/skills/move_robot_util.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/any.pb.h"
#include "intrinsic/icon/cc_client/state_variable_path.h"
#include "intrinsic/icon/equipment/equipment_utils.h"
#include "intrinsic/icon/equipment/icon_equipment.pb.h"
#include "intrinsic/icon/proto/joint_space.pb.h"
#include "intrinsic/math/proto/pose.pb.h"
#include "intrinsic/math/proto/quaternion.pb.h"
#include "intrinsic/motion_planning/proto/v1/geometric_constraints.pb.h"
#include "intrinsic/motion_planning/proto/v1/motion_blending_parameter.pb.h"

#include "intrinsic/motion_planning/proto/v1/motion_events.pb.h"

#include "intrinsic/motion_planning/proto/v1/motion_planning_limits.pb.h"
#include "intrinsic/motion_planning/proto/v1/motion_specification.pb.h"
#include "intrinsic/motion_planning/skills/move_robot.pb.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/proto/collision_settings.pb.h"

namespace intrinsic::skills {

namespace {

constexpr bool kDefaultDigitalInValueToTriggerStop = true;

absl::StatusOr<intrinsic_proto::motion_planning::v1::GeometricConstraint>
CreateMotionTargetFromSkillSpecification(
    intrinsic_proto::skills::MotionSegment const& params) {
  intrinsic_proto::motion_planning::v1::GeometricConstraint result;
  switch (params.motion_target_case()) {
    case intrinsic_proto::skills::MotionSegment::MotionTargetCase::
        kJointPosition: {
      *result.mutable_joint_position() = params.joint_position();
      break;
    }
    case intrinsic_proto::skills::MotionSegment::MotionTargetCase::
        kCartesianPose: {
      *result.mutable_cartesian_pose() = params.cartesian_pose();
      // TODO(b/406554236) - this shouldn't be necessary
      if (!result.cartesian_pose().target_frame_offset().has_orientation()) {
        // Use identity orientation
        result.mutable_cartesian_pose()
            ->mutable_target_frame_offset()
            ->mutable_orientation()
            ->set_w(1.);
      }
      break;
    }
    case intrinsic_proto::skills::MotionSegment::MotionTargetCase::
        kConstraintIntersection: {
      *result.mutable_constraint_intersection() =
          params.constraint_intersection();
      break;
    }
    case intrinsic_proto::skills::MotionSegment::MotionTargetCase::
        kPositionEquality: {
      *result.mutable_position_equality() = params.position_equality();
      break;
    }
    case intrinsic_proto::skills::MotionSegment::MotionTargetCase::
        kRotationEquality: {
      *result.mutable_rotation_equality() = params.rotation_equality();
      break;
    }
    case intrinsic_proto::skills::MotionSegment::MotionTargetCase::
        kRelativeCartesianPose: {
      *result.mutable_relative_cartesian_pose() =
          params.relative_cartesian_pose();
      break;
    }
    case intrinsic_proto::skills::MotionSegment::MotionTargetCase::
        kRelativePositionEquality: {
      *result.mutable_relative_position_equality() =
          params.relative_position_equality();
      break;
    }
    case intrinsic_proto::skills::MotionSegment::MotionTargetCase::
        kRelativeRotationEquality: {
      *result.mutable_relative_rotation_equality() =
          params.relative_rotation_equality();
      break;
    }
    case intrinsic_proto::skills::MotionSegment::MotionTargetCase::
        kRotationCone: {
      *result.mutable_rotation_cone() = params.rotation_cone();
      break;
    }
    case intrinsic_proto::skills::MotionSegment::MotionTargetCase::
        kJointPositionLimits: {
      *result.mutable_joint_position_limits() = params.joint_position_limits();
      break;
    }
    case intrinsic_proto::skills::MotionSegment::MotionTargetCase::kPointAt: {
      *result.mutable_point_at() = params.point_at();
      break;
    }
    case intrinsic_proto::skills::MotionSegment::MotionTargetCase::
        MOTION_TARGET_NOT_SET: {
      return absl::InvalidArgumentError(
          "Motion target type unknown or not specified. Please make sure "
          "that there is at least one motion segment and that it has a "
          "well specified motion target.");
    }
  }
  return result;
}

absl::StatusOr<intrinsic_proto::motion_planning::v1::MotionSegment>
CreateMotionSpecSegment(
    intrinsic_proto::skills::MotionSegment const& skill_motion_segment) {
  intrinsic_proto::motion_planning::v1::MotionSegment result;
  INTR_ASSIGN_OR_RETURN(
      *result.mutable_target(),
      CreateMotionTargetFromSkillSpecification(skill_motion_segment));

  // TODO(kmuelling): Remove this once the skill is updated to use the
  // new motion_type field.
  if (skill_motion_segment.motion_type() ==
      intrinsic_proto::skills::MotionSegment::LINEAR) {
    result.set_motion_type(
        intrinsic_proto::motion_planning::v1::MotionSegment::LINEAR);
  }
  if (skill_motion_segment.motion_type() ==
      intrinsic_proto::skills::MotionSegment::JOINT) {
    result.set_motion_type(
        intrinsic_proto::motion_planning::v1::MotionSegment::JOINT);
  }

  if (skill_motion_segment.has_collision_settings()) {
    *(result.mutable_collision_settings()) =
        skill_motion_segment.collision_settings();
  }

  if (skill_motion_segment.has_path_constraints()) {
    *result.mutable_path_constraints() =
        skill_motion_segment.path_constraints();
  }

  if (skill_motion_segment.has_joint_limits()) {
    *result.mutable_joint_limits() = skill_motion_segment.joint_limits();
  }

  if (skill_motion_segment.has_cartesian_limits()) {
    *result.mutable_cartesian_limits() =
        skill_motion_segment.cartesian_limits();
  }


  for (auto const& skill_motion_segment_event :
       skill_motion_segment.motion_segment_events()) {
    intrinsic_proto::motion_planning::v1::MotionSegmentEvent*
        motion_segment_event = result.add_motion_segment_events();
    *motion_segment_event->mutable_event_location() =
        skill_motion_segment_event.event_location();
    if (skill_motion_segment_event.has_trajectory_event()) {
      if (!motion_segment_event->mutable_event()->PackFrom(
              skill_motion_segment_event.trajectory_event())) {
        return absl::InternalError("Failed to pack trajectory event");
      }
    }
  }


  return result;
}

}  // namespace

absl::StatusOr<intrinsic_proto::motion_planning::v1::MotionSpecification>
CreateMotionSpecificationFromMoveRobotSkillParams(
    intrinsic_proto::skills::MoveRobotParams const& params) {
  intrinsic_proto::motion_planning::v1::MotionSpecification motion_spec;

  for (auto const& motion_segment : params.motion_segments()) {
    INTR_ASSIGN_OR_RETURN(*motion_spec.add_motion_segments(),
                          CreateMotionSpecSegment(motion_segment));
  }
  *motion_spec.mutable_curve_parameters() = params.curve_parameters();
  return motion_spec;
}

absl::Status ValidateMoveUntilSignal(
    const intrinsic_proto::skills::MoveUntilSignalParameters& params) {
  if (params.has_move_until_signal_condition() && !params.dio_block().empty()) {
    return absl::InvalidArgumentError(
        "Only one of move_until_signal_condition and dio_block can be "
        "specified. Prefer specifying move_until_signal_condition.");
  }

  if (params.has_move_until_signal_condition()) {
    // We will directly use the condition provided by the user, so no further
    // validation is needed.
    return absl::OkStatus();
  }

  if (params.dio_block().empty()) {
    return absl::InvalidArgumentError(
        "dio_block must be provided if move_until_signal_condition is not "
        "specified.");
  }

  return absl::OkStatus();
}

absl::StatusOr<intrinsic_proto::icon::v1::Condition>
TranslateMoveUntilSignalParametersToCondition(
    const intrinsic_proto::skills::MoveUntilSignalParameters& params,
    const EquipmentPack& equipment) {
  INTR_RETURN_IF_ERROR(ValidateMoveUntilSignal(params));

  if (params.has_move_until_signal_condition()) {
    return params.move_until_signal_condition();
  }

  std::string adio_part_name;
  if (params.has_adio_part_name()) {
    adio_part_name = params.adio_part_name();
  } else {
    // If the user has not provided an adio part name, pick it from the icon
    // equipment if it isn't ambiguous and we are not directly specifying an
    // ICON Condition.
    auto maybe_adio_part =
        equipment.Unpack<intrinsic_proto::icon::Icon2AdioPart>(
            "robot", icon::kIcon2AdioPartKey);
    if (!maybe_adio_part.ok()) {
      return absl::InvalidArgumentError(
          absl::StrCat("Failed to find Icon2AdioPart in equipment data and "
                       "move until signal was requested. Error: ",
                       maybe_adio_part.status()));
    }
    if (maybe_adio_part->icon_parts_size() > 1) {
      return absl::InvalidArgumentError(
          "No adio_part_name was given, and the given ICON instance has "
          "multiple. Please specify 'adio_part_name' to select one part to "
          "use.");
    }
    if (maybe_adio_part->icon_parts().empty()) {
      // TODO(keegang): Do we have to worry about version mismatch between the
      // resource registry (which sets the resource data) and the skill? I.e.,
      // if this skill runs against an old resource registry, the map field
      // will NOT be set.
      return absl::InvalidArgumentError(
          "No adio_part_name was given, and none were provided in the map.");
    }
    adio_part_name = maybe_adio_part->icon_parts(0);
  }

  const bool value_of_signal = params.has_value_of_signal()
                                   ? params.value_of_signal()
                                   : kDefaultDigitalInValueToTriggerStop;

  intrinsic_proto::icon::v1::Condition condition;
  condition.mutable_comparison()->set_state_variable_name(
      icon::ADIODigitalInputStateVariablePath(
          adio_part_name, params.dio_block(), params.signal_index()));
  condition.mutable_comparison()->set_operation(
      ::intrinsic_proto::icon::v1::Comparison::EQUAL);
  condition.mutable_comparison()->set_bool_value(value_of_signal);

  return condition;
}

}  // namespace intrinsic::skills
