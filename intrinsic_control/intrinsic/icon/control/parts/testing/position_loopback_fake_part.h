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

#ifndef INTRINSIC_ICON_CONTROL_PARTS_TESTING_POSITION_LOOPBACK_FAKE_PART_H_
#define INTRINSIC_ICON_CONTROL_PARTS_TESTING_POSITION_LOOPBACK_FAKE_PART_H_

#include <cstddef>

#include "intrinsic/icon/control/parts/fake_feature_interfaces.h"
#include "intrinsic/icon/control/parts/realtime_part_interface.h"
#include "intrinsic/icon/control/parts/testing/fake_realtime_part.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/kinematics/types/joint_limits.h"

namespace intrinsic::icon {

// PositionLoopbackFakePart bundles the FakeJointPosition, FakeJointSensor and
// JointLimitsInterface PartInterfaces and implements loopback behavior (i.e.
// setpoints provided via FakeJointPosition are reflected in
// FakeJointPositionSensor in the following cycle). Note that the
// JointLimitsInterface are not enforced.
//
// Also has a JointVelocityEstimator fake, which always returns zero velocity,
// but is helpful to get Actions working.
//
// FakeRealtimePart implements the RealtimePart state machine in a simplified
// way (all transitions immediately succeed), and provides an additional
// InduceFault() method.
//
// This can be used to test Application Layer clients by constructing an
// ActionController with this part and any required Actions.
class PositionLoopbackFakePart final
    : public FakeRealtimePart<FakeJointPosition, FakeJointPositionSensor,
                              FakeJointVelocityEstimator,
                              FakeJointAccelerationEstimator, FakeJointLimits,
                              FakeMoveOk, FakePayloadState> {
 public:
  // Constructs a PositionLoopbackFakePart whose JointLimitsInterface interface
  // reports infinite limits for all joints.
  explicit PositionLoopbackFakePart(size_t ndof);

  // Constructs a PositionLoopbackFakePart whose JointLimitsInterface interface
  // reports the given joint limits.
  PositionLoopbackFakePart(size_t ndof, const JointLimits& default_limits,
                           const JointLimits& maximum_limits);

  // The joint command/sensor interfaces are exposed as const because the
  // loopback behavior is handled internally.
  // Use these to verify joint angle expectations.
  const FakeJointPosition& JointPositionInterface() const;

  const FakeJointPositionSensor& JointPositionSensorInterface() const;

  // Sets joint position and velocity to all zeroes, and re-initializes the
  // FakeTransitionHandler from the base class to kUnknown.
  void Reset();

 private:
  size_t ndof_;

  static RealtimeStatus ReadStatusImpl(
      RealtimePartInterface::ReadStatusParameters params,
      FakeJointPosition& jpos, FakeJointPositionSensor& jsense,
      FakeJointVelocityEstimator& jvel_estimator,
      FakeJointAccelerationEstimator& jacc_estimator, FakeJointLimits& jlimits,
      FakeMoveOk& moveok, FakePayloadState& payload_state);
};

}  // namespace intrinsic::icon

#endif  // INTRINSIC_ICON_CONTROL_PARTS_TESTING_POSITION_LOOPBACK_FAKE_PART_H_
