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

#ifndef INTRINSIC_ICON_ACTIONS_LINEAR_GRIPPER_MOVE_INFO_H_
#define INTRINSIC_ICON_ACTIONS_LINEAR_GRIPPER_MOVE_INFO_H_

#include <optional>

#include "absl/types/optional.h"
#include "intrinsic/icon/actions/linear_gripper_move.pb.h"
#include "intrinsic/icon/proto/v1/types.pb.h"

namespace intrinsic::icon {

struct LinearGripperMoveInfo {
  static constexpr char kActionTypeName[] = "intrinsic.linear_gripper_move";
  static constexpr char kActionDescription[] =
      "Move the gripper's grasping width to the desired target width.";
  static constexpr char kIsDoneDescription[] =
      "This Action reports 'done' as when the distance to the commanded target "
      "is less than the convergence tolerance.";
  static constexpr char kDistanceToSensed[] = "intrinsic.distance_to_sensed";
  static constexpr char kDistanceToSensedDescription[] =
      "Distance between the commanded and the sensed width.";

  static constexpr char kSlotName[] = "linear_gripper";
  static constexpr char kSlotDescription[] =
      "A Part that implements the LinearGripper Feature Interface.";

  using FixedParams =
      ::intrinsic_proto::icon::actions::proto::LinearGripperMoveFixedParams;
};

intrinsic_proto::icon::v1::ActionSignature GetLinearGripperMoveSignature();

}  // namespace intrinsic::icon

#endif  // INTRINSIC_ICON_ACTIONS_LINEAR_GRIPPER_MOVE_INFO_H_
