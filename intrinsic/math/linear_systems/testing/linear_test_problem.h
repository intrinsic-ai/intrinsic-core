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

#ifndef INTRINSIC_MATH_LINEAR_SYSTEMS_TESTING_LINEAR_TEST_PROBLEM_H_
#define INTRINSIC_MATH_LINEAR_SYSTEMS_TESTING_LINEAR_TEST_PROBLEM_H_

#include "intrinsic/eigenmath/types.h"

namespace intrinsic {

struct LinearTestProblem {
  static constexpr size_t kStateDim = 5;
  static constexpr size_t kControlDim = 2;
  static constexpr size_t kOutputDim = 3;
  static constexpr double kEpsilon = 1e-5;

  // Creates a discrete-time, controllable, linear time-invariant system and
  // positive definite weighting matices for cost computation. The system is
  // physically meaningless.
  static LinearTestProblem Create() {
    LinearTestProblem problem;

    // A system dynamics matrix with direct feedthrough and small dynamic
    // coupling on the top right off-diagonal.
    problem.A.setIdentity(kStateDim, kStateDim);
    problem.A.topRightCorner(4, 4).diagonal().setConstant(0.1);

    // Control input matrix with direct and partially coupled action on the two
    // last states.
    problem.B.resize(kStateDim, kControlDim);
    problem.B.setZero();
    problem.B(4, 0) = 1.0;
    problem.B(3, 1) = 1.0;
    problem.B(3, 0) = 0.1;

    // System output matrix with meaningless artificial coupling between states.
    problem.C.resize(kOutputDim, kStateDim);
    problem.C.setZero();
    problem.C(0, 0) = 1;
    problem.C(0, 1) = 0.1;
    problem.C(1, 2) = 1;
    problem.C(1, 3) = 0.1;
    problem.C(2, 4) = 1;

    // A non-trivial, tri-diagonal symmetric positive definite covariance
    // matrix.
    problem.P.resize(kStateDim, kStateDim);
    problem.P.setZero();
    problem.P.diagonal().setConstant(5.0);
    problem.P.topRightCorner(4, 4).diagonal().setConstant(2.0);
    problem.P.bottomLeftCorner(4, 4).diagonal().setConstant(2.0);

    // A non-trivial, tri-diagonal symmetric positive definite state-weighting
    // matrix.
    problem.Q.resize(kStateDim, kStateDim);
    problem.Q.setZero();
    problem.Q.diagonal().setConstant(3.0);
    problem.Q.topRightCorner(4, 4).diagonal().setConstant(1.0);
    problem.Q.bottomLeftCorner(4, 4).diagonal().setConstant(1.0);

    // An identity input-weighting matrix.
    problem.R.setIdentity(kControlDim, kControlDim);

    // A non-trivial, tri-diagonal symmetric positive definite output-weighting
    // matrix.
    problem.V.resize(kOutputDim, kOutputDim);
    problem.V << 3, 1, 0,  //
        1, 3, 1,           //
        0, 1, 3;           //

    return problem;
  }

  eigenmath::MatrixXd A;  // NOLINT(google3-readability-class-member-naming)
  eigenmath::MatrixXd B;  // NOLINT(google3-readability-class-member-naming)
  eigenmath::MatrixXd C;  // NOLINT(google3-readability-class-member-naming)
  eigenmath::MatrixXd P;  // NOLINT(google3-readability-class-member-naming)
  eigenmath::MatrixXd Q;  // NOLINT(google3-readability-class-member-naming)
  eigenmath::MatrixXd R;  // NOLINT(google3-readability-class-member-naming)
  eigenmath::MatrixXd V;  // NOLINT(google3-readability-class-member-naming)
};

}  // namespace intrinsic

#endif  // INTRINSIC_MATH_LINEAR_SYSTEMS_TESTING_LINEAR_TEST_PROBLEM_H_
