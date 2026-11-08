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

#include "Eigen/Eigenvalues"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/utils/fixed_str_cat.h"
#include "intrinsic/icon/utils/realtime_status.h"

namespace intrinsic::icon {
namespace {

const double kMaxDifferenceThreshold = 1e-6;

bool IsSymmetric(const eigenmath::MatrixNd& matrix,
                 double max_difference_threshold = kMaxDifferenceThreshold) {
  return matrix.isApprox(matrix.transpose(), max_difference_threshold);
}

}  // namespace

icon::RealtimeStatus VirtualImpedanceDynamicsBiDiagonalization::Diagonalize(
    const eigenmath::MatrixNd& inertia_matrix,
    const eigenmath::MatrixNd& stiffness_matrix) {
  if (inertia_matrix.rows() != ndofs_) {
    return icon::FailedPreconditionError(
        icon::FixedStrCat<RealtimeStatus::kMaxMessageLength>(
            "Inertia matrix has wrong number of rows. Expected ", ndofs_,
            " but got ", inertia_matrix.rows(), "."));
  }
  if (inertia_matrix.cols() != ndofs_) {
    return icon::FailedPreconditionError(
        icon::FixedStrCat<RealtimeStatus::kMaxMessageLength>(
            "Inertia matrix has wrong number of columns. Expected ", ndofs_,
            " but got ", inertia_matrix.cols(), "."));
  }
  if (stiffness_matrix.rows() != ndofs_) {
    return icon::FailedPreconditionError(
        icon::FixedStrCat<RealtimeStatus::kMaxMessageLength>(
            "Stiffness matrix has wrong number of rows. Expected ", ndofs_,
            " but got ", stiffness_matrix.rows(), "."));
  }
  if (stiffness_matrix.cols() != ndofs_) {
    return icon::FailedPreconditionError(
        icon::FixedStrCat<RealtimeStatus::kMaxMessageLength>(
            "Stiffness matrix has wrong number of columns. Expected ", ndofs_,
            " but got ", stiffness_matrix.cols(), "."));
  }
  if (!IsSymmetric(inertia_matrix)) {
    return icon::FailedPreconditionError("Inertia matrix is not symmetric.");
  }
  if (!IsSymmetric(stiffness_matrix)) {
    return icon::FailedPreconditionError("Stiffness matrix is not symmetric.");
  }
  generalized_eigen_solver_.compute(stiffness_matrix, inertia_matrix,
                                    Eigen::ComputeEigenvectors);
  matrix_v_ = generalized_eigen_solver_.eigenvectors();
  matrix_v_inverse_ = matrix_v_.inverse();
  stiffness_matrix_diagonalized_ =
      matrix_v_.transpose() * stiffness_matrix * matrix_v_;

  if (!stiffness_matrix_diagonalized_.isDiagonal(kMaxDifferenceThreshold)) {
    // We should never reach this point.
    return icon::InternalError(
        "Stiffness matrix is not diagonal after change of basis.");
  }

  if (!(matrix_v_.transpose() * inertia_matrix * matrix_v_)
           .isIdentity(kMaxDifferenceThreshold)) {
    // We should never reach this point.
    return icon::InternalError(
        "Inertia matrix is not Identity after change of basis.");
  }

  return icon::OkStatus();
}

const eigenmath::MatrixNd&
VirtualImpedanceDynamicsBiDiagonalization::EigenVectorBasisV() const {
  return matrix_v_;
}

const eigenmath::MatrixNd&
VirtualImpedanceDynamicsBiDiagonalization::EigenVectorBasisVInverse() const {
  return matrix_v_inverse_;
}

eigenmath::MatrixNd
VirtualImpedanceDynamicsBiDiagonalization::InertiaMatrixDiagonalized() const {
  return eigenmath::MatrixNd::Identity(ndofs_, ndofs_);
}

const eigenmath::MatrixNd&
VirtualImpedanceDynamicsBiDiagonalization::StiffnessMatrixDiagonalized() const {
  return stiffness_matrix_diagonalized_;
}

}  // namespace intrinsic::icon
