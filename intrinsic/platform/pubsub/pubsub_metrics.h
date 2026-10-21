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

#ifndef INTRINSIC_PLATFORM_PUBSUB_PUBSUB_METRICS_H_
#define INTRINSIC_PLATFORM_PUBSUB_PUBSUB_METRICS_H_

#include <cstdint>
#include <string>

#include "absl/strings/string_view.h"
#include "opentelemetry/metrics/sync_instruments.h"

namespace intrinsic {

// A label to break down metrics by the PubSub topic.
inline constexpr char kTopicKey[] = "topic";

// Time spent in the PubSub adapter's Publish call. This does not measure the
// time it takes for a message to arrive at the subscriber.
opentelemetry::metrics::Histogram<uint64_t>& PublishLatencyMetric();

// Truncates the topic name to a maximum length of 15 characters.
// This is to avoid high cardinality metrics.
std::string TruncateTopicName(absl::string_view topic);

}  // namespace intrinsic

#endif  // INTRINSIC_PLATFORM_PUBSUB_PUBSUB_METRICS_H_
