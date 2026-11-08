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

#include "intrinsic/icon/server/fake_robot_connection.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/algorithm/container.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "intrinsic/icon/cc_client/operational_status.h"
#include "intrinsic/icon/common/id_types.h"
#include "intrinsic/icon/common/slot_part_map.h"
#include "intrinsic/icon/control/logging_mode.h"
#include "intrinsic/icon/control/parts/realtime_log_context.h"
#include "intrinsic/icon/proto/v1/types.pb.h"
#include "intrinsic/icon/server/example/noop_action.h"
#include "intrinsic/icon/server/operational_state_interface.h"
#include "intrinsic/icon/server/part_collection.h"
#include "intrinsic/icon/server/session_interface.h"
#include "intrinsic/icon/server/signature_utils.h"
#include "intrinsic/icon/server/testing/fake_operational_state.h"
#include "intrinsic/icon/server/testing/fake_part_collection.h"
#include "intrinsic/icon/server/testing/fake_session.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic::icon {

FakeRobotConnection::FakeRobotConnection(
    const intrinsic_proto::icon::v1::ServerConfig& config,
    const absl::flat_hash_set<std::string>& part_names)
    : FakeRobotConnection(/*config=*/config,
                          /*part_names=*/part_names,
                          /*action_signatures=*/std::nullopt,
                          /*create_session=*/nullptr,
                          /*operational_state=*/nullptr) {}

FakeRobotConnection::FakeRobotConnection(
    const intrinsic_proto::icon::v1::ServerConfig& config,
    PartCollection part_collection)
    : config_(config),
      part_collection_(std::move(part_collection)),
      action_signatures_({{std::string(kNoopAction), NoopActionSignature()}}),
      create_session_fn_(FakeSession::Create),
      operational_state_interface_(std::make_unique<FakeOperationalState>()) {}

FakeRobotConnection::FakeRobotConnection(
    const intrinsic_proto::icon::v1::ServerConfig& config,
    const absl::flat_hash_set<std::string>& part_names,
    absl::flat_hash_map<std::string, intrinsic_proto::icon::v1::ActionSignature>
        action_signatures)
    : FakeRobotConnection(/*config=*/config,
                          /*part_names=*/part_names,
                          /*action_signatures=*/std::move(action_signatures),
                          /*create_session=*/nullptr,
                          /*operational_state=*/nullptr) {}

FakeRobotConnection::FakeRobotConnection(
    const intrinsic_proto::icon::v1::ServerConfig& config,
    const absl::flat_hash_set<std::string>& part_names,
    ::intrinsic::icon::CreateSession create_session)
    : FakeRobotConnection(
          /*config=*/config,
          /*part_names=*/part_names,
          /*action_signatures=*/std::nullopt,
          /*create_session=*/std::move(create_session),
          /*operational_state=*/nullptr) {}

FakeRobotConnection::FakeRobotConnection(
    const intrinsic_proto::icon::v1::ServerConfig& config,
    const absl::flat_hash_set<std::string>& part_names,
    std::shared_ptr<OperationalStateInterface> operational_state)
    : FakeRobotConnection(/*config=*/config,
                          /*part_names=*/part_names,
                          /*action_signatures=*/std::nullopt,
                          /*create_session=*/nullptr,
                          /*operational_state=*/operational_state) {}

FakeRobotConnection::FakeRobotConnection(
    const intrinsic_proto::icon::v1::ServerConfig& config,
    const absl::flat_hash_set<std::string>& part_names,
    std::optional<absl::flat_hash_map<
        std::string, intrinsic_proto::icon::v1::ActionSignature>>
        action_signatures,
    ::intrinsic::icon::CreateSession create_session,
    std::shared_ptr<OperationalStateInterface> operational_state)
    : config_(config),
      part_collection_(FakePartCollectionNamesOnly(part_names).value()),
      action_signatures_(
          action_signatures == std::nullopt
              ? absl::flat_hash_map<
                    std::string, intrinsic_proto::icon::v1::
                                     ActionSignature>{{std::string(kNoopAction),
                                                       NoopActionSignature()}}
              : std::move(action_signatures.value())),
      create_session_fn_(create_session == nullptr ? FakeSession::Create
                                                   : std::move(create_session)),
      operational_state_interface_(
          (operational_state == nullptr)
              ? std::make_unique<FakeOperationalState>()
              : std::move(operational_state)) {}

absl::StatusOr<std::unique_ptr<SessionInterface>>
FakeRobotConnection::CreateSession(
    const absl::flat_hash_set<std::string>& part_names, SessionId session_id,
    const RealtimeLogContext& context) {
  absl::MutexLock l(create_session_mutex_);
  return create_session_fn_(part_names, session_id);
}

OperationalStateInterface&
FakeRobotConnection::MutableOperationalStateInterface() {
  return *operational_state_interface_;
}

absl::flat_hash_map<std::string, intrinsic_proto::icon::v1::ActionSignature>
FakeRobotConnection::ActionTypeToSignature() const {
  return action_signatures_;
}

PartCollection& FakeRobotConnection::MutablePartCollection() {
  return part_collection_;
}

const PartCollection& FakeRobotConnection::GetPartCollection() const {
  return part_collection_;
}

const intrinsic_proto::icon::v1::ServerConfig& FakeRobotConnection::config()
    const {
  return config_;
}

absl::Status FakeRobotConnection::ActionCompatibleWithSlotPartMap(
    absl::string_view action_type_name,
    const SlotPartMap& slot_part_map) const {
  auto name_and_signature = action_signatures_.find(action_type_name);
  if (name_and_signature == action_signatures_.end()) {
    return absl::NotFoundError(
        absl::StrCat("Action type '", action_type_name,
                     "' is unknown. Did you register it to your ChannelFake?"));
  }
  return ::intrinsic::icon::ActionCompatibleWithSlotPartMap(
      slot_part_map, name_and_signature->second,
      part_collection_.GetPartConfigs());
}

absl::Status FakeRobotConnection::ActionCompatibleWithPart(
    absl::string_view action_type_name, absl::string_view part_name) const {
  auto name_and_signature = action_signatures_.find(action_type_name);
  if (name_and_signature == action_signatures_.end()) {
    return absl::NotFoundError(
        absl::StrCat("Action type '", action_type_name,
                     "' is unknown. Did you register it to your ChannelFake?"));
  }
  const auto part_configs = part_collection_.GetPartConfigs();
  auto part_config = absl::c_find_if(
      part_configs,
      [part_name](const intrinsic_proto::icon::v1::PartConfig& part_config) {
        LOG(INFO) << "Trying to match " << part_config.name() << " to "
                  << part_name << ". Result: "
                  << (part_config.name() == part_name ? "true" : "false");
        return part_config.name() == part_name;
      });
  if (part_config == part_configs.end()) {
    return absl::NotFoundError(
        absl::StrCat("Could not find config for Part '", part_name, "'"));
  }
  // Try to find (at least) one Slot for which the Part in question has *all*
  // required FeatureInterfaces.
  absl::flat_hash_map<std::string, SlotPartCompatibility>
      slot_compatibility_map;
  for (const auto& [slot_name, slot_info] :
       name_and_signature->second.part_slot_infos()) {
    if (const SlotPartCompatibility compatibility =
            PartCompatibleWithSlot(*part_config, slot_info);
        compatibility.Compatible()) {
      return absl::OkStatus();
    } else {
      slot_compatibility_map[slot_name] = compatibility;
    }
  }

  return absl::InvalidArgumentError(absl::StrCat(
      "Part '", part_name,
      "' is not compatible with any of the Slots for Action type '",
      action_type_name, "'. Available Slots: [",
      absl::StrJoin(name_and_signature->second.part_slot_infos(), ", ",
                    [](std::string* str, const auto& slot_name_and_info) {
                      absl::StrAppend(str, slot_name_and_info.first);
                    }),
      // Appends the explanations why the part is not compatible to the
      // respective slots.
      "] Detailed Explanation: ",
      absl::StrJoin(
          slot_compatibility_map, ", ",
          [](std::string* str, const auto& slot_name_and_compatibility) {
            absl::string_view slot_name = slot_name_and_compatibility.first;
            const SlotPartCompatibility& compatibility =
                slot_name_and_compatibility.second;
            if (compatibility.missing_required_interfaces.empty() &&
                !compatibility.missing_optional_interfaces.empty()) {
              absl::StrAppend(
                  str, "Slot '", slot_name,
                  "' requires at least one of these optional interfaces "
                  "[",
                  absl::StrJoin(compatibility.missing_optional_interfaces,
                                ", "),
                  "]. ");
            }
            if (!compatibility.missing_required_interfaces.empty()) {
              absl::StrAppend(
                  str,
                  "The part is missing the following required interfaces for "
                  "slot '",
                  slot_name, "' [",
                  absl::StrJoin(compatibility.missing_required_interfaces,
                                ", "),
                  "]. ");
            }
          }),
      "]"));
}

void FakeRobotConnection::SetSpeedOverride(double new_speed_override) {
  absl::MutexLock lock(speed_override_mutex_);
  speed_override_ = new_speed_override;
}

double FakeRobotConnection::GetSpeedOverride() const {
  absl::MutexLock lock(speed_override_mutex_);
  return speed_override_;
}

void FakeRobotConnection::SetLoggingMode(LoggingMode logging_mode) {
  absl::MutexLock lock(logging_mode_mutex_);
  logging_mode_ = logging_mode;
}

LoggingMode FakeRobotConnection::GetLoggingMode() const {
  absl::MutexLock lock(logging_mode_mutex_);
  return logging_mode_;
}

absl::StatusOr<absl::flat_hash_map<std::string, OperationalState>>
FakeRobotConnection::GetPartStates() {
  INTR_ASSIGN_OR_RETURN(OperationalStatus status,
                        operational_state_interface_->GetStatus());
  const auto part_configs = part_collection_.GetPartConfigs();
  absl::flat_hash_map<std::string, OperationalState> result;
  for (const auto& part_config : part_configs) {
    result[part_config.name()] = status.state();
  }
  return result;
}

}  // namespace intrinsic::icon
