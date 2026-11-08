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

#include "intrinsic/icon/control/actions/action_test_helper.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/any.pb.h"
#include "intrinsic/icon/common/id_types.h"
#include "intrinsic/icon/common/slot_part_map.h"
#include "intrinsic/icon/control/action_factory_context.h"
#include "intrinsic/icon/control/actions/test_helpers.h"
#include "intrinsic/icon/control/fake_realtime_slot_map_impl.h"
#include "intrinsic/icon/control/parts/feature_interface_registry.h"
#include "intrinsic/icon/control/parts/realtime_part_interface.h"
#include "intrinsic/icon/control/realtime_signal_storage.h"
#include "intrinsic/icon/control/realtime_signal_types.h"
#include "intrinsic/icon/control/rtcl_action.h"
#include "intrinsic/icon/control/rtcl_action_factory_registry.h"
#include "intrinsic/icon/control/slot_types.h"
#include "intrinsic/icon/control/streaming_io_storage.h"
#include "intrinsic/icon/proto/v1/types.pb.h"
#include "intrinsic/kinematics/types/joint_trajectories.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic::icon {
namespace {

constexpr ActionInstanceId kActionInstanceId(23948);

// Adds RealtimeSlotId and (if necessary) an auto-generated PartConfig proto to
// `part_map`.
// Returns an error if any part's entry in `part_map` has a config proto whose
// `name` field does not match the corresponding map key.
absl::StatusOr<
    absl::flat_hash_map<std::string, ActionTestHelper::PartAndSlotInfo>>
MakePartNameToPartAndConfigAndSlotIdMap(
    const absl::flat_hash_map<std::string, ActionTestHelper::PartAndConfig>&
        part_map) {
  absl::flat_hash_map<std::string, ActionTestHelper::PartAndSlotInfo> map;
  RealtimeSlotId next_id(1);
  for (const auto& [part_name, part_and_config] : part_map) {
    if (part_and_config.config.has_value() &&
        part_and_config.config->name() != part_name) {
      return absl::InvalidArgumentError(absl::StrCat(
          "ActionTestHelper got conflicting part names. Part map key is '",
          part_name, "', but the config proto says that the part name is '",
          part_and_config.config->name(), "'"));
    }
    map.emplace(
        part_name,
        ActionTestHelper::PartAndSlotInfo{
            .part = part_and_config.part,
            .slot_info = {.config = part_and_config.config.value_or(
                              PartConfigFromPart(part_name,
                                                 part_and_config.part)),
                          .slot_id = next_id},
            .read_status_params = std::move(part_and_config.read_status_params),
            .apply_command_params =
                std::move(part_and_config.apply_command_params),
        });
    // Increment slot id.
    next_id = RealtimeSlotId(next_id.value() + 1);
  }
  return map;
}

// Generates a map from RealtimeSlotId to FeatureInterfaceRegistry for the given
// PartAndSlotInfo objects.
absl::flat_hash_map<RealtimeSlotId,
                    std::reference_wrapper<FeatureInterfaceRegistry>>
GetSlotIdToFeatureInterfaceMap(
    absl::flat_hash_map<std::string, ActionTestHelper::PartAndSlotInfo>&
        part_data_map) {
  absl::flat_hash_map<RealtimeSlotId,
                      std::reference_wrapper<FeatureInterfaceRegistry>>
      map;
  for (auto& [_, part_and_slot_info] : part_data_map) {
    map.emplace(part_and_slot_info.slot_info.slot_id,
                std::ref(part_and_slot_info.part.GetFeatureInterfaces()));
  }
  return map;
}

absl::StatusOr<SlotPartMap> SinglePartSlotMap(
    absl::string_view action_type_name, absl::string_view part_name) {
  auto action_signature =
      GetGlobalRtclActionFactoryRegistry().GetSignature(action_type_name);
  if (!action_signature.ok()) {
    return absl::NotFoundError(absl::StrCat(
        "No Signature registered for action type '", action_type_name,
        "'. Did you link in your action's _register target *and* mark it with "
        "`alwayslink = True`?"));
  }
  if (action_signature->part_slot_infos().size() != 1) {
    return absl::FailedPreconditionError(absl::StrCat(
        "The signature for action type '", action_type_name, "' has ",
        action_signature->part_slot_infos().size(),
        " slots. You can only call ActionTestHelper::CreateAction() with a "
        "single part name if the action type has a single slot."));
  }
  return SlotPartMap{
      {action_signature->part_slot_infos().begin()->first,
       std::string(part_name)},
  };
}

}  // namespace

// static
absl::StatusOr<ActionTestHelper> ActionTestHelper::Create(
    absl::string_view action_type_name,
    const ::google::protobuf::Message& params, const SlotPartMap& slot_part_map,
    absl::flat_hash_map<std::string, PartAndConfig> part_map,
    double frequency_hz) {
  google::protobuf::Any any_params;
  if (!any_params.PackFrom(params)) {
    return absl::InternalError("Failed to pack action params into Any");
  }

  return ActionTestHelper::CreateImpl(action_type_name, any_params,
                                      slot_part_map, std::move(part_map),
                                      frequency_hz);
}

absl::StatusOr<ActionTestHelper> ActionTestHelper::Create(
    absl::string_view action_type_name,
    const ::google::protobuf::Message& params, absl::string_view part_name,
    absl::flat_hash_map<std::string, PartAndConfig> part_map,
    double frequency_hz) {
  INTR_ASSIGN_OR_RETURN(SlotPartMap slot_part_map,
                        SinglePartSlotMap(action_type_name, part_name));
  return Create(action_type_name, params, std::move(slot_part_map), part_map,
                frequency_hz);
}

absl::StatusOr<ActionTestHelper> ActionTestHelper::Create(
    absl::string_view action_type_name, const SlotPartMap& slot_part_map,
    absl::flat_hash_map<std::string, PartAndConfig> part_map,
    double frequency_hz) {
  return ActionTestHelper::CreateImpl(action_type_name,
                                      ::google::protobuf::Any(), slot_part_map,
                                      std::move(part_map), frequency_hz);
}

absl::StatusOr<ActionTestHelper> ActionTestHelper::Create(
    absl::string_view action_type_name, absl::string_view part_name,
    absl::flat_hash_map<std::string, PartAndConfig> part_map,
    double frequency_hz) {
  INTR_ASSIGN_OR_RETURN(SlotPartMap slot_part_map,
                        SinglePartSlotMap(action_type_name, part_name));

  return Create(action_type_name, std::move(slot_part_map), part_map,
                frequency_hz);
}

ActionTestHelper::ActionTestHelper(
    const intrinsic_proto::icon::v1::ServerConfig server_config,
    absl::flat_hash_map<std::string, PartAndSlotInfo> part_data_map,
    std::unique_ptr<FakeRealtimeSlotMapImpl> fake_slot_map_impl,
    std::unique_ptr<StreamingIoStorage> streaming_io_storage,
    std::unique_ptr<RealtimeSignalStorage> signal_storage,
    std::unique_ptr<absl::flat_hash_map<std::string, RealtimeSignalId>>
        signal_id_map,
    std::unique_ptr<absl::flat_hash_map<ActionInstanceId, JointTrajectoryPVA>>
        trajectory_map,
    std::unique_ptr<RtclActionInterface> action)
    : server_config_(server_config),
      part_data_map_(std::move(part_data_map)),
      fake_slot_map_impl_(std::move(fake_slot_map_impl)),
      rt_slot_map_(*fake_slot_map_impl_),
      streaming_io_storage_(std::move(streaming_io_storage)),
      rt_streaming_io_storage_(FromStreamingIoStorage(*streaming_io_storage_)),
      rt_signal_storage_(std::move(signal_storage)),
      rt_signal_id_map_(std::move(signal_id_map)),
      trajectory_map_(std::move(trajectory_map)),
      action_(std::move(action)) {}

std::optional<JointTrajectoryPVA> ActionTestHelper::GetTrajectory() const {
  auto trajectory = trajectory_map_->find(kActionInstanceId);
  if (trajectory == trajectory_map_->end()) {
    return std::nullopt;
  }
  return trajectory->second;
}

absl::StatusOr<ActionTestHelper> ActionTestHelper::CreateImpl(
    absl::string_view action_type_name,
    const ::google::protobuf::Any& any_params, const SlotPartMap& slot_part_map,
    absl::flat_hash_map<std::string, PartAndConfig> part_map,
    double frequency_hz) {
  INTR_ASSIGN_OR_RETURN(auto part_data_map,
                        MakePartNameToPartAndConfigAndSlotIdMap(part_map));
  auto fake_slot_map_impl = std::make_unique<FakeRealtimeSlotMapImpl>(
      GetSlotIdToFeatureInterfaceMap(part_data_map));

  // Look up signature
  INTR_ASSIGN_OR_RETURN(
      auto action_signature,
      GetGlobalRtclActionFactoryRegistry().GetSignature(action_type_name));

  // Create StreamingIoStorage for this Action.
  auto streaming_io_storage =
      std::make_unique<StreamingIoStorage>(kActionInstanceId, action_signature);

  // Generate the map from Slot name to SlotInfo
  absl::flat_hash_map<std::string, SlotInfo> slot_info_map;
  for (const auto& [slot_name, part_name] : slot_part_map) {
    const auto& part_and_slot_info = part_data_map.find(part_name);
    if (part_and_slot_info == part_data_map.end()) {
      return absl::NotFoundError(absl::StrCat(
          "SlotPartMap tries to map slot '", slot_name, "' to part '",
          part_name, "', but there is no part with that name."));
    }
    slot_info_map[slot_name] = part_and_slot_info->second.slot_info;
  }
  auto trajectory_map = std::make_unique<
      absl::flat_hash_map<ActionInstanceId, JointTrajectoryPVA>>();

  INTR_ASSIGN_OR_RETURN(auto signal_storage_and_id_map,
                        CreateRealtimeSignalStorage(action_signature));
  auto signal_storage = std::make_unique<RealtimeSignalStorage>(
      signal_storage_and_id_map.signal_storage);
  auto signal_id_map =
      std::make_unique<absl::flat_hash_map<std::string, RealtimeSignalId>>(
          signal_storage_and_id_map.signal_id_map);
  intrinsic_proto::icon::v1::ServerConfig server_config;
  server_config.set_frequency_hz(frequency_hz);
  server_config.set_name("ActionTestHelper");
  // Invoke the Action factory.
  ActionFactoryContext action_factory_context(
      server_config, action_signature, slot_info_map,
      signal_storage_and_id_map.signal_id_map, *streaming_io_storage,
      kActionInstanceId, *trajectory_map);
  INTR_ASSIGN_OR_RETURN(
      std::unique_ptr<RtclActionInterface> action,
      GetGlobalRtclActionFactoryRegistry().CallFactory(
          action_type_name, any_params, action_factory_context));
  INTR_RETURN_IF_ERROR(action_factory_context.Validate());

  return ActionTestHelper(
      server_config, std::move(part_data_map), std::move(fake_slot_map_impl),
      std::move(streaming_io_storage), std::move(signal_storage),
      std::move(signal_id_map), std::move(trajectory_map), std::move(action));
}

absl::Status ActionTestHelper::SetSpeedOverride(double speed_override) {
  if (speed_override < 0.0 || speed_override > 1.0) {
    return absl::InvalidArgumentError(
        absl::StrCat("Speed override value must be between 0 and 1. Got new "
                     "speed override value ",
                     speed_override));
  }
  speed_override_ = speed_override;
  return absl::OkStatus();
}

absl::Status ActionTestHelper::SetRequestedBehaviorOverride(
    intrinsic_proto::icon::v1::BehaviorOverrideRequest
        behavior_override_request) {
  requested_behavior_override_ = behavior_override_request;
  return absl::OkStatus();
}

absl::Status ActionTestHelper::TriggerRealtimeSignal(
    absl::string_view signal_name) {
  if (rt_signal_id_map_ == nullptr || rt_signal_storage_ == nullptr) {
    return absl::FailedPreconditionError(
        "The realtime signal storage was not initialized correctly. Check that "
        "the signal was defined in the action signature.");
  }
  if (!rt_signal_id_map_->contains(signal_name)) {
    return absl::NotFoundError(absl::StrCat(
        "No RealtimeSignalId registered for signal '", signal_name, "'."));
  }
  auto signal_id = rt_signal_id_map_->at(signal_name);
  rt_signal_storage_->signal_values.at(signal_id.value()).current_value = true;
  return absl::OkStatus();
}

}  // namespace intrinsic::icon
