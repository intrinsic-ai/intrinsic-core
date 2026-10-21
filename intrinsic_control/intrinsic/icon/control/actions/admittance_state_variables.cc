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

#include "intrinsic/icon/control/actions/admittance_state_variables.h"

#include <cstdlib>
#include <limits>
#include <optional>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "intrinsic/icon/actions/action_utils.h"
#include "intrinsic/icon/actions/cartesian_impedance_parameters.h"
#include "intrinsic/icon/actions/compliance_info.h"
#include "intrinsic/icon/cc_client/condition.h"
#include "intrinsic/icon/common/builtins.h"
#include "intrinsic/icon/proto/v1/types.pb.h"
#include "intrinsic/icon/utils/fixed_str_cat.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/icon/utils/realtime_status_or.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/twist.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic::icon {

absl::Status AdmittanceStateVariables::RegisterVariables(
    ActionSignatureBuilder& builder) {
  INTR_RETURN_IF_ERROR(
      builder.AddStateVariable<intrinsic_proto::icon::v1::ActionSignature::
                                   StateVariableInfo::TYPE_BOOL>(
          kIsDone, ComplianceInfo::kIsDoneDescription));
  INTR_RETURN_IF_ERROR(
      builder.AddStateVariable<intrinsic_proto::icon::v1::ActionSignature::
                                   StateVariableInfo::TYPE_BOOL>(
          ComplianceInfo::kIsSettled, ComplianceInfo::kIsSettledDescription));
  INTR_RETURN_IF_ERROR(
      builder.AddStateVariable<intrinsic_proto::icon::v1::ActionSignature::
                                   StateVariableInfo::TYPE_BOOL>(
          ComplianceInfo::kReferencePoseReached,
          ComplianceInfo::kReferencePoseReachedDescription));
  INTR_RETURN_IF_ERROR(
      builder.AddStateVariable<intrinsic_proto::icon::v1::ActionSignature::
                                   StateVariableInfo::TYPE_DOUBLE>(
          ComplianceInfo::kReferencePositionError,
          ComplianceInfo::kReferencePositionErrorDescription));
  INTR_RETURN_IF_ERROR(
      builder.AddStateVariable<intrinsic_proto::icon::v1::ActionSignature::
                                   StateVariableInfo::TYPE_DOUBLE>(
          ComplianceInfo::kReferenceOrientationError,
          ComplianceInfo::kReferenceOrientationErrorDescription));
  INTR_RETURN_IF_ERROR(
      builder.AddStateVariable<intrinsic_proto::icon::v1::ActionSignature::
                                   StateVariableInfo::TYPE_INT64>(
          ComplianceInfo::kStreamingInputMessageCount,
          ComplianceInfo::kStreamingInputMessageCountDescription));
  INTR_RETURN_IF_ERROR(
      builder.AddStateVariable<intrinsic_proto::icon::v1::ActionSignature::
                                   StateVariableInfo::TYPE_DOUBLE>(
          ComplianceInfo::kSettledForSeconds,
          ComplianceInfo::kSettledForSecondsDescription));
  INTR_RETURN_IF_ERROR(
      builder.AddStateVariable<intrinsic_proto::icon::v1::ActionSignature::
                                   StateVariableInfo::TYPE_DOUBLE>(
          ComplianceInfo::kSensedForce,
          ComplianceInfo::kSensedForceDescription));
  INTR_RETURN_IF_ERROR(
      builder.AddStateVariable<intrinsic_proto::icon::v1::ActionSignature::
                                   StateVariableInfo::TYPE_DOUBLE>(
          ComplianceInfo::kSensedTorque,
          ComplianceInfo::kSensedTorqueDescription));
  INTR_RETURN_IF_ERROR(
      builder.AddStateVariable<intrinsic_proto::icon::v1::ActionSignature::
                                   StateVariableInfo::TYPE_DOUBLE>(
          ComplianceInfo::kSensedForceInDirection1,
          ComplianceInfo::kSensedForceInDirection1Description));
  INTR_RETURN_IF_ERROR(
      builder.AddStateVariable<intrinsic_proto::icon::v1::ActionSignature::
                                   StateVariableInfo::TYPE_DOUBLE>(
          ComplianceInfo::kSensedForceInDirection2,
          ComplianceInfo::kSensedForceInDirection2Description));
  INTR_RETURN_IF_ERROR(
      builder.AddStateVariable<intrinsic_proto::icon::v1::ActionSignature::
                                   StateVariableInfo::TYPE_DOUBLE>(
          ComplianceInfo::kSensedForceInDirection3,
          ComplianceInfo::kSensedForceInDirection3Description));
  INTR_RETURN_IF_ERROR(
      builder.AddStateVariable<intrinsic_proto::icon::v1::ActionSignature::
                                   StateVariableInfo::TYPE_DOUBLE>(
          ComplianceInfo::kSensedTorqueAboutAxis1,
          ComplianceInfo::kSensedTorqueAboutAxis1Description));
  INTR_RETURN_IF_ERROR(
      builder.AddStateVariable<intrinsic_proto::icon::v1::ActionSignature::
                                   StateVariableInfo::TYPE_DOUBLE>(
          ComplianceInfo::kSensedTorqueAboutAxis2,
          ComplianceInfo::kSensedTorqueAboutAxis2Description));
  INTR_RETURN_IF_ERROR(
      builder.AddStateVariable<intrinsic_proto::icon::v1::ActionSignature::
                                   StateVariableInfo::TYPE_DOUBLE>(
          ComplianceInfo::kSensedTorqueAboutAxis3,
          ComplianceInfo::kSensedTorqueAboutAxis3Description));
  INTR_RETURN_IF_ERROR(
      builder.AddStateVariable<intrinsic_proto::icon::v1::ActionSignature::
                                   StateVariableInfo::TYPE_DOUBLE>(
          ComplianceInfo::kManipulabilityIndex,
          ComplianceInfo::kManipulabilityIndexDescription));
  INTR_RETURN_IF_ERROR(
      builder.AddStateVariable<intrinsic_proto::icon::v1::ActionSignature::
                                   StateVariableInfo::TYPE_DOUBLE>(
          ComplianceInfo::kActionElapsedTimeSeconds,
          ComplianceInfo::kActionElapsedTimeSecondsDescription));
  INTR_RETURN_IF_ERROR(
      builder.AddStateVariable<intrinsic_proto::icon::v1::ActionSignature::
                                   StateVariableInfo::TYPE_DOUBLE>(
          ComplianceInfo::kTranslationalDistanceTraveled,
          ComplianceInfo::kTranslationalDistanceTraveledDescription));
  INTR_RETURN_IF_ERROR(builder.AddStateVariable<
                       intrinsic_proto::icon::v1::ActionSignature::
                           StateVariableInfo::TYPE_DOUBLE>(
      ComplianceInfo::kMaximumTranslationalDisplacementInWindow,
      ComplianceInfo::kMaximumTranslationalDisplacementInWindowDescription));
  return absl::OkStatus();
};

RealtimeStatusOr<StateVariableValue> AdmittanceStateVariables::GetStateVariable(
    absl::string_view name) const {
  if (name == kIsDone) {
    return StateVariableValue(is_done_);
  }
  if (name == ComplianceInfo::kIsSettled) {
    return StateVariableValue(is_settled_);
  }
  if (name == ComplianceInfo::kReferencePoseReached) {
    if (!reference_pose_reached_.has_value()) {
      return icon::UnavailableError(
          "reference_pose_reached is unknown (did you set call Sense() at "
          "least once?");
    }
    return StateVariableValue(reference_pose_reached_.value());
  }
  if (name == ComplianceInfo::kReferencePositionError) {
    if (!reference_position_error_.has_value()) {
      return icon::UnavailableError(
          "reference_position_error is unknown (did you set call Sense() at "
          "least once?");
    }
    return StateVariableValue(reference_position_error_.value());
  }
  if (name == ComplianceInfo::kReferenceOrientationError) {
    if (!reference_orientation_error_.has_value()) {
      return icon::UnavailableError(
          "reference_orientation_error is unknown (did you set call Sense() at "
          "least once?");
    }
    return StateVariableValue(reference_orientation_error_.value());
  }
  if (name == ComplianceInfo::kStreamingInputMessageCount) {
    return StateVariableValue(streaming_input_message_count_);
  }
  if (name == ComplianceInfo::kSettledForSeconds) {
    if (!settled_for_seconds_.has_value()) {
      return icon::UnavailableError(
          "SettledForSeconds is unknown (did you set call Sense() at least "
          "once?");
    }
    return StateVariableValue(settled_for_seconds_.value());
  }
  if (name == ComplianceInfo::kSensedForce) {
    if (!sensed_force_.has_value()) {
      return icon::UnavailableError(
          "Sensed Force is unknown (did you set call Sense() at least once?");
    }
    return StateVariableValue(sensed_force_.value());
  }
  if (name == ComplianceInfo::kSensedTorque) {
    if (!sensed_torque_.has_value()) {
      return icon::UnavailableError(
          "Sensed Torque is unknown (did you set call Sense() at least "
          "once?");
    }
    return StateVariableValue(sensed_torque_.value());
  }
  if (name == ComplianceInfo::kSensedForceInDirection1) {
    if (!sensed_force_in_direction_1_.has_value()) {
      return icon::UnavailableError(
          "Sensed Force in Direction 1 is unknown (did you set call Sense() at "
          "least once?");
    }
    return StateVariableValue(sensed_force_in_direction_1_.value());
  }
  if (name == ComplianceInfo::kSensedForceInDirection2) {
    if (!sensed_force_in_direction_2_.has_value()) {
      return icon::UnavailableError(
          "Sensed Force in Direction 2 is unknown (did you set call Sense() at "
          "least once?");
    }
    return StateVariableValue(sensed_force_in_direction_2_.value());
  }
  if (name == ComplianceInfo::kSensedForceInDirection3) {
    if (!sensed_force_in_direction_3_.has_value()) {
      return icon::UnavailableError(
          "Sensed Force in Direction 3 is unknown (did you set call Sense() at "
          "least once?");
    }
    return StateVariableValue(sensed_force_in_direction_3_.value());
  }
  if (name == ComplianceInfo::kSensedTorqueAboutAxis1) {
    if (!sensed_torque_about_axis_1_.has_value()) {
      return icon::UnavailableError(
          "Sensed Torque about Axis 1 is unknown (did you set call "
          "Sense() at least once?");
    }
    return StateVariableValue(sensed_torque_about_axis_1_.value());
  }
  if (name == ComplianceInfo::kSensedTorqueAboutAxis2) {
    if (!sensed_torque_about_axis_2_.has_value()) {
      return icon::UnavailableError(
          "Sensed Torque about Axis 2 is unknown (did you set call "
          "Sense() at least once?");
    }
    return StateVariableValue(sensed_torque_about_axis_2_.value());
  }
  if (name == ComplianceInfo::kSensedTorqueAboutAxis3) {
    if (!sensed_torque_about_axis_3_.has_value()) {
      return icon::UnavailableError(
          "Sensed Torque about Axis 3 is unknown (did you set call "
          "Sense() at least once?");
    }
    return StateVariableValue(sensed_torque_about_axis_3_.value());
  }

  if (name == ComplianceInfo::kManipulabilityIndex) {
    if (!manipulability_.has_value()) {
      return icon::UnavailableError(
          "Manipulability Index is unknown (did you set call Sense() at least "
          "once?");
    }
    return StateVariableValue(manipulability_.value());
  }
  if (name == ComplianceInfo::kActionElapsedTimeSeconds) {
    return StateVariableValue(elapsed_time_seconds_);
  }
  if (name == ComplianceInfo::kTranslationalDistanceTraveled) {
    if (!translational_distance_traveled_.has_value()) {
      return icon::UnavailableError(
          "Translational distance is unknown (did you set call Sense() at "
          "least once?");
    }
    return StateVariableValue(*translational_distance_traveled_);
  }
  if (name == ComplianceInfo::kMaximumTranslationalDisplacementInWindow) {
    return StateVariableValue(maximum_translational_displacement_in_window_);
  };
  return icon::NotFoundError(FixedStrCat<RealtimeStatus::kMaxMessageLength>(
      "State variable ", name, " not found."));
}

void AdmittanceStateVariables::Reset() {
  is_done_ = false;
  reference_pose_reached_ = std::nullopt;
  reference_position_error_ = std::nullopt;
  reference_orientation_error_ = std::nullopt;
  streaming_input_message_count_ = 0;
  settled_for_seconds_ = std::nullopt;
  sensed_force_ = std::nullopt;
  sensed_torque_ = std::nullopt;
  manipulability_ = std::nullopt;
  elapsed_time_seconds_ = 0.0;
  translational_distance_traveled_ = std::nullopt;
  is_settled_ = false;
  sensed_force_in_direction_1_ = std::nullopt;
  sensed_force_in_direction_2_ = std::nullopt;
  sensed_force_in_direction_3_ = std::nullopt;
  sensed_torque_about_axis_1_ = std::nullopt;
  sensed_torque_about_axis_2_ = std::nullopt;
  sensed_torque_about_axis_3_ = std::nullopt;
  maximum_translational_displacement_in_window_ =
      std::numeric_limits<double>::infinity();
}

void AdmittanceStateVariables::Update(const UpdateParams& values) {
  reference_pose_reached_ = values.current_task_t_tool.isApprox(
      values.reference_task_t_tool,
      state_variable_config_.position_error_threshold,
      state_variable_config_.orientation_error_threshold);
  reference_position_error_ = (values.current_task_t_tool.translation() -
                               values.reference_task_t_tool.translation())
                                  .norm();
  reference_orientation_error_ =
      std::abs(values.current_task_t_tool.quaternion().angularDistance(
          values.reference_task_t_tool.quaternion()));
  streaming_input_message_count_ = values.streaming_input_message_count;
  sensed_force_ = values.sensed_wrench.head<3>().norm();
  sensed_torque_ = values.sensed_wrench.tail<3>().norm();
  sensed_torque_about_axis_1_ = values.sensed_wrench.tail<3>().dot(
      state_variable_config_.torque_axis_1_in_task);
  sensed_torque_about_axis_2_ = values.sensed_wrench.tail<3>().dot(
      state_variable_config_.torque_axis_2_in_task);
  sensed_torque_about_axis_3_ = values.sensed_wrench.tail<3>().dot(
      state_variable_config_.torque_axis_3_in_task);
  sensed_force_in_direction_1_ = values.sensed_wrench.head<3>().dot(
      state_variable_config_.force_direction_1_in_task);
  sensed_force_in_direction_2_ = values.sensed_wrench.head<3>().dot(
      state_variable_config_.force_direction_2_in_task);
  sensed_force_in_direction_3_ = values.sensed_wrench.head<3>().dot(
      state_variable_config_.force_direction_3_in_task);

  is_done_ = values.is_done;
  manipulability_ = values.manipulability;
  elapsed_time_seconds_ = values.elapsed_time_seconds;
  translational_distance_traveled_ = values.translational_distance_traveled_m;
  maximum_translational_displacement_in_window_ =
      values.maximum_translational_displacement_in_window_m;
  is_settled_ = values.is_settled;
  settled_for_seconds_ = values.settled_for_seconds;
}

void AdmittanceStateVariables::SetStateVariableConfig(
    const cartesian_impedance::StateVariableConfiguration&
        state_variable_config) {
  state_variable_config_ = state_variable_config;
}

}  // namespace intrinsic::icon
