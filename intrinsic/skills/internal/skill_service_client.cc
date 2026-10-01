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

#include "intrinsic/skills/internal/skill_service_client.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "google/longrunning/operations.pb.h"
#include "google/protobuf/any.pb.h"
#include "google/protobuf/empty.pb.h"
#include "google/protobuf/message.h"
#include "google/rpc/status.pb.h"
#include "grpcpp/channel.h"
#include "grpcpp/client_context.h"
#include "grpcpp/completion_queue.h"
#include "grpcpp/support/async_unary_call.h"
#include "grpcpp/support/status.h"
#include "intrinsic/connect/cc/grpc/channel.h"
#include "intrinsic/logging/data_logger_client.h"
#include "intrinsic/logging/errors/proto/error_report.pb.h"
#include "intrinsic/logging/proto/context.pb.h"
#include "intrinsic/logging/proto/log_item.pb.h"
#include "intrinsic/logging/utils/logger_labels.h"
#include "intrinsic/skills/cc/client_common.h"
#include "intrinsic/skills/internal/error_utils.h"
#include "intrinsic/skills/internal/skill_service_client_interface.h"
#include "intrinsic/skills/proto/footprint.pb.h"
#include "intrinsic/skills/proto/skill_service.grpc.pb.h"
#include "intrinsic/skills/proto/skill_service.pb.h"
#include "intrinsic/skills/proto/skills.pb.h"
#include "intrinsic/stats/scoped_span.h"
#include "intrinsic/stats/tracing_utils.h"
#include "intrinsic/util/grpc/grpc.h"
#include "intrinsic/util/proto/any.h"
#include "intrinsic/util/proto_time.h"
#include "intrinsic/util/status/status_conversion_grpc.h"
#include "intrinsic/util/status/status_conversion_rpc.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/thread/concurrent_queue.h"
#include "intrinsic/util/thread/stop_token.h"
#include "intrinsic/util/thread/thread.h"
#include "opentelemetry/trace/span.h"

namespace intrinsic {
namespace skills {

namespace {

constexpr int kAsyncLogQueueSize = 200;

absl::StatusOr<intrinsic_proto::skills::GetFootprintRequest>
BuildGetFootprintRequestAndContextWithoutWorld(
    const intrinsic_proto::skills::SkillInstance& instance,
    const google::protobuf::Any& params,
    const intrinsic_proto::data_logger::Context& log_context) {
  intrinsic_proto::skills::GetFootprintRequest request;
  *request.mutable_instance() = instance;
  *request.mutable_parameters() = params;
  intrinsic_proto::data_logger::Context skill_context = log_context;
  skill_context.set_skill_id(data_logger::GenerateUid());
  *request.mutable_context() = skill_context;

  return request;
}

absl::StatusOr<intrinsic_proto::skills::ExecuteRequest>
BuildExecuteRequestAndContextWithoutWorld(
    const intrinsic_proto::skills::SkillInstance& instance,
    const google::protobuf::Message& params,
    const intrinsic_proto::skills::Footprint& footprint,
    const intrinsic_proto::data_logger::Context& log_context) {
  intrinsic_proto::skills::ExecuteRequest request;
  *request.mutable_instance() = instance;
  request.mutable_parameters()->PackFrom(params);
  *request.mutable_footprint() = footprint;
  intrinsic_proto::data_logger::Context skill_context = log_context;
  if (log_context.skill_id() != 0) {
    // This allows to track the hierarchy of skill executions for internal
    // skills, i.e. skills executed from within a skill.
    skill_context.set_parent_skill_id(log_context.skill_id());
  }
  skill_context.set_skill_id(data_logger::GenerateUid());
  *request.mutable_context() = skill_context;

  return request;
}

absl::StatusOr<intrinsic_proto::skills::ExecuteRequest>
BuildExecuteRequestAndContextWithoutWorld(
    const intrinsic_proto::skills::SkillInstance& instance,
    const google::protobuf::Any& params,
    const intrinsic_proto::skills::Footprint& footprint,
    const intrinsic_proto::data_logger::Context& log_context) {
  intrinsic_proto::skills::ExecuteRequest request;
  *request.mutable_instance() = instance;
  *request.mutable_parameters() = params;
  *request.mutable_footprint() = footprint;
  intrinsic_proto::data_logger::Context skill_context = log_context;
  if (log_context.skill_id() != 0) {
    // This allows to track the hierarchy of skill executions for internal
    // skills, i.e. skills executed from within a skill.
    skill_context.set_parent_skill_id(log_context.skill_id());
  }
  skill_context.set_skill_id(data_logger::GenerateUid());
  *request.mutable_context() = skill_context;

  return request;
}

absl::StatusOr<intrinsic_proto::skills::PreviewRequest>
BuildPreviewRequestAndContextWithoutWorld(
    const intrinsic_proto::skills::SkillInstance& instance,
    const google::protobuf::Message& params,
    const intrinsic_proto::skills::Footprint& footprint,
    const intrinsic_proto::data_logger::Context& log_context) {
  intrinsic_proto::skills::PreviewRequest request;
  *request.mutable_instance() = instance;
  request.mutable_parameters()->PackFrom(params);
  *request.mutable_footprint() = footprint;
  intrinsic_proto::data_logger::Context skill_context = log_context;
  if (log_context.skill_id() != 0) {
    // This allows to track the hierarchy of skill executions for internal
    // skills, i.e. skills executed from within a skill.
    skill_context.set_parent_skill_id(log_context.skill_id());
  }
  skill_context.set_skill_id(data_logger::GenerateUid());
  *request.mutable_context() = skill_context;

  return request;
}

absl::StatusOr<intrinsic_proto::skills::PreviewRequest>
BuildPreviewRequestAndContextWithoutWorld(
    const intrinsic_proto::skills::SkillInstance& instance,
    const google::protobuf::Any& params,
    const intrinsic_proto::skills::Footprint& footprint,
    const intrinsic_proto::data_logger::Context& log_context) {
  intrinsic_proto::skills::PreviewRequest request;
  *request.mutable_instance() = instance;
  *request.mutable_parameters() = params;
  *request.mutable_footprint() = footprint;
  intrinsic_proto::data_logger::Context skill_context = log_context;
  if (log_context.skill_id() != 0) {
    // This allows to track the hierarchy of skill executions for internal
    // skills, i.e. skills executed from within a skill.
    skill_context.set_parent_skill_id(log_context.skill_id());
  }
  skill_context.set_skill_id(data_logger::GenerateUid());
  *request.mutable_context() = skill_context;

  return request;
}

void LogQueueReader(
    StopToken stop_token,
    ConcurrentQueue<intrinsic_proto::data_logger::LogItem>& log_queue) {
  while (!stop_token.stop_requested() || !log_queue.IsEmpty()) {
    absl::StatusOr<intrinsic_proto::data_logger::LogItem> item_status =
        log_queue.Dequeue(absl::Milliseconds(500));

    if (!item_status.ok()) {
      // Dequeue() returned an error, which means the queue is empty.
      // check if stop_token is requested
      continue;
    }

    intrinsic_proto::data_logger::LogItem item = std::move(item_status.value());

    const stats::ScopedSpan scoped_span(
        "SkillServiceClient/LogQueueReader/ProcessItem");
    scoped_span.AddAttribute("size_bytes", item.ByteSizeLong());

    if (absl::Status status = data_logger::LogAndAwaitResponse(std::move(item));
        !status.ok()) {
      LOG(WARNING) << "Failed to log item; " << status;
    }
  }
}

}  // namespace

absl::StatusOr<std::unique_ptr<SkillServiceClient>> CreateSkillServiceClient(
    intrinsic_proto::skills::SkillInstance instance,
    const SkillServiceClientConfig& config) {
  const std::string& project_address = instance.project_handle().grpc_target();
  const std::string& execute_address = instance.execute_handle().grpc_target();

  const absl::Time deadline = absl::Now() + config.timeout;
  INTR_ASSIGN_OR_RETURN(std::shared_ptr<::grpc::Channel> project_channel,
                        connect::CreateClientChannel(
                            project_address, deadline,
                            connect::UnlimitedMessageSizeGrpcChannelArgs()));
  INTR_ASSIGN_OR_RETURN(std::shared_ptr<::grpc::Channel> execute_channel,
                        connect::CreateClientChannel(
                            execute_address, deadline,
                            connect::UnlimitedMessageSizeGrpcChannelArgs()));
  return std::make_unique<SkillServiceClient>(
      std::move(instance),
      intrinsic_proto::skills::Projector::NewStub(project_channel),
      intrinsic_proto::skills::Executor::NewStub(execute_channel), config);
}

void SkillServiceClient::InitConcurrentLogging() {
  log_queue_.emplace(kAsyncLogQueueSize);
  thread_ = Thread(LogQueueReader, std::ref(log_queue_.value()));
}

void SkillServiceClient::TearDownConcurrentLogging() {
  // The order is important here, since we first need to stop the thread
  // before we can destroy the queue in order to prevent dangling references.
  if (thread_.joinable()) {
    thread_.request_stop();
    thread_.join();
  }
  log_queue_.reset();
}

absl::Status SkillServiceClient::AddLogRequest(
    const intrinsic_proto::data_logger::LogItem& log_item) {
  intrinsic_proto::data_logger::LogItem log_item_with_label = log_item;
  INTR_RETURN_IF_ERROR(
      AddSkillLogIdLabel(*log_item_with_label.mutable_context()));
  if (!log_queue_.has_value()) {
    LOG_EVERY_N_SEC(WARNING, 5)
        << "Concurrent logging not initialized, logging directly";
    data_logger::LogAsync(log_item_with_label);
    return absl::OkStatus();
  }

  if (absl::Status enqueue_status =
          log_queue_->Enqueue(log_item_with_label, absl::ZeroDuration());
      !enqueue_status.ok()) {
    LOG_EVERY_N_SEC(WARNING, 5)
        << "Logging queue is full, dropping log request";
    return enqueue_status;
  }

  return absl::OkStatus();
}

absl::StatusOr<intrinsic_proto::skills::GetFootprintResult>
SkillServiceClient::GetFootprint(
    absl::string_view world_id, const google::protobuf::Any& params,
    std::optional<absl::Duration> timeout,
    const intrinsic_proto::data_logger::Context& log_context) {
  INTR_ASSIGN_OR_RETURN(intrinsic_proto::skills::GetFootprintRequest request,
                        BuildGetFootprintRequestAndContextWithoutWorld(
                            instance_, params,
                            log_context));

  request.set_world_id(world_id);

  return GetFootprint(request, timeout, request.context());
}

absl::Status SkillServiceClient::TryCancelGetFootprint() {
  absl::MutexLock l(&footprint_mutex_);
  if (!footprint_context_) {
    return absl::InternalError("Footprint context is not present.");
  }
  footprint_context_->TryCancel();
  return absl::OkStatus();
}

absl::StatusOr<intrinsic_proto::skills::GetFootprintResult>
SkillServiceClient::GetFootprint(
    const intrinsic_proto::skills::GetFootprintRequest& request,
    std::optional<absl::Duration> timeout,
    const intrinsic_proto::data_logger::Context& log_context) {
  const stats::ScopedSpan span("SkillServiceClient/GetFootprint");

  absl::ReleasableMutexLock lock(&footprint_mutex_);
  footprint_context_ = std::make_unique<::grpc::ClientContext>();
  absl::Time start_time = absl::Now();
  absl::Duration resolved_timeout =
      timeout.value_or(skills::kClientDefaultTimeout);
  footprint_context_->set_deadline(
      absl::ToChronoTime(start_time + resolved_timeout));

  {
    const stats::ScopedSpan span("SkillServiceClient/GetFootprint/LogRequest");

    intrinsic_proto::data_logger::LogItem log;
    log.mutable_metadata()->set_event_source("skills.footprint_request");
    *log.mutable_payload()->mutable_skills_get_footprint_request() = request;
    *log.mutable_context() = log_context;
    AddLogRequest(std::move(log)).IgnoreError();
  }

  intrinsic_proto::skills::GetFootprintResult response;

  ::grpc::CompletionQueue cq;
  ::grpc::Status rpc_status;
  int tag = 1;

  {
    auto rpc = projector_stub_->AsyncGetFootprint(footprint_context_.get(),
                                                  request, &cq);
    if (!rpc) {
      return absl::InternalError("Cannot create rpc call");
    }
    rpc->Finish(&response, &rpc_status, &tag);

    // Unlock the context, it may now be used for cancelling the call
    lock.Release();

    void* got_tag;
    bool ok = false;

    if (!cq.Next(&got_tag, &ok) || !ok || *static_cast<int*>(got_tag) != tag) {
      return absl::CancelledError("Could not start GetFootprint call");
    }
  }

  absl::Status result_status = ToAbslStatus(rpc_status);
  intrinsic_proto::skills::FootprintSummary summary;
  *summary.mutable_get_footprint_result() = response;
  INTR_ASSIGN_OR_RETURN(auto duration,
                        FromAbslDuration(absl::Now() - start_time));
  *summary.mutable_duration() = duration;
  *summary.mutable_instance() = instance_;
  AddErrorToSummary(result_status, summary);

  {
    const stats::ScopedSpan span("SkillServiceClient/GetFootprint/LogSummary");

    intrinsic_proto::data_logger::LogItem log;
    log.mutable_metadata()->set_event_source("skills.footprint_summary");
    *log.mutable_payload()->mutable_skills_footprint_summary() = summary;
    *log.mutable_context() = log_context;
    AddLogRequest(std::move(log)).IgnoreError();
  }

  if (!result_status.ok()) {
    return result_status;
  }

  return response;
}

absl::StatusOr<intrinsic_proto::skills::ExecuteResult>
SkillServiceClient::Execute(
    absl::string_view world_id, intrinsic_proto::skills::Footprint footprint,
    const google::protobuf::Message& params,
    std::optional<absl::Duration> timeout,
    const intrinsic_proto::data_logger::Context& log_context) {
  INTR_ASSIGN_OR_RETURN(intrinsic_proto::skills::ExecuteRequest request,
                        BuildExecuteRequestAndContextWithoutWorld(
                            instance_, params, footprint,
                            log_context));
  request.set_world_id(world_id);

  return Execute(request, timeout, request.context());
}

absl::StatusOr<intrinsic_proto::skills::ExecuteResult>
SkillServiceClient::Execute(
    absl::string_view world_id, intrinsic_proto::skills::Footprint footprint,
    const google::protobuf::Any& params,
    std::optional<absl::Duration> timeout,
    const intrinsic_proto::data_logger::Context& log_context) {
  INTR_ASSIGN_OR_RETURN(intrinsic_proto::skills::ExecuteRequest request,
                        BuildExecuteRequestAndContextWithoutWorld(
                            instance_, params, footprint,
                            log_context));
  request.set_world_id(world_id);

  return Execute(request, timeout, request.context());
}

void SkillServiceClient::ExecuteContext::LogExecuteRequest() {
  intrinsic_proto::data_logger::LogItem log;
  log.mutable_metadata()->set_event_source("skills.execute_request");
  *log.mutable_payload()->mutable_skills_execute_request() = request_;
  *log.mutable_context() = log_context_;
  client_.AddLogRequest(std::move(log)).IgnoreError();
}

void SkillServiceClient::ExecuteContext::LogSummary(
    const absl::StatusOr<intrinsic_proto::skills::ExecuteResult>& response) {
  intrinsic_proto::skills::ExecutionSummary summary;
  if (response.ok()) {
    *summary.mutable_result() = *response;
  } else {
    AddErrorToSummary(response.status(), summary);
  }

  // TODO(keegang): Add back after adding opencensus to release.
  summary.set_trace_id(
      stats::GetTraceIdHex(stats::GetCurrentSpan()->GetContext()));

  auto duration = FromAbslDuration(absl::Now() - start_time_);
  if (duration.ok()) {
    *summary.mutable_duration() = *duration;
  } else {
    LOG(WARNING) << "Failed to set summary duration: " << duration.status();
  }
  *summary.mutable_instance() = skill_instance_;

  {
    const stats::ScopedSpan span("SkillServiceClient/Execute/LogSummary");

    intrinsic_proto::data_logger::LogItem log;
    log.mutable_metadata()->set_event_source("skills.execution_summary");
    *log.mutable_payload()->mutable_skills_execution_summary() =
        std::move(summary);
    *log.mutable_context() = log_context_;
    client_.AddLogRequest(std::move(log)).IgnoreError();
  }
}

absl::StatusOr<intrinsic_proto::skills::ExecuteResult>
SkillServiceClient::Execute(
    const intrinsic_proto::skills::ExecuteRequest& request,
    std::optional<absl::Duration> timeout,
    const intrinsic_proto::data_logger::Context& log_context) {
  INTR_ASSIGN_OR_RETURN(
      std::unique_ptr<
          SkillServiceClientInterface::ExecuteClientContextInterface>
          exec_context,
      StartExecute(request));

  absl::StatusOr<intrinsic_proto::skills::ExecuteResult> response =
      WaitForExecute(timeout);

  exec_context->LogSummary(response);

  return response;
}

absl::StatusOr<
    std::unique_ptr<SkillServiceClientInterface::ExecuteClientContextInterface>>
SkillServiceClient::StartExecute(
    absl::string_view world_id, intrinsic_proto::skills::Footprint footprint,
    const google::protobuf::Message& params,
    const intrinsic_proto::data_logger::Context& log_context) {
  INTR_ASSIGN_OR_RETURN(intrinsic_proto::skills::ExecuteRequest request,
                        BuildExecuteRequestAndContextWithoutWorld(
                            instance_, params, footprint,
                            log_context));
  request.set_world_id(world_id);

  return StartExecute(request);
}

absl::StatusOr<
    std::unique_ptr<SkillServiceClientInterface::ExecuteClientContextInterface>>
SkillServiceClient::StartExecute(
    absl::string_view world_id, intrinsic_proto::skills::Footprint footprint,
    const google::protobuf::Any& params,
    const intrinsic_proto::data_logger::Context& log_context) {
  INTR_ASSIGN_OR_RETURN(intrinsic_proto::skills::ExecuteRequest request,
                        BuildExecuteRequestAndContextWithoutWorld(
                            instance_, params, footprint,
                            log_context));
  request.set_world_id(world_id);

  return StartExecute(request);
}

absl::StatusOr<
    std::unique_ptr<SkillServiceClientInterface::ExecuteClientContextInterface>>
SkillServiceClient::StartExecute(
    const intrinsic_proto::skills::ExecuteRequest& request) {
  auto exec_context = std::make_unique<ExecuteContext>(
      *this, instance_, request, request.context());

  const stats::ScopedSpan span("SkillServiceClient/StartExecute");

  {
    const stats::ScopedSpan span("SkillServiceClient/StartExecute/LogRequest");
    exec_context->LogExecuteRequest();
  }

  ::grpc::ClientContext execute_grpc_client_context;
  intrinsic::ConfigureClientContext(&execute_grpc_client_context);
  google::longrunning::Operation operation;

  absl::Status start_status = ToAbslStatus(executor_stub_->StartExecute(
      &execute_grpc_client_context, request, &operation));
  if (!start_status.ok()) {
    return start_status;
  }

  {
    absl::MutexLock lock(&operation_name_mutex_);
    if (!operation_name_.empty()) {
      return absl::InternalError(
          "A SkillServiceClient instance can only be used for one skill "
          "execution operation.");
    }
    operation_name_ = operation.name();
  }

  return exec_context;
}

absl::StatusOr<intrinsic_proto::skills::ExecuteResult>
SkillServiceClient::WaitForExecuteWithContext(
    ::grpc::ClientContext& wait_ctx, std::optional<absl::Duration> timeout) {
  const stats::ScopedSpan span("SkillServiceClient/WaitForExecute");

  if (operation_name_.empty()) {
    return absl::InternalError("Must call StartExecute before WaitForExecute.");
  }

  google::longrunning::WaitOperationRequest wait_req;
  google::longrunning::Operation operation;
  wait_req.set_name(operation_name_);
  if (timeout.has_value()) {
    INTR_ASSIGN_OR_RETURN(*wait_req.mutable_timeout(),
                          FromAbslDuration(timeout.value()));
  }
  INTR_RETURN_IF_ERROR(ToAbslStatus(
      executor_stub_->WaitOperation(&wait_ctx, wait_req, &operation)));

  if (operation.has_response()) {
    return UnpackAny<intrinsic_proto::skills::ExecuteResult>(
        operation.response());
  }
  LOG(INFO) << "No response: " << operation;

  return ToAbslStatus(operation.error());
}

absl::Status SkillServiceClient::TryCancelExecute() {
  const stats::ScopedSpan span("SkillServiceClient/TryCancelExecute");

  ::grpc::ClientContext ctx;
  intrinsic::ConfigureClientContext(&ctx);
  google::longrunning::CancelOperationRequest cancel_req;
  {
    absl::MutexLock lock(&operation_name_mutex_);
    if (operation_name_.empty()) {
      return absl::InternalError(
          "Must call StartExecute before TryCancelExecute.");
    }
    cancel_req.set_name(operation_name_);
  }
  google::protobuf::Empty empty;
  return ToAbslStatus(
      executor_stub_->CancelOperation(&ctx, cancel_req, &empty));
}

absl::StatusOr<intrinsic_proto::skills::PreviewResult>
SkillServiceClient::Preview(
    absl::string_view world_id, intrinsic_proto::skills::Footprint footprint,
    const google::protobuf::Message& params,
    std::optional<absl::Duration> timeout,
    const intrinsic_proto::data_logger::Context& log_context) {
  INTR_ASSIGN_OR_RETURN(intrinsic_proto::skills::PreviewRequest request,
                        BuildPreviewRequestAndContextWithoutWorld(
                            instance_, params, footprint,
                            log_context));
  request.set_world_id(world_id);

  return Preview(request, timeout, request.context());
}

absl::StatusOr<intrinsic_proto::skills::PreviewResult>
SkillServiceClient::Preview(
    absl::string_view world_id, intrinsic_proto::skills::Footprint footprint,
    const google::protobuf::Any& params,
    std::optional<absl::Duration> timeout,
    const intrinsic_proto::data_logger::Context& log_context) {
  INTR_ASSIGN_OR_RETURN(intrinsic_proto::skills::PreviewRequest request,
                        BuildPreviewRequestAndContextWithoutWorld(
                            instance_, params, footprint,
                            log_context));
  request.set_world_id(world_id);

  return Preview(request, timeout, request.context());
}

absl::StatusOr<intrinsic_proto::skills::PreviewResult>
SkillServiceClient::Preview(
    const intrinsic_proto::skills::PreviewRequest& request,
    std::optional<absl::Duration> timeout,
    const intrinsic_proto::data_logger::Context& log_context) {
  const stats::ScopedSpan span("SkillServiceClient/Preview");

  // Start the preview then, if successful, wait for the result.
  const absl::Time start_time = absl::Now();
  absl::Status logged_status;
  intrinsic_proto::skills::PreviewResult response;
  auto start_result = StartPreview(request);
  if (start_result.ok()) {
    auto wait_result = WaitForPreview(timeout);
    logged_status = wait_result.status();
    if (wait_result.ok()) {
      response = *wait_result;
    }
  } else {
    logged_status = start_result;
  }

  // Log the summary.
  intrinsic_proto::skills::PreviewSummary summary;
  *summary.mutable_result() = response;

  // TODO(keegang): Add back after adding opencensus to release.
  summary.set_trace_id(
      stats::GetTraceIdHex(stats::GetCurrentSpan()->GetContext()));

  INTR_ASSIGN_OR_RETURN(auto duration,
                        FromAbslDuration(absl::Now() - start_time));
  *summary.mutable_duration() = duration;
  *summary.mutable_instance() = instance_;
  AddErrorToSummary(logged_status, summary);

  {
    const stats::ScopedSpan span("SkillServiceClient/Preview/LogSummary");

    intrinsic_proto::data_logger::LogItem log;
    log.mutable_metadata()->set_event_source("skills.preview_summary");
    *log.mutable_payload()->mutable_skills_preview_summary() = summary;
    *log.mutable_context() = log_context;
    AddLogRequest(std::move(log)).IgnoreError();
  }

  if (!logged_status.ok()) {
    return logged_status;
  }

  return response;
}

absl::Status SkillServiceClient::StartPreview(
    absl::string_view world_id, intrinsic_proto::skills::Footprint footprint,
    const google::protobuf::Message& params,
    const intrinsic_proto::data_logger::Context& log_context) {
  INTR_ASSIGN_OR_RETURN(intrinsic_proto::skills::PreviewRequest request,
                        BuildPreviewRequestAndContextWithoutWorld(
                            instance_, params, footprint,
                            log_context));
  request.set_world_id(world_id);

  return StartPreview(request);
}

absl::Status SkillServiceClient::StartPreview(
    absl::string_view world_id, intrinsic_proto::skills::Footprint footprint,
    const google::protobuf::Any& params,
    const intrinsic_proto::data_logger::Context& log_context) {
  INTR_ASSIGN_OR_RETURN(intrinsic_proto::skills::PreviewRequest request,
                        BuildPreviewRequestAndContextWithoutWorld(
                            instance_, params, footprint,
                            log_context));
  request.set_world_id(world_id);

  return StartPreview(request);
}

absl::Status SkillServiceClient::StartPreview(
    const intrinsic_proto::skills::PreviewRequest& request) {
  {
    const stats::ScopedSpan span("SkillServiceClient/Preview/LogRequest");

    intrinsic_proto::data_logger::LogItem log;
    log.mutable_metadata()->set_event_source("skills.preview_request");
    *log.mutable_payload()->mutable_skills_preview_request() = request;
    *log.mutable_context() = request.context();
    AddLogRequest(std::move(log)).IgnoreError();
  }

  ::grpc::ClientContext preview_context;
  intrinsic::ConfigureClientContext(&preview_context);
  // Same timeout that predict/get footprint have.
  preview_context.set_deadline(
      absl::ToChronoTime(absl::Now() + skills::kClientDefaultTimeout));
  google::longrunning::Operation operation;
  INTR_RETURN_IF_ERROR(ToAbslStatus(
      executor_stub_->StartPreview(&preview_context, request, &operation)));

  {
    absl::MutexLock lock(&operation_name_mutex_);
    if (!operation_name_.empty()) {
      return absl::InternalError(
          "A SkillServiceClient instance can only be used for one skill "
          "execution operation.");
    }
    operation_name_ = operation.name();
  }

  return absl::OkStatus();
}

absl::StatusOr<intrinsic_proto::skills::PreviewResult>
SkillServiceClient::WaitForPreview(std::optional<absl::Duration> timeout) {
  const stats::ScopedSpan span("SkillServiceClient/WaitForPreview");

  if (operation_name_.empty()) {
    return absl::InternalError("Must call StartPreview before WaitForPreview.");
  }

  ::grpc::ClientContext wait_ctx;
  google::longrunning::WaitOperationRequest wait_req;
  google::longrunning::Operation operation;
  wait_req.set_name(operation_name_);
  if (timeout.has_value()) {
    INTR_ASSIGN_OR_RETURN(*wait_req.mutable_timeout(),
                          FromAbslDuration(timeout.value()));
  }
  INTR_RETURN_IF_ERROR(ToAbslStatus(
      executor_stub_->WaitOperation(&wait_ctx, wait_req, &operation)));

  if (operation.has_response()) {
    return UnpackAny<intrinsic_proto::skills::PreviewResult>(
        operation.response());
  }

  return ToAbslStatus(operation.error());
}

absl::Status SkillServiceClient::TryCancelPreview() {
  const stats::ScopedSpan span("SkillServiceClient/TryCancelPreview");

  ::grpc::ClientContext ctx;
  intrinsic::ConfigureClientContext(&ctx);
  google::longrunning::CancelOperationRequest cancel_req;
  {
    absl::MutexLock lock(&operation_name_mutex_);
    if (operation_name_.empty()) {
      return absl::InternalError(
          "Must call StartPreview before TryCancelPreview.");
    }
    cancel_req.set_name(operation_name_);
  }
  google::protobuf::Empty empty;
  return ToAbslStatus(
      executor_stub_->CancelOperation(&ctx, cancel_req, &empty));
}

absl::Status SkillServiceClient::ClearOperations() {
  const stats::ScopedSpan span("SkillServiceClient/ClearOperations");

  ::grpc::ClientContext ctx;
  intrinsic::ConfigureClientContext(&ctx);
  google::protobuf::Empty clear_req;
  google::protobuf::Empty empty;
  return ToAbslStatus(executor_stub_->ClearOperations(&ctx, clear_req, &empty));
}

}  // namespace skills
}  // namespace intrinsic
