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

// Defines the prometheus metrics to be exported by the Executive.
// The views are statically registered when InitGoogle runs.
#ifndef INTRINSIC_EXECUTIVE_ENGINE_CLIPS_METRICS_H_
#define INTRINSIC_EXECUTIVE_ENGINE_CLIPS_METRICS_H_

#include <cstdint>

#include "opentelemetry/metrics/sync_instruments.h"

namespace intrinsic {
namespace executive {

// A histogram of the number of rules invoked by CLIPS.
opentelemetry::metrics::Histogram<uint64_t>& RuleInvocationsMeasure();
opentelemetry::metrics::Histogram<double>& ClipsRunTimeMeasure();

void RegisterExecutiveMetrics();

}  // namespace executive
}  // namespace intrinsic

#endif  // INTRINSIC_EXECUTIVE_ENGINE_CLIPS_METRICS_H_
