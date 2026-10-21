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

#include "intrinsic/icon/control/rtcl_reaction_manager.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

#include "absl/algorithm/container.h"
#include "absl/base/optimization.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/types/span.h"
#include "intrinsic/icon/common/id_types.h"
#include "intrinsic/icon/control/actions/empty_action.h"
#include "intrinsic/icon/control/behavior_override.h"
#include "intrinsic/icon/control/reaction.h"
#include "intrinsic/icon/control/realtime_actions_reactions.h"
#include "intrinsic/icon/control/realtime_bridge_types.h"
#include "intrinsic/icon/control/realtime_condition.h"
#include "intrinsic/icon/control/realtime_operational_status.h"
#include "intrinsic/icon/control/realtime_part_manager.h"
#include "intrinsic/icon/control/realtime_session_channels.h"
#include "intrinsic/icon/control/realtime_signal_access.h"
#include "intrinsic/icon/control/realtime_slot_map.h"
#include "intrinsic/icon/control/rtcl_action.h"
#include "intrinsic/icon/control/rtcl_action_instance.h"
#include "intrinsic/icon/control/rtcl_realtime_session.h"
#include "intrinsic/icon/control/streaming_io_realtime.h"
#include "intrinsic/icon/control/streaming_io_storage.h"
#include "intrinsic/icon/proto/v1/types.pb.h"
#include "intrinsic/icon/server/session_interface.h"
#include "intrinsic/icon/utils/clock.h"
#include "intrinsic/icon/utils/log.h"
#include "intrinsic/icon/utils/malloc_guard.h"
#include "intrinsic/icon/utils/realtime_stack_trace.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/icon/utils/realtime_status_macro.h"
#include "intrinsic/icon/utils/realtime_status_or.h"
#include "intrinsic/util/fixed_vector.h"

namespace intrinsic::icon {
namespace {

using ::intrinsic::icon::BehaviorOverrideRequestToRealtimeSafeString;
using ::intrinsic_proto::icon::v1::BehaviorOverrideRequest;

bool ActionPairHasSlotOverlap(const RtclActionInstance& action1,
                              const RtclActionInstance& action2) {
  const size_t min_size = std::min(action1.visibility_by_slot_id->size(),
                                   action2.visibility_by_slot_id->size());
  for (size_t i = 0; i < min_size; ++i) {
    if (action1.visibility_by_slot_id->at(i) &&
        action2.visibility_by_slot_id->at(i)) {
      return true;
    }
  }
  return false;
}

// Evaluates the state variables of `current_action` to determine whether
// `reaction` should trigger.
RealtimeStatusOr<bool> ReactionConditionMatches(
    const RtclActionInterface& current_action, const RealtimeReaction& reaction,
    const AggregatedRobotStatus& robot_status) {
  RealtimeStatusOr<bool> matches_or =
      LookupAndEvaluate(current_action, reaction.condition, robot_status);
  if (reaction.from_action_index.has_value()) {
    if (ABSL_PREDICT_FALSE(!matches_or.ok())) {
      // Unavailable is an expected error while an Action is still executing or
      // waiting for something it needs to populate a State Variable's value.
      // Only log *other* errors.
      if (matches_or.status().code() == absl::StatusCode::kUnavailable) {
        return false;
      }
      INTRINSIC_RT_LOG_THROTTLED(ERROR)
          << "failed to evaluate condition : " << matches_or.status().message();
    } else if (matches_or.value()) {
      INTRINSIC_RT_LOG_THROTTLED(INFO)
          << "realtime reaction (id " << reaction.id.value()
          << ") matches on current action index "
          << *reaction.from_action_index;
    }
  } else {
    if (ABSL_PREDICT_FALSE(!matches_or.ok())) {
      INTRINSIC_RT_LOG_THROTTLED(ERROR)
          << "failed to evaluate condition : " << matches_or.status().message();
    } else if (matches_or.value()) {
      INTRINSIC_RT_LOG_THROTTLED(INFO)
          << "free-standing realtime reaction (id " << reaction.id.value()
          << ") evaluates to true";
    }
  }
  return matches_or;
}

// Returns true if the reaction should fire.
// `condition_matches` should indicate the fulfillment of the reaction
// condition.
bool ShouldFire(RealtimeReaction& reaction, const bool condition_matches) {
  return !reaction.fired && condition_matches;
}

// Notifies the non-rt listeners about the triggering of a reaction.
// `reaction_id` refers to the reaction to send.
// `current_action_id` refers to the action that that the reaction is bound to.
// Use std::nullopt for free-standing reactions.
// `next_action_id` refers to the action this reaction switches to, if any.
// Uses the reaction queue in `channels` to send the reaction event.
void SendEvent(const ReactionId reaction_id,
               const std::optional<ActionInstanceId> current_action_id,
               const std::optional<ActionInstanceId> next_action_id,
               RealtimeSessionChannels& channels) {
  // Create and populate a ReactionEvent to send to the bridge.
  ReactionEvent reaction_event = {
      .id = reaction_id,
      .previous_action_id = current_action_id,
      // Next action id becomes the current since we switch to it.
      .current_action_id = next_action_id,
  };

  bool reaction_sent = channels.reaction_queue.writer()->Insert(reaction_event);
  if (!reaction_sent) {
    INTRINSIC_RT_LOG_THROTTLED(WARNING)
        << "Send reaction failed. This does not affect realtime operation, "
           "but clients may miss Reaction callbacks.";
  }
}

}  // namespace

RtclReactionManager::RtclReactionManager(
    RealtimePartManager* part_manager, RealtimeSessionChannels* channels,
    RtclRealtimeSession* current_session,
    FixedVector<std::optional<size_t>, kMaxRealtimeParts>*
        active_action_indices)
    : part_manager_(part_manager),
      channels_(channels),
      current_session_(current_session),
      active_action_indices_(active_action_indices) {}

RealtimeStatusOr<bool> RtclReactionManager::EvaluateReactionAndSendEvent(
    RealtimeReaction& reaction, const RtclActionInterface& action,
    const AggregatedRobotStatus& robot_status,
    const std::optional<ActionInstanceId> from_action_id) {
  if (reaction.from_action_index.has_value() && !from_action_id.has_value()) {
    return InternalError(RealtimeStatus::StrCat(
        "Reaction ", reaction.id.value(), " is associated with action index ",
        *reaction.from_action_index,
        ", but `current_action_id` is not set. This is a bug"));
  }

  INTRINSIC_RT_ASSIGN_OR_RETURN(
      const bool condition_matches,
      ReactionConditionMatches(action, reaction, robot_status));

  // If fire_once is not set:
  // Reset the fired state if the condition does not
  // match (anymore), since we want to fire on every rising edge.
  if (!reaction.fire_once && !condition_matches) {
    reaction.fired = false;
  }

  if (!ShouldFire(reaction, condition_matches)) {
    return false;
  }

  bool rt_reaction = false;
  // Gets overwritten if there is a realtime reaction.
  std::optional<ActionInstanceId> next_action_id;
  if (reaction.target_action_index.has_value()) {
    if (reaction.target_action_index < 0 ||
        reaction.target_action_index >= current_session_->actions.size()) {
      return InvalidArgumentError(
          RealtimeStatus::StrCat("Reaction with ID ", reaction.id.value(),
                                 " attempts to switch to invalid Action index ",
                                 *reaction.target_action_index));
    }

    if (current_session_->actions[*reaction.target_action_index] == nullptr) {
      return InvalidArgumentError(
          RealtimeStatus::StrCat("Reaction with ID ", reaction.id.value(),
                                 " attempts to switch to deleted Action index ",
                                 *reaction.target_action_index));
    }

    next_action_id =
        current_session_->actions[*reaction.target_action_index]->id;

    rt_reaction = true;
  }
  SendEvent(reaction.id, from_action_id, next_action_id, *channels_);

  if (reaction.triggered_signal_id.has_value() &&
      reaction.from_action_index.has_value()) {
    INTRINSIC_RT_ASSIGN_OR_RETURN(
        RtclActionInstance * action_with_triggered_signal,
        GetAction(reaction.from_action_index.value()));
    INTRINSIC_RT_LOG(INFO) << "Real time signal with id "
                           << reaction.triggered_signal_id.value()
                           << " was triggered for action with id: "
                           << action_with_triggered_signal->id.value();
    action_with_triggered_signal->realtime_signal_storage
        ->signal_values[reaction.triggered_signal_id.value()]
        .current_value = true;
  }

  reaction.fired = true;

  return rt_reaction;
}

RealtimeStatus RtclReactionManager::AssignActionToSuitableActionEntry(
    size_t action_index_to_start) {
  if (action_index_to_start >= current_session_->actions.size()) {
    return InternalError("Action index is out of range!");
  }
  if (current_session_ == nullptr) {
    return InternalError("Current session is null!");
  }
  INTRINSIC_RT_ASSIGN_OR_RETURN(const RtclActionInstance* new_action,
                                GetAction(action_index_to_start));

  for (size_t i = 0; i < active_action_indices_->size(); ++i) {
    std::optional<size_t>& active_action_index = (*active_action_indices_)[i];
    if (!active_action_index.has_value()) {
      continue;  // The slot is already in use.
    }
    if (*active_action_index == action_index_to_start) {
      return OkStatus();  // The action is already active.
    }
    INTRINSIC_RT_ASSIGN_OR_RETURN(const auto action,
                                  GetAction(*active_action_index));
    if (icon::ActionPairHasSlotOverlap(*action, *new_action)) {
      // Found an action with a slot overlap. Make sure that all overlapping
      // actions are deactivated before calling
      // AssignActionToSuitableActionEntry().
      return InternalError(
          "Found an already active action with a part slot overlap. This is a "
          "bug.");
    }
  }
  // If `new_action` does not have a slot overlap with any of the active
  // actions, pick the first free index.
  for (size_t i = 0; i < active_action_indices_->size(); ++i) {
    std::optional<size_t>& action_index = (*active_action_indices_)[i];
    if (!action_index.has_value()) {
      action_index = action_index_to_start;
      return OkStatus();
    }
  }

  return InternalError(RealtimeStatus::StrCat(
      "Cannot start another action (requested action ID ",
      current_session_->actions.at(action_index_to_start)->id.value(),
      ") since there is no free slot. This is a bug. The maximum number of "
      "parallel actions is ",
      active_action_indices_->size()));
}

RealtimeStatus RtclReactionManager::StartActions(
    absl::Span<const size_t> action_indices, double speed_override,
    BehaviorOverrideRequest behavior_override_request) {
  // Deactivate all active actions with a slot overlap to the requested actions.
  for (const auto action_to_start_index : action_indices) {
    INTRINSIC_RT_ASSIGN_OR_RETURN(const RtclActionInstance* action_to_start,
                                  GetAction(action_to_start_index));
    for (std::optional<size_t>& active_action_index : *active_action_indices_) {
      if (!active_action_index.has_value()) {
        continue;
      }
      INTRINSIC_RT_ASSIGN_OR_RETURN(RtclActionInstance * active_action,
                                    GetAction(*active_action_index));
      if (ActionPairHasSlotOverlap(*action_to_start, *active_action)) {
        INTRINSIC_RT_LOG(INFO)
            << "Preempting action with ID " << active_action->id.value()
            << " due to part slot overlap with action with ID "
            << action_to_start->id.value();
        DeactivateAction(*active_action);
        active_action_index = std::nullopt;
      }
    }
  }
  ScopedThreadLocalReaction scoped_malloc_reaction(
      icon::MallocGuardReaction::kStoreViolationWithTrace);

  // Now start all requested actions.
  for (const auto action_to_start_index : action_indices) {
    if (action_to_start_index >= current_session_->actions.size()) {
      return InvalidArgumentError(RealtimeStatus::StrCat(
          "Cannot start Action at index ", action_to_start_index,
          ", Session only has ", current_session_->actions.size(), " Actions"));
    }

    INTRINSIC_RT_ASSIGN_OR_RETURN(RtclActionInstance * action_to_start,
                                  GetAction(action_to_start_index));
    if (!ActionSupportsRequestedOverrideBehavior(
            *action_to_start->supported_behavior_overrides_by_enum_value,
            behavior_override_request)) {
      return AbortedError(RealtimeStatus::StrCat(
          "Requested '",
          BehaviorOverrideRequestToRealtimeSafeString(
              behavior_override_request),
          "' not supported by Action at index ", action_to_start_index,
          " with ActionInstanceId ", action_to_start->id.value(), "."));
    }

    RealtimePartManager::SlotMap part_manager_slot_map =
        part_manager_->GetSlotMapForAction(
            action_to_start->visibility_by_slot_id.get());
    RealtimeSlotMap action_slot_map(part_manager_slot_map);
    RealtimeStatus on_enter_status = action_to_start->action->OnEnter(
        {.slot_map = action_slot_map,
         .speed_override = speed_override,
         .requested_behavior_override = behavior_override_request});
    if (icon::GetThreadLocalMallocViolations().num_violations > 0) {
      auto message = RealtimeStatus::StrCat(
          "OnEnter() of action instance ", action_to_start->id.value(),
          " allocated ",
          icon::GetThreadLocalMallocViolations().allocated_bytes.load(),
          " bytes on the heap.");
      INTRINSIC_RT_LOG(ERROR)
          << message << "\nLatest stacktrace:\n"
          << GenerateRtErrorStackTrace(absl::MakeConstSpan(
                 icon::GetThreadLocalMallocViolations()
                     .latest_violation_stack_trace->stack_trace,
                 icon::GetThreadLocalMallocViolations()
                     .latest_violation_stack_trace->num_frames));
      return icon::ResourceExhaustedError(message);
    }
    if (!on_enter_status.ok()) {
      INTRINSIC_RT_LOG(ERROR)
          << "PUBLIC: Session " << current_session_->session_id.value()
          << ": OnEnter() failed while starting Action "
          << action_to_start->id.value()
          << " with status: " << on_enter_status.ToString();
      return on_enter_status;
    }
    // Reset the fired status of all reactions associated with the newly
    // activated action.
    for (RealtimeReaction& reaction : current_session_->reactions) {
      if (reaction.from_action_index == action_to_start_index) {
        reaction.fired = false;
      }
    }

    INTRINSIC_RT_RETURN_IF_ERROR(
        AssignActionToSuitableActionEntry(action_to_start_index));
    INTRINSIC_RT_LOG(INFO) << "Started Action with ID "
                           << action_to_start->id.value();
  }
  return OkStatus();
}

bool RtclReactionManager::DeactivateActionByIndex(
    size_t action_index_to_deactivate) {
  for (auto& index : (*active_action_indices_)) {
    if (!index.has_value() || (*index != action_index_to_deactivate)) {
      continue;
    }
    RtclActionInstance* current_action =
        current_session_->actions.at(index.value());

    if (current_action == nullptr) {
      INTRINSIC_RT_LOG(INFO)
          << "Will not reset streaming IO storage for "
             "previously active Action. The Action has been deleted.";
    } else {
      DeactivateAction(*current_action);
    }
    index = std::nullopt;
    return true;
  }
  return false;
}

void RtclReactionManager::DeactivateAction(
    RtclActionInstance& action_instance) {
  ResetRealtimeStreamingIoStorage(*action_instance.streaming_io_storage);
  INTRINSIC_RT_LOG(INFO) << "Deactivated Action with ID "
                         << action_instance.id.value();
}

RealtimeStatus RtclReactionManager::CheckReactionEffects(
    const FixedVector<size_t, kMaxRealtimeParts>& triggered_reactions) {
  if (ABSL_PREDICT_FALSE(current_session_ == nullptr)) {
    return InternalError(
        "Can't run CheckReactionEffects: Session pointer is null. This is a "
        "programming error in ICON.");
  }

  // All action indices that are requested by the given reactions.
  FixedVector<size_t, kMaxRealtimeParts> requested_actions;

  for (const auto& reaction_index : triggered_reactions) {
    if (ABSL_PREDICT_FALSE(reaction_index >=
                           current_session_->reactions.size())) {
      return OutOfRangeError(RealtimeStatus::StrCat(
          "The reaction index ", reaction_index, " is out of range of ",
          current_session_->reactions.size()));
    }
    auto reaction_event = current_session_->reactions[reaction_index];
    if (reaction_event.target_action_index.has_value()) {
      requested_actions.push_back(*reaction_event.target_action_index);
    }
  }

  INTRINSIC_RT_ASSIGN_OR_RETURN(
      auto conflicting_action_pair,
      GetFirstSlotOverlap(*current_session_, requested_actions));
  if (conflicting_action_pair.has_value()) {
    return ResourceExhaustedError(
        RealtimeStatus::StrCat("The same part slot is requested from multiple "
                               "actions during reaction handling: ",
                               conflicting_action_pair->first, " and ",
                               conflicting_action_pair->second));
  }

  return OkStatus();
}

RealtimeStatus RtclReactionManager::ExecuteRtReactions(
    Time cycle_start_time, double speed_override,
    BehaviorOverrideRequest behavior_override_request,
    absl::Span<const size_t> triggered_reactions) {
  FixedVector<size_t, kMaxRealtimeParts> actions_to_start;
  for (const auto& index : triggered_reactions) {
    const RealtimeReaction& rt_reaction_event =
        current_session_->reactions[index];
    const std::optional<int64_t> current_action_index =
        rt_reaction_event.from_action_index;
    const std::optional<int64_t> next_action_index =
        rt_reaction_event.target_action_index;
    if (rt_reaction_event.stop_associated_action) {
      // This reaction is configured to explicitly stop the active action (not
      // only when it is preempted by another action).
      if (current_action_index.has_value()) {
        DeactivateActionByIndex(current_action_index.value());
      } else {
        INTRINSIC_RT_LOG(WARNING)
            << "Action should be stopped but no action is active.";
      }
    }

    if (next_action_index.has_value()) {
      if (current_session_->actions[next_action_index.value()] == nullptr) {
        return InvalidArgumentError(RealtimeStatus::StrCat(
            "Reaction attempts to switch to deleted Action index ",
            next_action_index.value(), " after initial check."));
      }
      if (absl::c_find(actions_to_start, *next_action_index) ==
          actions_to_start.end()) {
        actions_to_start.push_back(next_action_index.value());
      }
    }
  }

  INTRINSIC_RT_RETURN_IF_ERROR(StartActions(actions_to_start, speed_override,
                                            behavior_override_request));

  for (auto action_index : actions_to_start) {
    INTRINSIC_RT_ASSIGN_OR_RETURN(RtclActionInstance * action_to_start,
                                  GetAction(action_index));
    RealtimePartManager::SlotMap part_manager_slot_map =
        part_manager_->GetSlotMapForAction(
            action_to_start->visibility_by_slot_id.get());
    RealtimeSlotMap action_slot_map(part_manager_slot_map);
    StreamingIoRealtimeAccess streaming_io_access(
        cycle_start_time, *action_to_start->streaming_io_storage);
    RealtimeSignalAccess signal_access(
        *action_to_start->realtime_signal_storage);

    if (RealtimeStatus sense_status = action_to_start->action->Sense(
            {.slot_map = action_slot_map,
             .streaming_io_access = streaming_io_access,
             .signal_access = signal_access,
             .speed_override = speed_override,
             .requested_behavior_override = behavior_override_request});
        !sense_status.ok()) {
      INTRINSIC_RT_LOG(ERROR)
          << "Session " << current_session_->session_id.value()
          << ": Sense() failed for Action " << action_to_start->id.value()
          << " with status: " << sense_status.ToString();
      DeactivateActionByIndex(action_index);
      return sense_status;
    }
  }
  return OkStatus();
}

RealtimeStatus RtclReactionManager::HandleReactions(
    Time cycle_start_time, double speed_override,
    BehaviorOverrideRequest behavior_override_request,
    const AggregatedRobotStatus& robot_status) {
  if (current_session_ == nullptr) {
    return InternalError(
        "Can't run HandleReactions: Session pointer is null. This is a "
        "programming error in ICON.");
  }

  // Check that all parts required by the reactions can be read. This is needed
  // for free-standing reactions, because they can continue even if some parts
  // are faulted.
  for (size_t i = 0; i < current_session_->reactions.size(); ++i) {
    const RealtimeCondition& condition =
        current_session_->reactions[i].condition;
    for (int j = 0; j < condition.required_parts_by_index.size(); ++j) {
      if (!condition.required_parts_by_index[j]) {
        continue;
      }

      INTRINSIC_RT_ASSIGN_OR_RETURN(RealtimeOperationalStatus status,
                                    part_manager_->GetPartState(j));
      if (!(status.state == RealtimeOperationalState::kEnabled ||
            status.state == RealtimeOperationalState::kDisabled)) {
        return FailedPreconditionError(RealtimeStatus::StrCat(
            "Part ", part_manager_->GetPartName(j).value(), " is faulted (",
            status.fault_reason, ") but needed to evaluate reaction id ",
            current_session_->reactions[i].id.value()));
      }
    }
  }

  FixedVector<size_t, kMaxRealtimeParts> triggered_reactions;
  // Check if the given rt reaction already exists and if there is a conflict
  // between reactions.
  auto check_and_add_rt_reaction = [this, &triggered_reactions](
                                       const RealtimeReaction& reaction,
                                       size_t index) -> RealtimeStatus {
    // Check if there is already a triggered reaction with same origin and
    // target action. We accept having the same reaction multiple times if
    // they have the same `stop_associated_action` configuration, but only add
    // the reaction once to `triggered_reactions`. We accept this, because if
    // multiple reactions request the same action switch, there is no conflict
    // and we do not need trigger an error.
    auto it =
        absl::c_find_if(triggered_reactions, [&reaction, this](size_t& index) {
          const auto& candidate = current_session_->reactions[index];
          return candidate.from_action_index == reaction.from_action_index &&
                 candidate.target_action_index == reaction.target_action_index;
        });
    if (it == triggered_reactions.end()) {
      triggered_reactions.push_back(index);
    } else {
      const auto& candidate = current_session_->reactions[*it];
      if (candidate.stop_associated_action != reaction.stop_associated_action) {
        return InvalidArgumentError(
            "Triggered reactions with same source and target action have a "
            "different `stop_associated_action` configuration. This is "
            "ambiguous and thus invalid.");
      }
    }
    return OkStatus();
  };

  EmptyAction noop_action;
  // Evaluate free-standing reactions.
  for (size_t i = 0; i < current_session_->reactions.size(); ++i) {
    RealtimeReaction& reaction = current_session_->reactions[i];
    if (reaction.from_action_index.has_value()) {
      continue;  // Only handle free-standing reactions here.
    }
    INTRINSIC_RT_ASSIGN_OR_RETURN(
        const bool trigger,
        EvaluateReactionAndSendEvent(reaction, noop_action, robot_status,
                                     std::nullopt));
    if (trigger) {
      INTRINSIC_RT_RETURN_IF_ERROR(check_and_add_rt_reaction(reaction, i));
    }
  }

  // Evaluate reactions associated with an action.
  for (auto& active_action_index : (*active_action_indices_)) {
    if (!active_action_index.has_value()) {
      continue;
    }
    if (*active_action_index >= current_session_->actions.size()) {
      return NotFoundError(
          RealtimeStatus::StrCat("No Action at index ", *active_action_index));
    }
    if (current_session_->actions.at(active_action_index.value()) == nullptr) {
      return InternalError(RealtimeStatus::StrCat(
          "Action at index ", *active_action_index, " is nullptr"));
    }

    if (current_session_->actions.at(active_action_index.value())->action ==
        nullptr) {
      return InternalError(RealtimeStatus::StrCat(
          "Action instance at index ", *active_action_index, " is nullptr"));
    }

    RtclActionInterface* active_action =
        current_session_->actions.at(active_action_index.value())->action.get();
    ActionInstanceId active_action_id =
        current_session_->actions.at(*active_action_index)->id;

    // Evaluates all reactions bound to the active action. For
    // those that trigger, the non-RT thread is notified.
    for (size_t i = 0; i < current_session_->reactions.size(); ++i) {
      RealtimeReaction& reaction = current_session_->reactions[i];
      if ((!reaction.from_action_index
                .has_value())  // Free-standing reactions
                               // are already handled above.
          || (active_action_index !=
              reaction.from_action_index))  // Reaction is not associated with
                                            // this action -> skip evaluation.
      {
        continue;
      }
      // Set the previous value if the reaction holds a real-time signal.
      if (reaction.triggered_signal_id.has_value() &&
          reaction.from_action_index.has_value()) {
        INTRINSIC_RT_ASSIGN_OR_RETURN(
            RtclActionInstance * action_with_triggered_signal,
            GetAction(reaction.from_action_index.value()));
        action_with_triggered_signal->realtime_signal_storage
            ->signal_values[reaction.triggered_signal_id.value()]
            .previous_value =
            action_with_triggered_signal->realtime_signal_storage
                ->signal_values[reaction.triggered_signal_id.value()]
                .current_value;
      }
      INTRINSIC_RT_ASSIGN_OR_RETURN(
          const bool trigger,
          EvaluateReactionAndSendEvent(reaction, *active_action, robot_status,
                                       active_action_id));
      if (trigger) {
        INTRINSIC_RT_RETURN_IF_ERROR(check_and_add_rt_reaction(reaction, i));
      }
    }
  }

  INTRINSIC_RT_RETURN_IF_ERROR(CheckReactionEffects(triggered_reactions));

  return ExecuteRtReactions(cycle_start_time, speed_override,
                            behavior_override_request, triggered_reactions);
}
}  // namespace intrinsic::icon
