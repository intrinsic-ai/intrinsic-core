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

#ifndef INTRINSIC_EIGENMATH_TEST_UTILS_H_
#define INTRINSIC_EIGENMATH_TEST_UTILS_H_

#include "Eigen/Core"
#include "Eigen/Geometry"
#include "intrinsic/eigenmath/manifolds.h"
#include "intrinsic/eigenmath/so3.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/math/pose3.h"

namespace intrinsic {
namespace eigenmath {

// Deprecated. Eigen style sampling of rotation.
static inline SO3d getRandomSO3d() {
  // Uses the uniform sampling method mentioned by [LaValle, Planning
  // Algorithms, Cambridge University Press, 2006] in section 5.2.2,
  // which is taken from [Shoemake, Uniform random rotations, in Kirk
  // (ed), Graphics Gems III, pg 124–132, Academic, New York, 1992]
  Vector3d samples = Vector3d::Random();       // range [-1,1]
  samples[0] = fabs(samples[0]);               // range [0,1]
  samples[1] = 2.0 * M_PI * fabs(samples[1]);  // range [0,2pi]
  samples[2] = 2.0 * M_PI * fabs(samples[2]);  // range [0,2pi]
  // LaValle uses the (w,x,y,z) convention, same as Eigen
  SO3d orientation(Quaterniond(sqrt(1 - samples[0]) * sin(samples[1]),
                               sqrt(1 - samples[0]) * cos(samples[1]),
                               sqrt(samples[0]) * sin(samples[2]),
                               sqrt(samples[0]) * cos(samples[2])));
  return orientation;
}

// Deprecated.  Eigen style sampling of rotation.
static inline Quaterniond getRandomQuaterniond() {
  return getRandomSO3d().quaternion();
}

// Deprecated.  Eigen style sampling of pose.
static inline Pose3d getRandomPose3d() {
  return Pose3d(getRandomSO3d(), Vector3d::Random());  // NOLINT
}

// Compares 2 vectors by subtracting and calculating the norm.
//
// Note: this avoids Eigen::Matrix::isApprox() which returns false if
// the sign of any component is different or if the value is very close to 0.
//
template <typename T, typename DerivedValue, typename DerivedExpected>
static inline bool isNormApprox(
    const Eigen::MatrixBase<DerivedValue>& value,
    const Eigen::MatrixBase<DerivedExpected>& expected, T epsilon) {
  const T delta = (value - expected).norm();
  return delta <= epsilon;
}

// Compare 2 SO3 by comparing logSO3 of first times inverse of second.
//
// Note: this avoids Eigen::Matrix::isApprox() which return false if
// the sign of any component is different or if the value is very close to 0.
//
template <typename T, int OptionsValue, int OptionsExpected>
static inline bool isNormApprox(const SO3<T, OptionsValue>& value,
                                const SO3<T, OptionsExpected>& expected,
                                T epsilon) {
  return isNormApprox(intrinsic::eigenmath::logSO3(value * expected.inverse()),
                      Vector3<T>(0, 0, 0), epsilon);
}

// Compare 2 SO3 by comparing logSO3 of first times inverse of second.
//
// Note: this avoids Eigen::Matrix::isApprox() which return false if
// the sign of any component is different or if the value is very close to 0.
template <typename T, int OptionsValue, int OptionsExpected>
static inline bool isNormApprox(const Pose3<T, OptionsValue>& value,
                                const Pose3<T, OptionsExpected>& expected,
                                T epsilon) {
  return isNormApprox(value.so3(), expected.so3(), epsilon) &&
         isNormApprox(value.translation(), expected.translation(), epsilon);
}

}  // namespace eigenmath
}  // namespace intrinsic

#endif  // INTRINSIC_EIGENMATH_TEST_UTILS_H_
