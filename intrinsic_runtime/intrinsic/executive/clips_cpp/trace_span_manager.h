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

#ifndef INTRINSIC_EXECUTIVE_CLIPS_CPP_TRACE_SPAN_MANAGER_H_
#define INTRINSIC_EXECUTIVE_CLIPS_CPP_TRACE_SPAN_MANAGER_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/nullability.h"
#include "absl/base/thread_annotations.h"
#include "absl/container/node_hash_map.h"
#include "absl/log/log_streamer.h"
#include "absl/random/random.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "intrinsic/executive/clips_cpp/environment.h"
#include "intrinsic/util/status/status_macros.h"
#include "opentelemetry/common/attribute_value.h"
#include "opentelemetry/trace/span.h"
#include "opentelemetry/trace/span_context.h"
#include "opentelemetry/trace/span_metadata.h"
#include "ortools/base/strong_int.h"
#include "third_party/imported/cpp_libraries/clock/clock.h"

namespace intrinsic {
namespace executive {
namespace clips {

DEFINE_STRONG_INT_TYPE(TraceSpanReferenceId, int64_t);

// Manage tracing Span objects in CLIPS contexts.
// Maintains a mapping from ID (int64) to OTel Span and its metadata. It also
// provides useful CLIPS functions to start and end Span objects from CLIPS.
// Class is thread-unsafe.
class TraceSpanManager {
 public:
  // A TraceSpanManager::ScopedSpan is a scoped object that tracks a span that
  // is already stored in the given span_mgr.
  // When the object goes out of scope, the tracked span is ended in the
  // span_mgr.
  class ScopedSpan {
   public:
    // Constructs the ScopedSpan for ref_id in span_mgr. Optionally a
    // completion_status and message can be passed to end the span with another
    // status besides OK.
    explicit ScopedSpan(TraceSpanReferenceId ref_id, TraceSpanManager* span_mgr,
                        opentelemetry::trace::StatusCode completion_status =
                            opentelemetry::trace::StatusCode::kOk,
                        absl::string_view status_message = "");
    ~ScopedSpan();

    ScopedSpan(const ScopedSpan& rhs) = delete;
    ScopedSpan(ScopedSpan&& rhs) = delete;
    ScopedSpan& operator=(const ScopedSpan& rhs) = delete;
    ScopedSpan& operator=(ScopedSpan&& rhs) = delete;

    // Do not track the contained span_reference_id any more.
    // Does not end the span, but transfers the responsibility to end the span
    // to the span manager and its clients.
    TraceSpanReferenceId Release();

   private:
    TraceSpanReferenceId span_reference_id_;
    TraceSpanManager* span_mgr_;

    opentelemetry::trace::StatusCode completion_status_;
    std::string status_message_;
  };

  // ID representing a non-existing span. Particularly relevant on the CLIPS
  // side where we cannot deal with StatusOr.
  static constexpr TraceSpanReferenceId kInvalidTraceSpanReferenceId =
      TraceSpanReferenceId(0);

  // Wraps an OpenTelemetry span with additional metadata tracked by the
  // manager.
  //
  // Because OpenTelemetry Span objects are write-only and do not expose
  // inspectable APIs for retrieving their start time or recorded annotations
  // once added, `SpanInfo` retains this information locally to support duration
  // calculations and inspection in CLIPS.
  struct SpanInfo {
    std::shared_ptr<opentelemetry::trace::Span> span;
    absl::Time start_time;
    std::vector<std::pair<absl::Time, std::string>> annotations;
  };

  // Creates a TraceSpanManager and registers its functions in `environment`.
  //
  // `clock` drives the locally tracked timing information only, i.e.,
  // `SpanInfo` start times, annotation timestamps and execution durations. The
  // timestamps of the underlying OpenTelemetry spans and events are still
  // recorded by the OpenTelemetry SDK using the system clock.
  //
  // `clock` is not owned. If a custom clock is passed, it must outlive the
  // returned TraceSpanManager. The default real clock is never destroyed.
  static absl::StatusOr<std::unique_ptr<TraceSpanManager>> Create(
      Environment* environment,
      util::Clock* absl_nonnull clock ABSL_ATTRIBUTE_LIFETIME_BOUND =
          util::Clock::RealClock()) ABSL_LOCKS_EXCLUDED(environment->mutex());
  ~TraceSpanManager() ABSL_LOCKS_EXCLUDED(env_->mutex());

  // Add an externally started span to the SpanManager.
  // Its main use is to serve as a parent span for other spans in CLIPS as we do
  // not create root spans directly in CLIPS.
  // The span must have been already started and is considered running. From
  // hereon the SpanManager takes care of managing this span, in particular
  // ending it. This means that the passed in span object must not be ended by
  // any other means.
  absl::StatusOr<TraceSpanReferenceId> AddSpan(
      std::shared_ptr<opentelemetry::trace::Span> span);

  // Starts a new root span with display name <name>.
  // Only use this, if it must be a new root span as it creates a new trace
  // independent of other spans. Use StartSpan by default instead.
  // The span_type is added as an attribute to the span.
  // Returns an error or the TraceSpanReferenceId of the new span.
  absl::StatusOr<TraceSpanReferenceId> StartRootSpan(
      absl::string_view name, absl::string_view span_type);

  // Starts a span with display name <name>.
  // The span_type is added as an attribute to the span.
  // The parent_reference_id identifies the parent span of the span to start and
  // must exist.
  // Returns an error or the TraceSpanReferenceId of the new span.
  absl::StatusOr<TraceSpanReferenceId> StartSpan(
      absl::string_view name, absl::string_view span_type,
      TraceSpanReferenceId parent_reference_id);

  // Ends a span and removes it from the SpanManager.
  // The completion_status and message are set as the status of the span.
  // Returns an error if the span could not be ended or did not exist.
  absl::Status EndSpan(TraceSpanReferenceId id,
                       opentelemetry::trace::StatusCode completion_status =
                           opentelemetry::trace::StatusCode::kOk,
                       absl::string_view status_message = "");

  // Adds an attribute to the span identified by id.
  // Returns an error, if the span does not exist.
  absl::Status AddSpanAttribute(
      TraceSpanReferenceId id, absl::string_view key,
      const opentelemetry::common::AttributeValue& value);

  // Adds a time event annotation happening now.
  // Returns an error, if the span does not exist.
  absl::Status AddSpanEventAnnotation(TraceSpanReferenceId id,
                                      absl::string_view annotation);

  // Add a child link to the span identified by id. The linked span is
  // identified by trace_id_hex and span_id_hex.
  // Returns an error, if the span does not exist.
  absl::Status AddChildLink(TraceSpanReferenceId id,
                            absl::string_view trace_id_hex,
                            absl::string_view span_id_hex);

  // Returns a string representation of the underlying span in the form of
  // trace_id-span_id-span_options, where trace_id and span_id are the hex
  // representations of the IDs.
  // Note that a span object is only a reference to interact with the span and
  // properties like the name handed to StartSpan are not stored in the span
  // object and thus cannot be retrieved.
  absl::StatusOr<std::string> ToString(TraceSpanReferenceId id) const;

  // Returns the SpanContext of id, if a span with id is in SpanManager.
  absl::StatusOr<opentelemetry::trace::SpanContext> GetSpanContext(
      TraceSpanReferenceId id) const;

  // Returns the hex representation of the trace id that this span is in.
  absl::StatusOr<std::string> GetTraceIdHex(TraceSpanReferenceId id) const;

  // Returns the hex representation of this span's id.
  absl::StatusOr<std::string> GetSpanIdHex(TraceSpanReferenceId id) const;

  // Returns a link to Google cloud console to the trace that this span is in.
  // This assumes that opentelemetry instrumentation is set up to upload traces
  // to Google Cloud (which is the case for Intrinsic). The cloud_project that
  // is configured for the currently running application and where the traces
  // would be uploaded must be given for the link to work correctly.
  absl::StatusOr<std::string> GetGoogleCloudTracingLink(
      TraceSpanReferenceId id, absl::string_view cloud_project) const;

  // Returns the start time of a running span. The span is searched in the
  // running spans by its span context.
  absl::StatusOr<absl::Time> GetSpanStartTime(TraceSpanReferenceId id) const;

  // Searches for the given span reference id in the running spans and if that
  // that exists calculates its running duration up to now.
  // If there are events named "SUSPENDED"/"RESUME" the time between these is
  // not taken into account.
  // If start_event is set, the time from the start to the start_even is also
  // not taken into account.
  absl::StatusOr<absl::Duration> CalculateExecutionDuration(
      TraceSpanReferenceId id, absl::string_view start_event) const;

  // Get a Span object to use as the parent span for starting a Span not kept in
  // the TraceSpanManager. Do not use the returned span otherwise. Use
  // AddSpanAttribute/AddChildLink/etc. to interact with Spans in
  // TraceSpanManager. In particular do not end the returned span directly.
  absl::StatusOr<std::shared_ptr<opentelemetry::trace::Span>>
  GetSpanForParenting(TraceSpanReferenceId id) const {
    return GetSpan(id);
  }

  size_t GetNumSpans() const { return spans_.size(); }

  void DebugPrintAllSpans(
      std::ostream* stream = &absl::LogInfoStreamer(__builtin_FILE(),
                                                    __builtin_LINE())
                                  .stream()) const;

 private:
  explicit TraceSpanManager(Environment* environment,
                            util::Clock* absl_nonnull clock
                                ABSL_ATTRIBUTE_LIFETIME_BOUND =
                                    util::Clock::RealClock());

  template <typename ReturnType, typename... Args>
  absl::Status RegisterFunction(
      const std::string& name,
      const std::function<ReturnType(Args...)>& function)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(env_->mutex()) {
    INTR_RETURN_IF_ERROR(env_->AddFunction(name, function));
    functions_.push_back(name);
    return absl::OkStatus();
  }

  absl::Status RegisterFunctions() ABSL_EXCLUSIVE_LOCKS_REQUIRED(env_->mutex());
  void UnregisterFunctions() ABSL_EXCLUSIVE_LOCKS_REQUIRED(env_->mutex());

  std::string GetGoogleCloudTracingLinkFromTraceId(
      absl::string_view trace_id_hex, absl::string_view cloud_project) const;

  // Create a span context from the given trace and span id.
  absl::StatusOr<opentelemetry::trace::SpanContext> GetContextFromHexIds(
      absl::string_view trace_id_hex, absl::string_view span_id_hex);

  // Searches the running spans for a span with the same context as span_id.
  absl::StatusOr<const SpanInfo> GetRunningSpan(
      clips::TraceSpanReferenceId span_id) const;

  TraceSpanReferenceId GenerateNextSpanReferenceId();

  // Return a span pointer that can be passed on to other tracing functions.
  // Internal use only as modifying spans_ might invalidate the pointer. Thus
  // the resulting pointer should never be stored. We do not expose span
  // objects to the outside as they could be interacted with without our
  // knowledge.
  absl::StatusOr<std::shared_ptr<opentelemetry::trace::Span>> GetSpan(
      TraceSpanReferenceId id) const;

 private:
  absl::node_hash_map<TraceSpanReferenceId, SpanInfo> spans_;
  Environment* env_;
  util::Clock& clock_;
  absl::BitGen id_random_generator_;
  std::vector<std::string> functions_;
};

}  // namespace clips
}  // namespace executive
}  // namespace intrinsic

#endif  // INTRINSIC_EXECUTIVE_CLIPS_CPP_TRACE_SPAN_MANAGER_H_
