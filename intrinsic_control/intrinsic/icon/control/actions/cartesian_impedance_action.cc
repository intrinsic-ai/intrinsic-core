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

#include "intrinsic/icon/control/actions/cartesian_impedance_action.h"

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
#include "intrinsic/icon/actions/cartesian_admittance_action.pb.h"
#include "intrinsic/icon/actions/cartesian_impedance_info.h"
#include "intrinsic/icon/actions/cartesian_impedance_parameters.h"
#include "intrinsic/icon/cc_client/condition.h"
#include "intrinsic/icon/control/action_factory_context.h"
#include "intrinsic/icon/control/actions/admittance_state_variables.h"
#include "intrinsic/icon/control/algorithms/cartesian_impedance_commons.h"
#include "intrinsic/icon/control/algorithms/cartesian_impedance_controller.h"
#include "intrinsic/icon/control/algorithms/cartesian_impedance_reference_generator_interface.h"
#include "intrinsic/icon/control/algorithms/distance_tracker.h"
#include "intrinsic/icon/control/algorithms/is_settled_criterion.h"
#include "intrinsic/icon/control/parts/feature_interfaces.h"
#include "intrinsic/icon/control/realtime_slot_map.h"
#include "intrinsic/icon/control/slot_types.h"
#include "intrinsic/icon/control/streaming_io_types.h"
#include "intrinsic/icon/proto/generic_part_config.pb.h"
#include "intrinsic/icon/proto/v1/types.pb.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/icon/utils/realtime_status_macro.h"
#include "intrinsic/icon/utils/realtime_status_or.h"
#include "intrinsic/kinematics/ik_solvers/ik_solver_utils.h"
#include "intrinsic/kinematics/proto/kinematics_conversion.h"
#include "intrinsic/kinematics/skeleton.h"
#include "intrinsic/kinematics/state.h"
#include "intrinsic/kinematics/types/cart_state.h"
#include "intrinsic/kinematics/types/joint_state.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/twist.h"
#include "intrinsic/util/status/status_builder.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic::icon {
namespace {

absl::Status Preprocess(CartesianImpedanceParameters& params) {
  INTR_RETURN_IF_ERROR(
      cartesian_impedance::Preprocess(params.cartesian_target));
  INTR_RETURN_IF_ERROR(
      cartesian_impedance::Preprocess(params.nullspace_target));
  INTR_RETURN_IF_ERROR(
      cartesian_impedance::Preprocess(params.algorithm_configuration));
  INTR_RETURN_IF_ERROR(
      cartesian_impedance::Preprocess(params.state_variable_configuration));
  return OkStatus();
}

}  // namespace

CartesianImpedanceAction::CartesianImpedanceAction(
    RealtimeSlotId arm_slot_id, RealtimeSlotId ft_slot_id,
    StreamingInputId streaming_input_id, size_t njoints, double frequency_hz,
    std::unique_ptr<intrinsic::kinematics::Skeleton> skeleton,
    CartesianImpedanceParameters params,
    std::unique_ptr<MaximumTranslationalDistanceTracker> distance_tracker,
    std::unique_ptr<IsSettledCriterion> is_settled_criterion,
    bool use_acceleration_command_interface)
    : arm_slot_id_(arm_slot_id),
      ft_slot_id_(ft_slot_id),
      streaming_input_id_(streaming_input_id),
      njoints_(njoints),
      icon_frequency_hz_(frequency_hz),
      skeleton_(std::move(skeleton)),
      cart_impedance_params_(std::move(params)),
      controller_(
          CartesianImpedanceController::Create(*skeleton_, icon_frequency_hz_)
              .value()),
      impedance_reference_generator_holder_(frequency_hz),
      nullspace_reference_generator_holder_(njoints, frequency_hz),
      distance_tracker_(std::move(distance_tracker)),
      is_settled_criterion_(std::move(is_settled_criterion)),
      using_acceleration_command_interface_(
          use_acceleration_command_interface) {
  CHECK_OK(joint_state_.SetSize(njoints));
}

/*static*/
absl::StatusOr<std::unique_ptr<CartesianImpedanceAction>>
CartesianImpedanceAction::Create(
    const CartesianImpedanceInfo::FixedParams& params_proto,
    ActionFactoryContext& context) {
  INTR_ASSIGN_OR_RETURN(
      SlotInfo arm_info,
      context.GetSlotInfo(CartesianImpedanceInfo::kArmSlotName));
  INTR_ASSIGN_OR_RETURN(
      SlotInfo ft_info,
      context.GetSlotInfo(CartesianImpedanceInfo::kForceTorqueSlotName));

  // We ensure that we have at least one valid command interface. This is
  // necessary because the command interfaces are both optional interfaces.
  const absl::flat_hash_set<int> supported_interfaces = {
      arm_info.config.feature_interfaces().begin(),
      arm_info.config.feature_interfaces().end()};
  bool use_acceleration_command_interface;
  // Preferentially use a joint acceleration interface since it will imply
  // better inverse dynamics modeling on the robot controller.
  if (supported_interfaces.contains(
          intrinsic_proto::icon::v1::FeatureInterfaceTypes::
              FEATURE_INTERFACE_JOINT_ACCELERATION)) {
    use_acceleration_command_interface = true;
    LOG(INFO) << "The Cartesian Impedance action is using the joint "
                 "acceleration command interface.";
  } else if (supported_interfaces.contains(
                 intrinsic_proto::icon::v1::FeatureInterfaceTypes::
                     FEATURE_INTERFACE_JOINT_TORQUE)) {
    use_acceleration_command_interface = false;
    LOG(INFO) << "The Cartesian Impedance action is using the joint "
                 "torque command interface.";
  } else {
    return absl::FailedPreconditionError(
        "CartesianImpedanceAction requires one of the JointAcceleration or "
        "JointTorque feature interfaces in the arm part.");
  }

  INTR_ASSIGN_OR_RETURN(
      StreamingInputId streaming_input_id,
      (context.AddStreamingInputParser<CartesianImpedanceInfo::FixedParams,
                                       CartesianImpedanceParameters>(
          CartesianImpedanceInfo::kStreamingInputName,
          &CartesianImpedanceAction::ParseInput)));

  INTR_RETURN_IF_ERROR(
      (context.AddStreamingOutputConverter<
          cartesian_impedance::AdmittanceActionStreamingOutputStatus,
          CartesianImpedanceInfo::StreamingOutput>(
          cartesian_impedance::ToStreamingOutputStatusProto)));

  const intrinsic_proto::icon::GenericPartConfig& config =
      arm_info.config.generic_config();

  INTR_ASSIGN_OR_RETURN(
      CartesianImpedanceParameters params,
      FromProto(params_proto.cartesian_impedance_parameters()));

  INTR_RETURN_IF_ERROR(Preprocess(params));

  const size_t njoints = config.joint_position_sensor_config().num_joints();

  const double icon_frequency_hz = context.ServerConfig().frequency_hz();

  INTR_ASSIGN_OR_RETURN(
      std::unique_ptr<intrinsic::kinematics::Skeleton> skeleton,
      intrinsic::kinematics::FromProto(
          config.manipulator_kinematics_config().skeleton()));

  INTR_ASSIGN_OR_RETURN(
      auto distance_tracker,
      MaximumTranslationalDistanceTracker::Create(
          icon_frequency_hz, params.state_variable_configuration
                                 .time_window_for_maximum_displacement));

  INTR_ASSIGN_OR_RETURN(
      auto is_settled_criterion,
      IsSettledCriterion::Create(icon_frequency_hz,
                                 /*max_uncertainty_threshold=*/0.01));

  if (!skeleton->IsNonBranchingKinematicChain()) {
    return absl::FailedPreconditionError(
        "CartesianImpedanceAction requires a non-branching kinematic chain!");
  }

  if (skeleton->GetNumberDegreesOfFreedom() != njoints) {
    return FailedPreconditionErrorBuilder()
           << "Inconsistent number of joints, torque interface has " << njoints
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

  return std::make_unique<CartesianImpedanceAction>(
      arm_info.slot_id, ft_info.slot_id, streaming_input_id, njoints,
      icon_frequency_hz, std::move(skeleton), std::move(params),
      std::move(distance_tracker), std::move(is_settled_criterion),
      use_acceleration_command_interface);
}

// static
absl::StatusOr<CartesianImpedanceParameters>
CartesianImpedanceAction::ParseInput(
    const CartesianImpedanceInfo::FixedParams& proto_params) {
  INTR_ASSIGN_OR_RETURN(
      CartesianImpedanceParameters new_params,
      FromProto(proto_params.cartesian_impedance_parameters()));

  INTR_RETURN_IF_ERROR(Preprocess(new_params));

  return new_params;
}

RealtimeStatus CartesianImpedanceAction::SetCompleteReference(
    const RealtimeSlotMap& slot_map,
    const CartesianImpedanceParameters& parameters,
    const JointStatePVA& joint_state) {
  if (!controller_) {
    return FailedPreconditionError(
        "No controller set in CartesianImpedanceAction.");
  }

  INTRINSIC_RT_ASSIGN_OR_RETURN(
      (auto [joint_limits, cart_limits]),
      (slot_map.GetInterfacesForSlot<JointLimitsInterface,
                                     CartesianLimitsInterface>(arm_slot_id_)));

  // Set configuration, which does not require separate extraction steps.
  controller_->SetAlgorithmConfiguration(parameters.algorithm_configuration);
  state_variables_.SetStateVariableConfig(
      parameters.state_variable_configuration);

  // Extract and set task constraints.
  INTRINSIC_RT_ASSIGN_OR_RETURN(
      auto rt_constraints,
      cartesian_impedance::ToRealTimeConstraints(
          parameters.constraints, joint_limits->GetSystemLimits(),
          cart_limits->GetDefaultCartesianLimits(), njoints_));
  controller_->SetConstraints(rt_constraints);

  auto& kinematics = controller_->GetKinematicsState();
  INTRINSIC_RT_RETURN_IF_ERROR(kinematics.SetStatePVA(joint_state));
  INTRINSIC_RT_ASSIGN_OR_RETURN(
      Pose3d base_t_tip, kinematics.GetTransform(controller_->GetTipFrameId()));

  // Extract, check and set cartesian impedance target.
  INTRINSIC_RT_ASSIGN_OR_RETURN(auto rt_cartesian_target,
                                cartesian_impedance::ToRealTimeCartesianTarget(
                                    base_t_tip, parameters.cartesian_target));
  // Configure impedance reference generator and set new impedance reference.
  INTRINSIC_RT_RETURN_IF_ERROR(
      impedance_reference_generator_holder_.ConfigureReferenceGenerator(
          parameters.reference_generators.cartesian_reference_generator));
  INTRINSIC_RT_RETURN_IF_ERROR(
      impedance_reference_generator_holder_.GetActiveReferenceGenerator()
          .SetReference(rt_cartesian_target, rt_constraints.cart_limits));

  // If the current Cartesian target is set for the first time or has been
  // reset, we initialize it with the same parameters as the new target
  // parameters, but with the current end effector pose.
  if (!current_cartesian_target_.has_value()) {
    cartesian_impedance::CartesianTarget cartesian_target_without_pose =
        parameters.cartesian_target;
    cartesian_target_without_pose.task_t_tool_reference_pose.reset();
    INTRINSIC_RT_ASSIGN_OR_RETURN(
        current_cartesian_target_,
        cartesian_impedance::ToRealTimeCartesianTarget(
            base_t_tip, cartesian_target_without_pose));
    controller_->SetCartesianTarget(*current_cartesian_target_);
  }

  // Extract and set nullspace impedance target for redundant robots.
  if (njoints_ > 6) {
    INTRINSIC_RT_ASSIGN_OR_RETURN(
        auto rt_nullspace_target,
        cartesian_impedance::ToRealTimeNullspaceTarget(
            parameters.nullspace_target, joint_state));

    // Configure nullspace reference generator and set new nullspace reference.
    INTRINSIC_RT_RETURN_IF_ERROR(
        nullspace_reference_generator_holder_.ConfigureReferenceGenerator(
            parameters.reference_generators.nullspace_reference_generator));
    INTRINSIC_RT_RETURN_IF_ERROR(
        nullspace_reference_generator_holder_.GetActiveReferenceGenerator()
            .SetReference(rt_nullspace_target, rt_constraints.joint_limits));

    if (!current_nullspace_target_.has_value()) {
      // Current nullspace target is not set, initialize it with current joint
      // state and new target stiffness and damping.
      current_nullspace_target_ = rt_nullspace_target;
      current_nullspace_target_->joint_state = joint_state;
      controller_->SetNullspaceTarget(*current_nullspace_target_);
    }
  }

  return OkStatus();
}

RealtimeStatus CartesianImpedanceAction::OnEnter(OnEnterParameters params) {
  state_variables_.Reset();
  elapsed_time_seconds_ = 0.0;
  task_t_tool_start_ = std::nullopt;
  streaming_input_message_count_ = 0;
  distance_tracker_->Clear();

  if (!cart_impedance_params_.has_value()) {
    return FailedPreconditionError(
        "No parameters set for CartesianImpedanceAction.");
  }

  INTRINSIC_RT_ASSIGN_OR_RETURN(
      (auto [jpos_sensor, jvel_estimator]),
      (params.slot_map
           .GetInterfacesForSlot<JointPositionSensor, JointVelocityEstimator>(
               arm_slot_id_)));

  // Assemble current joint state.
  joint_state_.position = jpos_sensor->GetSensedPosition().position;
  joint_state_.velocity = jvel_estimator->GetVelocityEstimate().velocity;
  joint_state_.acceleration.setZero();

  // Reset current targets and controller internal state.
  controller_->ResetInternalState();
  current_cartesian_target_.reset();
  current_nullspace_target_.reset();

  INTRINSIC_RT_RETURN_IF_ERROR(is_settled_criterion_->Initialize());

  return SetCompleteReference(params.slot_map, *cart_impedance_params_,
                              joint_state_);
}

RealtimeStatus CartesianImpedanceAction::Sense(SenseParameters params) {
  INTRINSIC_RT_ASSIGN_OR_RETURN(
      (auto [jpos_sensor, jvel_estimator]),
      (params.slot_map
           .GetInterfacesForSlot<JointPositionSensor, JointVelocityEstimator>(
               arm_slot_id_)));

  INTRINSIC_RT_ASSIGN_OR_RETURN(
      (auto [ft_sensor]),
      (params.slot_map.GetInterfacesForSlot<ForceTorqueSensor>(ft_slot_id_)));

  // Update sensor readings and state estimates.
  joint_state_.position = jpos_sensor->GetSensedPosition().position;
  joint_state_.velocity = jvel_estimator->GetVelocityEstimate().velocity;
  joint_state_.acceleration.setZero();
  Wrench sensed_wrench_in_tip_frame = ft_sensor->WrenchAtTip();
  Wrench post_sensor_dynamic_load_at_tip =
      ft_sensor->PostSensorDynamicLoadAtTip();

  // Update setpoints if received new goals from streaming input.
  INTRINSIC_RT_ASSIGN_OR_RETURN(
      const intrinsic::icon::CartesianImpedanceParameters* streaming_parameters,
      params.streaming_io_access.PollInput<CartesianImpedanceParameters>(
          streaming_input_id_));
  if (streaming_parameters != nullptr) {
    ++streaming_input_message_count_;
    // New controller goal received, run real-time pre-processing steps and
    // set.
    INTRINSIC_RT_RETURN_IF_ERROR(SetCompleteReference(
        params.slot_map, *streaming_parameters, joint_state_));
  }

  // Propagate the cartesian impedance target generator using the currently set
  // cartesian target.
  if (!controller_->GetCartesianTarget().has_value()) {
    return InternalError(
        "No Cartesian target available in controller. Initialize controller "
        "first.");
  }
  CartStatePVA cart_state_sensed;
  cart_state_sensed.pose = controller_->GetRobotTaskToTool();
  cart_state_sensed.velocity = controller_->GetTwist();
  INTRINSIC_RT_ASSIGN_OR_RETURN(
      cartesian_impedance::RealTimeCartesianTarget cartesian_target,
      impedance_reference_generator_holder_.GetActiveReferenceGenerator()
          .Evaluate(*controller_->GetCartesianTarget(),
                    /*cart_state_sensed*/ cart_state_sensed,
                    params.speed_override));
  controller_->SetCartesianTarget(cartesian_target);

  // Propagate the nullspace target motion generator using the currently set
  // null-space target, if we have a redundant robot. Otherwise, skip and save
  // computation time.
  if (njoints_ > 6) {
    if (!controller_->GetNullspaceTarget().has_value()) {
      return InternalError(
          "No nullspace target available in controller. Initialize controller "
          "first.");
    }
    INTRINSIC_RT_ASSIGN_OR_RETURN(
        cartesian_impedance::RealTimeNullspaceTarget nullspace_target,
        nullspace_reference_generator_holder_.GetActiveReferenceGenerator()
            .Evaluate(*controller_->GetNullspaceTarget(),
                      params.speed_override));
    controller_->SetNullspaceTarget(nullspace_target);
  }

  INTRINSIC_RT_RETURN_IF_ERROR(
      controller_->PrepareControl(joint_state_, sensed_wrench_in_tip_frame,
                                  post_sensor_dynamic_load_at_tip));

  // Update elapsed time.
  elapsed_time_seconds_ += 1.0 / icon_frequency_hz_;

  // Evaluate state variables.
  const auto& reference_generator_target =
      impedance_reference_generator_holder_.GetActiveReferenceGenerator()
          .GetReference();

  const Pose3d task_t_tool_reference =
      Pose3d(reference_generator_target.task_t_tool_reference_orientation,
             reference_generator_target.task_t_tool_reference_position);

  // Compute manipulability.
  INTRINSIC_RT_ASSIGN_OR_RETURN(eigenmath::Matrix6Nd jacobian,
                                controller_->GetJacobian());
  double manipulability = kinematics::ComputeManipulability(jacobian);

  const cartesian_impedance::StateVariableConfiguration& state_variable_config =
      cart_impedance_params_->state_variable_configuration;

  // Update internal state to determine if system has reached being stationary
  // within a band. From here we can start counting the settling time. The
  // actual determination of is_settled is done below using the more
  // sophisticated probabilistic criterion based on the reference twist.
  double settled_for_seconds = controller_->UpdateSettlingTime(
      joint_state_,
      state_variable_config.translational_velocity_error_threshold,
      state_variable_config.angular_velocity_error_threshold,
      state_variable_config.joint_velocity_threshold);

  const auto twist_in_base_frame = cartesian_impedance::RotateCartesianVector(
      cartesian_target.robot_base_t_task.quaternion(),
      cartesian_target.tool_reference_twist);
  INTRINSIC_RT_ASSIGN_OR_RETURN(const eigenmath::MatrixNMd jacobian_pinv,
                                controller_->GetJacobianPinv());
  const eigenmath::VectorNd implied_reference_joint_velocity_command =
      jacobian_pinv * twist_in_base_frame;
  // Assumes the trajectory has ended when the joint velocities are within the
  // settling band.
  INTRINSIC_RT_ASSIGN_OR_RETURN(
      const bool is_settled,
      is_settled_criterion_->Update(
          joint_state_.velocity, implied_reference_joint_velocity_command,
          /*has_trajectory_ended=*/settled_for_seconds > 0.0));

  // IsDone is remapped to reference_pose_reached for simplicity.
  bool is_done = controller_->GetRobotTaskToTool().isApprox(
      task_t_tool_reference, state_variable_config.position_error_threshold,
      state_variable_config.orientation_error_threshold);

  if (!task_t_tool_start_.has_value()) {
    task_t_tool_start_ = controller_->GetRobotTaskToTool();
  }
  const double translational_distance_traveled =
      cartesian_impedance::ComputeTranslationalDistanceTraveled(
          *task_t_tool_start_, controller_->GetRobotTaskToTool(),
          state_variable_config.translational_distance_along);

  distance_tracker_->Add(controller_->GetRobotTaskToTool().translation());

  state_variables_.Update(
      {.current_task_t_tool = controller_->GetRobotTaskToTool(),
       .reference_task_t_tool = task_t_tool_reference,
       .sensed_wrench = controller_->SensedWrenchAtToolInTaskFrame(),
       .manipulability = manipulability,
       .elapsed_time_seconds = elapsed_time_seconds_,
       .translational_distance_traveled_m = translational_distance_traveled,
       .maximum_translational_displacement_in_window_m =
           distance_tracker_->GetMaximumDistance(),
       .is_done = is_done,
       .is_settled = is_settled,
       .settled_for_seconds = settled_for_seconds,
       .streaming_input_message_count = streaming_input_message_count_});

  // Write data into streaming output struct.
  //
  // TODO(b/461421151): Please note that the streaming output from this action
  // should match that from the CartesianAdmittanceAction so that they can be
  // used interchangeably by the skill framework.
  cartesian_impedance::AdmittanceActionStreamingOutputStatus streaming_output;

  const Pose3d task_t_base = cartesian_target.robot_base_t_task.inverse();
  cartesian_impedance::CartesianErrorState cntrl_error_state_in_task_frame =
      cartesian_impedance::RotateCartesianErrorState(
          task_t_base.quaternion(), controller_->GetCartesianErrorState());

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
      controller_->SensedWrenchAtToolInTaskFrame().head<3>();
  Eigen::Map<eigenmath::Vector3d>(
      streaming_output.sensed_torque_at_tool_in_task_frame) =
      controller_->SensedWrenchAtToolInTaskFrame().tail<3>();
  Eigen::Map<eigenmath::Vector3d>(
      streaming_output.post_sensor_dynamics_force_in_task_frame) =
      controller_->PostSensorDynamicLoadInTaskFrame().head<3>();
  Eigen::Map<eigenmath::Vector3d>(
      streaming_output.post_sensor_dynamics_torque_in_task_frame) =
      controller_->PostSensorDynamicLoadInTaskFrame().tail<3>();

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
  const std::optional<cartesian_impedance::RealTimeCartesianTarget>&
      controller_cartesian_target = controller_->GetCartesianTarget();
  if (controller_cartesian_target == std::nullopt) {
    return FailedPreconditionError("Cartesian target not initialized.");
  }
  Eigen::Map<eigenmath::Vector7d>(streaming_output.robot_tip_t_robot_tool);
  cartesian_impedance::CopyPoseToDoubles(
      controller_cartesian_target->robot_tip_t_robot_tool,
      streaming_output.robot_tip_t_robot_tool);
  Eigen::Map<eigenmath::Vector7d>(streaming_output.robot_base_t_task);
  cartesian_impedance::CopyPoseToDoubles(
      controller_cartesian_target->robot_base_t_task,
      streaming_output.robot_base_t_task);
  Eigen::Map<eigenmath::Vector7d>(streaming_output.task_t_tool_reference_pose) =
      eigenmath::Vector7d(
          controller_cartesian_target->task_t_tool_reference_position.x(),
          controller_cartesian_target->task_t_tool_reference_position.y(),
          controller_cartesian_target->task_t_tool_reference_position.z(),
          controller_cartesian_target->task_t_tool_reference_orientation.w(),
          controller_cartesian_target->task_t_tool_reference_orientation.x(),
          controller_cartesian_target->task_t_tool_reference_orientation.y(),
          controller_cartesian_target->task_t_tool_reference_orientation.z());

  Eigen::Map<eigenmath::Vector6d>(streaming_output.tool_reference_twist) =
      controller_cartesian_target->tool_reference_twist;
  Eigen::Map<eigenmath::Vector6d>(streaming_output.tool_reference_wrench) =
      controller_cartesian_target->tool_reference_wrench;

  return params.streaming_io_access.WriteOutput(streaming_output);
}

RealtimeStatus CartesianImpedanceAction::Control(ControlParameters params) {
  // Write the process wrench if exposed by the arm.
  ProcessWrenchAtEndeffector* process_wrench_at_tip =
      params.slot_map.GetMutableInterfaceForSlot<ProcessWrenchAtEndeffector>(
          arm_slot_id_);
  ForceTorqueSensor* ft_sensor =
      params.slot_map.GetMutableInterfaceForSlot<ForceTorqueSensor>(
          ft_slot_id_);

  // Write sensed wrench to the ProcessWrenchAtEndeffector feature interface.
  if (process_wrench_at_tip != nullptr && ft_sensor != nullptr) {
    // Set the process wrench at the tip, such that it can be taken into account
    // by the ROEM's controller.
    process_wrench_at_tip->SetProcessWrenchAtTip(ft_sensor->WrenchAtTip());
  }

  if (using_acceleration_command_interface_) {
    intrinsic::icon::JointAcceleration* joint_acceleration_command =
        params.slot_map.GetMutableInterfaceForSlot<JointAcceleration>(
            arm_slot_id_);

    if (joint_acceleration_command == nullptr) {
      return FailedPreconditionError(
          "Expected to find the joint acceleration command interface but it "
          "was not found.");
    }
    INTRINSIC_RT_ASSIGN_OR_RETURN(const JointStateA acceleration_command,
                                  controller_->ComputeControl(joint_state_));
    INTRINSIC_RT_ASSIGN_OR_RETURN(
        const eigenmath::VectorNd sensed_wrench_joint_torques,
        controller_->GetSensedWrenchJointTorques());
    INTRINSIC_RT_ASSIGN_OR_RETURN(
        JointAccelerationCommand command,
        JointAccelerationCommand::Create(acceleration_command.acceleration,
                                         sensed_wrench_joint_torques));
    return joint_acceleration_command->SetAccelerationSetpoints(command);
  }

  INTRINSIC_RT_ASSIGN_OR_RETURN(
      (auto [joint_torque, dynamics]),
      (params.slot_map.GetMutableInterfacesForSlot<JointTorque, Dynamics>(
          arm_slot_id_)));

  INTRINSIC_RT_ASSIGN_OR_RETURN(
      const auto joint_state,
      controller_->ComputeControl(joint_state_,
                                  dynamics->GetRigidBodyInterface()));

  return joint_torque->SetTorqueSetpoints(joint_state.torque);
}

RealtimeStatusOr<StateVariableValue> CartesianImpedanceAction::GetStateVariable(
    absl::string_view name) const {
  return state_variables_.GetStateVariable(name);
}

intrinsic_proto::icon::v1::ActionSignature
CartesianImpedanceAction::GetSignature() {
  ActionSignatureBuilder builder(CartesianImpedanceInfo::kActionTypeName,
                                 CartesianImpedanceInfo::kActionDescription);
  CHECK_OK(
      builder.AddPartSlot(CartesianImpedanceInfo::kArmSlotName,
                          CartesianImpedanceInfo::kArmSlotDescription,
                          /*required_feature_interfaces=*/
                          {
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
                              intrinsic_proto::icon::v1::FeatureInterfaceTypes::
                                  FEATURE_INTERFACE_DYNAMICS,
                          },
                          /*optional_feature_interfaces=*/
                          {
                              intrinsic_proto::icon::v1::FeatureInterfaceTypes::
                                  FEATURE_INTERFACE_JOINT_TORQUE,
                              intrinsic_proto::icon::v1::FeatureInterfaceTypes::
                                  FEATURE_INTERFACE_JOINT_ACCELERATION,
                          }));
  CHECK_OK(
      builder.AddPartSlot(CartesianImpedanceInfo::kForceTorqueSlotName,
                          CartesianImpedanceInfo::kForceTorqueSlotDescription,
                          /*required_feature_interfaces=*/
                          {
                              intrinsic_proto::icon::v1::FeatureInterfaceTypes::
                                  FEATURE_INTERFACE_FORCE_TORQUE_SENSOR,
                          }));
  CHECK_OK(
      builder.SetFixedParametersType<CartesianImpedanceInfo::FixedParams>());
  CHECK_OK(AdmittanceStateVariables::RegisterVariables(builder));
  CHECK_OK(builder.AddStreamingInput<CartesianImpedanceInfo::FixedParams>(
      CartesianImpedanceInfo::kStreamingInputName));
  CHECK_OK(builder.AddStreamingOutput<CartesianImpedanceInfo::StreamingOutput>(
      CartesianImpedanceInfo::kStreamingOutputName,
      CartesianImpedanceInfo::kStreamingOutputDescription));
  return builder.Finish();
}

}  // namespace intrinsic::icon
