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

#include "intrinsic/stats/metrics_utils.h"

#include <memory>
#include <string>
#include <vector>

#include "absl/strings/string_view.h"
#include "opentelemetry/metrics/provider.h"
#include "opentelemetry/sdk/metrics/aggregation/aggregation_config.h"
#include "opentelemetry/sdk/metrics/meter_provider.h"
#include "opentelemetry/sdk/metrics/view/instrument_selector_factory.h"
#include "opentelemetry/sdk/metrics/view/meter_selector_factory.h"
#include "opentelemetry/sdk/metrics/view/view_factory.h"

namespace intrinsic::stats {

constexpr char kMeterName[] = "intrinsic-c++";

namespace metrics_api = opentelemetry::metrics;
namespace metrics_sdk = opentelemetry::sdk::metrics;

std::shared_ptr<metrics_api::Meter> GetMeter() {
  return metrics_api::Provider::GetMeterProvider()->GetMeter(kMeterName);
}

void RegisterHistogramView(absl::string_view instrument_name,
                           absl::string_view view_name,
                           absl::Span<const double> boundaries) {
  std::shared_ptr<opentelemetry::metrics::MeterProvider> api_provider =
      metrics_api::Provider::GetMeterProvider();

  auto sdk_provider =
      std::dynamic_pointer_cast<metrics_sdk::MeterProvider>(api_provider);
  if (!sdk_provider) {
    return;
  }

  std::unique_ptr<metrics_sdk::InstrumentSelector> selector =
      metrics_sdk::InstrumentSelectorFactory::Create(
          metrics_sdk::InstrumentType::kHistogram,
          /*name=*/std::string(instrument_name),
          /*unit=*/"");

  std::unique_ptr<metrics_sdk::MeterSelector> meter_selector =
      metrics_sdk::MeterSelectorFactory::Create(
          /*name=*/kMeterName, /*version=*/"", /*schema=*/"");

  auto config = std::make_shared<metrics_sdk::HistogramAggregationConfig>();
  std::vector<double> otel_boundaries(boundaries.begin(), boundaries.end());
  config->boundaries_ = std::move(otel_boundaries);

  std::unique_ptr<metrics_sdk::View> view = metrics_sdk::ViewFactory::Create(
      /*name=*/std::string(view_name), /*description=*/"",
      metrics_sdk::AggregationType::kHistogram, config);

  sdk_provider->AddView(std::move(selector), std::move(meter_selector),
                        std::move(view));
}

// Bucket boundaries helpers copied from the OpenCensus:
// https://github.com/census-instrumentation/opencensus-cpp/blob/master/opencensus/stats/internal/bucket_boundaries.cc

std::vector<double> LinearBucketBoundaries(int num_finite_buckets,
                                           double offset, double width) {
  std::vector<double> boundaries(num_finite_buckets + 1);

  double boundary = offset;
  for (int i = 0; i <= num_finite_buckets; ++i) {
    boundaries[i] = boundary;
    boundary += width;
  }
  return boundaries;
}

std::vector<double> ExponentialBucketBoundaries(int num_finite_buckets,
                                                double scale,
                                                double growth_factor) {
  std::vector<double> boundaries(num_finite_buckets + 1);
  boundaries[0] = 0.0;

  double upper_bound = scale;
  for (int i = 1; i <= num_finite_buckets; ++i) {
    boundaries[i] = upper_bound;
    upper_bound *= growth_factor;
  }
  return boundaries;
}

}  // namespace intrinsic::stats
