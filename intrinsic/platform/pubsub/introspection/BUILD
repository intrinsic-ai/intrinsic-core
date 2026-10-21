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

load("//bazel:cc_macros.bzl", "cc_binary", "cc_library")
load("//bazel:cc_oci_image.bzl", "cc_oci_image")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "metrics",
    srcs = ["metrics.cc"],
    hdrs = ["metrics.h"],
    deps = [
        "//intrinsic/stats:metrics_utils",
        "@abseil-cpp//absl/strings:string_view",
        "@io_opentelemetry_cpp//api",
    ],
)

cc_binary(
    name = "zenoh_introspection",
    srcs = ["zenoh_introspection.cc"],
    deps = [
        ":metrics",
        "//intrinsic/stats:opencensus",
        "//intrinsic_control/intrinsic/icon/release/portable:init_intrinsic",
        "//intrinsic_sdk/intrinsic/platform/pubsub/zenoh_util:zenoh_config",
        "//intrinsic_sdk/intrinsic/platform/pubsub/zenoh_util:zenoh_handle",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings:string_view",
        "@com_github_nlohmann_json//:json",
        "@or_tools//ortools/base:file",
    ],
)

cc_oci_image(
    name = "zenoh-introspection-image",
    binary = ":zenoh_introspection",
    data_path = "/",
)
