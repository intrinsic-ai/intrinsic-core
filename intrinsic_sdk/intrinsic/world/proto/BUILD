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

proto_library(
    name = "object_world_service_proto",
    srcs = ["object_world_service.proto"],
    visibility = ["//intrinsic:public_api_users"],
    deps = [
        ":object_world_updates_proto",
        "//intrinsic/resources/proto:geometric_resource_data_proto",
        "//intrinsic/world/proto:ppr_component_proto",
        "@com_google_googleapis//google/api:annotations_proto",
        "@com_google_googleapis//google/rpc:status_proto",
        "@com_google_protobuf//:any_proto",
        "@com_google_protobuf//:empty_proto",
        "@com_google_protobuf//:timestamp_proto",
        "@intrinsic_apis//intrinsic/icon/proto:cart_space_proto",
        "@intrinsic_apis//intrinsic/kinematics/types:joint_limits_proto",
        "@intrinsic_apis//intrinsic/kubernetes/generator/proto:auth_options_proto",
        "@intrinsic_apis//intrinsic/math/proto:pose_proto",
        "@intrinsic_apis//intrinsic/skills/proto:footprint_proto",
        "@intrinsic_apis//intrinsic/world/proto:collision_settings_proto",
        "@intrinsic_apis//intrinsic/world/proto:geometry_component_proto",
        "@intrinsic_apis//intrinsic/world/proto:kinematics_component_proto",
        "@intrinsic_apis//intrinsic/world/proto:object_world_refs_proto",
        "@intrinsic_apis//intrinsic/world/proto:physics_component_proto",
        "@intrinsic_apis//intrinsic/world/proto:robot_payload_proto",
        "@intrinsic_apis//intrinsic/world/proto:sensor_component_proto",
        "@intrinsic_apis//intrinsic/world/proto:simulation_component_proto",
    ],
)

cc_proto_library(
    name = "object_world_service_cc_proto",
    visibility = ["//intrinsic:public_api_users"],
    deps = [":object_world_service_proto"],
)

cc_grpc_library(
    name = "object_world_service_cc_grpc_proto",
    srcs = [":object_world_service_proto"],
    generate_mocks = True,
    grpc_only = True,
    visibility = ["//intrinsic:public_api_users"],
    deps = [":object_world_service_cc_proto"],
)

go_proto_library(
    name = "object_world_service_go_proto",
    compilers = [
        "@io_bazel_rules_go//proto:go_grpc_v2",
        "@io_bazel_rules_go//proto:go_proto",
        "//bazel:go_gen_grpc_gateway",
    ],
    importpath = "intrinsic/world/proto/object_world_service_go_proto",
    importpath_aliases = ["intrinsic/world/public/proto/object_world_service_go_proto"],
    protos = [":object_world_service_proto"],
    visibility = ["//intrinsic:public_api_users"],
    deps = [
        ":object_world_updates_go_proto",
        "//intrinsic/resources/proto:geometric_resource_data_go_proto",
        "//intrinsic/world/proto:ppr_component_go_proto",
        "@intrinsic_apis//intrinsic/icon/proto:cart_space_go_proto",
        "@intrinsic_apis//intrinsic/kinematics/types:joint_limits_go_proto",
        "@intrinsic_apis//intrinsic/kubernetes/generator/proto:auth_options_go_proto",
        "@intrinsic_apis//intrinsic/math/proto:pose_go_proto",
        "@intrinsic_apis//intrinsic/skills/proto:footprint_go_proto",
        "@intrinsic_apis//intrinsic/world/proto:collision_action_go_proto",
        "@intrinsic_apis//intrinsic/world/proto:collision_settings_go_proto",
        "@intrinsic_apis//intrinsic/world/proto:geometry_component_go_proto",
        "@intrinsic_apis//intrinsic/world/proto:kinematics_component_go_proto",
        "@intrinsic_apis//intrinsic/world/proto:object_world_refs_go_proto",
        "@intrinsic_apis//intrinsic/world/proto:physics_component_go_proto",
        "@intrinsic_apis//intrinsic/world/proto:robot_payload_go_proto",
        "@intrinsic_apis//intrinsic/world/proto:sensor_component_go_proto",
        "@intrinsic_apis//intrinsic/world/proto:simulation_component_go_proto",
        "@org_golang_google_genproto_googleapis_api//annotations",
        "@org_golang_google_genproto_googleapis_rpc//status",
        "@org_golang_google_protobuf//types/known/emptypb",
    ],
)

py_proto_library(
    name = "object_world_service_py_pb2",
    visibility = ["//intrinsic:public_api_users"],
    deps = [":object_world_service_proto"],
)

py_grpc_library(
    name = "object_world_service_py_pb2_grpc",
    srcs = [":object_world_service_proto"],
    visibility = ["//intrinsic:public_api_users"],
    deps = [":object_world_service_py_pb2"],
)

proto_library(
    name = "object_world_updates_proto",
    srcs = ["object_world_updates.proto"],
    visibility = ["//intrinsic:public_api_users"],
    deps = [
        "//intrinsic/world/proto:world_updates_proto",
        "@com_google_protobuf//:any_proto",
        "@com_google_protobuf//:wrappers_proto",
        "@intrinsic_apis//intrinsic/icon/proto:cart_space_proto",
        "@intrinsic_apis//intrinsic/kinematics/types:joint_limits_proto",
        "@intrinsic_apis//intrinsic/math/proto:pose_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:scene_object_proto",
        "@intrinsic_apis//intrinsic/world/proto:collision_action_proto",
        "@intrinsic_apis//intrinsic/world/proto:collision_settings_proto",
        "@intrinsic_apis//intrinsic/world/proto:geometry_component_proto",
        "@intrinsic_apis//intrinsic/world/proto:object_world_refs_proto",
        "@intrinsic_apis//intrinsic/world/proto:physics_component_proto",
        "@intrinsic_apis//intrinsic/world/proto:robot_payload_proto",
        "@intrinsic_apis//intrinsic/world/proto:simulation_component_proto",
        "@intrinsic_apis//intrinsic/world/proto:user_data_proto",
    ],
)

cc_proto_library(
    name = "object_world_updates_cc_proto",
    visibility = ["//intrinsic:public_api_users"],
    deps = [":object_world_updates_proto"],
)

py_proto_library(
    name = "object_world_updates_py_pb2",
    visibility = ["//intrinsic:public_api_users"],
    deps = [":object_world_updates_proto"],
)

go_proto_library(
    name = "object_world_updates_go_proto",
    importpath = "intrinsic/world/proto/object_world_updates_go_proto",
    importpath_aliases = ["intrinsic/world/public/proto/object_world_updates_go_proto"],
    protos = [":object_world_updates_proto"],
    visibility = ["//intrinsic:public_api_users"],
    deps = [
        "//intrinsic/world/proto:world_updates_go_proto",
        "@intrinsic_apis//intrinsic/geometry/proto:geometry_service_types_go_proto",
        "@intrinsic_apis//intrinsic/icon/proto:cart_space_go_proto",
        "@intrinsic_apis//intrinsic/kinematics/types:joint_limits_go_proto",
        "@intrinsic_apis//intrinsic/math/proto:pose_go_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:scene_object_go_proto",
        "@intrinsic_apis//intrinsic/world/proto:collision_action_go_proto",
        "@intrinsic_apis//intrinsic/world/proto:collision_settings_go_proto",
        "@intrinsic_apis//intrinsic/world/proto:geometry_component_go_proto",
        "@intrinsic_apis//intrinsic/world/proto:object_world_refs_go_proto",
        "@intrinsic_apis//intrinsic/world/proto:physics_component_go_proto",
        "@intrinsic_apis//intrinsic/world/proto:robot_payload_go_proto",
        "@intrinsic_apis//intrinsic/world/proto:simulation_component_go_proto",
        "@intrinsic_apis//intrinsic/world/proto:user_data_go_proto",
    ],
)
