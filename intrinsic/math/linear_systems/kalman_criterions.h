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

#ifndef INTRINSIC_MATH_LINEAR_SYSTEMS_KALMAN_CRITERIONS_H_
#define INTRINSIC_MATH_LINEAR_SYSTEMS_KALMAN_CRITERIONS_H_

#include "absl/status/statusor.h"
#include "intrinsic/eigenmath/types.h"

namespace intrinsic {

// Computes the Kalman controllability matrix for a discrete or continuous-time
// linear system.
absl::StatusOr<eigenmath::MatrixXd> ComputeControllabilityMatrix(
    const eigenmath::MatrixXd& A, const eigenmath::MatrixXd& B);

// Evaluates controllability according to the Kalman criterion for a discrete or
// continuous-time linear system. If the row-rank of the controllability matrix
// is equal to the state dimensionality, the system is controllable.
absl::StatusOr<bool> IsControllable(const eigenmath::MatrixXd& A,
                                    const eigenmath::MatrixXd& B);

// Evaluates observability according to the Kalman criterion for a discrete or
// continuous-time linear system. If the column-rank of the observability matrix
// is equal to the state dimensionality, the system is observable.
absl::StatusOr<bool> IsObservable(const eigenmath::MatrixXd& A,
                                  const eigenmath::MatrixXd& C);

}  // namespace intrinsic

#endif  // INTRINSIC_MATH_LINEAR_SYSTEMS_KALMAN_CRITERIONS_H_
