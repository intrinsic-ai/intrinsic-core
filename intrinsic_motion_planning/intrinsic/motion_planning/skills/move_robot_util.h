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

#ifndef MOTION_PLANNING_SKILLS_MOVE_ROBOT_UTIL_H_
#define MOTION_PLANNING_SKILLS_MOVE_ROBOT_UTIL_H_

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "intrinsic/icon/proto/v1/condition_types.pb.h"
#include "intrinsic/motion_planning/proto/v1/motion_specification.pb.h"
#include "intrinsic/motion_planning/skills/move_robot.pb.h"
#include "intrinsic/skills/cc/equipment_pack.h"

namespace intrinsic::skills {

// Returns an error if the parameters are not correctly set for usage.
absl::Status ValidateMoveUntilSignal(
    const intrinsic_proto::skills::MoveUntilSignalParameters& params);

// Translates MoveUntilSignalParameters to a core ICON Condition.
//
// If the parameters do not specify an adio_part_name, we attempt to deduce it
// from the equipment registry. If the equipment specifies a single ADIO part,
// we use it, otherwise we return an error.
absl::StatusOr<intrinsic_proto::icon::v1::Condition>
TranslateMoveUntilSignalParametersToCondition(
    const intrinsic_proto::skills::MoveUntilSignalParameters& params,
    const EquipmentPack& equipment);

absl::StatusOr<intrinsic_proto::motion_planning::v1::MotionSpecification>
CreateMotionSpecificationFromMoveRobotSkillParams(
    intrinsic_proto::skills::MoveRobotParams const& params);

}  // namespace intrinsic::skills

#endif  // MOTION_PLANNING_SKILLS_MOVE_ROBOT_UTIL_H_
