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

#include "intrinsic/math/linear_systems/kalman_criterions.h"

#include <cstddef>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic {

absl::StatusOr<eigenmath::MatrixXd> ComputeControllabilityMatrix(
    const eigenmath::MatrixXd& A, const eigenmath::MatrixXd& B) {
  const int state_dim = A.rows();
  const int control_dim = B.cols();

  if (state_dim < 1 || control_dim < 1 || B.rows() != state_dim ||
      A.cols() != state_dim) {
    return absl::FailedPreconditionError("System dimensions inconsistent.");
  }

  eigenmath::MatrixXd controllability_matrix(state_dim,
                                             state_dim * control_dim);

  controllability_matrix.block(0, 0, state_dim, control_dim) = B;

  for (size_t i = 1; i < state_dim; i++) {
    controllability_matrix.block(0, i * control_dim, state_dim, control_dim) =
        A * controllability_matrix.block(0, (i - 1) * control_dim, state_dim,
                                         control_dim);
  }

  return controllability_matrix;
}

absl::StatusOr<bool> IsControllable(const eigenmath::MatrixXd& A,
                                    const eigenmath::MatrixXd& B) {
  INTR_ASSIGN_OR_RETURN(eigenmath::MatrixXd controllablity_matrix,
                        ComputeControllabilityMatrix(A, B));
  Eigen::FullPivLU<eigenmath::MatrixXd> lu(controllablity_matrix);
  return lu.rank() == A.cols();
}

absl::StatusOr<bool> IsObservable(const eigenmath::MatrixXd& A,
                                  const eigenmath::MatrixXd& C) {
  // Exploit duality between controllability and observability:
  return IsControllable(A.transpose(), C.transpose());
}

}  // namespace intrinsic
