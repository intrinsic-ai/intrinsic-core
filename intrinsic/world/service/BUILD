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
load("//bazel:cc_macros.bzl", "cc_library")
load("//bazel:go_macros.bzl", "go_proto_library")
load("//bazel:protos.bzl", "proto_library")

package(default_visibility = ["//intrinsic:intrinsic_internal"])

cc_library(
    name = "world_mutex",
    hdrs = ["world_mutex.h"],
    deps = [
        "//intrinsic/util:unique_id",
        "//intrinsic/world",
        "//intrinsic/world/objects:object_world",
        "//intrinsic_sdk/intrinsic/platform/pubsub:publisher",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
    ],
)

cc_library(
    name = "world_storage",
    srcs = ["world_storage.cc"],
    hdrs = ["world_storage.h"],
    deps = [
        ":world_mutex",
        "//intrinsic/config/proto:resource_set_cc_proto",
        "//intrinsic/resources/proto:geometric_resource_data_cc_proto",
        "//intrinsic/stats:scoped_span",
        "//intrinsic/world",
        "//intrinsic/world:cc_proto",
        "//intrinsic_sdk/intrinsic/geometry/storage:geometry_library",
        "//intrinsic_sdk/intrinsic/geometry/storage:geometry_serializer",
        "//intrinsic_sdk/intrinsic/util/status:status_builder",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/util/thread:periodic",
        "//intrinsic_sdk/intrinsic/world/gzfile",
        "//intrinsic_sdk/intrinsic/world/hashing",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/functional:any_invocable",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@intrinsic_apis//intrinsic/math/proto:tf_message_cc_proto",
        "@or_tools//ortools/base:file",
        "@or_tools//ortools/base:path",
    ],
)

# Implementation for Intrinsic's WorldService
# Provides gRPC interface to create and update planning worlds
cc_library(
    name = "worldservice_impl",
    srcs = ["worldservice_impl.cc"],
    hdrs = ["worldservice_impl.h"],
    deps = [
        ":world_mutex",
        ":world_service_cc_grpc_proto",
        ":world_service_cc_proto",
        ":world_storage",
        "//intrinsic/choreographer/fabrication_toolkit:shape_util",
        "//intrinsic/skills:footprint_util",
        "//intrinsic/stats:scoped_span",
        "//intrinsic/world",
        "//intrinsic/world:entity",
        "//intrinsic/world:kinematic_world_views",
        "//intrinsic/world/component:ppr_component",
        "//intrinsic/world/util:entity_search_util",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/geometry/api:geometry",
        "//intrinsic_sdk/intrinsic/geometry/api:io",
        "//intrinsic_sdk/intrinsic/geometry/storage:geometry_library",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:proto_conversion",
        "//intrinsic_sdk/intrinsic/platform/pubsub:publisher",
        "//intrinsic_sdk/intrinsic/util/status:ret_check_grpc",
        "//intrinsic_sdk/intrinsic/util/status:status_builder_grpc",
        "//intrinsic_sdk/intrinsic/util/status:status_conversion_grpc",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/util/status:status_macros_grpc",
        "//intrinsic_sdk/intrinsic/world:entity_id",
        "//intrinsic_sdk/intrinsic/world/hashing",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/geometry/proto:geometry_service_types_cc_proto",
        "@intrinsic_apis//intrinsic/math/proto:pose_cc_proto",
        "@intrinsic_apis//intrinsic/messages:messages_cc_proto",
        "@intrinsic_apis//intrinsic/skills/proto:footprint_cc_proto",
    ],
)

cc_library(
    name = "object_world_service",
    srcs = ["object_world_service.cc"],
    hdrs = ["object_world_service.h"],
    deps = [
        ":world_compatibility_service_cc_proto",
        ":world_mutex",
        ":world_storage",
        "//intrinsic/resources/proto:geometric_resource_data_cc_proto",
        "//intrinsic/resources/proto:resource_instance_cc_proto",
        "//intrinsic/skills:footprint_util",
        "//intrinsic/stats:metrics_utils",
        "//intrinsic/stats:scoped_span",
        "//intrinsic/util:unique_id",
        "//intrinsic/util/proto:pb_hash",
        "//intrinsic/world",
        "//intrinsic/world:entity",
        "//intrinsic/world:update_world",
        "//intrinsic/world/collision:coal_collision_checker",
        "//intrinsic/world/collision:collision_checker_utils",
        "//intrinsic/world/collision:collision_context_cc_proto",
        "//intrinsic/world/collision/util:make_collision_settings",
        "//intrinsic/world/collision/util:make_rule_set",
        "//intrinsic/world/conversion:world_from_object_proto",
        "//intrinsic/world/objects:footprint_object_utils",
        "//intrinsic/world/objects:lazy_object_view",
        "//intrinsic/world/objects:object_world",
        "//intrinsic/world/objects:object_world_creation_utils",
        "//intrinsic/world/objects:object_world_proto_utils",
        "//intrinsic/world/objects:simple_transform_node_visitor",
        "//intrinsic/world/proto:world_entity_cc_proto",
        "//intrinsic/world/proto:world_updates_cc_proto",
        "//intrinsic/world/service/objects:object_world_converter",
        "//intrinsic/world/service/objects:object_world_updates_utils",
        "//intrinsic/world/service/objects:world_comparator",
        "//intrinsic/world/util:compose_world_util",
        "//intrinsic_control/intrinsic/icon/proto:cart_space_conversion",
        "//intrinsic_kinematics/intrinsic/kinematics/types:cartesian_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits_xd",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/geometry/storage:geometry_library",
        "//intrinsic_sdk/intrinsic/geometry/storage:geometry_serializer",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:proto_conversion",
        "//intrinsic_sdk/intrinsic/platform/pubsub",
        "//intrinsic_sdk/intrinsic/platform/pubsub:publisher",
        "//intrinsic_sdk/intrinsic/util:aggregate_type",
        "//intrinsic_sdk/intrinsic/util:eigen",
        "//intrinsic_sdk/intrinsic/util/status:ret_check",
        "//intrinsic_sdk/intrinsic/util/status:status_conversion_grpc",
        "//intrinsic_sdk/intrinsic/util/status:status_conversion_rpc",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/util/status:status_macros_grpc",
        "//intrinsic_sdk/intrinsic/world:entity_id",
        "//intrinsic_sdk/intrinsic/world/component:geometry_component",
        "//intrinsic_sdk/intrinsic/world/component:physics_component",
        "//intrinsic_sdk/intrinsic/world/hashing",
        "//intrinsic_sdk/intrinsic/world/objects:object_entity_filter",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_ids",
        "//intrinsic_sdk/intrinsic/world/proto:object_world_service_cc_grpc_proto",
        "//intrinsic_sdk/intrinsic/world/proto:object_world_service_cc_proto",
        "//intrinsic_sdk/intrinsic/world/proto:object_world_updates_cc_proto",
        "//intrinsic_sdk/intrinsic/world/robot_payload",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/cleanup",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/functional:any_invocable",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_googleapis//google/rpc:status_cc_proto",
        "@com_google_protobuf//:protobuf",
        "@com_google_protobuf//:protobuf_lite",
        "@intrinsic_apis//intrinsic/math/proto:pose_cc_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:entity_cc_proto",
        "@intrinsic_apis//intrinsic/skills/proto:footprint_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:object_world_refs_cc_proto",
        "@io_opentelemetry_cpp//api",
        "@or_tools//ortools/base:strong_int",
    ],
)

proto_library(
    name = "world_compatibility_service_proto",
    srcs = ["world_compatibility_service.proto"],
    deps = [
        "//intrinsic/world/proto:world_entity_proto",
        "//intrinsic_sdk/intrinsic/world/proto:object_world_service_proto",
        "@intrinsic_apis//intrinsic/math/proto:pose_proto",
        "@intrinsic_apis//intrinsic/world/proto:collision_settings_proto",
    ],
)

py_proto_library(
    name = "world_compatibility_service_py_pb2",
    deps = [
        ":world_compatibility_service_proto",
    ],
)

go_proto_library(
    name = "world_compatibility_service_go_proto",
    importpath = "intrinsic/world/service/world_compatibility_service_go_proto",
    protos = [":world_compatibility_service_proto"],
    deps = [
        "//intrinsic/world/proto:object_world_service_go_proto",
        "//intrinsic/world/proto:world_entity_go_proto",
        "@intrinsic_apis//intrinsic/math/proto:pose_go_proto",
        "@intrinsic_apis//intrinsic/world/proto:collision_settings_go_proto",
    ],
)

cc_proto_library(
    name = "world_compatibility_service_cc_proto",
    deps = [
        ":world_compatibility_service_proto",
    ],
)

proto_library(
    name = "world_service_proto",
    srcs = ["world_service.proto"],
    deps = [
        "//intrinsic/world:proto",
        "//intrinsic/world/proto:collections_component_proto",
        "//intrinsic/world/proto:world_entity_proto",
        "//intrinsic/world/proto:world_updates_proto",
        "@intrinsic_apis//intrinsic/math/proto:pose_proto",
        "@intrinsic_apis//intrinsic/world/proto:physics_component_proto",
        "@intrinsic_apis//intrinsic/world/proto:sensor_component_proto",
    ],
)

cc_proto_library(
    name = "world_service_cc_proto",
    deps = [
        ":world_service_proto",
    ],
)

py_proto_library(
    name = "world_service_py_pb2",
    deps = [
        ":world_service_proto",
    ],
)

go_proto_library(
    name = "world_service_go_proto",
    compilers = [
        "@io_bazel_rules_go//proto:go_grpc_v2",
        "@io_bazel_rules_go//proto:go_proto",
    ],
    importpath = "intrinsic/world/service/world_service_go_proto",
    protos = [":world_service_proto"],
    deps = [
        "//intrinsic/world:go_proto",
        "//intrinsic/world/proto:collections_component_go_proto",
        "//intrinsic/world/proto:world_entity_go_proto",
        "//intrinsic/world/proto:world_updates_go_proto",
        "@intrinsic_apis//intrinsic/math/proto:pose_go_proto",
        "@intrinsic_apis//intrinsic/world/proto:physics_component_go_proto",
        "@intrinsic_apis//intrinsic/world/proto:sensor_component_go_proto",
    ],
)

cc_grpc_library(
    name = "world_service_cc_grpc_proto",
    srcs = [":world_service_proto"],
    generate_mocks = True,
    grpc_only = True,
    deps = [
        ":world_service_cc_proto",
    ],
)

py_grpc_library(
    name = "world_service_py_pb2_grpc",
    srcs = [":world_service_proto"],
    deps = [
        ":world_service_py_pb2",
    ],
)
