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

load("@com_github_grpc_grpc//bazel:cc_grpc_library.bzl", "cc_grpc_library")
load("@com_google_protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("//bazel:cc_macros.bzl", "cc_binary")
load("//bazel:go_macros.bzl", "go_proto_library")
load("//bazel:protos.bzl", "proto_library")

proto_library(
    name = "hardware_module_introspection_proto",
    srcs = ["hardware_module_introspection.proto"],
)

cc_proto_library(
    name = "hardware_module_introspection_cc_proto",
    deps = [":hardware_module_introspection_proto"],
)

cc_grpc_library(
    name = "hardware_module_introspection_cc_grpc_proto",
    srcs = [":hardware_module_introspection_proto"],
    grpc_only = True,
    deps = [":hardware_module_introspection_cc_proto"],
)

cc_binary(
    name = "hardware_module_introspection",
    srcs = ["hardware_module_introspection.cc"],
    visibility = ["//visibility:public"],
    deps = [
        ":hardware_module_introspection_cc_grpc_proto",
        ":hardware_module_introspection_cc_proto",
        "//intrinsic_control/intrinsic/icon/hal:hardware_interface_utils",
        "//intrinsic_control/intrinsic/icon/hal:hardware_module_proxy",
        "//intrinsic_control/intrinsic/icon/release/portable:init_intrinsic",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/util/status:status_macros_grpc",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@com_github_grpc_grpc//:grpc++",
        "@com_github_grpc_grpc//:grpc_security_base",
    ],
)

go_proto_library(
    name = "hardware_module_introspection_go_proto",
    compilers = [
        "@io_bazel_rules_go//proto:go_grpc_v2",
        "@io_bazel_rules_go//proto:go_proto",
    ],
    importpath = "intrinsic/icon/hal/tools/hardware_module_introspection_go_proto",
    protos = [":hardware_module_introspection_proto"],
)
