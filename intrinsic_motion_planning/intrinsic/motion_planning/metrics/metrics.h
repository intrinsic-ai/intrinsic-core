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

#ifndef INTRINSIC_MOTION_PLANNING_METRICS_METRICS_H_
#define INTRINSIC_MOTION_PLANNING_METRICS_METRICS_H_

#include "absl/strings/string_view.h"
#include "opentelemetry/metrics/sync_instruments.h"

namespace intrinsic {
namespace motion_planning {

inline constexpr absl::string_view kCacheHitResultKey = "cache_hit_result";
inline constexpr absl::string_view kCallerIdKey = "caller_id";

// Distribution of time spent in PlanTrajectory (ms). The first call registers
// the histogram view, so it must happen after intrinsic::OpenCensusPlugin is
// constructed.
opentelemetry::metrics::Histogram<double>& MPSPlanTrajectoryTimeDist();

}  // namespace motion_planning
}  // namespace intrinsic

#endif  // INTRINSIC_MOTION_PLANNING_METRICS_METRICS_H_
