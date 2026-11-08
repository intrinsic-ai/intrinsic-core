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

#include "intrinsic/icon/control/algorithms/lissajous_search_pattern_generator.h"

#include <cmath>
#include <memory>
#include <optional>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "intrinsic/eigenmath/rotation_utils.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/actions/impedance_trajectory_generators.pb.h"
#include "intrinsic/icon/control/algorithms/cartesian_impedance_commons.h"
#include "intrinsic/icon/control/algorithms/cartesian_impedance_trajectory.h"
#include "intrinsic/icon/control/algorithms/compute_critical_damping.h"
#include "intrinsic/icon/control/algorithms/overdamping.h"
#include "intrinsic/icon/proto/cart_space_conversion.h"
#include "intrinsic/icon/utils/realtime_guard.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/icon/utils/realtime_status_macro.h"
#include "intrinsic/icon/utils/realtime_status_or.h"
#include "intrinsic/kinematics/types/cart_state.h"
#include "intrinsic/kinematics/types/state_rn.h"
#include "intrinsic/math/lissajous_pattern_utils.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/twist.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic::icon {

using lissajous_pattern_details::EvaluateLissajousPatternPosition;
using lissajous_pattern_details::EvaluateLissajousPatternVelocity;
using lissajous_pattern_details::kMaxLissajousRatio;
using lissajous_pattern_details::kMinLissajousRatio;
using lissajous_pattern_details::OptimizeLissajousPatternByRatio;

static constexpr double kZeroThreshold = 1e-12;

namespace {

// Rotate Matrix expressed in frame a into frame b. Important: this method only
// holds for block-diagonal matrix where the bottom left corner and the
// top-right corner are all zero.
eigenmath::Matrix6d Rotate6dMatrix(const eigenmath::Quaterniond& b_q_a,
                                   const eigenmath::Matrix6d& matrix_a) {
  eigenmath::Matrix6d matrix_b = eigenmath::Matrix6d::Zero();
  eigenmath::Matrix3d b_R_a = b_q_a.toRotationMatrix();
  matrix_b.block(0, 0, 3, 3) =
      b_R_a * matrix_a.block(0, 0, 3, 3) * b_R_a.transpose();
  matrix_b.block(3, 3, 3, 3) =
      b_R_a * matrix_a.block(3, 3, 3, 3) * b_R_a.transpose();
  return matrix_b;
}

}  // namespace

/*static*/
LissajousSearchPattern2D::InputParameters LissajousSearchPattern2D::FromProto(
    const intrinsic_proto::icon::actions::proto::LissajousSearchPattern2D&
        proto_params) {
  InputParameters params;
  params.x_size = proto_params.x_size();
  params.y_size = proto_params.y_size();
  if (proto_params.has_max_velocity()) {
    params.max_velocity = proto_params.max_velocity();
  }
  if (proto_params.has_max_lissajous_integer()) {
    params.max_lissajous_integer = proto_params.max_lissajous_integer();
  }
  return params;
}

/*static*/
intrinsic_proto::icon::actions::proto::LissajousSearchPattern2D
LissajousSearchPattern2D::ToProto(const InputParameters& params) {
  intrinsic_proto::icon::actions::proto::LissajousSearchPattern2D proto_params;
  proto_params.set_x_size(params.x_size);
  proto_params.set_y_size(params.y_size);
  proto_params.set_max_velocity(params.max_velocity);
  proto_params.set_max_lissajous_integer(params.max_lissajous_integer);
  return proto_params;
}

/*static*/
absl::StatusOr<LissajousSearchPattern2D> LissajousSearchPattern2D::Create(
    const InputParameters& params, double frequency_hz) {
  if (frequency_hz <= 0.0) {
    return absl::InvalidArgumentError("frequency_hz must be positive");
  }
  if (params.x_size < 0.0) {
    return absl::InvalidArgumentError("x_size must be non-negative");
  }
  if (params.y_size < 0.0) {
    return absl::InvalidArgumentError("y_size must be non-negative");
  }
  if (params.max_velocity < 0.0) {
    return absl::InvalidArgumentError("max_velocity must be positive");
  }
  if (params.max_lissajous_integer < kMinLissajousRatio ||
      params.max_lissajous_integer > kMaxLissajousRatio) {
    return absl::InvalidArgumentError(absl::StrCat(
        "max_lissajous_integer must be integers between [", kMinLissajousRatio,
        ",", kMaxLissajousRatio, "], but got ", params.max_lissajous_integer));
  }

  INTR_ASSIGN_OR_RETURN(
      GeometricalLissajousPattern geometrical_lissajous_pattern,
      OptimizeLissajousPatternByRatio(params.x_size, params.y_size,
                                      params.max_lissajous_integer,
                                      params.max_velocity));

  return LissajousSearchPattern2D(geometrical_lissajous_pattern, frequency_hz);
}

icon::RealtimeStatusOr<StateRnPV> LissajousSearchPattern2D::Step(
    double speed_override_factor) {
  StateRnPV setpoint;
  INTRINSIC_RT_RETURN_IF_ERROR(setpoint.SetSize(2));

  // Modulate internal execution time with speed override.
  time_since_pattern_execution_start_ +=
      speed_override_factor * 1. / frequency_hz_;

  // Evaluate new setpoint, apply simple linear scaling for velocity based on
  // speed override.
  setpoint.position = EvaluateLissajousPatternPosition(
      time_since_pattern_execution_start_, geometrical_lissajous_pattern_);
  setpoint.velocity =
      speed_override_factor *
      EvaluateLissajousPatternVelocity(time_since_pattern_execution_start_,
                                       geometrical_lissajous_pattern_);

  return setpoint;
}

/*static*/
absl::StatusOr<LissajousImpedanceSearchPattern::InputParameters>
LissajousImpedanceSearchPattern::FromProto(
    const intrinsic_proto::icon::actions::proto::
        LissajousImpedanceSearchPattern& proto_params) {
  INTRINSIC_ASSERT_NON_REALTIME();

  InputParameters params;

  if (proto_params.has_base_t_task()) {
    INTR_ASSIGN_OR_RETURN(params.base_t_task,
                          icon::FromProto(proto_params.base_t_task()));
  }

  params.search_pattern_2d_parameters =
      LissajousSearchPattern2D::FromProto(proto_params.search_pattern_2d());

  if (proto_params.has_yaw_oscillation_amplitude_rad()) {
    params.yaw_oscillation_amplitude_rad =
        proto_params.yaw_oscillation_amplitude_rad();
  }

  if (proto_params.has_yaw_oscillation_frequency_hz()) {
    params.yaw_oscillation_frequency_hz =
        proto_params.yaw_oscillation_frequency_hz();
  }

  params.search_force_newton = proto_params.search_force_newton();

  params.contact_stiffness = proto_params.contact_stiffness();

  if (proto_params.has_translational_virtual_stiffness_x_y()) {
    params.translational_virtual_stiffness_x_y =
        proto_params.translational_virtual_stiffness_x_y();
  }

  if (proto_params.has_rotational_virtual_stiffness_roll_pitch()) {
    params.rotational_virtual_stiffness_roll_pitch =
        proto_params.rotational_virtual_stiffness_roll_pitch();
  }

  if (proto_params.has_rotational_virtual_stiffness_yaw()) {
    params.rotational_virtual_stiffness_yaw =
        proto_params.rotational_virtual_stiffness_yaw();
  }

  params.translational_virtual_inertia =
      proto_params.translational_virtual_inertia();

  params.rotational_virtual_inertia = proto_params.rotational_virtual_inertia();

  if (proto_params.has_robot_tip_t_robot_tool()) {
    INTR_ASSIGN_OR_RETURN(
        params.robot_tip_t_robot_tool,
        icon::FromProto(proto_params.robot_tip_t_robot_tool()));
  }

  return params;
}

/*static*/
intrinsic_proto::icon::actions::proto::LissajousImpedanceSearchPattern
LissajousImpedanceSearchPattern::ToProto(const InputParameters& params) {
  intrinsic_proto::icon::actions::proto::LissajousImpedanceSearchPattern
      proto_params;
  if (params.base_t_task.has_value()) {
    *proto_params.mutable_base_t_task() = icon::ToProto(*params.base_t_task);
  }
  *proto_params.mutable_search_pattern_2d() =
      LissajousSearchPattern2D::ToProto(params.search_pattern_2d_parameters);
  proto_params.set_yaw_oscillation_amplitude_rad(
      params.yaw_oscillation_amplitude_rad);
  proto_params.set_yaw_oscillation_frequency_hz(
      params.yaw_oscillation_frequency_hz);
  proto_params.set_search_force_newton(params.search_force_newton);
  proto_params.set_contact_stiffness(params.contact_stiffness);
  proto_params.set_translational_virtual_stiffness_x_y(
      params.translational_virtual_stiffness_x_y);
  proto_params.set_rotational_virtual_stiffness_roll_pitch(
      params.rotational_virtual_stiffness_roll_pitch);
  proto_params.set_rotational_virtual_stiffness_yaw(
      params.rotational_virtual_stiffness_yaw);
  proto_params.set_translational_virtual_inertia(
      params.translational_virtual_inertia);
  proto_params.set_rotational_virtual_inertia(
      params.rotational_virtual_inertia);
  *proto_params.mutable_robot_tip_t_robot_tool() =
      icon::ToProto(params.robot_tip_t_robot_tool);
  return proto_params;
}

/*static*/
absl::StatusOr<std::unique_ptr<CartesianImpedanceTrajectoryInterface>>
LissajousImpedanceSearchPattern::Create(const InputParameters& params,
                                        double frequency_hz) {
  if (frequency_hz <= 0.) {
    return absl::InvalidArgumentError("frequency_hz must be positive");
  }
  if (params.search_force_newton < 0.0) {
    return absl::InvalidArgumentError(
        "'search_force_newton' must be non-negative");
  }
  if (params.contact_stiffness < 0.0) {
    return absl::InvalidArgumentError(
        "'contact_stiffness' must be non-negative");
  }
  if (params.translational_virtual_stiffness_x_y < 0.0) {
    return absl::InvalidArgumentError(
        "'translational_virtual_stiffness_x_y' must be non-negative");
  }
  if (params.rotational_virtual_stiffness_roll_pitch < 0.0) {
    return absl::InvalidArgumentError(
        "'rotational_virtual_stiffness_roll_pitch' must be non-negative");
  }
  if (params.rotational_virtual_stiffness_yaw < 0.0) {
    return absl::InvalidArgumentError(
        "'rotational_virtual_stiffness_yaw' must be non-negative");
  }
  if (params.translational_virtual_inertia < kZeroThreshold) {
    return absl::InvalidArgumentError(
        "'translational_virtual_inertia' must be positive");
  }
  if (params.rotational_virtual_inertia < kZeroThreshold) {
    return absl::InvalidArgumentError(
        "'rotational_virtual_inertia' must be positive");
  }
  if (params.yaw_oscillation_amplitude_rad < 0.0) {
    return absl::InvalidArgumentError(
        "'yaw_oscillation_amplitude_rad' must be non-negative");
  }
  if (params.yaw_oscillation_frequency_hz < 0.0) {
    return absl::InvalidArgumentError(
        "'yaw_oscillation_frequency_hz' must be non-negative");
  }

  INTR_ASSIGN_OR_RETURN(LissajousSearchPattern2D search_pattern_2d,
                        LissajousSearchPattern2D::Create(
                            params.search_pattern_2d_parameters, frequency_hz));

  return std::unique_ptr<CartesianImpedanceTrajectoryInterface>(
      new LissajousImpedanceSearchPattern(params, search_pattern_2d,
                                          frequency_hz));
}

RealtimeStatus LissajousImpedanceSearchPattern::Initialize(
    const CartStatePVA& base_t_tip_last_commanded) {
  // Auto-complete the task-frame if not specified by the user.
  const Pose3d base_t_tool_last_commanded =
      base_t_tip_last_commanded.pose * params_.robot_tip_t_robot_tool;

  if (!params_.base_t_task.has_value()) {
    params_.base_t_task = base_t_tool_last_commanded;
    task_t_tool_start_ = Pose3d::Identity();
  } else {
    // Keep the orientation of the task frame, but move it to the last
    // commanded position.
    params_.base_t_task->translation() =
        base_t_tool_last_commanded.translation();
    task_t_tool_start_ =
        Pose3d((params_.base_t_task->inverse() * base_t_tool_last_commanded)
                   .quaternion());
  }

  fixed_cartesian_impedance_target_ =
      cartesian_impedance::RealTimeCartesianTarget();
  fixed_cartesian_impedance_target_->robot_tip_t_robot_tool =
      params_.robot_tip_t_robot_tool;
  fixed_cartesian_impedance_target_->robot_base_t_task = *params_.base_t_task;
  fixed_cartesian_impedance_target_->tool_reference_acceleration.setZero();

  // Define compliance pattern using selection matrix - all DoF are compliant.
  fixed_cartesian_impedance_target_->wrench_selection_matrix.setIdentity();

  // Step 1 - compute directional stiffness matrix. The stiffness in insertion
  // direction is always zero.
  // Helper matrix representing purely translational stiffness in task frame.
  const eigenmath::Matrix6d stiffness_in_taskframe =
      eigenmath::Vector6d(params_.translational_virtual_stiffness_x_y,
                          params_.translational_virtual_stiffness_x_y,
                          /*insertion direction*/ 0.0,
                          params_.rotational_virtual_stiffness_roll_pitch,
                          params_.rotational_virtual_stiffness_roll_pitch,
                          params_.rotational_virtual_stiffness_yaw)
          .asDiagonal();

  fixed_cartesian_impedance_target_->cartesian_stiffness =
      Rotate6dMatrix(params_.base_t_task->quaternion(), stiffness_in_taskframe);

  // Step 2 - assemble inertia matrix in task frame. Note that there is no need
  // to explicitly rotate inertia matrix from task frame to robot base frame,
  // since it would result in the same diagonal matrix.
  fixed_cartesian_impedance_target_->virtual_cartesian_inertia_inverse
      .setZero();
  fixed_cartesian_impedance_target_->virtual_cartesian_inertia_inverse
      .diagonal()
      .segment(0, 3)
      .setConstant(1. / params_.translational_virtual_inertia);
  fixed_cartesian_impedance_target_->virtual_cartesian_inertia_inverse
      .diagonal()
      .segment(3, 3)
      .setConstant(1. / params_.rotational_virtual_inertia);

  // Step 3 - Compute damping and overdamping in robot base frame.
  // The resulting damping matrix is automatically expressed in the right frame.
  // TODO(b/319798940): currently we compute the damping matrix based on the
  // mock-stiffness-matrix which is all-identity and later scale it with
  // overdamping. Should we derive the damping matrix based on the "true"
  // stiffness matrix?
  const double kTranslationMockStiffness = 1.0;
  eigenmath::Matrix6d mock_stiffness_matrix = eigenmath::Matrix6d::Identity();
  INTRINSIC_RT_ASSIGN_OR_RETURN(
      fixed_cartesian_impedance_target_->cartesian_damping,
      ComputeCriticalDampingFromInverseInertiaMatrix(
          fixed_cartesian_impedance_target_->virtual_cartesian_inertia_inverse,
          mock_stiffness_matrix));

  // The overdamping is computed based on a simple mock stiffness value
  // (stiffness cancels from the equation in z direction). In non-contact
  // directions the stiffness is often deactivated. Overdamping is applied to
  // entire damping matrix for simplicity.
  INTRINSIC_RT_ASSIGN_OR_RETURN(
      double overdamping_factor,
      ComputeOverdampingConstantFromStiffnessScalars({
          .desired_virtual_stiffness_contact = kTranslationMockStiffness,
          // we lump all the compliance into one element of the serial chain
          // manipulator - compliance-device environment.
          .stiffness_compliance_device = params_.contact_stiffness,
          .criterion = kBenady,
      }));

  // TODO(b/319798937): figure out how exactly the overdamping should be
  // computed and applied. Should it be direction-specific?
  fixed_cartesian_impedance_target_->cartesian_damping *= overdamping_factor;

  // Assign the feedforward wrench.
  Wrench target_wrench = Wrench::ZERO;
  target_wrench.z() = params_.search_force_newton;
  fixed_cartesian_impedance_target_->tool_reference_wrench = target_wrench;

  return icon::OkStatus();
}

RealtimeStatusOr<cartesian_impedance::RealTimeCartesianTarget>
LissajousImpedanceSearchPattern::Step(double speed_override) {
  if (!fixed_cartesian_impedance_target_.has_value()) {
    return icon::FailedPreconditionError(
        "No realtime Cartesian target available. Did you call Initialize()?");
  }
  if (!params_.base_t_task.has_value()) {
    return icon::InternalError(
        "No base_t_task transform available. Please check Initialize()?");
  }

  // Initialize with fixed (constant) parameters.
  cartesian_impedance::RealTimeCartesianTarget rt_target =
      fixed_cartesian_impedance_target_.value();

  // Compute oscillation about search axis in z direction.
  const double kOmega = 2. * M_PI * params_.yaw_oscillation_frequency_hz;
  time_since_trajectory_start_internal_ += speed_override * 1. / frequency_hz_;
  double yaw_oscillation_angle_in_task_frame =
      params_.yaw_oscillation_amplitude_rad *
      std::sin(kOmega * time_since_trajectory_start_internal_);
  double yaw_oscillation_twist_in_task_frame =
      speed_override * params_.yaw_oscillation_amplitude_rad * kOmega *
      std::cos(kOmega * time_since_trajectory_start_internal_);

  // Compute 2d search pattern state.
  INTRINSIC_RT_ASSIGN_OR_RETURN(StateRnPV new_lissajous_state_2d_in_task_frame,
                                search_pattern_2d_.Step(speed_override));

  // Rotate the desired tool pose back to the start orientation and rotate the
  // yaw axis afterwards. This ensures that the tool always keeps the start
  // orientation and only the yaw oscillation is added.
  // Please see go/lissajous_pattern_search_frames_visualization for a
  // visualization of the frames.
  const Pose3d task_t_pattern_evaluation(
      eigenmath::QuaternionFromRPY(0.0, 0.0,
                                   yaw_oscillation_angle_in_task_frame),
      eigenmath::Vector3d(new_lissajous_state_2d_in_task_frame.position.x(),
                          new_lissajous_state_2d_in_task_frame.position.y(),
                          0.0));
  // We want to keep the relative pose of the tool reference frame w.r.t. the
  // pattern evaluation frame (`pattern_evaluation_t_tool_reference`) constant
  // and the same as the relative pose of the tool frame at start time w.r.t.
  // the task frame (`task_t_tool_start_`).
  const Pose3d& pattern_evaluation_t_tool_reference = task_t_tool_start_;
  const Pose3d task_t_tool_reference =
      task_t_pattern_evaluation * pattern_evaluation_t_tool_reference;

  // Update reference pose with pattern.
  rt_target.task_t_tool_reference_orientation =
      task_t_tool_reference.quaternion();
  rt_target.task_t_tool_reference_position =
      task_t_tool_reference.translation();

  // Express 2d pattern twist in 6d impedance target.
  Twist reference_twist;
  reference_twist << new_lissajous_state_2d_in_task_frame.velocity.x(),
      new_lissajous_state_2d_in_task_frame.velocity.y(), 0., 0., 0.,
      yaw_oscillation_twist_in_task_frame;
  rt_target.tool_reference_twist = reference_twist;

  return rt_target;
}

}  // namespace intrinsic::icon
