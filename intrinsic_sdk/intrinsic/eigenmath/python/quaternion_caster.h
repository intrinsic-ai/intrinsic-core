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

#ifndef INTRINSIC_EIGENMATH_PYTHON_QUATERNION_CASTER_H_
#define INTRINSIC_EIGENMATH_PYTHON_QUATERNION_CASTER_H_

#include <pybind11/eigen.h>
#include <pybind11/numpy.h>

#include "Eigen/Geometry"

namespace pybind11 {
namespace detail {

// Convert between Eigen::Quaternion and NumPy arrays.
// NOTE: Eigen::Quaternion stores its coefficients as (x, y, z, w) while
// NumPy-based libraries typically represent quaternions as (w, x, y, z), so
// we can't give a view of the Eigen::Quaternion buffer.
template <typename Scalar, int Options>
struct type_caster<Eigen::Quaternion<Scalar, Options>> {
 public:
  using Quaternion = Eigen::Quaternion<Scalar, Options>;
  // This macro declares a local variable 'value' of type Eigen::Quaternion.
  PYBIND11_TYPE_CASTER(Quaternion, _("Quaternion"));

  // Conversion part 1 (Python->C++)
  bool load(handle src, bool convert) {
    if (!convert) {
      return false;
    }
    array buf = array::ensure(src);
    if (!buf || buf.ndim() != 1 || buf.size() != 4) {
      return false;
    }
    array_t<Scalar> array = src.cast<array_t<Scalar>>();
    auto reader = array.unchecked();
    value = Quaternion(reader(0), reader(1), reader(2), reader(3));
    return true;
  }

  // Conversion part 2 (C++ -> Python)
  static handle cast(const Quaternion& src, return_value_policy, handle) {
    array_t<Scalar> array({4});
    auto writer = array.mutable_unchecked();
    writer(0) = src.w();
    writer(1) = src.x();
    writer(2) = src.y();
    writer(3) = src.z();
    return array.release();
  }
};

}  // namespace detail
}  // namespace pybind11

#endif  // INTRINSIC_EIGENMATH_PYTHON_QUATERNION_CASTER_H_
