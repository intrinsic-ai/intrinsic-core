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

#include "intrinsic/icon/control/parts/testing/position_loopback_fake_part.h"

#include <stddef.h>

#include "absl/log/check.h"
#include "intrinsic/icon/control/joint_position_command.h"
#include "intrinsic/icon/control/parts/fake_feature_interfaces.h"
#include "intrinsic/icon/control/parts/realtime_part_interface.h"
#include "intrinsic/icon/control/parts/testing/fake_realtime_part.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/icon/utils/realtime_status_or.h"
#include "intrinsic/kinematics/types/joint_limits.h"
#include "intrinsic/kinematics/types/joint_state.h"

namespace intrinsic::icon {

PositionLoopbackFakePart::PositionLoopbackFakePart(size_t ndof)
    : FakeRealtimePart<FakeJointPosition, FakeJointPositionSensor,
                       FakeJointVelocityEstimator,
                       FakeJointAccelerationEstimator, FakeJointLimits,
                       FakeMoveOk, FakePayloadState>(
          FakeJointPosition(ndof), FakeJointPositionSensor(ndof),
          FakeJointVelocityEstimator(ndof),
          FakeJointAccelerationEstimator(ndof),
          FakeJointLimits(JointLimits::Unlimited(ndof).value(),
                          JointLimits::Unlimited(ndof).value()),
          FakeMoveOk(), FakePayloadState(),
          /*read_status_callback=*/
          &PositionLoopbackFakePart::ReadStatusImpl),
      ndof_(ndof) {
  Reset();
}

PositionLoopbackFakePart::PositionLoopbackFakePart(
    size_t ndof, const JointLimits& default_limits,
    const JointLimits& maximum_limits)
    : FakeRealtimePart<FakeJointPosition, FakeJointPositionSensor,
                       FakeJointVelocityEstimator,
                       FakeJointAccelerationEstimator, FakeJointLimits,
                       FakeMoveOk, FakePayloadState>(
          FakeJointPosition(ndof), FakeJointPositionSensor(ndof),
          FakeJointVelocityEstimator(ndof),
          FakeJointAccelerationEstimator(ndof),
          FakeJointLimits(default_limits, maximum_limits), FakeMoveOk(),
          FakePayloadState(),
          /*read_status_callback=*/
          &PositionLoopbackFakePart::ReadStatusImpl),
      ndof_(ndof) {
  Reset();
}

const FakeJointPosition& PositionLoopbackFakePart::JointPositionInterface()
    const {
  return GetInterface<FakeJointPosition>();
}

const FakeJointPositionSensor&
PositionLoopbackFakePart::JointPositionSensorInterface() const {
  return GetInterface<FakeJointPositionSensor>();
}

void PositionLoopbackFakePart::Reset() {
  JointStateP joints;
  CHECK_OK(joints.SetSize(ndof_));
  GetInterface<FakeJointPositionSensor>().SetSensedPosition(joints);
  (void)GetInterface<FakeJointPosition>().SetPositionSetpoints(
      JointPositionCommand(joints.position));

  JointStateV zero_velocity;
  CHECK_OK(zero_velocity.SetSize(
      GetInterface<FakeJointLimits>().GetApplicationLimits().size()));
  GetInterface<FakeJointVelocityEstimator>().SetVelocityEstimate(zero_velocity);

  JointStateA zero_acceleration;
  CHECK_OK(zero_acceleration.SetSize(
      GetInterface<FakeJointLimits>().GetApplicationLimits().size()));
  GetInterface<FakeJointAccelerationEstimator>().SetAccelerationEstimate(
      zero_acceleration);
}

// Static
RealtimeStatus PositionLoopbackFakePart::ReadStatusImpl(
    RealtimePartInterface::ReadStatusParameters params, FakeJointPosition& jpos,
    FakeJointPositionSensor& jsense, FakeJointVelocityEstimator& jvel_estimator,
    FakeJointAccelerationEstimator& jacc_estimator, FakeJointLimits& jlimits,
    FakeMoveOk& moveok, FakePayloadState& payload_state) {
  jsense.SetSensedPosition(
      JointStateP(jpos.PreviousPositionSetpoints().position()));
  return OkStatus();
}

}  // namespace intrinsic::icon
