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

#ifndef INTRINSIC_EXECUTIVE_ENGINE_CLIPS_CONDUCTOR_CLIENT_H_
#define INTRINSIC_EXECUTIVE_ENGINE_CLIPS_CONDUCTOR_CLIENT_H_

#include <optional>
#include <string>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/flags/declare.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "google/longrunning/operations.pb.h"
#include "intrinsic/conductor/proto/conductor.grpc.pb.h"
#include "intrinsic/conductor/proto/conductor.pb.h"
#include "intrinsic/executive/clips_cpp/assert_facade.h"
#include "intrinsic/executive/clips_cpp/function_facade.h"
#include "intrinsic/executive/clips_cpp/protobuf.h"
#include "intrinsic/executive/clips_cpp/trace_span_manager.h"
#include "intrinsic/executive/proto/run_metadata.pb.h"
#include "intrinsic/util/thread/thread_pool.h"

ABSL_DECLARE_FLAG(bool, enable_conductor_process_start);

namespace intrinsic {
namespace executive {

// Access conductor service from CLIPS.
class ClipsConductorClient {
 public:
  explicit ClipsConductorClient(
      clips::EnvironmentAssertFacade* assert_facade,
      clips::ProtobufManager* protobuf_manager,
      intrinsic_proto::conductor::ConductorService::StubInterface*
          conductor_stub,
      clips::TraceSpanManager* span_manager);

  // Must be called after construction.
  // Register the conductor functions with the CLIPS environment, and creates
  // the thread bundle. Must be called in the same parent thread as TearDown().
  absl::Status Init(clips::EnvironmentFunctionFacade* function_facade,
                    absl::Duration client_operation_poll_interval)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(function_facade->clips_mutex());

  // Must be called in the same parent thread as Init().
  absl::Status TearDown();

  clips::EnvironmentAssertFacade* GetAssertFacade() { return assert_facade_; }

  struct ExecutiveOperationInfo {
    absl::string_view name;
    absl::string_view scene_id;
    intrinsic_proto::executive::RunMetadata::State state;
  };

  struct ConductorClientOperationInfo {
    std::string name;
    bool is_done;
  };

  // Notify the conductor that a process is being started. This calls the
  // PrepareProcessStart RPC in the conductor which starts a long running
  // operation in the conductor.
  // It is the caller's responsibility to assert a
  // `conductor-preparation-client-operation` fact with the result if the RPC
  // succeeds.
  absl::StatusOr<ConductorClientOperationInfo> PrepareConductorProcessStart(
      absl::string_view executive_operation_name, absl::string_view scene_id,
      const std::vector<ExecutiveOperationInfo>& all_executive_operations)
      const;

  // Stop a previously started `PrepareProcessStart` operation in conductor if
  // the executive operation failed to start running. This results in a
  // `CancelConductorPrepareProcessStartAsync` call and potentially also a
  // `NotifyAllProcessesStopped` call from the CLIPS environment.
  absl::Status AssertStopConductorPreparationFact(
      absl::string_view conductor_operation_name)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(GetAssertFacade()->GetClipsMutex());

 private:
  // Check if the conductor PrepareProcessStart LRO is done with `GetOperation`
  // RPC.
  // If the LRO was completed successfully, the unpacked response will
  // be returned.
  // If the `GetOperation` RPC fails or the LRO finished with an error, an error
  // status will be returned.
  // If the LRO is not done yet or the `GetOperation` RPC returns `Unavailable`
  // or `Unimplemented`, an Ok status with `std::nullopt` will be returned.
  absl::StatusOr<
      std::optional<intrinsic_proto::conductor::PrepareProcessStartResponse>>
  GetConductorPrepareProcessStartResponse(
      absl::string_view conductor_operation_name);
  void CheckConductorPrepareProcessStartDoneAsync(
      absl::string_view conductor_operation_name,
      clips::TraceSpanReferenceId parent_span_id);
  // If the LRO was completed successfully, the unpacked response will be
  // returned.
  // If the LRO finished with an error, an error status will be returned.
  // If the LRO is not done yet, an Ok status with `std::nullopt` will be
  // returned.
  absl::StatusOr<
      std::optional<intrinsic_proto::conductor::PrepareProcessStartResponse>>
  ExtractPrepareProcessStartResponse(
      const google::longrunning::Operation& operation);

  // Cancel the `PrepareProcessStart` client operation in conductor.
  void CancelConductorPrepareProcessStartAsync(
      absl::string_view conductor_operation_name,
      clips::TraceSpanReferenceId parent_span_id);

  // Notify the conductor that a process is being resumed.
  absl::Status PrepareConductorProcessResume(
      clips::TraceSpanReferenceId parent_span_id);

  // Notify the conductor that all running processes have reached a waiting
  // state.
  absl::Status NotifyConductorAllProcessesStopped(
      clips::TraceSpanReferenceId parent_span_id);

  clips::EnvironmentAssertFacade* assert_facade_;  // externally owned
  clips::ProtobufManager* protobuf_manager_;       // externally owned
  intrinsic_proto::conductor::ConductorService::StubInterface*
      conductor_stub_;                     // externally owned
  clips::TraceSpanManager* span_manager_;  // externally owned
  std::optional<intrinsic::ThreadPool> bundle_;
};

}  // namespace executive
}  // namespace intrinsic

#endif  // INTRINSIC_EXECUTIVE_ENGINE_CLIPS_CONDUCTOR_CLIENT_H_
