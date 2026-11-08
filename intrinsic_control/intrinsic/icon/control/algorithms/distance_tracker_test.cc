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

#include "intrinsic/icon/control/algorithms/distance_tracker.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <limits>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "internal/testing.h"
#include "intrinsic/eigenmath/types.h"

namespace intrinsic::icon {
namespace {

constexpr double kErrorTol = 1e-8;

using ::absl_testing::StatusIs;
using ::testing::HasSubstr;

TEST(MaximumTranslationalDistanceTracker, IncorrectInitializationOfFrequency) {
  EXPECT_THAT(MaximumTranslationalDistanceTracker::Create(
                  /*frequency_hz=*/0.0, /*time_window_sec=*/10.0),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("The frequency should be larger than zero.")));
}

TEST(MaximumTranslationalDistanceTracker, IncorrectInitializationOfTimeWindow) {
  EXPECT_THAT(
      MaximumTranslationalDistanceTracker::Create(
          /*frequency_hz=*/10.0, /*time_window_sec=*/-1.0),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("The time window should be larger than zero.")));
}

TEST(MaximumTranslationalDistanceTracker, ExpectedTrackingOnMinimalBufferSize) {
  ASSERT_OK_AND_ASSIGN(auto tracker,
                       MaximumTranslationalDistanceTracker::Create(1.0, 1.0));

  EXPECT_THAT(tracker->GetBufferSize(), 1);
  EXPECT_THAT(tracker->GetMaximumDistance(),
              std::numeric_limits<double>::infinity());
  tracker->Add(eigenmath::Vector3d(.1, .0, .0));
  EXPECT_NEAR(tracker->GetMaximumDistance(), 0.0, kErrorTol);

  tracker->Add(eigenmath::Vector3d(.2, .0, .0));
  EXPECT_NEAR(tracker->GetMaximumDistance(), 0.0, kErrorTol);

  tracker->Add(eigenmath::Vector3d(.3, .0, .0));
  EXPECT_NEAR(tracker->GetMaximumDistance(), 0.0, kErrorTol);
}

TEST(MaximumTranslationalDistanceTracker, ExpectedTrackingBufferSizeOfTwo) {
  ASSERT_OK_AND_ASSIGN(auto tracker,
                       MaximumTranslationalDistanceTracker::Create(1.0, 2.0));

  EXPECT_THAT(tracker->GetBufferSize(), 2);
  EXPECT_THAT(tracker->GetMaximumDistance(),
              std::numeric_limits<double>::infinity());

  tracker->Add(eigenmath::Vector3d(.1, .0, .0));
  EXPECT_THAT(tracker->GetMaximumDistance(),
              std::numeric_limits<double>::infinity());

  tracker->Add(eigenmath::Vector3d(.2, .0, .0));  // delta of 0.1
  EXPECT_NEAR(tracker->GetMaximumDistance(), 0.1, kErrorTol);

  tracker->Add(eigenmath::Vector3d(.4, .0, .0));  // delta of 0.2
  EXPECT_NEAR(tracker->GetMaximumDistance(), 0.2, kErrorTol);
}

TEST(MaximumTranslationalDistanceTracker, VerifyClearWorks) {
  ASSERT_OK_AND_ASSIGN(auto tracker,
                       MaximumTranslationalDistanceTracker::Create(1.0, 2.0));
  EXPECT_THAT(tracker->GetBufferSize(), 2);

  EXPECT_THAT(tracker->GetMaximumDistance(),
              std::numeric_limits<double>::infinity());

  tracker->Add(eigenmath::Vector3d(.3, .0, .0));
  tracker->Add(eigenmath::Vector3d(.3, .0, .0));
  EXPECT_NEAR(tracker->GetMaximumDistance(), 0.0, kErrorTol);

  tracker->Clear();
  EXPECT_THAT(tracker->GetMaximumDistance(),
              std::numeric_limits<double>::infinity());
}

}  // namespace
}  // namespace intrinsic::icon
