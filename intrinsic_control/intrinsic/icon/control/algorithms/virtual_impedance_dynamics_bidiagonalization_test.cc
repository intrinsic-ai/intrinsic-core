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

#include "intrinsic/icon/control/algorithms/virtual_impedance_dynamics_bidiagonalization.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "Eigen/Eigenvalues"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "benchmark/benchmark.h"
#include "internal/testing.h"
#include "intrinsic/eigenmath/matchers.h"
#include "intrinsic/eigenmath/pseudo_inverse.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/control/algorithms/testing/cartesian_impedance_controller_test_utils.h"
#include "intrinsic/icon/testing/malloc_test.h"
#include "intrinsic/icon/utils/log.h"
#include "intrinsic/icon/utils/realtime_status_matchers.h"

namespace intrinsic::icon {
namespace {

constexpr double kSmallTolerance = 1e-6;

using ::absl_testing::StatusIs;
using ::intrinsic::eigenmath::testing::IsApprox;
using ::testing::HasSubstr;

TEST(VirtualImpedanceDynamicsBiDiagonalizationTest,
     FailsForNonSymmetricMatrices) {
  VirtualImpedanceDynamicsBiDiagonalization diag(6);

  eigenmath::Matrix6d M_symmetric = GenerateSPDMatrix(6);
  eigenmath::Matrix6d K_symmetric = GenerateSPDMatrix(6);

  {
    eigenmath::Matrix6d K_asymmetric = K_symmetric;
    K_asymmetric(0, 1) = 0.01;
    EXPECT_THAT(diag.Diagonalize(M_symmetric, K_asymmetric),
                StatusIs(absl::StatusCode::kFailedPrecondition,
                         HasSubstr("Stiffness matrix is not symmetric")));
  }
  {
    eigenmath::Matrix6d M_asymmetric = M_symmetric;
    M_asymmetric(0, 1) = 0.01;
    EXPECT_THAT(diag.Diagonalize(M_asymmetric, K_symmetric),
                StatusIs(absl::StatusCode::kFailedPrecondition,
                         HasSubstr("Inertia matrix is not symmetric")));
  }
}

TEST(VirtualImpedanceDynamicsBiDiagonalizationTest,
     FailsForMatricesWithWrongDimensions) {
  VirtualImpedanceDynamicsBiDiagonalization diag(6);

  eigenmath::Matrix6d M_symmetric = GenerateSPDMatrix(6);
  eigenmath::Matrix6d K_symmetric = GenerateSPDMatrix(6);

  {
    EXPECT_THAT(diag.Diagonalize(M_symmetric, eigenmath::MatrixNMd::Ones(6, 7)),
                StatusIs(absl::StatusCode::kFailedPrecondition,
                         HasSubstr("Stiffness matrix has wrong number of "
                                   "columns. Expected 6 but got 7")));
  }
  {
    EXPECT_THAT(diag.Diagonalize(M_symmetric, eigenmath::MatrixNMd::Ones(7, 6)),
                StatusIs(absl::StatusCode::kFailedPrecondition,
                         HasSubstr("Stiffness matrix has wrong number of rows. "
                                   "Expected 6 but got 7")));
  }
  {
    EXPECT_THAT(diag.Diagonalize(eigenmath::MatrixNMd::Ones(6, 7), K_symmetric),
                StatusIs(absl::StatusCode::kFailedPrecondition,
                         HasSubstr("Inertia matrix has wrong number of "
                                   "columns. Expected 6 but got 7")));
  }
  {
    EXPECT_THAT(diag.Diagonalize(eigenmath::MatrixNMd::Ones(7, 6), K_symmetric),
                StatusIs(absl::StatusCode::kFailedPrecondition,
                         HasSubstr("Inertia matrix has wrong number of rows. "
                                   "Expected 6 but got 7")));
  }
}

TEST(VirtualImpedanceDynamicsBiDiagonalizationTest, WorksForMatrix6d) {
  eigenmath::Matrix6d M_test = GenerateSPDMatrix(6);
  eigenmath::Matrix6d K_test = GenerateSPDMatrix(6);

  VirtualImpedanceDynamicsBiDiagonalization diag(6);

  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  INTRINSIC_RT_ASSERT_OK(diag.Diagonalize(M_test, K_test));
  eigenmath::Matrix6d V = diag.EigenVectorBasisV();
  eigenmath::Matrix6d Vinv = diag.EigenVectorBasisVInverse();
  eigenmath::Matrix6d M_diag = diag.InertiaMatrixDiagonalized();
  eigenmath::Matrix6d K_diag = diag.StiffnessMatrixDiagonalized();
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();

  EXPECT_THAT(M_diag,
              IsApprox(eigenmath::Matrix6d::Identity(), kSmallTolerance));
  EXPECT_THAT(V.transpose() * M_test * V, IsApprox(M_diag, kSmallTolerance));
  EXPECT_THAT(V.transpose() * K_test * V, IsApprox(K_diag, kSmallTolerance));

  // Check that K_diag is really diagonal. First subtract the diagonal part from
  // the full matrix.
  eigenmath::Matrix6d K_without_diag =
      K_diag - eigenmath::Matrix6d(K_diag.diagonal().asDiagonal());
  // Check the residual is small.
  EXPECT_LT(K_without_diag.cwiseAbs().maxCoeff(), kSmallTolerance);

  // Check that inverse transformations hold.
  EXPECT_THAT(Vinv.transpose() * M_diag * Vinv,
              IsApprox(M_test, kSmallTolerance));
  EXPECT_THAT(Vinv.transpose() * K_diag * Vinv,
              IsApprox(K_test, kSmallTolerance));
}

TEST(VirtualImpedanceDynamicsBiDiagonalizationTest, WorksForMatrix2d) {
  eigenmath::MatrixNd M_test = GenerateSPDMatrix(2);
  eigenmath::MatrixNd K_test = GenerateSPDMatrix(2);

  VirtualImpedanceDynamicsBiDiagonalization diag(2);

  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  INTRINSIC_RT_ASSERT_OK(diag.Diagonalize(M_test, K_test));
  eigenmath::MatrixNd V = diag.EigenVectorBasisV();
  eigenmath::MatrixNd Vinv = diag.EigenVectorBasisVInverse();
  eigenmath::MatrixNd M_diag = diag.InertiaMatrixDiagonalized();
  eigenmath::MatrixNd K_diag = diag.StiffnessMatrixDiagonalized();
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
  ASSERT_EQ(V.rows(), 2);
  ASSERT_EQ(V.cols(), 2);
  ASSERT_EQ(M_diag.rows(), 2);
  ASSERT_EQ(M_diag.cols(), 2);
  ASSERT_EQ(K_diag.rows(), 2);
  ASSERT_EQ(K_diag.cols(), 2);

  EXPECT_THAT(M_diag,
              IsApprox(eigenmath::Matrix2d::Identity(), kSmallTolerance));
  EXPECT_THAT(V.transpose() * M_test * V, IsApprox(M_diag, kSmallTolerance));
  EXPECT_THAT(V.transpose() * K_test * V, IsApprox(K_diag, kSmallTolerance));

  // Check that K_diag is really diagonal. First subtract the diagonal part from
  // the full matrix.
  eigenmath::MatrixNd K_without_diag =
      K_diag - eigenmath::MatrixNd(K_diag.diagonal().asDiagonal());
  // Check the residual is small.
  EXPECT_LT(K_without_diag.cwiseAbs().maxCoeff(), kSmallTolerance);

  // Check that inverse transformations hold.
  EXPECT_THAT(Vinv.transpose() * M_diag * Vinv,
              IsApprox(M_test, kSmallTolerance));
  EXPECT_THAT(Vinv.transpose() * K_diag * Vinv,
              IsApprox(K_test, kSmallTolerance));
}

// Benchmarks for evaluating runtime performance of the Bidiagonalization step.
//
// To build the benchmarks
// bazel --bazelrc=/dev/null build -c opt --dynamic_mode=off --copt=-gmlt
// intrinsic/icon/control/algorithms:virtual_impedance_dynamics_bidiagonalization_test
//
// To run the benchmarks
// bazel-bin/intrinsic/icon/control/algorithms/virtual_impedance_dynamics_bidiagonalization_test
// --benchmark_filter=all
//
template <typename Matrix>
class BiDiagonalizationBenchmarkHelper {
 public:
  explicit BiDiagonalizationBenchmarkHelper(int dim)
      : ges_(),
        k_mat_test_(GenerateSPDMatrix(dim)),
        m_mat_test_(GenerateSPDMatrix(dim)) {}

  void RunDiagonalization() {
    ges_.compute(k_mat_test_, m_mat_test_, Eigen::ComputeEigenvectors);
    v_mat_ = ges_.eigenvectors();
    k_mat_diagonalized_ = v_mat_.transpose() * k_mat_test_ * v_mat_;
  }

  void SetRandomMatrices(int dim) {
    k_mat_test_ = GenerateSPDMatrix(dim);
    m_mat_test_ = GenerateSPDMatrix(dim);
  }

  // For comparison, we can also compute a pseudo inverse and get its timing.
  void ComputePseudoInverseComparison() {
    INTRINSIC_RT_ASSERT_OK(ComputePseudoInverse(k_mat_test_, 1e-6, false));
  }

 protected:
  Eigen::GeneralizedSelfAdjointEigenSolver<Matrix> ges_;
  Matrix k_mat_test_;
  Matrix m_mat_test_;
  Matrix v_mat_;
  Matrix k_mat_diagonalized_;
};

void BM_GeneralizedEigenSolver3D(benchmark::State& state) {
  RtLogInitForThisThread();
  BiDiagonalizationBenchmarkHelper<eigenmath::Matrix3d> benchmark_helper(3);
  for (auto s : state) {
    benchmark_helper.SetRandomMatrices(3);
    benchmark_helper.RunDiagonalization();
  }
}
BENCHMARK(BM_GeneralizedEigenSolver3D);

void BM_GeneralizedEigenSolver6D(benchmark::State& state) {
  RtLogInitForThisThread();
  BiDiagonalizationBenchmarkHelper<eigenmath::Matrix6d> benchmark_helper(6);
  for (auto s : state) {
    benchmark_helper.SetRandomMatrices(6);
    benchmark_helper.RunDiagonalization();
  }
}
BENCHMARK(BM_GeneralizedEigenSolver6D);

void BM_CompareComputePseudoInverse(benchmark::State& state) {
  RtLogInitForThisThread();
  BiDiagonalizationBenchmarkHelper<eigenmath::Matrix6d> benchmark_helper(6);
  for (auto s : state) {
    benchmark_helper.SetRandomMatrices(6);
    benchmark_helper.ComputePseudoInverseComparison();
  }
}
BENCHMARK(BM_CompareComputePseudoInverse);

void BM_VirtualImpedanceDynamicsBiDiagonalizationFullMatrix(
    benchmark::State& state) {
  RtLogInitForThisThread();
  VirtualImpedanceDynamicsBiDiagonalization virtual_impedance_dynamics(6);
  for (auto s : state) {
    INTRINSIC_RT_ASSERT_OK(virtual_impedance_dynamics.Diagonalize(
        GenerateSPDMatrix(6), GenerateSPDMatrix(6)));
  }
}
BENCHMARK(BM_VirtualImpedanceDynamicsBiDiagonalizationFullMatrix);

void BM_VirtualImpedanceDynamicsBiDiagonalizationBlockDiagonalMatrix(
    benchmark::State& state) {
  RtLogInitForThisThread();
  VirtualImpedanceDynamicsBiDiagonalization virtual_impedance_dynamics(6);
  for (auto s : state) {
    eigenmath::Matrix6d M = eigenmath::Matrix6d::Zero();
    eigenmath::Matrix6d K = eigenmath::Matrix6d::Zero();
    K.block<3, 3>(0, 0) = GenerateSPDMatrix(3);
    M.block<3, 3>(0, 0) = GenerateSPDMatrix(3);
    K.block<3, 3>(3, 3) = GenerateSPDMatrix(3);
    M.block<3, 3>(3, 3) = GenerateSPDMatrix(3);
    INTRINSIC_RT_ASSERT_OK(virtual_impedance_dynamics.Diagonalize(M, K));
  }
}
BENCHMARK(BM_VirtualImpedanceDynamicsBiDiagonalizationBlockDiagonalMatrix);

}  // namespace
}  // namespace intrinsic::icon
