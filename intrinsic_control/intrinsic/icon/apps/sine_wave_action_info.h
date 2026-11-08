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

#ifndef INTRINSIC_ICON_APPS_SINE_WAVE_ACTION_INFO_H_
#define INTRINSIC_ICON_APPS_SINE_WAVE_ACTION_INFO_H_

#include "intrinsic/icon/apps/sine_wave_action.pb.h"

namespace intrinsic::icon {

struct SineWaveActionInfo {
  static constexpr char kActionTypeName[] = "intrinsic.sine_wave";
  static constexpr char kSlotName[] = "arm";
  static constexpr char kSlotDescription[] =
      "The Action moves the joints of this Part according to the function "
      "sin(f*t)^3. Where f is the frequency and t is the passed time. This "
      "function has zero velocity and acceleration whenever f*t=0,pi,2*pi, "
      "etc.";
  static constexpr char kTime[] = "intrinsic.sine_wave.time";
  static constexpr char kTimeDescription[] =
      "Time (in seconds) since the Action became active. This is fed into the "
      "sine function.";

  using FixedParams = intrinsic_proto::icon::actions::SineWaveFixedParams;
};

}  // namespace intrinsic::icon

#endif  // INTRINSIC_ICON_APPS_SINE_WAVE_ACTION_INFO_H_
