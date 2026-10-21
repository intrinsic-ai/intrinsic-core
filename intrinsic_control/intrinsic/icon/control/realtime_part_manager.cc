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

#include "intrinsic/icon/control/realtime_part_manager.h"

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/cleanup/cleanup.h"
#include "absl/container/fixed_array.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "intrinsic/icon/cc_client/condition.h"
#include "intrinsic/icon/cc_client/operational_status.h"
#include "intrinsic/icon/common/builtins.h"
#include "intrinsic/icon/common/id_types.h"
#include "intrinsic/icon/common/part_properties.h"
#include "intrinsic/icon/control/initialized_async_buffer.h"
#include "intrinsic/icon/control/parts/feature_interface_registry.h"
#include "intrinsic/icon/control/parts/part_property_registry.h"
#include "intrinsic/icon/control/parts/realtime_log_context.h"
#include "intrinsic/icon/control/parts/realtime_part_interface.h"
#include "intrinsic/icon/control/parts/realtime_part_property_access.h"
#include "intrinsic/icon/control/realtime_bridge_types.h"
#include "intrinsic/icon/control/realtime_operational_status.h"
#include "intrinsic/icon/control/realtime_part_status_helpers.h"
#include "intrinsic/icon/control/realtime_signal_access.h"
#include "intrinsic/icon/control/realtime_slot_map.h"
#include "intrinsic/icon/control/realtime_state_manager_interface.h"
#include "intrinsic/icon/control/rtcl_action_instance.h"
#include "intrinsic/icon/control/rtcl_initialization.h"
#include "intrinsic/icon/control/safety/safety_messages.fbs.h"
#include "intrinsic/icon/control/slot_types.h"
#include "intrinsic/icon/control/streaming_io_realtime.h"
#include "intrinsic/icon/control/streaming_io_storage.h"
#include "intrinsic/icon/proto/v1/types.pb.h"
#include "intrinsic/icon/utils/async_buffer.h"
#include "intrinsic/icon/utils/clock.h"
#include "intrinsic/icon/utils/clock_base.h"
#include "intrinsic/icon/utils/current_cycle.h"
#include "intrinsic/icon/utils/log.h"
#include "intrinsic/icon/utils/malloc_guard.h"
#include "intrinsic/icon/utils/realtime_stack_trace.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/icon/utils/realtime_status_macro.h"
#include "intrinsic/icon/utils/realtime_status_or.h"
#include "intrinsic/platform/common/buffers/realtime_write_queue.h"
#include "intrinsic/util/fixed_vector.h"

namespace intrinsic::icon {

using ::intrinsic_fbs::SafetyStatusMessage;
using ::intrinsic_proto::icon::v1::BehaviorOverrideRequest;

namespace {

RealtimePartManager::AllPartProperties MakePartPropertyValueStorage(
    const intrinsic::FixedVector<std::vector<PartPropertyInitialData>,
                                 kMaxRealtimeParts>&
        part_property_data_in_index_order) {
  intrinsic::FixedVector<absl::FixedArray<PartPropertyValue>, kMaxRealtimeParts>
      ret;
  for (const std::vector<PartPropertyInitialData>& property_data_for_part :
       part_property_data_in_index_order) {
    std::vector<PartPropertyValue> values;
    values.reserve(property_data_for_part.size());
    absl::c_transform(
        property_data_for_part, std::back_inserter(values),
        [](const PartPropertyInitialData& initial_data) -> PartPropertyValue {
          return initial_data.initial_value;
        });
    ret.emplace_back(
        absl::FixedArray<PartPropertyValue>{values.begin(), values.end()});
  }
  return {.properties = std::move(ret)};
}

// The safety action is supposed to bring and keep the robot into a safe
// state regardless of the `speed_override` and `requested_behavior_override`.
// The constants below are safeguards.
// See rtcl_session_manager.cc for the handling of the behavior_override if we
// require a behavior different than "pause".
//
// The reason to set kSafetySpeedOverride is to provide the safety action with
// as much bandwith as possible to stop, even though it is supposed to ignore
// the paramter.
static constexpr double kSafetySpeedOverride = 1.0;
// LINT.IfChange
static constexpr BehaviorOverrideRequest kSafetyBehaviorOverride =
    BehaviorOverrideRequest::BEHAVIOR_OVERRIDE_REQUEST_PAUSE;
// LINT.ThenChange(
// //intrinsic_control/intrinsic/icon/control/behavior_override.h
// )

}  // namespace

// RealtimePartManager::SlotMap methods

RealtimePartManager::SlotMap::SlotMap(
    const absl::FixedArray<bool>* part_visibility_by_index,
    intrinsic::FixedVector<PartInfo, kMaxRealtimeParts>& part_infos)
    : part_visibility_by_index_(*part_visibility_by_index),
      part_infos_(part_infos) {}

FeatureInterfaceRegistry*
RealtimePartManager::SlotMap::GetMutableRegistryForSlot(
    RealtimeSlotId slot_id) {
  if (slot_id.value() >= part_visibility_by_index_.size() ||
      !part_visibility_by_index_[slot_id.value()] ||
      slot_id.value() >= part_infos_.size()) {
    return nullptr;
  }
  return &part_infos_.at(slot_id.value()).part.GetFeatureInterfaces();
}

const FeatureInterfaceRegistry*
RealtimePartManager::SlotMap::GetRegistryForSlot(RealtimeSlotId slot_id) const {
  if (slot_id.value() >= part_visibility_by_index_.size() ||
      !part_visibility_by_index_[slot_id.value()] ||
      slot_id.value() >= part_infos_.size()) {
    return nullptr;
  }
  return &part_infos_.at(slot_id.value()).part.GetFeatureInterfaces();
}

// static
intrinsic::FixedVector<RealtimePartManager::PartInfo, kMaxRealtimeParts>
RealtimePartManager::MakePartInfoArray(
    intrinsic::FixedVector<PartAndSafetyAction, kMaxRealtimeParts>
        parts_and_safety_actions) {
  intrinsic::FixedVector<PartInfo, kMaxRealtimeParts> part_info_vector;
  for (PartAndSafetyAction& part_and_safety_action : parts_and_safety_actions) {
    part_info_vector.emplace_back(PartInfo{
        .part = std::move(part_and_safety_action.part),
        .safety_action_io_storage =
            std::move(part_and_safety_action.safety_action_io_storage),
        .safety_action = std::move(part_and_safety_action.safety_action),
        .controlled_by = std::nullopt});
  }
  return part_info_vector;
}

RealtimePartManager::RealtimePartManager(
    intrinsic::FixedVector<PartAndSafetyAction, kMaxRealtimeParts>
        parts_and_safety_actions,
    const intrinsic::FixedVector<std::vector<PartPropertyInitialData>,
                                 kMaxRealtimeParts>&
        part_property_data_in_index_order)
    : part_states_buffer_(parts_and_safety_actions.size(),
                          OperationalState::kDisabled),
      part_infos_(MakePartInfoArray(std::move(parts_and_safety_actions))),
      part_properties_rt_to_non_rt_buffer_(
          MakePartPropertyValueStorage(part_property_data_in_index_order)),
      part_properties_non_rt_to_rt_buffer_(
          MakePartPropertyValueStorage(part_property_data_in_index_order)) {}

RealtimePartManager::SlotMap RealtimePartManager::GetSlotMapForAction(
    const absl::FixedArray<bool>* part_visibility_by_index) {
  return SlotMap(part_visibility_by_index, part_infos_);
}

RealtimeStatusOr<AggregatedRobotStatus> RealtimePartManager::BeginCycle(
    intrinsic::Time cycle_start) {
  if (cycle_start_.has_value()) {
    RealtimeStatus error =
        FailedPreconditionError("Must call FinishCycle() before BeginCycle().");
    return error;
  }
  cycle_start_ = cycle_start;

  // Get the active part properties buffers at the beginning of the cycle, for
  // all parts. This makes sure that all parts receive part property values from
  // the same point in time.
  part_properties_non_rt_to_rt_buffer_.GetActiveBuffer(
      &current_cycle_part_properties_read_);

  current_cycle_part_properties_write_ =
      part_properties_rt_to_non_rt_buffer_.GetFreeBuffer();
  current_cycle_part_properties_write_->timestamp_control =
      absl::Nanoseconds(intrinsic::toNSec<uint64_t>(*cycle_start_));
  absl::Time wall_time = absl::Now();
  // Add a reasonably close wall-time to the part_statuses and part properties
  // to enable easy filtering/ordering of entries e.g. by the datalogger. More
  // information in b/207633644.
  current_cycle_part_properties_write_->timestamp_wall = wall_time;

  FixedVector<OperationalState, kMaxRealtimeParts>* part_states =
      part_states_buffer_.GetFreeBuffer();
  part_states->assign(part_infos_.size(), OperationalState::kFaulted);
  absl::Cleanup cleanup = [&] { part_states_buffer_.CommitFreeBuffer(); };

  current_robot_status_ = {
      .wall_time = current_cycle_part_properties_write_->timestamp_wall,
      .timestamp_control =
          current_cycle_part_properties_write_->timestamp_control,
      .cycle = Cycle::GetCurrentCycle(),
      .robot_status = {.safety_status = current_safety_status_}};
  for (size_t part_index = 0; part_index < part_infos_.size(); ++part_index) {
    PartInfo& part_info = part_infos_.at(part_index);

    part_info.controlled_by = std::nullopt;

    {
      RealtimePartPropertyAccess property_access(
          /*non_rt_to_rt_values=*/current_cycle_part_properties_read_
              ->properties.at(part_index),
          /*rt_to_non_rt_values=*/current_cycle_part_properties_write_
              ->properties.at(part_index));
      RealtimeStatus status =
          part_info.part.ReadStatus(RealtimePartInterface::ReadStatusParameters{
              .part_properties = property_access,
              .safety_status = current_safety_status_});
      // ExtractRealtimePartStatus should be called during this function after
      // ReadStatus, even if ReadStatus fails.
      INTRINSIC_RT_ASSIGN_OR_RETURN(RealtimeOperationalStatus part_status,
                                    part_info.part.GetOperationalStatus());
      (*part_states)[part_index] = ToOperationalState(part_status.state);
      current_robot_status_->robot_status.part_statuses.push_back(
          ExtractRealtimePartStatus(
              part_info.part.GetFeatureInterfaces(),
              // toNsec gives a duration since the timeslicer clock epoch, then
              // we convert to absl::Duration to help us towards transitioning
              // to absl::Time types.
              absl::Nanoseconds(toNSec<int64_t>(*cycle_start_)), part_status));
      if (!status.ok()) {
        return status;
      }
    }
  }

  // We need to create a copy that can be moved by copy-elision. Otherwise, it
  // won't compile since:
  // * RealtimeStatusOr requires an rvalue for non-trivially-constructible
  // types.
  // * AggregatedRobotStatus is *not* trivially-constructible (because it has a
  // FixedVector member).
  //
  // Also:
  // * We do not want to std::move() out of robot_status here, because we need
  // it in FinishCycle.
  // * We do know the AggregatedRobotStatus copy constructor is realtime safe
  // (it only uses stack memory), so we can trick the check in RealtimeStatusOr
  // by creating a copy, then returning that (copy elision turns it into an
  // rvalue).
  return AggregatedRobotStatus{current_robot_status_->robot_status};
}

RealtimeStatus RealtimePartManager::MarkPartAsControlledByAction(
    size_t part_index, ActionInstanceId action_id) {
  if (part_index >= part_infos_.size()) {
    return InvalidArgumentError(RealtimeStatus::StrCat(
        "Part index ", part_index, " is invalid. There are only ",
        part_infos_.size(), " Parts."));
  }
  PartInfo& part_info = part_infos_[part_index];
  if (part_info.controlled_by.has_value()) {
    return FailedPreconditionError(
        RealtimeStatus::StrCat("Part '", part_info.part.GetName(),
                               "' is already controlled by action ",
                               part_info.controlled_by->value()));
  }
  part_info.controlled_by = action_id;
  part_info.safety_action_active = false;
  return OkStatus();
}

RealtimeStatus RealtimePartManager::UnmarkPartAsControlledByAction(
    size_t part_index) {
  if (part_index >= part_infos_.size()) {
    return InvalidArgumentError(RealtimeStatus::StrCat(
        "Part index ", part_index, " is invalid. There are only ",
        part_infos_.size(), " Parts."));
  }
  PartInfo& part_info = part_infos_[part_index];
  part_info.controlled_by = std::nullopt;
  return OkStatus();
}

RealtimeStatus RealtimePartManager::SetPartContext(
    size_t part_index, const RealtimeLogContext& context) {
  if (part_index >= part_infos_.size()) {
    return OutOfRangeError(RealtimeStatus::StrCat(
        "Part index ", part_index, " is invalid. There are only ",
        part_infos_.size(), " Parts."));
  }
  part_infos_[part_index].context = context;
  return OkStatus();
}

RealtimeStatus RealtimePartManager::EnterSafetyActionIfRequired(
    const RealtimeSlotMap& slot_map, PartInfo& part_info) {
  if (part_info.controlled_by.has_value()) {
    // Nothing to do here, a session has already marked this part as
    // controlled by an action.
    return OkStatus();
  }

  // Early return if the safety action is already active.
  if (part_info.safety_action_active) {
    return OkStatus();
  } else {
    // Print a log if the safety action is not active.
    INTRINSIC_RT_LOG(INFO) << "Part '" << part_info.part.GetName()
                           << "' is enabled, but not controlled by any "
                              "Action this cycle. Entering Safety Action";
  }

  // Reset the safety action's streaming IO storage so that it does not
  // receive stale streaming input values. Safety actions don't usually use
  // streaming I/O (there's no way for the client to send or receive streaming
  // I/Os to/from a safety action), but this bit of housekeeping is consistent
  // with the way other actions are invoked.
  ResetRealtimeStreamingIoStorage(
      *part_info.safety_action.streaming_io_storage);
  ScopedThreadLocalReaction scoped_malloc_reaction(
      icon::MallocGuardReaction::kStoreViolationWithTrace);

  // Call the safety action with "safe" `speed_override` and
  // `requested_behavior_override`.
  // The safety action is supposed to bring and keep the robot into a safe
  // state, so no special override needs to be defined.
  // LINT.IfChange
  RealtimeStatus on_enter_status = part_info.safety_action.action->OnEnter(
      {.slot_map = slot_map,
       .speed_override = kSafetySpeedOverride,
       .requested_behavior_override = kSafetyBehaviorOverride});
  // LINT.ThenChange(
  // //intrinsic_control/intrinsic/icon/control/behavior_override.h
  // )
  if (icon::GetThreadLocalMallocViolations().num_violations > 0) {
    auto message = RealtimeStatus::StrCat(
        "OnEnter() of safety action instance ",
        part_info.safety_action.id.value(), " allocated ",
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
  part_info.safety_action_active = on_enter_status.ok();
  if (!on_enter_status.ok()) {
    INTRINSIC_RT_LOG(ERROR)
        << "Failed to call OnEnter() on safety Action for Part '"
        << part_info.part.GetName() << "': " << on_enter_status.ToString();
  }
  return on_enter_status;
}

RealtimeStatusOr<RealtimePartManager::SafetyActionStatus>
RealtimePartManager::RunSafetyActionIfRequired(PartInfo& part_info) {
  SlotMap slot_map_internal =
      GetSlotMapForAction(part_info.safety_action.visibility_by_slot_id.get());
  RealtimeSlotMap slot_map(slot_map_internal);
  INTRINSIC_RT_RETURN_IF_ERROR(
      EnterSafetyActionIfRequired(slot_map, part_info));
  if (!part_info.safety_action_active) {
    return SafetyActionStatus::kNotRequired;
  }
  {
    StreamingIoRealtimeAccess streaming_io_access(
        *cycle_start_, *part_info.safety_action.streaming_io_storage);
    RealtimeSignalAccess rt_signal_access(
        *part_info.safety_action.realtime_signal_storage);
    // Call the safety action with "safe" `speed_override` and
    // `requested_behavior_override`.
    // The safety action is supposed to bring and keep the robot into a safe
    // state, so no special override needs to be defined.
    // LINT.IfChange
    INTRINSIC_RT_RETURN_IF_ERROR(part_info.safety_action.action->Sense(
        {.slot_map = slot_map,
         .streaming_io_access = streaming_io_access,
         .signal_access = rt_signal_access,
         .speed_override = kSafetySpeedOverride,
         .requested_behavior_override = kSafetyBehaviorOverride}));
    // LINT.ThenChange(
    // //intrinsic_control/intrinsic/icon/control/behavior_override.h
    // )
  }
  // Call the safety action with "safe" `speed_override` and
  // `requested_behavior_override`.
  // The safety action is supposed to bring and keep the robot into a safe
  // state, so no special override needs to be defined.
  // LINT.IfChange
  INTRINSIC_RT_RETURN_IF_ERROR(part_info.safety_action.action->Control(
      {.slot_map = slot_map,
       .speed_override = kSafetySpeedOverride,
       .requested_behavior_override = kSafetyBehaviorOverride}));
  // LINT.ThenChange(
  // //intrinsic_control/intrinsic/icon/control/behavior_override.h
  // )

  INTRINSIC_RT_ASSIGN_OR_RETURN(
      StateVariableValue is_done,
      part_info.safety_action.action->GetStateVariable(kIsDone));
  if (!std::holds_alternative<bool>(is_done)) {
    return InternalError(RealtimeStatus::StrCat(
        "State variable ", kIsDone, " of safety action for part ",
        part_info.part.GetName(), " is not a bool"));
  }
  if (std::get<bool>(is_done)) {
    return SafetyActionStatus::kDone;
  }
  return SafetyActionStatus::kRunning;
}

RealtimeStatus RealtimePartManager::FinishCycle(
    RealtimeStateManagerInterface& state_manager) {
  absl::Cleanup clear_cycle_start([this]() { cycle_start_ = std::nullopt; });

  if (!cycle_start_.has_value()) {
    RealtimeStatus error =
        FailedPreconditionError("Must call BeginCycle() before FinishCycle().");
    return error;
  }

  if (!current_robot_status_.has_value()) {
    return InternalError("The current robot status is not set. This is a bug!");
  }

  for (size_t part_index = 0; part_index < part_infos_.size(); ++part_index) {
    const auto& part_info = part_infos_.at(part_index);

    RealtimeLogContext context = part_info.context;
    if (part_info.controlled_by.has_value()) {
      context.icon_action_id = (*part_info.controlled_by).value();
    }
    current_robot_status_->part_contexts.push_back(context);
  }

  // Write the robot status to both the queue and the buffer.
  if (!robot_status_queue_.Writer().Write(*current_robot_status_)) {
    INTRINSIC_RT_LOG_THROTTLED(WARNING)
        << "Failed to push Part status into queue. "
           "Is the non-realtime reader too slow?";
  }
  *current_robot_status_buffer_.GetFreeBuffer() = *current_robot_status_;
  current_robot_status_buffer_.CommitFreeBuffer();
  // We do not need the robot status anymore in this cycle. Reset so that no old
  // robot status can be used in the next cycle.
  current_robot_status_ = std::nullopt;

  bool all_active_safety_actions_done = true;
  // Throughout this function, we do NOT use INTRINSIC_RT_RETURN_IF_ERROR(), in
  // order to try and leave the system in as healthy a state as possible:
  RealtimeStatus action_status = OkStatus();
  for (PartInfo& part_info : part_infos_) {
    INTRINSIC_RT_ASSIGN_OR_RETURN(RealtimeOperationalStatus part_status,
                                  part_info.part.GetOperationalStatus());
    OperationalState part_state = ToOperationalState(part_status.state);
    if (part_state != OperationalState::kEnabled) {
      // Don't run the safety Action, since the Part is not enabled.
      // Also mark the safety Action as inactive, so we call its OnEnter()
      // method when the Part does become active again.
      part_info.safety_action_active = false;
      part_info.controlled_by = std::nullopt;
      continue;
    }

    RealtimeStatusOr<SafetyActionStatus> safety_action_status =
        RunSafetyActionIfRequired(part_info);
    if (!safety_action_status.ok()) {
      action_status = safety_action_status.status();
      // Continue to the other parts to try and run their safety actions (if
      // applicable).
      INTRINSIC_RT_LOG(ERROR)
          << "Failed to invoke safety Action for Part '"
          << part_info.part.GetName() << "':" << action_status.ToString();
      all_active_safety_actions_done = false;
      continue;
    }
    if (safety_action_status.value() == SafetyActionStatus::kNotRequired ||
        safety_action_status.value() == SafetyActionStatus::kDone) {
      continue;
    }
    all_active_safety_actions_done = false;
  }
  // We need to wait with further transitions until the all current safety
  // actions are done.
  state_manager.PauseTransitions(!all_active_safety_actions_done);

  RealtimeStatus part_error_status = OkStatus();
  if (!action_status.ok()) {
    INTRINSIC_RT_LOG(ERROR)
        << "Safety Action for one or more Parts failed (see "
           "previous logs).";
  }

  // If everything went well above, just call ApplyCommand() on each Part.
  // If there was a problem with any of the safety Actions, attempt to shut
  // down all Parts, and also call ApplyCommand() to make sure the Parts have
  // a chance to react.
  for (size_t part_index = 0; part_index < part_infos_.size(); ++part_index) {
    auto& part_info = part_infos_.at(part_index);
    RealtimePartPropertyAccess property_access(
        current_cycle_part_properties_read_->properties.at(part_index),
        current_cycle_part_properties_write_->properties.at(part_index));
    part_error_status = OverwriteIfError(
        part_error_status, part_info.part.ApplyCommand(
                               RealtimePartInterface::ApplyCommandParameters{
                                   .part_properties = property_access,
                                   .safety_status = current_safety_status_}));
    if (!part_error_status.ok()) {
      INTRINSIC_RT_LOG(ERROR)
          << "Failed to call ApplyCommand on Part '" << part_info.part.GetName()
          << "': " << part_error_status.ToString();
    }
  }

  // Finalize the AsyncBuffer for part properties before the possible early
  // bailout
  part_properties_rt_to_non_rt_buffer_.CommitFreeBuffer();
  // Reset cached part property pointers – we should not refer to these until
  // the next call to BeginCycle(), where they're set to meaningful values
  // again.
  current_cycle_part_properties_read_ = nullptr;
  current_cycle_part_properties_write_ = nullptr;

  if (!action_status.ok()) {
    if (!part_error_status.ok()) {
      return InternalError(
          "Safety Action for one or more Parts failed AND calling "
          "ApplyCommand() on one or more Parts failed. Check ICON logs.");
    }
    return InternalError(
        "Safety Action for one or more Parts failed. Check ICON logs.");
  }
  return part_error_status;
}

RealtimeWriteQueue<PublishOutput>::NonRtReader&
RealtimePartManager::RobotStatusReader() {
  return robot_status_queue_.Reader();
}

InitializedAsyncBuffer<PublishOutput>&
RealtimePartManager::CurrentRobotStatusBuffer() {
  return current_robot_status_buffer_;
}

AsyncBuffer<RealtimePartManager::AllPartProperties>&
RealtimePartManager::PartPropertiesRtToNonRtBuffer() {
  return part_properties_rt_to_non_rt_buffer_;
}

AsyncBuffer<RealtimePartManager::AllPartProperties>&
RealtimePartManager::PartPropertiesNonRtToRtBuffer() {
  return part_properties_non_rt_to_rt_buffer_;
}

RealtimeStatusOr<RealtimeOperationalStatus> RealtimePartManager::GetPartState(
    size_t part_index) const {
  if (part_index >= part_infos_.size()) {
    return OutOfRangeError(RealtimeStatus::StrCat("Part index ", part_index,
                                                  " is out of range [0, ",
                                                  part_infos_.size(), ")"));
  }
  return part_infos_[part_index].part.GetOperationalStatus();
}

size_t RealtimePartManager::GetNumParts() const { return part_infos_.size(); }

std::optional<absl::string_view> RealtimePartManager::GetPartName(
    size_t part_index) const {
  if (part_index >= part_infos_.size()) {
    return std::nullopt;
  }
  return part_infos_[part_index].part.GetName();
}

RealtimeStatusOr<HardwareGroupSet> RealtimePartManager::GetHardwareDependencies(
    size_t part_index) const {
  if (part_index >= part_infos_.size()) {
    return OutOfRangeError(RealtimeStatus::StrCat("Part index ", part_index,
                                                  " is out of range [0, ",
                                                  part_infos_.size(), "]"));
  }
  return part_infos_[part_index].part.GetHardwareDependencies();
}

RealtimeStatus RealtimePartManager::CheckUnfinishedSafetyAction(
    size_t part_index) const {
  const auto& part_info = part_infos_[part_index];
  if (part_info.safety_action_active) {
    INTRINSIC_RT_ASSIGN_OR_RETURN(
        StateVariableValue is_done,
        part_info.safety_action.action->GetStateVariable(kIsDone));
    if (!std::holds_alternative<bool>(is_done)) {
      return InternalError(RealtimeStatus::StrCat(
          "State variable ", kIsDone, " of safety action for part ",
          part_info.part.GetName(), " is not a bool"));
    }
    if (!std::get<bool>(is_done)) {
      return UnavailableError(
          RealtimeStatus::StrCat("Part '", part_info.part.GetName(),
                                 " is not done running its safety action."));
    }
  }

  return OkStatus();
}

void RealtimePartManager::SetSafetyStatus(
    const SafetyStatusMessage& safety_status) {
  current_safety_status_.enable_button_status =
      safety_status.enable_button_status();
  current_safety_status_.estop_button_status =
      safety_status.estop_button_status();
  current_safety_status_.mode_of_safe_operation =
      safety_status.mode_of_safe_operation();
  current_safety_status_.requested_behavior =
      safety_status.requested_behavior();
}

}  // namespace intrinsic::icon
