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

#include "intrinsic/executive/clips_cpp/trace_span_manager.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/log/die_if_null.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/random/distributions.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "intrinsic/executive/clips_cpp/environment.h"
#include "intrinsic/executive/clips_cpp/value.h"
#include "intrinsic/stats/tracing_utils.h"
#include "intrinsic/util/status/return.h"
#include "intrinsic/util/status/status_macros.h"
#include "opentelemetry/trace/span.h"
#include "opentelemetry/trace/span_context.h"
#include "opentelemetry/trace/span_id.h"
#include "opentelemetry/trace/span_metadata.h"
#include "opentelemetry/trace/span_startoptions.h"
#include "opentelemetry/trace/trace_flags.h"
#include "opentelemetry/trace/trace_id.h"
#include "opentelemetry/trace/tracer.h"

namespace intrinsic {
namespace executive {
namespace clips {
namespace {

constexpr char kSpanStart[] = "span-start";
constexpr char kSpanStartRootSpan[] = "span-start-root-span";
constexpr char kSpanEnd[] = "span-end";
constexpr char kSpanEndFailure[] = "span-end-failure";
constexpr char kSpanAddAttribute[] = "span-add-attribute";
constexpr char kSpanAddEventAnnotation[] = "span-add-event-annotation";
constexpr char kSpanAddChildLink[] = "span-add-child-link";
constexpr char kSpanGetTraceId[] = "span-get-trace-id";
constexpr char kSpanGetSpanId[] = "span-get-span-id";
constexpr char kSpanGetTraceUrl[] = "span-get-trace-url";
constexpr char kSpanGetStartTime[] = "span-get-start-time";
constexpr char kSpanCalculateExecutionDuration[] =
    "span-calculate-execution-duration";

}  // namespace

TraceSpanManager::TraceSpanManager(Environment* env,
                                   util::Clock* absl_nonnull clock)
    : env_(env), clock_(*ABSL_DIE_IF_NULL(clock)) {}

TraceSpanManager::~TraceSpanManager() {
  if (env_ != nullptr) {
    absl::MutexLock lock(env_->mutex());
    UnregisterFunctions();
  }

  // If there are still spans_ running, we will end them now. Otherwise they
  // will never be recorded.
  // However, this is an error that should not be happening. Before destroying
  // the SpanManager, execution should have been stopped and all spans should
  // have been ended by some means.
  if (!spans_.empty()) {
    LOG(ERROR) << "Destroying SpanManager, but spans_ is not empty. All "
                  "contained spans will be force ended now.";
  }
  for (const auto& [ref_id, span_info] : spans_) {
    LOG(ERROR) << "Ending span " << ref_id << " with span id "
               << stats::GetSpanIdHex(span_info.span->GetContext());
    span_info.span->AddEvent("Force ended by destroying SpanManager");
    span_info.span->SetStatus(opentelemetry::trace::StatusCode::kError,
                              "Force ended by destroying SpanManager");
    span_info.span->End();
  }
}

absl::StatusOr<std::unique_ptr<TraceSpanManager>> TraceSpanManager::Create(
    Environment* environment, util::Clock* absl_nonnull clock)
    ABSL_LOCKS_EXCLUDED(environment->mutex()) {
  auto span_manager =
      absl::WrapUnique(new TraceSpanManager(environment, clock));
  absl::MutexLock lock(span_manager->env_->mutex());
  INTR_RETURN_IF_ERROR(span_manager->RegisterFunctions());
  return span_manager;
}

absl::Status TraceSpanManager::RegisterFunctions() {
  INTR_RETURN_IF_ERROR(RegisterFunction(
      kSpanStart,
      std::function([this](const std::string& span_name,
                           int64_t parent_span_reference_id,
                           const std::string& span_type) -> int64_t {
        INTR_ASSIGN_OR_RETURN(
            TraceSpanReferenceId span_ref_id,
            StartSpan(span_name, span_type,
                      TraceSpanReferenceId(parent_span_reference_id)),
            _.LogError().With(Return(kInvalidTraceSpanReferenceId.value())));
        return span_ref_id.value();
      })));
  INTR_RETURN_IF_ERROR(RegisterFunction(
      kSpanStartRootSpan,
      std::function([this](const std::string& span_name,
                           const std::string& span_type) -> int64_t {
        INTR_ASSIGN_OR_RETURN(
            TraceSpanReferenceId span_ref_id,
            StartRootSpan(span_name, span_type),
            _.LogError().With(Return(kInvalidTraceSpanReferenceId.value())));
        return span_ref_id.value();
      })));
  INTR_RETURN_IF_ERROR(RegisterFunction(
      kSpanEnd, std::function([this](int64_t id) {
        INTR_RETURN_IF_ERROR(EndSpan(TraceSpanReferenceId(id)))
            .LogError()
            .With(intrinsic::ExtraMessage()
                  << " while calling " << kSpanEnd << " for " << id)
            .With(ReturnVoid());
      })));
  INTR_RETURN_IF_ERROR(RegisterFunction(
      kSpanEndFailure,
      std::function([this](int64_t id, int status_code,
                           const std::string& failure_message) {
        TraceSpanReferenceId ref_id(id);
        if (status_code != 0) {
          absl::Status st = AddSpanAttribute(ref_id, "error.code", status_code);
          if (!st.ok()) {
            LOG(ERROR) << "Failed to add error.code attribute while calling "
                       << kSpanEndFailure << " for " << id << ": " << st;
          }
        }
        opentelemetry::trace::StatusCode otel_status =
            status_code == 0 ? opentelemetry::trace::StatusCode::kOk
                             : opentelemetry::trace::StatusCode::kError;
        INTR_RETURN_IF_ERROR(EndSpan(ref_id, otel_status, failure_message))
            .LogError()
            .With(intrinsic::ExtraMessage()
                  << " while calling " << kSpanEndFailure << " for " << id)
            .With(ReturnVoid());
      })));
  INTR_RETURN_IF_ERROR(RegisterFunction(
      kSpanAddAttribute,
      std::function([this](int64_t id, const std::string& key,
                           clips::Value value) {
        opentelemetry::common::AttributeValue attr_value;
        // we need this temporary storage for strings as
        // opentelemetry::common::AttributeValue is a variant that stores
        // strings as non-owning views (std::string_view or const char*).
        // Without this temporary storage, the underlying string would go out of
        // scope and be destroyed before the call to AddSpanAttribute.
        std::string string_value;
        switch (value.GetValueType()) {
          case Value::Type::kString:
            string_value = *value.GetString();
            attr_value = string_value;
            break;
          case Value::Type::kSymbol:
            string_value = *value.GetSymbolAsString();
            attr_value = string_value;
            break;
          case Value::Type::kInteger:
            attr_value = *value.GetInteger();
            break;
          default:
            string_value = value.ToString();
            attr_value = string_value;
            break;
        }

        INTR_RETURN_IF_ERROR(
            AddSpanAttribute(TraceSpanReferenceId(id), key, attr_value))
            .LogError()
            .With(intrinsic::ExtraMessage()
                  << " while calling " << kSpanAddAttribute << " for " << id)
            .With(ReturnVoid());
      })));
  INTR_RETURN_IF_ERROR(RegisterFunction(
      kSpanAddEventAnnotation,
      std::function([this](int64_t id, const std::string& annotation) {
        INTR_RETURN_IF_ERROR(
            AddSpanEventAnnotation(TraceSpanReferenceId(id), annotation))
            .LogError()
            .With(intrinsic::ExtraMessage()
                  << " while calling " << kSpanAddEventAnnotation << " for "
                  << id)
            .With(ReturnVoid());
      })));
  INTR_RETURN_IF_ERROR(RegisterFunction(
      kSpanAddChildLink,
      std::function([this](int64_t id, const std::string& trace_id_hex,
                           const std::string& span_id_hex) {
        INTR_RETURN_IF_ERROR(
            AddChildLink(TraceSpanReferenceId(id), trace_id_hex, span_id_hex))
            .LogError()
            .With(intrinsic::ExtraMessage()
                  << " while calling " << kSpanAddChildLink << " for " << id)
            .With(ReturnVoid());
      })));
  INTR_RETURN_IF_ERROR(RegisterFunction(
      kSpanGetTraceId, std::function([this](int64_t id) -> std::string {
        INTR_ASSIGN_OR_RETURN(
            std::string trace_id_hex, GetTraceIdHex(TraceSpanReferenceId(id)),
            _.LogError()
                .With(intrinsic::ExtraMessage()
                      << " while calling " << kSpanGetTraceId << " for " << id)
                .With(Return("")));
        return trace_id_hex;
      })));
  INTR_RETURN_IF_ERROR(RegisterFunction(
      kSpanGetSpanId, std::function([this](int64_t id) -> std::string {
        INTR_ASSIGN_OR_RETURN(
            std::string span_id_hex, GetSpanIdHex(TraceSpanReferenceId(id)),
            _.LogError()
                .With(intrinsic::ExtraMessage()
                      << " while calling " << kSpanGetSpanId << " for " << id)
                .With(Return("")));
        return span_id_hex;
      })));
  INTR_RETURN_IF_ERROR(RegisterFunction(
      kSpanGetTraceUrl,
      std::function([this](int64_t id, const std::string& google_cloud_project)
                        -> std::string {
        INTR_ASSIGN_OR_RETURN(
            std::string trace_url,
            GetGoogleCloudTracingLink(TraceSpanReferenceId(id),
                                      google_cloud_project),
            _.LogError()
                .With(intrinsic::ExtraMessage()
                      << " while calling " << kSpanGetTraceUrl << " for " << id)
                .With(Return("")));
        return trace_url;
      })));
  INTR_RETURN_IF_ERROR(RegisterFunction(
      kSpanCalculateExecutionDuration,
      std::function([this](int64_t id, const std::string& span_name,
                           const std::string& start_event) -> double {
        INTR_ASSIGN_OR_RETURN(
            absl::Duration duration,
            CalculateExecutionDuration(TraceSpanReferenceId(id), start_event),
            _.LogError()
                .With(intrinsic::ExtraMessage()
                      << " while calling " << kSpanCalculateExecutionDuration
                      << " for " << id)
                .With(Return(-1.0)));
        return absl::ToDoubleSeconds(duration);
      })));
  INTR_RETURN_IF_ERROR(RegisterFunction(
      kSpanGetStartTime,
      std::function([this](int64_t id,
                           const std::string& span_name) -> clips::Values {
        const clips::Values kInvalidTime = {clips::Symbol::False()};
        INTR_ASSIGN_OR_RETURN(absl::Time start_time,
                              GetSpanStartTime(TraceSpanReferenceId(id)),
                              _.LogError()
                                  .With(intrinsic::ExtraMessage()
                                        << " while calling "
                                        << kSpanGetStartTime << " for " << id)
                                  .With(Return(kInvalidTime)));
        absl::Duration d = start_time - absl::UnixEpoch();
        const int64_t s = absl::IDivDuration(d, absl::Seconds(1), &d);
        const int64_t n = absl::IDivDuration(d, absl::Nanoseconds(1), &d);
        return {clips::Value(s), clips::Value(n)};
      })));

  return absl::OkStatus();
}

void TraceSpanManager::UnregisterFunctions() {
  for (const std::string& f : functions_) {
    if (absl::Status s(env_->RemoveFunction(f)); !s.ok()) {
      LOG(ERROR) << "Failed to remove " << f << ": " << s;
    }
  }
  functions_.clear();
}

TraceSpanReferenceId TraceSpanManager::GenerateNextSpanReferenceId() {
  TraceSpanReferenceId new_id = kInvalidTraceSpanReferenceId;
  do {
    // shift to fit into int64
    new_id = TraceSpanReferenceId(
        absl::Uniform<uint64_t>(id_random_generator_) >> 1);
  } while (new_id == kInvalidTraceSpanReferenceId || spans_.contains(new_id));
  return new_id;
}

absl::StatusOr<TraceSpanReferenceId> TraceSpanManager::AddSpan(
    std::shared_ptr<opentelemetry::trace::Span> span) {
  const TraceSpanReferenceId next_span_reference_id =
      GenerateNextSpanReferenceId();
  spans_[next_span_reference_id] = SpanInfo{
      .span = std::move(span),
      .start_time = clock_.TimeNow(),
  };
  return next_span_reference_id;
}

absl::StatusOr<TraceSpanReferenceId> TraceSpanManager::StartRootSpan(
    absl::string_view name, absl::string_view span_type) {
  std::shared_ptr<opentelemetry::trace::Span> span =
      stats::StartSampledRootSpan(name);
  span->SetAttribute("bt_span_type", span_type);

  const TraceSpanReferenceId next_span_reference_id =
      GenerateNextSpanReferenceId();
  spans_[next_span_reference_id] = SpanInfo{
      .span = std::move(span),
      .start_time = clock_.TimeNow(),
  };
  return next_span_reference_id;
}

absl::StatusOr<TraceSpanReferenceId> TraceSpanManager::StartSpan(
    absl::string_view name, absl::string_view span_type,
    TraceSpanReferenceId parent_reference_id) {
  INTR_ASSIGN_OR_RETURN(
      std::shared_ptr<opentelemetry::trace::Span> parent_span,
      GetSpan(parent_reference_id),
      _ << " while retrieving parent span with id " << parent_reference_id);

  std::shared_ptr<opentelemetry::trace::Span> span =
      stats::GetTracer()->StartSpan(name,
                                    {.parent = parent_span->GetContext()});
  span->SetAttribute("bt_span_type", span_type);

  const TraceSpanReferenceId next_span_reference_id =
      GenerateNextSpanReferenceId();
  spans_[next_span_reference_id] = SpanInfo{
      .span = std::move(span),
      .start_time = clock_.TimeNow(),
  };
  return next_span_reference_id;
}

absl::Status TraceSpanManager::EndSpan(
    TraceSpanReferenceId id, opentelemetry::trace::StatusCode completion_status,
    absl::string_view status_message) {
  if (id == kInvalidTraceSpanReferenceId) {
    return absl::OkStatus();
  }
  INTR_ASSIGN_OR_RETURN(std::shared_ptr<opentelemetry::trace::Span> span,
                        GetSpan(id));
  span->SetStatus(completion_status, status_message);
  span->End();
  spans_.erase(id);
  return absl::OkStatus();
}

absl::Status TraceSpanManager::AddSpanAttribute(
    TraceSpanReferenceId id, absl::string_view key,
    const opentelemetry::common::AttributeValue& value) {
  INTR_ASSIGN_OR_RETURN(std::shared_ptr<opentelemetry::trace::Span> span,
                        GetSpan(id));
  span->SetAttribute(key, value);
  return absl::OkStatus();
}

absl::Status TraceSpanManager::AddSpanEventAnnotation(
    TraceSpanReferenceId id, absl::string_view annotation) {
  auto it = spans_.find(id);
  if (it == spans_.end()) {
    return absl::NotFoundError(absl::StrFormat("span %i unknown", id.value()));
  }

  it->second.span->AddEvent(annotation);
  it->second.annotations.push_back({clock_.TimeNow(), std::string(annotation)});
  return absl::OkStatus();
}

absl::Status TraceSpanManager::AddChildLink(TraceSpanReferenceId id,
                                            absl::string_view trace_id_hex,
                                            absl::string_view span_id_hex) {
  // TODO (b/525330816): implement links once available

  return absl::OkStatus();
}

absl::StatusOr<std::string> TraceSpanManager::ToString(
    TraceSpanReferenceId id) const {
  INTR_ASSIGN_OR_RETURN(std::shared_ptr<opentelemetry::trace::Span> span,
                        GetSpan(id));
  opentelemetry::trace::SpanContext ctx = span->GetContext();
  return absl::StrFormat("%s-%s-%02x", stats::GetTraceIdHex(ctx),
                         stats::GetSpanIdHex(ctx), ctx.trace_flags().flags());
}

absl::StatusOr<opentelemetry::trace::SpanContext>
TraceSpanManager::GetSpanContext(TraceSpanReferenceId id) const {
  INTR_ASSIGN_OR_RETURN(std::shared_ptr<opentelemetry::trace::Span> span,
                        GetSpan(id));
  return span->GetContext();
}

absl::StatusOr<std::string> TraceSpanManager::GetTraceIdHex(
    TraceSpanReferenceId id) const {
  INTR_ASSIGN_OR_RETURN(std::shared_ptr<opentelemetry::trace::Span> span,
                        GetSpan(id));
  return stats::GetTraceIdHex(span->GetContext());
}

absl::StatusOr<std::string> TraceSpanManager::GetSpanIdHex(
    TraceSpanReferenceId id) const {
  INTR_ASSIGN_OR_RETURN(std::shared_ptr<opentelemetry::trace::Span> span,
                        GetSpan(id));
  return stats::GetSpanIdHex(span->GetContext());
}

absl::StatusOr<opentelemetry::trace::SpanContext>
TraceSpanManager::GetContextFromHexIds(absl::string_view trace_id_hex,
                                       absl::string_view span_id_hex) {
  std::string trace_id_bytes = absl::HexStringToBytes(trace_id_hex);
  if (trace_id_bytes.length() != opentelemetry::trace::TraceId::kSize) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "GetContextFromHexIds: %s is not a valid trace id.", trace_id_hex));
  }
  opentelemetry::trace::TraceId trace_id(
      std::span<const uint8_t, opentelemetry::trace::TraceId::kSize>(
          reinterpret_cast<const uint8_t*>(trace_id_bytes.data()),
          opentelemetry::trace::TraceId::kSize));

  std::string span_id_bytes = absl::HexStringToBytes(span_id_hex);
  if (span_id_bytes.length() != opentelemetry::trace::SpanId::kSize) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "GetContextFromHexIds: %s is not a valid span id.", span_id_hex));
  }
  opentelemetry::trace::SpanId span_id(
      std::span<const uint8_t, opentelemetry::trace::SpanId::kSize>(
          reinterpret_cast<const uint8_t*>(span_id_bytes.data()),
          opentelemetry::trace::SpanId::kSize));
  opentelemetry::trace::SpanContext context(
      trace_id, span_id, opentelemetry::trace::TraceFlags(), false);
  return context;
}

std::string TraceSpanManager::GetGoogleCloudTracingLinkFromTraceId(
    absl::string_view trace_id_hex, absl::string_view cloud_project) const {
  return absl::StrFormat(
      "https://console.cloud.google.com/traces/explorer;traceId=%s?project=%s",
      trace_id_hex, cloud_project);
}

absl::StatusOr<std::string> TraceSpanManager::GetGoogleCloudTracingLink(
    TraceSpanReferenceId id, absl::string_view cloud_project) const {
  INTR_ASSIGN_OR_RETURN(std::string trace_id_hex, GetTraceIdHex(id));
  return GetGoogleCloudTracingLinkFromTraceId(trace_id_hex, cloud_project);
}

absl::StatusOr<const TraceSpanManager::SpanInfo>
TraceSpanManager::GetRunningSpan(clips::TraceSpanReferenceId span_id) const {
  auto it = spans_.find(span_id);
  if (it == spans_.end()) {
    return absl::NotFoundError(
        absl::StrFormat("span %i unknown", span_id.value()));
  }
  return it->second;
}

absl::StatusOr<absl::Time> TraceSpanManager::GetSpanStartTime(
    TraceSpanReferenceId id) const {
  if (id == clips::TraceSpanManager::kInvalidTraceSpanReferenceId) {
    return absl::NotFoundError("Cannot calculate duration for invalid span.");
  }
  INTR_ASSIGN_OR_RETURN(const SpanInfo running_span, GetRunningSpan(id));
  return running_span.start_time;
}

absl::StatusOr<absl::Duration> TraceSpanManager::CalculateExecutionDuration(
    TraceSpanReferenceId id, absl::string_view start_event) const {
  if (id == clips::TraceSpanManager::kInvalidTraceSpanReferenceId) {
    return absl::NotFoundError("Cannot calculate duration for invalid span.");
  }
  INTR_ASSIGN_OR_RETURN(const SpanInfo running_span, GetRunningSpan(id));

  absl::Duration suspended_duration = absl::ZeroDuration();
  std::optional<absl::Time> suspend_start;
  bool found_start_event = false;
  for (const auto& [timestamp, description] : running_span.annotations) {
    if (!start_event.empty() && description == start_event) {
      found_start_event = true;
      // The span explicitly states when a tree associated with it started. Thus
      // remove the time before that for tracking the actual execution duration.
      if (suspend_start.has_value()) {
        // Only count from start of span to suspend_start. The part until this
        // event will be tracked in the suspend interval.
        suspended_duration += *suspend_start - running_span.start_time;
      } else {
        suspended_duration += timestamp - running_span.start_time;
      }
    }
    if (description == "SUSPENDED") {
      if (suspend_start.has_value()) {
        LOG(WARNING) << "During execution time calculation: Found consecutive "
                        "'Suspended' events without a 'Resume' event.";
      } else {
        suspend_start = timestamp;
      }
    }
    if (description == "RESUME") {
      if (!suspend_start.has_value()) {
        LOG(WARNING) << "During execution time calculation: Found 'Resume' "
                        "event, but no matching 'Suspended' event.";
        continue;
      }
      suspended_duration += timestamp - *suspend_start;
      suspend_start.reset();
    }
  }
  if (!start_event.empty() && !found_start_event) {
    // start event expected, but hasn't happened, yet.
    return absl::ZeroDuration();
  }
  absl::Time now = clock_.TimeNow();
  // capture the operation being suspended right now
  if (suspend_start.has_value()) {
    suspended_duration += now - *suspend_start;
  }
  return (now - running_span.start_time) - suspended_duration;
}

void TraceSpanManager::DebugPrintAllSpans(std::ostream* stream) const {
  for (const auto& [ref_id, span_info] : spans_) {
    opentelemetry::trace::SpanContext ctx = span_info.span->GetContext();
    *stream << "span " << ref_id << " is "
            << absl::StrFormat("%s-%s-%02x", stats::GetTraceIdHex(ctx),
                               stats::GetSpanIdHex(ctx),
                               ctx.trace_flags().flags())
            << std::endl;
  }
}

absl::StatusOr<std::shared_ptr<opentelemetry::trace::Span>>
TraceSpanManager::GetSpan(TraceSpanReferenceId id) const {
  auto find_it = spans_.find(id);
  if (find_it == spans_.end()) {
    return absl::NotFoundError(absl::StrFormat("span %i unknown", id.value()));
  }
  return find_it->second.span;
}

TraceSpanManager::ScopedSpan::ScopedSpan(
    TraceSpanReferenceId ref_id, TraceSpanManager* span_mgr,
    opentelemetry::trace::StatusCode completion_status,
    absl::string_view status_message)
    : span_reference_id_(ref_id),
      span_mgr_(span_mgr),
      completion_status_(completion_status),
      status_message_(status_message) {}

TraceSpanManager::ScopedSpan::~ScopedSpan() {
  if (span_reference_id_ == kInvalidTraceSpanReferenceId) return;
  absl::Status st = span_mgr_->EndSpan(span_reference_id_, completion_status_,
                                       status_message_);
  if (!st.ok()) {
    LOG(ERROR) << "Failed to end ScopedSpan with " << st;
  }
}

TraceSpanReferenceId TraceSpanManager::ScopedSpan::Release() {
  TraceSpanReferenceId tracked_id = span_reference_id_;
  span_reference_id_ = TraceSpanManager::kInvalidTraceSpanReferenceId;
  return tracked_id;
}

}  // namespace clips
}  // namespace executive
}  // namespace intrinsic
