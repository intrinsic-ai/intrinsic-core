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

# Client libraries for the ObjectWorldService.

load("//bazel:cc_macros.bzl", "cc_library")

cc_library(
    name = "frame",
    srcs = ["frame.cc"],
    hdrs = ["frame.h"],
    visibility = ["//intrinsic:public_api_users"],
    deps = [
        ":object_world_ids",
        ":transform_node",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:proto_conversion",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world/proto:object_world_service_cc_proto",
        "@abseil-cpp//absl/status:statusor",
        "@intrinsic_apis//intrinsic/world/proto:object_world_refs_cc_proto",
    ],
)

cc_library(
    name = "object_entity_filter",
    srcs = ["object_entity_filter.cc"],
    hdrs = ["object_entity_filter.h"],
    visibility = ["//intrinsic:public_api_users"],
    deps = [
        ":object_world_ids",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
        "@intrinsic_apis//intrinsic/world/proto:object_world_refs_cc_proto",
    ],
)

cc_library(
    name = "object_world_ids",
    srcs = ["object_world_ids.cc"],
    hdrs = ["object_world_ids.h"],
    visibility = ["//intrinsic:public_api_users"],
    deps = [
        "//intrinsic_sdk/intrinsic/util:string_type",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "transform_node",
    srcs = ["transform_node.cc"],
    hdrs = ["transform_node.h"],
    visibility = ["//intrinsic:public_api_users"],
    deps = [
        ":object_world_ids",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "@intrinsic_apis//intrinsic/world/proto:object_world_refs_cc_proto",
    ],
)

cc_library(
    name = "world_object",
    srcs = ["world_object.cc"],
    hdrs = ["world_object.h"],
    visibility = ["//intrinsic:public_api_users"],
    deps = [
        ":frame",
        ":object_entity_filter",
        ":object_world_ids",
        ":transform_node",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:proto_conversion",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world/proto:object_world_service_cc_proto",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@intrinsic_apis//intrinsic/world/proto:object_world_refs_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:simulation_component_cc_proto",
    ],
)

cc_library(
    name = "kinematic_object",
    srcs = ["kinematic_object.cc"],
    hdrs = ["kinematic_object.h"],
    visibility = ["//intrinsic:public_api_users"],
    deps = [
        ":frame",
        ":object_world_ids",
        ":transform_node",
        ":world_object",
        "//intrinsic_control/intrinsic/icon/proto:cart_space_conversion",
        "//intrinsic_kinematics/intrinsic/kinematics/types:cartesian_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits_xd",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/util:eigen",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world/proto:object_world_service_cc_proto",
        "//intrinsic_sdk/intrinsic/world/robot_payload",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "object_world_client",
    srcs = ["object_world_client.cc"],
    hdrs = ["object_world_client.h"],
    visibility = ["//intrinsic:public_api_users"],
    deps = [
        ":frame",
        ":kinematic_object",
        ":object_entity_filter",
        ":object_world_ids",
        ":transform_node",
        ":world_object",
        "//intrinsic_control/intrinsic/icon/equipment:equipment_utils",
        "//intrinsic_control/intrinsic/icon/proto:cart_space_conversion",
        "//intrinsic_kinematics/intrinsic/kinematics/types:cartesian_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits_xd",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:proto_conversion",
        "//intrinsic_sdk/intrinsic/util:eigen",
        "//intrinsic_sdk/intrinsic/util/status:status_conversion_grpc",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world/hashing",
        "//intrinsic_sdk/intrinsic/world/proto:object_world_service_cc_grpc_proto",
        "//intrinsic_sdk/intrinsic/world/proto:object_world_service_cc_proto",
        "//intrinsic_sdk/intrinsic/world/proto:object_world_updates_cc_proto",
        "//intrinsic_sdk/intrinsic/world/robot_payload",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/icon/equipment:icon_equipment_cc_proto",
        "@intrinsic_apis//intrinsic/kinematics/types:joint_limits_cc_proto",
        "@intrinsic_apis//intrinsic/math/proto:pose_cc_proto",
        "@intrinsic_apis//intrinsic/resources/proto:resource_handle_cc_proto",
        "@intrinsic_apis//intrinsic/skills/proto:equipment_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:collision_settings_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:geometry_component_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:object_world_refs_cc_proto",
    ],
)
