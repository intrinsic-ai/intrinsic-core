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

#include "intrinsic/icon/control/algorithms/feedforward_wrench_interpolator_linear.h"

#include <algorithm>

#include "intrinsic/icon/control/algorithms/ensure_clamped.h"
#include "intrinsic/icon/control/algorithms/feedforward_wrench_interpolator_settings.h"
#include "intrinsic/math/twist.h"

namespace intrinsic {
namespace agent_bridge {

FeedforwardWrenchInterpolatorLinear::FeedforwardWrenchInterpolatorLinear() =
    default;

void FeedforwardWrenchInterpolatorLinear::Init(double frequency) {
  timestep_ = 1 / frequency;
}

bool FeedforwardWrenchInterpolatorLinear::
    ApplyFeedforwardWrenchInterpolatorSettings(
        const FeedforwardWrenchInterpolatorSettings& settings) {
  settings_ = settings;
  return true;
}

bool FeedforwardWrenchInterpolatorLinear::ComputeNextWrench(
    const Wrench& current_wrench, const Wrench& target_wrench,
    Wrench* next_wrench) {
  const Wrench clamped_target_wrench = intrinsic::control::EnsureClampedAndWarn(
      target_wrench, settings_.min_wrench, settings_.max_wrench);

  // Since each dof varies independently, they are handled separately.
  for (int i = 0; i < 6; ++i) {
    if (clamped_target_wrench[i] > current_wrench[i]) {
      (*next_wrench)[i] =
          current_wrench[i] + (settings_.max_wrench_dot[i] * timestep_);
      (*next_wrench)[i] = std::min(clamped_target_wrench[i], (*next_wrench)[i]);
    } else if (clamped_target_wrench[i] < current_wrench[i]) {
      (*next_wrench)[i] =
          current_wrench[i] - (settings_.max_wrench_dot[i] * timestep_);
      (*next_wrench)[i] = std::max(clamped_target_wrench[i], (*next_wrench)[i]);
    } else {
      (*next_wrench)[i] = current_wrench[i];
    }
  }

  return true;
}

}  // namespace agent_bridge
}  // namespace intrinsic
