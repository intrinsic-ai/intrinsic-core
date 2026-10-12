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

# Protos for internal skill framework types.

load("@com_github_grpc_grpc//bazel:cc_grpc_library.bzl", "cc_grpc_library")
load("@com_github_grpc_grpc//bazel:python_rules.bzl", "py_grpc_library")
load("@com_google_protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("@com_google_protobuf//bazel:py_proto_library.bzl", "py_proto_library")
load("//bazel:protos.bzl", "proto_library")

package(default_visibility = [
    "//visibility:public",
])

proto_library(
    name = "skill_registry_internal_proto",
    srcs = ["skill_registry_internal.proto"],
    deps = [
        "//intrinsic_sdk/intrinsic/skills/proto:skill_registry_config_proto",
        "@com_google_protobuf//:empty_proto",
        "@intrinsic_apis//intrinsic/skills/proto:skills_proto",
    ],
)

cc_proto_library(
    name = "skill_registry_internal_cc_proto",
    deps = [":skill_registry_internal_proto"],
)

cc_grpc_library(
    name = "skill_registry_internal_cc_grpc_proto",
    srcs = [":skill_registry_internal_proto"],
    generate_mocks = True,
    grpc_only = True,
    deps = [":skill_registry_internal_cc_proto"],
)

py_proto_library(
    name = "skill_registry_internal_py_pb2",
    deps = [":skill_registry_internal_proto"],
)

py_grpc_library(
    name = "skill_registry_internal_py_pb2_grpc",
    srcs = [":skill_registry_internal_proto"],
    deps = [":skill_registry_internal_py_pb2"],
)

proto_library(
    name = "behavior_tree_registry_internal_proto",
    srcs = ["behavior_tree_registry_internal.proto"],
    deps = ["//intrinsic_sdk/intrinsic/executive/proto:behavior_tree_proto"],
)

cc_proto_library(
    name = "behavior_tree_registry_internal_cc_proto",
    deps = [":behavior_tree_registry_internal_proto"],
)

cc_grpc_library(
    name = "behavior_tree_registry_internal_cc_grpc_proto",
    srcs = [":behavior_tree_registry_internal_proto"],
    generate_mocks = True,
    grpc_only = True,
    deps = [":behavior_tree_registry_internal_cc_proto"],
)

py_proto_library(
    name = "behavior_tree_registry_internal_py_pb2",
    deps = [":behavior_tree_registry_internal_proto"],
)

py_grpc_library(
    name = "behavior_tree_registry_internal_py_pb2_grpc",
    srcs = [":behavior_tree_registry_internal_proto"],
    deps = [":behavior_tree_registry_internal_py_pb2"],
)
