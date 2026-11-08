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

#include "intrinsic/icon/hal/interfaces/icon_state_utils.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

#include "flatbuffers/buffer.h"
#include "flatbuffers/detached_buffer.h"
#include "flatbuffers/verifier.h"
#include "intrinsic/icon/hal/interfaces/icon_state.fbs.h"

using ::intrinsic_fbs::BuildIconState;
using ::intrinsic_fbs::IconState;

namespace intrinsic::hardware {
namespace {

TEST(IconStateTest, BuildAndSet) {
  flatbuffers::DetachedBuffer buffer = BuildIconState();
  flatbuffers::Verifier verifier(buffer.data(), buffer.size());

  const auto icon_state = flatbuffers::GetMutableRoot<IconState>(buffer.data());
  ASSERT_NE(icon_state, nullptr);

  // Does not initialize to zero
  EXPECT_NE(icon_state->current_cycle(), 0);

  icon_state->mutate_current_cycle(std::numeric_limits<uint64_t>::max() - 42);
  EXPECT_EQ(icon_state->current_cycle(),
            std::numeric_limits<uint64_t>::max() - 42);
}

}  // namespace
}  // namespace intrinsic::hardware
