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

#ifndef INTRINSIC_PLATFORM_PUBSUB_INTROSPECTION_METRICS_H_
#define INTRINSIC_PLATFORM_PUBSUB_INTROSPECTION_METRICS_H_

#include <cstdint>

#include "absl/strings/string_view.h"

namespace intrinsic::zenoh_introspection {

// Eagerly initializes the Zenoh introspection metrics. Must be called in main()
// right after intrinsic::OpenCensusPlugin is constructed and before any Record*
// call, otherwise the instruments bind to the no-op meter.
void RegisterZenohIntrospectionMetrics();

// Records the total number of bytes and messages seen on `topic_name` by the
// Zenoh session on `host_name`. `direction` is "pub" or "sub".
void RecordTopicMetrics(absl::string_view direction,
                        absl::string_view topic_name,
                        absl::string_view host_name, int64_t bytes,
                        int64_t messages);

// Records the number of publishers in the Zenoh session on `host_name`.
void RecordNumPublishers(absl::string_view host_name, int64_t num_publishers);

// Records the number of subscribers in the Zenoh session on `host_name`.
void RecordNumSubscribers(absl::string_view host_name, int64_t num_subscribers);

}  // namespace intrinsic::zenoh_introspection

#endif  // INTRINSIC_PLATFORM_PUBSUB_INTROSPECTION_METRICS_H_
