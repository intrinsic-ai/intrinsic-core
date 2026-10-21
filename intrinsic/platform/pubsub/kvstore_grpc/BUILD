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

filegroup(
    name = "kvservice-template",
    srcs = ["kvservice.yaml"],
    visibility = [
        "//intrinsic:intrinsic_internal",
    ],
)

cc_library(
    name = "server_impl",
    srcs = [
        "metrics.cc",
        "server_impl.cc",
    ],
    hdrs = [
        "metrics.h",
        "server_impl.h",
    ],
    deps = [
        "//intrinsic/stats:metrics_utils",
        "//intrinsic_sdk/intrinsic/platform/pubsub",
        "//intrinsic_sdk/intrinsic/platform/pubsub:kvstore",
        "//intrinsic_sdk/intrinsic/util/status:status_conversion_grpc",
        "//intrinsic_sdk/intrinsic/util/status:status_macros_grpc",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/time",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/platform/pubsub/kvstore_grpc:kvstore_cc_grpc_proto",
        "@intrinsic_apis//intrinsic/platform/pubsub/kvstore_grpc:kvstore_cc_proto",
        "@io_opentelemetry_cpp//api",
    ],
)

cc_binary(
    name = "server_main",
    srcs = ["server_main.cc"],
    deps = [
        ":server_impl",
        "//intrinsic/stats:opencensus",
        "//intrinsic_control/intrinsic/icon/release/portable:init_intrinsic",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings:str_format",
        "@com_github_grpc_grpc//:grpc++",
        "@com_github_grpc_grpc//:grpc_security_base",
    ],
)

cc_oci_image(
    name = "kvservice-image",
    binary = ":server_main",
    data_path = "/",
    visibility = [
        "//intrinsic:intrinsic_internal",
    ],
)
