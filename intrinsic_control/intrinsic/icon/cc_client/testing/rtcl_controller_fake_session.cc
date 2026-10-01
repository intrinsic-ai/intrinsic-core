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

#include "intrinsic/icon/cc_client/testing/rtcl_controller_fake_session.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <any>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "intrinsic/icon/cc_client/condition.h"
#include "intrinsic/icon/cc_client/operational_status.h"
#include "intrinsic/icon/cc_client/testing/channel_fake.h"
#include "intrinsic/icon/common/id_types.h"
#include "intrinsic/icon/common/part_group.h"
#include "intrinsic/icon/common/slot_part_map.h"
#include "intrinsic/icon/control/action_factory_context.h"
#include "intrinsic/icon/control/mock_rtcl_action.h"
#include "intrinsic/icon/control/parts/realtime_part_status.h"
#include "intrinsic/icon/control/realtime_actions_reactions.h"
#include "intrinsic/icon/control/realtime_bridge_types.h"
#include "intrinsic/icon/control/realtime_condition.h"
#include "intrinsic/icon/control/realtime_signal_storage.h"
#include "intrinsic/icon/control/rtcl_action.h"
#include "intrinsic/icon/control/rtcl_action_factory_registry.h"
#include "intrinsic/icon/control/slot_types.h"
#include "intrinsic/icon/control/streaming_io_helpers.h"
#include "intrinsic/icon/control/streaming_io_storage.h"
#include "intrinsic/icon/proto/joint_trajectory_conversion.h"
#include "intrinsic/icon/proto/safety_status_conversion.h"
#include "intrinsic/icon/proto/v1/service.pb.h"
#include "intrinsic/icon/proto/v1/types.pb.h"
#include "intrinsic/icon/server/session_interface.h"
#include "intrinsic/icon/server/signature_utils.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/icon/utils/realtime_status_or.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic::icon {

namespace {

constexpr size_t kADIONumOfBlocks = 4;  // Analog in, analog out,
                                        // digital in and digital out

absl::StatusOr<absl::flat_hash_map<std::string, SlotInfo>> GetSlotInfoMap(
    const absl::flat_hash_map<
        std::string, intrinsic_proto::icon::v1::PartConfig>& part_config_map,
    const SlotPartMap& slot_part_map) {
  size_t next_index = 0;
  absl::flat_hash_map<std::string, SlotInfo> slot_info_map;
  for (const auto& [slot_name, part_name] : slot_part_map) {
    auto [slot_info_it, inserted] = slot_info_map.try_emplace(slot_name);
    if (!inserted) {
      return absl::FailedPreconditionError(
          absl::StrCat("Encountered duplicate Slot name '", slot_name, "'"));
    }
    // Simply use unique indices - actions never get a chance to use the
    // RealtimePartIndex since we don't actually call OnEnter(), Sense() and
    // Control() in this fake.
    slot_info_it->second.slot_id = RealtimeSlotId(next_index++);

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

absl::Status ActionCompatibleWithSlotPartMap(
    const intrinsic_proto::icon::v1::ActionSignature& action_signature,
    const SlotPartMap& slot_part_map,
    const absl::flat_hash_map<std::string,
                              intrinsic_proto::icon::v1::PartConfig>&
        part_configs_by_name) {
  for (const auto& [slot_name, part_name] : slot_part_map) {
    auto part_config = part_configs_by_name.find(part_name);
    if (part_config == part_configs_by_name.end()) {
      return absl::NotFoundError(
          absl::StrCat("Slot '", slot_name, "' maps to part '", part_name,
                       "', but that part does not exist."));
    }
    // Check if this Part works for the requested Slot.
    auto slot_info_it = action_signature.part_slot_infos().find(slot_name);
    if (slot_info_it == action_signature.part_slot_infos().end()) {
      return absl::NotFoundError(absl::StrCat(
          "SlotPartMap for Action type '", action_signature.action_type_name(),
          "' references Slot name '", slot_name,
          "', which is not a Slot the Action type accepts."));
    }
    if (const auto result =
            PartCompatibleWithSlot(part_config->second, slot_info_it->second);
        !result.Compatible()) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Part '", part_name, "' is not compatible with Slot '", slot_name,
          "' for Action type '", action_signature.action_type_name(),
          "'. Explanation: ", result.Explain()));
    }
  }
  return absl::OkStatus();
}

}  // namespace

RtclControllerFakeSession::~RtclControllerFakeSession() {
  if (validation_mode_ == ValidationMode::kNice) {
    return;
  }

  EXPECT_EQ(ordered_action_behaviors_.size(), 0)
      << "ChannelFake has unmet action expectations";
}

absl::Status RtclControllerFakeSession::AddAction(
    const ActionDescription& action) {
  absl::MutexLock lock(mutex_);
  if (actions_by_id_.contains(action.id)) {
    return absl::AlreadyExistsError(
        absl::StrCat("Trying to add action with ID ", action.id.value(),
                     ", but there is already an Action with that ID."));
  }

  for (const auto& [slot_name, part_name] : action.slot_part_map) {
    if (!part_configs_by_name_.contains(part_name)) {
      return absl::InvalidArgumentError(
          absl::StrCat("Cannot add Action with ID ", action.id.value(),
                       " because it uses part `", part_name,
                       "` that is not controlled by this session."));
    }
  }

  // Check if there is any behavior/expectation specified in the given sequence.
  // In strict mode, there must be an entry for each action. This check is to
  // fail early, during runtime it is also checked that there is an entry for
  // each action visit.
  if (validation_mode_ == ValidationMode::kStrict) {
    if (absl::c_find_if(ordered_action_behaviors_, [&action](const auto& elem) {
          return elem.action_id == action.id;
        }) == ordered_action_behaviors_.end()) {
      return absl::FailedPreconditionError(absl::StrCat(
          "Validation mode is strict, but there is no action "
          "behavior specified for action ID ",
          action.id.value(),
          ". If you want to allow this, use ChannelFake's nice mode."));
    }
  }

  // Get the action's signature.
  INTR_ASSIGN_OR_RETURN(::intrinsic_proto::icon::v1::ActionSignature signature,
                        GetGlobalRtclActionFactoryRegistry().GetSignature(
                            action.action_type_name));

  // Check action / part compatibility.
  INTR_RETURN_IF_ERROR(ActionCompatibleWithSlotPartMap(
      signature, action.slot_part_map, part_configs_by_name_));

  // Create the SlotInfo map we pass to the factory.
  INTR_ASSIGN_OR_RETURN(
      auto slot_info_map,
      GetSlotInfoMap(part_configs_by_name_, action.slot_part_map));
  INTR_ASSIGN_OR_RETURN(auto signal_id_map_and_storage,
                        CreateRealtimeSignalStorage(signature));
  auto streaming_io_storage =
      std::make_unique<StreamingIoStorage>(action.id, signature);
  ::intrinsic_proto::icon::v1::ServerConfig server_config;
  server_config.set_name("rtcl_controller_channel_fake");
  server_config.set_frequency_hz(frequency_hz_);
  ActionFactoryContext ctx(server_config, signature, slot_info_map,
                           signal_id_map_and_storage.signal_id_map,
                           *streaming_io_storage, action.id, trajectory_map_);

  // Run the action factory.
  INTR_ASSIGN_OR_RETURN(std::unique_ptr<RtclActionInterface> action_instance,
                        GetGlobalRtclActionFactoryRegistry().CallFactory(
                            action.action_type_name, action.params, ctx));
  actions_by_id_.emplace(
      action.id, ActionData{.description = action,
                            .action = std::move(action_instance),
                            .io_storage = std::move(streaming_io_storage),
                            .signal_storage = std::move(
                                signal_id_map_and_storage.signal_storage)});
  return absl::OkStatus();
}

absl::Status RtclControllerFakeSession::AddReaction(
    const intrinsic_proto::icon::v1::Reaction& reaction) {
  absl::MutexLock lock(mutex_);
  if (reaction.has_action_association() &&
      !actions_by_id_.contains(ActionInstanceId(
          reaction.action_association().action_instance_id()))) {
    return absl::NotFoundError(
        absl::StrCat("Attempting to add Reaction from Action ",
                     reaction.action_association().action_instance_id(),
                     ", but there is no Action with that ID."));
  }
  if (reaction.has_response() &&
      !actions_by_id_.contains(
          ActionInstanceId(reaction.response().start_action_instance_id()))) {
    return absl::NotFoundError(
        absl::StrCat("Attempting to add Reaction that switches to Action ",
                     reaction.response().start_action_instance_id(),
                     ", but there is no Action with that ID."));
  }
  if (reactions_.contains(ReactionId(reaction.reaction_instance_id()))) {
    return absl::AlreadyExistsError(
        absl::StrCat("There is already a Reaction with ID ",
                     reaction.reaction_instance_id()));
  }
  // Add the reaction for processing later.
  reactions_.emplace(ReactionId(reaction.reaction_instance_id()), reaction);

  return absl::OkStatus();
}

void RtclControllerFakeSession::StopActiveAction(ActionInstanceId action_id) {
  active_action_ids_.erase(action_id);
}

void RtclControllerFakeSession::StopAllActiveActions() {
  active_action_ids_.clear();
}

absl::Status RtclControllerFakeSession::UpdateActiveAction(
    ActionInstanceId id) {
  const auto it = actions_by_id_.find(id);
  if (it == actions_by_id_.end()) {
    return absl::NotFoundError(absl::StrCat(
        "Cannot switch to unknown action with ID ", id.value(),
        ". Make sure you actually added the action to your session."));
  }

  if (validation_mode_ == ValidationMode::kStrict &&
      !active_action_behavior_.has_value()) {
    return absl::FailedPreconditionError(
        "Validation mode is strict but there is no action behavior active");
  }

  if (active_action_behavior_.has_value()) {
    // Run the user-supplied parameter matcher of the active action behavior,
    // if any.
    if (std::function<absl::Status(const google::protobuf::Any&)>
            parameter_matcher =
                active_action_behavior_->action_behaviors.GetParameterMatcher();
        parameter_matcher != nullptr) {
      auto outcome = parameter_matcher(it->second.description.params);
      if (!outcome.ok()) {
        LOG(ERROR) << "Parameter matcher failed: " << outcome;
        return outcome;
      }
    }
  }

  active_action_ids_.insert(id);

  return absl::OkStatus();
}

absl::Status RtclControllerFakeSession::AddActionsAndReactions(
    const ActionsAndReactions& actions_and_reactions) {
  for (const auto& action : actions_and_reactions.action_descriptions) {
    INTR_RETURN_IF_ERROR(AddAction(action));
  }
  for (const auto& reaction : actions_and_reactions.reaction_descriptions) {
    INTR_RETURN_IF_ERROR(AddReaction(reaction));
  }
  return absl::OkStatus();
}

absl::Status RtclControllerFakeSession::RemoveActionsAndReactions(
    const ActionAndReactionIds& action_and_reaction_ids) {
  absl::MutexLock lock(mutex_);
  for (const ActionInstanceId& action_id : action_and_reaction_ids.action_ids) {
    if (!actions_by_id_.contains(action_id)) {
      return absl::NotFoundError(
          absl::StrCat("Trying to remove unknown Action ", action_id.value()));
    }
    if (!active_action_ids_.empty() && active_action_ids_.contains(action_id)) {
      active_action_ids_.erase(action_id);
    }
    actions_by_id_.erase(action_id);
    absl::erase_if(
        reactions_,
        [&action_id](
            const std::pair<ReactionId, intrinsic_proto::icon::v1::Reaction>&
                reaction_id_and_reaction) {
          const intrinsic_proto::icon::v1::Reaction& reaction =
              reaction_id_and_reaction.second;
          return (reaction.action_association().action_instance_id() ==
                  action_id.value()) ||
                 (reaction.has_response() &&
                  reaction.response().start_action_instance_id() ==
                      action_id.value());
        });
  }
  for (const ReactionId& reaction_id : action_and_reaction_ids.reaction_ids) {
    if (!reactions_.contains(reaction_id)) {
      return absl::NotFoundError(absl::StrCat(
          "Trying to remove unknown Reaction ", reaction_id.value()));
    }
    reactions_.erase(reaction_id);
  }
  return absl::OkStatus();
}

absl::Status RtclControllerFakeSession::RemoveAllActionsAndReactions() {
  absl::MutexLock lock(mutex_);
  actions_by_id_ = {};
  reactions_ = {};
  active_action_ids_ = {};
  return absl::OkStatus();
}

absl::Status RtclControllerFakeSession::StartActions(
    absl::Span<const ActionInstanceId> action_instance_ids,
    bool stop_active_actions) {
  absl::MutexLock lock(mutex_);
  if (stop_active_actions) {
    StopAllActiveActions();
  }

  for (const ActionInstanceId& action_instance_id : action_instance_ids) {
    if (!actions_by_id_.contains(action_instance_id)) {
      return absl::NotFoundError(absl::StrCat("Trying to start unknown Action ",
                                              action_instance_id.value()));
    }

    INTR_RETURN_IF_ERROR(ActivateNextBehavior());

    if (validation_mode_ == ValidationMode::kStrict) {
      if (!active_action_behavior_.has_value()) {
        return absl::OutOfRangeError(
            "ChannelFake has no active expectations, but the active action "
            "changed. If you want to allow this, use ChannelFake's nice mode.");
      }
      if (active_action_behavior_->action_id.has_value() &&
          active_action_behavior_->action_id != action_instance_id) {
        return absl::FailedPreconditionError(absl::StrCat(
            "ChannelFake expected action ",
            active_action_behavior_->action_id->value(),
            " to become active, but instead action ",
            action_instance_id.value(),
            " became active. If you want to allow this, use ChannelFake's nice "
            "mode."));
      }
    }

    if (active_action_behavior_.has_value()) {
      if (active_action_behavior_->action_id.has_value()) {
        LOG(INFO) << "Starting action with ID " << action_instance_id.value()
                  << " and active action behavior is for action with ID "
                  << active_action_behavior_->action_id->value();
      } else {
        LOG(INFO) << "Starting action with ID " << action_instance_id.value()
                  << " and active behavior is not associated with an action.";
      }
    } else {
      LOG(INFO) << "Starting action with ID " << action_instance_id.value()
                << " with no active behavior.";
    }
    INTR_RETURN_IF_ERROR(UpdateActiveAction(action_instance_id));
  }
  return absl::OkStatus();
}

absl::StatusOr<RtclControllerFakeSession::PublishOutputWithAdioNames>
RtclControllerFakeSession::GeneratePublishOutput() const {
  if (!get_robot_status_) {
    return absl::FailedPreconditionError(
        "No get_robot_status_ function was provided.");
  }
  const intrinsic_proto::icon::v1::GetStatusResponse robot_status =
      get_robot_status_();
  PublishOutputWithAdioNames result;
  if (robot_status.has_safety_status()) {
    result.publish_output.robot_status.safety_status =
        FromProto(robot_status.safety_status());
  }
  std::optional<size_t> max_index;
  for (auto& [part_name, index] : part_name_to_index_map_) {
    if (!max_index.has_value() || *max_index < index) {
      max_index = index;
    }
  }
  if (max_index.has_value()) {
    result.publish_output.robot_status.part_statuses.resize(*max_index + 1);
  }
  // We need to count all ADIO blocks that are needed and reserve the
  // slots for them, since the addresses to those strings are later used in
  // `RealtimePartStatus`.
  size_t adio_block_name_count = 0;
  for (auto& [part_name, part_status_proto] : robot_status.part_status()) {
    if (part_status_proto.has_adio_state()) {
      adio_block_name_count += kADIONumOfBlocks;
    }
  }
  result.adio_block_names.reserve(adio_block_name_count);

  for (auto& [part_name, part_status_proto] : robot_status.part_status()) {
    auto it = part_name_to_index_map_.find(part_name);
    if (it == part_name_to_index_map_.end()) {
      return absl::NotFoundError(
          absl::StrCat("Could not find index for part ", part_name));
    }
    RealtimePartStatus rt_part_status;
    auto get_keys_from_proto_map = [](const auto& map) {
      std::vector<std::string> keys;
      keys.reserve(map.size());
      for (auto& [key, value] : map) {
        keys.push_back(key);
      }
      return keys;
    };

    // Unfortunately we need some special case for the ADIO part since the
    // block names need to be allocated beforehand.
    if (part_status_proto.has_adio_state()) {
      QCHECK(result.adio_block_names.size() + kADIONumOfBlocks <=
             result.adio_block_names.capacity())
          << "There are more ADIO block names needed than "
             "reserved. This is a bug! Reserved: "
          << result.adio_block_names.capacity();

      result.adio_block_names.push_back(get_keys_from_proto_map(
          part_status_proto.adio_state().analog_inputs()));
      result.adio_block_names.push_back(get_keys_from_proto_map(
          part_status_proto.adio_state().analog_outputs()));
      result.adio_block_names.push_back(get_keys_from_proto_map(
          part_status_proto.adio_state().digital_inputs()));
      result.adio_block_names.push_back(get_keys_from_proto_map(
          part_status_proto.adio_state().digital_outputs()));
      INTR_ASSIGN_OR_RETURN(
          rt_part_status,
          FromProto(part_status_proto, &result.adio_block_names.rbegin()[3],
                    &result.adio_block_names.rbegin()[2],
                    &result.adio_block_names.rbegin()[1],
                    &result.adio_block_names.rbegin()[0]));
    } else {
      INTR_ASSIGN_OR_RETURN(rt_part_status, FromProto(part_status_proto));
    }
    if (it->second >= result.publish_output.robot_status.part_statuses.size()) {
      return absl::OutOfRangeError(absl::StrCat(
          "Part '", part_name, "' has index ", it->second,
          " that is out of range (",
          result.publish_output.robot_status.part_statuses.size(), ")"));
    }
    result.publish_output.robot_status.part_statuses[it->second] =
        std::move(rt_part_status);
  }
  return result;
}

absl::StatusOr<std::optional<ReactionEvent>>
RtclControllerFakeSession::PollReactions() {
  absl::MutexLock lock(mutex_);
  INTR_ASSIGN_OR_RETURN(OperationalStatus operational_status,
                        operational_state_.GetStatus());

  switch (operational_status.state()) {
    case OperationalState::kFaulted:
      return absl::AbortedError(absl::StrCat(
          "The robot faulted. ", operational_status.fault_reason()));
    case OperationalState::kDisabled:
      return absl::AbortedError("The robot is disabled.");
    case OperationalState::kEnabled:
      break;
  }

  if (!outstanding_reactions_.empty()) {
    ReactionEvent event = outstanding_reactions_.back();
    outstanding_reactions_.pop_back();
    return event;
  }

  bool activate_next_behavior = false;
  // Do action expectationy stuff here.
  if (validation_mode_ == ValidationMode::kStrict) {
    if (!active_action_ids_.empty() && !active_action_behavior_.has_value()) {
      return absl::FailedPreconditionError(
          "There is an active action, but no behavior is specified. If you "
          "want to allow this, use ChannelFake's nice mode.");
    }
    if (active_action_behavior_.has_value() &&
        active_action_behavior_->action_id.has_value() &&
        !active_action_ids_.contains(*active_action_behavior_->action_id)) {
      return absl::FailedPreconditionError(
          absl::StrCat("The next action expectation is for action with ID ",
                       active_action_behavior_->action_id->value(),
                       " but there is no active action with that ID. If you "
                       "want to allow this, use ChannelFake's nice mode."));
    }
    if (active_action_behavior_.has_value() &&
        active_action_behavior_->action_id.has_value() &&
        active_action_ids_.empty()) {
      return absl::FailedPreconditionError(
          "There is no active action, but an action behavior is scheduled. "
          "This is likely a bug. You can disable this check by using "
          "ChannelFake's nice mode.");
    }
  }

  // Instantiate ActionWill empty -> all entries are optional and nothing is
  // expected if not set later.
  ActionWill behavior;
  std::optional<ActionInstanceId> action_id_of_active_behavior;
  if (active_action_behavior_.has_value() &&
      (!active_action_behavior_->action_id.has_value() ||
       (active_action_ids_.contains(*active_action_behavior_->action_id)))) {
    behavior = active_action_behavior_->action_behaviors;
    action_id_of_active_behavior = active_action_behavior_->action_id;
    if (!active_action_behavior_->action_id.has_value()) {
      // If the behavior is not associated with an action, the active behavior
      // is used up in this cycle -> activate next action behavior.
      activate_next_behavior = true;
    }
  }

  if (behavior.GetRobotStatus().has_value()) {
    update_robot_status_(*behavior.GetRobotStatus());
  }

  if (auto new_operational_status = behavior.GetOperationalStatus();
      new_operational_status.has_value()) {
    const ::intrinsic_proto::icon::v1::OperationalStatus
        next_operational_status = new_operational_status->operational_status();
    // Bring the server's fake operational state into the desired state.
    switch (next_operational_status.state()) {
      case (intrinsic_proto::icon::v1::DISABLED):
        LOG(INFO) << "Action expectation is switching OperationalState to "
                     "DISABLED";
        INTR_RETURN_IF_ERROR(operational_state_.ClearFaults());
        // This is redundant in our current implementation, but might future
        // proof any changes to the operational state.
        INTR_RETURN_IF_ERROR(
            operational_state_.Disable(/*skip_cell_control_hardware=*/false));
        break;
      case (intrinsic_proto::icon::v1::ENABLED):
        LOG(INFO) << "Action expectation is switching OperationalState to "
                     "ENABLED";
        INTR_RETURN_IF_ERROR(operational_state_.ClearFaults());
        INTR_RETURN_IF_ERROR(operational_state_.Enable());
        break;
      case (intrinsic_proto::icon::v1::FAULTED):
        LOG(INFO) << "Action expectation is switching OperationalState to "
                     "FAULTED";
        operational_state_.Fault(next_operational_status.fault_reason());
        break;
      default:
        break;
    }
  }

  INTR_ASSIGN_OR_RETURN(const auto publish_output_with_names,
                        GeneratePublishOutput());

  ::testing::NiceMock<MockRtclAction> mock_action;
  ON_CALL(mock_action, GetStateVariableMock)
      .WillByDefault([](absl::string_view variable_name) {
        return FailedPreconditionError(RealtimeStatus::StrCat(
            "No action statevariables were registered. Requested variable: ",
            variable_name));
      });

  if (action_id_of_active_behavior.has_value() &&
      active_action_ids_.contains(*action_id_of_active_behavior)) {
    auto action = actions_by_id_.find(*action_id_of_active_behavior);

    if (action == actions_by_id_.end()) {
      return absl::InternalError(
          absl::StrCat("ChannelFake is trying to evaluate an action that does "
                       "not exist. Requested action ID: ",
                       action_id_of_active_behavior->value()));
    }

    ActionData& action_data = action->second;
    if (action_data.poll_reactions_count <
        behavior.InOrderStateVariableMaps().size()) {
      ON_CALL(mock_action, GetStateVariableMock)
          .WillByDefault([state_variable_map =
                              behavior.InOrderStateVariableMaps().at(
                                  action_data.poll_reactions_count),
                          active_action_id = *action_id_of_active_behavior](
                             absl::string_view variable_name)
                             -> RealtimeStatusOr<StateVariableValue> {
            if (auto value = state_variable_map.find(variable_name);
                value != state_variable_map.end()) {
              std::string value_string = std::visit(
                  [](const auto& value) { return absl::StrCat(value); },
                  value->second);
              LOG(INFO) << "Returning action state variable " << variable_name
                        << " with value " << value_string << " for action "
                        << active_action_id.value();
              return value->second;
            }
            return UnavailableError(RealtimeStatus::StrCat(
                "state variable '", variable_name, "' is not in provided map"));
          });
    }

    if (action_data.poll_reactions_count <
        behavior.OrderedStreamingOutputs().size()) {
      INTR_RETURN_IF_ERROR(
          WriteOutput(behavior.OrderedStreamingOutputs().at(
                          action_data.poll_reactions_count),
                      action_data.io_storage->output_channel_.get()));
    }

    // Move forward in the list of state variable maps, so that we trigger new
    // reactions the next time this action is active when PollReactions is
    // called. This also advances the streaming output index.
    action_data.poll_reactions_count++;

    // When there is no StateVariableMap left to process and no streaming
    // outputs left to process, this behavior is done.
    if (action_data.poll_reactions_count >=
            behavior.InOrderStateVariableMaps().size() &&
        action_data.poll_reactions_count >=
            behavior.OrderedStreamingOutputs().size()) {
      LOG_EVERY_N_SEC(INFO, 1)
          << "Action " << action_id_of_active_behavior->value()
          << " used all state variables and streaming "
             "outputs. Number of StateVariableMaps: "
          << behavior.InOrderStateVariableMaps().size()
          << ". Number of StreamingOutputs: "
          << behavior.OrderedStreamingOutputs().size();
      // Only activate next behavior if it is a non action behavior or a
      // behaviour for a pre-running action. If it is an action behavior for a
      // new starting action, it will be activated by the reaction or
      // StartAction().
      if (!ordered_action_behaviors_.empty() &&
          (!ordered_action_behaviors_.front().action_id.has_value() ||
           (ordered_action_behaviors_.front().action_id.has_value() &&
            ordered_action_behaviors_.front().is_already_running_action))) {
        activate_next_behavior = true;
      }

      // We need to check if there are any leftover expected streaming inputs
      // before switching actions.
      if (validation_mode_ == ValidationMode::kStrict &&
          action_data.next_streaming_input_parameter_matcher_index <
              behavior.GetOrderedStreamingInputParameterMatchers().size()) {
        return absl::FailedPreconditionError(
            "Validation mode is strict, but not all expected streaming inputs "
            "were detected before switching actions.");
      }
    }
  }

  absl::flat_hash_set<ActionInstanceId> next_action_ids;
  absl::flat_hash_set<ActionInstanceId> stopped_action_ids;

  // active_action_id_ might get changed in the loop, so make a copy of the
  // action ID here and use that.
  const auto currently_active_action_ids = active_action_ids_;
  for (const auto& [reaction_id, reaction] : reactions_) {
    if (reaction.fire_once() && reactions_fired_.contains(reaction_id)) {
      continue;
    }
    if (reaction.has_action_association() &&
        !currently_active_action_ids.contains(ActionInstanceId(
            reaction.action_association().action_instance_id()))) {
      std::string log_message = absl::StrCat(
          "Skipping reaction ", reaction_id.value(),
          " with action association ",
          reaction.action_association().action_instance_id(),
          " because it is not in the list of currently active actions: ");
      for (const auto& action_id : currently_active_action_ids) {
        absl::StrAppend(&log_message, " ", action_id.value());
      }
      LOG_EVERY_N_SEC(INFO, 1) << log_message;
      continue;
    }
    INTR_ASSIGN_OR_RETURN(
        RealtimeCondition condition,
        RealtimeConditionFromProto(
            reaction.condition(), part_name_to_index_map_,
            publish_output_with_names.publish_output.robot_status));
    RealtimeStatusOr<bool> matches_or = LookupAndEvaluate(
        mock_action, condition,
        publish_output_with_names.publish_output.robot_status);
    if (!matches_or.ok()) {
      LOG_EVERY_N_SEC(WARNING, 1)
          << "Error while evaluating condition: " << matches_or.status();

      matches_or = false;
    }

    // It's a rising edge if the condition matches and the reaction is evaluated
    // for the first time or in the previous cycle it was false.
    const bool rising_edge =
        matches_or.value() &&
        (!previous_reaction_states_.contains(reaction_id) ||
         !previous_reaction_states_[reaction_id]);
    previous_reaction_states_[reaction_id] = matches_or.value();
    // If the reaction fires, create a ReactionEvent.
    if (rising_edge) {
      ReactionEvent reaction_event{
          .id = reaction_id,
          .previous_action_id = std::nullopt,
          .current_action_id = std::nullopt,
      };
      if (reaction.has_action_association()) {
        reaction_event.previous_action_id = ActionInstanceId(
            reaction.action_association().action_instance_id());
        reaction_event.current_action_id = ActionInstanceId(
            reaction.action_association().action_instance_id());
      }
      // Overwrite `current_action_id` with `start_action_instance_id`, but only
      // if we have not changed the active action already while evaluating this
      // state variable map.
      if (reaction.has_response()) {
        activate_next_behavior = true;
        next_action_ids.insert(
            ActionInstanceId(reaction.response().start_action_instance_id()));
        reaction_event.current_action_id =
            ActionInstanceId(reaction.response().start_action_instance_id());
      }
      if (reaction.action_association().stop_associated_action()) {
        stopped_action_ids.insert(ActionInstanceId(
            reaction.action_association().action_instance_id()));
      }

      LOG(INFO)
          << "Reaction with id " << reaction_event.id.value()
          << " fired. Previously active action: "
          << (reaction_event.previous_action_id.has_value()
                  ? std::to_string(reaction_event.previous_action_id->value())
                  : "<none>")
          << " Now active action: "
          << (reaction_event.current_action_id.has_value()
                  ? std::to_string(reaction_event.current_action_id->value())
                  : "<none>");

      if (reaction.fire_once()) {
        const bool inserted = reactions_fired_.insert(reaction_id).second;
        // Since `fire_once` is set, only report it when we have inserted it
        // into the set of fired reactions.
        if (inserted) {
          outstanding_reactions_.push_back(reaction_event);
        }
      } else {
        outstanding_reactions_.push_back(reaction_event);
      }
    }
  }
  // Mirror ICON's behavior: activating multiple actions that claim the same
  // part in the same cycle is rejected with RESOURCE_EXHAUSTED, so tests catch
  // such conflicts instead of silently passing.
  absl::flat_hash_map<absl::string_view, ActionInstanceId> part_to_action;
  for (const ActionInstanceId& next_action_id : next_action_ids) {
    const auto action_it = actions_by_id_.find(next_action_id);
    if (action_it == actions_by_id_.end()) {
      continue;
    }
    for (const auto& [slot_name, part_name] :
         action_it->second.description.slot_part_map) {
      const auto [inserted_it, inserted] =
          part_to_action.try_emplace(part_name, next_action_id);
      if (!inserted && inserted_it->second != next_action_id) {
        return absl::ResourceExhaustedError(absl::StrCat(
            "Part '", part_name,
            "' is requested by multiple actions during reaction handling: ",
            inserted_it->second.value(), " and ", next_action_id.value()));
      }
    }
  }

  if (activate_next_behavior) {
    INTR_RETURN_IF_ERROR(ActivateNextBehavior());
  }
  for (const auto& stopped_action_id : stopped_action_ids) {
    StopActiveAction(stopped_action_id);
  }
  for (const auto& next_action_id : next_action_ids) {
    INTR_RETURN_IF_ERROR(UpdateActiveAction(next_action_id));
  }

  // Repeat the logic from the top of the function, so that callers do not have
  // to wait an extra cycle to receive their first ReactionEvent.
  if (outstanding_reactions_.empty()) {
    return std::nullopt;
  }
  ReactionEvent event = outstanding_reactions_.back();
  outstanding_reactions_.pop_back();
  return event;
}

absl::Status RtclControllerFakeSession::WriteToStreamingInput(
    ActionInstanceId action_instance_id, absl::string_view input_name,
    const google::protobuf::Any& value) {
  absl::MutexLock lock(mutex_);
  auto id_and_action = actions_by_id_.find(action_instance_id);
  if (id_and_action == actions_by_id_.end()) {
    return absl::NotFoundError(absl::StrCat(
        "Action with ID ", action_instance_id.value(), " not found."));
  }
  if (active_action_behavior_.has_value()) {
    absl::Span<const std::function<absl::Status(const google::protobuf::Any&)>>
        ordered_streaming_input_parameter_matchers =
            active_action_behavior_->action_behaviors
                .GetOrderedStreamingInputParameterMatchers();
    if (id_and_action->second.next_streaming_input_parameter_matcher_index <
        ordered_streaming_input_parameter_matchers.size()) {
      std::function<absl::Status(const google::protobuf::Any&)>
          parameter_matcher = ordered_streaming_input_parameter_matchers.at(
              id_and_action->second
                  .next_streaming_input_parameter_matcher_index);
      id_and_action->second.next_streaming_input_parameter_matcher_index++;
      INTR_RETURN_IF_ERROR(parameter_matcher(value));
    } else if (validation_mode_ == ValidationMode::kStrict) {
      return absl::FailedPreconditionError(
          "Validation mode is strict, but no streaming input parameter matcher "
          "was found for the streaming input.");
    }
  }
  return WriteStreamingInputAny(input_name, value,
                                *id_and_action->second.io_storage);
}

absl::StatusOr<::intrinsic_proto::icon::StreamingOutput>
RtclControllerFakeSession::GetLatestStreamingOutput(
    ActionInstanceId action_instance_id, absl::Time deadline) {
  absl::MutexLock lock(mutex_);
  auto id_and_action = actions_by_id_.find(action_instance_id);
  if (id_and_action == actions_by_id_.end()) {
    return absl::NotFoundError(absl::StrCat(
        "Action with ID ", action_instance_id.value(), " not found."));
  }
  INTR_ASSIGN_OR_RETURN(
      StreamingOutputWithCycle output_with_cycle,
      PollStreamingOutputAny(deadline, *id_and_action->second.io_storage));
  return output_with_cycle.output;
}

absl::StatusOr<::intrinsic_proto::icon::JointTrajectoryPVA>
RtclControllerFakeSession::GetPlannedTrajectory(
    ActionInstanceId action_instance_id) {
  absl::MutexLock lock(mutex_);
  if (auto id_and_trajectory = trajectory_map_.find(action_instance_id);
      id_and_trajectory == trajectory_map_.end()) {
    return absl::NotFoundError(absl::StrCat("Action with ID ",
                                            action_instance_id.value(),
                                            " has not saved a trajectory."));
  } else {
    return ToProto(id_and_trajectory->second);
  }
}

PartGroup RtclControllerFakeSession::GetPartGroup() const {
  absl::MutexLock lock(mutex_);
  PartGroup part_group;
  for (const auto& [part_name, _] : part_configs_by_name_) {
    part_group.insert(part_name);
  }
  return part_group;
}

absl::flat_hash_set<ActionInstanceId>
RtclControllerFakeSession::GetActionInstanceIds() const {
  absl::MutexLock lock(mutex_);
  absl::flat_hash_set<ActionInstanceId> action_instance_ids;
  for (const auto& [action_id, _] : actions_by_id_) {
    action_instance_ids.insert(action_id);
  }
  return action_instance_ids;
}

absl::Status RtclControllerFakeSession::ActivateNextBehavior()
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_) {
  INTR_RETURN_IF_ERROR(ResetActiveBehaviorIfDone());

  if (ordered_action_behaviors_.empty()) {
    LOG(INFO) << "No more action behaviors left to activate";
    return absl::OkStatus();
  }

  active_action_behavior_ = ordered_action_behaviors_.front();
  ordered_action_behaviors_.pop_front();
  if (active_action_behavior_->action_id.has_value()) {
    LOG(INFO) << "Activating next behavior for action with ID "
              << active_action_behavior_->action_id->value();
    if (active_action_behavior_->is_already_running_action) {
      LOG(INFO) << "Action with ID "
                << active_action_behavior_->action_id->value()
                << " is a pre-running action so will reset the poll reactions "
                   "count and next_streaming_input_parameter_matcher_index";
      auto action = actions_by_id_.find(*active_action_behavior_->action_id);
      if (action == actions_by_id_.end()) {
        return absl::InternalError(absl::StrCat(
            "ChannelFake is trying to evaluate an action that does "
            "not exist. Requested action ID: ",
            active_action_behavior_->action_id->value()));
      }
      ActionData& action_data = action->second;
      action_data.poll_reactions_count = 0;
      action_data.next_streaming_input_parameter_matcher_index = 0;
    }
  } else {
    LOG(INFO) << "Activating next behavior for an action-independent change.";
  }

  return absl::OkStatus();
}

absl::Status RtclControllerFakeSession::WriteOutput(
    const std::any& output, StreamingOutputChannel* output_channel) {
  if (output_channel == nullptr) {
    return icon::FailedPreconditionError(
        "The StreamingOutputChannel is null. Ensure an output converter was "
        "registered in the action factory.");
  }
  // Copy to the output buffer.
  if (auto* buffer_ptr = output_channel->output_buffer_.GetFreeBuffer();
      buffer_ptr == nullptr) {
    return icon::InternalError("Null buffer");
  } else {
    buffer_ptr->payload = output;
    output_channel->output_buffer_.CommitFreeBuffer();
  }

  // Copy to the output queue.
  {
    StreamingOutputChannel::PayloadAndTimestamp* output_buffer =
        output_channel->output_queue_.writer()->PrepareInsert();
    // output_buffer could be nullptr here which means the queue is full. If
    // this happens we'll just drop the update.
    if (output_buffer != nullptr) {
      output_buffer->payload = output;
      output_channel->output_queue_.writer()->FinishInsert();
    }
  }

  return icon::OkStatus();
}

absl::Status RtclControllerFakeSession::ResetActiveBehaviorIfDone()
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_) {
  if (validation_mode_ == ValidationMode::kStrict &&
      active_action_behavior_.has_value() &&
      active_action_behavior_->action_id.has_value()) {
    auto it = actions_by_id_.find(*active_action_behavior_->action_id);
    if (it->second.poll_reactions_count <
        active_action_behavior_->action_behaviors.InOrderStateVariableMaps()
            .size()) {
      return absl::FailedPreconditionError(
          "Validation mode is strict, but not all Action StateVariableMaps "
          "have been used! The last supplied StateVariableMap must trigger "
          "the reaction, not an earlier StateVariableMap. Also do not call "
          "StartAction() before using up all Action StateVariableMaps.");
    }
  }
  active_action_behavior_.reset();
  return absl::OkStatus();
}

}  // namespace intrinsic::icon
