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

#ifndef INTRINSIC_ICON_CONTROL_ALGORITHMS_VIRTUAL_IMPEDANCE_DYNAMICS_BIDIAGONALIZATION_H_
#define INTRINSIC_ICON_CONTROL_ALGORITHMS_VIRTUAL_IMPEDANCE_DYNAMICS_BIDIAGONALIZATION_H_

#include "Eigen/Eigenvalues"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/utils/realtime_status.h"

namespace intrinsic::icon {

// This class implements a bi-diagonalization of the virtual impedance dynamics
// such that the inertia and stiffness matrix are brought into diagonal form.
// Consider the six-dimensional virtual Cartesian Impedance Dynamics of form
//
// M*(ddx - a_ref) + D*(dx-v_ref) + K*(x-x_ref) = f - f_ref       (1)
//
// where M is the inertia matrix, D is the damping matrix, K is the stiffness
// matrix, x is the position, dx is the velocity, ddx is the acceleration, and
// f is the external force. x_ref is the reference position, v_ref is the
// reference velocity, a_ref is the reference acceleration, f_ref is the
// reference force.
//
// Given a symmetric and positive definite matrix M and a symmetric positive
// semi-definite matrix K, by solving the generalized Eigenvalue problem
// `(lambda*M - K)*v = 0` one can find a non singular matrix V such that
// `V^T*M*V = I (identity)` and `V^T*K*V = K_diag`, where `K_diag` is a strictly
// diagonal matrix which contains the generalized eigenvalues.
//
// We can diagonalize the inertia and stiffness part of Equation (1) by defining
// `x:= V*z`, where z becomes the new position vector. This leads to
//
// M*(V*ddz - a_ref) + D*(V*dz - v_ref) + K*(V*z - x_ref) = f - f_ref
//
// By pre-multiplying with V^T from the left, we obtain
//
// V^T*M*V*ddz + V^T*D*V*dz + V^T*K*V*z
//    = V^T*(f - f_ref + M*a_ref + D*v_ref + K*x_ref)
//
// Since V^T*M*V = I and V^T*K*V = K_diag, this simplifies to
//
// ddz + V^T*D*V*dz + K_diag*z = V^T*(f - f_ref + M*a_ref + D*v_ref + K*x_ref)
//
// where the right-hand side can be interpreted as a generic "driving force". On
// the left, all but the damping matrix is strictly diagonal.
//
// Reference for the original idea: A. Albu-Schaffer, C. Ott, U. Frese, and G.
// Hirzinger, “Cartesian impedance control of redundant robots: recent results
// with the DLR light-weight-arms,” in Proceedings of the IEEE International
// Conference on Robotics and Automation, vol. 3, Sep. 2003, pp. 3704–3709.
class VirtualImpedanceDynamicsBiDiagonalization {
 public:
  explicit VirtualImpedanceDynamicsBiDiagonalization(int ndofs)
      : ndofs_(ndofs), generalized_eigen_solver_(ndofs) {}

  icon::RealtimeStatus Diagonalize(const eigenmath::MatrixNd& inertia_matrix,
                                   const eigenmath::MatrixNd& stiffness_matrix);

  // Returns the generalized eigenvectors in matrix form, defining the transform
  // `x:= V*z`.
  const eigenmath::MatrixNd& EigenVectorBasisV() const;

  // Returns the generalized eigenvectors in matrix form, required to compute
  // `z = V.inverse() * x`.
  const eigenmath::MatrixNd& EigenVectorBasisVInverse() const;

  // The diagonalized inertia matrix is the Identity matrix by definition.
  eigenmath::MatrixNd InertiaMatrixDiagonalized() const;

  const eigenmath::MatrixNd& StiffnessMatrixDiagonalized() const;

 protected:
  int ndofs_;
  // Since the inertia matrix is guaranteed to be symmetric positive definite,
  // and the stiffness matrix is guaranteed to be symmetric positive
  // semi-definite, we can use a `GeneralizedSelfAdjointEigenSolver`.
  Eigen::GeneralizedSelfAdjointEigenSolver<eigenmath::MatrixNd>
      generalized_eigen_solver_;
  eigenmath::MatrixNd matrix_v_;
  eigenmath::MatrixNd matrix_v_inverse_;
  eigenmath::MatrixNd stiffness_matrix_diagonalized_;
};

}  // namespace intrinsic::icon

#endif  // INTRINSIC_ICON_CONTROL_ALGORITHMS_VIRTUAL_IMPEDANCE_DYNAMICS_BIDIAGONALIZATION_H_
