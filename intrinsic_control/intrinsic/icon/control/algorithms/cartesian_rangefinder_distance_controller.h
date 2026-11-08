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

#ifndef INTRINSIC_ICON_CONTROL_ALGORITHMS_CARTESIAN_RANGEFINDER_DISTANCE_CONTROLLER_H_
#define INTRINSIC_ICON_CONTROL_ALGORITHMS_CARTESIAN_RANGEFINDER_DISTANCE_CONTROLLER_H_

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

#include "Eigen/Core"
#include "absl/status/statusor.h"
#include "intrinsic/eigenmath/so3.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/control/algorithms/cartesian_rangefinder_distance_setpoint_generator.h"
#include "intrinsic/icon/control/algorithms/settling_time_counter.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/icon/utils/realtime_status_or.h"
#include "intrinsic/kinematics/chain.h"
#include "intrinsic/kinematics/types/cart_state.h"
#include "intrinsic/kinematics/types/cartesian_limits.h"
#include "intrinsic/kinematics/types/joint_limits.h"
#include "intrinsic/kinematics/types/joint_state.h"
#include "intrinsic/math/pose3.h"

namespace intrinsic::icon {

struct CartesianRangefinderDistanceControllerErrorState {
  // The position error in the order: x y z
  double position_error_in_tcp_frame[3];

  // TODO(b/269410815): Refactor this to use log-maps to improve the
  // visualization.
  //  The orientation error as quaternion in the order: x y z w
  double orientation_error_in_tcp_frame[4];
  // The eef pose in the order x y z r p y
  double pose_in_base_frame[6];
  // The eef twist in the order vx vy vz rx ry rz
  double twist_in_base_frame[6];
};

// Cartesian linear feedback controller for
// * tracking a reference pose trajectory (spline) and
// * simultaneously controlling a desired distance from the TCP based on sensor
// readings.
// The control inputs are joint positions.
class CartesianRangefinderDistanceController {
 public:
  // Define tolerance for numeric sanity checks.
  static constexpr double kNumericPrecision = 1e-8;

  // User-provided input parameters, to be handed over from a calling
  // action. Optional parameters will either be automatically derived or, in
  // case they are general, set to predetermined values.
  struct InputParameters {
    // The desired offset of the TCP frame from the workpiece in z-direction. A
    // primary control objective.
    double z_reference_offset_tcp_frame;
    // If the sensed distance between TCP and workpiece surpasses this value,
    // return an error status.
    std::optional<double> z_reference_offset_safety_threshold;
    // Task-space PD gain matrices, expressed in TCP frame.
    eigenmath::Matrix6d cartesian_stiffness_tcp_frame;
    std::optional<eigenmath::Matrix6d> cartesian_damping_tcp_frame;
    // Desired joint configuration for nullspace motion. The controller will
    // minimize the distance to that position with lower priority.
    std::optional<eigenmath::VectorXd> joint_position_nullspace;
    // Nullspace feedback gains (in joint coordinates).
    std::optional<double> nullspace_stiffness;
    std::optional<double> nullspace_damping;
    JointLimits joint_limits;
    CartesianLimits cartesian_limits;
    std::optional<double> low_pass_filter_gain;
    std::optional<double> jacobian_pinv_damping;
  };

  // The state is defined as 'dynamic' quantities getting low-pass filtered or
  // interpolated.
  struct State {
    static State Zero(int njoints) {
      return {
          .z_reference_offset_tcp_frame = 0.0,
          .joint_position_nullspace = eigenmath::VectorNd::Zero(njoints),
          .cartesian_stiffness_tcp_frame = eigenmath::Matrix6d::Identity(),
          .cartesian_damping_tcp_frame = eigenmath::Matrix6d::Identity(),
          .nullspace_stiffness = 0.0,
          .nullspace_damping = 0.0,
      };
    }

    double z_reference_offset_tcp_frame;
    eigenmath::VectorNd joint_position_nullspace;
    eigenmath::Matrix6d cartesian_stiffness_tcp_frame;
    eigenmath::Matrix6d cartesian_damping_tcp_frame;
    double nullspace_stiffness;
    double nullspace_damping;
  };

  // Parameters are quantities that are applied directly without filtering.
  // Low-pass filtering is omitted due to safety (hard constraints) or due to
  // lack of effect.
  struct Parameters {
    double low_pass_filter_gain;
    double jacobian_pinv_damping;
    JointLimits joint_limits;
    double z_reference_offset_safety_threshold;
  };

  // The error state represents the control error, used to compute control
  // signals and for convergence checking.
  struct ErrorState {
    eigenmath::Vector3d position_error_in_tcp_frame;
    eigenmath::SO3d orientation_error_in_tcp_frame;
    eigenmath::Vector6d pose_in_base_frame;
    eigenmath::Vector6d twist_in_base_frame;
  };

  CartesianRangefinderDistanceController() = delete;

  // Creates instance of the controller, fails in case of zero number of joints.
  static absl::StatusOr<std::unique_ptr<CartesianRangefinderDistanceController>>
  Create(size_t njoints, const Pose3d& flange_t_rangefinder,
         const Pose3d& flange_t_tcp, double frequency,
         SetpointGenerator& setpoint_generator);

  // Sets a new reference to the controller. Performs  elementary checks on the
  // validity of supplied parameters and returns an error in case of
  // inconsistencies.
  RealtimeStatus SetReference(const InputParameters& input,
                              const JointStatePVA& joint_state);

  // Evaluates feedback law and returns joint position control setpoints.
  RealtimeStatusOr<JointStatePVA> ComputeControl(
      double rangefinder_sensed_distance, const kinematics::Chain& chain);

  // Reset internal filter state to the current reference.
  void ResetFilterToReference();

  const State& GetFilterState() const { return filter_state_; }

  const State& GetReference() const { return reference_; }

  const ErrorState& GetErrorState() const { return error_state_; }

  // Returns true when the current error state and joint velocity fall below the
  // user-defined threshold and the entire trajectory has been played back,
  // returns false otherwise.
  bool ReferenceReached(const JointStatePVA& joint_state,
                        double position_error_threshold,
                        double orientation_error_threshold,
                        double translational_velocity_threshold,
                        double rotational_velocity_threshold,
                        double joint_velocity_threshold) const;

  // Returns time in seconds for how long the robot state has been settled, i.e.
  // velocities are below user-defined thresholds. In case the robot gets
  // "unsettled", i.e. moved, the counter returns negative cycle time,
  // -1.0/frequency_hz.
  // has to be called every control cycle.
  double UpdateSettlingTime(const JointStateV& joint_velocity,
                            double translational_velocity_threshold,
                            double rotational_velocity_threshold,
                            double joint_velocity_threshold);

 private:
  CartesianRangefinderDistanceController(size_t njoints,
                                         const Pose3d& flange_t_rangefinder,
                                         const Pose3d& flange_t_tcp,
                                         double frequency,
                                         SetpointGenerator& setpoint_generator);

  // Propagates a discretized lowpass filter used for interpolation between
  // current state and reference state. Filter is time-agnostic and depends on
  // the controller frequency.
  static void StepLowpassFilter(const State& reference,
                                const Parameters& params, State& filter_state);

  size_t njoints_;
  const Pose3d flange_t_rangefinder_;
  const Pose3d flange_t_tcp_;
  Parameters parameters_;
  State reference_;
  State filter_state_;
  ErrorState error_state_;
  JointStatePVA joint_state_command_;

  CartStatePVA base_t_last_commanded_;

  // Update frequency of the control loop.
  double frequency_;

  SetpointGenerator& setpoint_generator_;
  SettlingTimeCounter settling_time_counter_;
};

}  // namespace intrinsic::icon

#endif  // INTRINSIC_ICON_CONTROL_ALGORITHMS_CARTESIAN_RANGEFINDER_DISTANCE_CONTROLLER_H_
