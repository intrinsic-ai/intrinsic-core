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

#ifndef INTRINSIC_ICON_CONTROL_ALGORITHMS_FEEDFORWARD_WRENCH_INTERPOLATOR_SETTINGS_H_
#define INTRINSIC_ICON_CONTROL_ALGORITHMS_FEEDFORWARD_WRENCH_INTERPOLATOR_SETTINGS_H_

#include "intrinsic/math/twist.h"

namespace intrinsic {
namespace agent_bridge {

struct FeedforwardWrenchInterpolatorSettings {
  // Max feedforward wrench.
  Wrench max_wrench;
  // Min feedforward wrench.
  Wrench min_wrench;
  // An abrupt change in wrench has similar
  // effects on control output as an abrubt change in setpoint position.
  // We use these constraints to control the smoothness of the
  // wrench profile.
  eigenmath::Vector6d max_wrench_dot;
  eigenmath::Vector6d max_wrench_dotdot;
};

}  // namespace agent_bridge
}  // namespace intrinsic

#endif  // INTRINSIC_ICON_CONTROL_ALGORITHMS_FEEDFORWARD_WRENCH_INTERPOLATOR_SETTINGS_H_
