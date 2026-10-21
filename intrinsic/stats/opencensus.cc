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

#include "intrinsic/stats/opencensus.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "google/protobuf/descriptor.h"
#include "google/rpc/code.pb.h"
#include "grpcpp/ext/otel_plugin.h"
#include "grpcpp/support/interceptor.h"
#include "grpcpp/support/server_interceptor.h"
#include "intrinsic/stats/internal_constants.h"
#include "intrinsic/stats/metrics_utils.h"
#include "intrinsic/stats/opencensus-internal.h"
#include "opencensus/exporters/stats/prometheus/prometheus_exporter.h"
#include "opencensus/stats/stats.h"
#include "opencensus/tags/tag_key.h"
#include "opentelemetry/context/propagation/composite_propagator.h"
#include "opentelemetry/exporters/otlp/otlp_grpc_exporter_factory.h"
#include "opentelemetry/exporters/otlp/otlp_grpc_exporter_options.h"
#include "opentelemetry/exporters/prometheus/exporter_factory.h"
#include "opentelemetry/exporters/prometheus/exporter_options.h"
#include "opentelemetry/metrics/provider.h"
#include "opentelemetry/sdk/metrics/aggregation/aggregation_config.h"
#include "opentelemetry/sdk/metrics/meter_provider.h"
#include "opentelemetry/sdk/metrics/meter_provider_factory.h"
#include "opentelemetry/sdk/metrics/view/instrument_selector_factory.h"
#include "opentelemetry/sdk/metrics/view/meter_selector_factory.h"
#include "opentelemetry/sdk/metrics/view/view_factory.h"
#include "opentelemetry/sdk/resource/resource.h"
#include "opentelemetry/sdk/trace/batch_span_processor_factory.h"
#include "opentelemetry/sdk/trace/batch_span_processor_options.h"
#include "opentelemetry/sdk/trace/sampler.h"
#include "opentelemetry/sdk/trace/samplers/always_off.h"
#include "opentelemetry/sdk/trace/samplers/parent.h"
#include "opentelemetry/sdk/trace/tracer_provider_factory.h"
#include "opentelemetry/semconv/service_attributes.h"
#include "opentelemetry/trace/propagation/http_trace_context.h"
#include "opentelemetry/trace/provider.h"
#include "prometheus/exposer.h"

ABSL_FLAG(int, opencensus_metrics_port, 0,
          "Which port to expose Prometheus metrics on.");
ABSL_FLAG(bool, opencensus_tracing, false,
          "Whether to send traces to opentelemetry.");
ABSL_FLAG(std::string, otlp_tracing_endpoint,
          "oc-agent.app-intrinsic-base.svc.cluster.local:4317",
          "OTLP gRPC collector endpoint for traces.");

namespace intrinsic {
namespace {

using ::opencensus::exporters::stats::PrometheusExporter;
using ::opencensus::stats::Aggregation;
using ::opencensus::stats::BucketBoundaries;
using ::opencensus::stats::MeasureInt64;
using ::opencensus::stats::ViewDescriptor;
using ::opencensus::tags::TagKey;

constexpr char kRpcCountName[] = "intrinsic/stats/rpc_count";
constexpr char kRpcCountDescription[] =
    "Number of completed RPCs, tagged by method and status";
constexpr char kUnits[] = "1";

constexpr char kMethodKey[] = "method";
constexpr char kStatusKey[] = "status";

MeasureInt64 RpcCount() {
  static const auto measure =
      MeasureInt64::Register(kRpcCountName, kRpcCountDescription, kUnits);
  return measure;
}

TagKey MethodKey() {
  static const auto key = TagKey::Register(kMethodKey);
  return key;
}

TagKey StatusKey() {
  static const auto key = TagKey::Register(kStatusKey);
  return key;
}

opentelemetry::metrics::Counter<uint64_t>& OtelRpcCount() {
  static opentelemetry::metrics::Counter<uint64_t>* counter =
      stats::GetMeter()
          ->CreateUInt64Counter(kRpcCountName, kRpcCountDescription, kUnits)
          .release();
  return *counter;
}
class OpenCensusInterceptor : public ::grpc::experimental::Interceptor {
 public:
  // Does not take ownership of info, which is owned by the gRPC framework and
  // must outlive the interceptor.
  explicit OpenCensusInterceptor(::grpc::experimental::ServerRpcInfo* info,
                                 bool use_otel_metrics)
      : info_(info), use_otel_metrics_(use_otel_metrics) {}

  void Intercept(
      ::grpc::experimental::InterceptorBatchMethods* methods) override {
    if (methods->QueryInterceptionHookPoint(
            ::grpc::experimental::InterceptionHookPoints::PRE_SEND_STATUS)) {
      // Strip the leading / for consistency with other metrics.
      absl::string_view method = absl::StripPrefix(info_->method(), "/");
      std::string status =
          ::google::rpc::Code_Name(static_cast<::google::rpc::Code>(
              methods->GetSendStatus().error_code()));

      if (use_otel_metrics_) {
        OtelRpcCount().Add(1, {{kMethodKey, method}, {kStatusKey, status}});
      } else {
        ::opencensus::stats::Record(
            {{RpcCount(), 1}}, {{MethodKey(), method}, {StatusKey(), status}});
      }
    }
    methods->Proceed();
  }

 private:
  const ::grpc::experimental::ServerRpcInfo* info_;
  bool use_otel_metrics_;
};

}  // namespace

namespace internal {

// Exposed for testing.
ViewDescriptor RpcCountViewDescriptor() {
  // The metric must be registered before the view can be created.
  RpcCount();
  // Use Sum() aggregation instead of Count() so that we can initialize it to
  // zero.
  return ViewDescriptor()
      .set_name(kRpcCountName)
      .set_measure(kRpcCountName)
      .set_description(kRpcCountDescription)
      .set_aggregation(Aggregation::Sum())
      .add_column(MethodKey())
      .add_column(StatusKey());
}

InPlaceSampler::InPlaceSampler(
    std::shared_ptr<opentelemetry::sdk::trace::Sampler> default_sampler)
    : default_sampler_(default_sampler),
      description_("InPlaceSampler{" +
                   std::string{default_sampler->GetDescription()} + "}") {}

opentelemetry::sdk::trace::SamplingResult InPlaceSampler::ShouldSample(
    const opentelemetry::trace::SpanContext& parent_context,
    opentelemetry::trace::TraceId trace_id, std::string_view name,
    opentelemetry::trace::SpanKind span_kind,
    const opentelemetry::common::KeyValueIterable& attributes,
    const opentelemetry::trace::SpanContextKeyValueIterable& links) noexcept {
  opentelemetry::sdk::trace::Decision override_decision =
      opentelemetry::sdk::trace::Decision::DROP;
  bool has_override = false;

  attributes.ForEachKeyValue(
      [&](std::string_view key,
          const opentelemetry::common::AttributeValue& value) {
        if (key == stats::kSamplingOverrideAttributeKey) {
          if (auto pval = std::get_if<bool>(&value)) {
            override_decision =
                *pval ? opentelemetry::sdk::trace::Decision::RECORD_AND_SAMPLE
                      : opentelemetry::sdk::trace::Decision::DROP;
            has_override = true;
            return false;
          }
        }
        return true;
      });

  if (has_override) {
    if (!parent_context.IsValid()) {
      return {override_decision, /*attributes=*/nullptr,
              /*trace_state=*/opentelemetry::trace::TraceState::GetDefault()};
    }
    return {override_decision, /*attributes=*/nullptr,
            /*trace_state=*/parent_context.trace_state()};
  }

  return default_sampler_->ShouldSample(parent_context, trace_id, name,
                                        span_kind, attributes, links);
}

std::string_view InPlaceSampler::GetDescription() const noexcept {
  return description_;
}

}  // namespace internal

// How long to wait for traces to be uploaded. Each binary uploads its own
// traces in batch.
constexpr absl::Duration kTraceUploadTimeout = absl::Seconds(5);

// Custom buckets for gRPC latency, since the default ones were too granular.
std::shared_ptr<opentelemetry::sdk::metrics::HistogramAggregationConfig>
GrpcLatencyBucketsConfig() {
  auto latency_config = std::make_shared<
      opentelemetry::sdk::metrics::HistogramAggregationConfig>();
  latency_config->boundaries_ = {0.0,   0.0001, 0.0002, 0.0005, 0.001,
                                 0.002, 0.003,  0.005,  0.01,   0.015,
                                 0.02,  0.05,   0.1,    1.0,    10.0};
  return latency_config;
}

OpenCensusPlugin::OpenCensusPlugin(absl::string_view service_name,
                                   bool use_otel_metrics, bool initialize_sdk)
    : prometheus_exporter_(std::make_shared<PrometheusExporter>()) {
  int port = absl::GetFlag(FLAGS_opencensus_metrics_port);
  bool enable_tracing = absl::GetFlag(FLAGS_opencensus_tracing);
  if (port <= 0 && !enable_tracing) {
    return;
  }

  auto resource = opentelemetry::sdk::resource::Resource::Create(
      {{opentelemetry::semconv::service::kServiceName,
        std::string(service_name)}});

  // If no port is specified, don't create an http server since this will fail
  // on forge.
  if (port > 0) {
    if (use_otel_metrics) {
      LOG(INFO) << "Enabling OpenTelemetry metrics exposer on port " << port;
      opentelemetry::exporter::metrics::PrometheusExporterOptions otel_opts;
      otel_opts.url = absl::StrCat("0.0.0.0:", port);
      std::unique_ptr<opentelemetry::sdk::metrics::MetricReader> otel_exporter =
          opentelemetry::exporter::metrics::PrometheusExporterFactory::Create(
              otel_opts);

      auto meter_provider =
          std::make_shared<opentelemetry::sdk::metrics::MeterProvider>();

      std::unique_ptr<opentelemetry::sdk::metrics::InstrumentSelector>
          latency_selector =
              opentelemetry::sdk::metrics::InstrumentSelectorFactory::Create(
                  opentelemetry::sdk::metrics::InstrumentType::kHistogram,
                  /*name=*/"grpc.*.*.duration", /*unit=*/"");

      std::unique_ptr<opentelemetry::sdk::metrics::MeterSelector>
          meter_selector =
              opentelemetry::sdk::metrics::MeterSelectorFactory::Create(
                  /*name=*/"grpc-c++", /*version=*/"", /*schema=*/"");

      std::unique_ptr<opentelemetry::sdk::metrics::View> latency_view =
          opentelemetry::sdk::metrics::ViewFactory::Create(
              /*name=*/"", /*description=*/"",
              opentelemetry::sdk::metrics::AggregationType::kHistogram,
              GrpcLatencyBucketsConfig());

      meter_provider->AddView(std::move(latency_selector),
                              std::move(meter_selector),
                              std::move(latency_view));

      meter_provider->AddMetricReader(std::move(otel_exporter));
      opentelemetry::metrics::Provider::SetMeterProvider(
          std::move(meter_provider));
    } else {
      LOG(INFO) << "Enabling OpenCensus metrics exposer.";
      prometheus_exposer_ =
          std::make_unique<prometheus::Exposer>(absl::StrCat("0.0.0.0:", port));

      prometheus_exposer_->RegisterCollectable(prometheus_exporter_);
    }
  }

  if (initialize_sdk && enable_tracing) {
    LOG(INFO) << "Enabling OpenTelemetry tracer provider.";

    opentelemetry::exporter::otlp::OtlpGrpcExporterOptions exporter_options;
    exporter_options.endpoint = absl::GetFlag(FLAGS_otlp_tracing_endpoint);
    exporter_options.use_ssl_credentials = false;
    exporter_options.timeout = absl::ToChronoSeconds(kTraceUploadTimeout);

    auto otlp_exporter =
        opentelemetry::exporter::otlp::OtlpGrpcExporterFactory::Create(
            exporter_options);

    opentelemetry::sdk::trace::BatchSpanProcessorOptions processor_options;
    auto processor =
        opentelemetry::sdk::trace::BatchSpanProcessorFactory::Create(
            std::move(otlp_exporter), processor_options);
    // Use ParentBasedSampler with AlwaysOff as root to ensure we only sample
    // spans if the parent context (e.g., a calling service) was sampled,
    // preventing unsolicited root spans from generating telemetry overhead.
    auto always_off_sampler =
        std::make_shared<opentelemetry::sdk::trace::AlwaysOffSampler>();
    auto parent_based_sampler =
        std::make_shared<opentelemetry::sdk::trace::ParentBasedSampler>(
            always_off_sampler);
    auto sampler =
        std::make_unique<internal::InPlaceSampler>(parent_based_sampler);

    auto tracer_provider =
        opentelemetry::sdk::trace::TracerProviderFactory::Create(
            std::move(processor), resource, std::move(sampler));
    opentelemetry::trace::Provider::SetTracerProvider(
        std::move(tracer_provider));
  }

  if (enable_tracing || (port > 0 && use_otel_metrics)) {
    std::vector<
        std::unique_ptr<opentelemetry::context::propagation::TextMapPropagator>>
        propagators;

    propagators.push_back(
        grpc::OpenTelemetryPluginBuilder::MakeGrpcTraceBinTextMapPropagator());
    propagators.push_back(
        std::make_unique<
            opentelemetry::trace::propagation::HttpTraceContext>());
    auto composite_propagator = std::make_unique<
        opentelemetry::context::propagation::CompositePropagator>(
        std::move(propagators));

    grpc::OpenTelemetryPluginBuilder plugin_builder;
    plugin_builder.SetTracerProvider(
        opentelemetry::trace::Provider::GetTracerProvider());
    plugin_builder.SetMeterProvider(
        opentelemetry::metrics::Provider::GetMeterProvider());
    plugin_builder.SetTextMapPropagator(std::move(composite_propagator));

    absl::Status status = plugin_builder.BuildAndRegisterGlobal();
    if (!status.ok()) {
      LOG(ERROR) << "Failed to register gRPC OpenTelemetry Plugin: "
                 << status.ToString();
    }
  }
}

OpenCensusPlugin::~OpenCensusPlugin() { absl::SleepFor(kTraceUploadTimeout); }

OpenCensusInterceptorFactory::OpenCensusInterceptorFactory(
    absl::string_view service_full_name, bool use_otel_metrics)
    : use_otel_metrics_(use_otel_metrics) {
  if (!use_otel_metrics_) {
    internal::RpcCountViewDescriptor().RegisterForExport();
  }
  const google::protobuf::DescriptorPool* pool =
      google::protobuf::DescriptorPool::generated_pool();
  const google::protobuf::ServiceDescriptor* svc =
      pool->FindServiceByName(service_full_name);
  if (svc == nullptr) {
    LOG(WARNING) << "Disabling metrics for " << service_full_name
                 << " as the descriptor is not linked in to the binary.";
    return;
  }
  // Initialize the counters to zero so that Prometheus can reliably detect the
  // first RPC.
  for (int i = 0; i < svc->method_count(); i++) {
    std::string method =
        absl::StrFormat("%s/%s", svc->full_name(), svc->method(i)->name());
    for (int code = 0; google::rpc::Code_IsValid(code); code++) {
      if (use_otel_metrics_) {
        OtelRpcCount().Add(0, {{kMethodKey, method},
                               {kStatusKey, google::rpc::Code_Name(code)}});
      } else {
        opencensus::stats::Record(
            {{RpcCount(), 0}}, {{MethodKey(), method},
                                {StatusKey(), google::rpc::Code_Name(code)}});
      }
    }
  }
}

::grpc::experimental::Interceptor*
OpenCensusInterceptorFactory::CreateServerInterceptor(
    ::grpc::experimental::ServerRpcInfo* info) {
  if (absl::GetFlag(FLAGS_opencensus_metrics_port) <= 0) {
    // Metrics are disabled, so avoid creating a useless interceptor.
    return nullptr;
  }
  // The gRPC framework takes ownership of the interceptor and deletes it after
  // the RPC completes.
  return new OpenCensusInterceptor(info, use_otel_metrics_);
}

}  // namespace intrinsic
