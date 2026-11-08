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

#include "intrinsic/icon/control/algorithms/compute_critical_damping.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cmath>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "internal/testing.h"
#include "intrinsic/eigenmath/matchers.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/utils/realtime_status_matchers.h"

using intrinsic::eigenmath::testing::IsApprox;

namespace intrinsic::icon {
namespace {

TEST(ComputeCriticalDamping, ScalarComputationWorks) {
  const double m = 1.0;
  const double k = 1.0;
  EXPECT_OK(ComputeCriticalDamping(m, k).status());
  EXPECT_EQ(2.0, ComputeCriticalDamping(m, k).value());
}

TEST(ComputeCriticalDamping, ScalarComputationFailsForZeroMass) {
  const double m = 0.0;
  const double k = 1.0;
  EXPECT_THAT(ComputeCriticalDamping(m, k),
              RealtimeStatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST(ComputeCriticalDamping, ScalarComputationFailsForNegativeMass) {
  const double m = -1.0;
  const double k = 1.0;
  EXPECT_THAT(ComputeCriticalDamping(m, k),
              RealtimeStatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST(ComputeCriticalDamping, ScalarComputationFailsForZeroStiffness) {
  const double m = 1.0;
  const double k = 0.0;
  EXPECT_THAT(ComputeCriticalDamping(m, k),
              RealtimeStatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST(ComputeCriticalDamping, ScalarComputationFailsForNegativeStiffness) {
  const double m = 1.0;
  const double k = -1.0;
  EXPECT_THAT(ComputeCriticalDamping(m, k),
              RealtimeStatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST(ComputeCriticalDamping, DiagonalMatrixComputationWorks) {
  eigenmath::Matrix6d K = 2.0 * eigenmath::Matrix6d::Identity();
  eigenmath::Matrix6d M = 4.0 * eigenmath::Matrix6d::Identity();

  eigenmath::Matrix6d D_ref = 4.0 * sqrt(2.0) * eigenmath::Matrix6d::Identity();

  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(eigenmath::Matrix6d D,
                                    ComputeCriticalDamping(M, K));
  EXPECT_THAT(D, IsApprox(D_ref));

  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
      D, ComputeCriticalDampingFromInverseInertiaMatrix(M.inverse(), K));
  EXPECT_THAT(D, IsApprox(D_ref));
}

TEST(ComputeCriticalDamping, MatrixComputationWorks) {
  // A non-trivial, tri-diagonal symmetric positive definite stiffness matrix.
  eigenmath::Matrix6d K = eigenmath::Matrix6d::Zero();
  K.diagonal().setConstant(5.0);
  K.topRightCorner(5, 5).diagonal().setConstant(2.0);
  K.bottomLeftCorner(5, 5).diagonal().setConstant(2.0);

  // A non-trivial, tri-diagonal symmetric positive definite inertia matrix.
  eigenmath::Matrix6d M = eigenmath::Matrix6d::Zero();
  M.diagonal().setConstant(3);
  M.topRightCorner(5, 5).diagonal().setConstant(1.0);
  M.bottomLeftCorner(5, 5).diagonal().setConstant(1.0);

  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(eigenmath::Matrix6d D1,
                                    ComputeCriticalDamping(M, K));
  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
      eigenmath::Matrix6d D2,
      ComputeCriticalDampingFromInverseInertiaMatrix(M.inverse(), K));
  EXPECT_THAT(D1, IsApprox(D2));
}

TEST(ComputeCriticalDamping, MatrixComputationFailsForNegativeStiffness) {
  eigenmath::Matrix6d K = -eigenmath::Matrix6d::Identity();
  eigenmath::Matrix6d M = eigenmath::Matrix6d::Identity();

  EXPECT_THAT(ComputeCriticalDamping(M, K),
              RealtimeStatusIs(absl::StatusCode::kFailedPrecondition));
  EXPECT_THAT(ComputeCriticalDampingFromInverseInertiaMatrix(M.inverse(), K),
              RealtimeStatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST(ComputeCriticalDamping, MatrixComputationFailsForNegativeInertia) {
  eigenmath::Matrix6d K = eigenmath::Matrix6d::Identity();
  eigenmath::Matrix6d M = -eigenmath::Matrix6d::Identity();
  eigenmath::Matrix6d Minv = -eigenmath::Matrix6d::Identity();

  EXPECT_THAT(ComputeCriticalDamping(M, K),
              RealtimeStatusIs(absl::StatusCode::kFailedPrecondition));
  EXPECT_THAT(ComputeCriticalDampingFromInverseInertiaMatrix(Minv, K),
              RealtimeStatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST(ComputeCriticalDamping, MatrixComputationFailsForNonSymmetricStiffness) {
  eigenmath::Matrix6d M = eigenmath::Matrix6d::Identity();
  eigenmath::Matrix6d K = eigenmath::Matrix6d::Identity();
  K(0, 1) = 0.00001;

  EXPECT_THAT(ComputeCriticalDamping(M, K),
              RealtimeStatusIs(absl::StatusCode::kFailedPrecondition));
  EXPECT_THAT(ComputeCriticalDampingFromInverseInertiaMatrix(M.inverse(), K),
              RealtimeStatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST(ComputeCriticalDamping, MatrixComputationFailsForNonSymmetricInertia) {
  eigenmath::Matrix6d K = eigenmath::Matrix6d::Identity();
  eigenmath::Matrix6d M = eigenmath::Matrix6d::Identity();
  M(0, 1) = 0.00001;

  EXPECT_THAT(ComputeCriticalDamping(M, K),
              RealtimeStatusIs(absl::StatusCode::kFailedPrecondition));
  EXPECT_THAT(ComputeCriticalDampingFromInverseInertiaMatrix(M.inverse(), K),
              RealtimeStatusIs(absl::StatusCode::kFailedPrecondition));
}

}  // namespace
}  // namespace intrinsic::icon
