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

#include "intrinsic/math/inertia_utils.h"

#include "Eigen/Eigenvalues"
#include "absl/strings/string_view.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/icon/utils/realtime_status_macro.h"
#include "intrinsic/icon/utils/realtime_status_or.h"
#include "intrinsic/kinematics/types/to_fixed_string.h"
#include "intrinsic/math/almost_equals.h"

namespace intrinsic {

namespace {
constexpr double kEpsilon = 1e-6;

// Corrects a rotation matrix that is an improper rotation, i.e. a reflection in
// a plane perpendicular to the rotation axis.
//
// Some info on improper rotations and reflections can be found in `Fillmore,
// Jay P. "A note on rotation matrices." IEEE Computer Graphics and
// Applications 4.2 (1984): 30-33.`
// https://mathweb.ucsd.edu/~fillmore/papers/Fillmore_1984%20Rotation_Matrices.pdf
icon::RealtimeStatus FixRotationMatrixReflection(Eigen::Matrix3d& R) {
  const double det = R.determinant();
  if (AlmostEquals(det, 1.0, kEpsilon)) {
    return icon::OkStatus();
  }
  if (!AlmostEquals(det, -1.0, kEpsilon)) {
    return icon::InvalidArgumentError(icon::RealtimeStatus::StrCat(
        "Determinant is not close to -1, cannot fix rotation matrix: ", det));
  }
  // Negate one column to fix the reflection.
  R.col(0) *= -1.0;
  const double new_det = R.determinant();
  // Now do a sanity check that the matrix is close to orthogonal.
  if (!AlmostEquals(new_det, 1.0, kEpsilon)) {
    return icon::InvalidArgumentError(icon::RealtimeStatus::StrCat(
        "Determinant is not close to 1 after reflection fix: ", new_det));
  }
  const Eigen::Matrix3d RtR = R.transpose() * R;
  if (!RtR.isApprox(Eigen::Matrix3d::Identity(), kEpsilon)) {
    return icon::InvalidArgumentError(
        "Matrix is not close to orthogonal after reflection fix");
  }
  return icon::OkStatus();
}

}  // namespace

bool IsSymmetric(const eigenmath::Matrix3d& matrix,
                 double max_difference_threshold) {
  return matrix.isApprox(matrix.transpose(), max_difference_threshold);
}

icon::RealtimeStatusOr<eigenmath::Matrix3d> CreateInertiaTensor(
    double i_xx, double i_yy, double i_zz, double i_xy, double i_xz,
    double i_yz) {
  eigenmath::Matrix3d result{
      {i_xx, i_xy, i_xz}, {i_xy, i_yy, i_yz}, {i_xz, i_yz, i_zz}};
  INTRINSIC_RT_RETURN_IF_ERROR(ValidateInertia(result));
  return result;
}

icon::RealtimeStatus ValidateInertia(const eigenmath::Matrix3d& inertia) {
  // The link inertia tensor should be density realizable. In other words,
  // the inertia tensor expressed at the center of gravity should be positive
  // definite (symmetric and with positive eigenvalues) and its eigenvalues
  // fulfill the triangle inequalities.
  if (!IsSymmetric(inertia)) {
    return icon::InvalidArgumentError(icon::RealtimeStatus::StrCat(
        "Inertia tensor is not symmetric. Got ", "[[ ",
        eigenmath::ToFixedString(inertia.row(0)), "],[ ",
        eigenmath::ToFixedString(inertia.row(1)), "],[ ",
        eigenmath::ToFixedString(inertia.row(2)), "]]."));
  }

  Eigen::SelfAdjointEigenSolver<eigenmath::Matrix3d> eigen_solver(inertia);
  if (eigen_solver.info() != Eigen::Success) {
    return icon::UnknownError(
        "Eigen failed to compute eigenvalues for inertia tensor.");
  }
  const eigenmath::Vector3d& eigenvalues = eigen_solver.eigenvalues().real();
  if ((eigenvalues.array() <= 0.0).any()) {
    return icon::InvalidArgumentError(icon::RealtimeStatus::StrCat(
        "Inertia tensor is not positive definite. Not all eigenvalues "
        "> 0.0: ",
        eigenmath::ToFixedString(eigenvalues)));
  }
  const auto sum = eigenvalues.sum();
  for (int i = 0; i < 3; ++i) {
    if (sum < 2.0 * eigenvalues[i]) {
      return icon::InvalidArgumentError(
          icon::RealtimeStatus::StrCat("The inertia eigenvalues do not satisfy "
                                       "the triangle inequality: ",
                                       sum, " < ", 2.0 * eigenvalues[i], "."));
    }
  }
  return icon::OkStatus();
}

eigenmath::Matrix3d RotateInertiaTensor(
    const eigenmath::Matrix3d& inertia,
    const eigenmath::Quaterniond& rotation) {
  return rotation * inertia * rotation.inverse();
}

icon::RealtimeStatusOr<PrincipalInertiaMoments>
TransformToPrincipalInertiaMoments(const eigenmath::Matrix3d& inertia) {
  INTRINSIC_RT_RETURN_IF_ERROR(ValidateInertia(inertia));
  Eigen::SelfAdjointEigenSolver<eigenmath::Matrix3d> eigensolver(inertia);
  if (eigensolver.info() != Eigen::Success) {
    return icon::UnknownError(
        "Eigen failed to compute eigenvalues for principal inertia moments.");
  }
  if (eigensolver.eigenvectors().rows() != 3 ||
      eigensolver.eigenvectors().cols() != 3) {
    return icon::UnknownError(
        "Eigen failed to compute eigenvectors for principal inertia moments. "
        "Invalid number/size of eigenvectors.");
  }
  PrincipalInertiaMoments result;
  eigenmath::Matrix3d rotation_matrix = eigensolver.eigenvectors();
  result.moments = eigensolver.eigenvalues().real();

  INTRINSIC_RT_RETURN_IF_ERROR(FixRotationMatrixReflection(rotation_matrix));
  INTRINSIC_RT_RETURN_IF_ERROR(ValidateInertia(result.moments.asDiagonal()));
  result.rotation = eigenmath::Quaterniond(rotation_matrix);
  return result;
}

}  // namespace intrinsic
