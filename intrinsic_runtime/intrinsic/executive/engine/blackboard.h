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

#ifndef INTRINSIC_EXECUTIVE_ENGINE_BLACKBOARD_H_
#define INTRINSIC_EXECUTIVE_ENGINE_BLACKBOARD_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "google/protobuf/any.pb.h"
#include "intrinsic/executive/clips_cpp/environment.h"
#include "intrinsic/executive/clips_cpp/protobuf.h"
#include "intrinsic/executive/proto/blackboard_service.pb.h"
#include "intrinsic/util/string_type.h"

namespace intrinsic::executive {

// The Blackboard class allows access to the blackboard runtime key-value store
// in the CLIPS environment.
class Blackboard {
 public:
  INTRINSIC_DEFINE_STRING_TYPE(SnapshotHandle);

  Blackboard(clips::Environment& clips ABSL_ATTRIBUTE_LIFETIME_BOUND,
             clips::ProtobufManager& proto_mgr ABSL_ATTRIBUTE_LIFETIME_BOUND);

  absl::StatusOr<google::protobuf::Any> GetBlackboardValue(
      absl::string_view key, absl::string_view scope,
      absl::string_view operation_name) ABSL_LOCKS_EXCLUDED(clips_.mutex());

  absl::Status UpdateBlackboardValue(absl::string_view key,
                                     absl::string_view scope,
                                     absl::string_view operation_name,
                                     const google::protobuf::Any& value)
      ABSL_LOCKS_EXCLUDED(clips_.mutex());

  absl::StatusOr<std::vector<intrinsic_proto::executive::BlackboardValue>>
  ListBlackboardValues(
      absl::string_view request_scope, absl::string_view operation_name,
      intrinsic_proto::executive::ListBlackboardValuesRequest::View view)
      ABSL_LOCKS_EXCLUDED(clips_.mutex());

  absl::Status DeleteBlackboardValue(absl::string_view key,
                                     absl::string_view scope,
                                     absl::string_view operation_name)
      ABSL_LOCKS_EXCLUDED(clips_.mutex());

  absl::StatusOr<intrinsic_proto::executive::BlackboardSnapshot> CreateSnapshot(
      absl::string_view operation_name, absl::string_view display_name,
      intrinsic_proto::executive::BlackboardSnapshot::SnapshotSource
          snapshot_source) ABSL_LOCKS_EXCLUDED(clips_.mutex());

  // Load the snapshot identified by handle into target_operation_name.
  // The call succeeds even when not all protos could be loaded. In that case
  // it returns a list of ExtendedStatus diagnostics that indicate how the
  // load worked. Note that these are not necessarily errors, but also
  // warnings or info.
  absl::StatusOr<intrinsic_proto::status::ExtendedStatus> LoadSnapshot(
      const SnapshotHandle& handle, absl::string_view target_operation_name)
      ABSL_LOCKS_EXCLUDED(clips_.mutex());

  // Returns up to page_size snapshot protos starting from page_offset in
  // creation order.
  // If there are more snapshots after page_offset + page_size, returns the
  // offset to query the next snapshot from, 0 otherwise.
  std::pair<std::vector<intrinsic_proto::executive::BlackboardSnapshot>, int>
  ListSnapshots(int page_size, int page_offset);

  absl::Status DeleteSnapshot(const SnapshotHandle& handle);

 private:
  // A Snapshot is a fully self-contained blackboard snapshot, i.e., based on
  // this snapshot it is possible to re-create all protos without external
  // information.
  struct Snapshot {
    SnapshotHandle handle;
    std::string display_name;
    absl::Time save_time;
    intrinsic_proto::executive::BlackboardSnapshot::SnapshotSource
        snapshot_source;

    std::string source_operation_name;

    struct StoredDescriptorInfo {
      // Must be set if the pool was dynamically created.
      // If nullopt then this is representing the generated pool.
      std::optional<google::protobuf::FileDescriptorSet> file_descriptor_set;

      std::string display_name;
      std::string type_url_prefix;
    };

    struct StoredProto {
      std::string key;
      std::string scope;
      google::protobuf::Any proto;

      std::shared_ptr<StoredDescriptorInfo> pool;
    };

    std::vector<std::shared_ptr<StoredDescriptorInfo>> pools;
    std::vector<StoredProto> values;

    intrinsic_proto::executive::BlackboardSnapshot ToProto() const;
    // Determines an estimate of the size of this snapshot. The value only
    // represents an estimate of the user-provided data, but does not consider
    // internal memory usage of structures.
    size_t EstimatedSizeBytes() const;

    // Outputs internal information about contents of the snapshot. Use only for
    // debugging.
    void Dump() const;
  };

  // Internal helper to be able to record the current state of protos and their
  // associated descriptor pools used to link one to the other. Note that we
  // cannot use these proto ids as well nor pool ids for storage as these might
  // represent objects that are deleted within the lifetime of a snapshot
  struct BlackboardValueByIds {
    std::string key;
    std::string scope;
    clips::ProtoMessageId proto_id;
    clips::DescriptorPoolId pool_id;
  };

  // Retrieves the blackboard values to save as BlackboardValueByIds. In
  // addition also retrieves the DescriptorPoolId for all pools for these
  // values.
  absl::StatusOr<std::pair<std::vector<Blackboard::BlackboardValueByIds>,
                           absl::flat_hash_set<clips::DescriptorPoolId>>>
  GetSnapshotProtos(absl::string_view operation_name)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(clips_.mutex());

  // Saves the pools given by the DescriptorPoolId from the proto_mgr_ in the
  // pools field within the passed in snapshot.
  // The return values maps each saved DescriptorPoolId to the resulting stored
  // pointer in snapshot.pools.
  absl::StatusOr<absl::flat_hash_map<
      clips::DescriptorPoolId, std::shared_ptr<Snapshot::StoredDescriptorInfo>>>
  CreateSnapshotPools(
      const absl::flat_hash_set<clips::DescriptorPoolId>& pools_to_save,
      Snapshot& snapshot);

  // Saves the passed in protos in snapshot.protos. pool_id_to_info is used to
  // associate StoredDescriptorInfo with each StoredProto. snapshot.pools must
  // have been filled by the values in protos.
  absl::Status CreateSnapshotProtos(
      absl::Span<const BlackboardValueByIds> protos,
      const absl::flat_hash_map<
          clips::DescriptorPoolId,
          std::shared_ptr<Snapshot::StoredDescriptorInfo>>& pool_id_to_info,
      Snapshot& snapshot);

  // Get a mapping from skill id to the available skill pools in the operation.
  absl::StatusOr<absl::flat_hash_map<std::string, clips::DescriptorPoolId>>
  GetSkillPools(absl::string_view operation_name)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(clips_.mutex());

  // Load a pool for skill with skill_id. If available a matching pool from
  // skill_pools is chosen. Otherwise a new pool will be created.
  absl::StatusOr<clips::DescriptorPoolId> LoadSkillPool(
      absl::string_view skill_id,
      const absl::flat_hash_map<std::string, clips::DescriptorPoolId>&
          skill_pools,
      const Snapshot::StoredDescriptorInfo& descriptor_info,
      absl::string_view target_operation_name,
      std::vector<intrinsic_proto::status::ExtendedStatus>& load_diagnostics);

  // Load the pools contained in snapshot for target_operation_name. New pools
  // will be created for the StoredDescriptorInfo that contains a
  // file_descriptor_set. The return value maps the StoredDescriptorInfo from
  // snapshot.pools to the DescriptorPoolId of the created pool.
  absl::StatusOr<absl::flat_hash_map<
      Blackboard::Snapshot::StoredDescriptorInfo*, clips::DescriptorPoolId>>
  LoadPools(
      const Snapshot& snapshot, absl::string_view target_operation_name,
      std::vector<intrinsic_proto::status::ExtendedStatus>& load_diagnostics)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(clips_.mutex());

  // Add a blackboard-update fact for stored_proto using the proto represented
  // by loaded_proto_id. loaded_proto_id must already have been inserted in the
  // proto manager. Only inserts the update, but does not perform a CLIPS run.
  absl::Status AddBlackboardUpdate(const Snapshot::StoredProto& stored_proto,
                                   clips::ProtoMessageId loaded_proto_id,
                                   absl::string_view target_operation_name)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(clips_.mutex());

  // Create an ExtendedStatus from the result of trying to load 'proto' provided
  // in 'load_status'.
  intrinsic_proto::status::ExtendedStatus CreateLoadExtendedStatus(
      const absl::Status& load_status, const Snapshot::StoredProto& proto);

  // Loads the given proto assuming that it was saved from the generated pool.
  absl::Status LoadProtoFromGeneratedPool(
      const Snapshot::StoredProto& proto,
      absl::string_view target_operation_name)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(clips_.mutex());

  // Loads the given proto. pool_id is the matching descriptor pool. The pool
  // with pool_id must already exist in the ProtobufManager.
  absl::Status LoadProtoFromDynamicPool(const Snapshot::StoredProto& proto,
                                        clips::DescriptorPoolId pool_id,
                                        absl::string_view target_operation_name)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(clips_.mutex());

 private:
  absl::flat_hash_map<SnapshotHandle, Snapshot> snapshots_;

  // Externally owned
  clips::Environment& clips_;
  clips::ProtobufManager& proto_mgr_;
};

}  // namespace intrinsic::executive

#endif  // INTRINSIC_EXECUTIVE_ENGINE_BLACKBOARD_H_
