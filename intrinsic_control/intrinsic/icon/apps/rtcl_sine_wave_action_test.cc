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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstddef>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "google/protobuf/duration.pb.h"
#include "internal/testing.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/apps/sine_wave_action.pb.h"
#include "intrinsic/icon/apps/sine_wave_action_info.h"
#include "intrinsic/icon/apps/sine_wave_action_signature.h"
#include "intrinsic/icon/control/actions/action_test_helper.h"
#include "intrinsic/icon/control/joint_position_command.h"
#include "intrinsic/icon/control/parts/fake_feature_interfaces.h"
#include "intrinsic/icon/control/parts/testing/position_loopback_fake_part.h"
#include "intrinsic/icon/proto/v1/types.pb.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/icon/utils/realtime_status_macro.h"
#include "intrinsic/icon/utils/realtime_status_matchers.h"
#include "intrinsic/kinematics/types/joint_limits.h"
#include "intrinsic/kinematics/types/joint_state.h"
#include "intrinsic/util/proto/parse_text_proto.h"
#include "intrinsic/util/proto_time.h"

namespace intrinsic::icon {
namespace {

using ::absl_testing::StatusIs;
using ::intrinsic::ParseTextProtoOrDie;
using ::testing::AllOf;
using ::testing::DoubleEq;
using ::testing::DoubleNear;
using ::testing::Each;
using ::testing::Pointwise;
using ::testing::SizeIs;

constexpr char kPartName[] = "part";
constexpr size_t kNdof = 3;
constexpr double kFrequencyHz = 1.0;
constexpr double kMaxPos = 2.0;

class SineWaveActionTest : public ::testing::Test {
 public:
  SineWaveActionTest()
      : loopback_part_(
            kNdof, /*default_limits=*/
            CreateSimpleJointLimits(kNdof, /*max_position=*/kMaxPos,
                                    /*max_velocity=*/100,
                                    /*max_acceleration=*/1000,
                                    /*max_jerk=*/1000),
            /*maximum_limits=*/JointLimits::Unlimited(kNdof).value()) {}

  absl::StatusOr<ActionTestHelper> MakeActionWithParams(
      const SineWaveActionInfo::FixedParams& params) {
    return ActionTestHelper::Create(
        SineWaveActionInfo::kActionTypeName, params, kPartName,
        {{kPartName, ActionTestHelper::PartAndConfig{.part = loopback_part_}}},
        kFrequencyHz);
  }

  RealtimeStatus SetPosition(const eigenmath::VectorNd& joints) {
    INTRINSIC_RT_RETURN_IF_ERROR(
        loopback_part_.GetInterface<FakeJointPosition>().SetPositionSetpoints(
            JointPositionCommand(joints)));
    INTRINSIC_RT_RETURN_IF_ERROR(
        loopback_part_.ReadStatusWithoutParamsTestOnly());
    return loopback_part_.ApplyCommandWithoutParamsTestOnly();
  }

  JointPositionCommand PreviousPositionSetpoints() {
    return loopback_part_.JointPositionInterface().PreviousPositionSetpoints();
  }

 protected:
  PositionLoopbackFakePart loopback_part_;
};

TEST_F(SineWaveActionTest, Create) {
  ASSERT_OK_AND_ASSIGN(auto helper,
                       MakeActionWithParams(ParseTextProtoOrDie(R"pb(
                         joints: { amplitude_rad: 0 frequency_hz: 0.25 }
                         joints: { amplitude_rad: 1 frequency_hz: 0.25 }
                         joints: { amplitude_rad: 2 frequency_hz: 0.25 }
                       )pb")));

  intrinsic_proto::icon::v1::ActionSignature signature =
      GetSineWaveActionSignature();
  for (const intrinsic_proto::icon::v1::ActionSignature::StateVariableInfo&
           state_variable_info : signature.state_variable_infos()) {
    EXPECT_THAT(
        helper.GetStateVariable(state_variable_info.state_variable_name()),
        RealtimeIsOk());
  }
}

TEST_F(SineWaveActionTest, Works) {
  EXPECT_OK(SetPosition(eigenmath::VectorNd::Zero(kNdof)));

  ASSERT_OK_AND_ASSIGN(auto helper,
                       MakeActionWithParams(ParseTextProtoOrDie(R"pb(
                         joints: { amplitude_rad: 0 frequency_hz: 0.25 }
                         joints: { amplitude_rad: 1 frequency_hz: 0.25 }
                         joints: { amplitude_rad: 2 frequency_hz: 0.25 }
                       )pb")));

  EXPECT_OK(helper.EnterAction());
  // Action has not published a streaming output yet.
  EXPECT_THAT(helper.PollStreamingOutput<google::protobuf::Duration>(),
              StatusIs(absl::StatusCode::kDeadlineExceeded));

  // The initial sense/control step initializes the Action and zeroes the time.
  EXPECT_OK(helper.SenseAndControlAction());
  EXPECT_OK(helper.PollStreamingOutput<google::protobuf::Duration>());

  // Step once for each degree of freedom. Before the first step, the joints
  // should be at 0, and afterwards they should be at params->amplitudes[i],
  // then 0, then -params->amplitude[i], and so on.
  EXPECT_THAT(PreviousPositionSetpoints().position(), Each(DoubleEq(0.)));
  EXPECT_OK(helper.SenseAndControlAction());

  EXPECT_OK(helper.PollStreamingOutput<google::protobuf::Duration>());
  EXPECT_THAT(PreviousPositionSetpoints().position(),
              Pointwise(DoubleEq(), {0, 1, 2}));

  EXPECT_OK(helper.SenseAndControlAction());

  EXPECT_THAT(PreviousPositionSetpoints().position(),
              Each(DoubleNear(0., 1e-10)));

  EXPECT_OK(helper.SenseAndControlAction());

  EXPECT_THAT(PreviousPositionSetpoints().position(),
              Pointwise(DoubleEq(), {0, -1, -2}));

  EXPECT_OK(helper.SenseAndControlAction());

  EXPECT_THAT(PreviousPositionSetpoints().position(),
              Each(DoubleNear(0., 1e-10)));
}

TEST_F(SineWaveActionTest, MaintainsContinuity) {
  JointStateP jstate;
  ASSERT_OK(jstate.SetSize(kNdof));
  jstate.position.setConstant(kMaxPos * 0.5);
  EXPECT_OK(SetPosition(jstate.position));

  ASSERT_OK_AND_ASSIGN(auto helper,
                       MakeActionWithParams(ParseTextProtoOrDie(R"pb(
                         joints: { amplitude_rad: 0 frequency_hz: 0.25 }
                         joints: { amplitude_rad: 1 frequency_hz: 0.25 }
                         joints: { amplitude_rad: 2 frequency_hz: 0.25 }
                       )pb")));

  EXPECT_OK(helper.EnterAction());
  EXPECT_OK(helper.SenseAndControlAction());

  const JointPositionCommand actual_commanded_position =
      PreviousPositionSetpoints();
  // Expect that the actual commanded position is the same as the previous
  // position command.
  EXPECT_THAT(actual_commanded_position.position().head(3),
              Pointwise(DoubleEq(), jstate.position));
}

TEST_F(SineWaveActionTest, ClampsToPositionLimits) {
  SineWaveActionInfo::FixedParams params;
  for (size_t i = 0; i < kNdof; ++i) {
    intrinsic_proto::icon::actions::SineWaveFixedParams::JointSineParams*
        joint = params.add_joints();
    joint->set_amplitude_rad(2 * kMaxPos);
    joint->set_frequency_hz(0.25);
  }
  ASSERT_OK_AND_ASSIGN(auto helper, MakeActionWithParams(params));

  EXPECT_OK(helper.EnterAction());

  int command_ticks = 0;

  // The initial sense/control step initializes the Action and zeroes the time.
  EXPECT_OK(helper.SenseAndControlAction());
  command_ticks++;
  ASSERT_THAT(PreviousPositionSetpoints().position(),
              AllOf(SizeIs(kNdof), Each(DoubleEq(0.))));

  // After another step, the sine wave reaches the maximum amplitude of 2 *
  // kMaxPos, but the Action should clamp that to kMaxPos.
  EXPECT_OK(helper.SenseAndControlAction());
  command_ticks++;

  ASSERT_THAT(PreviousPositionSetpoints().position(),
              AllOf(SizeIs(kNdof), Each(DoubleEq(kMaxPos))));

  // Another step takes us back to 0...
  EXPECT_OK(helper.SenseAndControlAction());
  command_ticks++;

  ASSERT_THAT(PreviousPositionSetpoints().position(),
              AllOf(SizeIs(kNdof), Each(DoubleNear(0., 1e-10))));

  // And yet another to the negative peak, which is clamped to negative kMaxPos.
  EXPECT_OK(helper.SenseAndControlAction());
  command_ticks++;

  ASSERT_THAT(PreviousPositionSetpoints().position(),
              AllOf(SizeIs(kNdof), Each(DoubleEq(-1 * kMaxPos))));

  // Run sense one more time to update state variables

  EXPECT_OK(helper.SenseAction());

  ASSERT_OK_AND_ASSIGN(
      auto time_since_action_start,
      helper.PollStreamingOutput<google::protobuf::Duration>());
  ASSERT_OK_AND_ASSIGN(absl::Duration duration,
                       ToAbslDuration(time_since_action_start));
  EXPECT_EQ(duration, absl::Seconds(command_ticks / kFrequencyHz));
}

}  // namespace
}  // namespace intrinsic::icon
