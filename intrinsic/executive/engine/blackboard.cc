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

#include "intrinsic/executive/engine/blackboard.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/base/attributes.h"
#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "google/protobuf/any.pb.h"
#include "intrinsic/executive/clips_cpp/environment.h"
#include "intrinsic/executive/clips_cpp/fact.h"
#include "intrinsic/executive/clips_cpp/protobuf.h"
#include "intrinsic/executive/clips_cpp/value.h"
#include "intrinsic/executive/proto/blackboard_service.pb.h"
#include "intrinsic/util/proto_time.h"
#include "intrinsic/util/status/extended_status.pb.h"
#include "intrinsic/util/status/get_extended_status.h"
#include "intrinsic/util/status/status_builder.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/status/status_specs.h"
#include "intrinsic/util/unique_id.h"

namespace intrinsic::executive {

namespace {
std::string HumanReadableBytes(size_t bytes) {
  if (bytes < 1024) {
    return absl::StrFormat("%d B", bytes);
  }

  const std::vector<const char*> units = {"KiB", "MiB", "GiB", "TiB", "PiB"};
  int unit_index = 0;
  double value = bytes / 1024.0;

  while (value >= 1024.0 && unit_index < units.size() - 1) {
    value /= 1024.0;
    unit_index++;
  }

  return absl::StrFormat("%.1f %s", value, units[unit_index]);
}
}  // namespace

Blackboard::Blackboard(clips::Environment& clips ABSL_ATTRIBUTE_LIFETIME_BOUND,
                       clips::ProtobufManager& proto_mgr
                           ABSL_ATTRIBUTE_LIFETIME_BOUND)
    : clips_(clips), proto_mgr_(proto_mgr) {}

absl::StatusOr<google::protobuf::Any> Blackboard::GetBlackboardValue(
    absl::string_view key, absl::string_view scope,
    absl::string_view operation_name) ABSL_LOCKS_EXCLUDED(clips_.mutex()) {
  absl::MutexLock lock(*clips_.mutex());
  INTR_ASSIGN_OR_RETURN(
      clips::Fact bb_fact,
      clips_.GetUniqueFact(
          "blackboard-item",
          {{"key", key}, {"scope", scope}, {"operation-name", operation_name}}),
      _ << " looking for key " << key << " in scope " << scope);
  INTR_ASSIGN_OR_RETURN(clips::Value blackboard_value_id_val,
                        bb_fact.GetSlotValue("proto-id"));
  INTR_ASSIGN_OR_RETURN(int64_t blackboard_value_proto_id,
                        blackboard_value_id_val.GetInteger());
  if (blackboard_value_proto_id == clips::ProtobufManager::kInvalidId.value()) {
    return absl::NotFoundError(
        absl::StrFormat("Value not found for key %s.", key));
  }
  INTR_ASSIGN_OR_RETURN(
      google::protobuf::Any any,
      proto_mgr_.CastToAny(clips::ProtoMessageId(blackboard_value_proto_id)));

  return any;
}

absl::Status Blackboard::UpdateBlackboardValue(
    absl::string_view key, absl::string_view scope,
    absl::string_view operation_name, const google::protobuf::Any& value)
    ABSL_LOCKS_EXCLUDED(clips_.mutex()) {
  absl::MutexLock lock(*clips_.mutex());

  INTR_ASSIGN_OR_RETURN(
      clips::Fact bb_fact,
      clips_.GetUniqueFact(
          "blackboard-item",
          {{"key", key}, {"scope", scope}, {"operation-name", operation_name}}),
      std::move(_).With(AttachExtendedStatus(
          80001, absl::StrFormat("The blackboard value with "
                                 "key '%s' does not exist in scope '%s'",
                                 key, scope))));
  INTR_ASSIGN_OR_RETURN(clips::Value blackboard_value_id_val,
                        bb_fact.GetSlotValue("proto-id"));
  INTR_ASSIGN_OR_RETURN(int64_t blackboard_value_proto_id,
                        blackboard_value_id_val.GetInteger());

  INTR_ASSIGN_OR_RETURN(
      clips::ProtoMessageId new_message_id,
      proto_mgr_.CreateUpdatedProtoFromAny(
          clips::ProtoMessageId(blackboard_value_proto_id), value),
      std::move(_).With(WrapExtendedStatus(
          80001,
          absl::StrFormat("Could not update blackboard value with "
                          "key '%s' (scope '%s') from Any proto",
                          key, scope),
          intrinsic::StatusBuilder::LEGACY_AS_DEBUG_REPORT)));

  INTR_RETURN_IF_ERROR(clips_
                           .AssertFact("blackboard-update",
                                       {{"key", key},
                                        {"scope", scope},
                                        {"operation-name", operation_name},
                                        {"proto-id", new_message_id.value()}})
                           .status())
      .LogWarning();

  // Trigger CLIPS run
  clips_.NotifyRunner();

  return absl::OkStatus();
}

absl::StatusOr<std::vector<intrinsic_proto::executive::BlackboardValue>>
Blackboard::ListBlackboardValues(
    absl::string_view request_scope, absl::string_view operation_name,
    intrinsic_proto::executive::ListBlackboardValuesRequest::View view)
    ABSL_LOCKS_EXCLUDED(clips_.mutex()) {
  absl::MutexLock lock(*clips_.mutex());
  std::vector<clips::Fact> bb_facts = clips_.QueryFacts(
      "blackboard-item", {{"operation-name", operation_name}});
  std::vector<intrinsic_proto::executive::BlackboardValue> values;
  values.reserve(bb_facts.size());
  for (const auto& fact : bb_facts) {
    INTR_ASSIGN_OR_RETURN(clips::Value key_val, fact.GetSlotValue("key"));
    INTR_ASSIGN_OR_RETURN(std::string key, key_val.GetString());

    INTR_ASSIGN_OR_RETURN(clips::Value scope_val, fact.GetSlotValue("scope"));
    INTR_ASSIGN_OR_RETURN(std::string scope, scope_val.GetString());
    if (!request_scope.empty() && scope != request_scope) {
      continue;
    }

    INTR_ASSIGN_OR_RETURN(clips::Value blackboard_value_id_val,
                          fact.GetSlotValue("proto-id"));
    INTR_ASSIGN_OR_RETURN(int64_t blackboard_value_proto_id,
                          blackboard_value_id_val.GetInteger());
    if (blackboard_value_proto_id ==
        clips::ProtobufManager::kInvalidId.value()) {
      return absl::NotFoundError(absl::StrFormat(
          "Expected value for key %s, scope %s, operation %s not found.", key,
          scope, operation_name));
    }
    INTR_ASSIGN_OR_RETURN(
        google::protobuf::Any value_msg,
        proto_mgr_.CastToAny(clips::ProtoMessageId(blackboard_value_proto_id)));

    intrinsic_proto::executive::BlackboardValue value;
    value.set_key(key);
    value.set_scope(scope);
    value.set_operation_name(operation_name);

    if (view != intrinsic_proto::executive::ListBlackboardValuesRequest::FULL) {
      value_msg.clear_value();
    }
    *value.mutable_value() = std::move(value_msg);

    values.push_back(std::move(value));
  }
  return values;
}

absl::Status Blackboard::DeleteBlackboardValue(absl::string_view key,
                                               absl::string_view scope,
                                               absl::string_view operation_name)
    ABSL_LOCKS_EXCLUDED(clips_.mutex()) {
  absl::MutexLock lock(*clips_.mutex());
  INTR_RETURN_IF_ERROR(
      clips_
          .Evaluate(
              absl::StrFormat(R"clips((blackboard-remove "%s" "%s" "%s"))clips",
                              key, scope, operation_name))
          .status());
  // Trigger CLIPS run
  clips_.NotifyRunner();
  return absl::OkStatus();
}

intrinsic_proto::executive::BlackboardSnapshot Blackboard::Snapshot::ToProto()
    const {
  intrinsic_proto::executive::BlackboardSnapshot snapshot_proto;
  snapshot_proto.set_handle(handle.value());
  snapshot_proto.set_display_name(display_name);
  *snapshot_proto.mutable_save_time() =
      intrinsic::FromAbslTimeClampToValidRange(save_time);
  snapshot_proto.set_estimated_size_bytes(EstimatedSizeBytes());
  snapshot_proto.set_snapshot_source(snapshot_source);
  return snapshot_proto;
}

size_t Blackboard::Snapshot::EstimatedSizeBytes() const {
  size_t size = 0;
  size += handle.value().size();
  size += display_name.size();
  size += source_operation_name.size();

  for (const std::shared_ptr<StoredDescriptorInfo>& pool : pools) {
    if (pool->file_descriptor_set.has_value()) {
      size += pool->file_descriptor_set->ByteSizeLong();
    }
    size += pool->display_name.size();
    size += pool->type_url_prefix.size();
  }
  for (const StoredProto& proto : values) {
    size += proto.key.size();
    size += proto.scope.size();
    size += proto.proto.ByteSizeLong();
  }

  return size;
}

void Blackboard::Snapshot::Dump() const {
  LOG(INFO) << handle.value() << " " << display_name << " "
            << source_operation_name;

  for (const std::shared_ptr<StoredDescriptorInfo>& pool : pools) {
    LOG(INFO) << "Pool: " << pool->display_name << " " << pool->type_url_prefix;
  }
  for (const StoredProto& proto : values) {
    LOG(INFO) << "Proto: " << proto.key << " " << proto.scope << " "
              << proto.proto.type_url() << " '"
              << absl::BytesToHexString(std::string(proto.proto.value()))
              << "'";
  }
}

absl::StatusOr<absl::flat_hash_map<
    clips::DescriptorPoolId,
    std::shared_ptr<Blackboard::Snapshot::StoredDescriptorInfo>>>
Blackboard::CreateSnapshotPools(
    const absl::flat_hash_set<clips::DescriptorPoolId>& pools_to_save,
    Snapshot& snapshot) {
  absl::flat_hash_map<clips::DescriptorPoolId,
                      std::shared_ptr<Snapshot::StoredDescriptorInfo>>
      pool_id_to_info;
  for (clips::DescriptorPoolId pool_id : pools_to_save) {
    INTR_ASSIGN_OR_RETURN(clips::ProtobufManager::DescriptorPoolInfo pool_info,
                          proto_mgr_.GetDescriptorPool(pool_id));
    INTR_ASSIGN_OR_RETURN(
        std::optional<google::protobuf::FileDescriptorSet> file_descriptor_set,
        proto_mgr_.GetDescriptorPoolFileDescriptorSet(pool_id));
    auto new_pool_info = std::make_shared<Snapshot::StoredDescriptorInfo>(
        Snapshot::StoredDescriptorInfo{
            .file_descriptor_set = std::move(file_descriptor_set),
            .display_name = std::move(pool_info.display_name),
            .type_url_prefix = std::move(pool_info.type_url_prefix)});
    pool_id_to_info[pool_id] = new_pool_info;
    snapshot.pools.emplace_back(std::move(new_pool_info));
  }

  return pool_id_to_info;
}

absl::Status Blackboard::CreateSnapshotProtos(
    absl::Span<const BlackboardValueByIds> protos,
    const absl::flat_hash_map<clips::DescriptorPoolId,
                              std::shared_ptr<Snapshot::StoredDescriptorInfo>>&
        pool_id_to_info,
    Snapshot& snapshot) {
  for (const BlackboardValueByIds& value : protos) {
    INTR_ASSIGN_OR_RETURN(google::protobuf::Any value_msg,
                          proto_mgr_.CastToAny(value.proto_id));
    auto pool_it = pool_id_to_info.find(value.pool_id);
    if (pool_it == pool_id_to_info.end()) {
      return absl::InternalError(
          absl::StrFormat("Saved pool information not created for pool %d",
                          value.pool_id.value()));
    }
    std::shared_ptr<Snapshot::StoredDescriptorInfo> pool = pool_it->second;
    snapshot.values.emplace_back(Snapshot::StoredProto{.key = value.key,
                                                       .scope = value.scope,
                                                       .proto = value_msg,
                                                       .pool = pool});
  }

  return absl::OkStatus();
}

absl::StatusOr<std::pair<std::vector<Blackboard::BlackboardValueByIds>,
                         absl::flat_hash_set<clips::DescriptorPoolId>>>
Blackboard::GetSnapshotProtos(absl::string_view operation_name)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(clips_.mutex()) {
  std::vector<BlackboardValueByIds> values;
  absl::flat_hash_set<clips::DescriptorPoolId> pools;
  std::vector<clips::Fact> bb_facts = clips_.QueryFacts(
      "blackboard-item", {{"operation-name", operation_name}});
  values.reserve(bb_facts.size());
  for (const auto& fact : bb_facts) {
    INTR_ASSIGN_OR_RETURN(clips::Value bb_val_operation_name_val,
                          fact.GetSlotValue("operation-name"));
    INTR_ASSIGN_OR_RETURN(std::string bb_val_operation_name,
                          bb_val_operation_name_val.GetString());
    if (bb_val_operation_name != operation_name) {
      continue;
    }

    INTR_ASSIGN_OR_RETURN(clips::Value key_val, fact.GetSlotValue("key"));
    INTR_ASSIGN_OR_RETURN(std::string key, key_val.GetString());

    INTR_ASSIGN_OR_RETURN(clips::Value scope_val, fact.GetSlotValue("scope"));
    INTR_ASSIGN_OR_RETURN(std::string scope, scope_val.GetString());

    INTR_ASSIGN_OR_RETURN(clips::Value blackboard_value_id_val,
                          fact.GetSlotValue("proto-id"));
    INTR_ASSIGN_OR_RETURN(int64_t blackboard_value_proto_id,
                          blackboard_value_id_val.GetInteger());
    clips::ProtoMessageId blackboard_value_id(blackboard_value_proto_id);
    if (blackboard_value_id == clips::ProtobufManager::kInvalidId) {
      return absl::InternalError(absl::StrFormat(
          "Inconsistent blackboard state. Value with key '%s' and scope '%s' "
          "in operation '%s' had an invalid proto",
          key, scope, operation_name));
    }

    INTR_ASSIGN_OR_RETURN(clips::DescriptorPoolId pool_id,
                          proto_mgr_.FindPoolIdForProto(blackboard_value_id));

    values.push_back({.key = key,
                      .scope = scope,
                      .proto_id = blackboard_value_id,
                      .pool_id = pool_id});
    pools.insert(pool_id);
  }

  return std::make_pair(values, pools);
}

absl::StatusOr<intrinsic_proto::executive::BlackboardSnapshot>
Blackboard::CreateSnapshot(
    absl::string_view operation_name, absl::string_view display_name,
    intrinsic_proto::executive::BlackboardSnapshot::SnapshotSource
        snapshot_source) ABSL_LOCKS_EXCLUDED(clips_.mutex()) {
  absl::MutexLock lock(*clips_.mutex());

  Snapshot snapshot{.handle = SnapshotHandle(intrinsic::UniqueId()),
                    .display_name = std::string(display_name),
                    .snapshot_source = snapshot_source,
                    .source_operation_name = std::string(operation_name)};

  INTR_ASSIGN_OR_RETURN(
      (const auto& [values, pools]), GetSnapshotProtos(operation_name),
      std::move(_).With(WrapExtendedStatus(
          80100,
          absl::StrFormat(
              "Could not retrieve protos to snapshot for operation '%s'",
              operation_name),
          intrinsic::StatusBuilder::LEGACY_AS_DEBUG_REPORT)));

  INTR_ASSIGN_OR_RETURN(
      (absl::flat_hash_map<clips::DescriptorPoolId,
                           std::shared_ptr<Snapshot::StoredDescriptorInfo>>
           pool_id_to_info),
      CreateSnapshotPools(pools, snapshot),
      std::move(_).With(WrapExtendedStatus(
          80100,
          absl::StrFormat("Failed to save descriptor pools to "
                          "snapshot for operation '%s'",
                          operation_name),
          intrinsic::StatusBuilder::LEGACY_AS_DEBUG_REPORT)));

  INTR_RETURN_IF_ERROR(CreateSnapshotProtos(values, pool_id_to_info, snapshot))
      .With(
          WrapExtendedStatus(80100,
                             absl::StrFormat("Failed to save proto messages "
                                             "to snapshot for operation '%s'",
                                             operation_name),
                             intrinsic::StatusBuilder::LEGACY_AS_DEBUG_REPORT));

  snapshot.save_time = absl::Now();

  intrinsic_proto::executive::BlackboardSnapshot snapshot_proto =
      snapshot.ToProto();

  LOG(INFO) << "Saved blackboard snapshot '" << snapshot.handle
            << "' with name '" << snapshot.display_name << "' of size "
            << HumanReadableBytes(snapshot.EstimatedSizeBytes())
            << " containing " << snapshot.values.size() << " protos and "
            << snapshot.pools.size() << " pools from operation '"
            << snapshot.source_operation_name << "'";

  snapshots_[snapshot.handle] = std::move(snapshot);

  return snapshot_proto;
}

absl::StatusOr<absl::flat_hash_map<std::string, clips::DescriptorPoolId>>
Blackboard::GetSkillPools(absl::string_view operation_name)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(clips_.mutex()) {
  absl::flat_hash_map<std::string, clips::DescriptorPoolId> skill_pools;
  std::vector<clips::Fact> skill_infos =
      clips_.QueryFacts("skill-info", {{"operation-name", operation_name}});
  for (const clips::Fact& skill_info : skill_infos) {
    INTR_ASSIGN_OR_RETURN(clips::Value skill_id_val,
                          skill_info.GetSlotValue("skill-id"));
    INTR_ASSIGN_OR_RETURN(std::string skill_id, skill_id_val.GetString());
    // Prefer the return value pool if available as blackboard values are
    // usually return values
    INTR_ASSIGN_OR_RETURN(
        clips::Value skill_return_pool_val,
        skill_info.GetSlotValue("return-value-descriptor-pool-id"));
    INTR_ASSIGN_OR_RETURN(int64_t skill_return_pool_int,
                          skill_return_pool_val.GetInteger());
    clips::DescriptorPoolId skill_return_pool_id(skill_return_pool_int);
    if (skill_return_pool_id !=
        clips::ProtobufManager::kInvalidDescriptorPoolId) {
      skill_pools[skill_id] = skill_return_pool_id;
      continue;
    }

    INTR_ASSIGN_OR_RETURN(
        clips::Value skill_parameter_pool_val,
        skill_info.GetSlotValue("parameter-descriptor-pool-id"));
    INTR_ASSIGN_OR_RETURN(int64_t skill_parameter_pool_int,
                          skill_parameter_pool_val.GetInteger());
    clips::DescriptorPoolId skill_parameter_pool_id(skill_parameter_pool_int);
    if (skill_parameter_pool_id !=
        clips::ProtobufManager::kInvalidDescriptorPoolId) {
      skill_pools[skill_id] = skill_parameter_pool_id;
    }
  }
  return skill_pools;
}

absl::StatusOr<clips::DescriptorPoolId> Blackboard::LoadSkillPool(
    absl::string_view skill_id,
    const absl::flat_hash_map<std::string, clips::DescriptorPoolId>&
        skill_pools,
    const Snapshot::StoredDescriptorInfo& descriptor_info,
    absl::string_view target_operation_name,
    std::vector<intrinsic_proto::status::ExtendedStatus>& load_diagnostics) {
  auto skill_pool_it = skill_pools.find(skill_id);
  if (skill_pool_it != skill_pools.end()) {
    return skill_pool_it->second;
  }

  intrinsic_proto::status::ExtendedStatus es = CreateExtendedStatus(
      80113,
      absl::StrFormat(
          "No skill pool available in operation '%s' for restoring "
          "protos from asset id '%s'. Protos will not be migrated but "
          "re-created from descriptor information in the snapshot. This "
          "warning can be safely ignored if '%s' refers to an asset that is "
          "not a skill (e.g., a process).",
          target_operation_name, skill_id, skill_id),
      {.severity = intrinsic_proto::status::ExtendedStatus::WARNING});
  load_diagnostics.push_back(std::move(es));
  INTR_ASSIGN_OR_RETURN(
      clips::DescriptorPoolId pool_id,
      proto_mgr_.AddDescriptorPool(
          *descriptor_info.file_descriptor_set, descriptor_info.display_name,
          descriptor_info.type_url_prefix, target_operation_name));
  return pool_id;
}

absl::StatusOr<absl::flat_hash_map<Blackboard::Snapshot::StoredDescriptorInfo*,
                                   clips::DescriptorPoolId>>
Blackboard::LoadPools(
    const Snapshot& snapshot, absl::string_view target_operation_name,
    std::vector<intrinsic_proto::status::ExtendedStatus>& load_diagnostics)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(clips_.mutex()) {
  INTR_ASSIGN_OR_RETURN(
      (absl::flat_hash_map<std::string, clips::DescriptorPoolId> skill_pools),
      GetSkillPools(target_operation_name));

  absl::flat_hash_map<Snapshot::StoredDescriptorInfo*, clips::DescriptorPoolId>
      loaded_pools;
  for (const std::shared_ptr<Snapshot::StoredDescriptorInfo>& descriptor_info :
       snapshot.pools) {
    if (!descriptor_info->file_descriptor_set.has_value()) {
      // This was the generated pool, do not create a pool
      continue;
    }
    absl::StatusOr<std::string> skill_id =
        proto_mgr_.GetAssetIdFromTypeUrlPrefix(
            descriptor_info->type_url_prefix);
    clips::DescriptorPoolId pool_id =
        clips::ProtobufManager::kInvalidDescriptorPoolId;
    if (skill_id.ok()) {
      INTR_ASSIGN_OR_RETURN(
          pool_id, LoadSkillPool(*skill_id, skill_pools, *descriptor_info,
                                 target_operation_name, load_diagnostics));
    } else {
      INTR_ASSIGN_OR_RETURN(pool_id, proto_mgr_.AddDescriptorPool(
                                         *descriptor_info->file_descriptor_set,
                                         descriptor_info->display_name,
                                         descriptor_info->type_url_prefix,
                                         target_operation_name));
    }
    loaded_pools[descriptor_info.get()] = pool_id;
  }
  return loaded_pools;
}

absl::Status Blackboard::AddBlackboardUpdate(
    const Snapshot::StoredProto& stored_proto,
    clips::ProtoMessageId loaded_proto_id,
    absl::string_view target_operation_name)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(clips_.mutex()) {
  VLOG(1) << "Adding blackboard update for " << stored_proto.key << " in "
          << stored_proto.scope << " within " << target_operation_name;
  return clips_
      .AssertFact("blackboard-update",
                  {{"key", stored_proto.key},
                   {"scope", stored_proto.scope},
                   {"operation-name", target_operation_name},
                   {"proto-id", loaded_proto_id.value()}})
      .status();
}

absl::Status Blackboard::LoadProtoFromGeneratedPool(
    const Snapshot::StoredProto& proto, absl::string_view target_operation_name)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(clips_.mutex()) {
  if (proto.pool->file_descriptor_set.has_value()) {
    return absl::InternalError(
        absl::StrFormat("Proto to load had a file descriptor set, but no "
                        "loaded pool. (Key %s, Scope %s)",
                        proto.key, proto.scope));
  }

  // Generated pool protos do not have a loaded pool, this will be
  // loaded from the generated pool
  INTR_ASSIGN_OR_RETURN(
      clips::ProtobufManager::DescriptorPoolInfo pool_info,
      proto_mgr_.GetDescriptorPool(
          clips::ProtobufManager::kGeneratedDescriptorPoolId));
  INTR_ASSIGN_OR_RETURN(
      std::unique_ptr<google::protobuf::Message> casted_proto,
      proto_mgr_.CastFromAnyWithPool(proto.proto, pool_info, ""));
  INTR_ASSIGN_OR_RETURN(
      clips::ProtoMessageId loaded_proto_id,
      proto_mgr_.AddProto(std::move(casted_proto),
                          clips::ProtobufManager::kGeneratedDescriptorPoolId));

  return AddBlackboardUpdate(proto, loaded_proto_id, target_operation_name);
}

absl::Status Blackboard::LoadProtoFromDynamicPool(
    const Snapshot::StoredProto& proto, clips::DescriptorPoolId pool_id,
    absl::string_view target_operation_name)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(clips_.mutex()) {
  INTR_ASSIGN_OR_RETURN(clips::ProtobufManager::DescriptorPoolInfo pool_info,
                        proto_mgr_.GetDescriptorPool(pool_id));
  INTR_ASSIGN_OR_RETURN(
      std::unique_ptr<google::protobuf::Message> casted_proto,
      proto_mgr_.CastFromAnyWithPool(proto.proto, pool_info, ""));
  INTR_ASSIGN_OR_RETURN(clips::ProtoMessageId loaded_proto_id,
                        proto_mgr_.AddProto(std::move(casted_proto), pool_id));

  return AddBlackboardUpdate(proto, loaded_proto_id, target_operation_name);
}

intrinsic_proto::status::ExtendedStatus Blackboard::CreateLoadExtendedStatus(
    const absl::Status& load_status, const Snapshot::StoredProto& proto) {
  if (load_status.ok()) {
    return CreateExtendedStatus(
        80111,
        absl::StrFormat("Loaded value for key '%s' in scope '%s'", proto.key,
                        proto.scope),
        {.severity = intrinsic_proto::status::ExtendedStatus::INFO});
  }
  intrinsic_proto::status::ExtendedStatus es = CreateExtendedStatus(
      80112,
      absl::StrFormat("Failed to load value for key '%s' in scope '%s'",
                      proto.key, proto.scope),
      {.severity = intrinsic_proto::status::ExtendedStatus::ERROR});
  std::optional<intrinsic_proto::status::ExtendedStatus> load_extended_status =
      GetExtendedStatus(load_status);
  if (load_extended_status.has_value()) {
    *es.add_context() = std::move(*load_extended_status);
  } else {
    es.mutable_debug_report()->set_message(load_status.message());
  }
  return es;
}

absl::StatusOr<intrinsic_proto::status::ExtendedStatus>
Blackboard::LoadSnapshot(const SnapshotHandle& handle,
                         absl::string_view target_operation_name)
    ABSL_LOCKS_EXCLUDED(clips_.mutex()) {
  auto snapshot_it = snapshots_.find(handle);
  if (snapshot_it == snapshots_.end()) {
    return CreateStatus(
        80120,
        absl::StrFormat(
            "Cannot load snapshot with handle '%s' as it doesn't exist.",
            handle.value()),
        absl::StatusCode::kNotFound);
  }
  const Snapshot& snapshot = snapshot_it->second;

  absl::MutexLock lock(*clips_.mutex());

  std::vector<intrinsic_proto::status::ExtendedStatus> load_diagnostics;

  // 1. Load pools that aren't generated pools
  INTR_ASSIGN_OR_RETURN(
      (absl::flat_hash_map<Snapshot::StoredDescriptorInfo*,
                           clips::DescriptorPoolId>
           loaded_pools),
      LoadPools(snapshot, target_operation_name, load_diagnostics),
      std::move(_).With(WrapExtendedStatus(
          80110,
          absl::StrFormat("Failed to load descriptor pools from "
                          "snapshot with handle '%s' in operation '%s'",
                          handle.value(), target_operation_name),
          intrinsic::StatusBuilder::LEGACY_AS_DEBUG_REPORT)));

  // 2. Load protos using the loaded pools
  for (const Snapshot::StoredProto& proto : snapshot.values) {
    auto find_loaded_pool = loaded_pools.find(proto.pool.get());
    absl::Status load_status;
    if (find_loaded_pool == loaded_pools.end()) {
      load_status = LoadProtoFromGeneratedPool(proto, target_operation_name);
    } else {
      clips::DescriptorPoolId pool_id = find_loaded_pool->second;
      load_status =
          LoadProtoFromDynamicPool(proto, pool_id, target_operation_name);
    }
    load_diagnostics.push_back(CreateLoadExtendedStatus(load_status, proto));
  }

  intrinsic_proto::status::ExtendedStatus result_es =
      CreateExtendedStatus(80114, "Snapshot was loaded");
  result_es.set_severity(intrinsic_proto::status::ExtendedStatus::INFO);
  for (const intrinsic_proto::status::ExtendedStatus& es : load_diagnostics) {
    if (es.severity() > result_es.severity()) {
      result_es.set_severity(es.severity());
    }
    *result_es.add_context() = std::move(es);
  }
  if (result_es.severity() >= intrinsic_proto::status::ExtendedStatus::ERROR) {
    result_es.mutable_status_code()->set_code(80110);
    result_es.mutable_user_report()->set_message(
        "Could not load all protos and pools in the snapshot");
  }

  // Trigger CLIPS run
  clips_.NotifyRunner();
  return result_es;
}

std::pair<std::vector<intrinsic_proto::executive::BlackboardSnapshot>, int>
Blackboard::ListSnapshots(int page_size, int page_offset) {
  std::vector<intrinsic_proto::executive::BlackboardSnapshot> snapshots;
  for (const auto& [handle, snapshot] : snapshots_) {
    snapshots.push_back(snapshot.ToProto());
  }
  absl::c_sort(snapshots,
               [](const intrinsic_proto::executive::BlackboardSnapshot& lhs,
                  const intrinsic_proto::executive::BlackboardSnapshot& rhs) {
                 absl::Time lhs_time =
                     intrinsic::ToAbslTimeNoValidation(lhs.save_time());
                 absl::Time rhs_time =
                     intrinsic::ToAbslTimeNoValidation(rhs.save_time());
                 return lhs_time < rhs_time;
               });

  page_size = std::max(page_size, 1);
  page_offset = std::max(page_offset, 0);
  std::vector<intrinsic_proto::executive::BlackboardSnapshot> ret;
  int snapshots_end = snapshots.size();
  int new_page_offset = 0;
  if (page_offset + page_size < snapshots_end) {
    snapshots_end = page_offset + page_size;
    new_page_offset = snapshots_end;
  }
  for (int i = page_offset; i < snapshots_end; ++i) {
    ret.push_back(std::move(snapshots[i]));
  }
  return {ret, new_page_offset};
}

absl::Status Blackboard::DeleteSnapshot(const SnapshotHandle& handle) {
  size_t num_erased = snapshots_.erase(handle);
  if (num_erased == 0) {
    return CreateStatus(
        80120,
        absl::StrFormat(
            "Cannot delete snapshot with handle '%s' as it doesn't exist.",
            handle.value()),
        absl::StatusCode::kNotFound);
  }
  return absl::OkStatus();
}

}  // namespace intrinsic::executive
