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

#ifndef INTRINSIC_ICON_CONTROL_ALGORITHMS_CARTESIAN_RANGEFINDER_DISTANCE_SETPOINT_GENERATOR_H_
#define INTRINSIC_ICON_CONTROL_ALGORITHMS_CARTESIAN_RANGEFINDER_DISTANCE_SETPOINT_GENERATOR_H_

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

#include "absl/status/statusor.h"
#include "intrinsic/icon/control/algorithms/cartesian_bspline.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/kinematics/types/cart_state.h"
#include "intrinsic/kinematics/types/cartesian_limits.h"
#include "intrinsic/math/pose3.h"

namespace intrinsic::icon {

class SetpointGenerator {
 public:
  virtual ~SetpointGenerator() = default;

  // Writes the next setpoint to `setpoint`. It automatically steps to the next
  // timestep. If the call returns the setpoint at step n the next call will
  // return step n+1. This stepping depends on the used SetpointGenerator child
  // class and is configured there.
  virtual RealtimeStatus ComputeSetpoint(CartStatePVA& setpoint) = 0;

  // True if the last point of the trajectory is reached.
  virtual bool LastPointReached() const = 0;
};

class CartesianBSplineSetpointGenerator : public SetpointGenerator {
 public:
  // Create a CartesianBSplineSetpointGenerator which is called at a fixed
  // frequency.
  static absl::StatusOr<std::unique_ptr<CartesianBSplineSetpointGenerator>>
  Create(double frequency);

  // Maximum number of accepted poses.
  static constexpr size_t kMaxNumberControlPoints = 8192;

  RealtimeStatus ComputeSetpoint(CartStatePVA& setpoint) override;

  // Define ths Cartesian b-Spline through the reference_targets. The spline
  // follows the given cartesian_limits.
  RealtimeStatus SetReferencePoints(
      const CartesianLimits& cartesian_limits,
      const std::vector<Pose3d>& reference_targets);

  bool LastPointReached() const override;

 private:
  CartesianBSplineSetpointGenerator(double frequency, size_t num_points);

  CartesianBSplineSetpointGenerator() = delete;

  control::CartesianBSpline spline_;
  int spline_status_;
};

class FilterSetpointGenerator : public SetpointGenerator {
 public:
  // Create a FilterSetpointGenerator with a given `low_pass_filter_gain` used
  // to filter the reference pose.
  static absl::StatusOr<std::unique_ptr<FilterSetpointGenerator>> Create(
      double low_pass_filter_gain, double position_error_threshold,
      double orientation_error_threshold);

  RealtimeStatus ComputeSetpoint(CartStatePVA& setpoint) override;

  // Reset the setpoint and the filtered_setpoint to the `previous_setpoint`.
  RealtimeStatus Initialize(const CartesianLimits& cartesian_limits,
                            const CartStatePVA& previous_setpoint);

  // Set the reference setpoint.
  RealtimeStatus SetReferencePoint(const CartStatePVA& setpoint);

  bool LastPointReached() const override;

 private:
  FilterSetpointGenerator(double low_pass_filter_gain,
                          double position_error_threshold,
                          double orientation_error_threshold);
  const double low_pass_filter_gain_;
  CartStatePVA filtered_setpoint_;
  std::optional<CartStatePVA> setpoint_;
  std::optional<CartesianLimits> cartesian_limits_;
  const double position_error_threshold_;
  const double orientation_error_threshold_;
};

}  // namespace intrinsic::icon

#endif  // INTRINSIC_ICON_CONTROL_ALGORITHMS_CARTESIAN_RANGEFINDER_DISTANCE_SETPOINT_GENERATOR_H_
