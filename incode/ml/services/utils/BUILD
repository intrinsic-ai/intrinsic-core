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

load("@rules_python//python:defs.bzl", "py_library")

package(default_visibility = [
    "//incode/ml:__subpackages__",
])

py_library(
    name = "otel_tracing",
    srcs = ["otel_tracing.py"],
    visibility = [
        "//intrinsic_inference:__subpackages__",
        "//intrinsic_perception/intrinsic/perception/skills:__subpackages__",
        "//incode/ml:__subpackages__",
    ],
    deps = [
        "@ai_intrinsic_sdks_pip_deps//opentelemetry_api",
        "@ai_intrinsic_sdks_pip_deps//opentelemetry_exporter_gcp_trace",
        "@ai_intrinsic_sdks_pip_deps//opentelemetry_exporter_otlp_proto_grpc",
        "@ai_intrinsic_sdks_pip_deps//opentelemetry_instrumentation_grpc",
        "@ai_intrinsic_sdks_pip_deps//opentelemetry_sdk",
        "@com_google_absl_py//absl/logging",
    ],
)
