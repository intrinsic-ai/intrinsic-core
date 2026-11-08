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

#include <gtest/gtest.h>

#include <memory>

#include "Eigen/Eigen"
#include "Eigen/Eigenvalues"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/testing/malloc_test.h"

namespace intrinsic {
namespace control {
using MatrixXd = Eigen::MatrixXd;

TEST(EigenRTAlloc, blas_static) {
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();

  eigenmath::Matrix3d m3 = eigenmath::Matrix3d::Random();
  eigenmath::Vector3d v3 = eigenmath::Vector3d::Random();

  eigenmath::Vector3d res __attribute__((unused)) = m3 * v3 + v3 + 2 * v3;
  auto auto_res __attribute__((unused)) = m3 * v3 + v3 + 2 * v3;  // NOLINT

  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
}

TEST(EigenRTAlloc, linalg_static) {
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();

  eigenmath::Matrix3d m3 __attribute__((unused)) =
      eigenmath::Matrix3d::Random();
  eigenmath::Vector3d v3 __attribute__((unused)) =
      eigenmath::Vector3d::Random();

  eigenmath::Matrix3d inv __attribute__((unused)) = m3.inverse();
  double d __attribute__((unused)) = m3.determinant();

  eigenmath::Vector3d res __attribute__((unused)) = m3.inverse() * v3;

  eigenmath::Vector3d res2 __attribute__((unused)) = m3.ldlt().solve(v3);
  eigenmath::Vector3d res3 __attribute__((unused)) =
      m3.partialPivLu().solve(v3);

  Eigen::SelfAdjointEigenSolver<eigenmath::Matrix3d> eigensolver(m3);
  auto ev __attribute__((unused)) = eigensolver.eigenvectors();  // NOLINT

  eigenmath::Vector3d f;
  f << 1, 2, 3;

  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
}

TEST(EigenRTAlloc, blas_MatrixNd) {
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();

  eigenmath::MatrixNd m = eigenmath::MatrixNd::Random(5, 5);
  eigenmath::VectorNd v = eigenmath::VectorNd::Random(5);

  eigenmath::VectorNd res __attribute__((unused)) = m * v + v + 2 * v;
  auto auto_res __attribute__((unused)) = m * v + v + 2 * v;  // NOLINT

  auto col __attribute__((unused)) = m.col(3);  // NOLINT
  eigenmath::VectorNd f(3);
  f << 1, 2, 3;
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
}

TEST(EigenRTAlloc, linalg_MatrixNd) {
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();

  int sz = eigenmath::MAX_EIGEN_MATRIX_SIZE - 1;

  eigenmath::MatrixNd m = eigenmath::MatrixNd::Random(sz, sz);
  eigenmath::VectorNd v = eigenmath::VectorNd::Random(sz);

  eigenmath::MatrixNd inv __attribute__((unused)) = m.inverse();
  double d __attribute__((unused)) = m.determinant();

  eigenmath::VectorNd res __attribute__((unused)) = m.inverse() * v;

  eigenmath::VectorNd res2 __attribute__((unused)) = m.ldlt().solve(v);
  eigenmath::VectorNd res3 __attribute__((unused)) = m.partialPivLu().solve(v);

  Eigen::SelfAdjointEigenSolver<eigenmath::MatrixNd> eigensolver(m);
  auto ev = eigensolver.eigenvectors();  // NOLINT

  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
}

TEST(EigenRTAlloc, DISABLED_blas_dynamic) {
  MatrixXd m3 = MatrixXd::Random(3, 3);
  eigenmath::VectorXd v3 = eigenmath::VectorXd::Random(3);
  eigenmath::VectorXd res;

  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  // this fails, as it allocates temporaries.
  res = m3 * v3 + v3 + 2 * v3;

  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
}

}  // namespace control
}  // namespace intrinsic
