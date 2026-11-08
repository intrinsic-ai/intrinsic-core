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

#include "intrinsic/icon/hal/interfaces/control_mode_utils.h"

#include <gtest/gtest.h>

#include "flatbuffers/buffer.h"
#include "flatbuffers/detached_buffer.h"
#include "flatbuffers/verifier.h"
#include "intrinsic/icon/hal/interfaces/control_mode.fbs.h"

using ::intrinsic_fbs::ControlMode;
using ::intrinsic_fbs::ControlModeStatus;

namespace intrinsic::hardware {
namespace {

TEST(ControlModeTest, BuildControlModeStatusInitializesWithStatus) {
  flatbuffers::DetachedBuffer buffer =
      BuildControlModeStatus(ControlMode::kHandguiding);
  flatbuffers::Verifier verifier(buffer.data(), buffer.size());
  ASSERT_EQ(verifier.VerifyBuffer<ControlModeStatus>(), true);

  auto message = flatbuffers::GetMutableRoot<ControlModeStatus>(buffer.data());
  ASSERT_NE(message, nullptr);

  ASSERT_EQ(message->status(), ControlMode::kHandguiding);
}

}  // namespace
}  // namespace intrinsic::hardware
