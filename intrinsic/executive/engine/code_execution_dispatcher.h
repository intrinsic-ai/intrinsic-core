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

#ifndef INTRINSIC_EXECUTIVE_ENGINE_CODE_EXECUTION_DISPATCHER_H_
#define INTRINSIC_EXECUTIVE_ENGINE_CODE_EXECUTION_DISPATCHER_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "google/longrunning/operations.pb.h"
#include "google/protobuf/any.pb.h"
#include "google/protobuf/duration.pb.h"
#include "google/protobuf/message.h"
#include "intrinsic/executive/clips_cpp/assert_facade.h"
#include "intrinsic/executive/clips_cpp/function_facade.h"
#include "intrinsic/executive/clips_cpp/protobuf.h"
#include "intrinsic/executive/clips_cpp/readonly_facade.h"
#include "intrinsic/executive/clips_cpp/trace_span_manager.h"
#include "intrinsic/executive/clips_cpp/value.h"
#include "intrinsic/executive/proto/code_execution_service.grpc.pb.h"
#include "intrinsic/util/status/extended_status.pb.h"
#include "intrinsic/util/status/status_specs.h"
#include "intrinsic/util/thread/thread_pool.h"

namespace intrinsic {
namespace executive {

// Asynchronously runs code execution requests in the background.
// Receives 'code-execution-start' from CLIPS and runs the code execution in a
// separate thread. When the code execution has finished, it calls back to CLIPS
// and asserts the status and return value as a fact.
class CodeExecutionDispatcher {
 public:
  CodeExecutionDispatcher(
      clips::EnvironmentAssertFacade* assert_facade,
      clips::ProtobufManager* proto_manager,
      clips::TraceSpanManager* span_manager,
      intrinsic_proto::executive::CodeExecutionService::StubInterface*
          code_execution_stub);

  // Must be called after construction.
  // Registers the code-execution related functions with the CLIPS environment,
  // and creates the thread bundle. Must be called in the same parent thread as
  // TearDown().
  absl::Status Init(clips::EnvironmentFunctionFacade* function_facade,
                    clips::EnvironmentReadonlyFacade* readonly_facade)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(function_facade->clips_mutex(),
                                    readonly_facade->GetClipsMutex());

  void StartCodeExecution(const std::string& operation_name,
                          const clips::Symbol& tree_id, int64_t node_id,
                          const std::string& world_id,
                          clips::ProtoMessageId parameters_proto_id,
                          clips::ProtoMessageId python_code_proto_id,
                          const std::string& parameter_message_full_name,
                          const std::string& return_value_message_full_name,
                          clips::ProtoMessageId file_descriptor_set_proto_id,
                          clips::TraceSpanReferenceId parent_span_reference_id)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(assert_facade_->GetClipsMutex());

  void CancelCodeExecution(const std::string& operation_name,
                           const clips::Symbol& tree_id, int64_t node_id)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(assert_facade_->GetClipsMutex());

  // Must be called in the same parent thread as Init().
  absl::Status TearDown();

  // For testing.
  void WaitForFinishAll();

  // See code_execution_status.clp.
  static constexpr std::string_view kCodeExecutionStateRunning = "RUNNING";
  static constexpr std::string_view kCodeExecutionStateCanceling = "CANCELING";
  static constexpr std::string_view kCodeExecutionStateCanceled = "CANCELED";
  static constexpr std::string_view kCodeExecutionStateSucceeded = "SUCCEEDED";
  static constexpr std::string_view kCodeExecutionStateFailed = "FAILED";

 private:
  clips::EnvironmentAssertFacade* assert_facade_;  // externally owned.
  clips::ProtobufManager* proto_mgr_;              // externally owned.
  clips::TraceSpanManager* span_mgr_;              // externally owned.
  intrinsic_proto::executive::CodeExecutionService::StubInterface*
      code_execution_stub_;  // externally owned
  google::protobuf::Duration operation_timeout_;

  std::optional<intrinsic::ThreadPool> bundle_;

  // The CodeExecutionDispatcher maintains ongoing code execution operations in
  // a map: code_executions_.
  // This links the code-execution-instance in CLIPS identified by the tuple of
  // (operation-name [the executive operation], tree-id, node-id) to an ongoing
  // operation stored in CodeExecutionOperation.
  // In this way a cancel call can refer to the code execution by its tuple of
  // CLIPS values and the CodeExecutionDispatcher performs the mapping to an
  // underlying operation name to actually call the cancelation.

  // Used to refer to a code-execution-instance fact that is handling a
  // CodeExecution in CLIPS.
  // Contains: operation name, tree id, node id
  using CodeExecutionInstanceReference =
      std::tuple<std::string, std::string, int64_t>;
  static std::string ToString(const CodeExecutionInstanceReference& cei_ref);
  struct CodeExecutionOperation {
    google::longrunning::Operation operation;
  };
  absl::flat_hash_map<CodeExecutionInstanceReference, CodeExecutionOperation>
      code_executions_ ABSL_GUARDED_BY(code_executions_mutex_);
  absl::Mutex code_executions_mutex_;

 private:
  struct ReportClipsStateUpdateOptions {
    std::optional<google::protobuf::Any> return_value;
    std::optional<intrinsic_proto::status::ExtendedStatus> extended_status;
  };
  // Reports a code-execution-state-update for cei_ref with the given
  // code_execution_status.
  void ReportClipsStateUpdate(std::string_view code_execution_status,
                              const CodeExecutionInstanceReference& cei_ref,
                              ReportClipsStateUpdateOptions options = {})
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(assert_facade_->GetClipsMutex());

  // Reports a code-execution-response-stdout for cei_ref with the given stdout
  // and sequence.
  void ReportClipsResponseStdout(std::string_view stdout, int64_t sequence,
                                 const CodeExecutionInstanceReference& cei_ref,
                                 clips::EnvironmentAssertFacade* assert_facade)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(assert_facade->GetClipsMutex());

  // Sends a request to the code execution service to restart all Jupyter
  // kernels. Should be called after an operation finished to trigger a kernel
  // restart as soon as possible rather than have the code execution service
  // restart the kernel once a request with a new operation id set as scope
  // comes in.
  absl::Status ResetKernels(clips::TraceSpanReferenceId parent_span_id);

  // Retrieves proto_id from proto_mgr and returns the proto on success.
  // If the proto cannot be found, a failed code-execution-state-update is
  // asserted for the given cei_ref. Further execution is expected to stop in
  // that case.
  template <typename M, typename>
  absl::StatusOr<M> GetProtoOrReportError(
      clips::ProtoMessageId proto_id, clips::ProtobufManager* proto_mgr,
      const CodeExecutionDispatcher::CodeExecutionInstanceReference& cei_ref,
      clips::EnvironmentAssertFacade* assert_facade)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(assert_facade->GetClipsMutex());
};

template <typename M, typename = std::enable_if_t<
                          std::is_base_of_v<google::protobuf::Message, M>>>
inline absl::StatusOr<M> CodeExecutionDispatcher::GetProtoOrReportError(
    clips::ProtoMessageId proto_id, clips::ProtobufManager* proto_mgr,
    const CodeExecutionDispatcher::CodeExecutionInstanceReference& cei_ref,
    clips::EnvironmentAssertFacade* assert_facade)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(assert_facade_->GetClipsMutex()) {
  assert_facade->GetClipsMutex()->AssertHeld();
  absl::StatusOr<std::unique_ptr<M>> proto = proto_mgr->GetProtoAs<M>(proto_id);
  if (!proto.ok()) {
    absl::string_view msg_type = M::descriptor()->full_name();
    ReportClipsStateUpdate(
        kCodeExecutionStateFailed, cei_ref,
        {.extended_status = CreateExtendedStatus(
             14007,
             absl::StrFormat("Could not retrieve %s proto.", msg_type))});
    return proto.status();
  }
  return std::move(**proto);
}

}  // namespace executive
}  // namespace intrinsic

#endif  // INTRINSIC_EXECUTIVE_ENGINE_CODE_EXECUTION_DISPATCHER_H_
