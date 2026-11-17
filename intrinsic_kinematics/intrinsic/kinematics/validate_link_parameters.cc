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

#include "intrinsic/kinematics/validate_link_parameters.h"

#include <cmath>

#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/math/inertia_utils.h"

namespace intrinsic::kinematics {

icon::RealtimeStatus ValidateMass(double mass_kg) {
  if (!std::isfinite(mass_kg)) {
    return icon::InvalidArgumentError(icon::RealtimeStatus::StrCat(
        "The mass should be finite, but got ", mass_kg, " kg instead."));
  }
  if (mass_kg <= 0) {
    return icon::InvalidArgumentError(icon::RealtimeStatus::StrCat(
        "The mass should be > 0.0, but got ", mass_kg, " kg instead."));
  }
  return icon::OkStatus();
}

icon::RealtimeStatus ValidateInertia(const eigenmath::Matrix3d& inertia) {
  return intrinsic::ValidateInertia(inertia);
}

}  // namespace intrinsic::kinematics
