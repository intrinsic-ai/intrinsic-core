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

load("//bazel:python.bzl", "py_library")

package(default_visibility = ["//visibility:public"])

py_library(
    name = "skill_interface",
    srcs = ["skill_interface.py"],
    deps = [
        ":execute_context",
        ":execute_request",
        ":get_footprint_context",
        ":get_footprint_request",
        ":preview_context",
        ":preview_request",
        "//intrinsic/skills/proto:skill_service_py_pb2",
        "//intrinsic/util/status:status_exception",
        "@com_google_protobuf//:protobuf_python",
        "@intrinsic_apis//intrinsic/skills/proto:footprint_py_pb2",
    ],
)

py_library(
    name = "skill_interface_utils",
    srcs = ["skill_interface_utils.py"],
    deps = [
        ":skill_interface",
        "//intrinsic/skills/internal:execute_context_impl_py",
        "//intrinsic/skills/internal:preview_context_impl_py",
        "@intrinsic_apis//intrinsic/resources/proto:resource_handle_py_pb2",
    ],
)

py_library(
    name = "get_footprint_context",
    srcs = ["get_footprint_context.py"],
    deps = [
        "//intrinsic/geometry/proto:geometry_service_py_pb2_grpc",
        "//intrinsic_sdk/intrinsic/motion_planning:motion_planner_client_py",
        "//intrinsic_sdk/intrinsic/world/python:object_world_client",
        "//intrinsic_sdk/intrinsic/world/python:object_world_ids",
        "//intrinsic_sdk/intrinsic/world/python:object_world_resources",
    ],
)

py_library(
    name = "get_footprint_request",
    srcs = ["get_footprint_request.py"],
    deps = ["@com_google_protobuf//:protobuf_python"],
)

py_library(
    name = "execute_context",
    srcs = ["execute_context.py"],
    deps = [
        ":skill_canceller",
        ":skill_logging_context",
        "//intrinsic/geometry/proto:geometry_service_py_pb2_grpc",
        "//intrinsic_sdk/intrinsic/motion_planning:motion_planner_client_py",
        "//intrinsic_sdk/intrinsic/world/python:object_world_client",
        "@intrinsic_apis//intrinsic/resources/proto:resource_handle_py_pb2",
    ],
)

py_library(
    name = "execute_request",
    srcs = ["execute_request.py"],
    deps = ["@com_google_protobuf//:protobuf_python"],
)

py_library(
    name = "preview_context",
    srcs = ["preview_context.py"],
    deps = [
        ":skill_canceller",
        ":skill_logging_context",
        "//intrinsic/geometry/proto:geometry_service_py_pb2_grpc",
        "//intrinsic/world/proto:object_world_updates_py_pb2",
        "//intrinsic_sdk/intrinsic/motion_planning:motion_planner_client_py",
        "//intrinsic_sdk/intrinsic/world/python:object_world_client",
        "//intrinsic_sdk/intrinsic/world/python:object_world_ids",
        "//intrinsic_sdk/intrinsic/world/python:object_world_resources",
    ],
)

py_library(
    name = "preview_request",
    srcs = ["preview_request.py"],
    deps = ["@com_google_protobuf//:protobuf_python"],
)

py_library(
    name = "skill_canceller",
    srcs = ["skill_canceller.py"],
)

py_library(
    name = "proto_utils",
    srcs = ["proto_utils.py"],
    deps = ["@com_google_protobuf//:protobuf_python"],
)

py_library(
    name = "skill_logging_context",
    srcs = ["skill_logging_context.py"],
    deps = [
        "@intrinsic_apis//intrinsic/logging/proto:context_py_pb2",
    ],
)

py_library(
    name = "skill_data",
    srcs = ["skill_data.py"],
)
