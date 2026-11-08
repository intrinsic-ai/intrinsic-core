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

#include "intrinsic/icon/hal/interfaces/hardware_module_state_utils.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "flatbuffers/buffer.h"
#include "flatbuffers/detached_buffer.h"
#include "flatbuffers/verifier.h"
#include "intrinsic/icon/hal/interfaces/hardware_module_state.fbs.h"

using ::intrinsic_fbs::BuildHardwareModuleState;
using ::intrinsic_fbs::HardwareModuleState;
using ::intrinsic_fbs::StateCode;
using ::testing::Eq;

namespace intrinsic::hardware {
namespace {

TEST(HardwareModuleStateTest, CreateHardwareModuleState) {
  flatbuffers::DetachedBuffer buffer = BuildHardwareModuleState();
  flatbuffers::Verifier verifier(buffer.data(), buffer.size());

  const auto hardware_module_state =
      flatbuffers::GetMutableRoot<HardwareModuleState>(buffer.data());
  ASSERT_NE(hardware_module_state, nullptr);

  EXPECT_THAT(hardware_module_state->code(), Eq(StateCode::kDeactivated));
  EXPECT_THAT(GetMessage(hardware_module_state), Eq(""));

  SetState(hardware_module_state, StateCode::kFaulted, "Some Error Message");
  EXPECT_THAT(hardware_module_state->code(), Eq(StateCode::kFaulted));
  EXPECT_THAT(GetMessage(hardware_module_state), Eq("Some Error Message"));
}

}  // namespace
}  // namespace intrinsic::hardware
