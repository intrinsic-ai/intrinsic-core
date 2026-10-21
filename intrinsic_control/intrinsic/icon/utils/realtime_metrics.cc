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

#include "intrinsic/icon/utils/realtime_metrics.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "google/protobuf/struct.pb.h"
#include "intrinsic/icon/testing/realtime_annotations.h"
#include "intrinsic/icon/utils/log.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/icon/utils/realtime_status_macro.h"
#include "intrinsic/performance/analysis/proto/performance_metrics.pb.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/time/clock_steady_interface.h"
#include "intrinsic/util/time/time.h"

namespace intrinsic::icon {

// static
absl::StatusOr<CycleTimeMetrics> CycleTimeMetrics::Create(
    const absl::Duration cycle_duration) {
  CycleTimeMetrics metrics;
  INTR_ASSIGN_OR_RETURN(metrics.apply_command_duration,
                        CycleTimeHistogram10::Create(cycle_duration));
  INTR_ASSIGN_OR_RETURN(metrics.read_status_duration,
                        CycleTimeHistogram10::Create(cycle_duration));
  INTR_ASSIGN_OR_RETURN(metrics.duration_between_read_status_calls,
                        CycleTimeHistogram10::Create(cycle_duration));
  INTR_ASSIGN_OR_RETURN(metrics.process_duration,
                        CycleTimeHistogram10::Create(cycle_duration));
  INTR_ASSIGN_OR_RETURN(metrics.execution_duration,
                        CycleTimeHistogram10::Create(cycle_duration));
  return metrics;
}

void CycleTimeMetrics::Reset() {
  apply_command_duration.Reset();
  read_status_duration.Reset();
  duration_between_read_status_calls.Reset();
  process_duration.Reset();
  execution_duration.Reset();
}

CycleTimeMetricsHelper::CycleTimeMetricsHelper(
    bool log_cycle_time_warnings,
    const ClockSteadyInterface& clock ABSL_ATTRIBUTE_LIFETIME_BOUND)
    : log_cycle_time_warnings_(log_cycle_time_warnings), clock_(clock) {}

// static
absl::StatusOr<CycleTimeMetricsHelper> CycleTimeMetricsHelper::Create(
    const absl::Duration cycle_duration, bool log_cycle_time_warnings,
    const ClockSteadyInterface& clock ABSL_ATTRIBUTE_LIFETIME_BOUND) {
  CycleTimeMetricsHelper helper(log_cycle_time_warnings, clock);
  INTR_ASSIGN_OR_RETURN(helper.metrics_,
                        CycleTimeMetrics::Create(cycle_duration));
  return helper;
}

void CycleTimeMetricsHelper::Reset() {
  metrics_.Reset();
  apply_command_start_ = TimeSteady::InfinitePast();
  apply_command_end_ = TimeSteady::InfinitePast();
  read_status_start_ = TimeSteady::InfinitePast();
  read_status_end_ = TimeSteady::InfinitePast();

  previous_read_status_duration_ = absl::InfiniteDuration();
  previous_apply_command_duration_ = absl::InfiniteDuration();
  previous_process_duration_ = absl::InfiniteDuration();
  previous_execution_duration_ = absl::InfiniteDuration();
}

void CycleTimeMetricsHelper::ResetReadStatusStart() {
  read_status_start_ = TimeSteady::InfinitePast();
}

RealtimeStatus CycleTimeMetricsHelper::ReadStatusStart() {
  const TimeSteady now = clock_.Now();
  const TimeSteady previous_read_status_start = read_status_start_;
  const absl::Duration duration_between_read_status_calls =
      now - previous_read_status_start;
  const absl::Duration& cycle_duration =
      metrics_.read_status_duration.CycleDuration();

  if (metrics_.read_status_duration.NumEntries() >=
      std::numeric_limits<uint32_t>::max()) [[unlikely]] {
    metrics_.Reset();
    INTRINSIC_RT_LOG(INFO) << "Metrics reset due to overflow.";
  }

  // Record `read_status_start_` before returning any error from prior-phase
  // histograms. This ensures the matching `ReadStatusEnd()` call measures the
  // current cycle instead of a stale start timestamp.
  read_status_start_ = now;

  if (apply_command_end_ != TimeSteady::InfinitePast()) [[likely]] {
    previous_execution_duration_ = now - apply_command_end_;
    INTRINSIC_RT_RETURN_IF_ERROR(
        metrics_.execution_duration.Add(previous_execution_duration_));
  }

  if (previous_read_status_start != TimeSteady::InfinitePast()) [[likely]] {
    INTRINSIC_RT_RETURN_IF_ERROR(
        metrics_.duration_between_read_status_calls.Add(
            duration_between_read_status_calls));

    if (log_cycle_time_warnings_ &&
        duration_between_read_status_calls >=
            cycle_duration * kCycleTimeOverrunWarningFactor) [[unlikely]] {
      // Non Throttled so we see all occurrences.
      INTRINSIC_RT_LOG_THROTTLED(WARNING)
          << "Long duration between read_status_calls: "
          << duration_between_read_status_calls
          << " expected: " << cycle_duration
          << " [previous: rs=" << previous_read_status_duration_
          << ", proc=" << previous_process_duration_
          << ", ac=" << previous_apply_command_duration_
          << ", exec=" << previous_execution_duration_ << "]";
    }

    if (log_cycle_time_warnings_ &&
        duration_between_read_status_calls <=
            cycle_duration * kCycleTimeUnderrunWarningFactor) [[unlikely]] {
      // Non Throttled so we see all occurrences.
      INTRINSIC_RT_LOG_THROTTLED(WARNING)
          << "Short duration between read_status_calls: "
          << duration_between_read_status_calls
          << " expected: " << cycle_duration
          << " [previous: rs=" << previous_read_status_duration_
          << ", proc=" << previous_process_duration_
          << ", ac=" << previous_apply_command_duration_
          << ", exec=" << previous_execution_duration_ << "]";
    }
  }

  return OkStatus();
}

RealtimeStatus CycleTimeMetricsHelper::ReadStatusEnd() {
  read_status_end_ = clock_.Now();
  const absl::Duration& cycle_duration =
      metrics_.read_status_duration.CycleDuration();
  const absl::Duration max_op_duration =
      cycle_duration * kSingleOpWarningFactor;
  const absl::Duration duration = read_status_end_ - read_status_start_;

  if (read_status_start_ == TimeSteady::InfinitePast()) [[unlikely]] {
    return FailedPreconditionError(
        "ReadStatusStart() was not called before ReadStatusEnd().");
  }

  INTRINSIC_RT_RETURN_IF_ERROR(metrics_.read_status_duration.Add(duration));

  if (log_cycle_time_warnings_ && duration >= max_op_duration) [[unlikely]] {
    // Non Throttled so we see all occurrences.
    INTRINSIC_RT_LOG_THROTTLED(WARNING)
        << "Long duration of ReadStatus: " << duration
        << " max: " << max_op_duration
        << " [previous: rs=" << previous_read_status_duration_
        << ", proc=" << previous_process_duration_
        << ", ac=" << previous_apply_command_duration_
        << ", exec=" << previous_execution_duration_ << "]";
  }

  previous_read_status_duration_ = duration;
  return OkStatus();
};

RealtimeStatus CycleTimeMetricsHelper::ApplyCommandStart() {
  const TimeSteady now = clock_.Now();

  // Record `apply_command_start_` before returning any error from
  // `process_duration.Add()` so `ApplyCommandEnd()` always sees the current
  // start timestamp.
  apply_command_start_ = now;

  if (read_status_end_ != TimeSteady::InfinitePast()) [[likely]] {
    previous_process_duration_ = now - read_status_end_;
    INTRINSIC_RT_RETURN_IF_ERROR(
        metrics_.process_duration.Add(previous_process_duration_));
  }

  return OkStatus();
}

RealtimeStatus CycleTimeMetricsHelper::ApplyCommandEnd() {
  apply_command_end_ = clock_.Now();

  if (apply_command_start_ == TimeSteady::InfinitePast()) [[unlikely]] {
    return FailedPreconditionError(
        "ApplyCommandStart() was not called before ApplyCommandEnd().");
  }

  const absl::Duration& cycle_duration =
      metrics_.apply_command_duration.CycleDuration();
  const absl::Duration max_op_duration =
      cycle_duration * kSingleOpWarningFactor;
  const absl::Duration duration = apply_command_end_ - apply_command_start_;
  INTRINSIC_RT_RETURN_IF_ERROR(metrics_.apply_command_duration.Add(duration));

  if (log_cycle_time_warnings_ && duration >= max_op_duration) [[unlikely]] {
    // Non throttled so we see all occurrences.
    INTRINSIC_RT_LOG_THROTTLED(WARNING)
        << "Long duration of ApplyCommand: " << duration
        << " max: " << max_op_duration
        << " [previous: ac=" << previous_apply_command_duration_
        << ", exec=" << previous_execution_duration_
        << ", rs=" << previous_read_status_duration_
        << ", proc=" << previous_process_duration_ << "]";
  }
  previous_apply_command_duration_ = duration;
  return OkStatus();
}

ReadStatusScope::ReadStatusScope(CycleTimeMetricsHelper* metrics_helper,
                                 bool is_active)
    : metrics_helper_(metrics_helper), is_active_(is_active) {
  if (!metrics_helper_) {
    return;
  }
  if (!is_active_) [[unlikely]] {
    return;
  }

  if (const auto status = metrics_helper_->ReadStatusStart(); !status.ok()) {
    INTRINSIC_RT_LOG_THROTTLED(WARNING)
        << "Failed to gather ReadStatus metrics: " << status.message();
  }
}

ReadStatusScope::~ReadStatusScope() {
  if (!metrics_helper_) {
    return;
  }
  if (!is_active_) [[unlikely]] {
    return;
  }
  if (const auto status = metrics_helper_->ReadStatusEnd(); !status.ok()) {
    INTRINSIC_RT_LOG_THROTTLED(WARNING)
        << "Failed to collect ReadStatus metrics: " << status.message();
  }
}

ApplyCommandScope::ApplyCommandScope(CycleTimeMetricsHelper* metrics_helper,
                                     bool is_active)
    : metrics_helper_(metrics_helper), is_active_(is_active) {
  if (!metrics_helper_) {
    return;
  }
  if (!is_active_) [[unlikely]] {
    return;
  }
  if (const auto status = metrics_helper_->ApplyCommandStart(); !status.ok())
      [[unlikely]] {
    INTRINSIC_RT_LOG_THROTTLED(WARNING)
        << "Failed to gather ApplyCommand metrics: " << status.message();
  }
}

ApplyCommandScope::~ApplyCommandScope() {
  if (!metrics_helper_) {
    return;
  }
  if (!is_active_) [[unlikely]] {
    return;
  }
  if (const auto status = metrics_helper_->ApplyCommandEnd(); !status.ok())
      [[unlikely]] {
    INTRINSIC_RT_LOG_THROTTLED(WARNING)
        << "Failed to collect ApplyCommand metrics: " << status.message();
  }
}

namespace metrics_internal {

void InsertDurationField(
    intrinsic_proto::performance::analysis::proto::PerformanceMetrics&
        perf_metrics,
    absl::string_view field_name,
    absl::Duration duration) INTRINSIC_NON_REALTIME_ONLY {
  google::protobuf::Value field_value_proto;
  field_value_proto.set_number_value(absl::ToInt64Microseconds(duration));
  perf_metrics.mutable_metrics()->mutable_metrics()->mutable_fields()->insert(
      std::make_pair(absl::StrCat(field_name, "_us"), field_value_proto));
}

void InsertNumericField(
    intrinsic_proto::performance::analysis::proto::PerformanceMetrics&
        perf_metrics,
    absl::string_view field_name,
    double field_value) INTRINSIC_NON_REALTIME_ONLY {
  google::protobuf::Value field_value_proto;
  field_value_proto.set_number_value(field_value);
  perf_metrics.mutable_metrics()->mutable_metrics()->mutable_fields()->insert(
      std::make_pair(std::string(field_name), field_value_proto));
}

void InsertListValueField(
    intrinsic_proto::performance::analysis::proto::PerformanceMetrics&
        perf_metrics,
    absl::string_view field_name,
    const google::protobuf::ListValue& listvalue) INTRINSIC_NON_REALTIME_ONLY {
  google::protobuf::Value field_value_proto;
  *field_value_proto.mutable_list_value() = listvalue;
  perf_metrics.mutable_metrics()->mutable_metrics()->mutable_fields()->insert(
      std::make_pair(std::string(field_name), field_value_proto));
}

void InsertValueField(
    intrinsic_proto::performance::analysis::proto::PerformanceMetrics&
        perf_metrics,
    absl::string_view field_name,
    const google::protobuf::Value& value_proto) INTRINSIC_NON_REALTIME_ONLY {
  perf_metrics.mutable_metrics()->mutable_metrics()->mutable_fields()->insert(
      std::make_pair(std::string(field_name), value_proto));
}

google::protobuf::Value SingleBucket(
    absl::string_view bucket_name, uint32_t bucket_count,
    absl::string_view interval) INTRINSIC_NON_REALTIME_ONLY {
  google::protobuf::Value value_proto;

  google::protobuf::Struct* bucket = value_proto.mutable_struct_value();

  google::protobuf::Value count_proto;
  count_proto.set_number_value(bucket_count);
  bucket->mutable_fields()->insert(std::make_pair("count", count_proto));

  google::protobuf::Value interval_proto;
  interval_proto.set_string_value(interval);
  bucket->mutable_fields()->insert(std::make_pair("interval", interval_proto));

  return value_proto;
}

google::protobuf::ListValue InsertLessThanCycleDurationEntries(
    intrinsic_proto::performance::analysis::proto::PerformanceMetrics&
        perf_metrics,
    const absl::Duration bucket_size,
    absl::Span<const uint32_t> buckets_lt_cycle_duration)
    INTRINSIC_NON_REALTIME_ONLY {
  const size_t num_buckets_lt_cycle_duration = buckets_lt_cycle_duration.size();
  const size_t kNumZeroPad = log10(num_buckets_lt_cycle_duration - 1) + 1;

  google::protobuf::ListValue listvalue;
  listvalue.mutable_values()->Reserve(num_buckets_lt_cycle_duration);

  for (size_t i = 0; i < num_buckets_lt_cycle_duration; ++i) {
    absl::Duration bucket_start = bucket_size * i;
    absl::Duration bucket_end = bucket_size * (i + 1);

    std::ostringstream key;
    // Prepend 'bucket_lt_cycle ' for simple identification of buckets in the
    // exported JSON.
    key << "bucket_lt_cycle ";
    // Prepend zeros so the buckets are sorted when sorted alphabetically..
    key << std::setfill('0');
    key << std::setw(kNumZeroPad) << i;

    metrics_internal::InsertValueField(
        perf_metrics, key.str(),
        SingleBucket(key.str(), buckets_lt_cycle_duration[i],
                     absl::StrFormat("[%v %v)", bucket_start, bucket_end)));
  }
  return listvalue;
}

google::protobuf::ListValue InsertGreaterEqualCycleDurationEntries(
    intrinsic_proto::performance::analysis::proto::PerformanceMetrics&
        perf_metrics,
    const absl::Duration bucket_size,
    const size_t num_buckets_lt_cycle_duration,
    absl::Span<const uint32_t> buckets_ge_cycle_duration)
    INTRINSIC_NON_REALTIME_ONLY {
  const size_t num_ge_cycle_duration_buckets = buckets_ge_cycle_duration.size();
  const size_t kNumZeroPad = log10(num_ge_cycle_duration_buckets - 1) + 1;

  google::protobuf::ListValue listvalue;
  listvalue.mutable_values()->Reserve(num_ge_cycle_duration_buckets);

  for (size_t i = 0; i < num_ge_cycle_duration_buckets; ++i) {
    absl::Duration bucket_start =
        bucket_size * (i + num_buckets_lt_cycle_duration);
    absl::Duration bucket_end =
        bucket_size * (i + 1 + num_buckets_lt_cycle_duration);

    std::ostringstream key;

    // Prepends 'bucket_ge_cycle ' for simple identification of buckets in the
    // exported JSON.
    key << "bucket_ge_cycle ";
    // Prepen zeros so the buckets are sorted when sorted alphabetically..
    key << std::setfill('0');
    key << std::setw(kNumZeroPad) << i;

    metrics_internal::InsertValueField(
        perf_metrics, key.str(),
        SingleBucket(key.str(), buckets_ge_cycle_duration[i],
                     absl::StrFormat("[%v %v)", bucket_start, bucket_end)));
  }
  return listvalue;
}

}  // namespace metrics_internal

std::vector<intrinsic_proto::performance::analysis::proto::PerformanceMetrics>
ToPerformanceMetrics(const CycleTimeMetrics& metrics)
    INTRINSIC_NON_REALTIME_ONLY {
  std::vector<intrinsic_proto::performance::analysis::proto::PerformanceMetrics>
      metrics_protos;
  metrics_protos.reserve(5);
  // ApplyCommand start to ApplyCommand end.
  metrics_protos.push_back(ToPerformanceMetrics(
      "apply_command_duration", metrics.apply_command_duration));
  // ReadStatus start to ReadStatus end.
  metrics_protos.push_back(ToPerformanceMetrics("read_status_duration",
                                                metrics.read_status_duration));
  // ReadStatus start to ReadStatus start.
  metrics_protos.push_back(
      ToPerformanceMetrics("duration_between_read_status_calls",
                           metrics.duration_between_read_status_calls));
  // From ReadStatus end to ApplyCommand start. (=ICON duration).
  metrics_protos.push_back(
      ToPerformanceMetrics("process_duration", metrics.process_duration));
  // From ApplyCommand end to ReadStatus start. (=Hardware duration).
  metrics_protos.push_back(
      ToPerformanceMetrics("execution_duration", metrics.execution_duration));
  return metrics_protos;
}

}  // namespace intrinsic::icon
