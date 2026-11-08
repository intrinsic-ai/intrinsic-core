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

#ifndef INTRINSIC_ICON_ACTIONS_NATIVE_HAND_GUIDING_INFO_H_
#define INTRINSIC_ICON_ACTIONS_NATIVE_HAND_GUIDING_INFO_H_

#include "google/protobuf/empty.pb.h"

namespace intrinsic {
namespace icon {

struct NativeHandGuidingInfo {
  static constexpr char kActionTypeName[] = "intrinsic.native_hand_guiding";
  static constexpr char kActionDescription[] =
      "Commands the robot into the hand-guiding control mode. The part faults "
      "if the preconditions for switching to hand guiding are not met when the "
      "action becomes active.";
  static constexpr char kSlotName[] = "hand_guiding";
  static constexpr char kSlotDescription[] =
      "The action activates the native hand guiding control mode of this part";

  // The action doesn't accept any parameters.
  using FixedParams = google::protobuf::Empty;
};

}  // namespace icon
}  // namespace intrinsic

#endif  // INTRINSIC_ICON_ACTIONS_NATIVE_HAND_GUIDING_INFO_H_
