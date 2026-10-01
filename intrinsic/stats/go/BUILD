# Copyright 2026 Intrinsic Innovation LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

load("//bazel:go_macros.bzl", "go_library")

package(default_visibility = ["//visibility:public"])

go_library(
    name = "telemetry",
    srcs = [
        "otel_telemetry.go",
        "telemetry.go",
    ],
    importpath = "intrinsic/stats/go/telemetry",
    deps = [
        "@com_github_golang_glog//:go_default_library",
        "@com_github_googlecloudplatform_opentelemetry_operations_go_exporter_trace//:go_default_library",
        "@io_opencensus_go//plugin/ocgrpc:go_default_library",
        "@io_opencensus_go//stats:go_default_library",
        "@io_opencensus_go//stats/view:go_default_library",
        "@io_opencensus_go//tag:go_default_library",
        "@io_opencensus_go//trace:go_default_library",
        "@io_opencensus_go_contrib_exporter_prometheus//:go_default_library",
        "@io_opentelemetry_go_contrib_instrumentation_google_golang_org_grpc_otelgrpc//:otelgrpc",
        "@io_opentelemetry_go_contrib_instrumentation_net_http_otelhttp//:otelhttp",
        "@io_opentelemetry_go_contrib_propagators_b3//:b3",
        "@io_opentelemetry_go_contrib_propagators_opencensus//:opencensus",
        "@io_opentelemetry_go_otel//:otel",
        "@io_opentelemetry_go_otel//attribute",
        "@io_opentelemetry_go_otel//codes",
        "@io_opentelemetry_go_otel//propagation",
        "@io_opentelemetry_go_otel//semconv/v1.34.0:v1_34_0",
        "@io_opentelemetry_go_otel_bridge_opencensus//:opencensus",
        "@io_opentelemetry_go_otel_exporters_otlp_otlptrace_otlptracegrpc//:otlptracegrpc",
        "@io_opentelemetry_go_otel_sdk//resource",
        "@io_opentelemetry_go_otel_sdk//trace",
        "@io_opentelemetry_go_otel_trace//:trace",
        "@org_golang_google_grpc//:go_default_library",
        "@org_golang_google_grpc//codes:go_default_library",
        "@org_golang_google_grpc//stats:go_default_library",
        "@org_golang_google_grpc//status:go_default_library",
    ],
)
