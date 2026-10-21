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

#include "intrinsic/icon/control/actions/cartesian_admittance_action.h"

#include <cmath>
#include <cstddef>
#include <memory>
#include <optional>
#include <utility>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/actions/action_utils.h"
#include "intrinsic/icon/actions/cartesian_admittance_info.h"
#include "intrinsic/icon/actions/cartesian_impedance_parameters.h"
#include "intrinsic/icon/cc_client/condition.h"
#include "intrinsic/icon/control/action_factory_context.h"
#include "intrinsic/icon/control/actions/admittance_state_variables.h"
#include "intrinsic/icon/control/algorithms/admittance_controller_factory.h"
#include "intrinsic/icon/control/algorithms/admittance_controller_interface.h"
#include "intrinsic/icon/control/algorithms/cartesian_admittance_controller.h"
#include "intrinsic/icon/control/algorithms/cartesian_impedance_commons.h"
#include "intrinsic/icon/control/algorithms/distance_tracker.h"
#include "intrinsic/icon/control/algorithms/is_settled_criterion.h"
#include "intrinsic/icon/control/joint_position_command.h"
#include "intrinsic/icon/control/parts/feature_interfaces.h"
#include "intrinsic/icon/control/slot_types.h"
#include "intrinsic/icon/control/streaming_io_types.h"
#include "intrinsic/icon/proto/generic_part_config.pb.h"
#include "intrinsic/icon/proto/v1/types.pb.h"
#include "intrinsic/icon/testing/realtime_annotations.h"
#include "intrinsic/icon/utils/fixed_str_cat.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/icon/utils/realtime_status_macro.h"
#include "intrinsic/icon/utils/realtime_status_or.h"
#include "intrinsic/kinematics/chain.h"
#include "intrinsic/kinematics/elements.h"
#include "intrinsic/kinematics/ik/inverse_kinematics_interface.h"
#include "intrinsic/kinematics/ik_solvers/ik_solver_utils.h"
#include "intrinsic/kinematics/proto/kinematics_conversion.h"
#include "intrinsic/kinematics/skeleton.h"
#include "intrinsic/kinematics/state.h"
#include "intrinsic/kinematics/types/cart_state.h"
#include "intrinsic/kinematics/types/check_cartesian_limits.h"
#include "intrinsic/kinematics/types/joint_state.h"
#include "intrinsic/kinematics/types/to_fixed_string.h"
#include "intrinsic/math/pluecker_transform.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/twist.h"
#include "intrinsic/util/status/status_builder.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic::icon {

namespace {

constexpr double kTooLargeMeasurement = 1e6;

}  // namespace

CartesianAdmittanceAction::CartesianAdmittanceAction(
    RealtimeSlotId arm_slot_id, RealtimeSlotId ft_slot_id,
    StreamingInputId streaming_input_id, kinematics::ElementId tip_id,
    size_t njoints, double icon_frequency_hz,
    int num_controller_substeps_per_icon_cycle,
    CartesianImpedanceParameters params,
    std::unique_ptr<AdmittanceControllerInterface> controller,
    std::unique_ptr<MaximumTranslationalDistanceTracker> distance_tracker,
    std::unique_ptr<IsSettledCriterion> is_settled_criterion)
    : arm_slot_id_(arm_slot_id),
      ft_slot_id_(ft_slot_id),
      streaming_input_id_(streaming_input_id),
      njoints_(njoints),
      controller_(std::move(controller)),
      impedance_reference_generator_holder_(
          icon_frequency_hz * num_controller_substeps_per_icon_cycle),
      nullspace_reference_generator_holder_(
          njoints, icon_frequency_hz * num_controller_substeps_per_icon_cycle),
      tip_id_(tip_id),
      params_(std::move(params)),
      icon_frequency_hz_(icon_frequency_hz),
      num_controller_substeps_per_icon_cycle_(
          num_controller_substeps_per_icon_cycle),
      distance_tracker_(std::move(distance_tracker)),
      is_settled_criterion_(std::move(is_settled_criterion)) {}

/*static*/
absl::StatusOr<std::unique_ptr<CartesianAdmittanceAction>>
CartesianAdmittanceAction::Create(
    const CartesianAdmittanceInfo::FixedParams& params_proto,
    ActionFactoryContext& context) {
  INTR_ASSIGN_OR_RETURN(
      SlotInfo arm_info,
      context.GetSlotInfo(CartesianAdmittanceInfo::kArmSlotName));
  INTR_ASSIGN_OR_RETURN(
      SlotInfo ft_info,
      context.GetSlotInfo(CartesianAdmittanceInfo::kForceTorqueSlotName));
  intrinsic_proto::icon::GenericPartConfig config =
      arm_info.config.generic_config();
  INTR_ASSIGN_OR_RETURN(
      std::unique_ptr<intrinsic::kinematics::Skeleton> skeleton,
      intrinsic::kinematics::FromProto(
          config.manipulator_kinematics_config().skeleton()));
  if (!skeleton->IsNonBranchingKinematicChain()) {
    return absl::FailedPreconditionError(
        "CartesianAdmittanceAction requires a non-branching kinematic chain!");
  }
  INTR_ASSIGN_OR_RETURN(kinematics::ElementId tip_id,
                        skeleton->FindNonBranchingKinematicChainTip());
  INTR_ASSIGN_OR_RETURN(
      StreamingInputId streaming_input_id,
      (context.AddStreamingInputParser<CartesianAdmittanceInfo::FixedParams,
                                       CartesianImpedanceParameters>(
          CartesianAdmittanceInfo::kStreamingInputName,
          &CartesianAdmittanceAction::ParseInput)));

  INTR_RETURN_IF_ERROR(
      (context.AddStreamingOutputConverter<
          cartesian_impedance::AdmittanceActionStreamingOutputStatus,
          CartesianAdmittanceInfo::StreamingOutput>(
          cartesian_impedance::ToStreamingOutputStatusProto)));

  size_t njoints = config.joint_position_config().num_joints();
  if (skeleton->GetNumberDegreesOfFreedom() != njoints) {
    return FailedPreconditionErrorBuilder()
           << "Inconsistent number of joints, limits has " << njoints
           << ", kinematics has " << skeleton->GetNumberDegreesOfFreedom();
  }

  if (config.joint_position_sensor_config().num_joints() != njoints) {
    return FailedPreconditionError(
        "Inconsistent number of joints in joint position sensor.");
  }

  if (config.joint_velocity_estimator_config().num_joints() != njoints) {
    return FailedPreconditionError(
        "Inconsistent number of joints in velocity estimator.");
  }

  // A hard-coded heuristic to determine the number of controller substeps
  // executed during one ICON control cycle.
  const double kMinAdmittanceControllerFrequencyHz = 1000.0;
  const double icon_frequency_hz = context.ServerConfig().frequency_hz();
  const int num_controller_substeps_per_icon_cycle =
      std::ceil(kMinAdmittanceControllerFrequencyHz / icon_frequency_hz);
  const double effective_control_frequency_hz =
      num_controller_substeps_per_icon_cycle * icon_frequency_hz;
  LOG(INFO) << "Admittance Controller running with "
               "'num_controller_substeps_per_icon_cycle': "
            << num_controller_substeps_per_icon_cycle
            << " and effective control frequency "
            << effective_control_frequency_hz;

  INTR_ASSIGN_OR_RETURN(CartesianImpedanceParameters params,
                        ParseInput(params_proto));
  INTR_ASSIGN_OR_RETURN(auto distance_tracker,
                        MaximumTranslationalDistanceTracker::Create(
                            effective_control_frequency_hz,
                            params.state_variable_configuration
                                .time_window_for_maximum_displacement));

  INTR_ASSIGN_OR_RETURN(
      auto is_settled_criterion,
      IsSettledCriterion::Create(effective_control_frequency_hz,
                                 /*max_uncertainty_threshold=*/0.01));

  INTR_ASSIGN_OR_RETURN(
      std::unique_ptr<AdmittanceControllerInterface> controller,
      CreateAdmittanceController(
          njoints, icon_frequency_hz * num_controller_substeps_per_icon_cycle,
          params));

  return std::make_unique<CartesianAdmittanceAction>(
      arm_info.slot_id, ft_info.slot_id, streaming_input_id, tip_id, njoints,
      icon_frequency_hz, num_controller_substeps_per_icon_cycle,
      std::move(params), std::move(controller), std::move(distance_tracker),
      std::move(is_settled_criterion));
}

// static
absl::StatusOr<CartesianImpedanceParameters>
CartesianAdmittanceAction::ParseInput(
    const CartesianAdmittanceInfo::FixedParams& proto_params) {
  INTR_ASSIGN_OR_RETURN(
      CartesianImpedanceParameters new_params,
      FromProto(proto_params.cartesian_impedance_parameters()));

  // Perform non-realtime pre-processing steps on the received parameters, such
  // as definiteness checking and auto-derivation of missing elements.
  INTR_RETURN_IF_ERROR(Preprocess(new_params.cartesian_target));
  INTR_RETURN_IF_ERROR(Preprocess(new_params.nullspace_target));
  INTR_RETURN_IF_ERROR(Preprocess(new_params.state_variable_configuration));
  INTR_RETURN_IF_ERROR(Preprocess(new_params.algorithm_configuration));

  return new_params;
}

RealtimeStatus CartesianAdmittanceAction::OnEnter(OnEnterParameters params)
    INTRINSIC_CHECK_REALTIME_SAFE {
  state_variables_.Reset();
  task_t_tool_start_ = std::nullopt;
  streaming_input_message_count_ = 0;
  distance_tracker_->Clear();

  const ForceTorqueSensor* ft_sensor =
      params.slot_map.GetInterfaceForSlot<ForceTorqueSensor>(ft_slot_id_);
  if (ft_sensor == nullptr) {
    return FailedPreconditionError(
        "Slot does not have ForceTorqueSensor feature interface");
  }
  INTRINSIC_RT_ASSIGN_OR_RETURN(
      (auto [jpos, jvel_estimator, joint_limits, cart_limits, kinematics]),
      (params.slot_map.GetInterfacesForSlot<
          JointPosition, JointVelocityEstimator, JointLimitsInterface,
          CartesianLimitsInterface, ManipulatorKinematics>(arm_slot_id_)));

  INTRINSIC_RT_ASSIGN_OR_RETURN(const kinematics::Chain* chain,
                                kinematics->GetKinematicsChain(tip_id_));

  // Assemble the previously commanded joint state.
  JointStatePVA previously_commanded_joint_state;
  INTRINSIC_RT_RETURN_IF_ERROR(
      previously_commanded_joint_state.SetSize(njoints_));
  previously_commanded_joint_state.position =
      jpos->PreviousPositionSetpoints().position();
  // Set previously commanded velocity feedforward or use sensed velocity in
  // case of unavailability.
  previously_commanded_joint_state.velocity =
      jpos->PreviousPositionSetpoints().velocity_feedforward().value_or(
          jvel_estimator->GetVelocityEstimate().velocity);

  // Set previously commanded acceleration feedforward.
  previously_commanded_joint_state.acceleration =
      jpos->PreviousPositionSetpoints().acceleration_feedforward().value_or(
          eigenmath::VectorNd::Zero(njoints_));

  // Reconstruct an "initial Cartesian target" which is used to initialize the
  // Action. To avoid discontinuities, it needs to be as close to the current
  // system state as possible.
  // Tracking kinematics::State as member because of b/287941171.
  state_ = kinematics::State(chain);

  INTRINSIC_RT_RETURN_IF_ERROR(
      state_->SetStatePVA(previously_commanded_joint_state));
  INTRINSIC_RT_ASSIGN_OR_RETURN(Pose3d init_base_t_tip,
                                state_->GetTransform(chain->GetTipId()));

  INTRINSIC_RT_ASSIGN_OR_RETURN(
      auto init_rt_cartesian_target,
      cartesian_impedance::ToRealTimeCartesianTarget(
          // The following Identity pose is just a placeholder, as it will be
          // overwritten below with the current pose anyway. We want to set the
          // pose corresponding to the previous command to the
          // init_rt_cartesian_target, not the user-provided pose.
          Pose3d::Identity(), params_.cartesian_target));

  // Initialize with current pose of tool frame.
  const Pose3d init_base_t_tool =
      init_base_t_tip * init_rt_cartesian_target.robot_tip_t_robot_tool;
  init_rt_cartesian_target.task_t_tool_reference_position =
      (init_rt_cartesian_target.robot_base_t_task.inverse() * init_base_t_tool)
          .translation();
  init_rt_cartesian_target.task_t_tool_reference_orientation =
      (init_rt_cartesian_target.robot_base_t_task.inverse() * init_base_t_tool)
          .quaternion();

  INTRINSIC_RT_ASSIGN_OR_RETURN(
      eigenmath::Matrix6Nd jacobian,
      state_->ComputeJacobian(
          chain->GetTipId(),
          init_rt_cartesian_target.robot_tip_t_robot_tool.translation()));
  init_rt_cartesian_target.tool_reference_twist =
      jacobian * previously_commanded_joint_state.velocity;
  init_rt_cartesian_target.tool_reference_acceleration =
      jacobian * previously_commanded_joint_state.acceleration;

  // Read sensed wrench, compute current wrench at tool and use it as
  // reference wrench for the controller.
  Wrench filtered_wrench(ft_sensor->WrenchAtTip());
  INTRINSIC_RT_RETURN_IF_ERROR(cartesian_impedance::ApplyDeadband(
      params_.algorithm_configuration.sensed_wrench_deadband, filtered_wrench));
  eigenmath::Matrix6d tool_F_tip = PlueckerForceTransformInverse(
      init_rt_cartesian_target.robot_tip_t_robot_tool);
  Wrench sensed_wrench_at_tool_in_tool_frame(tool_F_tip * filtered_wrench);
  // Rotate the sensed wrench into the base frame.
  init_rt_cartesian_target.tool_reference_wrench =
      cartesian_impedance::RotateCartesianVector(
          init_rt_cartesian_target.robot_base_t_task.quaternion().inverse() *
              init_base_t_tool.quaternion(),
          sensed_wrench_at_tool_in_tool_frame);

  controller_->SetCartesianTarget(init_rt_cartesian_target);

  // Construct initial nullspace target for redundant robots, overwrite target
  // position with previously commanded position.
  if (njoints_ > 6) {
    INTRINSIC_RT_ASSIGN_OR_RETURN(
        auto init_nullspace_target,
        cartesian_impedance::ToRealTimeNullspaceTarget(
            params_.nullspace_target, previously_commanded_joint_state));
    init_nullspace_target.joint_state = previously_commanded_joint_state;
    controller_->SetNullspaceTarget(init_nullspace_target);
  }

  // SetCompleteReference() will populate the reference generators with the
  // actually commanded parameters. Subsequently, the Action will interpolate
  // between the above set "initial" parameters and the commanded parameters.
  INTRINSIC_RT_RETURN_IF_ERROR(
      SetCompleteReference(*joint_limits, *cart_limits, params_,
                           previously_commanded_joint_state, init_base_t_tip));

  INTRINSIC_RT_RETURN_IF_ERROR(is_settled_criterion_->Initialize());

  // Reset current targets and controller internal state.
  return controller_->ResetInternalState(
      init_base_t_tool, init_rt_cartesian_target.tool_reference_twist,
      init_rt_cartesian_target.tool_reference_acceleration,
      previously_commanded_joint_state);
}

RealtimeStatus CartesianAdmittanceAction::SetCompleteReference(
    const JointLimitsInterface& joint_limits_interface,
    const CartesianLimitsInterface& cart_limits_interface,
    const CartesianImpedanceParameters& parameters,
    const JointStatePVA& joint_state_last_commanded,
    const Pose3d& base_t_tip_last_commanded) {
  if (!controller_) {
    return FailedPreconditionError(
        "No controller set in CartesianAdmittanceAction.");
  }

  // Set configuration, which does not require separate extraction steps.
  controller_->SetAlgorithmConfiguration(parameters.algorithm_configuration);
  state_variables_.SetStateVariableConfig(
      parameters.state_variable_configuration);
  state_variable_config_ = parameters.state_variable_configuration;

  // Extract and set task constraints.
  INTRINSIC_RT_ASSIGN_OR_RETURN(
      auto rt_constraints,
      cartesian_impedance::ToRealTimeConstraints(
          parameters.constraints, joint_limits_interface.GetSystemLimits(),
          cart_limits_interface.GetDefaultCartesianLimits(), njoints_));

  INTRINSIC_RT_RETURN_IF_ERROR(controller_->SetConstraints(rt_constraints));

  // Extract, check and set Cartesian impedance target.
  INTRINSIC_RT_ASSIGN_OR_RETURN(
      auto rt_cartesian_target,
      cartesian_impedance::ToRealTimeCartesianTarget(
          base_t_tip_last_commanded, parameters.cartesian_target));
  CartStatePV target_to_check;
  target_to_check.pose =
      rt_cartesian_target.robot_base_t_task *
      Pose3d(rt_cartesian_target.task_t_tool_reference_orientation,
             rt_cartesian_target.task_t_tool_reference_position);
  target_to_check.velocity = cartesian_impedance::RotateCartesianVector(
      rt_cartesian_target.robot_base_t_task.quaternion(),
      rt_cartesian_target.tool_reference_twist);
  if (!IsWithinLimits(target_to_check, rt_constraints.cart_limits)) {
    return icon::FailedPreconditionError(
        "Cartesian goal violates Cartesian limits.");
  }

  // Configure impedance reference generator and set new impedance reference.
  INTRINSIC_RT_RETURN_IF_ERROR(
      impedance_reference_generator_holder_.ConfigureReferenceGenerator(
          parameters.reference_generators.cartesian_reference_generator));
  INTRINSIC_RT_RETURN_IF_ERROR(
      impedance_reference_generator_holder_.GetActiveReferenceGenerator()
          .SetReference(rt_cartesian_target, rt_constraints.cart_limits));

  // Extract and set nullspace impedance target for redundant robots.
  if (njoints_ > 6) {
    INTRINSIC_RT_ASSIGN_OR_RETURN(
        auto rt_nullspace_target,
        cartesian_impedance::ToRealTimeNullspaceTarget(
            parameters.nullspace_target, joint_state_last_commanded));

    // Configure nullspace reference generator and set new nullspace
    // reference.
    INTRINSIC_RT_RETURN_IF_ERROR(
        nullspace_reference_generator_holder_.ConfigureReferenceGenerator(
            parameters.reference_generators.nullspace_reference_generator));
    INTRINSIC_RT_RETURN_IF_ERROR(
        nullspace_reference_generator_holder_.GetActiveReferenceGenerator()
            .SetReference(rt_nullspace_target, rt_constraints.joint_limits));
  }

  return OkStatus();
}

// Sense logic for substepping at higher control frequency.
RealtimeStatus CartesianAdmittanceAction::SenseSubStep(
    const JointPositionSensor& jpos_sensor,
    const JointVelocityEstimator& jvel_estimator,
    const JointLimitsInterface& joint_limits,
    const CartesianLimitsInterface& cart_limits,
    const ManipulatorKinematics& kinematics, const ForceTorqueSensor& ft_sensor,
    const JointStatePVA& previous_joint_command, double speed_override) {
  // Propagate the cartesian impedance target generator using the currently
  // set cartesian target.
  if (!controller_->GetCartesianTarget().has_value()) {
    return InternalError(
        "No Cartesian target available in controller. Initialize controller "
        "first.");
  }
  // Use the admittance controller's internal nominal motion reference as
  // `cart_state_sensed` for the reference generator. This is legitimate, as
  // the control error is near zero for high-stiffness position-controlled
  // robots.
  CartStatePVA cart_state_nominal;
  cart_state_nominal.pose = controller_->GetRobotTaskToTool();
  cart_state_nominal.velocity = controller_->GetTwist();
  INTRINSIC_RT_ASSIGN_OR_RETURN(
      cartesian_impedance::RealTimeCartesianTarget cartesian_target,
      impedance_reference_generator_holder_.GetActiveReferenceGenerator()
          .Evaluate(*controller_->GetCartesianTarget(),
                    /*cart_state_sensed=*/cart_state_nominal,
                    /*speed_override=*/speed_override));
  controller_->SetCartesianTarget(cartesian_target);

  // Propagate the nullspace target motion generator using the currently set
  // nullspace target, if we have a redundant robot. Otherwise, skip and save
  // computation time.
  if (njoints_ > 6) {
    if (!controller_->GetNullspaceTarget().has_value()) {
      return InternalError(
          "No nullspace target available in controller. Initialize "
          "controller first.");
    }
    INTRINSIC_RT_ASSIGN_OR_RETURN(
        cartesian_impedance::RealTimeNullspaceTarget nullspace_target,
        nullspace_reference_generator_holder_.GetActiveReferenceGenerator()
            .Evaluate(*controller_->GetNullspaceTarget(),
                      /*speed_override=*/speed_override));
    controller_->SetNullspaceTarget(nullspace_target);
  }

  const Wrench post_sensor_dynamic_load_at_tip =
      ft_sensor.PostSensorDynamicLoadAtTip();
  if (post_sensor_dynamic_load_at_tip.cwiseAbs().maxCoeff() >
          kTooLargeMeasurement ||
      post_sensor_dynamic_load_at_tip.hasNaN()) {
    return InternalError(FixedStrCat<RealtimeStatus::kMaxMessageLength>(
        "post_sensor_dynamic_load_in_tip_frame out of bound, got ",
        eigenmath::ToFixedString(post_sensor_dynamic_load_at_tip.transpose())));
  }

  INTRINSIC_RT_ASSIGN_OR_RETURN(const kinematics::Chain* chain,
                                kinematics.GetKinematicsChain(tip_id_));
  INTRINSIC_RT_RETURN_IF_ERROR(controller_->PrepareControl(
      chain->GetTipId(), ft_sensor.WrenchAtTip(),
      post_sensor_dynamic_load_at_tip, state_.value()));

  // Evaluate state variables.
  const auto& reference_generator_target =
      impedance_reference_generator_holder_.GetActiveReferenceGenerator()
          .GetReference();
  const Pose3d current_task_t_tool = controller_->GetRobotTaskToTool();
  const Pose3d task_t_tool_reference =
      Pose3d(reference_generator_target.task_t_tool_reference_orientation,
             reference_generator_target.task_t_tool_reference_position);
  // IsDone is remapped to reference_pose_reached for simplicity.
  const bool is_done = current_task_t_tool.isApprox(
      task_t_tool_reference, state_variable_config_.position_error_threshold,
      state_variable_config_.orientation_error_threshold);
  INTRINSIC_RT_ASSIGN_OR_RETURN(const eigenmath::Matrix6Nd jacobian,
                                controller_->GetJacobian());

  const double elapsed_time_seconds =
      state_variables_.GetElapsedTimeSeconds() +
      1. / (icon_frequency_hz_ * num_controller_substeps_per_icon_cycle_);

  if (!task_t_tool_start_.has_value()) {
    task_t_tool_start_ = controller_->GetRobotTaskToTool();
  }
  const double translational_distance_traveled =
      cartesian_impedance::ComputeTranslationalDistanceTraveled(
          *task_t_tool_start_, controller_->GetRobotTaskToTool(),
          state_variable_config_.translational_distance_along);

  cartesian_impedance::CartesianErrorState cntrl_error_state =
      controller_->GetCartesianErrorState();

  bool force_reached = cntrl_error_state.wrench.head<3>().norm() <
                       state_variable_config_.force_settled_band;

  INTRINSIC_RT_ASSIGN_OR_RETURN(
      const bool is_settled,
      is_settled_criterion_->Update(
          jvel_estimator.GetVelocityEstimate().velocity,
          previous_joint_command.velocity, force_reached));
  distance_tracker_->Add(controller_->GetRobotTaskToTool().translation());
  state_variables_.Update(
      {.current_task_t_tool = current_task_t_tool,
       .reference_task_t_tool = task_t_tool_reference,
       .sensed_wrench = controller_->GetSensedWrenchAtToolInTaskFrame(),
       .manipulability = kinematics::ComputeManipulability(jacobian),
       .elapsed_time_seconds = elapsed_time_seconds,
       .translational_distance_traveled_m = translational_distance_traveled,
       .maximum_translational_displacement_in_window_m =
           distance_tracker_->GetMaximumDistance(),
       .is_done = is_done,
       .is_settled = is_settled,
       .settled_for_seconds = controller_->UpdateSettlingTime(
           jvel_estimator.GetVelocityEstimate(),
           state_variable_config_.translational_velocity_error_threshold,
           state_variable_config_.angular_velocity_error_threshold,
           state_variable_config_.joint_velocity_threshold),
       .streaming_input_message_count = streaming_input_message_count_});

  return OkStatus();
}

RealtimeStatus CartesianAdmittanceAction::Sense(SenseParameters params) {
  if (!state_.has_value()) {
    return FailedPreconditionError("Kinematics state not initialized.");
  }

  INTRINSIC_RT_ASSIGN_OR_RETURN(
      (auto [jpos, jpos_sensor, jvel_estimator, joint_limits, cart_limits,
             kinematics]),
      (params.slot_map.GetInterfacesForSlot<
          JointPosition, JointPositionSensor, JointVelocityEstimator,
          JointLimitsInterface, CartesianLimitsInterface,
          ManipulatorKinematics>(arm_slot_id_)));
  const ForceTorqueSensor* ft_sensor =
      params.slot_map.GetInterfaceForSlot<ForceTorqueSensor>(ft_slot_id_);
  if (ft_sensor == nullptr) {
    return FailedPreconditionError(
        "Slot does not have ForceTorqueSensor feature interface");
  }

  // Update setpoints if received new goals from streaming input.
  INTRINSIC_RT_ASSIGN_OR_RETURN(
      const intrinsic::icon::CartesianImpedanceParameters* streaming_parameters,
      params.streaming_io_access.PollInput<CartesianImpedanceParameters>(
          streaming_input_id_));
  if (streaming_parameters != nullptr) {
    ++streaming_input_message_count_;
    // New controller goal received, run real-time pre-processing steps and set.
    // We can receive previous joint state and base_t_tip from the controller
    // because it has either been updated in the control cycle or set in
    // ResetInternalState().
    INTRINSIC_RT_RETURN_IF_ERROR(SetCompleteReference(
        *joint_limits, *cart_limits, *streaming_parameters,
        controller_->GetJointSpaceMotionReference(),
        /*base_t_tip_last_commanded=*/
        controller_->GetCartesianTarget()->robot_base_t_task *
            controller_->GetRobotTaskToTip()));
  }

  JointStatePVA previous_joint_command;
  INTRINSIC_RT_RETURN_IF_ERROR(previous_joint_command.SetSize(njoints_));
  previous_joint_command.position =
      jpos->PreviousPositionSetpoints().position();
  previous_joint_command.velocity =
      jpos->PreviousPositionSetpoints().velocity_feedforward().value_or(
          jvel_estimator->GetVelocityEstimate().velocity);
  previous_joint_command.acceleration =
      jpos->PreviousPositionSetpoints().acceleration_feedforward().value_or(
          eigenmath::VectorNd::Zero(njoints_));

  // Run one Sense() substep - remaining substeps to be called in Control().
  INTRINSIC_RT_RETURN_IF_ERROR(SenseSubStep(
      *jpos_sensor, *jvel_estimator, *joint_limits, *cart_limits, *kinematics,
      *ft_sensor, previous_joint_command, params.speed_override));

  const std::optional<cartesian_impedance::RealTimeCartesianTarget>&
      cartesian_target = controller_->GetCartesianTarget();
  if (cartesian_target == std::nullopt) {
    return FailedPreconditionError("Cartesian target not initialized.");
  }

  const Pose3d task_t_base = cartesian_target->robot_base_t_task.inverse();

  cartesian_impedance::CartesianErrorState cntrl_error_state_in_task_frame =
      cartesian_impedance::RotateCartesianErrorState(
          task_t_base.quaternion(), controller_->GetCartesianErrorState());

  // Write data into streaming output struct.
  cartesian_impedance::AdmittanceActionStreamingOutputStatus streaming_output;

  Eigen::Map<eigenmath::Vector3d>(streaming_output.error_state_position) =
      cntrl_error_state_in_task_frame.pose.head<3>();
  Eigen::Map<eigenmath::Vector3d>(streaming_output.error_state_orientation) =
      cntrl_error_state_in_task_frame.pose.tail<3>();
  Eigen::Map<eigenmath::Vector3d>(
      streaming_output.error_state_translational_velocity) =
      cntrl_error_state_in_task_frame.twist.head<3>();
  Eigen::Map<eigenmath::Vector3d>(
      streaming_output.error_state_angular_velocity) =
      cntrl_error_state_in_task_frame.twist.tail<3>();
  Eigen::Map<eigenmath::Vector3d>(streaming_output.error_state_force) =
      cntrl_error_state_in_task_frame.wrench.head<3>();
  Eigen::Map<eigenmath::Vector3d>(streaming_output.error_state_torque) =
      cntrl_error_state_in_task_frame.wrench.tail<3>();
  Eigen::Map<eigenmath::Vector3d>(
      streaming_output.previous_cartesian_translational_acceleration_command) =
      controller_->GetCartesianAccelerationCommand().head<3>();
  Eigen::Map<eigenmath::Vector3d>(
      streaming_output.previous_cartesian_angular_acceleration_command) =
      controller_->GetCartesianAccelerationCommand().tail<3>();
  Eigen::Map<eigenmath::Vector3d>(
      streaming_output.sensed_force_at_tool_in_task_frame) =
      controller_->GetSensedWrenchAtToolInTaskFrame().head<3>();
  Eigen::Map<eigenmath::Vector3d>(
      streaming_output.sensed_torque_at_tool_in_task_frame) =
      controller_->GetSensedWrenchAtToolInTaskFrame().tail<3>();
  Eigen::Map<eigenmath::Vector3d>(
      streaming_output.post_sensor_dynamics_force_in_task_frame) =
      controller_->GetPostSensorDynamicLoadInTaskFrame().head<3>();
  Eigen::Map<eigenmath::Vector3d>(
      streaming_output.post_sensor_dynamics_torque_in_task_frame) =
      controller_->GetPostSensorDynamicLoadInTaskFrame().tail<3>();
  streaming_output.settled_for_seconds =
      *state_variables_.GetSettledForSeconds();
  streaming_output.sensed_force_magnitude = *state_variables_.GetSensedForce();
  streaming_output.sensed_torque_magnitude =
      *state_variables_.GetSensedTorque();
  streaming_output.translational_distance_traveled =
      *state_variables_.GetTranslationalDistanceTraveled();
  streaming_output.maximum_translational_displacement_in_time_window =
      *state_variables_.GetMaximumTranslationalDisplacementInWindow();
  streaming_output.is_settled = state_variables_.GetIsSettled();

  // Write reference pose and twist.

  Eigen::Map<eigenmath::Vector7d>(streaming_output.robot_tip_t_robot_tool);
  cartesian_impedance::CopyPoseToDoubles(
      cartesian_target->robot_tip_t_robot_tool,
      streaming_output.robot_tip_t_robot_tool);
  Eigen::Map<eigenmath::Vector7d>(streaming_output.robot_base_t_task);
  cartesian_impedance::CopyPoseToDoubles(cartesian_target->robot_base_t_task,
                                         streaming_output.robot_base_t_task);
  Eigen::Map<eigenmath::Vector7d>(streaming_output.task_t_tool_reference_pose) =
      eigenmath::Vector7d(
          cartesian_target->task_t_tool_reference_position.x(),
          cartesian_target->task_t_tool_reference_position.y(),
          cartesian_target->task_t_tool_reference_position.z(),
          cartesian_target->task_t_tool_reference_orientation.w(),
          cartesian_target->task_t_tool_reference_orientation.x(),
          cartesian_target->task_t_tool_reference_orientation.y(),
          cartesian_target->task_t_tool_reference_orientation.z());

  Eigen::Map<eigenmath::Vector6d>(streaming_output.tool_reference_twist) =
      cartesian_target->tool_reference_twist;
  Eigen::Map<eigenmath::Vector6d>(streaming_output.tool_reference_wrench) =
      cartesian_target->tool_reference_wrench;

  // Write streaming output.
  return params.streaming_io_access.WriteOutput(streaming_output);
}

RealtimeStatusOr<JointStatePVA> CartesianAdmittanceAction::ControlSubStep(
    const kinematics::InverseKinematicsInterface& ik,
    const ForceTorqueSensor& ft_sensor) {
  return controller_->ComputeControl(&ik, ft_sensor.WrenchAtTip());
}

RealtimeStatus CartesianAdmittanceAction::Control(ControlParameters params) {
  INTRINSIC_RT_ASSIGN_OR_RETURN(
      (auto [jpos_sensor, jvel_estimator, joint_limits, cart_limits,
             kinematics]),
      (params.slot_map.GetInterfacesForSlot<
          JointPositionSensor, JointVelocityEstimator, JointLimitsInterface,
          CartesianLimitsInterface, ManipulatorKinematics>(arm_slot_id_)));
  const ForceTorqueSensor* ft_sensor =
      params.slot_map.GetInterfaceForSlot<ForceTorqueSensor>(ft_slot_id_);
  if (ft_sensor == nullptr) {
    return FailedPreconditionError(
        "Slot does not have ForceTorqueSensor feature interface");
  }

  const auto& ik = kinematics->GetInverseKinematicsSolver();
  INTRINSIC_RT_ASSIGN_OR_RETURN(JointStatePVA joint_command,
                                ControlSubStep(ik, *ft_sensor));

  // Run additional substeps. We run "num_controller_substeps - 1" cycles here
  // because the first substep was in Sense().
  for (int substep = 0; substep < num_controller_substeps_per_icon_cycle_ - 1;
       substep++) {
    INTRINSIC_RT_RETURN_IF_ERROR(SenseSubStep(
        *jpos_sensor, *jvel_estimator, *joint_limits, *cart_limits, *kinematics,
        *ft_sensor, joint_command, params.speed_override));

    INTRINSIC_RT_ASSIGN_OR_RETURN(joint_command,
                                  ControlSubStep(ik, *ft_sensor));
  }

  INTRINSIC_RT_ASSIGN_OR_RETURN(
      (auto [jpos]),
      (params.slot_map.GetMutableInterfacesForSlot<JointPosition>(
          arm_slot_id_)));

  INTRINSIC_RT_ASSIGN_OR_RETURN(
      const auto setpoints, JointPositionCommand::Create(
                                joint_command.position, joint_command.velocity,
                                joint_command.acceleration));
  INTRINSIC_RT_RETURN_IF_ERROR(jpos->SetPositionSetpoints(setpoints));

  ProcessWrenchAtEndeffector* process_wrench_at_tip =
      params.slot_map.GetMutableInterfaceForSlot<ProcessWrenchAtEndeffector>(
          arm_slot_id_);

  // Write sensed wrench to WrenchAtEndeffector feature interface.
  if (process_wrench_at_tip != nullptr && ft_sensor != nullptr) {
    // Set the process wrench at the tip, such that it can be taken into account
    // by the ROEM's own dynamics.
    process_wrench_at_tip->SetProcessWrenchAtTip(ft_sensor->WrenchAtTip());
  }

  return OkStatus();
}

RealtimeStatusOr<StateVariableValue>
CartesianAdmittanceAction::GetStateVariable(absl::string_view name) const {
  return state_variables_.GetStateVariable(name);
}

intrinsic_proto::icon::v1::ActionSignature
CartesianAdmittanceAction::GetSignature() {
  ActionSignatureBuilder builder(CartesianAdmittanceInfo::kActionTypeName,
                                 CartesianAdmittanceInfo::kActionDescription);
  CHECK_OK(
      builder.SetFixedParametersType<CartesianAdmittanceInfo::FixedParams>());
  CHECK_OK(
      builder.AddPartSlot(CartesianAdmittanceInfo::kArmSlotName,
                          CartesianAdmittanceInfo::kArmSlotDescription,
                          /*required_feature_interfaces=*/
                          {
                              intrinsic_proto::icon::v1::FeatureInterfaceTypes::
                                  FEATURE_INTERFACE_JOINT_POSITION,
                              intrinsic_proto::icon::v1::FeatureInterfaceTypes::
                                  FEATURE_INTERFACE_JOINT_POSITION_SENSOR,
                              intrinsic_proto::icon::v1::FeatureInterfaceTypes::
                                  FEATURE_INTERFACE_JOINT_VELOCITY_ESTIMATOR,
                              intrinsic_proto::icon::v1::FeatureInterfaceTypes::
                                  FEATURE_INTERFACE_CARTESIAN_LIMITS,
                              intrinsic_proto::icon::v1::FeatureInterfaceTypes::
                                  FEATURE_INTERFACE_JOINT_LIMITS,
                              intrinsic_proto::icon::v1::FeatureInterfaceTypes::
                                  FEATURE_INTERFACE_MANIPULATOR_KINEMATICS,
                          }));
  CHECK_OK(
      builder.AddPartSlot(CartesianAdmittanceInfo::kForceTorqueSlotName,
                          CartesianAdmittanceInfo::kForceTorqueSlotDescription,
                          /*required_feature_interfaces=*/
                          {
                              intrinsic_proto::icon::v1::FeatureInterfaceTypes::
                                  FEATURE_INTERFACE_FORCE_TORQUE_SENSOR,
                          }));
  CHECK_OK(AdmittanceStateVariables::RegisterVariables(builder));
  CHECK_OK(builder.AddStreamingInput<CartesianAdmittanceInfo::FixedParams>(
      CartesianAdmittanceInfo::kStreamingInputName,
      CartesianAdmittanceInfo::kStreamingInputDescription));
  CHECK_OK(builder.AddStreamingOutput<CartesianAdmittanceInfo::StreamingOutput>(
      CartesianAdmittanceInfo::kStreamingOutputName,
      CartesianAdmittanceInfo::kStreamingOutputDescription));
  return builder.Finish();
}

}  // namespace intrinsic::icon
