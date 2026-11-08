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

#include "intrinsic/icon/control/c_api/external_action_api/testing/icon_realtime_signal_access_and_map_fake.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "google/protobuf/any.pb.h"
#include "google/protobuf/wrappers.pb.h"
#include "internal/testing.h"
#include "intrinsic/icon/actions/action_utils.h"
#include "intrinsic/icon/control/c_api/external_action_api/icon_realtime_signal_access.h"
#include "intrinsic/icon/control/realtime_signal_types.h"
#include "intrinsic/icon/proto/v1/types.pb.h"
#include "intrinsic/icon/utils/realtime_status_matchers.h"

namespace intrinsic::icon {
namespace {

constexpr char kRealtimeSignalName[] = "signal1";
constexpr char kActionTypeName[] = "dummy_action";

using ::absl_testing::StatusIs;
using ::testing::HasSubstr;

::intrinsic_proto::icon::v1::ActionSignature GetActionSignature() {
  ActionSignatureBuilder b(kActionTypeName,
                           "test action with realtime signal.");
  (void)b.AddRealtimeSignal(kRealtimeSignalName, "test signal");
  return b.Finish();
}

TEST(IconRealtimeSignalAccessAndMapFakeTest, CorrectIdForValidName) {
  IconRealtimeSignalAccessAndMapFake signal_access_and_map(
      GetActionSignature());
  ASSERT_OK_AND_ASSIGN(
      auto id, signal_access_and_map.GetRealtimeSignalId(kRealtimeSignalName));
  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(auto signal_value,
                                    signal_access_and_map.ReadSignal(id));
  EXPECT_FALSE(signal_value.current_value);
  EXPECT_FALSE(signal_value.previous_value);
};

TEST(IconRealtimeSignalAccessAndMapFakeTest,
     GetRealtimeSignalIdFailsForInvalidName) {
  IconRealtimeSignalAccessAndMapFake signal_access_and_map(
      GetActionSignature());
  EXPECT_THAT(signal_access_and_map.GetRealtimeSignalId("bad_name"),
              StatusIs(absl::StatusCode::kNotFound, HasSubstr("bad_name")));
};

TEST(IconRealtimeSignalAccessAndMapFakeTest, ReadSignalFailsForInvalidId) {
  IconRealtimeSignalAccessAndMapFake signal_access_and_map(
      GetActionSignature());
  ASSERT_OK_AND_ASSIGN(
      auto id, signal_access_and_map.GetRealtimeSignalId(kRealtimeSignalName));
  EXPECT_THAT(
      signal_access_and_map.ReadSignal(RealtimeSignalId(id.value() + 1)),
      RealtimeStatusIs(absl::StatusCode::kNotFound));
};

TEST(IconRealtimeSignalAccessAndMapFakeAsIconRealtimeSignalAccessTest,
     CanReadValidSignal) {
  IconRealtimeSignalAccessAndMapFake signal_access_and_map(
      GetActionSignature());
  IconRealtimeSignalAccess icon_realtime_signal_access =
      signal_access_and_map.MakeIconRealtimeSignalAccess();
  ASSERT_OK_AND_ASSIGN(
      auto id, signal_access_and_map.GetRealtimeSignalId(kRealtimeSignalName));
  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(auto signal_value,
                                    icon_realtime_signal_access.ReadSignal(id));
  EXPECT_FALSE(signal_value.current_value);
  EXPECT_FALSE(signal_value.previous_value);
};

TEST(IconRealtimeSignalAccessAndMapFakeAsIconRealtimeSignalAccessTest,
     ReadSignalFailsForInvalidId) {
  IconRealtimeSignalAccessAndMapFake signal_access_and_map(
      GetActionSignature());
  IconRealtimeSignalAccess icon_realtime_signal_access =
      signal_access_and_map.MakeIconRealtimeSignalAccess();
  ASSERT_OK_AND_ASSIGN(
      auto id, signal_access_and_map.GetRealtimeSignalId(kRealtimeSignalName));
  EXPECT_THAT(
      icon_realtime_signal_access.ReadSignal(RealtimeSignalId(id.value() + 1)),
      RealtimeStatusIs(absl::StatusCode::kNotFound));
};

}  // namespace
}  // namespace intrinsic::icon
