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

#include "intrinsic/icon/control/c_api/convert_c_realtime_status.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/strings/string_view.h"
#include "intrinsic/icon/control/c_api/c_realtime_status.h"
#include "intrinsic/icon/utils/realtime_status.h"

namespace intrinsic::icon {
namespace {

using ::absl_testing::StatusIs;
using ::testing::SizeIs;

TEST(ConvertIntrinsicIconRealtimeStatus, FullSizeRealtimeStatusRoundtrip) {
  char max_size_buffer[kIntrinsicIconRealtimeStatusMaxMessageLength];
  std::fill(&max_size_buffer[0],
            &max_size_buffer[kIntrinsicIconRealtimeStatusMaxMessageLength],
            'A');
  RealtimeStatus status = intrinsic::icon::InternalError(
      absl::string_view(max_size_buffer, sizeof(max_size_buffer)));
  RealtimeStatus round_tripped = ToRealtimeStatus(FromRealtimeStatus(status));
  EXPECT_EQ(round_tripped.code(), status.code());
  EXPECT_EQ(round_tripped.message(), status.message());
}

TEST(ConvertIntrinsicIconRealtimeStatus, AbslStatusRoundtripGetsTruncated) {
  char longer_buffer[kIntrinsicIconRealtimeStatusMaxMessageLength * 2];
  std::fill(&longer_buffer[0],
            &longer_buffer[kIntrinsicIconRealtimeStatusMaxMessageLength * 2],
            'A');
  absl::Status status = absl::InternalError(
      absl::string_view(longer_buffer, sizeof(longer_buffer)));
  EXPECT_THAT(ToAbslStatus(FromAbslStatus(status)),
              StatusIs(status.code(),
                       SizeIs(kIntrinsicIconRealtimeStatusMaxMessageLength)));
}

}  // namespace
}  // namespace intrinsic::icon
