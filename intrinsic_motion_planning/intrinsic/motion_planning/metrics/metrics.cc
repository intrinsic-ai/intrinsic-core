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

#include "intrinsic/motion_planning/metrics/metrics.h"

#include "intrinsic/stats/metrics_utils.h"
#include "opentelemetry/metrics/meter.h"
#include "opentelemetry/metrics/sync_instruments.h"

namespace intrinsic {
namespace motion_planning {

using ::opentelemetry::metrics::Histogram;

constexpr char kMPSPlanTrajectoryTimeNameDist[] =
    "intrinsic/motion_planning/mps_plan_trajectory_time_dist";
constexpr char kMPSPlanTrajectoryTimeDistDescription[] =
    "Distribution of time taken to plan a trajectory";

constexpr char kMilliseconds[] = "ms";

Histogram<double>& MPSPlanTrajectoryTimeDist() {
  // Intentionally release the instrument so it remains alive for the program's
  // lifetime, avoiding global destructor ordering errors on service
  // destruction. This one-time allocation is safe and won't cause memory leaks.
  static Histogram<double>* histogram = []() {
    // The view must be registered before the instrument is created.
    stats::RegisterHistogramView(
        kMPSPlanTrajectoryTimeNameDist, /*view_name=*/"",
        // Exponential spacing between 1 and 512 milliseconds
        // [1, 2, 4, ..., 512] milliseconds
        stats::ExponentialBucketBoundaries(/*num_finite_buckets=*/10,
                                           /*scale=*/1.0,
                                           /*growth_factor=*/2.0));
    return stats::GetMeter()
        ->CreateDoubleHistogram(kMPSPlanTrajectoryTimeNameDist,
                                kMPSPlanTrajectoryTimeDistDescription,
                                kMilliseconds)
        .release();
  }();
  return *histogram;
}

}  // namespace motion_planning
}  // namespace intrinsic
