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

#ifndef INTRINSIC_EIGENMATH_PYTHON_POSE3_CASTER_H_
#define INTRINSIC_EIGENMATH_PYTHON_POSE3_CASTER_H_

#include <pybind11/cast.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <cstdint>
#include <exception>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <typeinfo>

#include "intrinsic/eigenmath/types.h"
#include "intrinsic/math/pose3.h"

namespace pybind11 {
namespace detail {

template <>
struct type_caster<intrinsic::Pose3d> {
  PYBIND11_TYPE_CASTER(intrinsic::Pose3d, _<intrinsic::Pose3d>());

  bool load(handle src, bool) {
    auto quaternion = src.attr("quaternion");
    double qw = quaternion.attr("w").cast<double>();
    double qx = quaternion.attr("x").cast<double>();
    double qy = quaternion.attr("y").cast<double>();
    double qz = quaternion.attr("z").cast<double>();
    auto translation = src.attr("translation");
    auto getitem = translation.attr("__getitem__");
    value = intrinsic::Pose3d(
        intrinsic::eigenmath::Quaterniond(qw, qx, qy, qz),
        intrinsic::eigenmath::Vector3d(getitem(0).cast<double>(),
                                       getitem(1).cast<double>(),
                                       getitem(2).cast<double>()));
    return true;
  }

  static handle cast(const intrinsic::Pose3d& pose, return_value_policy,
                     handle) {
    auto py_pose3_t =
        module::import("intrinsic.math.python.data_types").attr("Pose3");
    auto from_vec7 = py_pose3_t.attr("from_vec7");
    auto py_pose3 = from_vec7(pybind11::make_tuple(
        pose.translation().x(), pose.translation().y(), pose.translation().z(),
        pose.quaternion().x(), pose.quaternion().y(), pose.quaternion().z(),
        pose.quaternion().w()));
    return py_pose3.release();
  }
};

}  // namespace detail
}  // namespace pybind11

#endif  // INTRINSIC_EIGENMATH_PYTHON_POSE3_CASTER_H_
