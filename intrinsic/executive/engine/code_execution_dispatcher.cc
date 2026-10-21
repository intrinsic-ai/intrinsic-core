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

#include "intrinsic/executive/engine/code_execution_dispatcher.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/cleanup/cleanup.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "grpcpp/client_context.h"
#include "grpcpp/support/status.h"
#include "grpcpp/support/sync_stream.h"
#include "grpcpp/support/time.h"  // for grpc::TimePoint<absl::Time>
#include "intrinsic/executive/clips_cpp/assert_facade.h"
#include "intrinsic/executive/clips_cpp/function_facade.h"
#include "intrinsic/executive/clips_cpp/protobuf.h"
#include "intrinsic/executive/clips_cpp/readonly_facade.h"
#include "intrinsic/executive/clips_cpp/trace_span_manager.h"
#include "intrinsic/executive/clips_cpp/value.h"
#include "intrinsic/executive/proto/code_execution_service.grpc.pb.h"
#include "intrinsic/executive/proto/code_execution_service.pb.h"
#include "intrinsic/stats/scoped_span.h"
#include "intrinsic/util/grpc/grpc.h"
#include "intrinsic/util/proto_time.h"
#include "intrinsic/util/status/get_extended_status.h"
#include "intrinsic/util/status/return.h"
#include "intrinsic/util/status/status_builder.h"
#include "intrinsic/util/status/status_conversion_grpc.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/status/status_specs.h"
#include "intrinsic/util/thread/thread.h"
#include "opentelemetry/trace/span_context.h"
#include "opentelemetry/trace/span_startoptions.h"

namespace intrinsic {
namespace executive {

namespace {

// code/code_execution_service.go hardcodes the timeout, thus pick that value up
// here until there is a user-configurable timeout.
constexpr absl::Duration kOperationTimeout = absl::Seconds(65);
constexpr absl::Duration kGrpcTimeout = absl::Seconds(30);
constexpr absl::Duration kGrpcTimeoutResetKernels = absl::Seconds(5);

}  // namespace

CodeExecutionDispatcher::CodeExecutionDispatcher(
    clips::EnvironmentAssertFacade* assert_facade,
    clips::ProtobufManager* proto_manager,
    clips::TraceSpanManager* span_manager,
    intrinsic_proto::executive::CodeExecutionService::StubInterface*
        code_execution_stub)
    : assert_facade_(assert_facade),
      proto_mgr_(proto_manager),
      span_mgr_(span_manager),
      code_execution_stub_(code_execution_stub) {}

absl::Status CodeExecutionDispatcher::Init(
    clips::EnvironmentFunctionFacade* function_facade,
    clips::EnvironmentReadonlyFacade* readonly_facade) {
  if (bundle_.has_value()) {
    return absl::FailedPreconditionError(
        "CodeExecutionDispatcher::Init() must be called exactly once (and it "
        "already has been called before)");
  }

  INTR_ASSIGN_OR_RETURN(operation_timeout_,
                        FromAbslDuration(kOperationTimeout));

  // ensure that code_execution_status.clp has been loaded
  INTR_RETURN_IF_ERROR(
      readonly_facade->GetTemplate("code-execution-state-update").status());

  bundle_.emplace(10);
  INTR_RETURN_IF_ERROR(function_facade->AddFunction(
      "code-execution-start",
      std::function([this](const std::string& operation_name,
                           const clips::Symbol& tree_id, int64_t node_id,
                           const std::string& world_id,
                           int64_t parameters_proto_id,
                           int64_t python_code_proto_id,
                           const std::string& parameter_message_full_name,
                           const std::string& return_value_message_full_name,
                           int64_t file_descriptor_set_proto_id,
                           int64_t parent_span_reference_id) {
        assert_facade_->GetClipsMutex()->AssertHeld();
        StartCodeExecution(
            operation_name, tree_id, node_id, world_id,
            clips::ProtoMessageId(parameters_proto_id),
            clips::ProtoMessageId(python_code_proto_id),
            parameter_message_full_name, return_value_message_full_name,
            clips::ProtoMessageId(file_descriptor_set_proto_id),
            clips::TraceSpanReferenceId(parent_span_reference_id));
      })));
  INTR_RETURN_IF_ERROR(function_facade->AddFunction(
      "code-execution-cancel-async",
      std::function([this](const std::string& operation_name,
                           const clips::Symbol& tree_id, int64_t node_id) {
        assert_facade_->GetClipsMutex()->AssertHeld();
        CancelCodeExecution(operation_name, tree_id, node_id);
      })));

  INTR_RETURN_IF_ERROR(function_facade->AddFunction(
      "code-execution-service-restart-kernels", std::function([this]() {
        assert_facade_->GetClipsMutex()->AssertHeld();
        INTR_RETURN_IF_ERROR(ResetKernels()).LogWarning().With(ReturnVoid());
      })));

  return absl::OkStatus();
}

absl::Status CodeExecutionDispatcher::TearDown() {
  // Invokes the dtor for all threads and cancels prior to joining.
  bundle_.reset();
  return absl::OkStatus();
}

void CodeExecutionDispatcher::WaitForFinishAll() {
  // We are not clearing the bundle here, since semantically we just want to
  // wait for all threads to finish.
  bundle_.emplace(10);
}

std::string CodeExecutionDispatcher::ToString(
    const CodeExecutionDispatcher::CodeExecutionInstanceReference& cei_ref) {
  return absl::StrFormat("%s,%s,%d", std::get<0>(cei_ref), std::get<1>(cei_ref),
                         std::get<2>(cei_ref));
}

void CodeExecutionDispatcher::ReportClipsStateUpdate(
    std::string_view code_execution_status,
    const CodeExecutionInstanceReference& cei_ref,
    ReportClipsStateUpdateOptions options)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(assert_facade_->GetClipsMutex()) {
  const stats::ScopedSpan span(
      "CodeExecutionDispatcher/ReportClipsStateUpdate");
  const std::string& operation_name = std::get<0>(cei_ref);
  const clips::Symbol tree_id = clips::Symbol(std::get<1>(cei_ref));
  const int64_t node_id = std::get<2>(cei_ref);

  clips::ProtoMessageId return_value_id = clips::ProtobufManager::kInvalidId;
  clips::ProtoMessageId extended_status_id = clips::ProtobufManager::kInvalidId;
  if (options.return_value.has_value()) {
    return_value_id =
        proto_mgr_->AddGeneratedProto(*std::move(options.return_value));
    if (return_value_id == clips::ProtobufManager::kInvalidId) {
      LOG(ERROR) << "Failed to add code execution return value.";
    }
  }
  if (options.extended_status.has_value()) {
    extended_status_id =
        proto_mgr_->AddGeneratedProto(*std::move(options.extended_status));
    if (extended_status_id == clips::ProtobufManager::kInvalidId) {
      LOG(ERROR) << "Failed to add code execution extended status.";
    }
  }

  INTR_RETURN_IF_ERROR(
      assert_facade_
          ->AssertFact("code-execution-state-update",
                       {{"operation-name", operation_name},
                        {"tree-id", tree_id},
                        {"node-id", node_id},
                        {"state", clips::Symbol(code_execution_status)},
                        {"return-value-proto", return_value_id.value()},
                        {"extended-status-proto", extended_status_id.value()}})
          .status())
      .LogWarning()
      .With(intrinsic::ExtraMessage()
            << "Failed to report code execution status to CLIPS")
      .With(ReturnVoid());
  assert_facade_->NotifyRunner();
}

void CodeExecutionDispatcher::ReportClipsResponseStdout(
    std::string_view stdout, int64_t sequence,
    const CodeExecutionInstanceReference& cei_ref,
    clips::EnvironmentAssertFacade* assert_facade)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(assert_facade->GetClipsMutex()) {
  const std::string& operation_name = std::get<0>(cei_ref);
  const clips::Symbol tree_id = clips::Symbol(std::get<1>(cei_ref));
  const int64_t node_id = std::get<2>(cei_ref);

  INTR_RETURN_IF_ERROR(assert_facade
                           ->AssertFact("code-execution-response-stdout",
                                        {{"operation-name", operation_name},
                                         {"tree-id", tree_id},
                                         {"node-id", node_id},
                                         {"stdout", clips::Value(stdout)},
                                         {"sequence", clips::Value(sequence)}})
                           .status())
      .LogWarning()
      .With(intrinsic::ExtraMessage()
            << "Failed to report code execution response stdout to CLIPS")
      .With(ReturnVoid());
  assert_facade->NotifyRunner();
}

absl::Status CodeExecutionDispatcher::ResetKernels() {
  if (code_execution_stub_ == nullptr) {
    return absl::FailedPreconditionError("Code execution stub not initialized");
  }
  LOG(INFO) << "Calling CodeExecutionService::ResetKernels RPC.";
  grpc::ClientContext ctx;
  intrinsic::ConfigureClientContext(&ctx);
  ctx.set_deadline(absl::Now() + kGrpcTimeoutResetKernels);

  intrinsic_proto::executive::ResetKernelsRequest reset_kernels_request;
  intrinsic_proto::executive::ResetKernelsResponse reset_kernels_response;
  INTR_RETURN_IF_ERROR(ToAbslStatus(code_execution_stub_->ResetKernels(
      &ctx, reset_kernels_request, &reset_kernels_response)));
  LOG(INFO) << "CodeExecutionService::ResetKernels RPC returned ok.";
  return absl::OkStatus();
}

// Start a code execution for the code-execution-instance associated with
// operation_name,tree_id,node_id.
//
// parameters_proto_id points to an Any with type specified as in the
// CodeExecution proto.
// python_code_proto_id is a PythonCode proto.
void CodeExecutionDispatcher::StartCodeExecution(
    const std::string& operation_name, const clips::Symbol& tree_id,
    int64_t node_id, const std::string& world_id,
    clips::ProtoMessageId parameters_proto_id,
    clips::ProtoMessageId python_code_proto_id,
    const std::string& parameter_message_full_name,
    const std::string& return_value_message_full_name,
    clips::ProtoMessageId file_descriptor_set_proto_id,
    clips::TraceSpanReferenceId parent_span_reference_id) {
  CodeExecutionInstanceReference cei_ref =
      std::make_tuple(operation_name, tree_id.ToString(), node_id);
  if (!code_execution_stub_) {
    intrinsic_proto::status::ExtendedStatus extended_status =
        CreateExtendedStatus(14001, "");
    ReportClipsStateUpdate(kCodeExecutionStateFailed, cei_ref,
                           {.extended_status = extended_status});
    return;
  }

  opentelemetry::trace::SpanContext parent_context =
      opentelemetry::trace::SpanContext::GetInvalid();
  if (span_mgr_ != nullptr) {
    absl::StatusOr<opentelemetry::trace::SpanContext> status_or_context =
        span_mgr_->GetSpanContext(parent_span_reference_id);
    if (!status_or_context.ok()) {
      LOG(ERROR) << "Failed to GetSpanContext: " << status_or_context.status();
    } else {
      parent_context = *status_or_context;
    }
  }

  intrinsic_proto::executive::CodeExecutionRequest execute_request;

  if (!parameter_message_full_name.empty()) {
    INTR_ASSIGN_OR_RETURN(
        *execute_request.mutable_parameters(),
        GetProtoOrReportError<google::protobuf::Any>(
            parameters_proto_id, proto_mgr_, cei_ref, assert_facade_),
        _.With(ReturnVoid()));
  }

  INTR_ASSIGN_OR_RETURN(
      *execute_request.mutable_python_code(),
      GetProtoOrReportError<intrinsic_proto::executive::PythonCode>(
          python_code_proto_id, proto_mgr_, cei_ref, assert_facade_),
      _.With(ReturnVoid()));

  if (!parameter_message_full_name.empty() ||
      !return_value_message_full_name.empty()) {
    INTR_ASSIGN_OR_RETURN(
        *execute_request.mutable_file_descriptor_set(),
        GetProtoOrReportError<google::protobuf::FileDescriptorSet>(
            file_descriptor_set_proto_id, proto_mgr_, cei_ref, assert_facade_),
        _.With(ReturnVoid()));
  }

  execute_request.set_parameter_message_full_name(parameter_message_full_name);
  execute_request.set_return_value_message_full_name(
      return_value_message_full_name);
  execute_request.set_world_id(world_id);
  execute_request.set_scope(operation_name);

  if (auto status = bundle_->Schedule([this, cei_ref, parent_context,
                                       execute_request =
                                           std::move(execute_request)] {
        opentelemetry::trace::StartSpanOptions start_options;
        if (parent_context.IsValid()) {
          start_options.parent = parent_context;
        }
        const stats::ScopedSpan execution_span(
            "CodeExecutionDispatcher/Execute", start_options);
        LOG(INFO) << "Calling code execution for " << ToString(cei_ref);

        grpc::ClientContext execute_ctx;
        intrinsic::ConfigureClientContext(&execute_ctx);
        execute_ctx.set_deadline(absl::Now() + kGrpcTimeout);
        google::longrunning::Operation started_operation;
        grpc::Status execute_status;
        {
          const stats::ScopedSpan execute_rpc_span(
              "CodeExecutionService/ExecuteCode");
          execute_status = code_execution_stub_->ExecuteCode(
              &execute_ctx, execute_request, &started_operation);
        }
        if (!execute_status.ok()) {
          absl::MutexLock clips_lock(*assert_facade_->GetClipsMutex());
          std::optional<intrinsic_proto::status::ExtendedStatus>
              extended_status = GetExtendedStatus(execute_status);
          if (!extended_status.has_value()) {
            extended_status = CreateExtendedStatus(
                20000,
                absl::StrFormat("Failed to start code execution: %s",
                                ToAbslStatus(execute_status).ToString()));
            extended_status->mutable_user_report()->set_instructions(
                "Make sure the code execution service is available. Consult "
                "the troubleshooting guide: "
                "https://flowstate.intrinsic.ai/docs/support/"
                "troubleshooting_guide#solution-building-code-execution for "
                "more information.");
          }
          ReportClipsStateUpdate(kCodeExecutionStateFailed, cei_ref,
                                 {.extended_status = extended_status});
          return;
        }

        {
          absl::MutexLock lock(code_executions_mutex_);
          code_executions_[cei_ref] =
              CodeExecutionOperation{.operation = started_operation};
        }

        absl::Cleanup cleanup_operation = [cei_ref, started_operation, this,
                                           context = execution_span.context()] {
          // Remove the operation entry first to prevent any cancel calls coming
          // in on a deleted operation
          {
            absl::MutexLock lock(code_executions_mutex_);
            code_executions_.erase(cei_ref);
          }

          grpc::ClientContext delete_ctx;
          intrinsic::ConfigureClientContext(&delete_ctx);
          delete_ctx.set_deadline(absl::Now() + kGrpcTimeout);
          google::longrunning::DeleteOperationRequest request;
          request.set_name(started_operation.name());
          google::protobuf::Empty response;
          grpc::Status delete_status;
          {
            opentelemetry::trace::StartSpanOptions options;
            if (context.IsValid()) {
              options.parent = context;
            }
            const stats::ScopedSpan delete_rpc_span(
                "CodeExecutionService/DeleteOperation", options);
            delete_status = code_execution_stub_->DeleteOperation(
                &delete_ctx, request, &response);
          }
          if (!delete_status.ok()) {
            LOG(ERROR) << "Failed to delete code execution for "
                       << ToString(cei_ref) << ": "
                       << ToAbslStatus(delete_status);
          }
        };

        {
          absl::MutexLock clips_lock(*assert_facade_->GetClipsMutex());
          ReportClipsStateUpdate(kCodeExecutionStateRunning, cei_ref);
        }

        // Don't wait any longer than this for the operation to finish so we
        // don't end up stuck if the code execution hangs forever.
        absl::Time wait_deadline =
            absl::Now() + kOperationTimeout + kGrpcTimeout;

        // Create the context for the streaming RPC outside the thread to allow
        // cancelling it after waiting is complete. The stream should
        // automatically end when execution ends, but it seems more complete to
        // also request manual cancellation after we stop waiting.
        grpc::ClientContext stream_ctx;
        intrinsic::ConfigureClientContext(&stream_ctx);
        stream_ctx.set_deadline(wait_deadline);

        // Start a separate thread to receive non-final responses during the
        // operation. This will be things like messages emitted to stdout by the
        // executed code. The thread pushes these messages to `RunMetadata` in
        // the background as long as the operation is ongoing.
        intrinsic::Thread stream_thread([this, cei_ref, &stream_ctx,
                                         started_operation,
                                         parent_context =
                                             execution_span.context()]() {
          opentelemetry::trace::StartSpanOptions options;
          if (parent_context.IsValid()) {
            options.parent = parent_context;
          }
          const stats::ScopedSpan stream_span(
              "CodeExecutionService/StreamExecuteCodeResponse", options);
          intrinsic_proto::executive::StreamExecuteCodeResponseRequest
              stream_request;
          stream_request.set_operation_name(started_operation.name());
          std::unique_ptr<grpc::ClientReaderInterface<
              intrinsic_proto::executive::StreamExecuteCodeResponseResponse>>
              execute_code_response_reader(
                  code_execution_stub_->StreamExecuteCodeResponse(
                      &stream_ctx, stream_request));
          intrinsic_proto::executive::StreamExecuteCodeResponseResponse
              response;
          int64_t sequence = 0;
          while (execute_code_response_reader->Read(&response)) {
            absl::MutexLock clips_lock(*assert_facade_->GetClipsMutex());
            ReportClipsResponseStdout(response.stdout(), sequence++, cei_ref,
                                      assert_facade_);
          }
          grpc::Status execute_code_response_reader_status =
              execute_code_response_reader->Finish();
          // This failure isn't a huge deal. It just means that there was some
          // issue streaming responses, but these aren't critical to the
          // functionality of code execution. Log and continue.
          if (!execute_code_response_reader_status.ok()) {
            LOG(WARNING) << "Streaming code execution responses failed: "
                         << ToAbslStatus(execute_code_response_reader_status);
          }
        });

        // Actual long-running call waiting for the execution to finish
        grpc::ClientContext wait_ctx;
        intrinsic::ConfigureClientContext(&wait_ctx);
        wait_ctx.set_deadline(wait_deadline);
        google::longrunning::WaitOperationRequest execute_wait_request;
        execute_wait_request.set_name(started_operation.name());
        *execute_wait_request.mutable_timeout() = operation_timeout_;
        google::longrunning::Operation execute_result_operation;

        grpc::Status wait_status;
        {
          const stats::ScopedSpan wait_rpc_span(
              "CodeExecutionService/WaitOperation");
          wait_status = code_execution_stub_->WaitOperation(
              &wait_ctx, execute_wait_request, &execute_result_operation);
        }

        // Wait for the stream thread to finish. Ensures that we don't miss a
        // potential last stream message being processed. The end of the
        // operation should always end the stream and thus the thread. If not,
        // the thread still has a deadline.
        stream_thread.join();

        if (!wait_status.ok()) {
          absl::MutexLock clips_lock(*assert_facade_->GetClipsMutex());
          std::optional<intrinsic_proto::status::ExtendedStatus>
              extended_status = GetExtendedStatus(wait_status);
          if (!extended_status.has_value()) {
            extended_status = CreateExtendedStatus(
                20000, ToAbslStatus(wait_status).ToString());
          }
          ReportClipsStateUpdate(kCodeExecutionStateFailed, cei_ref,
                                 {.extended_status = extended_status});
          return;
        }

        std::optional<google::protobuf::Any> result;
        std::optional<intrinsic_proto::status::ExtendedStatus> extended_status;
        std::string result_state = std::string(kCodeExecutionStateFailed);
        if (execute_result_operation.done()) {
          if (execute_result_operation.has_response()) {
            // If response is set, then the operation is done with SUCCESS
            result_state = kCodeExecutionStateSucceeded;
            intrinsic_proto::executive::CodeExecutionResponse result_response;
            if (execute_result_operation.response().UnpackTo(
                    &result_response) &&
                !result_response.return_value().type_url().empty()) {
              result = result_response.return_value();
            }
          } else if (execute_result_operation.has_error()) {
            // If error is set, then the operation could be CANCELED by the
            // service, otherwise it FAILED.
            if (execute_result_operation.error().code() ==
                grpc::StatusCode::CANCELLED) {
              result_state = kCodeExecutionStateCanceled;
            } else {
              extended_status =
                  GetExtendedStatus(execute_result_operation.error());
              if (!extended_status.has_value()) {
                extended_status = CreateExtendedStatus(
                    14000,
                    absl::StrFormat(
                        "%d: %s", execute_result_operation.error().code(),
                        execute_result_operation.error().message()));
              }
            }
          }
        }

        {
          absl::MutexLock clips_lock(*assert_facade_->GetClipsMutex());
          ReportClipsStateUpdate(
              result_state, cei_ref,
              {.return_value = result, .extended_status = extended_status});
        }
      });
      !status.ok()) {
    intrinsic_proto::status::ExtendedStatus extended_status =
        CreateExtendedStatus(14002, status.ToString());
    ReportClipsStateUpdate(kCodeExecutionStateFailed, cei_ref,
                           {.extended_status = extended_status});
  }
}

void CodeExecutionDispatcher::CancelCodeExecution(
    const std::string& operation_name, const clips::Symbol& tree_id,
    int64_t node_id) {
  CodeExecutionInstanceReference cei_ref =
      std::make_tuple(operation_name, tree_id.ToString(), node_id);
  if (!code_execution_stub_) {
    intrinsic_proto::status::ExtendedStatus extended_status =
        CreateExtendedStatus(14001, "");
    ReportClipsStateUpdate(kCodeExecutionStateFailed, cei_ref,
                           {.extended_status = extended_status});
    return;
  }

  if (auto status = bundle_->Schedule([this, cei_ref] {
        LOG(INFO) << "Canceling code execution for " << ToString(cei_ref);

        grpc::ClientContext cancel_ctx;
        intrinsic::ConfigureClientContext(&cancel_ctx);
        cancel_ctx.set_deadline(absl::Now() + kGrpcTimeout);
        google::longrunning::CancelOperationRequest request;
        {
          absl::MutexLock lock(code_executions_mutex_);
          auto cancel_op = code_executions_.find(cei_ref);
          if (cancel_op == code_executions_.end()) {
            LOG(WARNING) << "Cannot cancel code execution for "
                         << ToString(cei_ref)
                         << " as no operation exists. Maybe it just fininshed?";
            return;
          }
          request.set_name(cancel_op->second.operation.name());
        }
        google::protobuf::Empty response;
        grpc::Status cancel_status = code_execution_stub_->CancelOperation(
            &cancel_ctx, request, &response);
        if (!cancel_status.ok()) {
          LOG(WARNING) << "Cannot cancel code execution for "
                       << ToString(cei_ref) << ": "
                       << ToAbslStatus(cancel_status)
                       << " Maybe it just fininshed?";
          return;
        }

        {
          absl::MutexLock clips_lock(*assert_facade_->GetClipsMutex());
          ReportClipsStateUpdate(kCodeExecutionStateCanceling, cei_ref);
        }
      });
      !status.ok()) {
    LOG(ERROR) << "Failed to cancel code execution: " << status;
  }
}

}  // namespace executive
}  // namespace intrinsic
