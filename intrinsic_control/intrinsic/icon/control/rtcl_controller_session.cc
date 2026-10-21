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

#include "intrinsic/icon/control/rtcl_controller_session.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/cleanup/cleanup.h"
#include "absl/container/fixed_array.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/functional/bind_front.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "intrinsic/icon/common/id_types.h"
#include "intrinsic/icon/common/part_group.h"
#include "intrinsic/icon/common/slot_part_map.h"
#include "intrinsic/icon/common/topic_names.h"
#include "intrinsic/icon/control/action_factory_context.h"
#include "intrinsic/icon/control/behavior_override.h"
#include "intrinsic/icon/control/initialized_async_buffer.h"
#include "intrinsic/icon/control/logging_mode.h"
#include "intrinsic/icon/control/parts/realtime_log_context.h"
#include "intrinsic/icon/control/reaction.h"
#include "intrinsic/icon/control/realtime_bridge_types.h"
#include "intrinsic/icon/control/realtime_condition.h"
#include "intrinsic/icon/control/realtime_signal_storage.h"
#include "intrinsic/icon/control/realtime_signal_types.h"
#include "intrinsic/icon/control/rtcl_action.h"
#include "intrinsic/icon/control/rtcl_action_factory_registry.h"
#include "intrinsic/icon/control/rtcl_action_instance.h"
#include "intrinsic/icon/control/rtcl_realtime_session.h"
#include "intrinsic/icon/control/rtcl_session_bridge_interface.h"
#include "intrinsic/icon/control/slot_types.h"
#include "intrinsic/icon/control/streaming_io_helpers.h"
#include "intrinsic/icon/control/streaming_io_storage.h"
#include "intrinsic/icon/proto/joint_space.pb.h"
#include "intrinsic/icon/proto/joint_trajectory_conversion.h"
#include "intrinsic/icon/proto/streaming_output.pb.h"
#include "intrinsic/icon/proto/v1/types.pb.h"
#include "intrinsic/icon/server/session_interface.h"
#include "intrinsic/icon/utils/async_buffer.h"
#include "intrinsic/icon/utils/fixed_string.h"
#include "intrinsic/icon/utils/realtime_guard.h"
#include "intrinsic/kinematics/types/joint_trajectories.h"
#include "intrinsic/logging/data_logger_client.h"
#include "intrinsic/logging/proto/log_item.pb.h"
#include "intrinsic/logging/utils/logger_labels.h"
#include "intrinsic/platform/pubsub/publisher.h"
#include "intrinsic/platform/pubsub/pubsub.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/thread/rt_thread.h"
#include "intrinsic/util/thread/thread.h"
#include "intrinsic/util/thread/thread_options.h"

namespace intrinsic::icon {

namespace {

using ::intrinsic_proto::icon::v1::BehaviorOverrideRequest;

absl::StatusOr<absl::flat_hash_map<std::string, SlotInfo>> GetSlotInfoMap(
    const absl::flat_hash_map<std::string, size_t>& part_names_to_index,
    const absl::flat_hash_map<
        std::string, intrinsic_proto::icon::v1::PartConfig>& part_config_map,
    const SlotPartMap& slot_part_map) {
  absl::flat_hash_map<std::string, SlotInfo> slot_info_map;
  for (const auto& [slot_name, part_name] : slot_part_map) {
    auto [slot_info_it, inserted] = slot_info_map.try_emplace(slot_name);
    if (!inserted) {
      return absl::FailedPreconditionError(
          absl::StrCat("Encountered duplicate Slot name '", slot_name, "'"));
    }
    if (auto part_index_it = part_names_to_index.find(part_name);
        part_index_it == part_names_to_index.end()) {
      return absl::NotFoundError(
          absl::StrCat("Failed to look up RealtimeSlotId for Slot '", slot_name,
                       "' (which is using Part '", part_name, "'"));
    } else {
      // Use the index of a Part in the realtime thread as RealtimeSlotId. The
      // realtime thread knows this and sets its RealtimeSlotId values
      // accordingly.
      slot_info_it->second.slot_id = RealtimeSlotId(part_index_it->second);
    }
    if (auto config_it = part_config_map.find(part_name);
        config_it == part_config_map.end()) {
      return absl::NotFoundError(
          absl::StrCat("Failed to look up PartConfig for Slot '", slot_name,
                       "' (which is using Part '", part_name, "'"));
    } else {
      slot_info_it->second.config = config_it->second;
    }
  }
  return slot_info_map;
}

absl::StatusOr<size_t> FindActionIndex(
    ActionInstanceId id,
    const absl::flat_hash_map<ActionInstanceId, size_t>& action_id_to_index) {
  auto it = action_id_to_index.find(id);
  if (it == action_id_to_index.end()) {
    return absl::NotFoundError(
        absl::StrCat("action id ", id.value(), " not found in action index"));
  }
  return it->second;
}

absl::StatusOr<RealtimeReaction> FromProto(
    const intrinsic_proto::icon::v1::Reaction& proto,
    const absl::flat_hash_map<ActionInstanceId, size_t>& action_id_to_index,
    const absl::flat_hash_map<std::string, size_t>& part_name_to_realtime_index,
    const absl::flat_hash_map<
        ActionInstanceId, absl::flat_hash_map<std::string, RealtimeSignalId>>&
        signal_name_to_id_maps,
    const AggregatedRobotStatus& robot_status) {
  if (!proto.has_condition()) {
    return absl::InvalidArgumentError("reaction must have condition");
  }
  RealtimeReaction reaction;
  reaction.id = ReactionId(proto.reaction_instance_id());
  reaction.fire_once = proto.fire_once();
  if (proto.has_action_association()) {
    reaction.stop_associated_action =
        proto.action_association().stop_associated_action();
    INTR_ASSIGN_OR_RETURN(
        reaction.from_action_index,
        FindActionIndex(
            ActionInstanceId(proto.action_association().action_instance_id()),
            action_id_to_index));
    if (proto.action_association().has_triggered_signal_name()) {
      auto action_instance_id_it = signal_name_to_id_maps.find(
          ActionInstanceId(proto.action_association().action_instance_id()));
      if (action_instance_id_it == signal_name_to_id_maps.end()) {
        return absl::NotFoundError(
            absl::StrCat("There is no signal name to ID map for action id ",
                         proto.action_association().action_instance_id()));
      }
      const auto& signal_name_to_id_map = action_instance_id_it->second;
      auto realtime_signal_id_it = signal_name_to_id_map.find(
          proto.action_association().triggered_signal_name());
      if (realtime_signal_id_it == signal_name_to_id_map.end()) {
        return absl::NotFoundError(
            absl::StrCat("No signal with name ",
                         proto.action_association().triggered_signal_name(),
                         " found for action id ",
                         proto.action_association().action_instance_id()));
      }
      reaction.triggered_signal_id = realtime_signal_id_it->second.value();
    }
  } else {
    // There is no associated action, so we cannot stop any action. Set to
    // false explicitly since stop_associated_action==true while there is
    // nothing to stop might be confusing.
    reaction.stop_associated_action = false;
  }
  if (!proto.has_response()) {
    // TODO (b/284140862): One cannot stop an action without
    // activating an action since it would break old behavior where this was
    // used to just trigger non-rt reactions. If this is needed, an explicit
    // stop action needs to be the target of the reaction.
    reaction.stop_associated_action = false;
  }

  INTR_ASSIGN_OR_RETURN(
      reaction.condition,
      RealtimeConditionFromProto(proto.condition(), part_name_to_realtime_index,
                                 robot_status));
  if (proto.has_response()) {
    INTR_ASSIGN_OR_RETURN(
        reaction.target_action_index,
        FindActionIndex(
            ActionInstanceId(proto.response().start_action_instance_id()),
            action_id_to_index));
    if (reaction.from_action_index.has_value()) {
      LOG(INFO) << "Adding reaction " << reaction.id.value()
                << " with switch from action " << *reaction.from_action_index
                << " to action " << *reaction.target_action_index;
    } else {
      LOG(INFO) << "Adding free-standing reaction " << reaction.id.value()
                << (reaction.target_action_index.has_value()
                        ? absl::StrCat(" with switch to action ",
                                       *reaction.target_action_index)
                        : std::string(" with no action switch"));
    }
  } else {
    reaction.target_action_index = std::nullopt;
    if (reaction.from_action_index.has_value()) {
      LOG(INFO) << "Adding reaction " << reaction.id.value()
                << " from_action_index " << *reaction.from_action_index
                << " without action switch";
    } else {
      LOG(INFO) << "Adding free-standing reaction " << reaction.id.value()
                << " without action switch";
    }
  }
  return reaction;
}

std::unique_ptr<RtclRealtimeSession> MakeRtclRealtimeSession(
    SessionId session_id,
    absl::Span<const std::unique_ptr<RtclActionInstance>> action_instances,
    const absl::flat_hash_map<ReactionId, RealtimeReaction>&
        reaction_id_to_reaction,
    absl::Span<const size_t> actions_indices_to_start = {},
    bool stop_active_actions = false) {
  absl::FixedArray<RtclActionInstance*> action_instance_raw_pointers(
      action_instances.size());
  size_t action_index = 0;
  for (const auto& pointer : action_instances) {
    action_instance_raw_pointers.at(action_index) = pointer.get();
    action_index++;
  }

  absl::FixedArray<RealtimeReaction> reactions(reaction_id_to_reaction.size());
  size_t reaction_index = 0;
  for (const auto& [key, value] : reaction_id_to_reaction) {
    reactions.at(reaction_index) = value;
    reaction_index++;
  }

  auto rtcl_realtime_session =
      std::make_unique<RtclRealtimeSession>(RtclRealtimeSession{
          .session_id = session_id,
          .stop_active_actions = stop_active_actions,
          .actions_to_start = absl::FixedArray<size_t>(
              actions_indices_to_start.begin(), actions_indices_to_start.end()),
          .actions = {std::move(action_instance_raw_pointers)},
          .reactions = {std::move(reactions)}});

  return rtcl_realtime_session;
}

absl::FixedArray<bool> MakeVisibilityMap(
    const absl::flat_hash_map<std::string, size_t>& part_names_to_index,
    absl::Span<const RealtimeSlotId> slot_ids) {
  auto max_part_index_it = absl::c_max_element(
      part_names_to_index, [](const std::pair<std::string, size_t>& lhs,
                              const std::pair<std::string, size_t>& rhs) {
        return lhs.second < rhs.second;
      });
  if (max_part_index_it == part_names_to_index.end()) {
    return {};
  }
  std::vector<bool> visibility_by_slot_id(max_part_index_it->second + 1, false);
  for (const RealtimeSlotId& slot_id : slot_ids) {
    visibility_by_slot_id[slot_id.value()] = true;
  }
  return absl::FixedArray<bool>{visibility_by_slot_id.begin(),
                                visibility_by_slot_id.end()};
}

// Uses the information in `action_description` to
// * create a StreamingIoStorage object for the Action to use and save it in
//   `streaming_io_storage_instances`
// * create a RealtimeSignalStorage object and corresponding signal name to ID
//   map (the latter goes into `signal_name_to_id_maps`)
// * create an Action instance for
// * add that instance to `action_instance_arena`
// * add the index in `action_instance_arena` of the newly created Action to
//   `action_id_to_index`
//
// Returns OkStatus on success, various errors otherwise.
// If this returns non-OK, we leave `streaming_io_storage_instances`,
// `action_id_to_index` and `action_instance_arena` in an undefined, invalid
// state.
absl::Status AddAction(
    const ActionDescription& action_description,
    const intrinsic_proto::icon::v1::ServerConfig& server_config,
    const absl::flat_hash_map<std::string, size_t>& part_name_to_realtime_index,
    const absl::flat_hash_map<std::string,
                              intrinsic_proto::icon::v1::PartConfig>&
        part_name_to_config,
    absl::flat_hash_map<ActionInstanceId, std::unique_ptr<StreamingIoStorage>>&
        streaming_io_storage_instances,
    absl::flat_hash_map<ActionInstanceId,
                        absl::flat_hash_map<std::string, RealtimeSignalId>>&
        realtime_signal_maps_by_action_id,
    absl::flat_hash_map<ActionInstanceId, size_t>& action_id_to_index,
    absl::flat_hash_map<ActionInstanceId, std::string>& action_id_to_type_name,
    std::vector<std::unique_ptr<RtclActionInstance>>& action_instance_arena,
    absl::flat_hash_map<ActionInstanceId, JointTrajectoryPVA>& trajectory_map) {
  LOG(INFO) << "Adding action with type '"
            << action_description.action_type_name << "' and instance id "
            << action_description.id.value() << " to the session.";

  if (!GetGlobalRtclActionFactoryRegistry().GetAllSignatures().contains(
          action_description.action_type_name)) {
    return absl::NotFoundError(absl::StrCat(
        "No Signature for Action type '", action_description.action_type_name,
        "'. Available Action types: ",
        absl::StrJoin(GetGlobalRtclActionFactoryRegistry().GetAllSignatures(),
                      ", ", [](std::string* str, const auto& value) {
                        absl::StrAppend(str, value.first, ": ", value.second);
                      })));
  }

  // Try and get the Action signature requested by action_description.
  INTR_ASSIGN_OR_RETURN(auto action_signature,
                        GetGlobalRtclActionFactoryRegistry().GetSignature(
                            action_description.action_type_name));

  // Create StreamingIoStorage for this Action.
  auto [io_storage_it, storage_inserted] =
      streaming_io_storage_instances.try_emplace(
          action_description.id, std::make_unique<StreamingIoStorage>(
                                     action_description.id, action_signature));
  if (!storage_inserted) {
    return absl::AlreadyExistsError(absl::StrCat(
        "Cannot add Action with ActionInstanceId(",
        action_description.id.value(),
        "). There's already an Action with that ActionInstanceId."));
  }

  // Generate the map from Slot name to SlotInfo
  INTR_ASSIGN_OR_RETURN(
      (absl::flat_hash_map<std::string, SlotInfo> slot_info_map),
      GetSlotInfoMap(part_name_to_realtime_index, part_name_to_config,
                     action_description.slot_part_map));

  INTR_ASSIGN_OR_RETURN(auto signal_storage_and_id_map,
                        CreateRealtimeSignalStorage(action_signature));

  realtime_signal_maps_by_action_id[action_description.id] =
      signal_storage_and_id_map.signal_id_map;

  // Invoke the Action factory.
  ActionFactoryContext action_factory_context(
      server_config, action_signature, slot_info_map,
      signal_storage_and_id_map.signal_id_map, *io_storage_it->second,
      action_description.id, trajectory_map);
  INTR_ASSIGN_OR_RETURN(std::unique_ptr<RtclActionInterface> action,
                        GetGlobalRtclActionFactoryRegistry().CallFactory(
                            action_description.action_type_name,
                            action_description.params, action_factory_context));
  INTR_RETURN_IF_ERROR(action_factory_context.Validate());

  // Make an RtclActionInstance from the RtclActionInterface we got, and save
  // it.
  std::vector<RealtimeSlotId> slot_ids;
  slot_ids.reserve(slot_info_map.size());
  for (const auto& [_, slot_info] : slot_info_map) {
    slot_ids.push_back(slot_info.slot_id);
  }
  // The index of the new Action instance will be equal to the size of the
  // container before insertion.
  action_id_to_index[action_description.id] = action_instance_arena.size();
  action_id_to_type_name[action_description.id] =
      action_description.action_type_name;

  INTR_ASSIGN_OR_RETURN(const auto behavior_overrides,
                        MakeBehaviorOverrideSupportArray(action_signature));

  action_instance_arena.push_back(
      std::make_unique<RtclActionInstance>(RtclActionInstance{
          .streaming_io_storage = std::make_unique<RealtimeStreamingIoStorage>(
              FromStreamingIoStorage(*io_storage_it->second)),
          .realtime_signal_storage = std::make_unique<RealtimeSignalStorage>(
              std::move(signal_storage_and_id_map.signal_storage)),
          .action = std::move(action),
          .visibility_by_slot_id = std::make_unique<absl::FixedArray<bool>>(
              MakeVisibilityMap(part_name_to_realtime_index, slot_ids)),
          .id = action_description.id,
          .supported_behavior_overrides_by_enum_value =
              std::make_unique<absl::FixedArray<bool>>(behavior_overrides)}));
  return absl::OkStatus();
}

absl::Status CheckIfFreestandingReactionContainsActionStateVariable(
    const RealtimeReaction& reaction) {
  if (reaction.from_action_index
          .has_value())  // A free-standing reaction is defined by
                         // not having a from_action_index.
  {
    return absl::OkStatus();
  }

  for (const ConditionElement& element : reaction.condition.elements) {
    if (std::holds_alternative<RealtimeComparison>(element)) {
      const RealtimeComparison& comparison =
          std::get<RealtimeComparison>(element);
      // If the operand is a string, it is a state action variable.
      if (std::holds_alternative<FixedString<kMaxStateVariableNameLength>>(
              comparison.operand)) {
        return absl::InvalidArgumentError(absl::StrCat(
            "Found action state variable (",
            std::get<FixedString<kMaxStateVariableNameLength>>(
                comparison.operand),
            ") in free-standing reaction with id ", reaction.id.value(),
            ". This is infeasible since the reaction is not associated with "
            "an action."));
      }
    }
  }
  return absl::OkStatus();
}

}  // namespace

void RtclControllerSession::PublishOutputStreams() {
  INTRINSIC_ASSERT_NON_REALTIME();
  const auto decimation_factor = CalculateDecimationFactor(
      server_config_.frequency_hz(), kThrottledStatusRate);
  if (!decimation_factor.ok()) {
    LOG(ERROR) << "Failed to calculate decimation factor: "
               << decimation_factor.status()
               << ", aborting PublishOutputStreams().";
    return;
  }
  while (!shutdown_notification_.HasBeenNotified()) {
    {
      // Scoped to separate the lock from the sleep call.
      absl::MutexLock l(session_mutex_);
      const LoggingMode logging_mode =
          logging_mode_buffer_.GetCurrentValue(absl::UnixEpoch())
              .value_or(LoggingMode::kThrottled);
      for (const auto& [action_id, io_storage] :
           streaming_io_storage_instances_) {
        if (!io_storage->output_channel_) {
          // No output defined for this action.
          continue;
        }

        absl::StatusOr<std::string> output_topic =
            TopicNameForStreamingOutput(server_config_.name(), action_id);
        if (!output_topic.ok()) {
          LOG_EVERY_N_SEC(ERROR, 10)
              << "Failed to create topic " << output_topic.status();
          continue;
        }

        auto maybe_output_or = GetNextStreamingOutput(*io_storage);
        if (!maybe_output_or.ok()) {
          LOG_EVERY_N_SEC(ERROR, 10)
              << "Failed to poll output: " << maybe_output_or.status();
          continue;
        } else if (!maybe_output_or->has_value()) {
          continue;
        }

        auto output_with_cycle = std::move(**maybe_output_or);

        // Wrap some additional information into the output.
        intrinsic_proto::icon::StreamingOutputWithMetadata output_with_metadata;
        *output_with_metadata.mutable_output() =
            std::move(output_with_cycle.output);
        output_with_metadata.set_action_instance_id(action_id.value());
        // Get the action type name.
        auto action_type_name_it = action_id_to_type_name_.find(action_id);
        if (action_type_name_it == action_id_to_type_name_.end()) {
          LOG_EVERY_N_SEC(ERROR, 10)
              << "Failed to lookup action. This means the session is in "
                 "a bad state!";
          continue;
        }
        output_with_metadata.set_action_type_name(action_type_name_it->second);

        // Publish the output.
        auto statusor = GetOrCreatePublisher(*output_topic);
        if (!statusor.ok()) {
          LOG_EVERY_N_SEC(ERROR, 10) << "Failed to create Publisher";
          continue;
        }
        intrinsic::Publisher& pub = statusor.value();

        intrinsic_proto::data_logger::LogItem log_item;
        log_item.mutable_metadata()->set_event_source(*output_topic);
        *log_item.mutable_context() = ToProto(log_context_);
        log_item.mutable_context()->set_icon_action_id(action_id.value());
        if (absl::Status status =
                AddSkillLogIdLabel(*log_item.mutable_context());
            !status.ok()) {
          LOG_EVERY_N_SEC(ERROR, 10)
              << "Failed to add skill id label to context: " << status;
        }

        static_cast<void>(log_item.mutable_payload()->mutable_any()->PackFrom(
            output_with_metadata));  // TODO: Handle return value

        if (absl::Status status = pub.Publish(log_item); !status.ok()) {
          LOG_EVERY_N_SEC(ERROR, 10) << "Failed to publish output for topic '"
                                     << *output_topic << "', error: " << status;
          continue;
        }

        const bool log_throttled_cycle = IsThrottledLogCycle(
            output_with_cycle.cycle_index, *decimation_factor);

        if (logging_mode == LoggingMode::kFullRate || log_throttled_cycle) {
          data_logger::LogAsync(log_item, [](absl::Status status) {
            if (!status.ok()) {
              LOG_EVERY_N_SEC(ERROR, 5)
                  << "Failed to log streaming output: " << status;
            }
          });
        }
      }
    }
    // The GetNextStreamingOutput does not block, so a bit of sleeping to avoid
    // hammering the cpus.
    //
    // Note, we may want to make this more dynamic based on the amount of time
    // the publishing takes.
    absl::SleepFor(absl::Microseconds(300));
  }
}

absl::StatusOr<std::reference_wrapper<intrinsic::Publisher>>
RtclControllerSession::GetOrCreatePublisher(absl::string_view topic) {
  absl::MutexLock lock(publishers_mutex_);
  auto it = publishers_.find(topic);
  if (it != publishers_.end()) {
    return std::ref(it->second);
  }

  INTR_ASSIGN_OR_RETURN(auto publisher,
                        pub_sub_.CreatePublisher(topic, TopicConfig()),
                        absl::InternalError("Failed to create publisher"));

  std::tie(it, std::ignore) = publishers_.emplace(topic, std::move(publisher));
  return std::ref(it->second);
}

RtclControllerSession::RtclControllerSession(
    const intrinsic_proto::icon::v1::ServerConfig& server_config,
    const absl::flat_hash_map<std::string, size_t>& part_name_to_realtime_index,
    SessionId session_id,
    absl::flat_hash_map<std::string, intrinsic_proto::icon::v1::PartConfig>
        part_config_map,
    std::unique_ptr<RtclSessionBridgeInterface> session_bridge,
    const RealtimeLogContext& log_context,
    InitializedAsyncBuffer<PublishOutput>& robot_status_buffer,
    InitializedAsyncBuffer<LoggingMode>& logging_mode_buffer)

    : server_config_(server_config),
      session_id_(session_id),
      part_name_to_realtime_index_(part_name_to_realtime_index),
      part_name_to_config_(part_config_map),
      session_bridge_(std::move(session_bridge)),
      robot_status_buffer_(robot_status_buffer),
      logging_mode_buffer_(logging_mode_buffer),
      log_context_(log_context) {
  log_context_.icon_session_id = session_id.value();
  if (auto thread = CreateRealtimeCapableThread(
          ThreadOptions()
              .SetName("PublishOutputStreams")
              // The publishing thread must not run at realtime priority.
              .SetNormalPriorityAndScheduler(),
          [this]() { PublishOutputStreams(); });
      thread.ok()) {
    publishing_thread_ = *std::move(thread);
  } else {
    LOG(WARNING) << "Failed to start a publishing thread for session "
                 << session_id.value() << ". Error: " << thread.status();
  }
}

RtclControllerSession::~RtclControllerSession() {
  shutdown_notification_.Notify();
  // Not joinable if controller not initialize()d
  if (publishing_thread_.joinable()) {
    publishing_thread_.join();
  }
  FinishSession();
}

void RtclControllerSession::FinishSession() {
  // Reacquire mutex, because any MutexLock in the surrounding function goes out
  // of scope before this is executed.
  absl::MutexLock l(session_mutex_);
  (void)session_bridge_->FinishSession();
  // The session is done (or broken), so it can't refer to the installed
  // RtclRealtimeSession any more.
  installed_realtime_session_.reset();
}

absl::Status RtclControllerSession::AddActionsAndReactions(
    const ActionsAndReactions& actions_and_reactions) {
  // Make a Cleanup that invokes finish_session if we encounter any errors.
  absl::Cleanup finish_session_on_error =
      absl::bind_front(&RtclControllerSession::FinishSession, this);
  // TODO(b/216481963): Throughout, check whether we need to distinguish between
  // ReaderMutexLocks and WriterMutexLocks to prevent deadlocks in real-world
  // use.
  absl::MutexLock l(session_mutex_);
  INTR_RETURN_IF_ERROR(session_bridge_->GetSessionStatus());

  for (const ActionDescription& action_description :
       actions_and_reactions.action_descriptions) {
    if (known_action_ids_.contains(action_description.id)) {
      // Disarm the Cleanup (this is a non-fatal error).
      std::move(finish_session_on_error).Cancel();
      return absl::AlreadyExistsError(absl::StrCat(
          "Duplicate ActionInstanceId(", action_description.id.value(),
          "). You cannot reuse ActionInstanceIDs within the same sessions, "
          "even if the corresponding action has been removed."));
    }

    for (const auto& [slot_name, part_name] :
         action_description.slot_part_map) {
      if (!part_name_to_config_.contains(part_name)) {
        std::move(finish_session_on_error).Cancel();
        return absl::InvalidArgumentError(absl::StrCat(
            "Cannot add Action with ID ", action_description.id.value(),
            " because it uses part `", part_name,
            "` that is not controlled by this session."));
      }
    }

    INTR_RETURN_IF_ERROR(AddAction(
        action_description, server_config_, part_name_to_realtime_index_,
        part_name_to_config_, streaming_io_storage_instances_,
        action_id_to_realtime_signal_map_, action_id_to_index_,
        action_id_to_type_name_, action_instance_arena_, trajectory_map_));
  }
  if (!actions_and_reactions.reaction_descriptions.empty()) {
    constexpr absl::Time unix_epoch_start = absl::FromUnixMicros(0);
    // Only query the status when there are actually some reactions.
    std::optional<PublishOutput> robot_status =
        robot_status_buffer_.GetCurrentValue(
            unix_epoch_start  // Do not wait. The value should be already there
                              // if the RT thread is actually running.
        );

    if (!robot_status.has_value()) {
      return absl::UnavailableError(
          "Could not retrieve robot status from rt-to-nonrt-buffer for "
          "verification of part status field conditions! This most likely "
          "means that the realtime thread is not running yet.");
    }

    // Convert all Reaction descriptions to realtime-friendly counterparts.
    for (const ::intrinsic_proto::icon::v1::Reaction& proto :
         actions_and_reactions.reaction_descriptions) {
      if (known_reaction_ids_.contains(
              ReactionId(proto.reaction_instance_id()))) {
        // Disarm the Cleanup (this is a non-fatal error).
        std::move(finish_session_on_error).Cancel();
        return absl::AlreadyExistsError(absl::StrCat(
            "Duplicate ReactionId(", proto.reaction_instance_id(),
            "). You cannot reuse ReactionIds within the same sessions, "
            "even if the corresponding reaction has been removed."));
      }

      INTR_ASSIGN_OR_RETURN(
          RealtimeReaction reaction,
          FromProto(proto, action_id_to_index_, part_name_to_realtime_index_,
                    action_id_to_realtime_signal_map_,
                    robot_status->robot_status));
      INTR_RETURN_IF_ERROR(
          CheckIfFreestandingReactionContainsActionStateVariable(reaction));

      if (bool inserted =
              reaction_instances_by_id_.try_emplace(reaction.id, reaction)
                  .second;
          !inserted) {
        // Disarm the Cleanup (this is a non-fatal error).
        std::move(finish_session_on_error).Cancel();
        return absl::AlreadyExistsError(absl::StrCat(
            "Cannot add Reaction with ReactionId(", reaction.id.value(),
            "). There's already a Reaction with that ReactionId."));
      }
    }
  }
  // Make a new RtclRealtimeSession object that holds pointers to all current
  // Actions and Reactions (not just the ones added above, but also those that
  // were there before).
  std::unique_ptr<RtclRealtimeSession> rtcl_realtime_session =
      MakeRtclRealtimeSession(session_id_, action_instance_arena_,
                              reaction_instances_by_id_);

  // Do some preliminary sanity checks on the session data.
  INTR_RETURN_IF_ERROR(VerifySessionData(*rtcl_realtime_session));

  // Now we're ready to install the RtclRealtimeSession! The realtime thread
  // only assumes exclusive write access to this instance, not ownership. In
  // particular, it *must not* delete it, since we still own the unique_ptr.
  //
  // The realtime thread's exclusive write access ends as soon as it
  // acknowledges the installation of another RtclRealtimeSession pointer.
  //
  // If we failed to install the new session pointer, bail out (this deletes
  // rtcl_realtime_session and triggers the Cleanup that calls
  // finish_session_bridge_).
  INTR_RETURN_IF_ERROR(
      session_bridge_->InstallSessionData(rtcl_realtime_session.get()));

  // We've successfully installed the new RtclRealtimeSession, so save it, and
  // delete the previous one. It must not be replaced until the next call to
  // either install_session_data_bridge_ or finish_session_bridge_->
  installed_realtime_session_ = std::move(rtcl_realtime_session);

  // Add the new action and reaction IDs to our sets of known IDs to prevent
  // reuse within this session.
  for (const ActionDescription& action_description :
       actions_and_reactions.action_descriptions) {
    known_action_ids_.insert(action_description.id);
  }
  for (const ::intrinsic_proto::icon::v1::Reaction& proto :
       actions_and_reactions.reaction_descriptions) {
    known_reaction_ids_.insert(ReactionId(proto.reaction_instance_id()));
  }

  // Disarm the Cleanup.
  std::move(finish_session_on_error).Cancel();

  return absl::OkStatus();
}

absl::Status RtclControllerSession::RemoveActionsAndReactions(
    const ActionAndReactionIds& action_and_reaction_ids) {
  // Make a Cleanup that invokes finish_session if we encounter any errors.
  absl::Cleanup finish_session_on_error =
      absl::bind_front(&RtclControllerSession::FinishSession, this);

  absl::MutexLock l(session_mutex_);
  INTR_RETURN_IF_ERROR(session_bridge_->GetSessionStatus());
  absl::flat_hash_set<ActionInstanceId> action_ids_to_remove{
      action_and_reaction_ids.action_ids.begin(),
      action_and_reaction_ids.action_ids.end()};
  absl::flat_hash_set<size_t> action_indices_to_remove;
  for (const auto& action_id : action_ids_to_remove) {
    auto action_index_it = action_id_to_index_.find(action_id);
    if (action_index_it == action_id_to_index_.end()) {
      // Disarm the Cleanup (this is a non-fatal error).
      std::move(finish_session_on_error).Cancel();
      return absl::NotFoundError(absl::StrCat("Cannot remove Action with ID ",
                                              action_id.value(),
                                              ", no such Action exists."));
    }
    action_indices_to_remove.insert(action_index_it->second);
  }
  absl::flat_hash_set<ReactionId> reaction_ids_to_remove;
  for (const auto& reaction_id : action_and_reaction_ids.reaction_ids) {
    if (!reaction_instances_by_id_.contains(reaction_id)) {
      // Disarm the Cleanup (this is a non-fatal error).
      std::move(finish_session_on_error).Cancel();
      return absl::NotFoundError(absl::StrCat("Cannot remove Reaction with ID ",
                                              reaction_id.value(),
                                              ", no such Reaction exists."));
    }
    reaction_ids_to_remove.insert(reaction_id);
  }

  // Find what reactions need to be deleted since they belong to a removed
  // action or are removed explicitly.
  absl::flat_hash_map<ReactionId, RealtimeReaction> reactions_after_removals;
  for (const auto& [id, reaction] : reaction_instances_by_id_) {
    bool originates_at_deleted_action =
        reaction.from_action_index.has_value() &&
        action_indices_to_remove.contains(*reaction.from_action_index);
    bool switches_to_deleted_action =
        reaction.target_action_index.has_value() &&
        action_indices_to_remove.contains(reaction.target_action_index.value());
    bool should_remove_explicitly =
        reaction_ids_to_remove.contains(reaction.id);
    if (originates_at_deleted_action || switches_to_deleted_action ||
        should_remove_explicitly) {
      continue;
    }
    reactions_after_removals.emplace(reaction.id, reaction);
  }
  std::unique_ptr<RtclRealtimeSession> new_session = MakeRtclRealtimeSession(
      session_id_, action_instance_arena_, reactions_after_removals);
  // Set the RtclActionInstance pointers for removed Actions to nullptr.
  for (auto& action_ptr : new_session->actions) {
    if (action_ptr == nullptr) {
      // This action has already been removed, so it cannot be a candidate for
      // removal.
      continue;
    }
    if (action_ids_to_remove.contains(action_ptr->id)) {
      action_ptr = nullptr;
    }
  }

  // Attempt to install the new session.
  INTR_RETURN_IF_ERROR(session_bridge_->InstallSessionData(new_session.get()));

  // If we succeeded, update our member variables:
  //
  // 1. Delete the removed Actions from action_instance_arena_.
  for (auto& action_ptr : action_instance_arena_) {
    if (action_ptr == nullptr) {
      // This action has already been removed, so it cannot be a candidate for
      // removal.
      continue;
    }
    if (action_ids_to_remove.contains(action_ptr->id)) {
      action_ptr = nullptr;
    }
  }
  // 2. Remove the IDs of removed Actions from action_id_to_index_, and
  // erase their corresponding StreamingIoStorage instances and realtime signal
  // name maps.
  for (const auto& action_id : action_ids_to_remove) {
    action_id_to_index_.erase(action_id);
    action_id_to_type_name_.erase(action_id);
    streaming_io_storage_instances_.erase(action_id);
    action_id_to_realtime_signal_map_.erase(action_id);
  }
  // 3. Save reactions_after_removals_ as reaction_instances_by_id_.
  reaction_instances_by_id_ = std::move(reactions_after_removals);
  // 4. Save new_session as installed_realtime_session_.
  installed_realtime_session_ = std::move(new_session);

  // Finally, disarm the Cleanup.
  std::move(finish_session_on_error).Cancel();

  return absl::OkStatus();
}

absl::Status RtclControllerSession::RemoveAllActionsAndReactions() {
  absl::Cleanup finish_session_on_error =
      absl::bind_front(&RtclControllerSession::FinishSession, this);

  absl::MutexLock l(session_mutex_);
  INTR_RETURN_IF_ERROR(session_bridge_->GetSessionStatus());

  auto empty_realtime_session = std::make_unique<RtclRealtimeSession>(
      RtclRealtimeSession{.session_id = session_id_,
                          .actions_to_start = {},
                          .actions = absl::FixedArray<RtclActionInstance*>(
                              action_instance_arena_.size(), nullptr),
                          .reactions = {}});
  // If we fail to install the new session pointer, bail out (this deletes
  // empty_realtime_session and triggers the Cleanup that calls
  // finish_session_bridge_).
  INTR_RETURN_IF_ERROR(
      session_bridge_->InstallSessionData(empty_realtime_session.get()));

  // We've successfully installed the new RtclRealtimeSession, so save it, and
  // delete the previous one. It must not be replaced until the next call to
  // either install_session_data_bridge_ or finish_session_bridge_->
  installed_realtime_session_ = std::move(empty_realtime_session);

  // All Actions and Reactions are now unused, so delete them.
  absl::c_fill(action_instance_arena_, nullptr);
  reaction_instances_by_id_.clear();
  action_id_to_index_.clear();
  action_id_to_type_name_.clear();
  streaming_io_storage_instances_.clear();

  // Disarm the Cleanup.
  std::move(finish_session_on_error).Cancel();
  return absl::OkStatus();
}

absl::Status RtclControllerSession::StartActions(
    absl::Span<const ActionInstanceId> action_instance_ids,
    bool stop_active_actions) {
  absl::Cleanup finish_session_on_error =
      absl::bind_front(&RtclControllerSession::FinishSession, this);

  absl::MutexLock l(session_mutex_);
  std::vector<size_t> actions_to_start;
  for (auto& action_instance_id : action_instance_ids) {
    INTR_RETURN_IF_ERROR(session_bridge_->GetSessionStatus());
    auto action_index_it = action_id_to_index_.find(action_instance_id);
    if (action_index_it == action_id_to_index_.end()) {
      // Disarm the Cleanup (this is a non-fatal error).
      std::move(finish_session_on_error).Cancel();
      return absl::NotFoundError(absl::StrCat("Cannot start Action with ID ",
                                              action_instance_id.value(),
                                              ", no such Action exists."));
    }
    actions_to_start.push_back(action_index_it->second);
  }

  // TODO(b/480114363): Consider checking that all actions support a currently
  // active BehaviorOverrideRequest.
  // This would mean that adding them will lead to immediate session closure.
  // As the request is transient, the case can be made that the
  // BehaviorOverrideRequest may be over once the action is actually called.
  std::unique_ptr<RtclRealtimeSession> rtcl_realtime_session =
      MakeRtclRealtimeSession(session_id_, action_instance_arena_,
                              reaction_instances_by_id_, actions_to_start,
                              stop_active_actions);

  INTR_RETURN_IF_ERROR(
      session_bridge_->InstallSessionData(rtcl_realtime_session.get()));
  installed_realtime_session_ = std::move(rtcl_realtime_session);

  std::move(finish_session_on_error).Cancel();
  return absl::OkStatus();
}

absl::StatusOr<std::optional<ReactionEvent>>
RtclControllerSession::PollReactions() {
  absl::MutexLock l(session_mutex_);
  INTR_RETURN_IF_ERROR(session_bridge_->GetSessionStatus());
  return session_bridge_->PollReactions();
}

absl::Status RtclControllerSession::WriteToStreamingInput(
    ActionInstanceId action_instance_id, absl::string_view input_name,
    const google::protobuf::Any& input) {
  absl::MutexLock l(session_mutex_);
  INTR_RETURN_IF_ERROR(session_bridge_->GetSessionStatus());
  auto streaming_io_storage_it =
      streaming_io_storage_instances_.find(action_instance_id);
  if (streaming_io_storage_it == streaming_io_storage_instances_.end()) {
    return absl::NotFoundError(absl::StrCat("Cannot write to streaming input '",
                                            input_name, "' for Action with ID ",
                                            action_instance_id.value(),
                                            " because no such Action exists."));
  }
  INTR_RETURN_IF_ERROR(WriteStreamingInputAny(
      input_name, input, *streaming_io_storage_it->second));

  return absl::OkStatus();
}

absl::StatusOr<::intrinsic_proto::icon::StreamingOutput>
RtclControllerSession::GetLatestStreamingOutput(
    ActionInstanceId action_instance_id, absl::Time deadline) {
  absl::MutexLock l(session_mutex_);
  INTR_RETURN_IF_ERROR(session_bridge_->GetSessionStatus());

  auto streaming_io_storage_it =
      streaming_io_storage_instances_.find(action_instance_id);
  if (streaming_io_storage_it == streaming_io_storage_instances_.end()) {
    return absl::NotFoundError(absl::StrCat(
        "Cannot poll streaming output for Action with ID ",
        action_instance_id.value(), " because no such Action exists."));
  }
  INTR_ASSIGN_OR_RETURN(
      StreamingOutputWithCycle output_with_cycle,
      PollStreamingOutputAny(deadline, *streaming_io_storage_it->second));

  return output_with_cycle.output;
}

absl::StatusOr<::intrinsic_proto::icon::JointTrajectoryPVA>
RtclControllerSession::GetPlannedTrajectory(
    ActionInstanceId action_instance_id) {
  absl::MutexLock l(session_mutex_);
  INTR_RETURN_IF_ERROR(session_bridge_->GetSessionStatus());

  // Check if action ID exists in current session.
  if (!action_id_to_index_.contains(action_instance_id)) {
    return absl::FailedPreconditionError(absl::StrCat(
        "GetPlannedTrajectory() - Action with ActionInstanceId ",
        action_instance_id.value(), " does not exist in current Session."));
  }

  // Check if for the given action ID, a trajectory has been stored.
  if (!trajectory_map_.contains(action_instance_id)) {
    return absl::NotFoundError(
        absl::StrCat("GetPlannedTrajectory() - No trajectory is available for "
                     "Action with ActionInstanceId ",
                     action_instance_id.value()));
  }
  return ToProto(trajectory_map_[action_instance_id]);
}

PartGroup RtclControllerSession::GetPartGroup() const {
  PartGroup out;
  for (const auto& part_name_and_config : part_name_to_config_) {
    out.insert(part_name_and_config.first);
  }
  return out;
}

absl::flat_hash_set<ActionInstanceId>
RtclControllerSession::GetActionInstanceIds() const {
  absl::MutexLock l(session_mutex_);
  absl::flat_hash_set<ActionInstanceId> action_instance_ids;
  for (const auto& [action_instance_id, _] : action_id_to_index_) {
    action_instance_ids.insert(action_instance_id);
  }
  return action_instance_ids;
}

}  // namespace intrinsic::icon
