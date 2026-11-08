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

#ifndef INTRINSIC_MATH_LINEAR_SYSTEMS_TESTING_HARMONIC_OSCILLATOR_TEST_SYSTEM_H_
#define INTRINSIC_MATH_LINEAR_SYSTEMS_TESTING_HARMONIC_OSCILLATOR_TEST_SYSTEM_H_

#include "intrinsic/eigenmath/types.h"

namespace intrinsic {

struct HarmonicOscillatorTestSystem {
  static constexpr size_t kStateDim = 2;
  static constexpr size_t kControlDim = 1;
  static constexpr size_t kOutputDim = 1;

  HarmonicOscillatorTestSystem() = delete;

  // Construct the discrete-time linear system equations for a harmonic
  // oscillator and sampling time dt.
  explicit HarmonicOscillatorTestSystem(const double dt) {
    double k = 10.0;  // Spring stiffness [N/m]
    double m = 1.0;   // Mass [kg].
    double d = 0.1;   // Damping coefficient [kg/m/s].

    A.resize(kStateDim, kStateDim);
    A << 1, dt, -dt * k / m, 1.0 - dt * d / m;

    B.resize(kStateDim, kControlDim);
    B << 0, dt / m;

    C.resize(kOutputDim, kStateDim);
    C << 1, 0;  // We observe position only.
  }

  eigenmath::MatrixNd A;  // NOLINT(google3-readability-class-member-naming)
  eigenmath::MatrixNd B;  // NOLINT(google3-readability-class-member-naming)
  eigenmath::MatrixNd C;  // NOLINT(google3-readability-class-member-naming)
};

}  // namespace intrinsic

#endif  // INTRINSIC_MATH_LINEAR_SYSTEMS_TESTING_HARMONIC_OSCILLATOR_TEST_SYSTEM_H_
