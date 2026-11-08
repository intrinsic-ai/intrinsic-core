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

#ifndef INTRINSIC_ICON_CONTROL_C_API_EXTERNAL_ACTION_API_TESTING_LOOPBACK_FAKE_ARM_H_
#define INTRINSIC_ICON_CONTROL_C_API_EXTERNAL_ACTION_API_TESTING_LOOPBACK_FAKE_ARM_H_

#include <cstddef>
#include <optional>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/control/c_api/c_feature_interfaces.h"
#include "intrinsic/icon/control/joint_position_command.h"
#include "intrinsic/icon/proto/v1/types.pb.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/icon/utils/realtime_status_or.h"
#include "intrinsic/kinematics/types/joint_limits.h"
#include "intrinsic/kinematics/types/joint_state.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/twist.h"

namespace intrinsic::icon {

// Implements a fake robot arm that has six degrees of freedom, as well as a
// fake force/torque sensor.
// It implements all feature interfaces that are currently available in the ICON
// plugin Action API.
//
// Sensed/Estimated position and velocity are always equal to the current
// command, and the command starts out at zero. Use SetPositionSetpoints() if
// your test requires a non-zero initial state.
//
// The Kinematics interface uses identity kinematics, i.e. each joint maps
// directly to one Cartesian dimension.
class LoopbackFakeArm final {
 public:
  static constexpr size_t kNdof = 6;
  static_assert(kNdof <= eigenmath::MAX_EIGEN_VECTOR_SIZE,
                "ICON API does not support vectors of size 6");

  // Returns a PartConfig proto for a fake arm called `name`, with the given
  // joint limits. This is the same proto that the Factory of an Action using
  // this Part receives from its ActionFactoryContext.
  static absl::StatusOr<::intrinsic_proto::icon::v1::PartConfig> GetPartConfig(
      absl::string_view name,
      std::optional<JointLimits> application_limits = std::nullopt,
      std::optional<JointLimits> system_limits = std::nullopt);

  // Returns the vtable struct for the feature interface implementations in this
  // fake part.
  static IntrinsicIconFeatureInterfaceVtable GetFeatureInterfaceVtable();

  // Returns an IntrinsicIconFeatureInterfacesForSlot struct populated with the
  // feature interface pointers for `arm`. Pass these pointers to the functions
  // in the vtable you get from `GetFeatureInterfaceVtable()` above to use them.
  //
  // That said, don't do that directly, Use ActionTestHelper instead, which.
  static IntrinsicIconFeatureInterfacesForSlot
  MakeIntrinsicIconFeatureInterfacesForSlot(LoopbackFakeArm* arm);

  // Returns an IntrinsicIconConstFeatureInterfacesForSlot struct populated with
  // the feature interface pointers for `arm`. Pass these pointers to the
  // functions in the vtable you get from `GetFeatureInterfaceVtable()` above to
  // use them.
  //
  // That said, don't do that directly, use ActionTestHelper instead.
  static IntrinsicIconConstFeatureInterfacesForSlot
  MakeIntrinsicIconConstFeatureInterfacesForSlot(const LoopbackFakeArm* arm);

  // Creates a new LoopbackFakeArm with unlimited application and system joint
  // limits.
  // If your test requires specific joint limits, make sure to call
  // `SetApplicationLimits()` and/or `SetSystemLimits()` to set them!
  LoopbackFakeArm();

  // Updates the application limits.
  //
  // Returns InvalidArgumentError if `application_limits` has anything but 6
  // degrees of freedom.
  absl::Status SetApplicationLimits(const JointLimits& application_limits);

  // Updates the system limits.
  //
  // Returns InvalidArgumentError if `system_limits` has anything but 6 degrees
  // of freedom.
  absl::Status SetSystemLimits(const JointLimits& system_limits);

  // Updates the reported wrench of the IconForceTorqueSensorInterface.
  void SetWrenchAtTip(const Wrench& wrench_at_tip);

  // Updates the position setpoints, with optional velocity and acceleration
  // feedforwards.
  //
  // Returns InvalidArgumentError if `setpoints` has anything but 6 degrees of
  // freedom.
  RealtimeStatus SetPositionSetpoints(const JointPositionCommand& setpoints);

  // Returns the JointPositionCommand that was last passed to
  // SetPositionSetpoints.
  JointPositionCommand PreviousPositionSetpoints() const;

  // Returns the current sensed position. This is equivalent to whatever
  // position was last passed to SetPositionSetpoints().
  JointStateP GetSensedPosition() const;

  // Returns the current velocity. This is equivalent to whatever velocity
  // feedforward was last passed to SetPositionSetpoints(), or zero if there was
  // no velocity feedforward.
  JointStateV GetVelocityEstimate() const;

  // Returns the current application limits. Update these in your test code
  // using SetApplicationLimmits().
  JointLimits GetApplicationLimits() const;

  // Returns the current system limits. Update these in your test code using
  // SetSystemLimits().
  JointLimits GetSystemLimits() const;

  // Returns the current wrench. Update this in your test code
  // using SetWrenchAtTip().
  Wrench WrenchAtTip() const;

  // Tares the F/T sensor – this resets the internal wrench value back to
  // all-zero.
  RealtimeStatus Tare();

  // Runs identity kinematics on `dof_positions` and returns a Pose3d. The
  // identity kinematics map the six joints of `dof_position` to [x, y, z, roll,
  // pitch, yaw] (in that order).
  //
  // Returns InvalidArgumentError if `dof_positions` has anything but 6 degrees
  // of freedom.
  RealtimeStatusOr<Pose3d> ComputeChainFK(
      const JointStateP& dof_positions) const;

  // Returns a 6x6 identity matrix regardless of the values in `dof_positions`,
  // as long as `dof_position` has exactly 6 degrees of freedom.
  //
  // Returns InvalidArgumentError if `dof_positions` has anything but 6 degrees
  // of freedom.
  RealtimeStatusOr<eigenmath::Matrix6Nd> ComputeChainJacobian(
      const JointStateP& dof_positions) const;

 private:
  JointPositionCommand current_setpoints_;
  JointLimits application_limits_;
  JointLimits system_limits_;
  Wrench current_wrench_at_tip_;
};

}  // namespace intrinsic::icon

#endif  // INTRINSIC_ICON_CONTROL_C_API_EXTERNAL_ACTION_API_TESTING_LOOPBACK_FAKE_ARM_H_
