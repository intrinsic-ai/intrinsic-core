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

#include "intrinsic/math/testing/fuzz_math_util.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

#include "Eigen/Core"
#include "Eigen/SVD"
#include "fuzztest/fuzztest.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/proto/pose.pb.h"
#include "intrinsic/math/proto_conversion.h"

namespace intrinsic::testing {

using ::intrinsic::eigenmath::Matrix3d;
using ::intrinsic::eigenmath::Quaterniond;
using ::intrinsic::eigenmath::Vector3d;
using ::intrinsic::eigenmath::Vector4d;
using ::intrinsic::eigenmath::Vector6d;

constexpr double kZeroSquaredNormThreshold = 1e-6;

fuzztest::Domain<Quaterniond> QuaterniondWith(
    fuzztest::Domain<double> double_domain) {
  return fuzztest::ConstructorOf<Quaterniond>(double_domain, double_domain,
                                              double_domain, double_domain);
}

fuzztest::Domain<Quaterniond> UnitQuaterniond() {
  return fuzztest::Map(
      [](const Quaterniond& q) -> Quaterniond { return q.normalized(); },
      fuzztest::Filter(
          [](const Quaterniond& q) {
            return q.squaredNorm() > kZeroSquaredNormThreshold;
          },
          QuaterniondWith(fuzztest::Map(
              [](double x) {
                // Sample normal distribution. If we assume that
                // fuzztest provides uniform values in the range
                // provided, then, when normalized, these vectors
                // this would create a uniform sampling of the N-d
                // ball surface:
                // https://mathworld.wolfram.com/HyperspherePointPicking.html
                return std::exp(-0.5 * x * x) / std::sqrt(2 * M_PI);
              },
              fuzztest::InRange(-1.5, 1.5)))));
}

fuzztest::Domain<Quaterniond> FiniteQuaterniond() {
  return QuaterniondWith(fuzztest::Finite<double>());
}

fuzztest::Domain<Quaterniond> NonUnitQuaterniond() {
  // As-is, generating a unit quaternion is almost impossible. To be 100% sure,
  // set one scalar > 1, thus, the sum of the squared elements must be > 1.0.
  return fuzztest::Map(
      [](Quaterniond q, double gt_one, int coeff) {
        q.coeffs()[coeff] = gt_one;
        return q;
      },
      FiniteQuaterniond(),
      fuzztest::InRange(1.1, std::numeric_limits<double>::max()),
      fuzztest::InRange(0, 3));
}

fuzztest::Domain<Vector3d> Vector3dWith(
    fuzztest::Domain<double> double_domain) {
  return fuzztest::ConstructorOf<Vector3d>(double_domain, double_domain,
                                           double_domain);
}

fuzztest::Domain<Vector3d> FiniteVector3d() {
  return Vector3dWith(fuzztest::Finite<double>());
}

fuzztest::Domain<Vector3d> NonZeroVector3dWith(
    fuzztest::Domain<double> double_domain) {
  return fuzztest::Filter(
      [](const Vector3d& v) {
        return v.squaredNorm() > kZeroSquaredNormThreshold;
      },
      Vector3dWith(double_domain));
}

fuzztest::Domain<Vector4d> Vector4dWith(
    fuzztest::Domain<double> double_domain) {
  return fuzztest::ConstructorOf<Vector4d>(double_domain, double_domain,
                                           double_domain, double_domain);
}

fuzztest::Domain<Vector6d> Vector6dWith(
    fuzztest::Domain<double> double_domain) {
  return fuzztest::ConstructorOf<Vector6d>(double_domain, double_domain,
                                           double_domain, double_domain,
                                           double_domain, double_domain);
}

fuzztest::Domain<Vector6d> FiniteVector6d() {
  return Vector6dWith(fuzztest::Finite<double>());
}

fuzztest::Domain<Pose> FinitePose() {
  return fuzztest::ConstructorOf<Pose>(UnitQuaterniond(), FiniteVector3d());
}

fuzztest::Domain<intrinsic_proto::Pose> FinitePoseProto() {
  return fuzztest::Map([](const Pose& p) { return ToProto(p); }, FinitePose());
}

fuzztest::Domain<std::unique_ptr<google::protobuf::Message>>
FinitePoseProtoFields() {
  return fuzztest::Map(
      [](const intrinsic_proto::Pose& p)
          -> std::unique_ptr<google::protobuf::Message> {
        return std::make_unique<intrinsic_proto::Pose>(p);
      },
      FinitePoseProto());
}

fuzztest::Domain<Matrix3d> Matrix3dWith(
    fuzztest::Domain<double> double_domain) {
  return fuzztest::Map(
      [](const Vector3d& v1, const Vector3d& v2, const Vector3d& v3) {
        Matrix3d m;
        m << v1, v2, v3;
        return m;
      },
      Vector3dWith(double_domain), Vector3dWith(double_domain),
      Vector3dWith(double_domain));
}

namespace {

// Smallest allowed ratio between the smallest and the largest singular value of
// a matrix generated by FullRankMatrix3dWith(), i.e., the inverse of its
// largest allowed condition number.
constexpr double kMinRelativeSingularValue = 1e-3;

// Bounds on the largest absolute coefficient of a matrix generated by
// FullRankMatrix3dWith(). Together with kMinRelativeSingularValue they keep the
// determinant within the range of normal (non-zero, finite) doubles.
constexpr double kMinFullRankMatrixScale = 1e-100;
constexpr double kMaxFullRankMatrixScale = 1e100;

}  // namespace

Matrix3d RepairToFullRank(const Matrix3d& m) {
  if (!m.allFinite()) {
    return Matrix3d::Identity();
  }
  const double max_coeff = m.cwiseAbs().maxCoeff();
  if (max_coeff == 0.0) {
    return Matrix3d::Identity();
  }
  const double scale =
      std::clamp(max_coeff, kMinFullRankMatrixScale, kMaxFullRankMatrixScale);

  // Normalizes the coefficients to [-1, 1] so that the SVD neither overflows
  // nor underflows.
  Eigen::JacobiSVD<Matrix3d> svd(m / max_coeff,
                                 Eigen::ComputeFullU | Eigen::ComputeFullV);
  Vector3d singular_values = svd.singularValues();
  // The largest singular value `singular_values(0)` >= 1, so
  // `min_singular_value` is at least kMinRelativeSingularValue.
  const double min_singular_value =
      kMinRelativeSingularValue * singular_values(0);
  // `singular_values(2)` is the smallest singular value.
  if (scale == max_coeff && singular_values(2) >= min_singular_value) {
    return m;
  }
  singular_values = singular_values.cwiseMax(min_singular_value);
  return scale * svd.matrixU() * singular_values.asDiagonal() *
         svd.matrixV().transpose();
}

fuzztest::Domain<Matrix3d> FullRankMatrix3dWith(
    fuzztest::Domain<double> double_domain) {
  return fuzztest::Map(RepairToFullRank, Matrix3dWith(double_domain));
}

fuzztest::Domain<Matrix3d> FiniteMatrix3d() {
  return Matrix3dWith(fuzztest::Finite<double>());
}

fuzztest::Domain<Matrix3d> RotationMatrix3d() {
  return fuzztest::Map(
      [](const Quaterniond& q) -> Matrix3d { return q.toRotationMatrix(); },
      UnitQuaterniond());
}

}  // namespace intrinsic::testing
