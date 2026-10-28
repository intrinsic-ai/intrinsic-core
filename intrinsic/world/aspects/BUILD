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
    name = "entity_dof_kinematic_view",
    srcs = ["entity_dof_kinematic_view.cc"],
    hdrs = ["entity_dof_kinematic_view.h"],
    deps = [
        ":entity_world_interface",
        "//intrinsic/world:configuration_validation",
        "//intrinsic/world:entity",
        "//intrinsic/world:kinematic_world_views",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits_xd",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/util:macros",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world:entity_id",
        "//intrinsic_sdk/intrinsic/world:labels",
        "//intrinsic_sdk/intrinsic/world/component:kinematics_component",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_library(
    name = "entity_kinematic_utils",
    srcs = ["entity_kinematic_utils.cc"],
    hdrs = ["entity_kinematic_utils.h"],
    deps = [
        ":entity_world_interface",
        "//intrinsic/world:entity",
        "//intrinsic_sdk/intrinsic/util/status:status_builder",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world:entity_id",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_library(
    name = "entity_deferred_ik_problem",
    srcs = ["entity_deferred_ik_problem.cc"],
    hdrs = ["entity_deferred_ik_problem.h"],
    deps = [
        ":entity_kinematic_utils",
        ":entity_world_interface",
        "//intrinsic/utils:ik_helper",
        "//intrinsic/world:kinematic_world_views",
        "//intrinsic/world:kinematics_builder",
        "//intrinsic_kinematics/intrinsic/kinematics:skeleton",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state",
        "//intrinsic_kinematics/intrinsic/kinematics/types:state_rn",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/util:macros",
        "//intrinsic_sdk/intrinsic/world:entity_id",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/log:die_if_null",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_library(
    name = "entity_grouping",
    srcs = ["entity_grouping.cc"],
    hdrs = ["entity_grouping.h"],
    deps = [
        ":entity_world_interface",
        "//intrinsic/world:entity",
        "//intrinsic/world:grouping",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world:entity_id",
        "//intrinsic_sdk/intrinsic/world:labels",
        "//intrinsic_sdk/intrinsic/world/hashing",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "entity_robot_cartesian_kinematic_view",
    srcs = ["entity_robot_cartesian_kinematic_view.cc"],
    hdrs = ["entity_robot_cartesian_kinematic_view.h"],
    deps = [
        ":entity_deferred_ik_problem",
        ":entity_dof_kinematic_view",
        ":entity_kinematic_utils",
        ":entity_world_interface",
        "//intrinsic/world:entity",
        "//intrinsic/world:kinematic_world_views",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/util:macros",
        "//intrinsic_sdk/intrinsic/world:entity_id",
        "//intrinsic_sdk/intrinsic/world/component:kinematics_component",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "chained_robot_cartesian_kinematic_view",
    srcs = ["chained_robot_cartesian_kinematic_view.cc"],
    hdrs = ["chained_robot_cartesian_kinematic_view.h"],
    deps = [
        ":entity_deferred_ik_problem",
        ":entity_world_interface",
        "//intrinsic/world:kinematic_world_views",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/util:aggregate_type",
        "//intrinsic_sdk/intrinsic/util:macros",
        "//intrinsic_sdk/intrinsic/util/status:status_builder",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world:entity_id",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "entity_world_interface",
    hdrs = ["entity_world_interface.h"],
    deps = [
        "//intrinsic/world:entity",
        "//intrinsic/world:grouping",
        "//intrinsic/world:kinematic_world_views",
        "//intrinsic/world/collision:collision_checker",
        "//intrinsic/world/collision:collision_context_cc_proto",
        "//intrinsic/world/proto:collections_component_cc_proto",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/util:macros",
        "//intrinsic_sdk/intrinsic/util/status:status_builder",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world:entity_id",
        "//intrinsic_sdk/intrinsic/world:labels",
        "//intrinsic_sdk/intrinsic/world/component:geometry_component",
        "//intrinsic_sdk/intrinsic/world/hashing",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@intrinsic_apis//intrinsic/world/proto:collision_checker_config_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:kinematics_component_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:physics_component_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:sensor_component_cc_proto",
    ],
)
