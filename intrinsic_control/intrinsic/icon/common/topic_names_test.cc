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

#include "intrinsic/icon/common/topic_names.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "intrinsic/icon/common/id_types.h"

namespace intrinsic::icon {
namespace {

using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;
using ::testing::HasSubstr;

TEST(TopicNameForRobotStatus, Works) {
  EXPECT_THAT(TopicNameForRobotStatus("robot"),
              IsOkAndHolds("/icon/robot/robot_status"));
}

TEST(TopicNameForRobotStatus, EmptyRobotName) {
  EXPECT_THAT(TopicNameForRobotStatus(""),
              StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("empty")));
}

TEST(TopicNameForCoeExport, Works) {
  EXPECT_THAT(TopicNameForCoeExport("robot"),
              IsOkAndHolds("/icon/robot/coe_export"));
}

TEST(TopicNameForCoeExport, EmptyRobotName) {
  EXPECT_THAT(TopicNameForCoeExport(""),
              StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("empty")));
}

TEST(TopicNameForDiagnosisExport, Works) {
  EXPECT_THAT(TopicNameForDiagnosisExport("robot"),
              IsOkAndHolds("/icon/robot/diagnosis"));
}

TEST(TopicNameForDiagnosisExport, EmptyRobotName) {
  EXPECT_THAT(TopicNameForDiagnosisExport(""),
              StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("empty")));
}

TEST(TopicNameForStreamingOutput, Works) {
  EXPECT_THAT(TopicNameForStreamingOutput("robot", ActionInstanceId(5)),
              "/icon/robot/output_streams/action_5");
}

TEST(TopicNameForStreamingOutput, EmptyRobotName) {
  EXPECT_THAT(TopicNameForStreamingOutput("", ActionInstanceId(5)),
              StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("empty")));
}

}  // namespace
}  // namespace intrinsic::icon
