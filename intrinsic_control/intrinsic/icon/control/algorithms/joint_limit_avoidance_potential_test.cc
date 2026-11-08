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

#include "intrinsic/icon/control/algorithms/joint_limit_avoidance_potential.h"

#include <gtest/gtest.h>

namespace intrinsic {
namespace control {
namespace {

using intrinsic::control::SingleJointAvoidanceTorque;

TEST(SingleJointTest, UpperLimitTest) {
  double max_torque = 100;
  double upper_limit = 10.;
  double upper_activation_threshold = 8.0;
  double lower_limit = -3.;
  double lower_activation_threshold = -2.;

  // zero output for out-of-activation values?
  ASSERT_EQ(SingleJointAvoidanceTorque(lower_limit, lower_activation_threshold,
                                       upper_limit, upper_activation_threshold,
                                       max_torque, 6.),
            0.0);
  ASSERT_EQ(SingleJointAvoidanceTorque(lower_limit, lower_activation_threshold,
                                       upper_limit, upper_activation_threshold,
                                       max_torque, 5.),
            0.0);
  ASSERT_EQ(SingleJointAvoidanceTorque(lower_limit, lower_activation_threshold,
                                       upper_limit, upper_activation_threshold,
                                       max_torque, 7.99),
            0.0);
  ASSERT_EQ(SingleJointAvoidanceTorque(lower_limit, lower_activation_threshold,
                                       upper_limit, upper_activation_threshold,
                                       max_torque, 3.),
            0.0);

  // correct sign for in-activation zone
  ASSERT_LT(SingleJointAvoidanceTorque(lower_limit, lower_activation_threshold,
                                       upper_limit, upper_activation_threshold,
                                       max_torque, 8.1),
            0.0);
  ASSERT_LT(SingleJointAvoidanceTorque(lower_limit, lower_activation_threshold,
                                       upper_limit, upper_activation_threshold,
                                       max_torque, 9.0),
            0.0);

  // maximum output for over-limit values?
  ASSERT_EQ(SingleJointAvoidanceTorque(lower_limit, lower_activation_threshold,
                                       upper_limit, upper_activation_threshold,
                                       max_torque, 10.1),
            -max_torque);
  ASSERT_EQ(SingleJointAvoidanceTorque(lower_limit, lower_activation_threshold,
                                       upper_limit, upper_activation_threshold,
                                       max_torque, 10e3),
            -max_torque);
}

TEST(SingleJointTest, LowerLimitTest) {
  double max_torque = 100;
  double upper_limit = 10.;
  double upper_activation_threshold = 8.0;
  double lower_limit = -3.;
  double lower_activation_threshold = -2.;

  // zero output for out-of-activation values?
  ASSERT_EQ(SingleJointAvoidanceTorque(lower_limit, lower_activation_threshold,
                                       upper_limit, upper_activation_threshold,
                                       max_torque, 6.),
            0.0);
  ASSERT_EQ(SingleJointAvoidanceTorque(lower_limit, lower_activation_threshold,
                                       upper_limit, upper_activation_threshold,
                                       max_torque, -1.),
            0.0);
  ASSERT_EQ(SingleJointAvoidanceTorque(lower_limit, lower_activation_threshold,
                                       upper_limit, upper_activation_threshold,
                                       max_torque, -1.99),
            0.0);
  ASSERT_EQ(SingleJointAvoidanceTorque(lower_limit, lower_activation_threshold,
                                       upper_limit, upper_activation_threshold,
                                       max_torque, -0.5),
            0.0);

  // correct sign for in-activation zone
  ASSERT_GT(SingleJointAvoidanceTorque(lower_limit, lower_activation_threshold,
                                       upper_limit, upper_activation_threshold,
                                       max_torque, -2.1),
            0.0);
  ASSERT_GT(SingleJointAvoidanceTorque(lower_limit, lower_activation_threshold,
                                       upper_limit, upper_activation_threshold,
                                       max_torque, -2.99),
            0.0);
  // maximum output for over-limit values?
  ASSERT_EQ(SingleJointAvoidanceTorque(lower_limit, lower_activation_threshold,
                                       upper_limit, upper_activation_threshold,
                                       max_torque, -3.1),
            max_torque);
  ASSERT_EQ(SingleJointAvoidanceTorque(lower_limit, lower_activation_threshold,
                                       upper_limit, upper_activation_threshold,
                                       max_torque, -10e3),
            max_torque);
}

TEST(SingleJointTest, NegativeUpperLimitTest) {
  double max_torque = 100;
  double upper_limit = -1.;
  double upper_activation_threshold = -2;
  double lower_limit = -5.;
  double lower_activation_threshold = -4.;

  // zero output for out-of-activation values?
  ASSERT_EQ(SingleJointAvoidanceTorque(lower_limit, lower_activation_threshold,
                                       upper_limit, upper_activation_threshold,
                                       max_torque, -3.1),
            0.0);
  ASSERT_EQ(SingleJointAvoidanceTorque(lower_limit, lower_activation_threshold,
                                       upper_limit, upper_activation_threshold,
                                       max_torque, -3.),
            0.0);
  ASSERT_EQ(SingleJointAvoidanceTorque(lower_limit, lower_activation_threshold,
                                       upper_limit, upper_activation_threshold,
                                       max_torque, -3.99),
            0.0);
  ASSERT_EQ(SingleJointAvoidanceTorque(lower_limit, lower_activation_threshold,
                                       upper_limit, upper_activation_threshold,
                                       max_torque, -3.5),
            0.0);

  // correct sign for in-activation zone
  ASSERT_LT(SingleJointAvoidanceTorque(lower_limit, lower_activation_threshold,
                                       upper_limit, upper_activation_threshold,
                                       max_torque, -1.1),
            0.0);
  ASSERT_LT(SingleJointAvoidanceTorque(lower_limit, lower_activation_threshold,
                                       upper_limit, upper_activation_threshold,
                                       max_torque, -1.99),
            0.0);
  // maximum output for over-limit values?
  ASSERT_EQ(SingleJointAvoidanceTorque(lower_limit, lower_activation_threshold,
                                       upper_limit, upper_activation_threshold,
                                       max_torque, -7.1),
            max_torque);
  ASSERT_EQ(SingleJointAvoidanceTorque(lower_limit, lower_activation_threshold,
                                       upper_limit, upper_activation_threshold,
                                       max_torque, -10e3),
            max_torque);
}

TEST(SingleJointTest, PositiveLowerLimitTest) {
  double max_torque = 100;
  double upper_limit = 10.;
  double upper_activation_threshold = 8.0;
  double lower_limit = 3.;
  double lower_activation_threshold = 4.;

  // zero output for out-of-activation values?
  ASSERT_EQ(SingleJointAvoidanceTorque(lower_limit, lower_activation_threshold,
                                       upper_limit, upper_activation_threshold,
                                       max_torque, 6.),
            0.0);
  ASSERT_EQ(SingleJointAvoidanceTorque(lower_limit, lower_activation_threshold,
                                       upper_limit, upper_activation_threshold,
                                       max_torque, 5.),
            0.0);
  ASSERT_EQ(SingleJointAvoidanceTorque(lower_limit, lower_activation_threshold,
                                       upper_limit, upper_activation_threshold,
                                       max_torque, 5.99),
            0.0);
  ASSERT_EQ(SingleJointAvoidanceTorque(lower_limit, lower_activation_threshold,
                                       upper_limit, upper_activation_threshold,
                                       max_torque, 5.5),
            0.0);

  // correct sign for in-activation zone
  ASSERT_GT(SingleJointAvoidanceTorque(lower_limit, lower_activation_threshold,
                                       upper_limit, upper_activation_threshold,
                                       max_torque, 3.1),
            0.0);
  ASSERT_GT(SingleJointAvoidanceTorque(lower_limit, lower_activation_threshold,
                                       upper_limit, upper_activation_threshold,
                                       max_torque, 3.99),
            0.0);
  // maximum output for over-limit values?
  ASSERT_EQ(SingleJointAvoidanceTorque(lower_limit, lower_activation_threshold,
                                       upper_limit, upper_activation_threshold,
                                       max_torque, -3.1),
            max_torque);
  ASSERT_EQ(SingleJointAvoidanceTorque(lower_limit, lower_activation_threshold,
                                       upper_limit, upper_activation_threshold,
                                       max_torque, -10e3),
            max_torque);
}

TEST(SingleJointTestWithMinTorque, UpperLimitTest) {
  double max_torque = 100;
  double min_torque = -10;
  double upper_limit = 10.;
  double upper_activation_threshold = 8.0;
  double lower_limit = -3.;
  double lower_activation_threshold = -2.;

  // zero output for out-of-activation values?
  ASSERT_EQ(SingleJointAvoidanceTorque(lower_limit, lower_activation_threshold,
                                       upper_limit, upper_activation_threshold,
                                       min_torque, max_torque, 6.),
            0.0);
  ASSERT_EQ(SingleJointAvoidanceTorque(lower_limit, lower_activation_threshold,
                                       upper_limit, upper_activation_threshold,
                                       min_torque, max_torque, 5.),
            0.0);
  ASSERT_EQ(SingleJointAvoidanceTorque(lower_limit, lower_activation_threshold,
                                       upper_limit, upper_activation_threshold,
                                       min_torque, max_torque, 7.99),
            0.0);
  ASSERT_EQ(SingleJointAvoidanceTorque(lower_limit, lower_activation_threshold,
                                       upper_limit, upper_activation_threshold,
                                       min_torque, max_torque, 3.),
            0.0);

  // correct sign for in-activation zone
  ASSERT_LT(SingleJointAvoidanceTorque(lower_limit, lower_activation_threshold,
                                       upper_limit, upper_activation_threshold,
                                       min_torque, max_torque, 8.1),
            0.0);
  ASSERT_LT(SingleJointAvoidanceTorque(lower_limit, lower_activation_threshold,
                                       upper_limit, upper_activation_threshold,
                                       min_torque, max_torque, 9.0),
            0.0);

  // maximum output for over-limit values?
  ASSERT_EQ(SingleJointAvoidanceTorque(lower_limit, lower_activation_threshold,
                                       upper_limit, upper_activation_threshold,
                                       min_torque, max_torque, 10.1),
            min_torque);
  ASSERT_EQ(SingleJointAvoidanceTorque(lower_limit, lower_activation_threshold,
                                       upper_limit, upper_activation_threshold,
                                       min_torque, max_torque, 10e3),
            min_torque);
}

TEST(SingleJointTestWithMinTorque, LowerLimitTest) {
  double max_torque = 100;
  double min_torque = -10;
  double upper_limit = 10.;
  double upper_activation_threshold = 8.0;
  double lower_limit = -3.;
  double lower_activation_threshold = -2.;

  // zero output for out-of-activation values?
  ASSERT_EQ(SingleJointAvoidanceTorque(lower_limit, lower_activation_threshold,
                                       upper_limit, upper_activation_threshold,
                                       min_torque, max_torque, 6.),
            0.0);
  ASSERT_EQ(SingleJointAvoidanceTorque(lower_limit, lower_activation_threshold,
                                       upper_limit, upper_activation_threshold,
                                       min_torque, max_torque, -1.),
            0.0);
  ASSERT_EQ(SingleJointAvoidanceTorque(lower_limit, lower_activation_threshold,
                                       upper_limit, upper_activation_threshold,
                                       min_torque, max_torque, -1.99),
            0.0);
  ASSERT_EQ(SingleJointAvoidanceTorque(lower_limit, lower_activation_threshold,
                                       upper_limit, upper_activation_threshold,
                                       min_torque, max_torque, -0.5),
            0.0);

  // correct sign for in-activation zone
  ASSERT_GT(SingleJointAvoidanceTorque(lower_limit, lower_activation_threshold,
                                       upper_limit, upper_activation_threshold,
                                       min_torque, max_torque, -2.1),
            0.0);
  ASSERT_GT(SingleJointAvoidanceTorque(lower_limit, lower_activation_threshold,
                                       upper_limit, upper_activation_threshold,
                                       min_torque, max_torque, -2.99),
            0.0);
  // maximum output for over-limit values?
  ASSERT_EQ(SingleJointAvoidanceTorque(lower_limit, lower_activation_threshold,
                                       upper_limit, upper_activation_threshold,
                                       min_torque, max_torque, -3.1),
            max_torque);
  ASSERT_EQ(SingleJointAvoidanceTorque(lower_limit, lower_activation_threshold,
                                       upper_limit, upper_activation_threshold,
                                       min_torque, max_torque, -10e3),
            max_torque);
}

TEST(SingleJointTestWithMinTorque, NegativeUpperLimitTest) {
  double max_torque = 100;
  double min_torque = -10;
  double upper_limit = -1.;
  double upper_activation_threshold = -2;
  double lower_limit = -5.;
  double lower_activation_threshold = -4.;

  // zero output for out-of-activation values?
  ASSERT_EQ(SingleJointAvoidanceTorque(lower_limit, lower_activation_threshold,
                                       upper_limit, upper_activation_threshold,
                                       min_torque, max_torque, -3.1),
            0.0);
  ASSERT_EQ(SingleJointAvoidanceTorque(lower_limit, lower_activation_threshold,
                                       upper_limit, upper_activation_threshold,
                                       min_torque, max_torque, -3.),
            0.0);
  ASSERT_EQ(SingleJointAvoidanceTorque(lower_limit, lower_activation_threshold,
                                       upper_limit, upper_activation_threshold,
                                       min_torque, max_torque, -3.99),
            0.0);
  ASSERT_EQ(SingleJointAvoidanceTorque(lower_limit, lower_activation_threshold,
                                       upper_limit, upper_activation_threshold,
                                       min_torque, max_torque, -3.5),
            0.0);

  // correct sign for in-activation zone
  ASSERT_LT(SingleJointAvoidanceTorque(lower_limit, lower_activation_threshold,
                                       upper_limit, upper_activation_threshold,
                                       min_torque, max_torque, -1.1),
            0.0);
  ASSERT_LT(SingleJointAvoidanceTorque(lower_limit, lower_activation_threshold,
                                       upper_limit, upper_activation_threshold,
                                       min_torque, max_torque, -1.99),
            0.0);
  // maximum output for over-limit values?
  ASSERT_EQ(SingleJointAvoidanceTorque(lower_limit, lower_activation_threshold,
                                       upper_limit, upper_activation_threshold,
                                       min_torque, max_torque, -7.1),
            max_torque);
  ASSERT_EQ(SingleJointAvoidanceTorque(lower_limit, lower_activation_threshold,
                                       upper_limit, upper_activation_threshold,
                                       min_torque, max_torque, -10e3),
            max_torque);
}

TEST(SingleJointTestWithMinTorque, PositiveLowerLimitTest) {
  double max_torque = 100;
  double min_torque = -10;
  double upper_limit = 10.;
  double upper_activation_threshold = 8.0;
  double lower_limit = 3.;
  double lower_activation_threshold = 4.;

  // zero output for out-of-activation values?
  ASSERT_EQ(SingleJointAvoidanceTorque(lower_limit, lower_activation_threshold,
                                       upper_limit, upper_activation_threshold,
                                       min_torque, max_torque, 6.),
            0.0);
  ASSERT_EQ(SingleJointAvoidanceTorque(lower_limit, lower_activation_threshold,
                                       upper_limit, upper_activation_threshold,
                                       min_torque, max_torque, 5.),
            0.0);
  ASSERT_EQ(SingleJointAvoidanceTorque(lower_limit, lower_activation_threshold,
                                       upper_limit, upper_activation_threshold,
                                       min_torque, max_torque, 5.99),
            0.0);
  ASSERT_EQ(SingleJointAvoidanceTorque(lower_limit, lower_activation_threshold,
                                       upper_limit, upper_activation_threshold,
                                       min_torque, max_torque, 5.5),
            0.0);

  // correct sign for in-activation zone
  ASSERT_GT(SingleJointAvoidanceTorque(lower_limit, lower_activation_threshold,
                                       upper_limit, upper_activation_threshold,
                                       min_torque, max_torque, 3.1),
            0.0);
  ASSERT_GT(SingleJointAvoidanceTorque(lower_limit, lower_activation_threshold,
                                       upper_limit, upper_activation_threshold,
                                       min_torque, max_torque, 3.99),
            0.0);
  // maximum output for over-limit values?
  ASSERT_EQ(SingleJointAvoidanceTorque(lower_limit, lower_activation_threshold,
                                       upper_limit, upper_activation_threshold,
                                       min_torque, max_torque, -3.1),
            max_torque);
  ASSERT_EQ(SingleJointAvoidanceTorque(lower_limit, lower_activation_threshold,
                                       upper_limit, upper_activation_threshold,
                                       min_torque, max_torque, -10e3),
            max_torque);
}

}  // namespace
}  // namespace control
}  // namespace intrinsic
