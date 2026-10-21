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

#ifndef INTRINSIC_UTIL_TIME_CLOCK_STEADY_INTERFACE_H_
#define INTRINSIC_UTIL_TIME_CLOCK_STEADY_INTERFACE_H_

#include "intrinsic/icon/testing/realtime_annotations.h"
#include "intrinsic/util/time/time.h"

namespace intrinsic {

// A clock that monotonically increases in time, unlike a system clock (aka wall
// clock). ClockSteadyInterface is useful for measuring elapsed time.
class ClockSteadyInterface {
 public:
  virtual ~ClockSteadyInterface() = default;

  // Returns a TimePointSteady representing the current value of the clock.
  virtual TimeSteady Now() const INTRINSIC_CHECK_REALTIME_SAFE = 0;
};

}  // namespace intrinsic

#endif  // INTRINSIC_UTIL_TIME_CLOCK_STEADY_INTERFACE_H_
