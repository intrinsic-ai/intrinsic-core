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

#ifndef INTRINSIC_ICON_CONTROL_ACTIONS_ADMITTANCE_STATE_VARIABLES_H_
#define INTRINSIC_ICON_CONTROL_ACTIONS_ADMITTANCE_STATE_VARIABLES_H_

#include <limits>
#include <optional>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "intrinsic/icon/actions/action_utils.h"
#include "intrinsic/icon/actions/cartesian_impedance_parameters.h"
#include "intrinsic/icon/cc_client/condition.h"
#include "intrinsic/icon/utils/realtime_status_or.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/twist.h"

namespace intrinsic::icon {

// State variables used in the Cartesian and Trajectory admittance actions. The
// variables are described in intrinsic/icon/actions/compliance_info.h
class AdmittanceStateVariables {
 public:
  // Registers the state variables in the given action signature builder.
  static absl::Status RegisterVariables(ActionSignatureBuilder& builder);

  // Returns the state variable value for the given name.
  // Returns an error if the state variable is not available.
  RealtimeStatusOr<StateVariableValue> GetStateVariable(
      absl::string_view name) const;

  // Sets the state variable configuration.
  void SetStateVariableConfig(
      const cartesian_impedance::StateVariableConfiguration&
          state_variable_config);

  // Resets all state variables to their default values.
  void Reset();

  struct UpdateParams {
    // The current pose of the tool in the task frame.
    Pose3d current_task_t_tool;
    // The desired reference pose of the tool in the task frame.
    Pose3d reference_task_t_tool;
    double settling_time_s;
    // The sensed wrench at the tool.
    Wrench sensed_wrench;
    double manipulability;
    double elapsed_time_seconds;
    double translational_distance_traveled_m;
    double maximum_translational_displacement_in_window_m;
    bool is_done;
    bool is_settled;
    double settled_for_seconds;
    int64_t streaming_input_message_count = 0;
  };
  // Updates the state variables with the given values. Computes the values for
  // reference and force related state variables internally.
  void Update(const UpdateParams& values);

  double GetElapsedTimeSeconds() const { return elapsed_time_seconds_; }
  std::optional<double> GetSettledForSeconds() const {
    return settled_for_seconds_;
  }
  std::optional<double> GetSensedForce() const { return sensed_force_; }
  std::optional<double> GetSensedTorque() const { return sensed_torque_; }
  std::optional<double> GetTranslationalDistanceTraveled() const {
    return translational_distance_traveled_;
  }
  std::optional<double> GetMaximumTranslationalDisplacementInWindow() const {
    return maximum_translational_displacement_in_window_;
  }
  bool GetIsSettled() const { return is_settled_; }

 private:
  cartesian_impedance::StateVariableConfiguration state_variable_config_;

  bool is_done_ = false;
  std::optional<bool> reference_pose_reached_ = std::nullopt;
  std::optional<double> reference_position_error_ = std::nullopt;
  std::optional<double> reference_orientation_error_ = std::nullopt;
  int64_t streaming_input_message_count_ = 0;
  std::optional<double> settled_for_seconds_ = std::nullopt;
  std::optional<double> sensed_force_ = std::nullopt;
  std::optional<double> sensed_torque_ = std::nullopt;
  std::optional<double> manipulability_ = std::nullopt;
  double elapsed_time_seconds_ = 0.0;
  std::optional<double> translational_distance_traveled_ = std::nullopt;
  bool is_settled_ = false;
  std::optional<double> sensed_force_in_direction_1_ = std::nullopt;
  std::optional<double> sensed_force_in_direction_2_ = std::nullopt;
  std::optional<double> sensed_force_in_direction_3_ = std::nullopt;
  std::optional<double> sensed_torque_about_axis_1_ = std::nullopt;
  std::optional<double> sensed_torque_about_axis_2_ = std::nullopt;
  std::optional<double> sensed_torque_about_axis_3_ = std::nullopt;
  double maximum_translational_displacement_in_window_ =
      std::numeric_limits<double>::infinity();
};

}  // namespace intrinsic::icon

#endif  // INTRINSIC_ICON_CONTROL_ACTIONS_ADMITTANCE_STATE_VARIABLES_H_
