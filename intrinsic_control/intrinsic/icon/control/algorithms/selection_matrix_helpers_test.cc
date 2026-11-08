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

#include "intrinsic/icon/control/algorithms/selection_matrix_helpers.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "internal/testing.h"
#include "intrinsic/eigenmath/matchers.h"
#include "intrinsic/eigenmath/types.h"

using ::absl_testing::StatusIs;
using ::intrinsic::eigenmath::testing::IsApprox;
using ::testing::HasSubstr;

namespace intrinsic::icon {
namespace {

TEST(SelectionMatrixHelpersTest, FailsIfBooleanSelectionEmpty) {
  EXPECT_THAT(ComputeWrenchSelectionMatrixFromBooleans(false, false, false,
                                                       false, false, false),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       HasSubstr("one DoF must be free")));
}

TEST(SelectionMatrixHelpersTest, ReturnsIdentityMatrixIfAllBooleansTrue) {
  ASSERT_OK_AND_ASSIGN(auto P, ComputeWrenchSelectionMatrixFromBooleans(
                                   true, true, true, true, true, true));
  EXPECT_THAT(P, IsApprox(eigenmath::Matrix6d::Identity()));
}

TEST(SelectionMatrixHelpersTest, TestBooleanPattern) {
  ASSERT_OK_AND_ASSIGN(auto P, ComputeWrenchSelectionMatrixFromBooleans(
                                   true, false, true, false, true, true));

  eigenmath::Matrix6d expected_result = eigenmath::Matrix6d::Zero();
  expected_result.diagonal() << 1, 0, 1, 0, 1, 1;
  EXPECT_THAT(P, IsApprox(expected_result));
}

TEST(SelectionMatrixHelpersTest, WrongVectorDimensionsError) {
  // Too many vector entries.
  EXPECT_THAT(ComputeWrenchSelectionMatrixForCompliantMotionInSubspaceSpannedBy(
                  {eigenmath::Vector3d::UnitX(), eigenmath::Vector3d::UnitX(),
                   eigenmath::Vector3d::UnitX(), eigenmath::Vector3d::UnitX()},
                  {
                      eigenmath::Vector3d::UnitX(),
                      eigenmath::Vector3d::UnitZ(),
                      eigenmath::Vector3d::UnitY(),
                  }),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       HasSubstr("Too many translational basis vectors")));

  EXPECT_THAT(ComputeWrenchSelectionMatrixForCompliantMotionInSubspaceSpannedBy(
                  {eigenmath::Vector3d::UnitX(), eigenmath::Vector3d::UnitX(),
                   eigenmath::Vector3d::UnitX()},
                  {
                      eigenmath::Vector3d::UnitX(),
                      eigenmath::Vector3d::UnitX(),
                      eigenmath::Vector3d::UnitX(),
                      eigenmath::Vector3d::UnitX(),
                  }),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       HasSubstr("Too many rotational basis vectors")));

  EXPECT_THAT(
      ComputeWrenchSelectionMatrixForCompliantMotionInSubspaceSpannedBy({}, {}),
      StatusIs(absl::StatusCode::kFailedPrecondition,
               HasSubstr("one DoF must be free")));
}

TEST(SelectionMatrixHelpersTest,
     ComputeWrenchSelectionMatrixFromVectorsIsIdentity) {
  // Compute projection matrix P for a selection of basis vectors which covers
  // the full 6d space.
  ASSERT_OK_AND_ASSIGN(
      auto P, ComputeWrenchSelectionMatrixForCompliantMotionInSubspaceSpannedBy(
                  {eigenmath::Vector3d::UnitZ(), eigenmath::Vector3d::UnitY(),
                   eigenmath::Vector3d::UnitX()},
                  {
                      eigenmath::Vector3d::UnitX(),
                      eigenmath::Vector3d::UnitZ(),
                      eigenmath::Vector3d::UnitY(),
                  }));

  EXPECT_THAT(P, IsApprox(eigenmath::Matrix6d::Identity()));
}

TEST(SelectionMatrixHelpersTest, ComputeWrenchSelectionMatrixFromVectorsWorks) {
  // Compute projection matrix P for a selection of basis vectors which covers
  // the full 6d space.
  ASSERT_OK_AND_ASSIGN(
      auto P,
      ComputeWrenchSelectionMatrixForCompliantMotionInSubspaceSpannedBy(
          {eigenmath::Vector3d::UnitZ()}, {
                                              eigenmath::Vector3d::UnitY(),
                                          }));

  eigenmath::Matrix6d expected_result = eigenmath::Matrix6d::Zero();
  expected_result.diagonal() << 0, 0, 1, 0, 1, 0;
  EXPECT_THAT(P, IsApprox(expected_result));
}

TEST(SelectionMatrixHelpersTest, CollinearBasisVectorsResultInError) {
  // Collinear vectors in the basis should result in an error.
  EXPECT_THAT(
      ComputeWrenchSelectionMatrixForCompliantMotionInSubspaceSpannedBy(
          {eigenmath::Vector3d::UnitX(), eigenmath::Vector3d::UnitX()}, {}),
      StatusIs(absl::StatusCode::kFailedPrecondition,
               HasSubstr("C's columns are not linearly independent")));
}

TEST(SelectionMatrixHelpersTest, WrongInputMatrixDimensionsCausesError) {
  eigenmath::Matrix6Nd C(6, 7);
  EXPECT_THAT(
      ComputeWrenchSelectionMatrixForCompliantMotionInSubspaceSpannedBy(C),
      StatusIs(absl::StatusCode::kFailedPrecondition,
               HasSubstr("more columns than rows")));
  EXPECT_THAT(ComputeWrenchSelectionMatrixForCompliantMotionInNullspaceOf(C),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       HasSubstr("more columns than rows")));
}

}  // namespace
}  // namespace intrinsic::icon
