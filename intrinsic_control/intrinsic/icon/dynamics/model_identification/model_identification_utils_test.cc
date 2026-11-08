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

#include "intrinsic/icon/dynamics/model_identification/model_identification_utils.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <vector>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "internal/testing.h"
#include "intrinsic/eigenmath/types.h"

namespace intrinsic::icon {
namespace {

using ::absl_testing::StatusIs;
using ::testing::HasSubstr;
using ::testing::SizeIs;

TEST(ExecutionRecord, SetSizeFailsDueToInvalidSize) {
  ExecutionRecord execution_record;
  EXPECT_THAT(execution_record.SetSize(-1),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Number of DoFs must be non-negative.")));
}

TEST(ExecutionRecord, SetSizeSucceeds) {
  ExecutionRecord execution_record;
  EXPECT_OK(execution_record.SetSize(2));
  EXPECT_THAT(execution_record.measured_state, SizeIs(2));
  EXPECT_THAT(execution_record.commanded_state, SizeIs(2));
}

TEST(RegressionPair, SetSizeFailsDueToInvalidSize) {
  {
    RegressionPair regression_pair;
    EXPECT_THAT(regression_pair.SetSize(-1, 0),
                StatusIs(absl::StatusCode::kInvalidArgument,
                         HasSubstr("Rows must be non-negative.")));
  }
  {
    RegressionPair regression_pair;
    EXPECT_THAT(regression_pair.SetSize(0, -1),
                StatusIs(absl::StatusCode::kInvalidArgument,
                         HasSubstr("Cols must be non-negative.")));
  }
}

TEST(RegressionPair, SetSizeSucceeds) {
  RegressionPair regression_pair;
  EXPECT_OK(regression_pair.SetSize(2, 3));
  EXPECT_THAT(regression_pair.reg_vector, SizeIs(2));
  EXPECT_THAT(regression_pair.reg_matrix, SizeIs(6));
  EXPECT_EQ(regression_pair.reg_matrix.rows(), 2);
  EXPECT_EQ(regression_pair.reg_matrix.cols(), 3);
}

TEST(RegressionPair, ConstructCumulativeRegressorFailsDueToEmptyInput) {
  EXPECT_THAT(ConstructCumulativeRegressor({}),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("No regression pairs provided.")));
}

TEST(RegressionPair, FailsDueToRowMismatchInRegressionPairs) {
  std::vector<RegressionPair> reg_pairs;
  reg_pairs.push_back(
      RegressionPair{.reg_vector = eigenmath::VectorXd::Zero(3),
                     .reg_matrix = eigenmath::MatrixXd::Zero(3, 3)});
  reg_pairs.push_back(
      RegressionPair{.reg_vector = eigenmath::VectorXd::Zero(4),
                     .reg_matrix = eigenmath::MatrixXd::Zero(2, 4)});
  EXPECT_THAT(ConstructCumulativeRegressor(reg_pairs),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Rows of the regressor matrix and regressor "
                                 "vector must match.")));
}

TEST(RegressionPair, FailsDueToColumnMismatchInRegressionPairs) {
  std::vector<RegressionPair> reg_pairs;
  reg_pairs.push_back(
      RegressionPair{.reg_vector = eigenmath::VectorXd::Zero(3),
                     .reg_matrix = eigenmath::MatrixXd::Zero(3, 3)});
  reg_pairs.push_back(
      RegressionPair{.reg_vector = eigenmath::VectorXd::Zero(3),
                     .reg_matrix = eigenmath::MatrixXd::Zero(3, 4)});
  EXPECT_THAT(
      ConstructCumulativeRegressor(reg_pairs),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Columns of the regressor matrix must be the same.")));
}

TEST(RegressionPair, ComputesCorrectCumulativeRegressor) {
  const eigenmath::VectorXd ones_vector = eigenmath::VectorXd::Ones(3);
  const eigenmath::MatrixXd identity_matrix =
      eigenmath::MatrixXd::Identity(3, 3);

  std::vector<RegressionPair> reg_pairs;
  reg_pairs.push_back(
      RegressionPair{.reg_vector = ones_vector, .reg_matrix = identity_matrix});
  reg_pairs.push_back(RegressionPair{.reg_vector = 2.0 * ones_vector,
                                     .reg_matrix = 2.0 * identity_matrix});
  ASSERT_OK_AND_ASSIGN(const RegressionPair cumulative_reg,
                       ConstructCumulativeRegressor(reg_pairs));
  EXPECT_EQ(
      cumulative_reg.reg_vector,
      0.5 * (reg_pairs[0].reg_matrix.transpose() * reg_pairs[0].reg_vector +
             reg_pairs[1].reg_matrix.transpose() * reg_pairs[1].reg_vector));
  EXPECT_EQ(
      cumulative_reg.reg_matrix,
      0.5 * (reg_pairs[0].reg_matrix.transpose() * reg_pairs[0].reg_matrix +
             reg_pairs[1].reg_matrix.transpose() * reg_pairs[1].reg_matrix));
}

}  // namespace
}  // namespace intrinsic::icon
