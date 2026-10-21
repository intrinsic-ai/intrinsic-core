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

#ifndef INTRINSIC_MOTION_PLANNING_TRAJECTORY_EXECUTION_UTILS_H_
#define INTRINSIC_MOTION_PLANNING_TRAJECTORY_EXECUTION_UTILS_H_

#include <memory>
#include <optional>
#include <string>

#include "absl/status/status.h"
#include "absl/types/span.h"
#include "intrinsic/icon/equipment/equipment_utils.h"
#include "intrinsic/icon/proto/joint_space.pb.h"
#include "intrinsic/icon/proto/v1/condition_types.pb.h"
#include "intrinsic/logging/proto/context.pb.h"
#include "intrinsic/motion_planning/proto/v1/motion_events.pb.h"  
#include "intrinsic/skills/cc/skill_canceller.h"
#include "intrinsic/util/grpc/channel_interface.h"

namespace intrinsic::motion_planning {

// Creates and executes a TrajectoryTrackingAction.
//
// Pre-condition:
// * The ICON equipment has a position-controlled arm part.
//
// Params:
//   icon_equipment: The ICON equipment with a position-controlled arm part
//      and potentially ADIO part.
//   joint_trajectory_proto:
//      The joint trajectory to execute.
//   settling_timeout_seconds:
//      The duration of time to wait after the trajectory is done to consider
//      it settled.
//   use_is_settled_as_condition:
//      If true, uses the `is_settled` state of the tracking action to
//      determine if it's done. Otherwise, uses the completion of the
//      trajectory.
//   canceller:
//      An object to handle cancellations.
//   move_until_signal_condition:
//      Optional condition. If present, adds "move_until_signal" behavior to
//      the trajectory.
//   stopped_on_signal:
//      An optional flag to reflect if the motion is stopped by signal. This is
//      an output parameter.
absl::Status ExecuteJointTrajectory(
    const intrinsic_proto::data_logger::Context& context,
    const icon::IconEquipment& icon_equipment,
    const intrinsic_proto::icon::JointTrajectoryPVA& joint_trajectory_proto,
    double settling_timeout_seconds, bool use_is_settled_as_condition,
    skills::SkillCanceller& canceller,
    std::optional<intrinsic_proto::icon::v1::Condition>
        move_until_signal_condition = std::nullopt,
    bool* stopped_on_signal = nullptr

    ,
    absl::Span<const intrinsic_proto::motion_planning::v1::MotionEvent>
        motion_events = {}


);

// Same as above but with the icon equipment broken down for more convenient
// usage.
absl::Status ExecuteJointTrajectory(
    const intrinsic_proto::data_logger::Context& context,
    const std::shared_ptr<ChannelInterface>& channel,
    const std::string& position_part_name,
    const intrinsic_proto::icon::JointTrajectoryPVA& joint_trajectory_proto,
    double settling_timeout_seconds, bool use_is_settled_as_condition,
    skills::SkillCanceller& canceller,
    std::optional<intrinsic_proto::icon::v1::Condition>
        move_until_signal_condition = std::nullopt,
    bool* stopped_on_signal = nullptr

    ,
    absl::Span<const intrinsic_proto::motion_planning::v1::MotionEvent>
        motion_events = {}


);

}  // namespace intrinsic::motion_planning

#endif  // INTRINSIC_MOTION_PLANNING_TRAJECTORY_EXECUTION_UTILS_H_
