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

#ifndef INTRINSIC_ICON_UTILS_REALTIME_METRICS_H_
#define INTRINSIC_ICON_UTILS_REALTIME_METRICS_H_

#include <sys/types.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "google/protobuf/struct.pb.h"
#include "gtest/gtest_prod.h"
#include "intrinsic/icon/testing/realtime_annotations.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/performance/analysis/proto/performance_metrics.pb.h"
#include "intrinsic/util/time/clock_steady_interface.h"
#include "intrinsic/util/time/time.h"

// Contains helpers to measure cycle time metrics of hardware modules.
// The CycleTimeMetricsHelper can be configured to log warnings/errors when the
// cycle time is breached, or a single operation took too long.
//
// The cumulative Metrics can be exported as
// intrinsic_proto::performance::analysis::proto::PerformanceMetrics proto for
// storage and analysis.
//
// Expected Usage:
//
// During Init Phase:
// * Create a CycleTimeMetricsHelper that stores the metrics (`clock` is a
//   `ClockSteadyInterface` that must outlive the helper).
// ASSERT_OK_AND_ASSIGN(auto helper, CycleTimeMetricsHelper::Create(
//                                       Your_Cycle_Time,
//                                       /*log_cycle_time_warnings=*/true,
//                                       clock));
//
// At the top of ReadStatus:
// * Call ReadStatusScope read_status_scope(&helper_, state == kEnabled);
//
// At the top of ApplyCommand:
// * Call ApplyCommandScope apply_command_scope(&helper_, state == kEnabled);
//
// Every n-cycles (n can be zero for every cycle):
// * Use a RealtimeWriteQueue to send the histograms to a non-rt thread.
// * Use helper.Metrics() for a read only reference to the data.
//
// When appropriate e.g. never, or during Activate/EnableMotion:
// * Call helper.Reset() to reset all measurements.
//   or helper.ResetReadStatusStart() to only reset the read_status_start_ time.
//   NOTE: Measurements are also reset when a bucket is full.
//
// In a non-rt thread:
// * Export the histogram.
//
// Histogram data is cumulative so that no data is lost when the export is
// throttled.
namespace intrinsic::icon {

// Histogram to measure the distribution of a cyclic event without overwhelming
// a realtime thread by storing the measurements in cumulative buckets/slots.
//
// The histogram stores the cumulative count of events occur within the
// configured 2x`cycle_duration` in the respective bucket.
//
// The template parameter `kNumBucketsPerCycleDuration` defines the number of
// buckets for the cycle duration. The full number of buckets is
// 2*`kNumBucketsPerCycleDuration`.
//
// Explicitly counts the number of overruns and the largest duration of an
// event.
//
// `Reset()` resets the counts and overruns. It does not reset the
// cycle_duration.
//
// An overrun is defined as a duration that is outside of the range of buckets
// (=2x cycle_duration).
// -----------------------------------------------------------------------------
// Example:
// For CycleTimeHistogram<10> with a cycle time of 10ms, we have:
// 10 buckets for counts less than the cycle duration and 10 buckets for counts
// greater than or equal to the cycle duration.
//
// Storage in the format [index] <bound of values>:
// [0] [0,1)ms
// [1] [1,2)ms
// [2] [2,3)ms
// [3] [3,4)ms
// [4] [4,5)ms
// [5] [5,6)ms
// [6] [6,7)ms
// [7] [7,8)ms
// [8] [8,9)ms
// [9] [9,10)ms
//
// GE cycle duration storage:
// [0] [10,11)ms
// [1] [11,12)ms
// [2] [12,13)ms
// [3] [13,14)ms
// [4] [14,15)ms
// [5] [15,16)ms
// [6] [16,17)ms
// [7] [17,18)ms
// [8] [18,19)ms
// [9] [19,20)ms
template <const size_t kNumBucketsPerCycleDuration>
class CycleTimeHistogram {
 public:
  // Initializes the histogram with the cycle period.
  static absl::StatusOr<CycleTimeHistogram<kNumBucketsPerCycleDuration>> Create(
      const absl::Duration cycle_duration) INTRINSIC_NON_REALTIME_ONLY {
    if (cycle_duration <= absl::ZeroDuration()) [[unlikely]] {
      return InvalidArgumentError(RealtimeStatus::StrCat(
          "cycle_duration '", cycle_duration, "' must be positive."));
    }

    return CycleTimeHistogram<kNumBucketsPerCycleDuration>(cycle_duration);
  }

  // For usage in containers.
  CycleTimeHistogram<kNumBucketsPerCycleDuration>() = default;

  // Does not reset the cycle_duration.
  // Resets the counts and overruns.
  // Call to start a new cycle of measurements.
  void Reset() INTRINSIC_CHECK_REALTIME_SAFE {
    less_than_cycle_duration_counts_.fill(0);
    greater_equal_cycle_duration_counts_.fill(0);
    num_overruns_ = 0;
    num_entries_less_than_cycle_duration_ = 0;
    num_entries_greater_equal_cycle_duration_ = 0;
    max_ = absl::ZeroDuration();
  }

  // Adds a positive duration to the histogram.
  // Returns `FailedPreconditionError` if the duration is negative or zero, or
  // if the histogram was not initialized via `Create()`. We use
  // `FailedPreconditionError` instead of `InvalidArgumentError` because callers
  // typically use `CycleTimeMetricsHelper` (which computes durations from clock
  // reads rather than caller arguments) and forward this status directly.
  RealtimeStatus Add(const absl::Duration duration)
      INTRINSIC_CHECK_REALTIME_SAFE {
    if (duration <= absl::ZeroDuration()) [[unlikely]] {
      return FailedPreconditionError(RealtimeStatus::StrCat(
          "duration '", duration, "' must be positive."));
    }
    if (configured_cycle_duration_ <= absl::ZeroDuration()) [[unlikely]] {
      return FailedPreconditionError(
          RealtimeStatus::StrCat("cycle_duration '", configured_cycle_duration_,
                                 "' must be positive. Likely not initialized. "
                                 "Use Create() to initialize."));
    }

    max_ = std::max(max_, duration);

    // fraction is in the interval [0, inf) based on configured_cycle_duration_.
    const int64_t fraction =
        (duration * static_cast<int64_t>(kNumBucketsPerCycleDuration)) /
        configured_cycle_duration_;
    if (fraction < kNumBucketsPerCycleDuration) [[likely]] {
      num_entries_less_than_cycle_duration_++;
      // Increments the correct slot within the bounds of the array.
      less_than_cycle_duration_counts_[fraction]++;

    } else if (fraction < 2 * kNumBucketsPerCycleDuration) {
      num_entries_greater_equal_cycle_duration_++;
      // Store detailed statistics for overruns up to one additional
      // cycle_time.
      greater_equal_cycle_duration_counts_[fraction -
                                           kNumBucketsPerCycleDuration]++;
    } else {
      // overrun without statistics
      num_overruns_++;
    }

    return OkStatus();
  }

  absl::Duration CycleDuration() const INTRINSIC_CHECK_REALTIME_SAFE {
    return configured_cycle_duration_;
  }

  // Number of entries that are larger than 2x cycle duration and not stored
  // in a bucket.
  // The longest duration is stored separately as `Max()`.
  uint32_t NumberOfOverruns() const INTRINSIC_CHECK_REALTIME_SAFE {
    return num_overruns_;
  }

  // The largest duration that was added to the histogram. It is not stored
  // in a bucket when it was > 2x cycle duration.
  absl::Duration Max() const INTRINSIC_CHECK_REALTIME_SAFE { return max_; }

  // The number of entries that are shorter than the cycle period.
  uint32_t NumberOfEntriesLessThanCycleDuration() const
      INTRINSIC_CHECK_REALTIME_SAFE {
    return num_entries_less_than_cycle_duration_;
  }

  // The number of entries that are equal or larger than the cycle period.
  // Not all of those entries are stored as buckets.
  uint32_t NumberOfEntriesGreaterEqualCycleDuration() const
      INTRINSIC_CHECK_REALTIME_SAFE {
    return num_entries_greater_equal_cycle_duration_ + NumberOfOverruns();
  }

  // The number of entries that were added to the histogram, including overruns.
  // Entries outside of the range of buckets (overrruns) are counted, but not
  // stored.
  uint32_t NumEntries() const INTRINSIC_CHECK_REALTIME_SAFE {
    return NumberOfEntriesLessThanCycleDuration() +
           NumberOfEntriesGreaterEqualCycleDuration();
  }

  // Buckets for overruns of up to one additional cycle_time.
  absl::Span<const uint32_t> BucketsGECycleDuration() const
      INTRINSIC_CHECK_REALTIME_SAFE {
    return absl::MakeSpan(greater_equal_cycle_duration_counts_.begin(),
                          greater_equal_cycle_duration_counts_.end());
  }

  // The non overrun buckets of the histogram.
  absl::Span<const uint32_t> BucketsLTCycleDuration() const
      INTRINSIC_CHECK_REALTIME_SAFE {
    return absl::MakeSpan(less_than_cycle_duration_counts_.begin(),
                          less_than_cycle_duration_counts_.end());
  }

  // Not realtime safe!
  template <typename Sink>
  friend void AbslStringify(Sink& sink, const CycleTimeHistogram& p)
      INTRINSIC_NON_REALTIME_ONLY {
    absl::Format(&sink,
                 "cycle_duration:%v num_entries[%d] "
                 "num_entries_lt_cycle_duration[%d] "
                 "num_entries_ge_cycle_duration[%d] num_overruns:%v max:%v "
                 " buckets_lt_cycle_duration[%s]"
                 " buckets_ge_cycle_duration[%s]",
                 absl::FormatDuration(p.configured_cycle_duration_),
                 p.NumEntries(), p.NumberOfEntriesLessThanCycleDuration(),
                 p.NumberOfEntriesGreaterEqualCycleDuration(),
                 p.NumberOfOverruns(), absl::FormatDuration(p.Max()),
                 absl::StrJoin(p.BucketsLTCycleDuration().begin(),
                               p.BucketsLTCycleDuration().end(), "|"),
                 absl::StrJoin(p.BucketsGECycleDuration().begin(),
                               p.BucketsGECycleDuration().end(), "|"));
  }

 private:
  FRIEND_TEST(CycleTimeMetricsHelper, ResetsOnOverflow);
  // Directly constructs an empty histogram with the provided cycle period.
  explicit CycleTimeHistogram<kNumBucketsPerCycleDuration>(
      absl::Duration cycle_duration)
      : configured_cycle_duration_(cycle_duration) {}

  // Stores the cycle statistics for event [0, configured_cycle_duration_) in
  // buckets of cycle_time/kNumBuckets. Uses array so that the histogram can be
  // put into a rt to non-rt queue.
  std::array<uint32_t, kNumBucketsPerCycleDuration>
      less_than_cycle_duration_counts_ = {0};
  // Stores statistics for events from [configured_cycle_duration_,
  // 2*configured_cycle_duration_) in buckets of cycle_time/kNumBuckets.
  // Uses array so that the histogram can be put into a rt to non-rt queue (is
  // trivially copyable).
  std::array<uint32_t, kNumBucketsPerCycleDuration>
      greater_equal_cycle_duration_counts_ = {0};

  uint32_t num_entries_less_than_cycle_duration_ = 0;
  uint32_t num_entries_greater_equal_cycle_duration_ = 0;
  // Count of events not stored in buckests >= 2*configured_cycle_duration_.
  uint32_t num_overruns_ = 0;
  // The longest duration in the histogram.
  // Is not stored in a bucket, when > 2*configured_cycle_duration_.
  absl::Duration max_ = absl::ZeroDuration();
  // The cycle period that was used to create the histogram.
  // Longer durations are considered an overrun.
  absl::Duration configured_cycle_duration_ = absl::ZeroDuration();
};

// Histogram with ten normal buckets and ten bucket for "overruns".
using CycleTimeHistogram10 = CycleTimeHistogram<10>;

// Collection of cycle time histograms that measure important cycle time
// metrics.
// To be put into the rt to non rt queue ~every few cycles e.g. every Second.
struct CycleTimeMetrics {
  static absl::StatusOr<CycleTimeMetrics> Create(absl::Duration cycle_duration);

  // Resets the values stored in the histograms to start a new measuring cycle.
  void Reset();

  // ApplyCommand start to ApplyCommand end.
  CycleTimeHistogram10 apply_command_duration;
  // ReadStatus start to ReadStatus end.
  CycleTimeHistogram10 read_status_duration;
  // ReadStatus start to ReadStatus start.
  // Not measuring duration_between_apply_command_calls (ApplyCommand start to
  // ApplyCommand start) because ApplyCommand is only called when the HWM is
  // enabled.
  CycleTimeHistogram10 duration_between_read_status_calls;
  // From ReadStatus end to ApplyCommand start. (=ICON duration).
  CycleTimeHistogram10 process_duration;
  // From ApplyCommand end to ReadStatus start. (=Hardware duration).
  CycleTimeHistogram10 execution_duration;
};

// Helper to measure cycle time metrics.
// Optionally logs warnings when the cycle time is breached, or a
// single operation took too long (see kCycleTimeWarningFactor and
// kSingleOpWarningFactor).
// Measures:
// - apply_command_duration: ApplyCommand start to ApplyCommand end.
// - read_status_duration: ReadStatus start to ReadStatus end.
// - duration_between_read_status_calls: ReadStatus start to ReadStatus start.
// - process_duration: From ReadStatus end to ApplyCommand start. (=ICON
//   duration).
// - execution_duration: From ApplyCommand end to ReadStatus start. (=Hardware
//   duration).
// Expects to be called in the following order:
// 1. ReadStatusStart()
// 2. ReadStatusEnd()
// 3. ApplyCommandStart()
// 4. ApplyCommandEnd()
// goto 1
class CycleTimeMetricsHelper {
 public:
  // When `log_cycle_time_warnings` is true.
  // Logs a warning if the time between ReadStatus calls is not within the
  // accepted range.
  static constexpr double kCycleTimeOverrunWarningFactor = 1.15;  // 15% jitter.
  static_assert(1.0 < kCycleTimeOverrunWarningFactor &&
                kCycleTimeOverrunWarningFactor < 2.0);
  // Somewhat hacky `abs` to constexpr compute the unterrun factor.
  static constexpr double kCycleTimeUnderrunWarningFactor =
      2.0 - kCycleTimeOverrunWarningFactor;
  static_assert(0.0 < kCycleTimeUnderrunWarningFactor &&
                kCycleTimeUnderrunWarningFactor < 1.0);
  // When `log_cycle_time_warnings` is true.
  // Logs a warning if the duration of ReadStatus or ApplyCommand is >= than
  // cycle_duration * kSingleOpWarningFactor
  static constexpr double kSingleOpWarningFactor = .5;

  // Creates a CycleTimeMetricsHelper for the given `cycle_duration`, using
  // `clock` to measure durations.
  // If `log_cycle_time_warnings` is true, logs warnings/errors when the cycle
  // time is breached, or a single operation took too long.
  static absl::StatusOr<CycleTimeMetricsHelper> Create(
      absl::Duration cycle_duration, bool log_cycle_time_warnings,
      const ClockSteadyInterface& clock ABSL_ATTRIBUTE_LIFETIME_BOUND);

  // Resets the helper to the initial state.
  // Resets the values stored in the histograms to start a new measuring cycle.
  void Reset();
  // Resets the read_status_start_ time so that the first ReadStatus cycle is
  // measured correctly when not calling `Reset()` to fully reset the metrics.
  // Call in `Activate` to reset the `read_status_start_` time.
  void ResetReadStatusStart();

  // Returns FailedPreconditionError if:
  // * now - time of `ReadStatusEnd()` <= 0 and `ReadStatusEnd()` has been
  //  called at least once.
  RealtimeStatus ApplyCommandStart() INTRINSIC_CHECK_REALTIME_SAFE;

  // Returns FailedPreconditionError if:
  // * `ApplyCommandStart()` has not been called.
  // * now -time of `ApplyCommandStart()` <= 0.
  RealtimeStatus ApplyCommandEnd() INTRINSIC_CHECK_REALTIME_SAFE;

  // Returns FailedPreconditionError if:
  // * now - time of `ReadStatusStart()` <= 0 and `ReadStatusStart()` has been
  //  called at least once.
  // * now - time of `ApplyCommandEnd()` <= 0 and `ApplyCommandEnd()` has been
  //  called at least once.
  // Resets all histograms to the initial state before triggering an overflow.
  RealtimeStatus ReadStatusStart() INTRINSIC_CHECK_REALTIME_SAFE;

  // Returns FailedPreconditionError if:
  // * `ReadStatusStart()` has not been called.
  // * time of now - `ReadStatusStart()` <= 0.
  RealtimeStatus ReadStatusEnd() INTRINSIC_CHECK_REALTIME_SAFE;
  // Returns a read only reference to the metrics.
  const CycleTimeMetrics& Metrics() const INTRINSIC_CHECK_REALTIME_SAFE {
    return metrics_;
  }
  // Returns a mutable reference to the metrics.
  CycleTimeMetrics& MutableMetrics() INTRINSIC_CHECK_REALTIME_SAFE {
    return metrics_;
  }

 private:
  explicit CycleTimeMetricsHelper(bool log_cycle_time_warnings,
                                  const ClockSteadyInterface& clock
                                      ABSL_ATTRIBUTE_LIFETIME_BOUND);

  bool log_cycle_time_warnings_;
  const ClockSteadyInterface& clock_;

  TimeSteady apply_command_start_ = TimeSteady::InfinitePast();
  TimeSteady apply_command_end_ = TimeSteady::InfinitePast();

  TimeSteady read_status_start_ = TimeSteady::InfinitePast();
  TimeSteady read_status_end_ = TimeSteady::InfinitePast();
  CycleTimeMetrics metrics_;

  absl::Duration previous_read_status_duration_ = absl::InfiniteDuration();
  absl::Duration previous_apply_command_duration_ = absl::InfiniteDuration();
  absl::Duration previous_process_duration_ = absl::InfiniteDuration();
  absl::Duration previous_execution_duration_ = absl::InfiniteDuration();
};

// Helper that automatically calls ReadStatusStart() on creation and
// ReadStatusEnd() on destruction.
// Pass nullptr to disable metrics.
// Set `is_active` to false to disable metrics and warnings for the current
// call.
class ReadStatusScope {
 public:
  // Is a no-op when `is_active` is false.
  ReadStatusScope(CycleTimeMetricsHelper* metrics_helper, bool is_active);

  ~ReadStatusScope();

 private:
  CycleTimeMetricsHelper* metrics_helper_;
  bool is_active_;
};

// Helper that automatically calls ApplyCommandStart() on creation and
// ApplyCommandEnd() on destruction.
// Pass nullptr to disable metrics.
// Set `is_active` to false to disable metrics and warnings for the current
// call.
class ApplyCommandScope {
 public:
  // Is a no-op when `is_active` is false.
  ApplyCommandScope(CycleTimeMetricsHelper* metrics_helper, bool is_active);

  ~ApplyCommandScope();

 private:
  CycleTimeMetricsHelper* metrics_helper_;
  bool is_active_;
};

namespace metrics_internal {
// Helper functions to build performance metrics proto

// Converts Duration to Int64Microseconds and automatically appends `_us` to
// the field_name.
void InsertDurationField(
    intrinsic_proto::performance::analysis::proto::PerformanceMetrics&
        perf_metrics,
    absl::string_view field_name,
    absl::Duration duration) INTRINSIC_NON_REALTIME_ONLY;

void InsertNumericField(
    intrinsic_proto::performance::analysis::proto::PerformanceMetrics&
        perf_metrics,
    absl::string_view field_name,
    double field_value) INTRINSIC_NON_REALTIME_ONLY;

void InsertListValueField(
    intrinsic_proto::performance::analysis::proto::PerformanceMetrics&
        perf_metrics,
    absl::string_view field_name,
    const google::protobuf::ListValue& listvalue) INTRINSIC_NON_REALTIME_ONLY;

void InsertValueField(
    intrinsic_proto::performance::analysis::proto::PerformanceMetrics&
        perf_metrics,
    absl::string_view field_name,
    const google::protobuf::Value& value_proto) INTRINSIC_NON_REALTIME_ONLY;

google::protobuf::Value SingleBucket(
    absl::string_view bucket_name, uint32_t bucket_count,
    absl::string_view interval) INTRINSIC_NON_REALTIME_ONLY;

// Inserts the buckets lower than cycle duration into the performance metrics
// proto `perf_metrics`. The buckets are inserted as fields in the format:
//  key: "bucket_lt_cycle 00"
//  ...
//  key: "bucket_lt_cycle 10"
// Every bucket contains their interval and the number of entries.
google::protobuf::ListValue InsertLessThanCycleDurationEntries(
    intrinsic_proto::performance::analysis::proto::PerformanceMetrics&
        perf_metrics,
    absl::Duration bucket_size,
    absl::Span<const uint32_t> buckets_lt_cycle_duration)
    INTRINSIC_NON_REALTIME_ONLY;

// Inserts the buckets greater or equal than cycle duration into the performance
// metrics proto `perf_metrics`. The buckets are inserted as fields in the
// format:
//  key: "bucket_ge_cycle 00"
//  ...
//  key: "bucket_ge_cycle 10"
// Every bucket contains their interval and the number of entries.
google::protobuf::ListValue InsertGreaterEqualCycleDurationEntries(
    intrinsic_proto::performance::analysis::proto::PerformanceMetrics&
        perf_metrics,
    absl::Duration bucket_size, size_t num_buckets_lt_cycle_duration,
    absl::Span<const uint32_t> buckets_ge_cycle_duration)
    INTRINSIC_NON_REALTIME_ONLY;

}  // namespace metrics_internal

// Converts an instance of CycleTimeHistogram to a PerformanceMetrics proto for
// exporting.
// The proto contains the following fields:
//  - num_entries: The number of entries that were added to the histogram.
//  - num_entries_ge_cycle_duration: The number of durations greater or equal
//    the cycle duration that were added to the histogram.
//  - num_overruns: The number of durations that were longer than the 2x cycle
//    duration and were not stored in the histogram.
//  - max: The maximum duration that was added to the histogram.
//  - cycle_duration: The cycle period of the histogram.
//  - num_buckets_per_cycle_duration: The number of buckets that store the
//    histogram data for durations less than the cycle duration.
//    The details of one cycle are stored in this number of
//    buckets. Details for an additional cycle of durations are stored in the
//    same number of buckets.
//  - bucket_size: The size of each bucket in the histogram (cycle_duration /
//  num_buckets).
// The individual buckets are exported as fields named "bucket_lt_cycle
// <index>".
//
// The extra buckets for an additional cycle are exported as fields named
// "bucket_ge_cycle <index>".
template <const size_t kNumBuckets>
intrinsic_proto::performance::analysis::proto::PerformanceMetrics
ToPerformanceMetrics(absl::string_view metric_name,
                     const CycleTimeHistogram<kNumBuckets>& histogram)
    INTRINSIC_NON_REALTIME_ONLY {
  intrinsic_proto::performance::analysis::proto::PerformanceMetrics metrics;
  *metrics.mutable_metric_name() = metric_name;

  metrics_internal::InsertNumericField(metrics, "num_entries",
                                       histogram.NumEntries());
  metrics_internal::InsertNumericField(
      metrics, "num_entries_ge_cycle_duration",
      histogram.NumberOfEntriesGreaterEqualCycleDuration());
  metrics_internal::InsertNumericField(metrics, "num_overruns",
                                       histogram.NumberOfOverruns());
  metrics_internal::InsertDurationField(metrics, "max", histogram.Max());
  metrics_internal::InsertDurationField(metrics, "cycle_duration",
                                        histogram.CycleDuration());
  const auto buckets = histogram.BucketsLTCycleDuration();
  const size_t num_buckets_lt_cycle_duration = buckets.size();
  metrics_internal::InsertNumericField(
      metrics, "num_buckets_per_cycle_duration", num_buckets_lt_cycle_duration);
  const absl::Duration bucket_size =
      histogram.CycleDuration() / num_buckets_lt_cycle_duration;
  metrics_internal::InsertDurationField(metrics, "bucket_size", bucket_size);
  metrics_internal::InsertLessThanCycleDurationEntries(metrics, bucket_size,
                                                       buckets);
  metrics_internal::InsertGreaterEqualCycleDurationEntries(
      metrics, bucket_size, num_buckets_lt_cycle_duration,
      histogram.BucketsGECycleDuration());

  return metrics;
}

// Converts an instance of CycleTimeMetrics to a vector of PerformanceMetrics
// protos for exporting. See ToPerformanceMetrics above for details on the proto
// format.
std::vector<intrinsic_proto::performance::analysis::proto::PerformanceMetrics>
ToPerformanceMetrics(const CycleTimeMetrics& metrics)
    INTRINSIC_NON_REALTIME_ONLY;

}  // namespace intrinsic::icon

#endif  // INTRINSIC_ICON_UTILS_REALTIME_METRICS_H_
