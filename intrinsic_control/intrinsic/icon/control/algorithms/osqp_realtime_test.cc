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


#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstddef>
#include <limits>

#include "Eigen/SparseCore"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "internal/testing.h"
#include "intrinsic/eigenmath/matchers.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/testing/malloc_test.h"
#include "osqp++.h"

namespace intrinsic {
namespace {

using ::absl_testing::StatusIs;
using ::intrinsic::eigenmath::testing::IsApprox;
using ::testing::DoubleNear;

constexpr double kInfinity = std::numeric_limits<double>::infinity();
constexpr size_t kStateDim = 2;
constexpr size_t kConstraintDim = 1;

// This test solves a 2-dimensional quadratic optimization problem with
// 1-dimensional state constraint and checks for memory allocation. The problem
// reads as
//
// min_(x,y) x^2 + 0.5 * x * y + y^2 + x, s.t. x >= 1
//
// For more information about OSQP and the OSQP-CPP interface, see
// https://github.com/google/osqp-cpp
// While OSQP seems to be fully realtime-compatible in principle, Google's
// OSQP-CPP interfaces adds absl::Status return values, which renders the code
// unsuitable for hard realtime applications. Do not use OSQP-CPP in real-time
// code.
osqp::OsqpInstance CreateOsqpInstance() {
  osqp::OsqpInstance instance;

  // The objective Hessian must be upper triangular, otherwise memory will
  // be allocated while setting the matrix. See implementation of
  // OsqpSolver::UpdateObjectiveMatrix().
  Eigen::SparseMatrix<double> objective_hessian(kStateDim, kStateDim);
  objective_hessian.insert(0, 0) = 2.0;
  objective_hessian.insert(0, 1) = 0.5;
  objective_hessian.insert(1, 1) = 2.0;

  eigenmath::VectorNd objective_jacobian(kStateDim);
  objective_jacobian << 1.0, 0.0;

  Eigen::SparseMatrix<double> constraint_jacobian(kConstraintDim, kStateDim);
  constraint_jacobian.insert(0, 0) = 1.0;

  eigenmath::VectorNd constraint_lower_bound =
      eigenmath::VectorNd::Constant(kConstraintDim, 1.0);
  eigenmath::VectorNd constraint_upper_bound =
      eigenmath::VectorNd::Constant(kConstraintDim, kInfinity);

  instance.objective_matrix = objective_hessian;
  instance.constraint_matrix = constraint_jacobian;
  instance.objective_vector = objective_jacobian;
  instance.lower_bounds = constraint_lower_bound;
  instance.upper_bounds = constraint_upper_bound;

  return instance;
}

TEST(OsqpRealtimeTest, SolvePerformsNoMalloc) {
  osqp::OsqpInstance instance = CreateOsqpInstance();
  osqp::OsqpSolver solver;

  osqp::OsqpSettings settings;
  settings.eps_abs = 1e-5;
  settings.eps_rel = 1e-5;
  // Note that OsqpSolver::Init() performs memory allocation.
  absl::Status init_status = solver.Init(instance, settings);
  EXPECT_THAT(init_status, StatusIs(absl::StatusCode::kOk));

  EXPECT_TRUE(solver.IsInitialized());

  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  osqp::OsqpExitCode exit_code = solver.Solve();
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
  EXPECT_EQ(exit_code, osqp::OsqpExitCode::kOptimal);

  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  double optimal_objective = solver.objective_value();
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();

  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  intrinsic::eigenmath::VectorNd optimal_solution = solver.primal_solution();
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();

  const double known_optimal_cost = 1.9375;
  EXPECT_THAT(optimal_objective, DoubleNear(known_optimal_cost, 1e-3));

  eigenmath::Vector2d known_solution_vector;
  known_solution_vector << 1, -0.25;
  EXPECT_THAT(optimal_solution, IsApprox(known_solution_vector, 1e-3));
}

TEST(OsqpRealtimeTest, UpdatingProblemMatricesPerformsNoMalloc) {
  osqp::OsqpInstance instance = CreateOsqpInstance();
  osqp::OsqpSolver solver;

  osqp::OsqpSettings settings;
  settings.eps_abs = 1e-5;
  settings.eps_rel = 1e-5;
  absl::Status init_status = solver.Init(instance, settings);
  EXPECT_THAT(init_status, StatusIs(absl::StatusCode::kOk));

  // TODO(giftthaler): Osqp-Cpp employs absl::Status, which is not
  // realtime-safe. The following tests pass because no Status different from
  // Ok() gets returned. In the current form, the Osqp-cpp interface should not
  // be used in the RT control loop.
  absl::Status update_status;

  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  update_status = solver.UpdateConstraintMatrix(instance.constraint_matrix);
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
  EXPECT_OK(update_status);

  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  update_status = solver.UpdateObjectiveMatrix(instance.objective_matrix);
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
  EXPECT_OK(update_status);

  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  update_status = solver.SetObjectiveVector(
      instance.objective_vector);  // TODO(giftthaler): there is a naming
                                   // inconsistency in the interface. We might
                                   // want to reach out to the author and ask?
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
  EXPECT_OK(update_status);

  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  update_status =
      solver.SetBounds(instance.lower_bounds, instance.upper_bounds);
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
  EXPECT_OK(update_status);
}

}  // namespace
}  // namespace intrinsic
