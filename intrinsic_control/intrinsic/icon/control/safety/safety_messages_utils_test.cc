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

#include "intrinsic/icon/control/safety/safety_messages_utils.h"

#include <gtest/gtest.h>

#include <bitset>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "flatbuffers/buffer.h"
#include "intrinsic/icon/control/safety/extern/safety_status.fbs.h"
#include "intrinsic/icon/control/safety/safety_messages.fbs.h"

namespace intrinsic_fbs {
namespace {

TEST(SafetyStatusMessage, BuildSafetyStatusMessageCreatesFullyMutableBuffer) {
  auto ssm_buffer = BuildSafetyStatusMessage();
  SafetyStatusMessage* ssm =
      flatbuffers::GetMutableRoot<SafetyStatusMessage>(ssm_buffer.data());
  ASSERT_NE(ssm, nullptr);

  // Expect default initialization sets UNKNOWN.
  EXPECT_EQ(ssm->mode_of_safe_operation(), ModeOfSafeOperation::UNKNOWN);
  EXPECT_EQ(ssm->estop_button_status(), ButtonStatus::UNKNOWN);
  EXPECT_EQ(ssm->enable_button_status(), ButtonStatus::UNKNOWN);

  // Mutate the buffer.
  ssm->mutate_mode_of_safe_operation(ModeOfSafeOperation::AUTOMATIC);
  EXPECT_EQ(ssm->mode_of_safe_operation(), ModeOfSafeOperation::AUTOMATIC);

  ssm->mutate_estop_button_status(ButtonStatus::ENGAGED);
  EXPECT_EQ(ssm->estop_button_status(), ButtonStatus::ENGAGED);

  ssm->mutate_enable_button_status(ButtonStatus::ENGAGED);
  EXPECT_EQ(ssm->enable_button_status(), ButtonStatus::ENGAGED);
}

TEST(ExtractModeOfSafeOperation, WhenUnknownOrAutoOrT1) {
  std::bitset<8> inputs_when_status_is_unknown(0b00000000);
  EXPECT_EQ(ExtractModeOfSafeOperation(inputs_when_status_is_unknown),
            ModeOfSafeOperation::UNKNOWN);

  std::bitset<8> inputs_when_status_is_auto(0b00000000);
  inputs_when_status_is_auto[AsIndex(SafetyStatusBit::MSO_AUTO)] = true;
  EXPECT_EQ(ExtractModeOfSafeOperation(inputs_when_status_is_auto),
            ModeOfSafeOperation::AUTOMATIC);

  std::bitset<8> inputs_when_status_is_t1(0b00000000);
  inputs_when_status_is_t1[AsIndex(SafetyStatusBit::MSO_T1)] = true;
  EXPECT_EQ(ExtractModeOfSafeOperation(inputs_when_status_is_t1),
            ModeOfSafeOperation::TEACH_PENDANT_1);

  std::bitset<8> inputs_when_status_is_ambiguous(0b00000000);
  inputs_when_status_is_ambiguous[AsIndex(SafetyStatusBit::MSO_AUTO)] = true;
  inputs_when_status_is_ambiguous[AsIndex(SafetyStatusBit::MSO_T1)] = true;
  EXPECT_EQ(ExtractModeOfSafeOperation(inputs_when_status_is_ambiguous),
            ModeOfSafeOperation::UNKNOWN);
}

TEST(ExtractEStopButtonStatus, WhenEnagedAndDisengaged) {
  // Mode: AUTOMATIC
  std::bitset<8> inputs_in_auto(0b00000000);
  inputs_in_auto[AsIndex(SafetyStatusBit::MSO_AUTO)] = true;

  EXPECT_EQ(ExtractEStopButtonStatus(inputs_in_auto), ButtonStatus::ENGAGED);
  inputs_in_auto[AsIndex(SafetyStatusBit::E_STOP)] = true;
  EXPECT_EQ(ExtractEStopButtonStatus(inputs_in_auto), ButtonStatus::DISENGAGED);

  // Mode: TEACH_PENDANT_1
  std::bitset<8> inputs_in_t1(0b00000000);
  inputs_in_t1[AsIndex(SafetyStatusBit::MSO_T1)] = true;

  EXPECT_EQ(ExtractEStopButtonStatus(inputs_in_t1), ButtonStatus::ENGAGED);
  inputs_in_t1[AsIndex(SafetyStatusBit::E_STOP)] = true;
  EXPECT_EQ(ExtractEStopButtonStatus(inputs_in_t1), ButtonStatus::DISENGAGED);

  // Ambiguous mode.
  // This mode should never appear, but since we're looking at the modes to
  // determine whether the button is supported, we should define the expected
  // behavior even if the mode is ambiguous.
  std::bitset<8> inputs_when_ambiguous_mode(0b00000000);
  inputs_when_ambiguous_mode[AsIndex(SafetyStatusBit::MSO_AUTO)] = true;
  inputs_when_ambiguous_mode[AsIndex(SafetyStatusBit::MSO_T1)] = true;

  EXPECT_EQ(ExtractEStopButtonStatus(inputs_when_ambiguous_mode),
            ButtonStatus::NOT_AVAILABLE);
  inputs_when_ambiguous_mode[AsIndex(SafetyStatusBit::E_STOP)] = true;
  EXPECT_EQ(ExtractEStopButtonStatus(inputs_when_ambiguous_mode),
            ButtonStatus::NOT_AVAILABLE);

  // E-Stop unsupported.
  std::bitset<8> inputs_when_unsupported(0b00000000);
  EXPECT_EQ(ExtractEStopButtonStatus(inputs_when_unsupported),
            ButtonStatus::NOT_AVAILABLE);
  inputs_when_unsupported[AsIndex(SafetyStatusBit::E_STOP)] = true;
  EXPECT_EQ(ExtractEStopButtonStatus(inputs_when_unsupported),
            ButtonStatus::NOT_AVAILABLE);
}

TEST(ExtractEnableButtonStatus, WhenEnagedAndDisengaged) {
  // Mode: AUTOMATIC
  // Enable is always DISENGAGED in AUTOMATIC.
  std::bitset<8> inputs_in_auto(0b00000000);
  inputs_in_auto[AsIndex(SafetyStatusBit::MSO_AUTO)] = true;

  EXPECT_EQ(ExtractEnableButtonStatus(inputs_in_auto),
            ButtonStatus::DISENGAGED);
  inputs_in_auto[AsIndex(SafetyStatusBit::SS1_T)] = true;
  EXPECT_EQ(ExtractEnableButtonStatus(inputs_in_auto),
            ButtonStatus::DISENGAGED);

  // Mode: TEACH_PENDANT_1
  std::bitset<8> inputs_in_t1(0b00000000);
  inputs_in_t1[AsIndex(SafetyStatusBit::MSO_T1)] = true;

  EXPECT_EQ(ExtractEnableButtonStatus(inputs_in_t1), ButtonStatus::DISENGAGED);
  inputs_in_t1[AsIndex(SafetyStatusBit::SS1_T)] = true;
  EXPECT_EQ(ExtractEnableButtonStatus(inputs_in_t1), ButtonStatus::ENGAGED);

  // Ambiguous mode.
  // This mode should never appear, but since we're looking at the modes to
  // determine whether the button is supported, we should define the expected
  // behavior even if the mode is ambiguous.
  std::bitset<8> inputs_when_ambiguous_mode(0b00000000);
  inputs_when_ambiguous_mode[AsIndex(SafetyStatusBit::MSO_AUTO)] = true;
  inputs_when_ambiguous_mode[AsIndex(SafetyStatusBit::MSO_T1)] = true;

  EXPECT_EQ(ExtractEnableButtonStatus(inputs_when_ambiguous_mode),
            ButtonStatus::NOT_AVAILABLE);
  inputs_when_ambiguous_mode[AsIndex(SafetyStatusBit::SS1_T)] = true;
  EXPECT_EQ(ExtractEnableButtonStatus(inputs_when_ambiguous_mode),
            ButtonStatus::NOT_AVAILABLE);

  // Enable unsupported.
  std::bitset<8> inputs_when_unsupported(0b00000000);
  EXPECT_EQ(ExtractEnableButtonStatus(inputs_when_unsupported),
            ButtonStatus::NOT_AVAILABLE);
  inputs_when_unsupported[AsIndex(SafetyStatusBit::SS1_T)] = true;
  EXPECT_EQ(ExtractEnableButtonStatus(inputs_when_unsupported),
            ButtonStatus::NOT_AVAILABLE);
}

TEST(ExtractRequestedBehavior, WhenInNormalOperation) {
  // As long as the SafetyStatusBit::SS1_T is high, we're in NORMAL_OPERATION.
  for (std::size_t safety_status = 0;
       safety_status < std::numeric_limits<uint8_t>::max(); ++safety_status) {
    std::bitset<8> inputs_when_normal_operation(safety_status);
    inputs_when_normal_operation[AsIndex(SafetyStatusBit::SS1_T)] = true;

    const auto requested_behavior =
        ExtractRequestedBehavior(inputs_when_normal_operation);

    EXPECT_EQ(requested_behavior, RequestedBehavior::NORMAL_OPERATION);
  }
}

TEST(ExtractRequestedBehavior, WhenInSS1OrSS2) {
  // As soon as SafetyStatusBit::SS1_T is low we expect a STOP. It depends on
  // the value of SafetyStatusBit::E_STOP whether we consider this to be a
  // CONTROLLED (SS2/protective) or HARDWARE (SS1/emergency) stop.
  for (std::size_t safety_status = 0;
       safety_status < std::numeric_limits<uint8_t>::max(); ++safety_status) {
    std::bitset<8> inputs_when_stop(safety_status);
    inputs_when_stop[AsIndex(SafetyStatusBit::SS1_T)] = false;

    const auto requested_behavior = ExtractRequestedBehavior(inputs_when_stop);

    if (inputs_when_stop[AsIndex(SafetyStatusBit::E_STOP)]) {
      EXPECT_EQ(requested_behavior,
                RequestedBehavior::SAFE_STOP_2_TIME_MONITORED);
    } else {
      EXPECT_EQ(requested_behavior,
                RequestedBehavior::SAFE_STOP_1_TIME_MONITORED);
    }
  }
}

}  // namespace
}  // namespace intrinsic_fbs
