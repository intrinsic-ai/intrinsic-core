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

#include <gmock/gmock.h>

#include <memory>

#include "absl/status/status_matchers.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/control/algorithms/wrench_stability_monitor.h"
#include "intrinsic/icon/control/algorithms/wrench_stability_monitor_factory.h"
#include "intrinsic/icon/equipment/force_control_settings.pb.h"
#include "intrinsic/icon/testing/malloc_test.h"
#include "intrinsic/icon/utils/realtime_status_matchers.h"
#include "intrinsic/math/twist.h"

namespace intrinsic::icon {
namespace {

using ::testing::DoubleNear;

constexpr double kControlFrequencyHz = 1000.0;
constexpr double kSmallTolerance = 1.0e-6;

TEST(WrenchStabilityMonitorIocTest, CreationSucceeds) {
  intrinsic_proto::icon::ForceControlSettings settings;
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<WrenchStabilityMonitor> monitor,
      CreateWrenchStabilityMonitor(settings, kControlFrequencyHz));
  EXPECT_NE(monitor, nullptr);
}

TEST(WrenchStabilityMonitorIocTest,
     CreationSucceedsForNegativeControlFrequency) {
  intrinsic_proto::icon::ForceControlSettings settings;

  // Idle monitor does not validate input parameters.
  EXPECT_OK(
      CreateWrenchStabilityMonitor(settings, /*control_frequency_hz=*/-1.0));
}

TEST(WrenchStabilityMonitorIocTest, ReturnsZeroStabilityIndexOnAnySignal) {
  constexpr double kControlFrequencyHz = 10.0;
  intrinsic_proto::icon::ForceControlSettings settings;
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<WrenchStabilityMonitor> monitor,
      CreateWrenchStabilityMonitor(settings, kControlFrequencyHz));

  // Feed an alternating signal with increasing amplitude.
  const int kNumSamples = std::round(3.0 * kControlFrequencyHz);
  for (int i = 0; i < kNumSamples; ++i) {
    const double sign = (i % 2 == 0) ? 1.0 : -1.0;
    const double amplitude = static_cast<double>(10 * i);
    const Wrench alternating_wrench(
        eigenmath::Vector6d::Constant(sign * amplitude));
    IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
    INTRINSIC_RT_EXPECT_OK(monitor->Update(alternating_wrench));
    IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();

    // Idle monitor always reports zero stability index.
    EXPECT_THAT(monitor->GetStabilityIndex(), DoubleNear(0.0, kSmallTolerance));
  }
}

}  // namespace
}  // namespace intrinsic::icon
