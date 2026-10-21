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

#ifndef INTRINSIC_EXECUTIVE_ENGINE_CLIPS_LOGGER_H_
#define INTRINSIC_EXECUTIVE_ENGINE_CLIPS_LOGGER_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "intrinsic/executive/clips_cpp/function_facade.h"
#include "intrinsic/executive/clips_cpp/protobuf.h"
#include "intrinsic/executive/clips_cpp/trace_span_manager.h"
#include "intrinsic/executive/proto/log_items.pb.h"
#include "intrinsic/logging/proto/flowstate_event.pb.h"
#include "intrinsic/logging/proto/log_item.pb.h"
#include "intrinsic/util/thread/concurrent_queue.h"
#include "intrinsic/util/thread/stop_token.h"
#include "intrinsic/util/thread/thread.h"
#include "intrinsic/world/proto/object_world_service.grpc.pb.h"
#include "opentelemetry/trace/span_context.h"

namespace intrinsic {
namespace executive {

// Logs executive information to data logger.
// Provides a function to the CLIPS environment for logging.
class ClipsLogger {
 public:
  explicit ClipsLogger(
      clips::ProtobufManager* proto_manager,
      intrinsic_proto::world::ObjectWorldService::StubInterface* world_stub =
          nullptr,
      clips::TraceSpanManager* span_manager = nullptr);
  ~ClipsLogger();

  // Must be called after construction.
  // Registers the function "log-executive-state" with the CLIPS environment.
  absl::Status Init(clips::EnvironmentFunctionFacade* facade)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(facade->clips_mutex());

  absl::Status TearDown();

 private:
  struct LogRequest {
    static std::unique_ptr<LogRequest> Create(
        const intrinsic_proto::executive::LoggedOperation& logged_operation,
        const intrinsic_proto::data_logger::Context& context,
        int64_t pregenerated_log_id,
        const opentelemetry::trace::SpanContext& parent_span_context,
        std::string log_world_id, std::string_view span_name,
        bool is_for_history);
    static std::unique_ptr<LogRequest> Create(
        const intrinsic_proto::flowstate_event::FlowstateEvent& event,
        const intrinsic_proto::data_logger::Context& context,
        int64_t pregenerated_log_id,
        const opentelemetry::trace::SpanContext& parent_span_context,
        std::string_view span_name);

    LogRequest(intrinsic_proto::data_logger::LogItem&& log_item,
               const opentelemetry::trace::SpanContext& parent_span_context,
               std::string log_world_id, std::string_view span_name)
        : log_item(std::move(log_item)),
          parent_span_context(parent_span_context),
          log_world_id(log_world_id),
          span_name(span_name) {}

    intrinsic_proto::data_logger::LogItem log_item;

    opentelemetry::trace::SpanContext parent_span_context;
    std::string log_world_id;
    std::string_view span_name;
  };

  absl::Status InitAsyncLogging();
  absl::Status AddLogRequest(std::unique_ptr<LogRequest> request);
  void LogQueueReader(StopToken stop_token, ConcurrentQueue<LogRequest>& queue);

  void DeleteWorld(std::string_view world_id);

  clips::ProtobufManager* proto_manager_;  // externally owned
  clips::TraceSpanManager* span_manager_;  // externally owned, can be nullptr
  intrinsic_proto::world::ObjectWorldService::StubInterface*
      world_stub_;  // externally owned, can be nullptr

  Thread worker_;
  std::optional<ConcurrentQueue<LogRequest>> log_req_channel_;
};

}  // namespace executive
}  // namespace intrinsic

#endif  // INTRINSIC_EXECUTIVE_ENGINE_CLIPS_LOGGER_H_
