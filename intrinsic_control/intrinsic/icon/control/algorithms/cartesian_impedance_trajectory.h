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

#ifndef INTRINSIC_ICON_CONTROL_ALGORITHMS_CARTESIAN_IMPEDANCE_TRAJECTORY_H_
#define INTRINSIC_ICON_CONTROL_ALGORITHMS_CARTESIAN_IMPEDANCE_TRAJECTORY_H_

#include <memory>

#include "absl/status/statusor.h"
#include "intrinsic/icon/actions/cartesian_impedance.pb.h"
#include "intrinsic/icon/actions/cartesian_impedance_parameters.h"
#include "intrinsic/icon/control/algorithms/cartesian_impedance_commons.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/icon/utils/realtime_status_or.h"
#include "intrinsic/kinematics/types/cart_state.h"
#include "intrinsic/kinematics/types/joint_state.h"
#include "intrinsic/math/pose3.h"

namespace intrinsic::icon {

// Interface class for Cartesian impedance trajectory generators.
class CartesianImpedanceTrajectoryInterface {
 public:
  virtual ~CartesianImpedanceTrajectoryInterface() = default;

  // Returns the initial transform from the robot's flange to a target frame
  // where motions and forces are being commanded. This function must be
  // implemented to be available before Initialize().
  // TODO(b/379261850): resolve anti-pattern.
  virtual const Pose3d& GetInitialRobotTipTRobotTool() const = 0;

  // Initialize the trajectory generator at real-time. The last commanded
  // Cartesian state `base_t_target_last_commanded` is supplied to allow the
  // reference generator to instantaneously adapt it's trajectory to match the
  // initial state of the system and avoid discontinuities. Note that this is
  // required to be the true `base_t_target` transform including a potential
  // target frame offset `tip_t_target`!
  virtual RealtimeStatus Initialize(
      const CartStatePVA& base_t_target_last_commanded) = 0;

  // Steps along the desired impedance trajectory, must return a valid and
  // stable `RealTimeCartesianTarget` at every cycle. Do not expect the caller
  // to perform additional stability checks.
  virtual RealtimeStatusOr<cartesian_impedance::RealTimeCartesianTarget> Step(
      double speed_override) = 0;

  // Returns the trajectory execution phase, a value in [0,1].
  virtual double GetPhase() const = 0;
};

// Interface class for nullspace impedance trajectory generators. Can be used to
// define secondary tasks in the nullspace of the task defined by a
// `CartesianImpedanceTrajectoryInterface`.
class NullspaceTrajectoryInterface {
 public:
  virtual ~NullspaceTrajectoryInterface() = default;

  // Initialize the trajectory generator at real-time. The last commanded
  // joint state is supplied to allow the reference generator to
  // instantaneously adapt it's trajectory to match the initial state of the
  // system and avoid discontinuities.
  virtual RealtimeStatus Initialize(
      const JointStatePVA& joint_state_last_commanded) = 0;

  // Steps along the desired impedance trajectory, must return a valid and
  // stable `RealTimeNullspaceTarget` at every cycle. Do not expect the caller
  // to perform additional stability checks.
  virtual RealtimeStatusOr<cartesian_impedance::RealTimeNullspaceTarget> Step(
      double speed_override) = 0;

  // Returns the trajectory execution phase, a value in [0,1].
  virtual double GetPhase() const = 0;
};

// `ConstantCartesianImpedance` returns a time-invariant Cartesian impedance
// setpoint at every call to Step(). Note that constant impedance parameters do
// not imply steady state of the controlled robot, it only means that there
// realtime impedance targets remain unaltered all the time.
class ConstantCartesianImpedance
    : public CartesianImpedanceTrajectoryInterface {
 public:
  explicit ConstantCartesianImpedance(
      const cartesian_impedance::CartesianTarget& params)
      : non_rt_cartesian_target_(params) {}

  static absl::StatusOr<std::unique_ptr<CartesianImpedanceTrajectoryInterface>>
  Create(const intrinsic_proto::icon::actions::proto::CartesianTarget&
             proto_params);

  const Pose3d& GetInitialRobotTipTRobotTool() const override {
    return non_rt_cartesian_target_.robot_tip_t_robot_tool;
  }

  RealtimeStatus Initialize(
      const CartStatePVA& base_t_target_last_commanded) override;

  RealtimeStatusOr<cartesian_impedance::RealTimeCartesianTarget> Step(
      double speed_override) override;

  // Always returns 1.0.
  double GetPhase() const override { return 1.0; }

 private:
  // Hard-coded safety bounds for avoiding too discontinuous setpoints.
  static constexpr double kMaxTranslationErrorAtInitTime = 0.1;
  static constexpr double kMaxRotationErrorAtInitTime = 1.0;

  cartesian_impedance::CartesianTarget non_rt_cartesian_target_;
  cartesian_impedance::RealTimeCartesianTarget rt_cartesian_target_;
};

// `ConstantNullspaceTarget` returns a time-invariant nullspace impedance
// setpoint at every call to Step(). Note that constant impedance parameters do
// not imply steady state of the controlled robot, it only means that there
// realtime impedance targets remain unaltered all the time.
class ConstantNullspaceTarget : public NullspaceTrajectoryInterface {
 public:
  explicit ConstantNullspaceTarget(
      const cartesian_impedance::NullspaceTarget& params)
      : non_rt_nullspace_target_(params) {}

  static absl::StatusOr<std::unique_ptr<NullspaceTrajectoryInterface>> Create(
      const intrinsic_proto::icon::actions::proto::NullspaceTarget&
          proto_params);

  RealtimeStatus Initialize(
      const JointStatePVA& joint_state_last_commanded) override;

  RealtimeStatusOr<cartesian_impedance::RealTimeNullspaceTarget> Step(
      double speed_override) override;

  // Always returns 1.0.
  double GetPhase() const override { return 1.0; }

 private:
  cartesian_impedance::NullspaceTarget
      non_rt_nullspace_target_;  // Used just during Initialize().
  cartesian_impedance::RealTimeNullspaceTarget rt_nullspace_target_;
};

}  // namespace intrinsic::icon

#endif  // INTRINSIC_ICON_CONTROL_ALGORITHMS_CARTESIAN_IMPEDANCE_TRAJECTORY_H_
