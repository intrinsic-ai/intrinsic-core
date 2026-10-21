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

#include <gtest/gtest.h>

#include <vector>

#include "intrinsic/motion_planning/motion_planner/motion_planner_topp_test_fixture.h"

namespace intrinsic {

std::vector<ToppTestParams> GetToppTestParams() {
  return {
      ToppTestParams{.test_name = "AccLimitedTopp",
                     .set_infinite_jerk_limits = true},
  };
}

void MotionPlannerToppBaseTest::SetUpTopp(const ToppTestParams& topp_params) {
  topp_test_params_ = topp_params;
  ASSERT_TRUE(topp_test_params_.set_infinite_jerk_limits)
      << "Jerk-limited trajectory generation is not available in OSS build.";
  motion_planner_flags_.enable_path_refinement_validation_step = true;
}

}  // namespace intrinsic
