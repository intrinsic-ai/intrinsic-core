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

#ifndef INTRINSIC_ICON_CONTROL_ALGORITHMS_CARTESIAN_RANGEFINDER_DISTANCE_SETPOINT_GENERATOR_CC_
#define INTRINSIC_ICON_CONTROL_ALGORITHMS_CARTESIAN_RANGEFINDER_DISTANCE_SETPOINT_GENERATOR_CC_

#include "intrinsic/icon/control/algorithms/cartesian_rangefinder_distance_setpoint_generator.h"

#include <cstddef>
#include <memory>
#include <vector>

#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "intrinsic/eigenmath/manifolds.h"
#include "intrinsic/eigenmath/so3.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/control/algorithms/cartesian_bspline.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/kinematics/types/cart_state.h"
#include "intrinsic/kinematics/types/cartesian_limits.h"
#include "intrinsic/kinematics/types/check_cartesian_limits.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/util/eigen.h"

namespace intrinsic::icon {

CartesianBSplineSetpointGenerator::CartesianBSplineSetpointGenerator(
    double frequency, size_t num_points)
    : spline_(frequency, num_points),
      spline_status_(control::CartesianBSpline::ERROR) {}

absl::StatusOr<std::unique_ptr<CartesianBSplineSetpointGenerator>>
CartesianBSplineSetpointGenerator::Create(double frequency) {
  // Using `new` to access a non-public constructor.
  return absl::WrapUnique(new CartesianBSplineSetpointGenerator(
      frequency, kMaxNumberControlPoints));
}

RealtimeStatus CartesianBSplineSetpointGenerator::ComputeSetpoint(
    CartStatePVA& setpoint) {
  // Evaluate the spline as reference trajectory.
  // TODO(giftthaler): Note that there is an open bug that spline does not
  // keep the timestep for the last sample if less than one timestep is
  // remaining. A slight jump might occur at the end of the spline.
  spline_status_ = spline_.GetStatus();
  if (spline_status_ == control::CartesianBSpline::FINAL_STATE_REACHED) {
    // We need to enforce setting the final velocity and acceleration to zero.
    setpoint.velocity.setZero();
    setpoint.acceleration.setZero();
    return OkStatus();
  }
  if (spline_status_ != control::CartesianBSpline::WORKING) {
    return InternalError("The cartesian bspline is not working.");
  }
  if (!spline_.ComputeSetpoint(&setpoint)) {
    return InternalError("Cartesian bspline failed to compute the setpoint.");
  }
  return OkStatus();
}

RealtimeStatus CartesianBSplineSetpointGenerator::SetReferencePoints(
    const CartesianLimits& cartesian_limits,
    const std::vector<Pose3d>& reference_targets) {
  if (reference_targets.empty()) {
    return FailedPreconditionError("No data points in trajectory.");
  }
  if (reference_targets.size() > kMaxNumberControlPoints) {
    return FailedPreconditionError("Too many data points in trajectory.");
  }
  spline_.ClearTrajectory();
  if (!spline_.SetLimits(cartesian_limits)) {
    return InternalError("Setting cartesian limits for bspline failed.");
  }
  CartStatePVA last_target;
  last_target.pose = reference_targets.front();
  last_target.velocity.setZero();
  last_target.acceleration.setZero();

  if (!spline_.SetControlPoints(reference_targets, last_target)) {
    return InternalError(
        "Setting cartesian control points for bspline failed.");
  }
  spline_status_ = spline_.GetStatus();
  if (spline_status_ != control::CartesianBSpline::WORKING) {
    return InternalError("The cartesian bspline failed to initialize.");
  }
  return OkStatus();
}

bool CartesianBSplineSetpointGenerator::LastPointReached() const {
  return spline_status_ == control::CartesianBSpline::FINAL_STATE_REACHED;
}

FilterSetpointGenerator::FilterSetpointGenerator(
    double low_pass_filter_gain, double position_error_threshold,
    double orientation_error_threshold)
    : low_pass_filter_gain_(low_pass_filter_gain),
      position_error_threshold_(position_error_threshold),
      orientation_error_threshold_(orientation_error_threshold) {}

/*static*/ absl::StatusOr<std::unique_ptr<FilterSetpointGenerator>>
FilterSetpointGenerator::Create(double low_pass_filter_gain,
                                double position_error_threshold,
                                double orientation_error_threshold) {
  if (low_pass_filter_gain <= 0 || low_pass_filter_gain > 1.0) {
    return absl::InvalidArgumentError(
        "low_pass_filter_gain must be > 0 and <= 1.0");
  }
  if (position_error_threshold <= 0) {
    return absl::InvalidArgumentError("position_error_threshold must be > 0");
  }
  if (orientation_error_threshold <= 0) {
    return absl::InvalidArgumentError(
        "orientation_error_threshold must be > 0");
  }
  // Employing Wrap Unique due to private constructor.
  return absl::WrapUnique(new FilterSetpointGenerator(
      low_pass_filter_gain, position_error_threshold,
      orientation_error_threshold));
}

RealtimeStatus FilterSetpointGenerator::ComputeSetpoint(
    CartStatePVA& setpoint) {
  if (!cartesian_limits_.has_value()) {
    return FailedPreconditionError(
        "cartesian_limits are not set. Call SetReferencePoint() first.");
  }

  // Compute a new filtered_setpoint_ if `setpoint_` is defined. If the setpoint
  // has no value the `filtered_setpoint_` is held at the value given in `Init`.
  if (setpoint_.has_value()) {
    filtered_setpoint_.pose = interpolate(
        filtered_setpoint_.pose, setpoint_->pose, low_pass_filter_gain_);

    if (!IsWithinLimits(filtered_setpoint_, *cartesian_limits_)) {
      return FailedPreconditionError(
          "setpoint is outside of cartesian_limits.");
    }
  }

  setpoint = filtered_setpoint_;
  return OkStatus();
}

RealtimeStatus FilterSetpointGenerator::SetReferencePoint(
    const CartStatePVA& setpoint) {
  if (!IsWithinLimits(setpoint, *cartesian_limits_)) {
    return FailedPreconditionError("setpoint is outside of cartesian_limits.");
  }
  setpoint_ = setpoint;

  return OkStatus();
}

RealtimeStatus FilterSetpointGenerator::Initialize(
    const CartesianLimits& cartesian_limits,
    const CartStatePVA& previous_setpoint) {
  if (!cartesian_limits.IsValid()) {
    return FailedPreconditionError("cartesian_limits are not valid.");
  }
  if (!IsWithinLimits(previous_setpoint, cartesian_limits)) {
    return FailedPreconditionError("setpoint is outside of cartesian_limits.");
  }
  cartesian_limits_ = cartesian_limits;
  filtered_setpoint_ = previous_setpoint;
  setpoint_.reset();
  return OkStatus();
}

bool FilterSetpointGenerator::LastPointReached() const {
  // Returns true only if at least one setpoint was received.
  if (!setpoint_.has_value()) return false;
  eigenmath::Vector3d position_error =
      setpoint_->pose.translation() - filtered_setpoint_.pose.translation();
  eigenmath::SO3d orientation_error =
      eigenmath::SO3d(setpoint_->pose.quaternion().inverse() *
                      filtered_setpoint_.pose.quaternion());
  return position_error.array().abs().maxCoeff() < position_error_threshold_ &&
         (eigenmath::logSO3(orientation_error).array().abs().maxCoeff() <
          orientation_error_threshold_);
}

}  // namespace intrinsic::icon

#endif  // INTRINSIC_ICON_CONTROL_ALGORITHMS_CARTESIAN_RANGEFINDER_DISTANCE_SETPOINT_GENERATOR_CC_
