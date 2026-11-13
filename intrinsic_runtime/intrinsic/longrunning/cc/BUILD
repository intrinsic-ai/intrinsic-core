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

cc_library(
    name = "grpc_operations_proxy",
    srcs = ["grpc_operations_proxy.cc"],
    hdrs = ["grpc_operations_proxy.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":operations_proxy",
        "//intrinsic_sdk/intrinsic/connect/cc/grpc:channel",
        "//intrinsic_sdk/intrinsic/util/status:status_conversion_grpc",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/log:die_if_null",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_googleapis//google/longrunning:longrunning_cc_grpc",
        "@com_google_googleapis//google/longrunning:longrunning_cc_proto",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_library(
    name = "operation_context",
    srcs = ["operation_context.cc"],
    hdrs = ["operation_context.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//intrinsic_sdk/intrinsic/util/thread:stop_token",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/synchronization",
    ],
)

cc_library(
    name = "operation_scheduler",
    srcs = ["operation_scheduler.cc"],
    hdrs = ["operation_scheduler.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":operation_context",
        ":operation_scheduler_interface",
        "//intrinsic_sdk/intrinsic/util/status:status_conversion_rpc",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/util/thread:stop_token",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/functional:bind_front",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@com_google_googleapis//google/longrunning:longrunning_cc_proto",
        "@com_google_googleapis//google/rpc:status_cc_proto",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_library(
    name = "operation_scheduler_interface",
    hdrs = ["operation_scheduler_interface.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":operation_context",
        ":operations_proxy",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@com_google_googleapis//google/longrunning:longrunning_cc_proto",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_library(
    name = "operations_proxy",
    hdrs = ["operations_proxy.h"],
    visibility = ["//visibility:public"],
    deps = [
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@com_google_googleapis//google/longrunning:longrunning_cc_proto",
    ],
)

cc_library(
    name = "operations_service_impl",
    srcs = ["operations_service_impl.cc"],
    hdrs = ["operations_service_impl.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":operations_proxy",
        "//intrinsic_sdk/intrinsic/util/status:status_macros_grpc",
        "@abseil-cpp//absl/log:die_if_null",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_googleapis//google/longrunning:longrunning_cc_grpc",
        "@com_google_googleapis//google/longrunning:longrunning_cc_proto",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_library(
    name = "wrappers",
    srcs = ["wrappers.cc"],
    hdrs = ["wrappers.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":operation_context",
        ":operation_scheduler_interface",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@com_google_googleapis//google/longrunning:longrunning_cc_proto",
        "@com_google_protobuf//:protobuf",
    ],
)
