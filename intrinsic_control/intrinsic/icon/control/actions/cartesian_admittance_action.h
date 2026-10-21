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

#ifndef INTRINSIC_ICON_CONTROL_ACTIONS_CARTESIAN_ADMITTANCE_ACTION_H_
#define INTRINSIC_ICON_CONTROL_ACTIONS_CARTESIAN_ADMITTANCE_ACTION_H_

#include <stdbool.h>

#include <cstddef>
#include <memory>
#include <optional>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "intrinsic/icon/actions/cartesian_admittance_info.h"
#include "intrinsic/icon/actions/cartesian_impedance_parameters.h"
#include "intrinsic/icon/cc_client/condition.h"
#include "intrinsic/icon/control/action_factory_context.h"
#include "intrinsic/icon/control/actions/admittance_state_variables.h"
#include "intrinsic/icon/control/algorithms/admittance_controller_interface.h"
#include "intrinsic/icon/control/algorithms/distance_tracker.h"
#include "intrinsic/icon/control/algorithms/impedance_reference_generator_holder.h"
#include "intrinsic/icon/control/algorithms/is_settled_criterion.h"
#include "intrinsic/icon/control/parts/feature_interfaces.h"
#include "intrinsic/icon/control/rtcl_action.h"
#include "intrinsic/icon/control/slot_types.h"
#include "intrinsic/icon/control/streaming_io_types.h"
#include "intrinsic/icon/proto/v1/types.pb.h"
#include "intrinsic/icon/testing/realtime_annotations.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/icon/utils/realtime_status_or.h"
#include "intrinsic/kinematics/elements.h"
#include "intrinsic/kinematics/ik/inverse_kinematics_interface.h"
#include "intrinsic/kinematics/state.h"
#include "intrinsic/kinematics/types/joint_state.h"
#include "intrinsic/math/pose3.h"

namespace intrinsic::icon {

// Cartesian Admittance action using joint positions as control input, based on
// the algorithmic implementation in
// intrinsic/icon/control/algorithms/cartesian_admittance_controller.h
class CartesianAdmittanceAction final
    : public intrinsic::icon::RtclActionInterface {
 public:
  CartesianAdmittanceAction() = delete;

  CartesianAdmittanceAction(
      RealtimeSlotId arm_slot_id, RealtimeSlotId ft_slot_id,
      StreamingInputId streaming_input_id, kinematics::ElementId tip_id,
      size_t njoints, double icon_frequency_hz,
      int num_controller_substeps_per_icon_cycle,
      CartesianImpedanceParameters params,
      std::unique_ptr<AdmittanceControllerInterface> controller,
      std::unique_ptr<MaximumTranslationalDistanceTracker> distance_tracker,
      std::unique_ptr<IsSettledCriterion> is_settled_criterion);

  static absl::StatusOr<std::unique_ptr<CartesianAdmittanceAction>> Create(
      const CartesianAdmittanceInfo::FixedParams& params_proto,
      ActionFactoryContext& context) INTRINSIC_NON_REALTIME_ONLY;

  // Perform non-realtime pre-processing steps on the received parameters, such
  // as definiteness checking and auto-derivation of missing elements.
  static absl::StatusOr<CartesianImpedanceParameters> ParseInput(
      const CartesianAdmittanceInfo::FixedParams& proto_params);

  static intrinsic_proto::icon::v1::ActionSignature GetSignature();

  // Resets current control targets and controller internal state.
  RealtimeStatus OnEnter(OnEnterParameters params)
      INTRINSIC_CHECK_REALTIME_SAFE override;

  RealtimeStatus Sense(SenseParameters params)
      INTRINSIC_CHECK_REALTIME_SAFE override;

  RealtimeStatus Control(ControlParameters params)
      INTRINSIC_CHECK_REALTIME_SAFE override;

  RealtimeStatusOr<StateVariableValue> GetStateVariable(
      absl::string_view name) const override;

 private:
  // Sense logic for substepping at higher control frequency.
  RealtimeStatus SenseSubStep(const JointPositionSensor& jpos_sensor,
                              const JointVelocityEstimator& jvel_estimator,
                              const JointLimitsInterface& joint_limits,
                              const CartesianLimitsInterface& cart_limits,
                              const ManipulatorKinematics& kinematics,
                              const ForceTorqueSensor& ft_sensor,
                              const JointStatePVA& previous_joint_command,
                              double speed_override)
      INTRINSIC_CHECK_REALTIME_SAFE;

  // Control logic for substepping at higher control frequency.
  RealtimeStatusOr<JointStatePVA> ControlSubStep(
      const kinematics::InverseKinematicsInterface& ik,
      const ForceTorqueSensor& ft_sensor) INTRINSIC_CHECK_REALTIME_SAFE;

  // Sets all controller parameters at once after running real-time checks.
  RealtimeStatus SetCompleteReference(
      const JointLimitsInterface& joint_limits_interface,
      const CartesianLimitsInterface& cart_limits_interface,
      const CartesianImpedanceParameters& parameters,
      const JointStatePVA& joint_state, const Pose3d& base_t_tip);

  RealtimeSlotId arm_slot_id_;
  RealtimeSlotId ft_slot_id_;
  StreamingInputId streaming_input_id_;
  int njoints_;
  std::unique_ptr<AdmittanceControllerInterface> controller_;
  ImpedanceReferenceGeneratorHolder impedance_reference_generator_holder_;
  NullspaceReferenceGeneratorHolder nullspace_reference_generator_holder_;
  AdmittanceStateVariables state_variables_;
  cartesian_impedance::StateVariableConfiguration state_variable_config_;
  kinematics::ElementId tip_id_;
  CartesianImpedanceParameters params_;

  // Frequency at which ICON is ticked (can be different from internal control
  // frequency due to substepping).
  const double icon_frequency_hz_;
  // Number of controller substeps executed per ICON control cycle.
  const int num_controller_substeps_per_icon_cycle_;

  std::optional<Pose3d> task_t_tool_start_;
  int64_t streaming_input_message_count_ = 0;
  std::unique_ptr<MaximumTranslationalDistanceTracker> distance_tracker_;

  // Tracking kinematics::State as member because of b/287941171.
  std::optional<kinematics::State> state_ = std::nullopt;

  std::unique_ptr<IsSettledCriterion> is_settled_criterion_;
};

}  // namespace intrinsic::icon

#endif  // INTRINSIC_ICON_CONTROL_ACTIONS_CARTESIAN_ADMITTANCE_ACTION_H_
