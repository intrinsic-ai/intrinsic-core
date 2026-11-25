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

#include "intrinsic/executive/engine/clips_logger.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "absl/functional/bind_front.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "google/longrunning/operations.pb.h"
#include "grpcpp/client_context.h"
#include "grpcpp/support/status.h"
#include "intrinsic/executive/clips_cpp/function_facade.h"
#include "intrinsic/executive/clips_cpp/protobuf.h"
#include "intrinsic/executive/clips_cpp/trace_span_manager.h"
#include "intrinsic/executive/clips_cpp/value.h"
#include "intrinsic/executive/proto/behavior_tree.pb.h"
#include "intrinsic/executive/proto/log_items.pb.h"
#include "intrinsic/logging/data_logger_client.h"
#include "intrinsic/logging/log_item_builder.h"
#include "intrinsic/logging/proto/context.pb.h"
#include "intrinsic/logging/proto/flowstate_event.pb.h"
#include "intrinsic/logging/proto/log_item.pb.h"
#include "intrinsic/stats/scoped_span.h"
#include "intrinsic/stats/tracing_utils.h"
#include "intrinsic/util/grpc/grpc.h"
#include "intrinsic/util/status/return.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/thread/concurrent_queue.h"
#include "intrinsic/util/thread/stop_token.h"
#include "intrinsic/util/thread/thread.h"
#include "intrinsic/world/proto/object_world_service.grpc.pb.h"
#include "intrinsic/world/proto/object_world_service.pb.h"
#include "intrinsic/world/proto/object_world_updates.pb.h"
#include "intrinsic/world/service/world_compatibility_service.pb.h"
#include "opentelemetry/trace/span.h"
#include "opentelemetry/trace/span_context.h"
#include "opentelemetry/trace/span_startoptions.h"
#include "opentelemetry/trace/tracer.h"

namespace intrinsic {
namespace executive {

using ::intrinsic::data_logger::Builder;
using ::intrinsic::executive::clips::ProtoMessageId;
using ::intrinsic::executive::clips::TraceSpanReferenceId;
using ::intrinsic_proto::executive::LoggedOperation;
using ::intrinsic_proto::flowstate_event::FlowstateEvent;

constexpr char kLogLoggedOperationAsync[] = "log-logged-operation-async";
// Name of a CLIPS function does the same as `kLogLoggedOperationAsync` above
// except that it additionally logs the (same) operation state to a special
// history event source. Only called once at the beginning of the execution.
constexpr char kLogLoggedOperationWithHistoryAsync[] =
    "log-logged-operation-with-history-async";
constexpr char kLogFlowstateEventAsync[] = "log-flowstate-event-async";
constexpr char kLogGenUid[] = "log-gen-uid";
constexpr char kLogLoggedOperationAsyncSpanName[] =
    "Log LoggedOperation (async)";
constexpr char kLogLoggedOperationForHistoryAsyncSpanName[] =
    "Log LoggedOperation [for history] (async)";
constexpr char kLogFlowstateEventAsyncSpanName[] =
    "Log Flowstate Event (async)";
constexpr int kAsyncLogQueueSize = 200;

namespace {

opentelemetry::trace::SpanContext GetParentSpanContext(
    clips::TraceSpanManager* span_manager, int64_t parent_span_id) {
  opentelemetry::trace::SpanContext parent_span_context =
      opentelemetry::trace::SpanContext::GetInvalid();
  if (span_manager != nullptr &&
      TraceSpanReferenceId(parent_span_id) !=
          clips::TraceSpanManager::kInvalidTraceSpanReferenceId) {
    absl::StatusOr<opentelemetry::trace::SpanContext> retrieved_context =
        span_manager->GetSpanContext(TraceSpanReferenceId(parent_span_id));
    if (!retrieved_context.ok()) {
      LOG(ERROR) << "Failed to GetSpanContext " << retrieved_context.status();
    } else {
      parent_span_context = *retrieved_context;
    }
  }
  return parent_span_context;
}

}  // end namespace

std::unique_ptr<ClipsLogger::LogRequest> ClipsLogger::LogRequest::Create(
    const intrinsic_proto::executive::LoggedOperation& logged_operation,
    const intrinsic_proto::data_logger::Context& context,
    int64_t pregenerated_log_id,
    const opentelemetry::trace::SpanContext& parent_span_context,
    std::string log_world_id, std::string_view span_name, bool is_for_history) {
  return std::make_unique<LogRequest>(
      Builder::From(logged_operation, is_for_history)
          .WithUid(pregenerated_log_id)
          .WithContext(context)
          .Item(),
      parent_span_context, log_world_id, span_name);
}

std::unique_ptr<ClipsLogger::LogRequest> ClipsLogger::LogRequest::Create(
    const intrinsic_proto::flowstate_event::FlowstateEvent& event,
    const intrinsic_proto::data_logger::Context& context,
    int64_t pregenerated_log_id,
    const opentelemetry::trace::SpanContext& parent_span_context,
    std::string_view span_name) {
  return std::make_unique<LogRequest>(
          // clang-format off
          Builder::PackAnyFrom(event)
          // clang-format on
          .WithUid(pregenerated_log_id)
          .WithContext(context)
          .Item(),
      parent_span_context, "", span_name);
}

ClipsLogger::ClipsLogger(
    clips::ProtobufManager* proto_manager,
    intrinsic_proto::world::ObjectWorldService::StubInterface* world_stub,
    clips::TraceSpanManager* span_manager)
    : proto_manager_(proto_manager),
      span_manager_(span_manager),
      world_stub_(world_stub) {}

ClipsLogger::~ClipsLogger() { TearDown().IgnoreError(); }

absl::Status ClipsLogger::InitAsyncLogging() {
  log_req_channel_.emplace(kAsyncLogQueueSize);

  // Setup reader thread that performs actual logging from queue
  worker_ = Thread(absl::bind_front(&ClipsLogger::LogQueueReader, this),
                   std::ref(log_req_channel_.value()));
  return absl::OkStatus();
}

// Must be called after construction.
// Registers logging functions with the CLIPS environment.
absl::Status ClipsLogger::Init(clips::EnvironmentFunctionFacade* facade) {
  INTR_RETURN_IF_ERROR(InitAsyncLogging());

  // The CLIPS functions that use this cannot themselves accept the additional
  // argument for logging to history since 9 is the maximum number of args.
  auto log_logged_operation =
      [this](int64_t lo_proto_id, int64_t context_proto_id,
             int64_t pregenerated_log_id, int64_t parent_span_id,
             const std::string& log_world_id,
             bool log_to_history) -> clips::Symbol {
    // Get logged operation from ProtobufManager
    std::unique_ptr<intrinsic_proto::executive::LoggedOperation> pm_state_proto;
    INTR_ASSIGN_OR_RETURN(pm_state_proto,
                          proto_manager_->GetProtoAs<LoggedOperation>(
                              ProtoMessageId(lo_proto_id)),
                          _.LogError().With(Return(clips::Symbol::False())));

    // Get context from ProtobufManager
    std::unique_ptr<intrinsic_proto::data_logger::Context> pm_context_proto;
    INTR_ASSIGN_OR_RETURN(
        pm_context_proto,
        proto_manager_->GetProtoAs<intrinsic_proto::data_logger::Context>(
            ProtoMessageId(context_proto_id)),
        _.LogError().With(Return(clips::Symbol::False())));

    opentelemetry::trace::SpanContext parent_span_context =
        GetParentSpanContext(span_manager_, parent_span_id);

    std::unique_ptr<LogRequest> request = LogRequest::Create(
        *pm_state_proto, *pm_context_proto, pregenerated_log_id,
        parent_span_context, log_world_id, kLogLoggedOperationAsyncSpanName,
        /*is_for_history=*/false);

    std::unique_ptr<LogRequest> history_request;
    if (log_to_history) {
      // Omit the world ID on this request since it's not pertinent and we don't
      // want to create a second clone. The world ID in a log item will be
      // deleted after logging, so passing the same one twice is not sound.
      history_request = LogRequest::Create(
          *pm_state_proto, *pm_context_proto, pregenerated_log_id,
          parent_span_context, /*log_world_id=*/"",
          kLogLoggedOperationForHistoryAsyncSpanName, /*is_for_history=*/true);
    }

    // Logging history before non-history to ensure the timestamp of the history
    // log item will be before any operations logged during execution.
    if (history_request) {
      INTR_RETURN_IF_ERROR(AddLogRequest(std::move(history_request)))
          .LogError()
          .With(Return(clips::Symbol::False()));
    }

    INTR_RETURN_IF_ERROR(AddLogRequest(std::move(request)))
        .LogError()
        .With(Return(clips::Symbol::False()));

    return clips::Symbol::True();
  };

  INTR_RETURN_IF_ERROR(facade->AddFunction(
      kLogLoggedOperationAsync,
      std::function([log_logged_operation](
                        int64_t lo_proto_id, int64_t context_proto_id,
                        int64_t pregenerated_log_id, int64_t parent_span_id,
                        const std::string& log_world_id) -> clips::Symbol {
        return log_logged_operation(lo_proto_id, context_proto_id,
                                    pregenerated_log_id, parent_span_id,
                                    log_world_id, false);
      })));

  INTR_RETURN_IF_ERROR(facade->AddFunction(
      kLogLoggedOperationWithHistoryAsync,
      std::function([log_logged_operation](
                        int64_t lo_proto_id, int64_t context_proto_id,
                        int64_t pregenerated_log_id, int64_t parent_span_id,
                        const std::string& log_world_id) -> clips::Symbol {
        return log_logged_operation(lo_proto_id, context_proto_id,
                                    pregenerated_log_id, parent_span_id,
                                    log_world_id, true);
      })));

  INTR_RETURN_IF_ERROR(facade->AddFunction(
      kLogFlowstateEventAsync,
      std::function([this](int64_t fe_proto_id, int64_t context_proto_id,
                           int64_t pregenerated_log_id,
                           int64_t parent_span_id) -> clips::Symbol {
        // Get state from ProtobufManager
        std::unique_ptr<intrinsic_proto::flowstate_event::FlowstateEvent>
            pm_event_proto;
        INTR_ASSIGN_OR_RETURN(
            pm_event_proto,
            proto_manager_->GetProtoAs<FlowstateEvent>(
                ProtoMessageId(fe_proto_id)),
            _.LogError().With(Return(clips::Symbol::False())));

        // Get context from ProtobufManager
        std::unique_ptr<intrinsic_proto::data_logger::Context> pm_context_proto;
        INTR_ASSIGN_OR_RETURN(
            pm_context_proto,
            proto_manager_->GetProtoAs<intrinsic_proto::data_logger::Context>(
                ProtoMessageId(context_proto_id)),
            _.LogError().With(Return(clips::Symbol::False())));

        opentelemetry::trace::SpanContext parent_span_context =
            GetParentSpanContext(span_manager_, parent_span_id);

        std::unique_ptr<ClipsLogger::LogRequest> request = LogRequest::Create(
            *pm_event_proto, *pm_context_proto, pregenerated_log_id,
            parent_span_context, kLogFlowstateEventAsyncSpanName);

        INTR_RETURN_IF_ERROR(AddLogRequest(std::move(request)))
            .LogError()
            .With(Return(clips::Symbol::False()));
        return clips::Symbol::True();
      })));

  INTR_RETURN_IF_ERROR(facade->AddFunction(
      kLogGenUid, std::function([]() -> int64_t {
        // Need to cast because CLIPS only
        // handles signed numbers. Must be cast
        // back to uint64 when leaving CLIPS
        // again.
        return static_cast<int64_t>(data_logger::GenerateUid());
      })));

  return absl::OkStatus();
}

absl::Status ClipsLogger::TearDown() {
  // Closing the queue causes the worker thread to stop.
  if (log_req_channel_.has_value()) {
    log_req_channel_->Close();
  }
  if (worker_.joinable()) {
    worker_.request_stop();
    worker_.join();
  }
  return absl::OkStatus();
}

void ClipsLogger::DeleteWorld(std::string_view world_id) {
  if (!world_stub_) return;

  grpc::ClientContext delete_context;
  intrinsic::ConfigureClientContext(&delete_context);
  intrinsic_proto::world::DeleteWorldRequest delete_request;
  delete_request.set_world_id(world_id);
  google::protobuf::Empty delete_response;
  if (!world_stub_
           ->DeleteWorld(&delete_context, delete_request, &delete_response)
           .ok()) {
    LOG(WARNING) << "Failed to delete temporary world " << world_id;
  }
}

absl::Status ClipsLogger::AddLogRequest(
    std::unique_ptr<ClipsLogger::LogRequest> request) {
  if (!log_req_channel_.has_value()) {
    DeleteWorld(request->log_world_id);
    return absl::FailedPreconditionError(
        "Asynchronous logging not enabled, dropping request");
  }

  // Copy the world ID before moving the request.
  const std::string world_id = request->log_world_id;
  if (absl::Status status =
          log_req_channel_->Enqueue(std::move(*request), absl::ZeroDuration());
      !status.ok()) {
    // The queue is full and we drop the log request.
    DeleteWorld(world_id);
    return status;
  }

  return absl::OkStatus();
}

void ClipsLogger::LogQueueReader(StopToken stop_token,
                                 ConcurrentQueue<LogRequest>& queue) {
  while (true) {
    absl::StatusOr<LogRequest> request = queue.Dequeue(absl::Milliseconds(500));
    if (absl::IsUnavailable(request.status())) {
      // Queue is empty and closed, stop this thread.
      return;
    }
    if (!request.ok()) {
      // Queue is empty, wait and try again.
      continue;
    }

    std::optional<stats::ScopedSpan> scoped_span;
    if (request->parent_span_context.IsValid()) {
      scoped_span.emplace(stats::GetTracer()->StartSpan(
          request->span_name, {.parent = request->parent_span_context}));
    }

    // Get world proto from world service (gRPC)
    if (world_stub_ != nullptr && !request->log_world_id.empty() &&
        request->log_item.payload().has_executive_operation()) {
      grpc::ClientContext world_context;
      intrinsic::ConfigureClientContext(&world_context);

      intrinsic_proto::world::ListObjectsRequest list_request;
      list_request.set_world_id(request->log_world_id);
      // GetWorldWithEntities always returned objects in the FULL view. Keep
      // that level of detail (object and frame poses) for log consumers.
      list_request.set_view(intrinsic_proto::world::ObjectView::FULL);
      intrinsic_proto::world::ListObjectsResponse list_response;
      grpc::Status list_status = world_stub_->ListObjects(
          &world_context, list_request, &list_response);
      if (!list_status.ok()) {
        LOG(WARNING) << "Failed to list objects in world "
                     << request->log_world_id << ": "
                     << list_status.error_message();
      } else {
        intrinsic_proto::world::WorldWithEntities world_with_entities;
        *world_with_entities.mutable_objects() =
            std::move(*list_response.mutable_objects());
        data_logger::LogAsync(
                // clang-format off
                Builder::PackAnyFrom(world_with_entities)
                // clang-format on
                .WithEventSource("executive.world_state")
                .WithContext(request->log_item.context())
                .Item(),
            [](absl::Status status) {
              if (!status.ok()) {
                LOG(WARNING) << "Failed to log WorldWithEntities; " << status;
              }
            });
      }
    }

    if (!request->log_world_id.empty()) {
      DeleteWorld(request->log_world_id);
    }

    // Send (gRPC) log entry
    if (scoped_span) {
      scoped_span->AddAttribute(
          "size_bytes", static_cast<int64_t>(request->log_item.ByteSizeLong()));
    }
    // The code below is a performance optimization to ensure faster shutdown.
    if (stop_token.stop_requested()) {
      data_logger::LogAsync(
          std::move(request->log_item), [](absl::Status status) {
            if (!status.ok()) {
              LOG(WARNING) << "Failed to log LogItem; " << status;
            }
          });
    } else {
      absl::Status status =
          data_logger::LogAndAwaitResponse(std::move(request->log_item));
      if (!status.ok()) {
        LOG(WARNING) << "Failed to log LogItem; " << status;
      }
    }
  }
}

}  // namespace executive
}  // namespace intrinsic
