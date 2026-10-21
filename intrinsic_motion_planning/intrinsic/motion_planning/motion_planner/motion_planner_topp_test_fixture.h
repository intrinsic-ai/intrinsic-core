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

#ifndef INTRINSIC_MOTION_PLANNING_MOTION_PLANNER_MOTION_PLANNER_TOPP_TEST_FIXTURE_H_
#define INTRINSIC_MOTION_PLANNING_MOTION_PLANNER_MOTION_PLANNER_TOPP_TEST_FIXTURE_H_

#include <gtest/gtest.h>

#include <ostream>
#include <string>
#include <vector>

#include "intrinsic/motion_planning/motion_planner/motion_planner_base_test_fixture.h"
#include "intrinsic/motion_planning/motion_planner/motion_planner_flags.h"

namespace intrinsic {

// Parameters for testing different Time-Optimal Path Parameterization (TOPP)
// solvers within `MotionPlannerTest`. The `set_infinite_jerk_limits` flag
// controls whether joint jerk limits are finite, which determines if a
// jerk-limited or an acceleration-limited TOPP solver is used. In OSS builds,
// given that a jerk-limited TOPP is unavailable, setting
// `set_infinite_jerk_limits` to false will cause tests to be skipped.
struct ToppTestParams {
  std::string test_name;
  bool set_infinite_jerk_limits = true;

  friend void PrintTo(const ToppTestParams& params, std::ostream* os) {
    *os << params.test_name
        << " (set_infinite_jerk_limits=" << params.set_infinite_jerk_limits
        << ")";
  }
};

// Returns TOPP solver test parameters supported in the active build
// environment.
std::vector<ToppTestParams> GetToppTestParams();

// Unparameterized base fixture holding the TOPP state and lifecycle
// methods.
class MotionPlannerToppBaseTest : public MotionPlannerBaseTest {
 protected:
  void SetUpTopp(const ToppTestParams& topp_params);

  ToppTestParams topp_test_params_;
  MotionPlannerFlags motion_planner_flags_;
};

// Parameterized test fixture solely for testing TOPP variations
class MotionPlannerToppTest : public MotionPlannerToppBaseTest,
                              public ::testing::TestWithParam<ToppTestParams> {
 protected:
  void SetUp() override { SetUpTopp(GetParam()); }
};

}  // namespace intrinsic

#endif  // INTRINSIC_MOTION_PLANNING_MOTION_PLANNER_MOTION_PLANNER_TOPP_TEST_FIXTURE_H_
