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

#ifndef INTRINSIC_ICON_DYNAMICS_MODEL_IDENTIFICATION_MODEL_IDENTIFICATION_UTILS_H_
#define INTRINSIC_ICON_DYNAMICS_MODEL_IDENTIFICATION_MODEL_IDENTIFICATION_UTILS_H_

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/kinematics/types/joint_state.h"

namespace intrinsic::icon {

// Number of parameters to be identified for each rigid body.
constexpr int kNumInertialParametersPerBody = 10;

// An ExecutionRecord is a container for a recorded sample with all available
// data, including measured as well as commanded states.
struct ExecutionRecord {
  JointStatePVAT measured_state;
  JointStatePVA commanded_state;
  absl::Status SetSize(int ndofs);
};

// Structure holding the regression matrix and vector used to describe an
// equality of the `reg_vector` to the product of the `reg_matrix` times a
// vector of inertial parameters.
struct RegressionPair {
  eigenmath::VectorXd reg_vector;
  eigenmath::MatrixXd reg_matrix;
  absl::Status SetSize(int rows, int cols);
};

// Construct a a cumulative matrix and vector (sufficient statistics) that
// summarizes the information from all `composite_regression_pairs`.
absl::StatusOr<RegressionPair> ConstructCumulativeRegressor(
    absl::Span<const RegressionPair> composite_regression_pairs);

}  // namespace intrinsic::icon

#endif  // INTRINSIC_ICON_DYNAMICS_MODEL_IDENTIFICATION_MODEL_IDENTIFICATION_UTILS_H_
