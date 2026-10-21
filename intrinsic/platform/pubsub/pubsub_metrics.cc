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

#include "intrinsic/platform/pubsub/pubsub_metrics.h"

#include <cstdint>
#include <string>

#include "absl/strings/string_view.h"
#include "intrinsic/stats/metrics_utils.h"
#include "opentelemetry/metrics/meter.h"
#include "opentelemetry/metrics/sync_instruments.h"

namespace intrinsic {

namespace {

using ::opentelemetry::metrics::Histogram;

constexpr char kMicrosecondsUnit[] = "us";
constexpr char kPublishLatencyName[] = "intrinsic/pubsub/publish_latency";
constexpr char kPublishLatencyDescription[] =
    "Time in us spent in the Publish call";
constexpr int kMaxTopicNameLength = 15;

}  // namespace

Histogram<uint64_t>& PublishLatencyMetric() {
  // Intentionally release the instrument so it remains alive for the program's
  // lifetime, avoiding global destructor ordering errors on service
  // destruction. This one-time allocation is safe and won't cause memory leaks.
  static Histogram<uint64_t>* metric = []() {
    stats::RegisterHistogramView(kPublishLatencyName, /*view_name=*/"",
                                 stats::ExponentialBucketBoundaries(
                                     /*num_finite_buckets=*/5, /*scale=*/10,
                                     /*growth_factor=*/3.0));
    return stats::GetMeter()
        ->CreateUInt64Histogram(kPublishLatencyName, kPublishLatencyDescription,
                                kMicrosecondsUnit)
        .release();
  }();
  return *metric;
}

std::string TruncateTopicName(absl::string_view topic) {
  if (topic.size() > kMaxTopicNameLength) {
    return std::string(topic.substr(0, kMaxTopicNameLength));
  }
  return std::string(topic);
}

}  // namespace intrinsic
