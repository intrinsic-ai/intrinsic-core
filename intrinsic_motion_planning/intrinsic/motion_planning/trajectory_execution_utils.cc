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

#include "intrinsic/motion_planning/trajectory_execution_utils.h"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/cleanup/cleanup.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "intrinsic/icon/actions/adio_info.h"
#include "intrinsic/icon/actions/empty_action_signature.h"  
#include "intrinsic/icon/actions/trajectory_tracking_action_info.h"
#include "intrinsic/icon/cc_client/client.h"
#include "intrinsic/icon/cc_client/client_utils.h"
#include "intrinsic/icon/cc_client/condition.h"
#include "intrinsic/icon/cc_client/session.h"
#include "intrinsic/icon/common/builtins.h"
#include "intrinsic/icon/common/id_types.h"
#include "intrinsic/icon/proto/v1/condition_types.pb.h"
#include "intrinsic/icon/proto/v1/motion_event_types.pb.h"  
#include "intrinsic/logging/proto/context.pb.h"
#include "intrinsic/motion_planning/proto/v1/motion_events.pb.h"  
#include "intrinsic/skills/cc/skill_canceller.h"
#include "intrinsic/stats/scoped_span.h"
#include "intrinsic/util/grpc/channel_interface.h"
#include "intrinsic/util/proto/any.h"
#include "intrinsic/util/proto_time.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/thread/thread.h"

namespace intrinsic::motion_planning {

using ::intrinsic::icon::AllOf;
using ::intrinsic::icon::AnyOf;
using ::intrinsic::icon::IsGreaterThanOrEqual;
using ::intrinsic::icon::IsTrue;
using ::intrinsic::icon::ReactionDescriptor;
using ::intrinsic::skills::SkillCanceller;

namespace {

// Because we are sending fully sampled trajectories,
// we need to make sure we respect ICON's upper bound
// for inbound data to the trajectory execution action.
// In practice, this is of little concern as this allows
// us to send ~16 minutes long trajectories at 1kHz.
constexpr size_t kMaxTrajectorySampleSize = 1'000'000;

constexpr double kEpsilonSpeedOverride = 1e-6;
// Phase-based completion check. Unlike IsDone(), this stays false after an
// early path-accurate stop, so unreached events are not fired on early stop,
// while end-of-trajectory events still fire on nominal completion.
constexpr double kTrajectoryCompletedProgress = 1.0 - 1e-12;

absl::StatusOr<std::vector<icon::ActionDescriptor>> AddMotionEventsAndActions(
    icon::ActionDescriptor& motion_action,
    absl::Span<const intrinsic_proto::motion_planning::v1::MotionEvent>
        motion_events,
    int action_instance_id_start, double speed_override_factor) {
  std::vector<icon::ActionDescriptor> event_actions;
  const icon::Condition trajectory_completed_condition = IsGreaterThanOrEqual(
      icon::TrajectoryTrackingActionInfo::kTrajectoryProgress,
      kTrajectoryCompletedProgress);

  for (const auto& motion_event : motion_events) {
    INTR_ASSIGN_OR_RETURN(const auto skill_motion_event,
                          UnpackAny<intrinsic_proto::icon::v1::TrajectoryEvent>(
                              motion_event.event()));

    const icon::ActionInstanceId kAdioEventActionId(action_instance_id_start++);
    const icon::ActionInstanceId kAdioDefaultActionId(
        action_instance_id_start++);

    icon::ADIOActionInfo::FixedParams adio_event_fixed_params;
    if (skill_motion_event.has_adio_event()) {
      *adio_event_fixed_params.mutable_outputs() =
          skill_motion_event.adio_event().set_analog_digital_outputs();
    } else {
      return absl::InvalidArgumentError(
          "Only ADIO events are supported at the moment.");
    }
    auto adio_default_action_descriptor =
        icon::ActionDescriptor(icon::kEmptyActionTypeName, kAdioDefaultActionId,
                               {skill_motion_event.adio_event().part_name()});
    auto adio_event_action_descriptor =
        icon::ActionDescriptor(icon::ADIOActionInfo::kActionTypeName,
                               kAdioEventActionId,
                               {skill_motion_event.adio_event().part_name()})
            .WithFixedParams(adio_event_fixed_params)
            .WithReaction(
                icon::ReactionDescriptor(
                    icon::IsTrue(icon::ADIOActionInfo::kOutputsSet))
                    .WithRealtimeActionOnCondition(kAdioDefaultActionId));

    event_actions.push_back(adio_event_action_descriptor);
    event_actions.push_back(adio_default_action_descriptor);

    if (motion_event.keypoint().event_offset().has_time()) {
      INTR_ASSIGN_OR_RETURN(
          absl::Duration time_offset,
          ToAbslDuration(motion_event.keypoint().event_offset().time()));
      INTR_ASSIGN_OR_RETURN(
          absl::Duration time_keypoint,
          ToAbslDuration(motion_event.keypoint().keypoint_time_from_start()));

      // If the time offset results in a negative time, then the event will
      // fire immediately on starting the trajectory. If a scheduled event
      // extends beyond the duration of the trajectory, the event will be
      // triggered on nominal completion of the trajectory.
      const double event_time_seconds =
          absl::ToDoubleSeconds(time_offset) +
          absl::ToDoubleSeconds(time_keypoint) /
              std::max(kEpsilonSpeedOverride, speed_override_factor);
      const icon::Condition time_condition = IsGreaterThanOrEqual(
          icon::TrajectoryTrackingActionInfo::kTimeSinceTrajectoryStartSeconds,
          event_time_seconds);
      const icon::Condition trigger_condition =
          AnyOf({trajectory_completed_condition, time_condition});
      motion_action.WithReaction(
          ReactionDescriptor(trigger_condition)
              .WithParallelRealtimeActionOnCondition(kAdioEventActionId)
              .FireOnce());
    } else if (motion_event.keypoint()
                   .event_offset()
                   .has_cartesian_arc_length_meters()) {
      // If the Cartesian arc length offset results in a negative arc length,
      // then the event will fire immediately on starting the trajectory. If a
      // scheduled event extends beyond the arc length of the trajectory, the
      // event will be triggered on nominal completion of the trajectory.
      const double event_arc_length =
          motion_event.keypoint().event_offset().cartesian_arc_length_meters() +
          motion_event.keypoint()
              .keypoint_cartesian_arc_length_from_start_meters();
      const icon::Condition arc_length_condition =
          IsGreaterThanOrEqual(icon::TrajectoryTrackingActionInfo::
                                   kCartesianArcLengthAlongTrajectoryMeters,
                               event_arc_length);
      const icon::Condition trigger_condition =
          AnyOf({trajectory_completed_condition, arc_length_condition});
      motion_action.WithReaction(
          ReactionDescriptor(trigger_condition)
              .WithParallelRealtimeActionOnCondition(kAdioEventActionId)
              .FireOnce());
    } else {
      return absl::InvalidArgumentError(
          "Only time and cartesian_arc_length_meters are supported for "
          "MotionEventOffsets.");
    }
  }
  return event_actions;
}


}  // namespace

absl::Status ExecuteJointTrajectory(
    const intrinsic_proto::data_logger::Context& context,
    const icon::IconEquipment& icon_equipment,
    const intrinsic_proto::icon::JointTrajectoryPVA& joint_trajectory_proto,
    double settling_timeout_seconds, bool use_is_settled_as_condition,
    SkillCanceller& canceller,
    std::optional<intrinsic_proto::icon::v1::Condition>
        move_until_signal_condition,
    bool* stopped_on_signal

    ,
    absl::Span<const intrinsic_proto::motion_planning::v1::MotionEvent>
        motion_events


) {
  if (!icon_equipment.position_part_name.has_value()) {
    return absl::FailedPreconditionError(
        "ICON equipment doesn't have a position-controlled arm part.");
  }
  return ExecuteJointTrajectory(
      context, icon_equipment.channel, *icon_equipment.position_part_name,
      joint_trajectory_proto, settling_timeout_seconds,
      use_is_settled_as_condition, canceller, move_until_signal_condition,
      stopped_on_signal

      ,
      motion_events


  );
}

absl::Status ExecuteJointTrajectory(
    const intrinsic_proto::data_logger::Context& context,
    const std::shared_ptr<ChannelInterface>& channel,
    const std::string& position_part_name,
    const intrinsic_proto::icon::JointTrajectoryPVA& joint_trajectory_proto,
    double settling_timeout_seconds, bool use_is_settled_as_condition,
    SkillCanceller& canceller,
    std::optional<intrinsic_proto::icon::v1::Condition>
        move_until_signal_condition,
    bool* stopped_on_signal

    ,
    absl::Span<const intrinsic_proto::motion_planning::v1::MotionEvent>
        motion_events


) {
  // TODO: b/534624482 - Fail early if already cancelled.
  const stats::ScopedSpan span("skills/ExecuteJointTrajectory");
  span.AddAttribute("joint_trajectory.size",
                    joint_trajectory_proto.state_size());

  const std::string& arm_part = position_part_name;

  if (joint_trajectory_proto.state_size() > kMaxTrajectorySampleSize) {
    return absl::InternalError(
        "The generated trajectory was too long to transfer to ICON.");
  }

  if (joint_trajectory_proto.state_size() < 1) {
    return absl::InternalError("The generated trajectory is empty.");
  }

  icon::TrajectoryTrackingActionInfo::FixedParams joint_trajectory_fixed_params;
  *joint_trajectory_fixed_params.mutable_trajectory() = joint_trajectory_proto;

  // Functionality to define a timeout
  bool settling_timeout_exceeded = false;
  auto timeout_callback = [&settling_timeout_exceeded]() {
    settling_timeout_exceeded = true;
  };

  const icon::ActionInstanceId kTrackingActionId(0);
  const icon::ActionInstanceId kFinalStopActionId(1);
  auto tracking_action_descriptor =
      icon::ActionDescriptor(
          icon::TrajectoryTrackingActionInfo::kActionTypeName,
          kTrackingActionId, {arm_part})
          .WithFixedParams(joint_trajectory_fixed_params)
          .WithReaction(
              ReactionDescriptor(
                  IsGreaterThanOrEqual(icon::TrajectoryTrackingActionInfo::
                                           kTrajectoryDoneForSeconds,
                                       settling_timeout_seconds))
                  .WithWatcherOnCondition(timeout_callback)
                  .WithRealtimeActionOnCondition(kFinalStopActionId))
          .WithReaction(
              ReactionDescriptor(
                  use_is_settled_as_condition
                      ? AllOf({IsTrue(icon::TrajectoryTrackingActionInfo::
                                          kIsSettled),
                               icon::IsDone()})
                      : AllOf({icon::IsDone()}))
                  .WithRealtimeActionOnCondition(kFinalStopActionId));

  icon::ReactionHandle stop_handle(1);
  auto final_stop_action_descriptor =
      icon::ActionDescriptor(icon::kStopAction, kFinalStopActionId, {arm_part})
          .WithReaction(
              ReactionDescriptor(icon::IsDone()).WithHandle(stop_handle));

  if (move_until_signal_condition.has_value()) {
    LOG(INFO) << "Using move_until_signal.";

    if (stopped_on_signal == nullptr) {
      return absl::FailedPreconditionError(
          "stopped_on_signal has to be provided if using move_to_signal.");
    }

    INTR_ASSIGN_OR_RETURN(icon::Condition condition,
                          icon::FromProto(*move_until_signal_condition));

    // TODO: b/534622456 - Fix stale stopped_on_signal caller memory.
    tracking_action_descriptor.WithReaction(
        ReactionDescriptor(condition)
            .WithRealtimeSignalOnCondition(
                icon::TrajectoryTrackingActionInfo::kSignalPathAccurateStop)
            .WithWatcherOnCondition([stopped_on_signal]() {
              *stopped_on_signal = true;
              LOG(INFO) << "Move_to_signal triggering path accurate stop.";
            }));
  }

  icon::Client icon_client(channel);

  std::vector<std::string> parts_to_control{arm_part};


  INTR_ASSIGN_OR_RETURN(double speed_override, icon_client.GetSpeedOverride());
  absl::flat_hash_set<std::string> adio_part_names;
  for (const auto& motion_event : motion_events) {
    INTR_ASSIGN_OR_RETURN(const auto skill_motion_event,
                          UnpackAny<intrinsic_proto::icon::v1::TrajectoryEvent>(
                              motion_event.event()));
    adio_part_names.insert(skill_motion_event.adio_event().part_name());
  }

  parts_to_control.insert(parts_to_control.end(), adio_part_names.begin(),
                          adio_part_names.end());
  // Motion events use a phase-based completion condition (`kTrajectoryProgress
  // >= 1.0 - 1e-12`) as fallback. Unlike `IsDone()`, this stays false after an
  // early path-accurate stop, so unreached arc-length events are not fired on
  // early stop (preventing simultaneous trigger collisions on the same part
  // slot), while end-of-trajectory events still fire on nominal completion.
  // Note that time-based events use wall time, which keeps advancing while the
  // robot decelerates (and settles) after the stop signal, so they may still
  // fire during that phase.
  INTR_ASSIGN_OR_RETURN(
      std::vector<icon::ActionDescriptor> event_actions,
      AddMotionEventsAndActions(tracking_action_descriptor, motion_events, 2,
                                speed_override));


  std::vector<icon::ActionDescriptor> all_actions = {
      final_stop_action_descriptor, tracking_action_descriptor};


  std::move(event_actions.begin(), event_actions.end(),
            std::back_inserter(all_actions));


  absl::Status icon_status;

  {
    INTR_ASSIGN_OR_RETURN(
        std::unique_ptr<icon::Session> session,
        icon::Session::Start(channel, parts_to_control, context));

    INTR_ASSIGN_OR_RETURN(std::vector<icon::Action> actions,
                          session->AddActions(all_actions));
    icon::Action& final_stop_action = actions[0];
    icon::Action& tracking_action = actions[1];

    INTR_RETURN_IF_ERROR(session->StartAction(tracking_action));

    intrinsic::Thread thread([&canceller, &session, &final_stop_action]() {
      if (!canceller.Wait(absl::InfiniteDuration())) {
        return;
      }
      if (auto status = session->StartAction(final_stop_action); !status.ok()) {
        LOG(ERROR) << "Failed to start stop action: " << status;
      }
    });
    absl::Cleanup stop_thread = [&canceller] { canceller.StopWait(); };

    canceller.Ready();
    INTR_RETURN_IF_ERROR(session->RunWatcherLoopUntilReaction(stop_handle));
  }

  if (canceller.cancelled()) {
    return absl::CancelledError("Trajectory execution was cancelled.");
  }
  if (settling_timeout_exceeded) {
    return absl::DeadlineExceededError(
        absl::StrCat("Move failed for robot ", arm_part,
                     ". Failed to reach tolerance of goal within ",
                     settling_timeout_seconds, " seconds."));
  }
  return absl::OkStatus();
}

}  // namespace intrinsic::motion_planning
