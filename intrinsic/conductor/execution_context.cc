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

#include "intrinsic/conductor/execution_context.h"

#include <memory>
#include <utility>

#include "absl/base/nullability.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "intrinsic/conductor/proto/execution_context.pb.h"
#include "intrinsic/config/proto/operation_mode.pb.h"
#include "intrinsic/platform/pubsub/kvstore.h"
#include "intrinsic/util/status/extended_status.pb.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic::conductor {
namespace {

// Verification options for persisting the execution context. The timeout is
// generous since we want to make sure this is persisted. Otherwise, we can not
// differentiate between a deployment and service restart.
constexpr KeyValueStore::SetWithVerificationOptions kSaveVerificationOptions{
    .mode = KeyValueStore::SetWithVerificationOptions::VerificationMode::
        kHighConsistency,
    .timeout = absl::Minutes(5),
};

}  // namespace

ExecutionContext::ExecutionContext(Opts opts)
    : kv_store_(opts.kv_store),
      operation_mode_(opts.mode),
      belief_world_id_(std::move(opts.belief_world_id)),
      sim_world_id_(std::move(opts.sim_world_id)),
      staging_world_id_(std::move(opts.staging_world_id)),
      edit_world_id_(std::move(opts.edit_world_id)),
      edit_world_status_(std::move(opts.edit_world_status)) {}

absl::StatusOr<std::unique_ptr<ExecutionContext>> ExecutionContext::Create(
    intrinsic_proto::config::OperationMode mode,
    KeyValueStore* absl_nonnull kv_store) {
  auto ec = std::unique_ptr<ExecutionContext>(new ExecutionContext({
      .kv_store = kv_store,
      .mode = mode,
  }));
  INTR_RETURN_IF_ERROR(ec->Save());
  return ec;
}

absl::StatusOr<std::unique_ptr<ExecutionContext>> ExecutionContext::FromProto(
    const intrinsic_proto::conductor::ExecutionContext& proto,
    KeyValueStore* absl_nonnull kv_store) {
  std::optional<intrinsic_proto::status::ExtendedStatus> status;
  if (proto.has_edit_world_status()) {
    status = proto.edit_world_status();
  }
  return std::unique_ptr<ExecutionContext>(new ExecutionContext({
      .kv_store = kv_store,
      .mode = proto.mode(),
      .belief_world_id = proto.belief_world_id(),
      .sim_world_id = proto.sim_world_id(),
      .staging_world_id = proto.staging_world_id(),
      .edit_world_id = proto.edit_world_id(),
      .edit_world_status = status,
  }));
}

intrinsic_proto::conductor::ExecutionContext ExecutionContext::ToProto() const {
  intrinsic_proto::conductor::ExecutionContext proto;
  proto.set_mode(operation_mode_);
  proto.set_belief_world_id(belief_world_id_);
  proto.set_sim_world_id(sim_world_id_);
  proto.set_staging_world_id(staging_world_id_);
  proto.set_edit_world_id(edit_world_id_);
  if (edit_world_status_.has_value()) {
    *proto.mutable_edit_world_status() = *edit_world_status_;
  }
  return proto;
}

// TODO(b/511285562): Consider async saving if performance becomes a problem.
absl::Status ExecutionContext::SetEditWorldID(absl::string_view id) {
  edit_world_id_ = std::string(id);
  return Save();
}

std::string ExecutionContext::EditWorldID() const { return edit_world_id_; }

absl::Status ExecutionContext::SetStagingWorldID(absl::string_view id) {
  staging_world_id_ = std::string(id);
  return Save();
}

std::string ExecutionContext::StagingWorldID() const {
  return staging_world_id_;
}

std::string ExecutionContext::BeliefWorldID() const { return belief_world_id_; }

std::string ExecutionContext::SimWorldID() const { return sim_world_id_; }

intrinsic_proto::config::OperationMode ExecutionContext::OperationMode() const {
  return operation_mode_;
}

absl::Status ExecutionContext::SetEditWorldStatus(
    std::optional<intrinsic_proto::status::ExtendedStatus> status) {
  edit_world_status_ = status;
  return Save();
}

std::optional<intrinsic_proto::status::ExtendedStatus>
ExecutionContext::EditWorldStatus() const {
  return edit_world_status_;
}

absl::Status ExecutionContext::Save() {
  if (kv_store_ == nullptr) {
    return absl::InternalError("KVStore is null");
  }

  // Blocks until the exact value is verified in the KV store.
  INTR_RETURN_IF_ERROR(
      kv_store_->SetWithVerification(kExecutionContextStorageKey, ToProto(),
                                     kSaveVerificationOptions))
      .LogError();
  LOG(INFO) << "Saved execution context to KV store at ["
            << kExecutionContextStorageKey << "]";
  return absl::OkStatus();
}

void ExecutionContext::WaitUntilSerialized() {
  // No-op in synchronous implementation.
}

void ExecutionContext::Close() {
  // No-op in synchronous implementation.
}

}  // namespace intrinsic::conductor
