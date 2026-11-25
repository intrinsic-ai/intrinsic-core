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

#include "intrinsic/executive/engine/clips_metrics.h"

#include <cstdint>
#include <memory>
#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "intrinsic/stats/metrics_utils.h"
#include "opentelemetry/metrics/meter.h"
#include "opentelemetry/metrics/sync_instruments.h"

namespace intrinsic {
namespace executive {

using ::opentelemetry::metrics::Histogram;

// The units of the measure. A "1" indicates the measure is unitless.
constexpr const char* kUnitsCount = "1";
constexpr const char* kRuleInvocationsMeasureName =
    "intrinsic/executive/rule_invocations";

constexpr const char* kClipsRunTimeUnit = "ms";
constexpr const char* kClipsRunTimeName = "intrinsic/executive/clips_run_time";

Histogram<uint64_t>& RuleInvocationsMeasure() {
  // Intentionally release the instrument so it remains alive for the program's
  // lifetime, avoiding global destructor ordering errors on service
  // destruction. This one-time allocation is safe and won't cause memory leaks.
  static Histogram<uint64_t>* histogram =
      stats::GetMeter()
          ->CreateUInt64Histogram(kRuleInvocationsMeasureName,
                                  "How many rules were invoked.", kUnitsCount)
          .release();
  return *histogram;
}

Histogram<double>& ClipsRunTimeMeasure() {
  static Histogram<double>* histogram =
      stats::GetMeter()
          ->CreateDoubleHistogram(kClipsRunTimeName, "Time of a CLIPS run.",
                                  kClipsRunTimeUnit)
          .release();
  return *histogram;
}

void RegisterExecutiveMetrics() {
  stats::RegisterHistogramView(
      kRuleInvocationsMeasureName,
      absl::StrCat(kRuleInvocationsMeasureName, "/distribution"),
      {1.0, 2.0, 5.0, 10.0, 25.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0});
  RuleInvocationsMeasure();

  stats::RegisterHistogramView(kClipsRunTimeName,
                               absl::StrCat(kClipsRunTimeName, "/distribution"),
                               {1.0, 2.0, 5.0, 10.0, 20.0, 30.0, 40.0, 50.0,
                                100.0, 200.0, 500.0, 1000.0, 2000.0});
  ClipsRunTimeMeasure();
}

}  // namespace executive
}  // namespace intrinsic
