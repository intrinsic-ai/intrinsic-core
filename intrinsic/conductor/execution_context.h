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

#ifndef INTRINSIC_CONDUCTOR_EXECUTION_CONTEXT_H_
#define INTRINSIC_CONDUCTOR_EXECUTION_CONTEXT_H_

#include <memory>
#include <optional>
#include <string>

#include "absl/base/attributes.h"
#include "absl/base/nullability.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "intrinsic/conductor/proto/execution_context.pb.h"
#include "intrinsic/config/proto/operation_mode.pb.h"
#include "intrinsic/platform/pubsub/kvstore.h"
#include "intrinsic/util/status/extended_status.pb.h"

namespace intrinsic::conductor {

constexpr absl::string_view kExecutionContextStorageKey =
    "conductor_execution_context";

// An ExecutionContext holds state for the currently deployed solution within
// the Flowstate platform.
class ExecutionContext {
 public:
  // Create will generate a new execution context for the given operating mode.
  // The ExecutionContext itself should be considered opaque.
  static absl::StatusOr<std::unique_ptr<ExecutionContext>> Create(
      intrinsic_proto::config::OperationMode mode,
      KeyValueStore* absl_nonnull kv_store ABSL_ATTRIBUTE_LIFETIME_BOUND);

  // FromProto populates an execution context from the given proto.
  static absl::StatusOr<std::unique_ptr<ExecutionContext>> FromProto(
      const intrinsic_proto::conductor::ExecutionContext& proto,
      KeyValueStore* absl_nonnull kv_store ABSL_ATTRIBUTE_LIFETIME_BOUND);

  // ToProto serializes the execution context into a proto.
  intrinsic_proto::conductor::ExecutionContext ToProto() const;

  // WaitUntilSerialized blocks until any serialization operations have
  // finished. In this synchronous implementation, it is a no-op.
  void WaitUntilSerialized();

  // Close cleans up the persistence for the execution context.
  // In this implementation, it is a no-op.
  void Close();

  // SetEditWorldID sets the world ID in the world service that we used as the
  // basis for the belief world during the given behavior tree execution.
  absl::Status SetEditWorldID(absl::string_view id);

  // EditWorldID gets the world ID in the world service that we used as the
  // basis for the belief world during the given behavior tree execution.
  std::string EditWorldID() const;

  // SetStagingWorldID sets the world ID in the world service that we expect to
  // use as the initial belief world in the next behavior tree execution.
  absl::Status SetStagingWorldID(absl::string_view id);

  // StagingWorldID gets the world ID in the world service that we expect to
  // use as the initial belief world in the next behavior tree execution.
  std::string StagingWorldID() const;

  // BeliefWorldID returns the ID of the belief world in the world service.
  std::string BeliefWorldID() const;

  // SimWorldID returns the ID of the simulated world in the world service.
  std::string SimWorldID() const;

  // OperationMode returns the operation mode for the currently deployed
  // solution. This is either SIMULATED or REAL_HARDWARE.
  intrinsic_proto::config::OperationMode OperationMode() const;

  // SetEditWorldStatus sets or clears the warning extended status of the edit
  // world.
  absl::Status SetEditWorldStatus(
      std::optional<intrinsic_proto::status::ExtendedStatus> status);

  // EditWorldStatus returns the warning extended status of the edit world.
  std::optional<intrinsic_proto::status::ExtendedStatus> EditWorldStatus()
      const;

  // Save serializes the execution context and stores it in the KV store.
  absl::Status Save();

 private:
  struct Opts {
    KeyValueStore* absl_nonnull kv_store;
    intrinsic_proto::config::OperationMode mode;
    // LINT.IfChange(default_world_ids)
    std::string belief_world_id = "world";
    std::string sim_world_id = "sim_world";
    // LINT.ThenChange(
    //   //intrinsic/world/service/object_world_service.cc:published_world_ids
    // )
    std::string staging_world_id = "";
    std::string edit_world_id = "";
    std::optional<intrinsic_proto::status::ExtendedStatus> edit_world_status =
        std::nullopt;
  };

  explicit ExecutionContext(Opts opts);

  KeyValueStore* kv_store_;
  intrinsic_proto::config::OperationMode operation_mode_;
  std::string belief_world_id_;
  std::string sim_world_id_;
  std::string staging_world_id_;
  std::string edit_world_id_;
  std::optional<intrinsic_proto::status::ExtendedStatus> edit_world_status_;
};

}  // namespace intrinsic::conductor

#endif  // INTRINSIC_CONDUCTOR_EXECUTION_CONTEXT_H_
