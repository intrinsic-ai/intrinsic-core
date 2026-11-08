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

#include "intrinsic/icon/control/algorithms/cartesian_target_scaling.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "intrinsic/eigenmath/matchers.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/control/algorithms/reference_limit_settings.h"
#include "intrinsic/kinematics/types/cart_state.h"
#include "intrinsic/math/pose3.h"

using ::intrinsic::eigenmath::testing::IsApprox;

constexpr double kSmallTolerance = 1e-3;

namespace intrinsic::icon {

namespace {
TEST(AgentBridgeReferenceScalingTest, TestScaleTargetCommandModePosition) {
  CartStatePV target_state, scaled_target_state, expected_scaled_target_state;
  CartStatePVA sensed_state, current_reference_state, next_reference_state;

  target_state.pose = Pose3d(eigenmath::Quaterniond(/*w=*/1, /*x=*/0.0, 0.0, 0),
                             eigenmath::Vector3d(0.0, 0.0, 0.0));

  sensed_state.pose =
      Pose3d(eigenmath::Quaterniond(/*w=*/0, /*x=*/0.707, 0., 0.707),
             eigenmath::Vector3d(0.0, 0.1, 0.0));

  ReferenceLimitSettings settings{
      .max_translation = 0.01, .max_rotation = 0.01, .activation_ratio = 0.95};

  ScaleTargetCommand(TrajectoryGenerationMode::POSITION, target_state,
                     sensed_state, current_reference_state,
                     next_reference_state, settings, scaled_target_state);

  // Expected scaled target state should be within the thresholds set in the
  // ReferenceLimitSetting compared to the sensed_state
  expected_scaled_target_state.pose = Pose3d(
      eigenmath::Quaterniond(/*w=*/0., /*x=*/0.707098, 0.004999, 0.707098),
      eigenmath::Vector3d(0., 0.09, 0.0));

  EXPECT_THAT(scaled_target_state.pose,
              IsApprox(expected_scaled_target_state.pose, kSmallTolerance));
}

}  // namespace
}  // namespace intrinsic::icon
