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

# This package contains internal proto definition required for implementing the
# pinch gripper service.

load("@com_github_grpc_grpc//bazel:cc_grpc_library.bzl", "cc_grpc_library")
load("@com_github_grpc_grpc//bazel:python_rules.bzl", "py_grpc_library")
load("@com_google_protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("@com_google_protobuf//bazel:py_proto_library.bzl", "py_proto_library")
load("//bazel:protos.bzl", "proto_library")
load("//intrinsic_sdk/bazel:go_macros.bzl", "go_proto_library")

package(default_visibility = ["//visibility:public"])

proto_library(
    name = "pinch_gripper_server_proto",
    srcs = ["pinch_gripper_server.proto"],
    deps = [
        ":generic_pinch_gripper_configs_proto",
        "@com_google_protobuf//:duration_proto",
        "@intrinsic_apis//intrinsic/hardware/gripper:gripper_proto",
    ],
)

cc_proto_library(
    name = "pinch_gripper_server_cc_proto",
    deps = [":pinch_gripper_server_proto"],
)

go_proto_library(
    name = "pinch_gripper_server_go_proto",
    importpath = "intrinsic/hardware/gripper/service/proto/pinch_gripper_server_go_proto",
    protos = [":pinch_gripper_server_proto"],
    deps = [
        ":generic_pinch_gripper_configs_go_proto",
        "@intrinsic_apis//intrinsic/hardware/gripper:gripper_go_proto",
    ],
)

cc_grpc_library(
    name = "pinch_gripper_server_cc_grpc_proto",
    srcs = [":pinch_gripper_server_proto"],
    generate_mocks = True,
    grpc_only = True,
    deps = [":pinch_gripper_server_cc_proto"],
)

py_proto_library(
    name = "pinch_gripper_server_py_pb2",
    deps = [":pinch_gripper_server_proto"],
)

py_grpc_library(
    name = "pinch_gripper_server_py_pb2_grpc",
    srcs = [":pinch_gripper_server_proto"],
    deps = [":pinch_gripper_server_py_pb2"],
)

proto_library(
    name = "generic_pinch_gripper_configs_proto",
    srcs = ["generic_pinch_gripper_configs.proto"],
    deps = [
        "@intrinsic_apis//intrinsic/hardware/gripper:gripper_proto",
    ],
)

py_proto_library(
    name = "generic_pinch_gripper_configs_py_pb2",
    deps = [":generic_pinch_gripper_configs_proto"],
)

cc_proto_library(
    name = "generic_pinch_gripper_configs_cc_proto",
    deps = [":generic_pinch_gripper_configs_proto"],
)

go_proto_library(
    name = "generic_pinch_gripper_configs_go_proto",
    importpath = "intrinsic/hardware/gripper/service/proto/generic_pinch_gripper_configs_go_proto",
    protos = [":generic_pinch_gripper_configs_proto"],
    deps = [
        "@intrinsic_apis//intrinsic/hardware/gripper:gripper_go_proto",
    ],
)
