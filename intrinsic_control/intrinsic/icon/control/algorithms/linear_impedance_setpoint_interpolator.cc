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

#include "intrinsic/icon/control/algorithms/linear_impedance_setpoint_interpolator.h"

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "intrinsic/eigenmath/interpolation.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/actions/cartesian_impedance_parameters.h"
#include "intrinsic/icon/actions/impedance_trajectory_generators.pb.h"
#include "intrinsic/icon/control/algorithms/cartesian_impedance_commons.h"
#include "intrinsic/icon/control/algorithms/cartesian_impedance_trajectory.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/icon/utils/realtime_status_macro.h"
#include "intrinsic/icon/utils/realtime_status_or.h"
#include "intrinsic/kinematics/types/cart_state.h"
#include "intrinsic/math/ipow.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/symmetric_positive_definite_matrix_squareroot.h"
#include "intrinsic/math/time_series_utils.h"
#include "intrinsic/math/twist.h"
#include "intrinsic/util/proto_time.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic::icon {

absl::StatusOr<ImpedanceTrajectory> FromProto(
    const intrinsic_proto::icon::actions::proto::
        LinearImpedanceSetpointInterpolator& proto) {
  if (proto.cartesian_impedance_waypoints_size() != proto.time_stamps_size()) {
    return absl::InvalidArgumentError(
        absl::StrCat("LinearImpedanceSetpointInterpolator sizes do not match: "
                     "cartesian_impedance_waypoints is size ",
                     proto.cartesian_impedance_waypoints_size(),
                     " but time_stamps is size ", proto.time_stamps_size()));
  }

  ImpedanceTrajectory impedance_trajectory;
  for (int i = 0; i < proto.time_stamps_size(); ++i) {
    impedance_trajectory.time_stamps.push_back(
        ToAbslDurationNoValidation(proto.time_stamps(i)));
    INTR_ASSIGN_OR_RETURN(auto impedance_waypoint,
                          FromProto(proto.cartesian_impedance_waypoints(i)));
    impedance_trajectory.impedance_waypoints.push_back(
        std::move(impedance_waypoint));
  }
  return impedance_trajectory;
}

absl::StatusOr<
    intrinsic_proto::icon::actions::proto::LinearImpedanceSetpointInterpolator>
ToProto(const ImpedanceTrajectory& params) {
  if (params.time_stamps.size() != params.impedance_waypoints.size()) {
    return absl::InvalidArgumentError(
        absl::StrCat("ImpedanceTrajectory sizes do not match: "
                     "impedance_waypoints is size ",
                     params.impedance_waypoints.size(),
                     " but time_stamps is size ", params.time_stamps.size()));
  }
  intrinsic_proto::icon::actions::proto::LinearImpedanceSetpointInterpolator
      proto;
  for (int i = 0; i < params.time_stamps.size(); ++i) {
    INTR_RETURN_IF_ERROR(
        FromAbslDuration(params.time_stamps[i], proto.add_time_stamps()));
    *proto.add_cartesian_impedance_waypoints() =
        ToProto(params.impedance_waypoints[i]);
  }
  return proto;
}

/*static*/
absl::StatusOr<std::unique_ptr<CartesianImpedanceTrajectoryInterface>>
LinearCartesianImpedanceSetpointInterpolator::Create(
    const ImpedanceTrajectory& impedance_trajectory,
    absl::Duration interpolation_time_step) {
  if (impedance_trajectory.time_stamps.empty()) {
    return absl::InvalidArgumentError("Time stamps must not be empty.");
  }

  if (impedance_trajectory.time_stamps.front() != absl::ZeroDuration()) {
    return absl::InvalidArgumentError("Time stamps must start at zero.");
  }

  if (impedance_trajectory.impedance_waypoints.size() !=
      impedance_trajectory.time_stamps.size()) {
    return absl::InvalidArgumentError(
        "Impedance waypoints and time stamps must have the same size.");
  }

  if (interpolation_time_step <= absl::ZeroDuration()) {
    return absl::InvalidArgumentError(
        "Interpolation time step must be positive.");
  }

  INTR_RETURN_IF_ERROR(ElementsIncreaseStrictlyMonotonically(
      absl::MakeConstSpan(impedance_trajectory.time_stamps)))
      << "Time stamps must be strictly monotonically increasing.";

  std::vector<cartesian_impedance::RealTimeCartesianTarget> rt_targets;
  rt_targets.reserve(impedance_trajectory.impedance_waypoints.size());
  for (cartesian_impedance::CartesianTarget waypoint :
       impedance_trajectory.impedance_waypoints) {
    if (!waypoint.task_t_tool_reference_pose.has_value()) {
      return absl::InvalidArgumentError(
          "Pose must be set for every impedance waypoint, auto-derivation of "
          "poses is not supported for "
          "LinearCartesianImpedanceSetpointInterpolator.");
    }
    INTR_RETURN_IF_ERROR(cartesian_impedance::Preprocess(waypoint));
    const Pose3d kUnusedDummyPose = Pose3d::Identity();
    INTRINSIC_RT_ASSIGN_OR_RETURN(
        const cartesian_impedance::RealTimeCartesianTarget rt_target,
        cartesian_impedance::ToRealTimeCartesianTarget(kUnusedDummyPose,
                                                       waypoint));
    rt_targets.push_back(std::move(rt_target));
  }

  // Computationally expensive creation of interpolation containers.
  std::vector<EntropyRegulatedWassersteinDistanceInterpolation>
      stiffness_interpolators;
  stiffness_interpolators.reserve(rt_targets.size() - 1);
  std::vector<EntropyRegulatedWassersteinDistanceInterpolation>
      damping_interpolators;
  damping_interpolators.reserve(rt_targets.size() - 1);
  std::vector<EntropyRegulatedWassersteinDistanceInterpolation>
      inertia_inverse_interpolators;
  inertia_inverse_interpolators.reserve(rt_targets.size() - 1);
  for (int i = 0; i < rt_targets.size() - 1; ++i) {
    INTR_ASSIGN_OR_RETURN(
        auto stiffness_interpolator,
        EntropyRegulatedWassersteinDistanceInterpolation::Create(
            rt_targets[i].cartesian_stiffness,
            rt_targets[i + 1].cartesian_stiffness));
    stiffness_interpolators.push_back(std::move(stiffness_interpolator));
    INTR_ASSIGN_OR_RETURN(
        auto damping_interpolator,
        EntropyRegulatedWassersteinDistanceInterpolation::Create(
            rt_targets[i].cartesian_damping,
            rt_targets[i + 1].cartesian_damping));
    damping_interpolators.push_back(std::move(damping_interpolator));
    INTR_ASSIGN_OR_RETURN(
        auto inertia_inverse_interpolator,
        EntropyRegulatedWassersteinDistanceInterpolation::Create(
            rt_targets[i].virtual_cartesian_inertia_inverse,
            rt_targets[i + 1].virtual_cartesian_inertia_inverse));
    inertia_inverse_interpolators.push_back(
        std::move(inertia_inverse_interpolator));
  };

  // Using new because of private constructor.
  return absl::WrapUnique(new LinearCartesianImpedanceSetpointInterpolator(
      impedance_trajectory.time_stamps, interpolation_time_step,
      /*rt_impedance_targets=*/std::move(rt_targets),
      /*inertia_matrix_inverse_interpolations=*/
      absl::MakeSpan(inertia_inverse_interpolators),
      /*stiffness_matrix_interpolations=*/
      absl::MakeSpan(stiffness_interpolators),
      /*damping_matrix_interpolations=*/absl::MakeSpan(damping_interpolators)));
}

LinearCartesianImpedanceSetpointInterpolator::
    LinearCartesianImpedanceSetpointInterpolator(
        absl::Span<const absl::Duration> time_stamps,
        absl::Duration interpolation_time_step,
        absl::Span<const cartesian_impedance::RealTimeCartesianTarget>
            rt_impedance_targets,
        absl::Span<const EntropyRegulatedWassersteinDistanceInterpolation>
            inertia_matrix_inverse_interpolations,
        absl::Span<const EntropyRegulatedWassersteinDistanceInterpolation>
            stiffness_matrix_interpolations,
        absl::Span<const EntropyRegulatedWassersteinDistanceInterpolation>
            damping_matrix_interpolations)
    : interpolation_time_step_(interpolation_time_step),
      time_stamps_(time_stamps.begin(), time_stamps.end()),
      rt_cartesian_targets_(rt_impedance_targets.begin(),
                            rt_impedance_targets.end()),
      cartesian_inertia_inverse_interpolations_(
          inertia_matrix_inverse_interpolations.begin(),
          inertia_matrix_inverse_interpolations.end()),
      cartesian_stiffness_interpolations_(
          stiffness_matrix_interpolations.begin(),
          stiffness_matrix_interpolations.end()),
      cartesian_damping_interpolations_(damping_matrix_interpolations.begin(),
                                        damping_matrix_interpolations.end()) {}

RealtimeStatus LinearCartesianImpedanceSetpointInterpolator::Initialize(
    const CartStatePVA& base_t_target_last_commanded) {
  time_since_trajectory_start_ = absl::ZeroDuration();
  return icon::OkStatus();
}

RealtimeStatusOr<cartesian_impedance::RealTimeCartesianTarget>
LinearCartesianImpedanceSetpointInterpolator::Step(double speed_override) {
  if (speed_override < 0.0 || speed_override > 1.0) {
    return icon::InvalidArgumentError("Speed override must be in [0, 1].");
  }
  time_since_trajectory_start_ += speed_override * interpolation_time_step_;

  const int lhs_index_for_time = GetLowerIndexForValue<absl::Duration>(
      absl::MakeConstSpan(time_stamps_), time_since_trajectory_start_);

  if (lhs_index_for_time >= rt_cartesian_targets_.size() - 1) {
    return cartesian_impedance::RealTimeCartesianTarget(
        rt_cartesian_targets_.back());
  }

  absl::Duration t_lower = time_stamps_[lhs_index_for_time];
  absl::Duration t_upper = time_stamps_[lhs_index_for_time + 1];

  const double t_percentage = absl::FDivDuration(
      (time_since_trajectory_start_ - t_lower), (t_upper - t_lower));

  cartesian_impedance::RealTimeCartesianTarget interpolated_cart_target;
  INTRINSIC_RT_ASSIGN_OR_RETURN(
      interpolated_cart_target.virtual_cartesian_inertia_inverse,
      cartesian_inertia_inverse_interpolations_.at(lhs_index_for_time)
          .Interpolate(t_percentage));
  INTRINSIC_RT_ASSIGN_OR_RETURN(
      interpolated_cart_target.cartesian_stiffness,
      cartesian_stiffness_interpolations_.at(lhs_index_for_time)
          .Interpolate(t_percentage));
  INTRINSIC_RT_ASSIGN_OR_RETURN(
      interpolated_cart_target.cartesian_damping,
      cartesian_damping_interpolations_.at(lhs_index_for_time)
          .Interpolate(t_percentage));

  const cartesian_impedance::RealTimeCartesianTarget& target_lower =
      rt_cartesian_targets_[lhs_index_for_time];
  const cartesian_impedance::RealTimeCartesianTarget& target_upper =
      rt_cartesian_targets_[lhs_index_for_time + 1];

  interpolated_cart_target.task_t_tool_reference_position =
      eigenmath::InterpolateLinear(t_percentage,
                                   target_lower.task_t_tool_reference_position,
                                   target_upper.task_t_tool_reference_position);
  interpolated_cart_target.task_t_tool_reference_orientation =
      eigenmath::Interpolate(t_percentage,
                             target_lower.task_t_tool_reference_orientation,
                             target_upper.task_t_tool_reference_orientation);
  interpolated_cart_target.robot_tip_t_robot_tool =
      eigenmath::Interpolate(t_percentage, target_lower.robot_tip_t_robot_tool,
                             target_upper.robot_tip_t_robot_tool);
  interpolated_cart_target.tool_reference_acceleration =
      ::intrinsic::IPow(speed_override, 2) *
      eigenmath::InterpolateLinear(t_percentage,
                                   target_lower.tool_reference_acceleration,
                                   target_upper.tool_reference_acceleration);
  interpolated_cart_target.tool_reference_twist =
      speed_override * eigenmath::InterpolateLinear(
                           t_percentage, target_lower.tool_reference_twist,
                           target_upper.tool_reference_twist);
  interpolated_cart_target.tool_reference_wrench = eigenmath::InterpolateLinear(
      t_percentage, target_lower.tool_reference_wrench,
      target_upper.tool_reference_wrench);

  interpolated_cart_target.wrench_selection_matrix =
      target_lower.wrench_selection_matrix;

  return interpolated_cart_target;
}

const Pose3d&
LinearCartesianImpedanceSetpointInterpolator::GetInitialRobotTipTRobotTool()
    const {
  return rt_cartesian_targets_.front().robot_tip_t_robot_tool;
}

double LinearCartesianImpedanceSetpointInterpolator::GetPhase() const {
  if (time_stamps_.size() == 1) {
    return 1.0;
  }
  return std::min(1.0, absl::ToDoubleSeconds(time_since_trajectory_start_) /
                           absl::ToDoubleSeconds(time_stamps_.back()));
}

}  // namespace intrinsic::icon
