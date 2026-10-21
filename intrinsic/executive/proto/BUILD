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
load("@com_github_grpc_grpc//bazel:python_rules.bzl", "py_grpc_library")
load("@com_google_protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("@com_google_protobuf//bazel:py_proto_library.bzl", "py_proto_library")
load("//bazel:go_macros.bzl", "go_proto_library")
load("//bazel:protos.bzl", "proto_library")

package(default_visibility = ["//intrinsic:public_api_users"])

# code_execution_service
proto_library(
    name = "code_execution_service_proto",
    srcs = ["code_execution_service.proto"],
    deps = [
        "@com_google_googleapis//google/longrunning:operations_proto",
        "@com_google_protobuf//:any_proto",
        "@com_google_protobuf//:descriptor_proto",
        "@com_google_protobuf//:empty_proto",
        "@intrinsic_apis//intrinsic/executive/proto:code_execution_proto",
    ],
)

py_proto_library(
    name = "code_execution_service_py_pb2",
    deps = [":code_execution_service_proto"],
)

py_grpc_library(
    name = "code_execution_service_py_pb2_grpc",
    srcs = [":code_execution_service_proto"],
    deps = [":code_execution_service_py_pb2"],
)

cc_proto_library(
    name = "code_execution_service_cc_proto",
    deps = [":code_execution_service_proto"],
)

cc_grpc_library(
    name = "code_execution_service_cc_grpc_proto",
    srcs = [":code_execution_service_proto"],
    generate_mocks = True,
    grpc_only = True,
    deps = [":code_execution_service_cc_proto"],
)

go_proto_library(
    name = "code_execution_service_go_proto",
    compilers = [
        "@io_bazel_rules_go//proto:go_grpc_v2",
        "@io_bazel_rules_go//proto:go_proto",
    ],
    importpath = "intrinsic/executive/proto/code_execution_service_go_proto",
    protos = [":code_execution_service_proto"],
    deps = [
        "@com_google_cloud_go_longrunning//autogen/longrunningpb",
        "@intrinsic_apis//intrinsic/executive/proto:code_execution_go_proto",
        "@org_golang_google_protobuf//types/descriptorpb:go_default_library",
        "@org_golang_google_protobuf//types/known/anypb",
        "@org_golang_google_protobuf//types/known/emptypb",
    ],
)

# executive_debug_service
proto_library(
    name = "executive_debug_service_proto",
    srcs = ["executive_debug_service.proto"],
    visibility = ["//intrinsic/executive:__subpackages__"],
    deps = [
        "@opentelemetry_proto//:trace_proto",
    ],
)

cc_proto_library(
    name = "executive_debug_service_cc_proto",
    visibility = ["//intrinsic/executive:__subpackages__"],
    deps = [":executive_debug_service_proto"],
)

cc_grpc_library(
    name = "executive_debug_service_cc_grpc_proto",
    srcs = [":executive_debug_service_proto"],
    generate_mocks = True,
    grpc_only = True,
    visibility = [
        "//intrinsic/executive:__subpackages__",
    ],
    deps = [":executive_debug_service_cc_proto"],
)

go_proto_library(
    name = "executive_debug_service_go_proto",
    compilers = [
        "@io_bazel_rules_go//proto:go_grpc_v2",
        "@io_bazel_rules_go//proto:go_proto",
    ],
    importpath = "intrinsic/executive/proto/executive_debug_service_go_proto",
    protos = [":executive_debug_service_proto"],
    visibility = [
        "//intrinsic/executive:__subpackages__",
        "//intrinsic/tools/inctl/cmd:__pkg__",
        "//intrinsic_sdk/intrinsic/tools/inctl/cmd:__pkg__",
    ],
    deps = [
        "@io_opentelemetry_go_proto_otlp//trace/v1:go_default_library",
    ],
)

# executive_config
proto_library(
    name = "executive_config_proto",
    srcs = ["executive_config.proto"],
    deps = ["@com_google_protobuf//:duration_proto"],
)

cc_proto_library(
    name = "executive_config_cc_proto",
    deps = [":executive_config_proto"],
)

# executive_flags
proto_library(
    name = "executive_flags_proto",
    srcs = ["executive_flags.proto"],
    deps = [":executive_config_proto"],
)

cc_proto_library(
    name = "executive_flags_cc_proto",
    deps = [":executive_flags_proto"],
)

alias(
    name = "executive_service_proto",
    actual = "//intrinsic_sdk/intrinsic/executive/proto:executive_service_proto",
    tags = ["go/intrinsic-ioc-transition-tdd"],
)

alias(
    name = "executive_service_go_proto",
    actual = "//intrinsic_sdk/intrinsic/executive/proto:executive_service_go_proto",
    tags = ["go/intrinsic-ioc-transition-tdd"],
)

proto_library(
    name = "process_tree_proto",
    srcs = ["process_tree.proto"],
    deps = ["@intrinsic_apis//intrinsic/executive/proto:behavior_tree_proto"],
)

# process_tree
cc_proto_library(
    name = "process_tree_cc_proto",
    deps = [":process_tree_proto"],
)

py_proto_library(
    name = "process_tree_py_pb2",
    deps = [":process_tree_proto"],
)
