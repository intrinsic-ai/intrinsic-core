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

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/icon/utils/realtime_status_macro.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic::icon {

absl::Status ExecutionRecord::SetSize(int ndofs) {
  if (ndofs < 0) {
    return icon::InvalidArgumentError("Number of DoFs must be non-negative.");
  }
  INTRINSIC_RT_RETURN_IF_ERROR(measured_state.SetSize(ndofs));
  INTRINSIC_RT_RETURN_IF_ERROR(commanded_state.SetSize(ndofs));
  return absl::OkStatus();
}

absl::Status RegressionPair::SetSize(int rows, int cols) {
  if (rows < 0) {
    return icon::InvalidArgumentError("Rows must be non-negative.");
  }
  if (cols < 0) {
    return icon::InvalidArgumentError("Cols must be non-negative.");
  }
  reg_vector = eigenmath::VectorNd::Zero(rows);
  reg_matrix = eigenmath::MatrixNd::Zero(rows, cols);
  return absl::OkStatus();
}

absl::StatusOr<RegressionPair> ConstructCumulativeRegressor(
    absl::Span<const RegressionPair> composite_regression_pairs) {
  if (composite_regression_pairs.empty()) {
    return absl::InvalidArgumentError("No regression pairs provided.");
  }
  const int num_rows = composite_regression_pairs[0].reg_matrix.rows();
  const int num_cols = composite_regression_pairs[0].reg_matrix.cols();
  for (int id = 1; id < composite_regression_pairs.size(); ++id) {
    const RegressionPair& reg_pair = composite_regression_pairs[id];
    if (reg_pair.reg_matrix.rows() != num_rows ||
        reg_pair.reg_vector.size() != num_rows) {
      return absl::InvalidArgumentError(
          "Rows of the regressor matrix and regressor vector must match.");
    }
    if (reg_pair.reg_matrix.cols() != num_cols) {
      return absl::InvalidArgumentError(
          "Columns of the regressor matrix must be the same.");
    }
  }

  RegressionPair result_pair;
  INTR_RETURN_IF_ERROR(result_pair.SetSize(num_cols, num_cols));
  for (const RegressionPair& reg_pair : composite_regression_pairs) {
    result_pair.reg_vector +=
        reg_pair.reg_matrix.transpose() * reg_pair.reg_vector;
    result_pair.reg_matrix +=
        reg_pair.reg_matrix.transpose() * reg_pair.reg_matrix;
  }
  result_pair.reg_matrix /= composite_regression_pairs.size();
  result_pair.reg_vector /= composite_regression_pairs.size();
  return result_pair;
}

}  // namespace intrinsic::icon
