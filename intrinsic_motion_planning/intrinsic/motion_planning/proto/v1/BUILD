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

package(default_visibility = ["//visibility:public"])

# gazelle:go_grpc_compilers //bazel:go_gen_grpc_gateway, @io_bazel_rules_go//proto:go_grpc_v2, @io_bazel_rules_go//proto:go_proto

# keep
proto_library(
    name = "motion_planner_service_proto",
    srcs = ["motion_planner_service.proto"],
    strip_import_prefix = "/intrinsic_motion_planning",
    deps = [
        ":motion_specification_proto",
        "@com_google_googleapis//google/api:annotations_proto",
        "@com_google_protobuf//:empty_proto",
        "@intrinsic_apis//intrinsic/assets/proto:field_metadata_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:resolved_dependency_proto",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:transformed_geometry_proto",
        "@intrinsic_apis//intrinsic/icon/proto:joint_space_proto",
        "@intrinsic_apis//intrinsic/kubernetes/generator/proto:auth_options_proto",
        "@intrinsic_apis//intrinsic/logging/proto:context_proto",
        "@intrinsic_apis//intrinsic/math/proto:pose_proto",
        "@intrinsic_apis//intrinsic/motion_planning/proto/v1:compute_ik_proto",
        "@intrinsic_apis//intrinsic/motion_planning/proto/v1:geometric_constraints_proto",
        "@intrinsic_apis//intrinsic/motion_planning/proto/v1:motion_planner_config_proto",
        "@intrinsic_apis//intrinsic/motion_planning/proto/v1:motion_planning_proto",
        "@intrinsic_apis//intrinsic/motion_planning/proto/v1:robot_specification_proto",
        "@intrinsic_apis//intrinsic/world/proto:collision_checker_config_proto",
        "@intrinsic_apis//intrinsic/world/proto:collision_settings_proto",
        "@intrinsic_apis//intrinsic/world/proto:object_world_refs_proto",
    ] + [],
)

cc_proto_library(
    name = "motion_planner_service_cc_proto",
    deps = [":motion_planner_service_proto"],
)

cc_grpc_library(
    name = "motion_planner_service_cc_grpc_proto",
    srcs = [":motion_planner_service_proto"],
    generate_mocks = True,
    grpc_only = True,
    deps = [":motion_planner_service_cc_proto"],
)

go_proto_library(
    name = "motion_planner_service_go_proto",
    compilers = [
        "@io_bazel_rules_go//proto:go_grpc_v2",
        "@io_bazel_rules_go//proto:go_proto",
        "//bazel:go_gen_grpc_gateway",
    ],
    importpath = "intrinsic/motion_planning/proto/v1/motion_planner_service_go_proto",
    proto = ":motion_planner_service_proto",
    visibility = ["//intrinsic:intrinsic_internal"],
    deps = [
        ":motion_specification_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:field_metadata_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:resolved_dependency_go_proto",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:transformed_geometry_go_proto",
        "@intrinsic_apis//intrinsic/icon/proto:joint_space_go_proto",
        "@intrinsic_apis//intrinsic/kubernetes/generator/proto:auth_options_go_proto",
        "@intrinsic_apis//intrinsic/logging/proto:context_go_proto",
        "@intrinsic_apis//intrinsic/math/proto:pose_go_proto",
        "@intrinsic_apis//intrinsic/motion_planning/proto/v1:compute_ik_go_proto",
        "@intrinsic_apis//intrinsic/motion_planning/proto/v1:geometric_constraints_go_proto",
        "@intrinsic_apis//intrinsic/motion_planning/proto/v1:motion_planner_config_go_proto",
        "@intrinsic_apis//intrinsic/motion_planning/proto/v1:motion_planning_go_proto",
        "@intrinsic_apis//intrinsic/motion_planning/proto/v1:robot_specification_go_proto",
        "@intrinsic_apis//intrinsic/world/proto:collision_checker_config_go_proto",
        "@intrinsic_apis//intrinsic/world/proto:collision_settings_go_proto",
        "@intrinsic_apis//intrinsic/world/proto:object_world_refs_go_proto",
        "@org_golang_google_genproto_googleapis_api//annotations",
    ] + [],
)

py_proto_library(
    name = "motion_planner_service_py_pb2",
    deps = [":motion_planner_service_proto"],
)

py_grpc_library(
    name = "motion_planner_service_py_pb2_grpc",
    srcs = [":motion_planner_service_proto"],
    deps = [":motion_planner_service_py_pb2"],
)

# keep
proto_library(
    name = "motion_specification_proto",
    srcs = ["motion_specification.proto"],
    strip_import_prefix = "/intrinsic_motion_planning",
    deps = [
        "@intrinsic_apis//intrinsic/motion_planning/proto/v1:geometric_constraints_proto",
        "@intrinsic_apis//intrinsic/motion_planning/proto/v1:motion_blending_parameter_proto",
        "@intrinsic_apis//intrinsic/motion_planning/proto/v1:motion_planning_limits_proto",
        "@intrinsic_apis//intrinsic/world/proto:collision_settings_proto",
    ] + [],
)

cc_proto_library(
    name = "motion_specification_cc_proto",
    deps = [":motion_specification_proto"],
)

py_proto_library(
    name = "motion_specification_py_pb2",
    deps = [":motion_specification_proto"],
)

go_proto_library(
    name = "motion_specification_go_proto",
    importpath = "intrinsic/motion_planning/proto/v1/motion_specification_go_proto",
    proto = ":motion_specification_proto",
    deps = [
        "@intrinsic_apis//intrinsic/motion_planning/proto/v1:geometric_constraints_go_proto",
        "@intrinsic_apis//intrinsic/motion_planning/proto/v1:motion_blending_parameter_go_proto",
        "@intrinsic_apis//intrinsic/motion_planning/proto/v1:motion_planning_limits_go_proto",
        "@intrinsic_apis//intrinsic/world/proto:collision_settings_go_proto",
    ] + [],
)
