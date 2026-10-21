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

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "grpc_relay",
    srcs = ["grpc_relay.cc"],
    hdrs = ["grpc_relay.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/util/grpc:channel",
        "//intrinsic_sdk/intrinsic/util/grpc:channel_interface",
        "//intrinsic_sdk/intrinsic/util/grpc:connection_params",
        "//intrinsic_sdk/intrinsic/util/status:status_builder",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/synchronization",
        "@com_github_grpc_grpc//:grpc++",
    ],
)

cc_binary(
    name = "proxy_server_main",
    srcs = ["proxy_server_main.cc"],
    visibility = [
        "//intrinsic/simulation/docker:__pkg__",
    ],
    deps = [
        ":grpc_relay",
        "//intrinsic/simulation/service/proto/first_party:simulation_service_cc_grpc_proto",
        "//intrinsic/simulation/service/proto/first_party:simulation_service_cc_proto",
        "//intrinsic/stats:opencensus",
        "//intrinsic_control/intrinsic/icon/release/portable:init_intrinsic",
        "//intrinsic_sdk/intrinsic/util/grpc:channel",
        "//intrinsic_sdk/intrinsic/util/grpc:connection_params",
        "//intrinsic_sdk/intrinsic/util/status:ret_check",
        "//intrinsic_sdk/intrinsic/util/status:status_builder",
        "//intrinsic_sdk/intrinsic/util/status:status_conversion_grpc",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
        "@com_github_grpc_grpc//:grpc++",
        "@intrinsic_apis//intrinsic/simulation/gazebo/proto/v1:gazebo_service_cc_grpc_proto",
        "@intrinsic_apis//intrinsic/simulation/gazebo/proto/v1:gazebo_service_cc_proto",
    ] + [],
)
