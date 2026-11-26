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

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

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

TEST(HardwareModuleStateTest, SetStateTruncatesWithoutWritingPastBuffer) {
  // Regression test for b/587207763: SetState with a >= 256-byte message must
  // write at most 255 characters plus NUL without touching bytes after struct.
  constexpr size_t kGuardBytes = 16;
  constexpr uint8_t kGuard = 0xA5;
  std::vector<uint8_t> storage(sizeof(HardwareModuleState) + kGuardBytes,
                               kGuard);
  flatbuffers::DetachedBuffer buffer = BuildHardwareModuleState();
  std::memcpy(storage.data(),
              flatbuffers::GetRoot<HardwareModuleState>(buffer.data()),
              sizeof(HardwareModuleState));
  auto* state = reinterpret_cast<HardwareModuleState*>(storage.data());

  const std::string exact_capacity_msg(state->message()->size(), 'a');
  SetState(state, StateCode::kFaulted, exact_capacity_msg);
  for (size_t i = sizeof(HardwareModuleState); i < storage.size(); ++i) {
    EXPECT_EQ(storage[i], kGuard) << "Guard byte overwritten at offset " << i;
  }
  EXPECT_EQ(GetMessage(state), std::string(state->message()->size() - 1, 'a'));
}

TEST(HardwareModuleStateTest, GetMessageDoesNotReadPastUnterminatedBuffer) {
  // Regression test for b/587207763: GetMessage must stop at message capacity
  // even when shared memory has no NUL terminator.
  constexpr size_t kGuardBytes = 16;
  std::vector<uint8_t> storage(sizeof(HardwareModuleState) + kGuardBytes, 'X');
  storage.back() = '\0';
  flatbuffers::DetachedBuffer buffer = BuildHardwareModuleState();
  std::memcpy(storage.data(),
              flatbuffers::GetRoot<HardwareModuleState>(buffer.data()),
              sizeof(HardwareModuleState));
  auto* state = reinterpret_cast<HardwareModuleState*>(storage.data());
  const size_t capacity = state->message()->size();
  std::memset(state->mutable_message()->Data(), 'a', capacity);

  const std::string_view got = GetMessage(state);
  EXPECT_EQ(got.size(), capacity);
  EXPECT_EQ(got, std::string(capacity, 'a'));
}

}  // namespace
}  // namespace intrinsic::hardware
