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

#ifndef INTRINSIC_MOTION_PLANNING_SERVICE_MOTION_PLANNER_SERVICE_TEST_FIXTURE_H_
#define INTRINSIC_MOTION_PLANNING_SERVICE_MOTION_PLANNER_SERVICE_TEST_FIXTURE_H_

#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"

namespace intrinsic {

inline constexpr std::string_view kTestOrgId = "test-org";
inline constexpr std::string_view kTestWorkcellName = "test-workcell";

struct MotionPlannerServiceTestParams {
  std::string test_name;
  bool set_infinite_jerk_limits;
};

std::vector<MotionPlannerServiceTestParams> GetMotionPlannerServiceTestParams();

class MotionPlannerServiceBaseTestHelper {
 public:
  MotionPlannerServiceBaseTestHelper();
  virtual ~MotionPlannerServiceBaseTestHelper();

  absl::Status CheckJerkLimitsSupported(bool set_infinite_jerk_limits);
};

}  // namespace intrinsic

#endif  // INTRINSIC_MOTION_PLANNING_SERVICE_MOTION_PLANNER_SERVICE_TEST_FIXTURE_H_
