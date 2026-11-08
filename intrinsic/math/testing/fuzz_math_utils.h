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

#ifndef INTRINSIC_MATH_TESTING_FUZZ_MATH_UTILS_H_
#define INTRINSIC_MATH_TESTING_FUZZ_MATH_UTILS_H_

#include <memory>

#include "fuzztest/fuzztest.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/proto/pose.pb.h"

namespace intrinsic::testing {

// Generates a quaternion with values in the given domain.
fuzztest::Domain<eigenmath::Quaterniond> QuaterniondWith(
    fuzztest::Domain<double> double_domain);

// Generates a unit quaternion.
fuzztest::Domain<eigenmath::Quaterniond> UnitQuaterniond();

// Generates a quaternion with finite values. Contains floating point numbers
// which are neither infinity nor NaN.
fuzztest::Domain<eigenmath::Quaterniond> FiniteQuaterniond();

// Generates a quaternion which is not normalized.
fuzztest::Domain<eigenmath::Quaterniond> NonUnitQuaterniond();

// Generates a vector with values in the given domain.
fuzztest::Domain<eigenmath::Vector3d> Vector3dWith(
    fuzztest::Domain<double> double_domain);

// Generates a vector with finite values. Contains floating point numbers which
// are neither infinity nor NaN.
fuzztest::Domain<eigenmath::Vector3d> FiniteVector3d();

// Generates a vector with values in the given domain. The vector is guaranteed
// to have a non-zero norm.
fuzztest::Domain<eigenmath::Vector3d> NonZeroVector3dWith(
    fuzztest::Domain<double> double_domain);

// Generates a vector with values in the given domain.
fuzztest::Domain<eigenmath::Vector4d> Vector4dWith(
    fuzztest::Domain<double> double_domain);

// Generates a vector with values in the given domain.
fuzztest::Domain<eigenmath::Vector6d> Vector6dWith(
    fuzztest::Domain<double> double_domain);

// Generates a vector with finite values. Contains floating point numbers which
// are neither infinity nor NaN.
fuzztest::Domain<eigenmath::Vector6d> FiniteVector6d();

template <typename ProtoTy>
auto FiniteDoubleProto() {
  return fuzztest::Arbitrary<ProtoTy>().WithDoubleFields(
      fuzztest::Finite<double>());
}

// Generates a pose with a finite translation and a unit quaternion. Note, the
// composition of finite poses is not guaranteed to be finite! In order to
// maintain this property, your test must set the floating point rounding mode
// to be FE_TOWARDZERO. For more details please see
// go/intrinsic-maintaining-finite-poses.
fuzztest::Domain<Pose> FinitePose();

// Generate a proto using FinitePose(). The ProtoFields() version is for use
// in `.WithProtobufField()` combinators. Note, the same caveats from
// `FinitePose` regarding composition of poses derived from this proto applies.
fuzztest::Domain<intrinsic_proto::Pose> FinitePoseProto();
fuzztest::Domain<std::unique_ptr<google::protobuf::Message>>
FinitePoseProtoFields();

// Generates a matrix with values in the given domain.
fuzztest::Domain<eigenmath::Matrix3d> Matrix3dWith(
    fuzztest::Domain<double> double_domain);

// Returns `m` unchanged if it is well-conditioned (condition number <= 1e3) and
// its largest absolute value lies in [1e-100, 1e100]. Otherwise raises the
// singular values of `m` that are smaller than 1e-3 of the largest one to that
// bound, and rescales the result so that its largest absolute value is
// approximately clamped to [1e-100, 1e100]. Non-finite and zero matrices map to
// the identity. The determinant of the returned matrix is always a normal
// (non-zero, finite) double.
eigenmath::Matrix3d RepairToFullRank(const eigenmath::Matrix3d& m);

// Generates a full-rank matrix from a matrix with values in the given domain
// using RepairToFullRank().
// Well-conditioned matrices (condition number <= 1e3) whose largest absolute
// value lies in [1e-100, 1e100] are generated unchanged. Other matrices are
// repaired by raising their small singular values and clamping their scale, so
// their values may lie outside of the given domain. Matrices with non-finite or
// only zero values are replaced by the identity.
fuzztest::Domain<eigenmath::Matrix3d> FullRankMatrix3dWith(
    fuzztest::Domain<double> double_domain);

// Generates a matrix with finite values. Contains floating point numbers which
// are neither infinity nor NaN.
fuzztest::Domain<eigenmath::Matrix3d> FiniteMatrix3d();

// Generates a rotation matrix.
fuzztest::Domain<eigenmath::Matrix3d> RotationMatrix3d();

}  // namespace intrinsic::testing

#endif  // INTRINSIC_MATH_TESTING_FUZZ_MATH_UTILS_H_
