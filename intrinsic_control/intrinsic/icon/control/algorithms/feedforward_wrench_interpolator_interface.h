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

#ifndef INTRINSIC_ICON_CONTROL_ALGORITHMS_FEEDFORWARD_WRENCH_INTERPOLATOR_INTERFACE_H_
#define INTRINSIC_ICON_CONTROL_ALGORITHMS_FEEDFORWARD_WRENCH_INTERPOLATOR_INTERFACE_H_

#include "absl/base/macros.h"
#include "intrinsic/icon/control/algorithms/feedforward_wrench_interpolator_settings.h"
#include "intrinsic/kinematics/types/joint_state.h"
#include "intrinsic/math/twist.h"

namespace intrinsic {
namespace agent_bridge {

// Interface for smoothly varying requested feedforward wrench.
class FeedforwardWrenchInterpolatorInterface {
 public:
  // Initializes the interpolator with the control frequency.
  virtual void Init(double frequency) = 0;
  // Applies new limits to the interpolator.
  virtual bool ApplyFeedforwardWrenchInterpolatorSettings(
      const FeedforwardWrenchInterpolatorSettings& settings) = 0;
  // Computes the next wrench, given current wrench
  // and target wrench. Returns success / failure as a boolean.
  ABSL_MUST_USE_RESULT virtual bool ComputeNextWrench(
      const Wrench& current_wrench, const Wrench& target_wrench,
      Wrench* next_wrench) = 0;

  virtual ~FeedforwardWrenchInterpolatorInterface() = default;
};

}  // namespace agent_bridge
}  // namespace intrinsic

#endif  // INTRINSIC_ICON_CONTROL_ALGORITHMS_FEEDFORWARD_WRENCH_INTERPOLATOR_INTERFACE_H_
