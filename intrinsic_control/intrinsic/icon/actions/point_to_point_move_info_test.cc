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

#include "intrinsic/icon/actions/point_to_point_move_info.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <vector>

#include "absl/status/status_matchers.h"
#include "internal/testing.h"
#include "intrinsic/icon/proto/joint_space.pb.h"
#include "intrinsic/kinematics/types/joint_limits.h"

namespace intrinsic {
namespace icon {
namespace {

using ::testing::ElementsAreArray;

TEST(PointToPointMoveInfoTest, CreatePointToPointMoveFixedParamsFillsInParams) {
  std::vector<double> goal_position = {1, -0.5, 0.5, 0, 3, 0};
  std::vector<double> goal_velocity = {2, 0.5, -0.75, 1, 2, -4};

  PointToPointMoveInfo::FixedParams fixed_params =
      CreatePointToPointMoveFixedParams(goal_position, goal_velocity);

  EXPECT_THAT(fixed_params.goal_position().joints(),
              ElementsAreArray(goal_position));
  EXPECT_THAT(fixed_params.goal_velocity().joints(),
              ElementsAreArray(goal_velocity));
}

TEST(PointToPointMoveInfoTest,
     CreatePointToPointMoveFixedParamsWorksWithLimits) {
  std::vector<double> goal_position = {1, -0.5, 0.5, 0, 3, 0};
  std::vector<double> goal_velocity = {2, 0.5, -0.75, 1, 2, -4};
  JointLimits limits;
  ASSERT_OK(limits.SetSize(6));
  limits.SetUnlimited();
  limits.max_position.setConstant(10.0);
  limits.min_position.setConstant(-10.0);

  PointToPointMoveInfo::FixedParams fixed_params =
      CreatePointToPointMoveFixedParams(goal_position, goal_velocity, limits);

  EXPECT_THAT(fixed_params.goal_position().joints(),
              ElementsAreArray(goal_position));
  EXPECT_THAT(fixed_params.goal_velocity().joints(),
              ElementsAreArray(goal_velocity));
  EXPECT_THAT(fixed_params.joint_limits().max_position().values(),
              ElementsAreArray(limits.max_position));
  EXPECT_THAT(fixed_params.joint_limits().min_position().values(),
              ElementsAreArray(limits.min_position));
}

}  // namespace
}  // namespace icon
}  // namespace intrinsic
