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

#include "intrinsic/icon/control/algorithms/overdamping.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <string>

#include "absl/status/status.h"
#include "internal/testing.h"
#include "intrinsic/eigenmath/matchers.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/testing/malloc_test.h"
#include "intrinsic/icon/utils/realtime_status_matchers.h"

using ::intrinsic::eigenmath::testing::EigenMatrixNear;
using ::testing::DoubleNear;
using ::testing::HasSubstr;

constexpr double kSmallTolerance = 5e-4;
constexpr double kLargeTolerance = 0.16;

namespace intrinsic::icon {
namespace {

TEST(ComputeOverdampingConstantFromStiffnessScalarsFailsTest,
     ComputeOverdampingConstantFromStiffnessScalarsFailsWithNegativeStiffness) {
  for (int i = 0; i < 4; ++i) {
    double stiffness_robot_joints = 1.0;
    double stiffness_compliance_device = 1.0;
    double stiffness_environment = 1.0;
    double desired_virtual_stiffness_contact = 1.0;
    std::string expected_problematic_stiffness_name;
    if (i == 0) {
      stiffness_robot_joints = -1.0;
      expected_problematic_stiffness_name = "stiffness_robot_joints";
    } else if (i == 1) {
      stiffness_compliance_device = -1.0;
      expected_problematic_stiffness_name = "stiffness_compliance_device";
    } else if (i == 2) {
      stiffness_environment = -1.0;
      expected_problematic_stiffness_name = "stiffness_environment";
    } else {
      desired_virtual_stiffness_contact = -1.0;
      expected_problematic_stiffness_name = "desired_virtual_stiffness_contact";
    }
    EXPECT_THAT(
        ComputeOverdampingConstantFromStiffnessScalars(
            OverdampingFromStiffnessScalarsInputParameters{
                .desired_virtual_stiffness_contact =
                    desired_virtual_stiffness_contact,
                .stiffness_compliance_device = stiffness_compliance_device,
                .stiffness_robot_joints = stiffness_robot_joints,
                .stiffness_environment = stiffness_environment}),
        RealtimeStatusIs(
            absl::StatusCode::kInvalidArgument,
            AllOf(HasSubstr(expected_problematic_stiffness_name),
                  HasSubstr(" must be >= "), HasSubstr(", but got "),
                  HasSubstr("-1 instead."))));
  }
}

TEST(ComputeOverdampingConstantFromStiffnessScalarsFailsTest,
     ComputeOverdampingConstantFromStiffnessScalarsFailsWithZero) {
  for (int i = 0; i < 4; ++i) {
    double stiffness_robot_joints = 1.0;
    double stiffness_compliance_device = 1.0;
    double stiffness_environment = 1.0;
    double desired_virtual_stiffness_contact = 1.0;
    std::string expected_problematic_stiffness_name;
    if (i == 0) {
      stiffness_robot_joints = 0.0;
      expected_problematic_stiffness_name = "stiffness_robot_joints";
    } else if (i == 1) {
      stiffness_compliance_device = 0.0;
      expected_problematic_stiffness_name = "stiffness_compliance_device";
    } else if (i == 2) {
      stiffness_environment = 0.0;
      expected_problematic_stiffness_name = "stiffness_environment";
    } else {
      desired_virtual_stiffness_contact = 0.0;
      expected_problematic_stiffness_name = "desired_virtual_stiffness_contact";
    }
    EXPECT_THAT(
        ComputeOverdampingConstantFromStiffnessScalars(
            OverdampingFromStiffnessScalarsInputParameters{
                .desired_virtual_stiffness_contact =
                    desired_virtual_stiffness_contact,
                .stiffness_compliance_device = stiffness_compliance_device,
                .stiffness_robot_joints = stiffness_robot_joints,
                .stiffness_environment = stiffness_environment}),
        RealtimeStatusIs(
            absl::StatusCode::kInvalidArgument,
            AllOf(HasSubstr(expected_problematic_stiffness_name),
                  HasSubstr(" must be >= "), HasSubstr(", but got "),
                  HasSubstr("0 instead."))));
  }
}

struct ComputeOverdampingConstantFromStiffnessScalarsWorksTestCase {
  double stiffness_robot_joints = std::numeric_limits<double>::infinity();
  double stiffness_compliance_device;
  double stiffness_environment = std::numeric_limits<double>::infinity();
  double desired_virtual_stiffness_contact;
  double expected_overdamping_constant_benady;
  double expected_overdamping_constant_h_infinity;
  double expected_overdamping_constant_spr;
  double tolerance;
};

using ComputeOverdampingConstantFromStiffnessScalarsWorksTest =
    ::testing::TestWithParam<
        ComputeOverdampingConstantFromStiffnessScalarsWorksTestCase>;

TEST_P(ComputeOverdampingConstantFromStiffnessScalarsWorksTest,
       ComputeOverdampingConstantFromStiffnessScalarsWorks) {
  const ComputeOverdampingConstantFromStiffnessScalarsWorksTestCase& test_case =
      GetParam();
  EXPECT_THAT(
      ComputeOverdampingConstantFromStiffnessScalars(
          OverdampingFromStiffnessScalarsInputParameters{
              .desired_virtual_stiffness_contact =
                  test_case.desired_virtual_stiffness_contact,
              .stiffness_compliance_device =
                  test_case.stiffness_compliance_device,
              .stiffness_robot_joints = test_case.stiffness_robot_joints,
              .stiffness_environment = test_case.stiffness_environment,
              .criterion = OverdampingConstantCriterion::kBenady}),
      RealtimeIsOkAndHolds(
          DoubleNear(test_case.expected_overdamping_constant_benady,
                     test_case.tolerance)));
  EXPECT_THAT(
      ComputeOverdampingConstantFromStiffnessScalars(
          OverdampingFromStiffnessScalarsInputParameters{
              .desired_virtual_stiffness_contact =
                  test_case.desired_virtual_stiffness_contact,
              .stiffness_compliance_device =
                  test_case.stiffness_compliance_device,
              .stiffness_robot_joints = test_case.stiffness_robot_joints,
              .stiffness_environment = test_case.stiffness_environment,
              .criterion = OverdampingConstantCriterion::kHInfinity}),
      RealtimeIsOkAndHolds(
          DoubleNear(test_case.expected_overdamping_constant_h_infinity,
                     test_case.tolerance)));
  EXPECT_THAT(
      ComputeOverdampingConstantFromStiffnessScalars(
          OverdampingFromStiffnessScalarsInputParameters{
              .desired_virtual_stiffness_contact =
                  test_case.desired_virtual_stiffness_contact,
              .stiffness_compliance_device =
                  test_case.stiffness_compliance_device,
              .stiffness_robot_joints = test_case.stiffness_robot_joints,
              .stiffness_environment = test_case.stiffness_environment,
              .criterion =
                  OverdampingConstantCriterion::kStrictlyPositiveReal}),
      RealtimeIsOkAndHolds(DoubleNear(
          test_case.expected_overdamping_constant_spr, test_case.tolerance)));
}

TEST_P(ComputeOverdampingConstantFromStiffnessScalarsWorksTest, NoMalloc) {
  for (const auto criterion :
       {OverdampingConstantCriterion::kBenady,
        OverdampingConstantCriterion::kHInfinity,
        OverdampingConstantCriterion::kStrictlyPositiveReal}) {
    IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
    INTRINSIC_RT_ASSERT_OK(ComputeOverdampingConstantFromStiffnessScalars(
        OverdampingFromStiffnessScalarsInputParameters{
            .desired_virtual_stiffness_contact =
                GetParam().desired_virtual_stiffness_contact,
            .stiffness_compliance_device =
                GetParam().stiffness_compliance_device,
            .stiffness_robot_joints = GetParam().stiffness_robot_joints,
            .stiffness_environment = GetParam().stiffness_environment,
            .criterion = criterion}));
    IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
  }
}

INSTANTIATE_TEST_SUITE_P(
    ComputeOverdampingConstantFromStiffnessScalarsWorksTests,
    ComputeOverdampingConstantFromStiffnessScalarsWorksTest,
    ::testing::Values<
        ComputeOverdampingConstantFromStiffnessScalarsWorksTestCase>(
        // Scenario 0: trivial case #0
        ComputeOverdampingConstantFromStiffnessScalarsWorksTestCase{
            .stiffness_compliance_device = 16.0,
            .desired_virtual_stiffness_contact = 2.0,
            .expected_overdamping_constant_benady = 3.0,
            .expected_overdamping_constant_h_infinity = 0.5 *
                                                        (sqrt(17.0) - 1.0),
            .expected_overdamping_constant_spr = 1.0,
            .tolerance = kSmallTolerance,
        },
        // Scenario 1: trivial case #1
        ComputeOverdampingConstantFromStiffnessScalarsWorksTestCase{
            .stiffness_compliance_device = 32.0,
            .stiffness_environment = 32.0,
            .desired_virtual_stiffness_contact = 2.0,
            .expected_overdamping_constant_benady = 3.0,
            .expected_overdamping_constant_h_infinity = 0.5 *
                                                        (sqrt(17.0) - 1.0),
            .expected_overdamping_constant_spr = 1.0,
            .tolerance = kSmallTolerance,
        },
        // Scenario 2: trivial case #2
        ComputeOverdampingConstantFromStiffnessScalarsWorksTestCase{
            .stiffness_compliance_device = 16.0,
            .stiffness_environment = 1.0e32,
            .desired_virtual_stiffness_contact = 2.0,
            .expected_overdamping_constant_benady = 3.0,
            .expected_overdamping_constant_h_infinity = 0.5 *
                                                        (sqrt(17.0) - 1.0),
            .expected_overdamping_constant_spr = 1.0,
            .tolerance = kSmallTolerance,
        },
        // Scenario 3: an infinite environment stiffness and a small positive
        // robot joint stiffness
        ComputeOverdampingConstantFromStiffnessScalarsWorksTestCase{
            .stiffness_robot_joints = 2.0,
            .stiffness_compliance_device = 4.0,
            .desired_virtual_stiffness_contact = 1.0 / 3.0,
            .expected_overdamping_constant_benady = sqrt(5.0),
            .expected_overdamping_constant_h_infinity = 1.0,
            .expected_overdamping_constant_spr = 0.5 * (sqrt(5.0) - 1.0),
            .tolerance = kSmallTolerance,
        },
        // Scenario 4: a small positive environment stiffness and an infinite
        // robot joint stiffness
        ComputeOverdampingConstantFromStiffnessScalarsWorksTestCase{
            .stiffness_compliance_device = 2.0,
            .stiffness_environment = 8.0,
            .desired_virtual_stiffness_contact = 0.2,
            .expected_overdamping_constant_benady = 3.0,
            .expected_overdamping_constant_h_infinity = 0.5 *
                                                        (sqrt(17.0) - 1.0),
            .expected_overdamping_constant_spr = 1.0,
            .tolerance = kSmallTolerance,
        },
        // Scenario 5: all small positive stiffness scalars
        ComputeOverdampingConstantFromStiffnessScalarsWorksTestCase{
            .stiffness_robot_joints = 8.0,
            .stiffness_compliance_device = 2.0,
            .stiffness_environment = 16.0,
            .desired_virtual_stiffness_contact = 1.0 / 11.0,
            .expected_overdamping_constant_benady = sqrt(17.0),
            .expected_overdamping_constant_h_infinity = 0.5 *
                                                        (sqrt(33.0) - 1.0),
            .expected_overdamping_constant_spr = 0.5 * (sqrt(17.0) - 1.0),
            .tolerance = kSmallTolerance,
        },
        // Scenario 6: all infinite stiffness scalars
        ComputeOverdampingConstantFromStiffnessScalarsWorksTestCase{
            .stiffness_compliance_device =
                std::numeric_limits<double>::infinity(),
            .desired_virtual_stiffness_contact =
                std::numeric_limits<double>::infinity(),
            .expected_overdamping_constant_benady = 1.0,
            .expected_overdamping_constant_h_infinity = 0.0,
            .expected_overdamping_constant_spr = 0.0,
            .tolerance =
                kLargeTolerance,  // because mostly affected by numerical
                                  // issues of processing very large numbers
        }));

TEST(ComputeOverdampingTest, ComputeOverdampingReturnsCorrectMatrix) {
  const eigenmath::Matrix6d inertia = eigenmath::Matrix6d::Identity() * 10.0;
  const double kEnvironmentStiffness = 10000.0;
  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
      const eigenmath::MatrixNd overdamping,
      ComputeOverdamping(inertia, kEnvironmentStiffness));

  // The expected overdamping matrix is computed manually: D = 2 * sqrt(M) *
  // sqrt(K_D + K_E) with K_D = 1234.0 (mock stiffness) and K_E = 10000.0 (test
  // environment stiffness).
  EXPECT_THAT(overdamping,
              EigenMatrixNear(eigenmath::Matrix6d::Identity() * 670.343196,
                              kSmallTolerance));
}

}  // namespace
}  // namespace intrinsic::icon
