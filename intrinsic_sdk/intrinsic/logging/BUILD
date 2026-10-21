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

load("//bazel:cc_macros.bzl", "cc_library")

# Component to log structured data with context.
package(default_visibility = ["//visibility:public"])

cc_library(
    name = "data_logger_client",
    srcs = ["data_logger_client.cc"],
    hdrs = ["data_logger_client.h"],
    deps = [
        ":structured_logging_client",
        "//intrinsic_sdk/intrinsic/connect/cc/grpc:channel",
        "//intrinsic_sdk/intrinsic/logging/proto:log_item_cc_proto",
        "//intrinsic_sdk/intrinsic/logging/proto:logger_service_cc_grpc",
        "//intrinsic_sdk/intrinsic/logging/proto:logger_service_cc_proto",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/util/status:status_macros_grpc",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/random",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@com_github_grpc_grpc//:grpc++",
    ],
)

cc_library(
    name = "structured_logging_client",
    srcs = ["structured_logging_client.cc"],
    hdrs = ["structured_logging_client.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/connect/cc/grpc:channel",
        "//intrinsic_sdk/intrinsic/logging/proto:log_item_cc_proto",
        "//intrinsic_sdk/intrinsic/logging/proto:logger_service_cc_grpc",
        "//intrinsic_sdk/intrinsic/logging/proto:logger_service_cc_proto",
        "//intrinsic_sdk/intrinsic/util:proto_time",
        "//intrinsic_sdk/intrinsic/util/status:status_conversion_grpc",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/util/status:status_macros_grpc",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
        "@com_github_grpc_grpc//:grpc++",
        "@intrinsic_apis//intrinsic/logging/proto:downsampler_cc_proto",
    ],
)
