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
    name = "conversions",
    srcs = ["conversions.cc"],
    hdrs = ["conversions.h"],
    visibility = ["//intrinsic:public_api_users"],
    deps = [
        "//intrinsic/util:eigen",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "@com_google_protobuf//:protobuf_lite",
        "@intrinsic_apis//intrinsic/icon/proto:joint_space_cc_proto",
    ],
)

cc_library(
    name = "motion_planner_client",
    srcs = ["motion_planner_client.cc"],
    hdrs = ["motion_planner_client.h"],
    visibility = ["//intrinsic:public_api_users"],
    deps = [
        ":conversions",
        "//intrinsic/util:eigen",
        "//intrinsic/util/grpc:channel_interface",
        "//intrinsic/util/status:status_conversion_grpc",
        "//intrinsic/util/status:status_macros",
        "//intrinsic_motion_planning/intrinsic/motion_planning/proto/v1:motion_planner_service_cc_grpc_proto",
        "//intrinsic_motion_planning/intrinsic/motion_planning/proto/v1:motion_planner_service_cc_proto",
        "//intrinsic_motion_planning/intrinsic/motion_planning/proto/v1:motion_specification_cc_proto",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:proto_conversion",
        "//intrinsic_sdk/intrinsic/world/objects:kinematic_object",
        "//intrinsic_sdk/intrinsic/world/objects:transform_node",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:string_view",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:transformed_geometry_cc_proto",
        "@intrinsic_apis//intrinsic/icon/proto:joint_space_cc_proto",
        "@intrinsic_apis//intrinsic/logging/proto:context_cc_proto",
        "@intrinsic_apis//intrinsic/motion_planning/proto:motion_target_cc_proto",
        "@intrinsic_apis//intrinsic/motion_planning/proto/v1:compute_ik_cc_proto",
        "@intrinsic_apis//intrinsic/motion_planning/proto/v1:motion_planner_config_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:collision_checker_config_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:collision_settings_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:object_world_refs_cc_proto",
    ] + [],
)

py_library(
    name = "motion_planner_client_py",
    srcs = ["motion_planner_client.py"],
    visibility = ["//intrinsic:public_api_users"],
    deps = [
        "//intrinsic/assets/dependencies:utils_py",
        "//intrinsic_motion_planning/intrinsic/motion_planning/proto/v1:motion_planner_service_py_pb2",
        "//intrinsic_motion_planning/intrinsic/motion_planning/proto/v1:motion_planner_service_py_pb2_grpc",
        "//intrinsic_motion_planning/intrinsic/motion_planning/proto/v1:motion_specification_py_pb2",
        "//intrinsic_sdk/intrinsic/math/python:data_types",
        "//intrinsic_sdk/intrinsic/math/python:proto_conversion",
        "//intrinsic_sdk/intrinsic/world/python:object_world_ids",
        "@com_github_grpc_grpc//src/python/grpcio/grpc:grpcio",
        "@com_google_protobuf//:protobuf_python",
        "@intrinsic_apis//intrinsic/assets/proto/v1:resolved_dependency_py_pb2",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:transformed_geometry_py_pb2",
        "@intrinsic_apis//intrinsic/icon/proto:joint_space_py_pb2",
        "@intrinsic_apis//intrinsic/logging/proto:context_py_pb2",
        "@intrinsic_apis//intrinsic/motion_planning/proto:motion_target_py_pb2",
        "@intrinsic_apis//intrinsic/motion_planning/proto/v1:compute_ik_py_pb2",
        "@intrinsic_apis//intrinsic/motion_planning/proto/v1:geometric_constraints_py_pb2",
        "@intrinsic_apis//intrinsic/motion_planning/proto/v1:motion_planner_config_py_pb2",
        "@intrinsic_apis//intrinsic/motion_planning/proto/v1:motion_planning_py_pb2",
        "@intrinsic_apis//intrinsic/motion_planning/proto/v1:robot_specification_py_pb2",
        "@intrinsic_apis//intrinsic/world/proto:collision_checker_config_py_pb2",
        "@intrinsic_apis//intrinsic/world/proto:collision_settings_py_pb2",
    ],
)
