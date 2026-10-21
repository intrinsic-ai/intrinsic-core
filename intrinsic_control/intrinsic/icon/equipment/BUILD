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
load("//bazel:python.bzl", "py_library")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "channel_factory",
    srcs = ["channel_factory.cc"],
    hdrs = ["channel_factory.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/connect/cc/grpc:channel",
        "//intrinsic_sdk/intrinsic/util/grpc:channel",
        "//intrinsic_sdk/intrinsic/util/grpc:channel_interface",
        "//intrinsic_sdk/intrinsic/util/grpc:connection_params",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/time",
    ],
)

cc_library(
    name = "equipment_utils",
    srcs = ["equipment_utils.cc"],
    hdrs = ["equipment_utils.h"],
    deps = [
        ":channel_factory",
        "//intrinsic_sdk/intrinsic/connect/cc/grpc:channel",
        "//intrinsic_sdk/intrinsic/skills/cc:equipment_pack",
        "//intrinsic_sdk/intrinsic/skills/cc:skill_utils",
        "//intrinsic_sdk/intrinsic/util/grpc:channel_interface",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@intrinsic_apis//intrinsic/icon/equipment:icon_equipment_cc_proto",
        "@intrinsic_apis//intrinsic/skills/proto:equipment_cc_proto",
    ],
)

cc_library(
    name = "resolved_dependency_utils",
    srcs = ["resolved_dependency_utils.cc"],
    hdrs = ["resolved_dependency_utils.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/assets:interface_utils",
        "//intrinsic_sdk/intrinsic/assets/dependencies:utils_cc",
        "//intrinsic_sdk/intrinsic/connect/cc/grpc:channel",
        "//intrinsic_sdk/intrinsic/util/grpc:channel",
        "//intrinsic_sdk/intrinsic/util/grpc:channel_interface",
        "//intrinsic_sdk/intrinsic/util/status:status_conversion_grpc",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world/objects:kinematic_object",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_client",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_ids",
        "//intrinsic_sdk/intrinsic/world/util:object_reference_utils",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@com_github_grpc_grpc//:grpc++",
        "@intrinsic_apis//intrinsic/assets/proto/v1:resolved_dependency_cc_proto",
        "@intrinsic_apis//intrinsic/icon/proto/v1:service_cc_grpc_proto",
        "@intrinsic_apis//intrinsic/icon/proto/v1:service_cc_proto",
        "@intrinsic_apis//intrinsic/icon/proto/v1:types_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:object_world_refs_cc_proto",
    ],
)

py_library(
    name = "equipment_utils_py",
    srcs = ["equipment_utils.py"],
    deps = [
        "//intrinsic_control/intrinsic/icon/python:icon",
        "//intrinsic_sdk/intrinsic/util/grpc:connection",
        "@com_google_absl_py//absl/logging",
        "@intrinsic_apis//intrinsic/icon/equipment:icon_equipment_py_pb2",
        "@intrinsic_apis//intrinsic/resources/proto:resource_handle_py_pb2",
        "@intrinsic_apis//intrinsic/skills/proto:equipment_py_pb2",
    ],
)

py_library(
    name = "resolved_dependency_utils_py",
    srcs = ["resolved_dependency_utils.py"],
    deps = [
        "//intrinsic_control/intrinsic/icon/python:icon",
        "//intrinsic_sdk/intrinsic/assets:interface_utils_py",
        "//intrinsic_sdk/intrinsic/assets/dependencies:utils_py",
        "//intrinsic_sdk/intrinsic/world/python:object_world_client",
        "//intrinsic_sdk/intrinsic/world/python:object_world_ids",
        "//intrinsic_sdk/intrinsic/world/python:object_world_resources",
        "@com_github_grpc_grpc//src/python/grpcio/grpc:grpcio",
        "@intrinsic_apis//intrinsic/assets/proto/v1:resolved_dependency_py_pb2",
        "@intrinsic_apis//intrinsic/icon/proto/v1:service_py_pb2",
        "@intrinsic_apis//intrinsic/icon/proto/v1:service_py_pb2_grpc",
        "@intrinsic_apis//intrinsic/world/proto:object_world_refs_py_pb2",
    ],
)
