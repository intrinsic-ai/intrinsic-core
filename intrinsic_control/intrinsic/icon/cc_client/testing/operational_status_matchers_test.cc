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

#include "intrinsic/icon/cc_client/testing/operational_status_matchers.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "intrinsic/icon/cc_client/operational_status.h"

namespace intrinsic::icon {
namespace {

using ::absl_testing::IsOkAndHolds;
using ::testing::HasSubstr;
using ::testing::Not;

TEST(OperationalStatusTest, OperationalStatusIsDisabled) {
  const OperationalStatus disabled = OperationalStatus::Disabled();
  EXPECT_THAT(disabled, OperationalStatusIs(OperationalState::kDisabled));
  EXPECT_THAT(disabled, Not(OperationalStatusIsFaulted(
                            "disabled state should not have fault_reason")));
  EXPECT_THAT(disabled, Not(OperationalStatusIs(OperationalState::kFaulted)));
  EXPECT_THAT(disabled, Not(OperationalStatusIs(OperationalState::kEnabled)));
  absl::StatusOr<OperationalStatus> status_or = disabled;
  EXPECT_THAT(status_or,
              IsOkAndHolds(OperationalStatusIs(OperationalState::kDisabled)));
}

TEST(OperationalStatusTest, OperationalStatusIsEnabled) {
  const OperationalStatus enabled = OperationalStatus::Enabled();
  EXPECT_THAT(enabled, OperationalStatusIs(OperationalState::kEnabled));
  EXPECT_THAT(enabled, Not(OperationalStatusIsFaulted(
                           "enabled state should not have fault_reason")));
  EXPECT_THAT(enabled, Not(OperationalStatusIs(OperationalState::kDisabled)));
  EXPECT_THAT(enabled, Not(OperationalStatusIs(OperationalState::kFaulted)));
  absl::StatusOr<OperationalStatus> status_or = enabled;
  EXPECT_THAT(status_or,
              IsOkAndHolds(OperationalStatusIs(OperationalState::kEnabled)));
}

TEST(OperationalStatusTest, OperationalStatusIsFaulted) {
  const OperationalStatus faulted = OperationalStatus::Faulted("foo bar");
  EXPECT_THAT(faulted, OperationalStatusIs(OperationalState::kFaulted));
  EXPECT_THAT(faulted, OperationalStatusIsFaulted("foo bar"));
  EXPECT_THAT(faulted, Not(OperationalStatusIsFaulted("bar")));
  EXPECT_THAT(faulted, OperationalStatusIsFaulted(HasSubstr("bar")));
  EXPECT_THAT(faulted, Not(OperationalStatusIs(OperationalState::kDisabled)));
  EXPECT_THAT(faulted, Not(OperationalStatusIs(OperationalState::kEnabled)));
  absl::StatusOr<OperationalStatus> status_or = faulted;
  EXPECT_THAT(status_or,
              IsOkAndHolds(OperationalStatusIsFaulted(HasSubstr("bar"))));
}

}  // namespace
}  // namespace intrinsic::icon
