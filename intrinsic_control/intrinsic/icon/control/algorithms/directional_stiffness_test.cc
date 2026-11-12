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


#include "intrinsic/icon/control/algorithms/directional_stiffness.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdlib>

#include "absl/flags/parse.h"
#include "fuzztest/fuzztest.h"
#include "fuzztest/init_fuzztest.h"
#include "internal/testing.h"
#include "intrinsic/eigenmath/matchers.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/math/testing/fuzz_math_util.h"

namespace intrinsic::icon {
namespace {

using ::intrinsic::eigenmath::testing::EigenMatrixNear;
using ::testing::DoubleNear;
using ::testing::Each;

constexpr double kEpsilon = 1.0e-6;
constexpr double kBiggerEpsilon = 1.0e-5;
// Restrict the range of stiffness and direction values to avoid numeric issues.
constexpr double kMaxStiffnessValue = 1.0e7;
constexpr double kMaxDirectionValue = 1000.0;

fuzztest::Domain<double> ValidStiffness() {
  return fuzztest::InRange(0.0, kMaxStiffnessValue);
}

TEST(DirectionalStiffnessTest, ComputesCorrectStiffness) {
  ASSERT_OK_AND_ASSIGN(
      eigenmath::Matrix6d stiffness,
      ComputeDirectionalStiffness(
          /*translational_directions=*/{eigenmath::Vector3d(0.0, 0.0, 1.0)},
          /*rotational_directions=*/{},
          {.translational_stiffness_orthogonal_to_motion_direction = 20.0,
           .rotational_stiffness_orthogonal_to_motion_direction = 30.0}));

  EXPECT_EQ(stiffness, eigenmath::Matrix6d({{20.0, 0.0, 0.0, 0.0, 0.0, 0.0},
                                            {0.0, 20.0, 0.0, 0.0, 0.0, 0.0},
                                            {0.0, 0.0, 0.0, 0.0, 0.0, 0.0},
                                            {0.0, 0.0, 0.0, 30.0, 0.0, 0.0},
                                            {0.0, 0.0, 0.0, 0.0, 30.0, 0.0},
                                            {0.0, 0.0, 0.0, 0.0, 0.0, 30.0}}));
}

TEST(DirectionalStiffnessTest, ComputesCorrectStiffnessWithTwoDirections) {
  ASSERT_OK_AND_ASSIGN(
      eigenmath::Matrix6d stiffness,
      ComputeDirectionalStiffness(
          /*translational_directions=*/std::vector<
              eigenmath::Vector3d>{{0.0, 0.0, 1.0}, {0.0, 1.0, 0.0}},
          /*rotational_directions=*/{},
          {.translational_stiffness_orthogonal_to_motion_direction = 20.0,
           .rotational_stiffness_orthogonal_to_motion_direction = 30.0}));

  EXPECT_EQ(stiffness, eigenmath::Matrix6d({{20.0, 0.0, 0.0, 0.0, 0.0, 0.0},
                                            {0.0, 0.0, 0.0, 0.0, 0.0, 0.0},
                                            {0.0, 0.0, 0.0, 0.0, 0.0, 0.0},
                                            {0.0, 0.0, 0.0, 30.0, 0.0, 0.0},
                                            {0.0, 0.0, 0.0, 0.0, 30.0, 0.0},
                                            {0.0, 0.0, 0.0, 0.0, 0.0, 30.0}}));
}

TEST(DirectionalStiffnessTest, ComputesCorrectStiffnessWithThreeDirections) {
  ASSERT_OK_AND_ASSIGN(
      eigenmath::Matrix6d stiffness,
      ComputeDirectionalStiffness(
          /*translational_directions=*/std::vector<
              eigenmath::Vector3d>{{0.0, 0.0, 1.0},
                                   {0.0, 1.0, 0.0},
                                   {1.0, 0.0, 0.0}},
          /*rotational_directions=*/{},
          {.translational_stiffness_orthogonal_to_motion_direction = 20.0,
           .rotational_stiffness_orthogonal_to_motion_direction = 30.0}));
  EXPECT_THAT(
      stiffness,
      EigenMatrixNear(eigenmath::Matrix6d({{0.0, 0.0, 0.0, 0.0, 0.0, 0.0},
                                           {0.0, 0.0, 0.0, 0.0, 0.0, 0.0},
                                           {0.0, 0.0, 0.0, 0.0, 0.0, 0.0},
                                           {0.0, 0.0, 0.0, 30.0, 0.0, 0.0},
                                           {0.0, 0.0, 0.0, 0.0, 30.0, 0.0},
                                           {0.0, 0.0, 0.0, 0.0, 0.0, 30.0}}),
                      kEpsilon));
}

TEST(DirectionalStiffnessTest, ComputesCorrectStiffnessWithZeroDirectionss) {
  ASSERT_OK_AND_ASSIGN(
      eigenmath::Matrix6d stiffness,
      ComputeDirectionalStiffness(
          /*translational_directions=*/{},
          /*rotational_directions=*/{},
          {.translational_stiffness_orthogonal_to_motion_direction = 20.0,
           .rotational_stiffness_orthogonal_to_motion_direction = 30.0}));

  EXPECT_EQ(stiffness, eigenmath::Matrix6d({{20.0, 0.0, 0.0, 0.0, 0.0, 0.0},
                                            {0.0, 20.0, 0.0, 0.0, 0.0, 0.0},
                                            {0.0, 0.0, 20.0, 0.0, 0.0, 0.0},
                                            {0.0, 0.0, 0.0, 30.0, 0.0, 0.0},
                                            {0.0, 0.0, 0.0, 0.0, 30.0, 0.0},
                                            {0.0, 0.0, 0.0, 0.0, 0.0, 30.0}}));
}

TEST(DirectionalStiffnessTest,
     ComputesCorrectStiffnessWithRotationalDirections) {
  ASSERT_OK_AND_ASSIGN(
      eigenmath::Matrix6d stiffness,
      ComputeDirectionalStiffness(
          /*translational_directions=*/{eigenmath::Vector3d(0.0, 0.0, 1.0)},
          /*rotational_directions=*/{eigenmath::Vector3d(0.0, 0.0, 1.0)},
          {.translational_stiffness_orthogonal_to_motion_direction = 20.0,
           .rotational_stiffness_orthogonal_to_motion_direction = 30.0}));

  EXPECT_EQ(stiffness, eigenmath::Matrix6d({{20.0, 0.0, 0.0, 0.0, 0.0, 0.0},
                                            {0.0, 20.0, 0.0, 0.0, 0.0, 0.0},
                                            {0.0, 0.0, 0.0, 0.0, 0.0, 0.0},
                                            {0.0, 0.0, 0.0, 30.0, 0.0, 0.0},
                                            {0.0, 0.0, 0.0, 0.0, 30.0, 0.0},
                                            {0.0, 0.0, 0.0, 0.0, 0.0, 0.0}}));
}

TEST(DirectionalStiffnessTest,
     ComputesCorrectStiffnessWithTwoRotationalDirections) {
  ASSERT_OK_AND_ASSIGN(
      eigenmath::Matrix6d stiffness,
      ComputeDirectionalStiffness(
          /*translational_directions=*/{eigenmath::Vector3d(0.0, 0.0, 1.0)},
          /*rotational_directions=*/
          {eigenmath::Vector3d(0.0, 0.0, 1.0),
           eigenmath::Vector3d(0.0, 1.0, 0.0)},
          {.translational_stiffness_orthogonal_to_motion_direction = 20.0,
           .rotational_stiffness_orthogonal_to_motion_direction = 30.0}));

  EXPECT_EQ(stiffness, eigenmath::Matrix6d({{20.0, 0.0, 0.0, 0.0, 0.0, 0.0},
                                            {0.0, 20.0, 0.0, 0.0, 0.0, 0.0},
                                            {0.0, 0.0, 0.0, 0.0, 0.0, 0.0},
                                            {0.0, 0.0, 0.0, 30.0, 0.0, 0.0},
                                            {0.0, 0.0, 0.0, 0.0, 0.0, 0.0},
                                            {0.0, 0.0, 0.0, 0.0, 0.0, 0.0}}));
}

eigenmath::Vector3d GetOrthogonalVector(const eigenmath::Vector3d& vector) {
  double a = vector[0];
  double b = vector[1];
  double c = vector[2];
  eigenmath::Vector3d orthogonal_vector;
  if (std::abs(a) > std::abs(b)) {
    orthogonal_vector << -c, 0.0, a;
  } else if (std::abs(b) > std::abs(c)) {
    orthogonal_vector << -b, a, 0.0;
  } else {
    orthogonal_vector << 0.0, c, -b;
  }
  return orthogonal_vector.normalized();
}

void StiffnessInAllDirectionsIsCorrct(
    const eigenmath::Vector3d& translational_motion_direction,
    double translational_stiffness_orthogonal_to_motion_direction,
    double rotational_stiffness_orthogonal_to_motion_direction) {
  ASSERT_OK_AND_ASSIGN(
      eigenmath::Matrix6d stiffness,
      ComputeDirectionalStiffness(
          {translational_motion_direction}, /*rotational_directions=*/{},
          {translational_stiffness_orthogonal_to_motion_direction,
           rotational_stiffness_orthogonal_to_motion_direction}));

  const eigenmath::Vector3d translational_motion_direction_normalized =
      translational_motion_direction.normalized();

  // Check that the stiffness is zero along the translational motion direction.
  {
    eigenmath::Vector6d motion_direction = eigenmath::Vector6d::Zero();
    motion_direction.head<3>() = translational_motion_direction_normalized;

    eigenmath::Vector6d motion_direction_dot_stiffness =
        stiffness * motion_direction;

    EXPECT_THAT(motion_direction_dot_stiffness,
                Each(DoubleNear(0.0, kEpsilon)));
  }

  // Check stiffness orthogonal to the translational motion direction.
  {
    eigenmath::Vector3d orthogonal_translational_motion_direction =
        GetOrthogonalVector(translational_motion_direction_normalized);

    eigenmath::Vector6d motion_direction = eigenmath::Vector6d::Zero();
    motion_direction.head<3>() = orthogonal_translational_motion_direction;

    eigenmath::Vector6d motion_direction_dot_stiffness =
        stiffness * motion_direction;

    EXPECT_NEAR(motion_direction_dot_stiffness.norm(),
                translational_stiffness_orthogonal_to_motion_direction,
                kEpsilon);
  }

  // Check stiffness along the rotational motion direction.
  {
    eigenmath::Vector6d motion_direction = eigenmath::Vector6d::Zero();
    motion_direction.tail<3>() = translational_motion_direction_normalized;

    eigenmath::Vector6d motion_direction_dot_stiffness =
        stiffness * motion_direction;

    EXPECT_NEAR(motion_direction_dot_stiffness.norm(),
                rotational_stiffness_orthogonal_to_motion_direction, kEpsilon);
  }

  // Check stiffness orthogonal to the rotational motion direction.
  {
    eigenmath::Vector3d orthogonal_translational_motion_direction =
        GetOrthogonalVector(translational_motion_direction_normalized);

    eigenmath::Vector6d motion_direction = eigenmath::Vector6d::Zero();
    motion_direction.tail<3>() = orthogonal_translational_motion_direction;

    eigenmath::Vector6d motion_direction_dot_stiffness =
        stiffness * motion_direction;

    EXPECT_NEAR(motion_direction_dot_stiffness.norm(),
                rotational_stiffness_orthogonal_to_motion_direction, kEpsilon);
  }
}

FUZZ_TEST(DirectionalStiffnessTest, StiffnessInAllDirectionsIsCorrct)
    .WithDomains(testing::NonZeroVector3dWith(fuzztest::InRange<double>(
                     -kMaxDirectionValue, kMaxDirectionValue)),
                 ValidStiffness(), ValidStiffness());

void StiffnessInEveryTwoDirectionsIsCorrect(
    const eigenmath::Matrix3d& direction_matrix,
    double translational_stiffness_orthogonal_to_motion_direction,
    double rotational_stiffness_orthogonal_to_motion_direction) {
  const eigenmath::Vector3d translational_motion_direction_1 =
      direction_matrix.col(0);
  const eigenmath::Vector3d translational_motion_direction_2 =
      direction_matrix.col(1);
  ASSERT_OK_AND_ASSIGN(
      eigenmath::Matrix6d stiffness,
      ComputeDirectionalStiffness(
          std::vector<eigenmath::Vector3d>{translational_motion_direction_1,
                                           translational_motion_direction_2},
          /*rotational_directions=*/{},
          {translational_stiffness_orthogonal_to_motion_direction,
           rotational_stiffness_orthogonal_to_motion_direction}));
  const eigenmath::Vector3d translational_motion_direction_1_normalized =
      translational_motion_direction_1.normalized();
  const eigenmath::Vector3d translational_motion_direction_2_normalized =
      translational_motion_direction_2.normalized();

  // Check that the stiffness is zero in the translational motion plane.
  {
    eigenmath::Vector6d motion_direction = eigenmath::Vector6d::Zero();
    motion_direction.head<3>() = translational_motion_direction_1_normalized;

    eigenmath::Vector6d motion_direction_dot_stiffness =
        stiffness * motion_direction;

    EXPECT_THAT(motion_direction_dot_stiffness,
                Each(DoubleNear(0.0, kEpsilon)));
  }
  {
    eigenmath::Vector6d motion_direction = eigenmath::Vector6d::Zero();
    motion_direction.head<3>() = translational_motion_direction_2_normalized;

    eigenmath::Vector6d motion_direction_dot_stiffness =
        stiffness * motion_direction;

    EXPECT_THAT(motion_direction_dot_stiffness,
                Each(DoubleNear(0.0, kEpsilon)));
  }
  {
    eigenmath::Vector6d motion_direction = eigenmath::Vector6d::Zero();
    motion_direction.head<3>() = translational_motion_direction_1_normalized +
                                 translational_motion_direction_2_normalized;

    eigenmath::Vector6d motion_direction_dot_stiffness =
        stiffness * motion_direction;

    EXPECT_THAT(motion_direction_dot_stiffness,
                Each(DoubleNear(0.0, kEpsilon)));
  }

  // Check stiffness orthogonal to the translational motion direction.
  {
    eigenmath::Vector3d orthogonal_translational_motion_direction =
        translational_motion_direction_1_normalized
            .cross(translational_motion_direction_2_normalized)
            .normalized();

    eigenmath::Vector6d motion_direction = eigenmath::Vector6d::Zero();
    motion_direction.head<3>() = orthogonal_translational_motion_direction;

    eigenmath::Vector6d motion_direction_dot_stiffness =
        stiffness * motion_direction;

    EXPECT_NEAR(motion_direction_dot_stiffness.norm(),
                translational_stiffness_orthogonal_to_motion_direction,
                kEpsilon);
  }

  // Check stiffness along the rotational motion direction.
  {
    eigenmath::Vector6d motion_direction = eigenmath::Vector6d::Zero();
    motion_direction.tail<3>() = translational_motion_direction_1_normalized;

    eigenmath::Vector6d motion_direction_dot_stiffness =
        stiffness * motion_direction;

    EXPECT_NEAR(motion_direction_dot_stiffness.norm(),
                rotational_stiffness_orthogonal_to_motion_direction, kEpsilon);
  }
  {
    eigenmath::Vector6d motion_direction = eigenmath::Vector6d::Zero();
    motion_direction.tail<3>() = translational_motion_direction_2_normalized;

    eigenmath::Vector6d motion_direction_dot_stiffness =
        stiffness * motion_direction;

    EXPECT_NEAR(motion_direction_dot_stiffness.norm(),
                rotational_stiffness_orthogonal_to_motion_direction, kEpsilon);
  }
  {
    eigenmath::Vector6d motion_direction = eigenmath::Vector6d::Zero();
    motion_direction.tail<3>() = (translational_motion_direction_1_normalized +
                                  translational_motion_direction_2_normalized)
                                     .normalized();

    eigenmath::Vector6d motion_direction_dot_stiffness =
        stiffness * motion_direction;

    EXPECT_NEAR(motion_direction_dot_stiffness.norm(),
                rotational_stiffness_orthogonal_to_motion_direction,
                kBiggerEpsilon);
  }

  // Check stiffness orthogonal to the rotational motion direction.
  {
    eigenmath::Vector3d orthogonal_translational_motion_direction =
        translational_motion_direction_1_normalized
            .cross(translational_motion_direction_2_normalized)
            .normalized();

    eigenmath::Vector6d motion_direction = eigenmath::Vector6d::Zero();
    motion_direction.tail<3>() = orthogonal_translational_motion_direction;

    eigenmath::Vector6d motion_direction_dot_stiffness =
        stiffness * motion_direction;

    EXPECT_NEAR(motion_direction_dot_stiffness.norm(),
                rotational_stiffness_orthogonal_to_motion_direction,
                kBiggerEpsilon);
  }
}

FUZZ_TEST(DirectionalStiffnessTest, StiffnessInEveryTwoDirectionsIsCorrect)
    .WithDomains(testing::FullRankMatrix3dWith(fuzztest::InRange<double>(
                     -kMaxDirectionValue, kMaxDirectionValue)),
                 ValidStiffness(), ValidStiffness());

void StiffnessInEveryThreeDirectionsIsCorrct(
    const eigenmath::Matrix3d& direction_matrix,
    double translational_stiffness_orthogonal_to_motion_direction,
    double rotational_stiffness_orthogonal_to_motion_direction) {
  const eigenmath::Vector3d translational_motion_direction_1 =
      direction_matrix.col(0);
  const eigenmath::Vector3d translational_motion_direction_2 =
      direction_matrix.col(1);
  const eigenmath::Vector3d translational_motion_direction_3 =
      direction_matrix.col(2);
  ASSERT_OK_AND_ASSIGN(
      eigenmath::Matrix6d stiffness,
      ComputeDirectionalStiffness(
          std::vector<eigenmath::Vector3d>{translational_motion_direction_1,
                                           translational_motion_direction_2,
                                           translational_motion_direction_3},
          /*rotational_directions=*/{},
          {translational_stiffness_orthogonal_to_motion_direction,
           rotational_stiffness_orthogonal_to_motion_direction}));

  // Check that the stiffness is zero in all translational directions.
  EXPECT_THAT(
      stiffness,
      EigenMatrixNear(
          eigenmath::Matrix6d(
              {{0.0, 0.0, 0.0, 0.0, 0.0, 0.0},
               {0.0, 0.0, 0.0, 0.0, 0.0, 0.0},
               {0.0, 0.0, 0.0, 0.0, 0.0, 0.0},
               {0.0, 0.0, 0.0,
                rotational_stiffness_orthogonal_to_motion_direction, 0.0, 0.0},
               {0.0, 0.0, 0.0, 0.0,
                rotational_stiffness_orthogonal_to_motion_direction, 0.0},
               {0.0, 0.0, 0.0, 0.0, 0.0,
                rotational_stiffness_orthogonal_to_motion_direction}}),
          kEpsilon));
}

FUZZ_TEST(DirectionalStiffnessTest, StiffnessInEveryThreeDirectionsIsCorrct)
    .WithDomains(testing::FullRankMatrix3dWith(fuzztest::InRange<double>(
                     -kMaxDirectionValue, kMaxDirectionValue)),
                 ValidStiffness(), ValidStiffness());

}  // namespace
}  // namespace intrinsic::icon

int main(int argc, char** argv) {
  fuzztest::ParseAbslFlags(argc, argv);
  fuzztest::InitFuzzTest(&argc, &argv);
  testing::InitGoogleMock(&argc, argv);
  absl::ParseCommandLine(argc, argv);
  return RUN_ALL_TESTS();
}
