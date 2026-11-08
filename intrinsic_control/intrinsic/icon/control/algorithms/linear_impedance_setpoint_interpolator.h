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

#ifndef INTRINSIC_ICON_CONTROL_ALGORITHMS_LINEAR_IMPEDANCE_SETPOINT_INTERPOLATOR_H_
#define INTRINSIC_ICON_CONTROL_ALGORITHMS_LINEAR_IMPEDANCE_SETPOINT_INTERPOLATOR_H_

#include <memory>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "intrinsic/icon/actions/cartesian_impedance_parameters.h"
#include "intrinsic/icon/actions/impedance_trajectory_generators.pb.h"
#include "intrinsic/icon/control/algorithms/cartesian_impedance_commons.h"
#include "intrinsic/icon/control/algorithms/cartesian_impedance_trajectory.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/icon/utils/realtime_status_or.h"
#include "intrinsic/kinematics/types/cart_state.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/symmetric_positive_definite_matrix_squareroot.h"

namespace intrinsic::icon {

struct ImpedanceTrajectory {
  std::vector<absl::Duration> time_stamps;
  std::vector<cartesian_impedance::CartesianTarget> impedance_waypoints;
};

absl::StatusOr<ImpedanceTrajectory> FromProto(
    const intrinsic_proto::icon::actions::proto::
        LinearImpedanceSetpointInterpolator& proto);

absl::StatusOr<
    intrinsic_proto::icon::actions::proto::LinearImpedanceSetpointInterpolator>
ToProto(const ImpedanceTrajectory& params);

// A Cartesian impedance trajectory that interpolates between a set of
// Cartesian impedance waypoints in a linear fashion. Interpolation for positive
// definite gain matrices is done on the manifold of SPD matrices.
class LinearCartesianImpedanceSetpointInterpolator
    : public CartesianImpedanceTrajectoryInterface {
 public:
  // Creates a new Cartesian impedance trajectory that interpolates between the
  // given waypoints in a linear fashion.
  //
  // The interpolation time step is the time step at which the trajectory is
  // evaluated. The `impedance_waypoints` are define the impedance setpoints
  // that are reached at the respective `time_stamps`.
  //
  // Returns an error if the time stamps are not sorted in ascending order, or
  // if the number of time stamps is not equal to the number of impedance
  // waypoints.
  static absl::StatusOr<std::unique_ptr<CartesianImpedanceTrajectoryInterface>>
  Create(const ImpedanceTrajectory& impedance_trajectory,
         absl::Duration interpolation_time_step);

  RealtimeStatus Initialize(
      const CartStatePVA& base_t_target_last_commanded) override;

  // Returns the current Cartesian impedance setpoint.
  // Note: the wrench selection matrices (which are orthogonal projection
  // matrices) are not interpolated, just returned as constant matrices
  // according to the time progression.
  RealtimeStatusOr<cartesian_impedance::RealTimeCartesianTarget> Step(
      double speed_override) override;

  // TODO(b/379261850): it is an antipattern that this needs to be available
  // before the call to Initialize().
  const Pose3d& GetInitialRobotTipTRobotTool() const override;

  // Returns the phase of the trajectory, ranging from 0 to 1.
  double GetPhase() const override;

 private:
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
          damping_matrix_interpolations);

  absl::Duration interpolation_time_step_ = absl::ZeroDuration();
  absl::Duration time_since_trajectory_start_ = absl::ZeroDuration();

  // The setpoint trajectory with N+1 discrete waypoints.
  const std::vector<absl::Duration> time_stamps_;
  const std::vector<cartesian_impedance::RealTimeCartesianTarget>
      rt_cartesian_targets_;

  // Matrix interpolation containers with N discrete waypoints.
  const std::vector<EntropyRegulatedWassersteinDistanceInterpolation>
      cartesian_inertia_inverse_interpolations_;
  const std::vector<EntropyRegulatedWassersteinDistanceInterpolation>
      cartesian_stiffness_interpolations_;
  const std::vector<EntropyRegulatedWassersteinDistanceInterpolation>
      cartesian_damping_interpolations_;
};

}  // namespace intrinsic::icon

#endif  // INTRINSIC_ICON_CONTROL_ALGORITHMS_LINEAR_IMPEDANCE_SETPOINT_INTERPOLATOR_H_
