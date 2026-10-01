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

# Testing utilities for skills.

load("//bazel:cc_macros.bzl", "cc_library")
load("//bazel:python.bzl", "py_library")

package(
    default_testonly = True,
)

py_library(
    name = "skill_test_utils",
    srcs = ["skill_test_utils.py"],
    visibility = [
        "//intrinsic:public_api_users",
    ],
    deps = [
        "//intrinsic/assets:id_utils_py",
        "//intrinsic/geometry/proto:geometry_service_py_pb2_grpc",
        "//intrinsic/skills/internal:execute_context_impl_py",
        "//intrinsic/skills/internal:get_footprint_context_impl_py",
        "//intrinsic/skills/internal:preview_context_impl_py",
        "//intrinsic/world/proto:object_world_service_py_pb2_grpc",
        "//intrinsic_control/intrinsic/icon/release:file_helpers_py",
        "//intrinsic_motion_planning/intrinsic/motion_planning/proto/v1:motion_planner_service_py_pb2_grpc",
        "//intrinsic_sdk/intrinsic/motion_planning:motion_planner_client_py",
        "//intrinsic_sdk/intrinsic/skills/python:execute_request",
        "//intrinsic_sdk/intrinsic/skills/python:get_footprint_request",
        "//intrinsic_sdk/intrinsic/skills/python:preview_request",
        "//intrinsic_sdk/intrinsic/skills/python:skill_canceller",
        "//intrinsic_sdk/intrinsic/skills/python:skill_interface",
        "//intrinsic_sdk/intrinsic/skills/python:skill_logging_context",
        "//intrinsic_sdk/intrinsic/world/python:object_world_client",
        "@com_github_grpc_grpc//src/python/grpcio/grpc:grpcio",
        "@com_google_protobuf//:protobuf_python",
        "@intrinsic_apis//intrinsic/logging/proto:context_py_pb2",
        "@intrinsic_apis//intrinsic/resources/proto:resource_handle_py_pb2",
        "@intrinsic_apis//intrinsic/skills/proto:skill_manifest_py_pb2",
    ],
)

cc_library(
    name = "skill_test_utils_cc",
    testonly = True,
    srcs = ["skill_test_utils.cc"],
    hdrs = ["skill_test_utils.h"],
    visibility = [
        "//intrinsic:public_api_users",
    ],
    deps = [
        "//intrinsic/skills/internal:execute_context_impl",
        "//intrinsic/skills/internal:get_footprint_context_impl",
        "//intrinsic/skills/internal:preview_context_impl",
        "//intrinsic/util:file_helpers",
        "//intrinsic/util/path_resolver",
        "//intrinsic/util/status:status_macros",
        "//intrinsic/world/proto:object_world_service_cc_grpc_proto",
        "//intrinsic_motion_planning/intrinsic/motion_planning/proto/v1:motion_planner_service_cc_grpc_proto",
        "//intrinsic_sdk/intrinsic/geometry/storage:geometry_library",
        "//intrinsic_sdk/intrinsic/geometry/storage:in_memory_storage",
        "//intrinsic_sdk/intrinsic/motion_planning:motion_planner_client",
        "//intrinsic_sdk/intrinsic/skills/cc:equipment_pack",
        "//intrinsic_sdk/intrinsic/skills/cc:execute_request",
        "//intrinsic_sdk/intrinsic/skills/cc:skill_canceller",
        "//intrinsic_sdk/intrinsic/skills/cc:skill_interface",
        "//intrinsic_sdk/intrinsic/skills/cc:skill_logging_context",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_client",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@com_github_grpc_grpc//:grpc++",
        "@com_github_grpc_grpc//:grpc_security_base",
        "@com_google_cel_cpp//internal:testing_no_main",
        "@com_google_googletest//:gtest",  # buildcleaner: keep
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/assets/proto/v1:grpc_connection_cc_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:resolved_dependency_cc_proto",
        "@intrinsic_apis//intrinsic/skills/proto:skill_manifest_cc_proto",
    ],
)
