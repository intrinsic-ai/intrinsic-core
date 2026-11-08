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

#include "intrinsic/icon/control/c_api/external_action_api/sine_wave_plugin_action.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstddef>
#include <memory>
#include <optional>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "google/protobuf/any.pb.h"
#include "intrinsic/icon/cc_client/condition.h"
#include "intrinsic/icon/control/c_api/external_action_api/testing/action_test_helper.h"
#include "intrinsic/icon/control/c_api/external_action_api/testing/loopback_fake_arm.h"
#include "intrinsic/icon/utils/realtime_status_or.h"

namespace intrinsic::icon {
namespace {

constexpr size_t kNdof = 6;
constexpr double kFrequencyHz = 1.0;

using ::testing::DoubleEq;
using ::testing::DoubleNear;
using ::testing::Each;
using ::testing::Pointwise;
using ::testing::VariantWith;

SineWavePluginAction::ParameterProto ValidParams(size_t ndof) {
  SineWavePluginAction::ParameterProto params;
  for (int i = 0; i < ndof; ++i) {
    auto* joint = params.mutable_joints()->Add();
    joint->set_amplitude_rad(i);
    joint->set_frequency_hz(0.25);
  }
  return params;
}

TEST(SineWavePluginAction, CreateFailsWithoutSlot) {
  ActionTestHelper test_helper(/*control_frequency_hz=*/kFrequencyHz,
                               SineWavePluginAction::GetSignature());
  EXPECT_EQ(test_helper.CreateAction<SineWavePluginAction>(ValidParams(kNdof))
                .status()
                .code(),
            absl::StatusCode::kNotFound);
}

TEST(SineWavePluginAction, CreateSucceeds) {
  ActionTestHelper test_helper(/*control_frequency_hz=*/kFrequencyHz,
                               SineWavePluginAction::GetSignature());
  EXPECT_TRUE(test_helper.slot_map()
                  .AddLoopbackFakeArmSlot(SineWavePluginAction::kSlotName)
                  .ok());
  EXPECT_TRUE(
      test_helper.CreateAction<SineWavePluginAction>(ValidParams(kNdof)).ok());
}

TEST(SineWavePluginAction, Works) {
  ActionTestHelper test_helper(/*control_frequency_hz=*/kFrequencyHz,
                               SineWavePluginAction::GetSignature());
  absl::StatusOr<LoopbackFakeArm*> fake_arm_or =
      test_helper.slot_map().AddLoopbackFakeArmSlot(
          SineWavePluginAction::kSlotName);
  ASSERT_TRUE(fake_arm_or.ok());
  LoopbackFakeArm& fake_arm = *fake_arm_or.value();

  absl::StatusOr<std::unique_ptr<SineWavePluginAction>> action_or =
      test_helper.CreateAction<SineWavePluginAction>(ValidParams(kNdof));
  ASSERT_TRUE(action_or.ok());
  SineWavePluginAction& action = *action_or.value();

  EXPECT_TRUE(test_helper.EnterAction(action).ok());

  // Action has not published a streaming output yet.
  {
    absl::StatusOr<std::optional<google::protobuf::Any>> streaming_output =
        test_helper.streaming_io_registry().GetLatestOutput();
    ASSERT_TRUE(streaming_output.ok());
    EXPECT_FALSE(streaming_output->has_value());
  }

  // The initial sense/control step initializes the Action and zeroes the time.
  EXPECT_TRUE(test_helper.SenseAndControlAction(action).ok());

  {
    absl::StatusOr<std::optional<google::protobuf::Any>> streaming_output =
        test_helper.streaming_io_registry().GetLatestOutput();
    ASSERT_TRUE(streaming_output.ok());
    ASSERT_TRUE(streaming_output->has_value());
    SineWavePluginAction::StreamingOutputProto streaming_output_proto;
    ASSERT_TRUE(streaming_output.value()->UnpackTo(&streaming_output_proto));
    EXPECT_EQ(streaming_output_proto.seconds(), 0);
  }
  // Step three times. Before the first step, the joints should be at 0, and
  // afterwards they should be at params->amplitudes[i], then 0, then
  // -params->amplitude[i], and so on.
  EXPECT_THAT(fake_arm.PreviousPositionSetpoints().position(),
              Each(DoubleEq(0.)));
  EXPECT_TRUE(test_helper.SenseAndControlAction(action).ok());

  EXPECT_TRUE(test_helper.streaming_io_registry().GetLatestOutput().ok());
  EXPECT_THAT(fake_arm.PreviousPositionSetpoints().position(),
              Pointwise(DoubleEq(), {0, 1, 2, 3, 4, 5}));

  EXPECT_TRUE(test_helper.SenseAndControlAction(action).ok());

  EXPECT_THAT(fake_arm.PreviousPositionSetpoints().position(),
              Each(DoubleNear(0., 1e-10)));

  EXPECT_TRUE(test_helper.SenseAndControlAction(action).ok());

  EXPECT_THAT(fake_arm.PreviousPositionSetpoints().position(),
              Pointwise(DoubleEq(), {0, -1, -2, -3, -4, -5}));

  EXPECT_TRUE(test_helper.SenseAndControlAction(action).ok());

  EXPECT_THAT(fake_arm.PreviousPositionSetpoints().position(),
              Each(DoubleNear(0., 1e-10)));
}

TEST(SineWavePluginAction, EchoesStreamingInputInStateVariable) {
  ActionTestHelper test_helper(/*control_frequency_hz=*/kFrequencyHz,
                               SineWavePluginAction::GetSignature());
  // Discard the fake arm pointer – we don't use it in this test.
  ASSERT_TRUE(test_helper.slot_map()
                  .AddLoopbackFakeArmSlot(SineWavePluginAction::kSlotName)
                  .ok());

  absl::StatusOr<std::unique_ptr<SineWavePluginAction>> action_or =
      test_helper.CreateAction<SineWavePluginAction>(ValidParams(kNdof));
  ASSERT_TRUE(action_or.ok());
  SineWavePluginAction& action = *action_or.value();

  EXPECT_TRUE(test_helper.EnterAction(action).ok());

  SineWavePluginAction::StreamingInputProto streaming_input_proto;
  streaming_input_proto.set_value(1.23);
  absl::StatusOr<float> parsed_streaming_input =
      test_helper.streaming_io_registry().InvokeInputParser<float>(
          SineWavePluginAction::kStreamingInputName, streaming_input_proto);
  ASSERT_TRUE(parsed_streaming_input.ok());
  EXPECT_EQ(parsed_streaming_input.value(), streaming_input_proto.value());

  EXPECT_TRUE(test_helper.SenseAndControlAction(action).ok());

  RealtimeStatusOr<StateVariableValue> state_variable =
      action.GetStateVariable(SineWavePluginAction::kStateVariableNumber);
  ASSERT_TRUE(state_variable.ok());
  EXPECT_THAT(state_variable.value(),
              VariantWith<double>(streaming_input_proto.value()));
}

}  // namespace
}  // namespace intrinsic::icon
