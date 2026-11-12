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

#include <pybind11/pybind11.h>

#include "intrinsic/eigenmath/python/pose3_caster.h"
#include "intrinsic/eigenmath/so3.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/math/pose3.h"

namespace intrinsic {

PYBIND11_MODULE(pose3_test_util, m) {
  m.def("identity", [] { return Pose3d(); });
  m.def(
      "translate",
      [](const Pose3d& pose) -> Pose3d {
        return pose * Pose3d(eigenmath::Vector3d(1, 2, 3));
      },
      pybind11::arg("pose"));
  m.def(
      "rotate",
      [](const Pose3d& pose) -> Pose3d {
        return pose * Pose3d(intrinsic::eigenmath::SO3d(M_PI, 0, 0));
      },
      pybind11::arg("pose"));
}

}  // namespace intrinsic
