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

#ifndef INTRINSIC_ICON_CONTROL_ALGORITHMS_FEEDFORWARD_WRENCH_INTERPOLATOR_LINEAR_H_
#define INTRINSIC_ICON_CONTROL_ALGORITHMS_FEEDFORWARD_WRENCH_INTERPOLATOR_LINEAR_H_

#include <memory>

#include "intrinsic/icon/control/algorithms/ensure_clamped.h"
#include "intrinsic/icon/control/algorithms/feedforward_wrench_interpolator_interface.h"
#include "intrinsic/icon/control/algorithms/feedforward_wrench_interpolator_settings.h"
#include "intrinsic/kinematics/types/joint_state.h"
#include "intrinsic/math/twist.h"

namespace intrinsic {
namespace agent_bridge {

// Linearly interpolates along the line from current to target wrench.
// Obeys the wrench dot limits.
// NOTE: wrench_dotdot limits are not enforced.

class FeedforwardWrenchInterpolatorLinear
    : public FeedforwardWrenchInterpolatorInterface {
 public:
  FeedforwardWrenchInterpolatorLinear();
  // Initializes the interpolator with the control frequency.
  void Init(double frequency) override;
  // Applies new settings to the interpolator.
  bool ApplyFeedforwardWrenchInterpolatorSettings(
      const FeedforwardWrenchInterpolatorSettings& settings) override;
  // Computes the next wrench, given current wrench
  // and target wrench. Returns success / failure as a boolean.
  bool ComputeNextWrench(const Wrench& current_wrench,
                         const Wrench& target_wrench,
                         Wrench* next_wrench) override;

 private:
  double timestep_;
  FeedforwardWrenchInterpolatorSettings settings_;
};

}  // namespace agent_bridge
}  // namespace intrinsic

#endif  // INTRINSIC_ICON_CONTROL_ALGORITHMS_FEEDFORWARD_WRENCH_INTERPOLATOR_LINEAR_H_
