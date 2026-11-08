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

#include "intrinsic/icon/control/algorithms/feedforward_wrench_interpolator_linear.h"

#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <random>

#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/control/algorithms/feedforward_wrench_interpolator_settings.h"
#include "intrinsic/icon/testing/malloc_test.h"
#include "intrinsic/math/twist.h"

namespace intrinsic {
namespace agent_bridge {
namespace {

class FeedforwardWrenchInterpolatorTest : public ::testing::Test {
 protected:
  const double kFrequency = 1000;
  const double kTimestep = 1 / kFrequency;

  std::unique_ptr<intrinsic::agent_bridge::FeedforwardWrenchInterpolatorLinear>
      feedforward_wrench_interpolator_;

  void SetUp() override {
    feedforward_wrench_interpolator_ = std::make_unique<
        intrinsic::agent_bridge::FeedforwardWrenchInterpolatorLinear>();
    feedforward_wrench_interpolator_->Init(kFrequency);
  }
};

TEST_F(FeedforwardWrenchInterpolatorTest, ConvergenceTest) {
  const double kConvergenceThreshold = 0.0000001;
  const double kMaxIterations = 80000;
  const double kComparisonEpsilon = 0.0001;

  const double kMaxForce = 1000;
  const double kMaxTorque = 50;

  intrinsic::agent_bridge::FeedforwardWrenchInterpolatorSettings
      feedforward_wrench_interpolation_settings;
  feedforward_wrench_interpolation_settings.max_wrench.head<3>().setConstant(
      kMaxForce);
  feedforward_wrench_interpolation_settings.max_wrench.tail<3>().setConstant(
      kMaxTorque);
  feedforward_wrench_interpolation_settings.min_wrench =
      -feedforward_wrench_interpolation_settings.max_wrench;
  feedforward_wrench_interpolation_settings.max_wrench_dot.head<3>()
      .setConstant(500.);
  feedforward_wrench_interpolation_settings.max_wrench_dot.tail<3>()
      .setConstant(25.);

  EXPECT_TRUE(feedforward_wrench_interpolator_
                  ->ApplyFeedforwardWrenchInterpolatorSettings(
                      feedforward_wrench_interpolation_settings));

  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_real_distribution<double> randForce(-kMaxForce, kMaxForce);
  std::uniform_real_distribution<double> randTorque(-kMaxTorque, kMaxTorque);

  // Define a random target
  Wrench target_wrench{randForce(gen),  randForce(gen),  randForce(gen),
                       randTorque(gen), randTorque(gen), randTorque(gen)};

  // Define a random start
  Wrench current_wrench{randForce(gen),  randForce(gen),  randForce(gen),
                        randTorque(gen), randTorque(gen), randTorque(gen)};

  // System state variables to keep track of.
  Wrench prev_wrench;
  Wrench current_wrench_dot;
  Wrench prev_wrench_dot;
  Wrench wrench_dotdot;
  Wrench next_wrench;

  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  for (int i = 0; i < kMaxIterations; ++i) {
    next_wrench.setConstant(2.0 * kMaxForce);  // Should always be overwritten.
    EXPECT_TRUE(feedforward_wrench_interpolator_->ComputeNextWrench(
        current_wrench, target_wrench, &next_wrench));

    prev_wrench = current_wrench;
    current_wrench = next_wrench;

    // Check that the limits were followed.
    prev_wrench_dot = current_wrench_dot;
    current_wrench_dot = (current_wrench - prev_wrench) / kTimestep;
    wrench_dotdot = (current_wrench_dot - prev_wrench_dot) / kTimestep;

    for (int k = 0; k < 6; ++k) {
      EXPECT_TRUE(current_wrench(k) <=
                      feedforward_wrench_interpolation_settings.max_wrench(k) &&
                  current_wrench(k) >=
                      feedforward_wrench_interpolation_settings.min_wrench(k));
      EXPECT_TRUE(fabs(current_wrench_dot(k)) <
                  feedforward_wrench_interpolation_settings.max_wrench_dot(k) +
                      kComparisonEpsilon);
    }
  }
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();

  eigenmath::Vector6d wrench_error = target_wrench - current_wrench;
  EXPECT_TRUE(wrench_error.norm() < kConvergenceThreshold);
}

}  // namespace
}  // namespace agent_bridge
}  // namespace intrinsic
