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

#include "intrinsic/platform/pubsub/kvstore_grpc/metrics.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "intrinsic/stats/metrics_utils.h"
#include "opentelemetry/common/attribute_value.h"
#include "opentelemetry/metrics/meter.h"
#include "opentelemetry/metrics/sync_instruments.h"

namespace intrinsic::kvstore {

using ::opentelemetry::metrics::Counter;
using ::opentelemetry::metrics::Histogram;

constexpr char kNanoSeconds[] = "ns";
constexpr char kBytes[] = "bytes";

// Get

constexpr char kGetRequestsReceivedName[] =
    "intrinsic/kvstore_grpc/get_requests";
constexpr char kGetRequestsReceivedDescription[] = "Number of Get requests";

constexpr char kGetDurationName[] = "intrinsic/kvstore_grpc/get_duration";
constexpr char kGetDurationDescription[] =
    "Time spent in KVStore Get request in nanoseconds";

// Instruments in this file are intentionally released so they remain alive
// for the program's lifetime, avoiding global destructor ordering errors on
// service destruction. Each is a one-time allocation, so this doesn't leak.
Counter<uint64_t>& GetMetricsScope::RequestsReceived() {
  static Counter<uint64_t>* counter =
      stats::GetMeter()
          ->CreateUInt64Counter(kGetRequestsReceivedName,
                                kGetRequestsReceivedDescription, "1")
          .release();
  return *counter;
}

Histogram<uint64_t>& GetMetricsScope::DurationNanos() {
  static Histogram<uint64_t>* histogram =
      stats::GetMeter()
          ->CreateUInt64Histogram(kGetDurationName, kGetDurationDescription,
                                  kNanoSeconds)
          .release();
  return *histogram;
}

GetMetricsScope::~GetMetricsScope() {
  const std::string status_str = absl::StatusCodeToString(status.code());
  const std::array<
      std::pair<absl::string_view, opentelemetry::common::AttributeValue>, 1>
      attributes = {{{kStatusCodeKey, status_str}}};
  const uint64_t duration_ns = absl::ToInt64Nanoseconds(
      std::max(absl::ZeroDuration(), absl::Now() - start_time));

  RequestsReceived().Add(1, attributes);
  DurationNanos().Record(duration_ns, attributes);
}

void GetMetricsScope::RegisterViews() {
  stats::RegisterHistogramView(kGetDurationName, /*view_name=*/"",
                               stats::ExponentialBucketBoundaries(
                                   /*num_finite_buckets=*/15, /*scale=*/50000,
                                   /*growth_factor=*/2.0));

  // Eagerly initialize the instruments to register them.
  RequestsReceived();
  DurationNanos();
}

// Set

constexpr char kSetRequestsReceivedName[] =
    "intrinsic/kvstore_grpc/set_requests";
constexpr char kSetRequestsReceivedDescription[] = "Number of Set requests";

constexpr char kSetDurationName[] = "intrinsic/kvstore_grpc/set_duration";
constexpr char kSetDurationDescription[] =
    "Time spent in KVStore Set request in nanoseconds";

constexpr char kSetPayloadSizeBytesName[] =
    "intrinsic/kvstore_grpc/set_payload_size_bytes";
constexpr char kSetPayloadSizeBytesDescription[] =
    "Size of KVStore Set request payload in bytes";

Counter<uint64_t>& SetMetricsScope::RequestsReceived() {
  static Counter<uint64_t>* counter =
      stats::GetMeter()
          ->CreateUInt64Counter(kSetRequestsReceivedName,
                                kSetRequestsReceivedDescription, "1")
          .release();
  return *counter;
}

Histogram<uint64_t>& SetMetricsScope::DurationNanos() {
  static Histogram<uint64_t>* histogram =
      stats::GetMeter()
          ->CreateUInt64Histogram(kSetDurationName, kSetDurationDescription,
                                  kNanoSeconds)
          .release();
  return *histogram;
}

Histogram<uint64_t>& SetMetricsScope::PayloadSizeBytes() {
  static Histogram<uint64_t>* histogram =
      stats::GetMeter()
          ->CreateUInt64Histogram(kSetPayloadSizeBytesName,
                                  kSetPayloadSizeBytesDescription, kBytes)
          .release();
  return *histogram;
}

SetMetricsScope::~SetMetricsScope() {
  const std::string status_str = absl::StatusCodeToString(status.code());
  const absl::string_view high_consistency_str =
      high_consistency ? "true" : "false";
  const std::array<
      std::pair<absl::string_view, opentelemetry::common::AttributeValue>, 2>
      attributes = {{{kStatusCodeKey, status_str},
                     {kHighConsistencyKey, high_consistency_str}}};
  const uint64_t duration_ns = absl::ToInt64Nanoseconds(
      std::max(absl::ZeroDuration(), absl::Now() - start_time));

  RequestsReceived().Add(1, attributes);
  DurationNanos().Record(duration_ns, attributes);

  if (payload_bytes > 0) {
    PayloadSizeBytes().Record(static_cast<uint64_t>(payload_bytes), attributes);
  }
}

void SetMetricsScope::RegisterViews() {
  stats::RegisterHistogramView(kSetDurationName, /*view_name=*/"",
                               stats::ExponentialBucketBoundaries(
                                   /*num_finite_buckets=*/15, /*scale=*/50000,
                                   /*growth_factor=*/2.0));
  stats::RegisterHistogramView(kSetPayloadSizeBytesName, /*view_name=*/"",
                               stats::ExponentialBucketBoundaries(
                                   /*num_finite_buckets=*/20, /*scale=*/100,
                                   /*growth_factor=*/2.0));

  // Eagerly initialize the instruments to register them.
  RequestsReceived();
  DurationNanos();
  PayloadSizeBytes();
}

// Delete

constexpr char kDeleteRequestsReceivedName[] =
    "intrinsic/kvstore_grpc/delete_requests";
constexpr char kDeleteRequestsReceivedDescription[] =
    "Number of Delete requests";

constexpr char kDeleteDurationName[] = "intrinsic/kvstore_grpc/delete_duration";
constexpr char kDeleteDurationDescription[] =
    "Time spent in KVStore Delete request in nanoseconds";

Counter<uint64_t>& DeleteMetricsScope::RequestsReceived() {
  static Counter<uint64_t>* counter =
      stats::GetMeter()
          ->CreateUInt64Counter(kDeleteRequestsReceivedName,
                                kDeleteRequestsReceivedDescription, "1")
          .release();
  return *counter;
}

Histogram<uint64_t>& DeleteMetricsScope::DurationNanos() {
  static Histogram<uint64_t>* histogram =
      stats::GetMeter()
          ->CreateUInt64Histogram(kDeleteDurationName,
                                  kDeleteDurationDescription, kNanoSeconds)
          .release();
  return *histogram;
}

DeleteMetricsScope::~DeleteMetricsScope() {
  const std::string status_str = absl::StatusCodeToString(status.code());
  const std::array<
      std::pair<absl::string_view, opentelemetry::common::AttributeValue>, 1>
      attributes = {{{kStatusCodeKey, status_str}}};
  const uint64_t duration_ns = absl::ToInt64Nanoseconds(
      std::max(absl::ZeroDuration(), absl::Now() - start_time));

  RequestsReceived().Add(1, attributes);
  DurationNanos().Record(duration_ns, attributes);
}

void DeleteMetricsScope::RegisterViews() {
  stats::RegisterHistogramView(kDeleteDurationName, /*view_name=*/"",
                               stats::ExponentialBucketBoundaries(
                                   /*num_finite_buckets=*/15, /*scale=*/50000,
                                   /*growth_factor=*/2.0));

  // Eagerly initialize the instruments to register them.
  RequestsReceived();
  DurationNanos();
}

// List

constexpr char kListRequestsReceivedName[] =
    "intrinsic/kvstore_grpc/list_requests";
constexpr char kListRequestsReceivedDescription[] = "Number of List requests";

constexpr char kListDurationName[] = "intrinsic/kvstore_grpc/list_duration";
constexpr char kListDurationDescription[] =
    "Time spent in KVStore List request in nanoseconds";

Counter<uint64_t>& ListMetricsScope::RequestsReceived() {
  static Counter<uint64_t>* counter =
      stats::GetMeter()
          ->CreateUInt64Counter(kListRequestsReceivedName,
                                kListRequestsReceivedDescription, "1")
          .release();
  return *counter;
}

Histogram<uint64_t>& ListMetricsScope::DurationNanos() {
  static Histogram<uint64_t>* histogram =
      stats::GetMeter()
          ->CreateUInt64Histogram(kListDurationName, kListDurationDescription,
                                  kNanoSeconds)
          .release();
  return *histogram;
}

ListMetricsScope::~ListMetricsScope() {
  const std::string status_str = absl::StatusCodeToString(status.code());
  const std::array<
      std::pair<absl::string_view, opentelemetry::common::AttributeValue>, 1>
      attributes = {{{kStatusCodeKey, status_str}}};
  const uint64_t duration_ns = absl::ToInt64Nanoseconds(
      std::max(absl::ZeroDuration(), absl::Now() - start_time));

  RequestsReceived().Add(1, attributes);
  DurationNanos().Record(duration_ns, attributes);
}

void ListMetricsScope::RegisterViews() {
  stats::RegisterHistogramView(kListDurationName, /*view_name=*/"",
                               stats::ExponentialBucketBoundaries(
                                   /*num_finite_buckets=*/15, /*scale=*/50000,
                                   /*growth_factor=*/2.0));

  // Eagerly initialize the instruments to register them.
  RequestsReceived();
  DurationNanos();
}

void RegisterKvstoreGrpcMetrics() {
  GetMetricsScope::RegisterViews();
  SetMetricsScope::RegisterViews();
  DeleteMetricsScope::RegisterViews();
  ListMetricsScope::RegisterViews();
}

}  // namespace intrinsic::kvstore
