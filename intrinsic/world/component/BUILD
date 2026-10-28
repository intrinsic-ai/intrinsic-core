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

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "attachment_component",
    srcs = ["attachment_component.cc"],
    hdrs = ["attachment_component.h"],
    deps = [
        "//intrinsic/utils/proto_conversion:blue_wrappers",
        "//intrinsic/world/proto:attachment_component_cc_proto",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:proto_conversion",
        "//intrinsic_sdk/intrinsic/util:proto_time",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world:entity_id",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_library(
    name = "collections_component",
    srcs = ["collections_component.cc"],
    hdrs = ["collections_component.h"],
    deps = [
        "//intrinsic/world/proto:collections_component_cc_proto",
        "//intrinsic_sdk/intrinsic/util/status:status_builder",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world:entity_id",
        "//intrinsic_sdk/intrinsic/world/hashing",
        "@abseil-cpp//absl/container:node_hash_map",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "collections_member_component",
    srcs = ["collections_member_component.cc"],
    hdrs = ["collections_member_component.h"],
    deps = [
        "//intrinsic/world/proto:collections_component_cc_proto",
        "//intrinsic/world/proto:collections_member_component_cc_proto",
        "//intrinsic_sdk/intrinsic/util/status:status_builder",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world:entity_id",
        "//intrinsic_sdk/intrinsic/world/hashing",
        "@abseil-cpp//absl/container:node_hash_map",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "collision_component",
    srcs = ["collision_component.cc"],
    hdrs = ["collision_component.h"],
    deps = [
        "//intrinsic/world/proto:collision_component_cc_proto",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world:entity_id",
        "//intrinsic_sdk/intrinsic/world/hashing",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "simulation_component",
    srcs = ["simulation_component.cc"],
    hdrs = ["simulation_component.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@intrinsic_apis//intrinsic/world/proto:simulation_component_cc_proto",
    ],
)

cc_library(
    name = "robot_component",
    srcs = ["robot_component.cc"],
    hdrs = ["robot_component.h"],
    deps = [
        "//intrinsic/world:kinematic_world_views",
        "//intrinsic/world/proto:generic_action_cc_proto",
        "//intrinsic/world/proto:robot_component_cc_proto",
        "//intrinsic_control/intrinsic/icon/proto:cart_space_conversion",
        "//intrinsic_kinematics/intrinsic/kinematics/types:cartesian_limits",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/util:aggregate_type",
        "//intrinsic_sdk/intrinsic/util:eigen",
        "//intrinsic_sdk/intrinsic/util/status:status_builder",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world:entity_id",
        "//intrinsic_sdk/intrinsic/world/hashing",
        "//intrinsic_sdk/intrinsic/world/robot_payload",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/icon/proto:cart_space_cc_proto",
    ],
)

cc_library(
    name = "user_data_component",
    srcs = ["user_data_component.cc"],
    hdrs = ["user_data_component.h"],
    deps = [
        "//intrinsic/world/proto:user_data_component_cc_proto",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world/hashing",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_library(
    name = "ppr_component",
    srcs = ["ppr_component.cc"],
    hdrs = ["ppr_component.h"],
    deps = [
        "//intrinsic/world/proto:ppr_component_cc_proto",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "projector_component",
    srcs = ["projector_component.cc"],
    hdrs = ["projector_component.h"],
    deps = [
        "//intrinsic/world/proto:projector_component_cc_proto",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
    ],
)
