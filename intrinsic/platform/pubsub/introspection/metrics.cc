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

#include "intrinsic/platform/pubsub/introspection/metrics.h"

#include <array>
#include <cstdint>
#include <utility>

#include "absl/strings/string_view.h"
#include "intrinsic/stats/metrics_utils.h"
#include "opentelemetry/common/attribute_value.h"
#include "opentelemetry/metrics/meter.h"
#include "opentelemetry/metrics/sync_instruments.h"

namespace intrinsic::zenoh_introspection {

namespace {

using ::opentelemetry::metrics::Gauge;

constexpr char kPubSubTopicBytesName[] = "intrinsic/pubsub/topic_bytes";
constexpr char kPubSubTopicBytesDesc[] = "Total number of bytes";

constexpr char kPubSubTopicMessagesName[] = "intrinsic/pubsub/topic_messages";
constexpr char kPubSubTopicMessagesDesc[] = "Total number of messages";

constexpr char kNumPublishersName[] = "intrinsic/pubsub/num_publishers";
constexpr char kNumPublishersDesc[] =
    "Number of publishers in each Zenoh session";

constexpr char kNumSubscribersName[] = "intrinsic/pubsub/num_subscribers";
constexpr char kNumSubscribersDesc[] =
    "Number of subscribers in each Zenoh session";

constexpr absl::string_view kPubSubDirectionNameKey = "direction";
constexpr absl::string_view kPubSubTopicNameKey = "topic_name";
constexpr absl::string_view kPubSubHostNameKey = "host_name";

// Instruments in this file are intentionally released so they remain alive
// for the program's lifetime, avoiding global destructor ordering errors on
// service destruction. Each is a one-time allocation, so this doesn't leak.
Gauge<int64_t>& PubSubTopicBytes() {
  static Gauge<int64_t>* gauge =
      stats::GetMeter()
          ->CreateInt64Gauge(kPubSubTopicBytesName, kPubSubTopicBytesDesc, "1")
          .release();
  return *gauge;
}

Gauge<int64_t>& PubSubTopicMessages() {
  static Gauge<int64_t>* gauge =
      stats::GetMeter()
          ->CreateInt64Gauge(kPubSubTopicMessagesName, kPubSubTopicMessagesDesc,
                             "1")
          .release();
  return *gauge;
}

Gauge<int64_t>& PubSubNumPublishers() {
  static Gauge<int64_t>* gauge =
      stats::GetMeter()
          ->CreateInt64Gauge(kNumPublishersName, kNumPublishersDesc, "1")
          .release();
  return *gauge;
}

Gauge<int64_t>& PubSubNumSubscribers() {
  static Gauge<int64_t>* gauge =
      stats::GetMeter()
          ->CreateInt64Gauge(kNumSubscribersName, kNumSubscribersDesc, "1")
          .release();
  return *gauge;
}

}  // namespace

void RegisterZenohIntrospectionMetrics() {
  // Eagerly initialize the instruments to register them.
  PubSubTopicBytes();
  PubSubTopicMessages();
  PubSubNumPublishers();
  PubSubNumSubscribers();
}

void RecordTopicMetrics(absl::string_view direction,
                        absl::string_view topic_name,
                        absl::string_view host_name, int64_t bytes,
                        int64_t messages) {
  const std::array<
      std::pair<absl::string_view, opentelemetry::common::AttributeValue>, 3>
      attributes = {{{kPubSubDirectionNameKey, direction},
                     {kPubSubTopicNameKey, topic_name},
                     {kPubSubHostNameKey, host_name}}};
  PubSubTopicBytes().Record(bytes, attributes);
  PubSubTopicMessages().Record(messages, attributes);
}

void RecordNumPublishers(absl::string_view host_name, int64_t num_publishers) {
  PubSubNumPublishers().Record(num_publishers,
                               {{kPubSubHostNameKey, host_name}});
}

void RecordNumSubscribers(absl::string_view host_name,
                          int64_t num_subscribers) {
  PubSubNumSubscribers().Record(num_subscribers,
                                {{kPubSubHostNameKey, host_name}});
}

}  // namespace intrinsic::zenoh_introspection
