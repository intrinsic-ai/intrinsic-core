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
load("@com_google_protobuf//bazel:proto_library.bzl", "proto_library")
load("@com_google_protobuf//bazel:py_proto_library.bzl", "py_proto_library")
load("@io_bazel_rules_go//proto:def.bzl", "go_proto_library")

package(default_visibility = ["//visibility:public"])

proto_library(
    name = "collision_rules_proto",
    srcs = ["collision_rules.proto"],
)

cc_proto_library(
    name = "collision_rules_cc_proto",
    deps = [":collision_rules_proto"],
)

go_proto_library(
    name = "collision_rules_go_proto",
    importpath = "intrinsic/scene/proto/v1/collision_rules_go_proto",
    protos = [":collision_rules_proto"],
)

py_proto_library(
    name = "collision_rules_py_pb2",
    deps = [":collision_rules_proto"],
)

proto_library(
    name = "entity_proto",
    srcs = ["entity.proto"],
    deps = [
        "@intrinsic_apis//intrinsic/math/proto:pose_proto",
        "@intrinsic_apis//intrinsic/world/proto:geometry_component_proto",
        "@intrinsic_apis//intrinsic/world/proto:kinematics_component_proto",
        "@intrinsic_apis//intrinsic/world/proto:physics_component_proto",
        "@intrinsic_apis//intrinsic/world/proto:sensor_component_proto",
    ],
)

cc_proto_library(
    name = "entity_cc_proto",
    deps = [
        ":entity_proto",
    ],
)

go_proto_library(
    name = "entity_go_proto",
    importpath = "intrinsic/scene/proto/v1/entity_go_proto",
    protos = [":entity_proto"],
    deps = [
        "@intrinsic_apis//intrinsic/math/proto:pose_go_proto",
        "@intrinsic_apis//intrinsic/world/proto:geometry_component_go_proto",
        "@intrinsic_apis//intrinsic/world/proto:kinematics_component_go_proto",
        "@intrinsic_apis//intrinsic/world/proto:physics_component_go_proto",
        "@intrinsic_apis//intrinsic/world/proto:sensor_component_go_proto",
    ],
)

py_proto_library(
    name = "entity_py_pb2",
    deps = [":entity_proto"],
)

proto_library(
    name = "object_properties_proto",
    srcs = ["object_properties.proto"],
)

cc_proto_library(
    name = "object_properties_cc_proto",
    deps = [":object_properties_proto"],
)

go_proto_library(
    name = "object_properties_go_proto",
    importpath = "intrinsic/scene/proto/v1/object_properties_go_proto",
    protos = [":object_properties_proto"],
)

py_proto_library(
    name = "object_properties_py_pb2",
    deps = [":object_properties_proto"],
)

proto_library(
    name = "simulation_spec_proto",
    srcs = ["simulation_spec.proto"],
)

cc_proto_library(
    name = "simulation_spec_cc_proto",
    deps = [":simulation_spec_proto"],
)

go_proto_library(
    name = "simulation_spec_go_proto",
    importpath = "intrinsic/scene/proto/v1/simulation_spec_go_proto",
    protos = [":simulation_spec_proto"],
)

proto_library(
    name = "scene_object_proto",
    srcs = ["scene_object.proto"],
    deps = [
        ":collision_rules_proto",
        ":entity_proto",
        ":object_properties_proto",
        ":simulation_spec_proto",
        "@com_google_protobuf//:any_proto",
    ],
)

cc_proto_library(
    name = "scene_object_cc_proto",
    deps = [":scene_object_proto"],
)

go_proto_library(
    name = "scene_object_go_proto",
    importpath = "intrinsic/scene/proto/v1/scene_object_go_proto",
    protos = [":scene_object_proto"],
    deps = [
        ":collision_rules_go_proto",
        ":entity_go_proto",
        ":geometry_stats_go_proto",
        ":object_properties_go_proto",
        ":simulation_spec_go_proto",
    ],
)

py_proto_library(
    name = "scene_object_py_pb2",
    deps = [":scene_object_proto"],
)

proto_library(
    name = "export_proto",
    srcs = ["export.proto"],
    deps = [
        ":scene_object_proto",
        "@com_google_protobuf//:descriptor_proto",
    ],
)

cc_proto_library(
    name = "export_cc_proto",
    deps = [":export_proto"],
)

go_proto_library(
    name = "export_go_proto",
    importpath = "intrinsic/scene/proto/v1/export_go_proto",
    protos = [":export_proto"],
    deps = [
        ":scene_object_go_proto",
        "@org_golang_google_protobuf//types/descriptorpb",
    ],
)

py_proto_library(
    name = "export_py_pb2",
    deps = [":export_proto"],
)

proto_library(
    name = "geometry_stats_proto",
    srcs = ["geometry_stats.proto"],
    deps = [
        "@intrinsic_apis//intrinsic/math/proto:point_proto",
        "@intrinsic_apis//intrinsic/math/proto:vector3_proto",
    ],
)

cc_proto_library(
    name = "geometry_stats_cc_proto",
    deps = [":geometry_stats_proto"],
)

go_proto_library(
    name = "geometry_stats_go_proto",
    importpath = "intrinsic/scene/proto/v1/geometry_stats_go_proto",
    protos = [":geometry_stats_proto"],
    deps = [
        "@intrinsic_apis//intrinsic/math/proto:point_go_proto",
        "@intrinsic_apis//intrinsic/math/proto:vector3_go_proto",
    ],
)

py_proto_library(
    name = "geometry_stats_py_pb2",
    deps = [":geometry_stats_proto"],
)

proto_library(
    name = "scene_object_updates_proto",
    srcs = ["scene_object_updates.proto"],
    deps = [
        ":collision_rules_proto",
        ":entity_proto",
        ":object_properties_proto",
        "@com_google_protobuf//:any_proto",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:transformed_geometry_proto",
        "@intrinsic_apis//intrinsic/kinematics/types:joint_limits_proto",
        "@intrinsic_apis//intrinsic/math/proto:matrix_proto",
        "@intrinsic_apis//intrinsic/math/proto:pose_proto",
        "@intrinsic_apis//intrinsic/world/proto:physics_component_proto",
    ],
)

cc_proto_library(
    name = "scene_object_updates_cc_proto",
    deps = [":scene_object_updates_proto"],
)

go_proto_library(
    name = "scene_object_updates_go_proto",
    importpath = "intrinsic/scene/proto/v1/scene_object_updates_go_proto",
    protos = [":scene_object_updates_proto"],
    deps = [
        ":collision_rules_go_proto",
        ":entity_go_proto",
        ":object_properties_go_proto",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:transformed_geometry_go_proto",
        "@intrinsic_apis//intrinsic/kinematics/types:joint_limits_go_proto",
        "@intrinsic_apis//intrinsic/math/proto:matrix_go_proto",
        "@intrinsic_apis//intrinsic/math/proto:pose_go_proto",
        "@intrinsic_apis//intrinsic/world/proto:physics_component_go_proto",
    ],
)

py_proto_library(
    name = "scene_object_updates_py_pb2",
    deps = [":scene_object_updates_proto"],
)

proto_library(
    name = "scene_object_config_proto",
    srcs = ["scene_object_config.proto"],
    deps = [
        ":collision_rules_proto",
        ":scene_object_updates_proto",
        "@com_google_protobuf//:any_proto",
    ],
)

cc_proto_library(
    name = "scene_object_config_cc_proto",
    deps = [":scene_object_config_proto"],
)

go_proto_library(
    name = "scene_object_config_go_proto",
    importpath = "intrinsic/scene/proto/v1/scene_object_config_go_proto",
    protos = [":scene_object_config_proto"],
    deps = [
        ":collision_rules_go_proto",
        ":scene_object_updates_go_proto",
    ],
)

py_proto_library(
    name = "scene_object_config_py_pb2",
    deps = [":scene_object_config_proto"],
)

proto_library(
    name = "scene_object_export_proto",
    srcs = ["scene_object_export.proto"],
    deps = [
        ":export_proto",
        "@com_google_googleapis//google/api:annotations_proto",
        "@intrinsic_apis//intrinsic/kubernetes/generator/proto:auth_options_proto",
    ],
)

cc_proto_library(
    name = "scene_object_export_cc_proto",
    deps = [":scene_object_export_proto"],
)

cc_grpc_library(
    name = "scene_object_export_cc_grpc_proto",
    srcs = [":scene_object_export_proto"],
    generate_mocks = True,
    grpc_only = True,
    deps = [":scene_object_export_cc_proto"],
)

go_proto_library(
    name = "scene_object_export_go_proto",
    compilers = [
        "@io_bazel_rules_go//proto:go_grpc_v2",
        "@io_bazel_rules_go//proto:go_proto",
        "@com_github_grpc_ecosystem_grpc_gateway_v2//protoc-gen-grpc-gateway:go_gen_grpc_gateway",
    ],
    importpath = "intrinsic/scene/proto/v1/scene_object_export_go_proto",
    protos = [":scene_object_export_proto"],
    deps = [
        ":export_go_proto",
        "@intrinsic_apis//intrinsic/kubernetes/generator/proto:auth_options_go_proto",
        "@org_golang_google_genproto_googleapis_api//annotations",
    ],
)

py_proto_library(
    name = "scene_object_export_py_pb2",
    deps = [":scene_object_export_proto"],
)

py_grpc_library(
    name = "scene_object_export_py_pb2_grpc",
    srcs = [":scene_object_export_proto"],
    deps = [":scene_object_export_py_pb2"],
)

# TODO(b/317911125): Enable scene_object_edit_service in the SDK after the service satisfies user facing requirements.

proto_library(
    name = "scene_object_edit_service_proto",
    srcs = ["scene_object_edit_service.proto"],
    deps = [
        ":scene_object_proto",
        ":scene_object_updates_proto",
        "@com_google_protobuf//:empty_proto",
        "@com_google_protobuf//:timestamp_proto",
        "@intrinsic_apis//intrinsic/kubernetes/generator/proto:auth_options_proto",
    ],
)

cc_proto_library(
    name = "scene_object_edit_service_cc_proto",
    deps = [":scene_object_edit_service_proto"],
)

py_proto_library(
    name = "scene_object_edit_service_py_pb2",
    deps = [":scene_object_edit_service_proto"],
)

go_proto_library(
    name = "scene_object_edit_service_go_proto",
    compilers = [
        "@io_bazel_rules_go//proto:go_grpc_v2",
        "@io_bazel_rules_go//proto:go_proto",
    ],
    importpath = "intrinsic/scene/proto/v1/scene_object_edit_service_go_proto",
    protos = [":scene_object_edit_service_proto"],
    deps = [
        ":scene_object_go_proto",
        ":scene_object_updates_go_proto",
        "@intrinsic_apis//intrinsic/kubernetes/generator/proto:auth_options_go_proto",
        "@org_golang_google_protobuf//types/known/emptypb",
        "@org_golang_google_protobuf//types/known/timestamppb",
    ],
)

cc_grpc_library(
    name = "scene_object_edit_service_cc_grpc_proto",
    srcs = [":scene_object_edit_service_proto"],
    generate_mocks = True,
    grpc_only = True,
    deps = [":scene_object_edit_service_cc_proto"],
)

py_grpc_library(
    name = "scene_object_edit_service_py_pb2_grpc",
    srcs = [":scene_object_edit_service_proto"],
    deps = [":scene_object_edit_service_py_pb2"],
)
