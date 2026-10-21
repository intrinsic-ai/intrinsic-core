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

#ifndef INTRINSIC_ICON_CONTROL_ACTIONS_CARTESIAN_IMPEDANCE_ACTION_H_
#define INTRINSIC_ICON_CONTROL_ACTIONS_CARTESIAN_IMPEDANCE_ACTION_H_

#include <stdbool.h>

#include <cstddef>
#include <memory>
#include <optional>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "intrinsic/icon/actions/cartesian_impedance_info.h"
#include "intrinsic/icon/actions/cartesian_impedance_parameters.h"
#include "intrinsic/icon/cc_client/condition.h"
#include "intrinsic/icon/control/action_factory_context.h"
#include "intrinsic/icon/control/actions/admittance_state_variables.h"
#include "intrinsic/icon/control/algorithms/cartesian_impedance_commons.h"
#include "intrinsic/icon/control/algorithms/cartesian_impedance_controller.h"
#include "intrinsic/icon/control/algorithms/distance_tracker.h"
#include "intrinsic/icon/control/algorithms/impedance_reference_generator_holder.h"
#include "intrinsic/icon/control/algorithms/is_settled_criterion.h"
#include "intrinsic/icon/control/realtime_slot_map.h"
#include "intrinsic/icon/control/rtcl_action.h"
#include "intrinsic/icon/control/slot_types.h"
#include "intrinsic/icon/control/streaming_io_types.h"
#include "intrinsic/icon/proto/v1/types.pb.h"
#include "intrinsic/icon/testing/realtime_annotations.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/icon/utils/realtime_status_or.h"
#include "intrinsic/kinematics/skeleton.h"
#include "intrinsic/kinematics/types/joint_state.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/twist.h"

namespace intrinsic::icon {

// Performs cartesian impedance control using joint torques as control input to
// the robot. Uses the "complete" impedance formulation, requiring force-torque
// sensor readings to achieve consistent manipulator and task-space dynamics
// decoupling.
class CartesianImpedanceAction final
    : public intrinsic::icon::RtclActionInterface {
 public:
  CartesianImpedanceAction() = delete;

  CartesianImpedanceAction(const CartesianImpedanceAction& other) = delete;

  CartesianImpedanceAction operator=(const CartesianImpedanceAction& other) =
      delete;

  CartesianImpedanceAction(
      RealtimeSlotId arm_slot_id, RealtimeSlotId ft_slot_id,
      StreamingInputId streaming_input_id, size_t njoints, double frequency_hz,
      std::unique_ptr<intrinsic::kinematics::Skeleton> skeleton,
      CartesianImpedanceParameters cartesian_impedance_params,
      std::unique_ptr<MaximumTranslationalDistanceTracker> distance_tracker,
      std::unique_ptr<IsSettledCriterion> is_settled_criterion,
      bool use_acceleration_command_interface);

  static absl::StatusOr<std::unique_ptr<CartesianImpedanceAction>> Create(
      const CartesianImpedanceInfo::FixedParams& params_proto,
      ActionFactoryContext& context) INTRINSIC_NON_REALTIME_ONLY;

  // Perform non-realtime pre-processing steps on the received parameters, such
  // as definiteness checking and auto-derivation of missing elements.
  static absl::StatusOr<CartesianImpedanceParameters> ParseInput(
      const CartesianImpedanceInfo::FixedParams& proto_params);

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
  RealtimeStatus SetCompleteReference(
      const RealtimeSlotMap& slot_map,
      const CartesianImpedanceParameters& parameters,
      const JointStatePVA& joint_state);

  RealtimeSlotId arm_slot_id_;
  RealtimeSlotId ft_slot_id_;
  StreamingInputId streaming_input_id_;
  int njoints_;
  const double icon_frequency_hz_;

  std::unique_ptr<intrinsic::kinematics::Skeleton> skeleton_;
  std::optional<CartesianImpedanceParameters> cart_impedance_params_;
  std::unique_ptr<CartesianImpedanceController> controller_;
  ImpedanceReferenceGeneratorHolder impedance_reference_generator_holder_;
  NullspaceReferenceGeneratorHolder nullspace_reference_generator_holder_;
  std::optional<cartesian_impedance::RealTimeCartesianTarget>
      current_cartesian_target_;
  std::optional<cartesian_impedance::RealTimeNullspaceTarget>
      current_nullspace_target_;
  JointStatePVA joint_state_;
  AdmittanceStateVariables state_variables_;
  double elapsed_time_seconds_ = 0.0;
  std::optional<Pose3d> task_t_tool_start_;
  std::unique_ptr<MaximumTranslationalDistanceTracker> distance_tracker_;
  std::unique_ptr<IsSettledCriterion> is_settled_criterion_;
  bool using_acceleration_command_interface_;
  int64_t streaming_input_message_count_ = 0;
};

}  // namespace intrinsic::icon

#endif  // INTRINSIC_ICON_CONTROL_ACTIONS_CARTESIAN_IMPEDANCE_ACTION_H_
