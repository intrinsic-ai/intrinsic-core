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

#include "intrinsic/icon/control/rtcl_session_manager.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <memory>
#include <optional>
#include <utility>

#include "absl/algorithm/container.h"
#include "absl/base/attributes.h"
#include "absl/container/fixed_array.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "intrinsic/icon/common/id_types.h"
#include "intrinsic/icon/control/behavior_override.h"
#include "intrinsic/icon/control/parts/realtime_log_context.h"
#include "intrinsic/icon/control/reaction.h"
#include "intrinsic/icon/control/realtime_bridge_types.h"
#include "intrinsic/icon/control/realtime_part_manager.h"
#include "intrinsic/icon/control/realtime_session_channels.h"
#include "intrinsic/icon/control/realtime_signal_access.h"
#include "intrinsic/icon/control/realtime_slot_map.h"
#include "intrinsic/icon/control/rtcl_action.h"
#include "intrinsic/icon/control/rtcl_action_instance.h"
#include "intrinsic/icon/control/rtcl_reaction_manager.h"
#include "intrinsic/icon/control/rtcl_realtime_session.h"
#include "intrinsic/icon/control/safety/extern/safety_status.fbs.h"
#include "intrinsic/icon/control/safety/safety_messages.fbs.h"
#include "intrinsic/icon/control/streaming_io_realtime.h"
#include "intrinsic/icon/interprocess/binary_futex.h"
#include "intrinsic/icon/proto/v1/types.pb.h"
#include "intrinsic/icon/utils/clock.h"
#include "intrinsic/icon/utils/log.h"
#include "intrinsic/icon/utils/malloc_guard.h"
#include "intrinsic/icon/utils/realtime_stack_trace.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/icon/utils/realtime_status_macro.h"
#include "intrinsic/icon/utils/realtime_status_or.h"
#include "intrinsic/util/fixed_vector.h"

namespace intrinsic::icon {

using ::intrinsic::icon::BehaviorOverrideRequestToRealtimeSafeString;
using ::intrinsic_proto::icon::v1::BehaviorOverrideRequest;

RealtimeStatusOr<RtclSessionManager> RtclSessionManager::Create(
    SessionId session_id,
    RealtimePartManager& part_manager ABSL_ATTRIBUTE_LIFETIME_BOUND,
    RealtimeSessionChannels& channels ABSL_ATTRIBUTE_LIFETIME_BOUND,
    const absl::flat_hash_set<size_t>& part_indices,
    const RealtimeLogContext& context,
    const intrinsic_fbs::SafetyStatusMessage* safety_status_message) {
  if (part_indices.size() > kMaxRealtimeParts) {
    return FailedPreconditionError(RealtimeStatus::StrCat(
        "part_indices contains more than ", kMaxRealtimeParts, " elements."));
  }
  // Create this in the beginning to make sure that the RtclSessionManager dtor
  // calls CleanupAndNotify in an error case.
  RtclSessionManager manager(session_id, part_manager, channels, part_indices,
                             safety_status_message);

  RealtimeLogContext session_context = context;
  session_context.icon_session_id = session_id.value();
  for (size_t index : part_indices) {
    INTRINSIC_RT_RETURN_IF_ERROR(
        part_manager.SetPartContext(index, session_context));
  }
  return manager;
}

// Creates a new RealtimeSessionData object that uses `channels`. If
// `channels` is non-null, immediately sets the session status buffer to OK.
RtclSessionManager::RtclSessionManager(
    SessionId session_id, RealtimePartManager& part_manager,
    RealtimeSessionChannels& channels,
    const absl::flat_hash_set<size_t>& part_indices,
    const intrinsic_fbs::SafetyStatusMessage* const safety_status_message)
    : session_id_(session_id),
      part_manager_(&part_manager),
      channels_(&channels),
      active_action_indices_(part_manager.GetNumParts()),
      safety_status_message_(safety_status_message),
      part_indices_(part_indices.begin(), part_indices.end()) {
  if (channels_ != nullptr) {
    *channels_->session_status_buffer.GetFreeBuffer() = session_status_;
    channels_->session_status_buffer.CommitFreeBuffer();
  }
}

RtclSessionManager::RtclSessionManager(RtclSessionManager&& other) {
  // Swap all values, so that `other`'s dtor does the cleanup for the channels
  // we used to hold, if any.
  std::swap(session_id_, other.session_id_);
  std::swap(part_manager_, other.part_manager_);
  std::swap(channels_, other.channels_);
  std::swap(current_session_, other.current_session_);
  std::swap(active_action_indices_, other.active_action_indices_);
  std::swap(session_status_, other.session_status_);
  std::swap(safety_status_message_, other.safety_status_message_);
  std::swap(part_indices_, other.part_indices_);
}

RtclSessionManager& RtclSessionManager::operator=(RtclSessionManager&& other) {
  // Swap all values, so that `other`'s dtor does the cleanup for the channels
  // we used to hold, if any.
  std::swap(session_id_, other.session_id_);
  std::swap(part_manager_, other.part_manager_);
  std::swap(channels_, other.channels_);
  std::swap(current_session_, other.current_session_);
  std::swap(active_action_indices_, other.active_action_indices_);
  std::swap(session_status_, other.session_status_);
  std::swap(safety_status_message_, other.safety_status_message_);
  std::swap(part_indices_, other.part_indices_);
  return *this;
}

RtclSessionManager::~RtclSessionManager() { CleanupAndNotify(); }

bool RtclSessionManager::IsActive() const { return channels_ != nullptr; }

SessionId RtclSessionManager::GetSessionId() const { return session_id_; }

void RtclSessionManager::SetErrorCleanupNotify(RealtimeStatus status) {
  // Save the status in the channel, so the non-realtime thread can be notified.
  session_status_ = status;
  if (channels_ != nullptr) {
    *channels_->session_status_buffer.GetFreeBuffer() = session_status_;
    channels_->session_status_buffer.CommitFreeBuffer();
    // If we have an active call from the non-realtime thread, we need to return
    // before nullifying the channels_ pointer. Otherwise the non-realtime
    // thread will be stuck waiting for a response that will never come.
    channels_->install_session_data_bridge.ServiceCall(
        [status](RtclRealtimeSession* new_session) -> RealtimeStatus {
          return status;
        });
  }
  CleanupAndNotify();
}

const RtclActionInstance* RtclSessionManager::CurrentActiveActionTestOnly()
    const {
  if (!IsActive()) {
    return nullptr;
  }
  for (auto index : active_action_indices_) {
    if (index.has_value()) {
      return current_session_->actions.at(*index);
    }
  }
  return nullptr;
}

FixedVector<const RtclActionInstance*, kMaxRealtimeParts>
RtclSessionManager::CurrentActiveActionsTestOnly() const {
  if (!IsActive()) {
    return {};
  }
  FixedVector<const RtclActionInstance*, kMaxRealtimeParts> result;
  for (auto index : active_action_indices_) {
    if (index.has_value()) {
      result.push_back(current_session_->actions.at(*index));
    }
  }
  return result;
}

RealtimeStatus RtclSessionManager::RunSense(
    Time cycle_start_time, double speed_override,
    BehaviorOverrideRequest behavior_override_request) {
  if (current_session_ == nullptr) {
    return InternalError(
        "Can't run Sense: Session pointer is null. This is a programming "
        "error in ICON.");
  }
  // We set the reaction outside of the loop since we bail out on first
  // violation anyway and thus can save some computation time by not
  // reconstructing it again and again.
  ScopedThreadLocalReaction scoped_malloc_reaction(
      icon::MallocGuardReaction::kStoreViolationWithTrace);
  for (std::optional<size_t> active_action_index : active_action_indices_) {
    if (!active_action_index.has_value()) {
      continue;
    }

    if (*active_action_index >= current_session_->actions.size()) {
      return NotFoundError(
          RealtimeStatus::StrCat("No Action at index ", *active_action_index));
    }
    if (current_session_->actions.at(*active_action_index) == nullptr) {
      return InternalError("Can't run Sense: Target action has been deleted.");
    }
    RtclActionInstance& current_action =
        *current_session_->actions.at(*active_action_index);
    StreamingIoRealtimeAccess streaming_io_access(
        cycle_start_time, *current_action.streaming_io_storage);
    RealtimePartManager::SlotMap part_manager_slot_map =
        part_manager_->GetSlotMapForAction(
            current_action.visibility_by_slot_id.get());
    RealtimeSignalAccess signal_access(*current_action.realtime_signal_storage);
    RealtimeSlotMap action_slot_map(part_manager_slot_map);
    if (!ActionSupportsRequestedOverrideBehavior(
            *current_action.supported_behavior_overrides_by_enum_value,
            behavior_override_request)) {
      return AbortedError(RealtimeStatus::StrCat(
          "Requested '",
          BehaviorOverrideRequestToRealtimeSafeString(
              behavior_override_request),
          "' not supported by current action with ActionInstanceId ",
          current_action.id.value(), "."));
    }

    RealtimeStatus sense_status = current_action.action->Sense(
        {.slot_map = action_slot_map,
         .streaming_io_access = streaming_io_access,
         .signal_access = signal_access,
         .speed_override = speed_override,
         .requested_behavior_override = behavior_override_request});
    if (icon::GetThreadLocalMallocViolations().num_violations > 0) {
      auto message = RealtimeStatus::StrCat(
          "Sense() of action instance ", current_action.id.value(),
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
    if (!sense_status.ok()) {
      INTRINSIC_RT_LOG(ERROR)
          << "Session " << session_id_.value() << ": Sense() failed for Action "
          << current_action.id.value()
          << " with status: " << sense_status.ToString();
      return sense_status;
    }
  }
  return OkStatus();
}

RealtimeStatus RtclSessionManager::RunControl(
    double speed_override, BehaviorOverrideRequest behavior_override_request) {
  if (current_session_ == nullptr) {
    return InternalError(
        "Can't run Control: Session pointer is null. This is a programming "
        "error in ICON.");
  }
  ScopedThreadLocalReaction scoped_malloc_reaction(
      icon::MallocGuardReaction::kStoreViolationWithTrace);
  for (const auto active_action_index : active_action_indices_) {
    if (!active_action_index.has_value()) {
      continue;
    }
    if (*active_action_index >= current_session_->actions.size()) {
      return NotFoundError(
          RealtimeStatus::StrCat("No Action at index ", *active_action_index));
    }
    if (current_session_->actions.at(*active_action_index) == nullptr) {
      return InternalError(
          "Can't run Control: Target action has been deleted.");
    }
    RtclActionInstance& current_action =
        *current_session_->actions.at(*active_action_index);
    RealtimePartManager::SlotMap part_manager_slot_map =
        part_manager_->GetSlotMapForAction(
            current_action.visibility_by_slot_id.get());
    RealtimeSlotMap slot_map(part_manager_slot_map);
    for (size_t i = 0; i < current_action.visibility_by_slot_id->size(); ++i) {
      if (current_action.visibility_by_slot_id->at(i)) {
        INTRINSIC_RT_RETURN_IF_ERROR(
            part_manager_->MarkPartAsControlledByAction(i, current_action.id));
      }
    }
    // Check is required, as the `behavior_override_request` can arrive after
    // the action is already started.
    if (!ActionSupportsRequestedOverrideBehavior(
            *current_action.supported_behavior_overrides_by_enum_value,
            behavior_override_request)) {
      return AbortedError(RealtimeStatus::StrCat(
          "Requested '",
          BehaviorOverrideRequestToRealtimeSafeString(
              behavior_override_request),
          "' not supported by current action with ActionInstanceId ",
          current_action.id.value(), "."));
    }

    RealtimeStatus control_status = current_action.action->Control(
        {.slot_map = slot_map,
         .speed_override = speed_override,
         .requested_behavior_override = behavior_override_request});
    if (icon::GetThreadLocalMallocViolations().num_violations > 0) {
      auto message = RealtimeStatus::StrCat(
          "Control() of action instance ", current_action.id.value(),
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
    if (!control_status.ok()) {
      INTRINSIC_RT_LOG(ERROR)
          << "Session " << session_id_.value()
          << ": Control() failed for Action " << current_action.id.value()
          << " with status: " << control_status.ToString();
      return control_status;
    }
  }
  return OkStatus();
}

RealtimeStatus RtclSessionManager::HandleReactions(
    Time cycle_start_time, double speed_override,
    BehaviorOverrideRequest behavior_override_request,
    const AggregatedRobotStatus& robot_status) {
  RtclReactionManager reaction_manager(
      part_manager_, channels_, current_session_, &active_action_indices_);
  return reaction_manager.HandleReactions(cycle_start_time, speed_override,
                                          behavior_override_request,
                                          robot_status);
}

void RtclSessionManager::FinishSessionIfRequested() {
  if (channels_ == nullptr) {
    return;
  }
  if (channels_->finish_requested.Value() == 0) {
    return;
  }
  CleanupAndNotify();
}

RealtimeStatus RtclSessionManager::HandleNewSessionDataImpl(
    RtclRealtimeSession* new_session, double speed_override,
    BehaviorOverrideRequest behavior_override_request) {
  INTRINSIC_RT_RETURN_IF_ERROR(session_status_);

  if (IsSafetyBlockingSessionCreation()) {
    // TODO(b/246248665) Add log statement, since the error below is
    // currently not yet visualized.
    INTRINSIC_RT_LOG_THROTTLED(INFO)
        << "Cannot create a new session with an active safety request.";

    return UnavailableError(
        "Cannot create a new session with an active safety request.");
  }

  if (current_session_ != nullptr) {
    // Maintain "fired" information from old session pointer, if any.
    for (auto& new_reaction : new_session->reactions) {
      for (const auto& old_reaction : current_session_->reactions) {
        if (old_reaction.id == new_reaction.id) {
          new_reaction.fired = old_reaction.fired;
          // Found a match for this reaction, so break from the inner loop
          // and continue to the next new_reaction.
          break;
        }
      }
    }
  }

  current_session_ = new_session;

  // Check whether the current active actions still exist in the new session
  // data.
  for (auto& active_action_index : active_action_indices_) {
    if (active_action_index.has_value()) {
      if (*active_action_index >= current_session_->actions.size()) {
        INTRINSIC_RT_LOG_THROTTLED(ERROR)
            << "New Session data has a smaller Actions array than "
               "before. This indicates an ICON bug, falling back to "
               "safety Action.";
        active_action_index = std::nullopt;
      } else if (current_session_->actions.at(*active_action_index) ==
                 nullptr) {
        INTRINSIC_RT_LOG_THROTTLED(INFO) << "Active Action was deleted, "
                                            "falling back to safety Action.";
        active_action_index = std::nullopt;
      }
    }
  }

  if (current_session_->stop_active_actions) {  // The user wants to clear
                                                // old active actions.
    INTRINSIC_RT_LOG(INFO) << "Stopping all active actions as per request.";
    absl::c_fill(active_action_indices_, std::nullopt);
  }

  INTRINSIC_RT_ASSIGN_OR_RETURN(
      const auto slot_overlap,
      GetFirstSlotOverlap(*current_session_,
                          current_session_->actions_to_start));
  if (slot_overlap.has_value()) {
    return InvalidArgumentError(RealtimeStatus::StrCat(
        "Actions to start with IDs ", slot_overlap->first, " and ",
        slot_overlap->second, " have overlapping slots."));
  }

  RtclReactionManager reaction_manager(
      part_manager_, channels_, current_session_, &active_action_indices_);
  return reaction_manager.StartActions(current_session_->actions_to_start,
                                       speed_override,
                                       behavior_override_request);
}

RealtimeStatus RtclSessionManager::HandleNewSessionData(
    double speed_override, BehaviorOverrideRequest behavior_override_request) {
  RealtimeStatus action_start_status = OkStatus();
  channels_->install_session_data_bridge.ServiceCall(
      [this, &action_start_status, speed_override, behavior_override_request](
          RtclRealtimeSession* new_session) -> RealtimeStatus {
        action_start_status = HandleNewSessionDataImpl(
            new_session, speed_override, behavior_override_request);
        return action_start_status;
      });
  return action_start_status;
}

RealtimeStatus RtclSessionManager::ExecuteCycle(
    intrinsic::Time cycle_start_time, double speed_override,
    const AggregatedRobotStatus& robot_status) {
  FinishSessionIfRequested();

  auto behavior_override_request =
      BehaviorOverrideRequest::BEHAVIOR_OVERRIDE_REQUEST_UNKNOWN;

  if (IsSafetyRequestingToPauseCurrentSession()) {
    // Commands actions to "pause" the robot motion.
    // Session is closed in `RunSense`, or `RunControl` if the action doesn't
    // support the `behavior_override_request`.
    behavior_override_request =
        BehaviorOverrideRequest::BEHAVIOR_OVERRIDE_REQUEST_PAUSE;

    // The speed override is set to 0 as a defensive measure.
    // Actions that support an `behavior_override_request` need to follow the
    // requested behavior over the `speed_override`.
    // Actions not supporting `behavior_override_request` are stopped.
    speed_override = 0.0;
    INTRINSIC_RT_LOG_THROTTLED(INFO)
        << "Safety requested '"
        << BehaviorOverrideRequestToRealtimeSafeString(
               behavior_override_request)
        << "' and set speed_override to " << speed_override << ".";
  }

  if (channels_ == nullptr) {
    // Nothing to do.
    return OkStatus();
  }
  if (RealtimeStatus status =
          HandleNewSessionData(speed_override, behavior_override_request);
      !status.ok()) {
    SetErrorCleanupNotify(status);
    return status;
  }
  if (current_session_ == nullptr) {
    return OkStatus();
  }
  if (RealtimeStatus status =
          RunSense(cycle_start_time, speed_override, behavior_override_request);
      !status.ok()) {
    SetErrorCleanupNotify(status);
    return status;
  }

  if (IsSafetyRequestingToTerminateSession()) {
    RealtimeStatus status = UnavailableError(
        "Terminating session due to an active safety request.");
    SetErrorCleanupNotify(status);
    return status;
  }

  if (RealtimeStatus status =
          HandleReactions(cycle_start_time, speed_override,
                          behavior_override_request, robot_status);
      !status.ok()) {
    SetErrorCleanupNotify(status);
    return status;
  }
  if (RealtimeStatus status =
          RunControl(speed_override, behavior_override_request);
      !status.ok()) {
    SetErrorCleanupNotify(status);
    return status;
  }

  return OkStatus();
}

void RtclSessionManager::CleanupAndNotify() {
  // Clear current_session so we don't attempt to execute the Session's Actions
  // in subsequent ticks, but hold on to the finish_done flag and set it after
  // clearing our pointers to let the non-RT thread know this Session is over.
  BinaryFutex* finish_done = nullptr;

  if (part_manager_ != nullptr) {
    for (size_t part_index : part_indices_) {
      RealtimeStatus status =
          part_manager_->SetPartContext(part_index, RealtimeLogContext{});
      if (!status.ok()) {
        INTRINSIC_RT_LOG(ERROR) << "Reset context for part " << part_index
                                << " failed: " << status.message();
      }
      // Only unmark parts if `current_session_` is not null. If there is no
      // session, we don't need to unmark parts. `current_session_` is set to
      // null later in the function and this avoids unmarking the parts over and
      // over.
      if (current_session_) {
        // When we're done with the session, we need to unmark all parts that
        // were marked as controlled by the session. This especially important
        // in case the session is aborted before all `ApplyCommand()` were
        // called this cycle.
        status = part_manager_->UnmarkPartAsControlledByAction(part_index);
        if (!status.ok()) {
          INTRINSIC_RT_LOG_THROTTLED(ERROR) << "Unmarking part " << part_index
                                            << " failed: " << status.message();
        }
      }
    }
  }

  if (channels_ != nullptr) {
    channels_->install_session_data_bridge.Close();
    // If we have an active call from the non-realtime thread, we need to return
    // before nullifying the channels_ pointer. Otherwise the non-realtime
    // thread will be stuck waiting for a response that will never come.
    channels_->install_session_data_bridge.ServiceCall(
        [](RtclRealtimeSession* new_session) -> RealtimeStatus {
          return AbortedError("The session was aborted.");
        });
    finish_done = &channels_->finish_done;
  }

  current_session_ = nullptr;
  absl::c_fill(active_action_indices_, std::nullopt);
  channels_ = nullptr;
  if (finish_done != nullptr) {
    if (auto status = finish_done->Post(); !status.ok()) {
      INTRINSIC_RT_LOG(ERROR)
          << "Failed to post finish_done: " << status.message();
    }
  }
}

bool RtclSessionManager::IsSafetyBlockingSessionCreation() const {
  return IsSafetyRequestingToTerminateSession();
}

bool RtclSessionManager::IsSafetyRequestingToTerminateSession() const {
  if (safety_status_message_ == nullptr) return false;

  return (safety_status_message_->requested_behavior() ==
              intrinsic_fbs::RequestedBehavior::SAFE_STOP_1_TIME_MONITORED ||
          safety_status_message_->requested_behavior() ==
              intrinsic_fbs::RequestedBehavior::SAFE_STOP_0);
}

bool RtclSessionManager::IsSafetyRequestingToPauseCurrentSession() const {
  if (safety_status_message_ == nullptr) return false;
  // Currently SS2 is understood as "resumable", whereas SS1 requires user input
  // for recovery.
  return (safety_status_message_->requested_behavior() ==
              intrinsic_fbs::RequestedBehavior::SAFE_STOP_2_TIME_MONITORED ||
          safety_status_message_->requested_behavior() ==
              intrinsic_fbs::RequestedBehavior::PAUSE);
}

}  // namespace intrinsic::icon
