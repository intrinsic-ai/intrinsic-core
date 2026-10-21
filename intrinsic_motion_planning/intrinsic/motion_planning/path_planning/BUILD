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

# gazelle:resolve proto intrinsic/messages/geometry_types.proto @intrinsic_apis//intrinsic/messages:messages_proto
# gazelle:resolve proto go intrinsic/messages/geometry_types.proto @intrinsic_apis//intrinsic/messages:messages_go_proto
# gazelle:resolve_regexp proto (intrinsic/motion_planning/path_planning(?:/.*)?)/(.*)\.proto //intrinsic_motion_planning/${1}:${2}_proto
# gazelle:resolve_regexp proto go (intrinsic/motion_planning/path_planning(?:/.*)?)/(.*)\.proto //intrinsic_motion_planning/${1}:${2}_go_proto
load("@com_google_protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("//bazel:cc_macros.bzl", "cc_library")
load("//bazel:go_macros.bzl", "go_proto_library")
load("//bazel:protos.bzl", "proto_library")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "robot_chain",
    srcs = ["robot_chain.cc"],
    hdrs = ["robot_chain.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/world:entity_id",
    ],
)

cc_library(
    name = "kinematics_system_proxy",
    srcs = ["kinematics_system_proxy.cc"],
    hdrs = ["kinematics_system_proxy.h"],
    deps = [
        "//intrinsic/math/numopt:constraint_interface",
        "//intrinsic/world/collision:collision_checker",
        "//intrinsic/world/collision:collision_types",
        "//intrinsic_control/intrinsic/icon/control/parts:feature_interfaces",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits_xd",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/util:eigen",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world:entity_id",
        "//intrinsic_sdk/intrinsic/world:labels",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/hash",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_library(
    name = "kinematics_system_proxy_util",
    srcs = ["kinematics_system_proxy_util.cc"],
    hdrs = ["kinematics_system_proxy_util.h"],
    deps = [
        ":concurrent_proxy",
        ":dof_view_proxy",
        ":kinematics_system_proxy",
        ":path_planner_definitions",
        ":path_segment",
        ":world_kinematics_system_proxy",
        "//intrinsic/world",
        "//intrinsic/world:configuration_validation",
        "//intrinsic/world/collision:collision_context_cc_proto",
        "//intrinsic/world/objects:object_world",
        "//intrinsic_motion_planning/intrinsic/motion_planning/proto:motion_specification_proto_utils",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world:entity_id",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_ids",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/motion_planning/proto/v1:geometric_constraints_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:collision_checker_config_cc_proto",
    ],
)

cc_library(
    name = "path_planner",
    srcs = ["path_planner.cc"],
    hdrs = ["path_planner.h"],
    deps = [
        ":kinematics_system_proxy",
        ":path_planner_cc_proto",
        ":path_planner_definitions",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning/data_structures:path_planner_graph",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/base:no_destructor",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:optional",
        "@com_google_protobuf//:protobuf",
        "@cppregpattern//:registry",
    ],
)

proto_library(
    name = "path_planner_proto",
    srcs = ["path_planner.proto"],
    # keep
    strip_import_prefix = "/intrinsic_motion_planning",
    deps = [
        "@intrinsic_apis//intrinsic/icon/proto:joint_space_proto",
        "@intrinsic_apis//intrinsic/messages:messages_proto",
    ],
)

go_proto_library(
    name = "path_planner_go_proto",
    importpath = "intrinsic/motion_planning/path_planning/path_planner_go_proto",
    proto = ":path_planner_proto",
    deps = [
        "@intrinsic_apis//intrinsic/icon/proto:joint_space_go_proto",
        "@intrinsic_apis//intrinsic/messages:messages_go_proto",
    ],
)

cc_proto_library(
    name = "path_planner_cc_proto",
    deps = [":path_planner_proto"],
)

cc_library(
    name = "path_planner_definitions",
    hdrs = ["path_planner_definitions.h"],
    deps = [
        "//intrinsic_control/intrinsic/icon/control/parts:feature_interfaces",
        "//intrinsic_kinematics/intrinsic/kinematics/ik_solvers/kinematic_chain:kinematic_chain_newton_raphson_ik_solver",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_library(
    name = "path_planning_utils",
    srcs = ["path_planning_utils.cc"],
    hdrs = ["path_planning_utils.h"],
    deps = [
        ":path_planner_definitions",
        "//intrinsic_control/intrinsic/icon/control/parts:feature_interfaces",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_kinematics/intrinsic/kinematics:group",
        "//intrinsic_kinematics/intrinsic/kinematics:state",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits_xd",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
    ] + [],
)

cc_library(
    name = "validators",
    srcs = ["validators.cc"],
    hdrs = ["validators.h"],
    deps = [
        ":kinematics_system_proxy",
        ":path_planner_definitions",
        ":validators_config_cc_proto",
        "//intrinsic/world/collision:collision_types",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/util/proto:parse_text_proto",
        "//intrinsic_sdk/intrinsic/util/status:ret_check",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_proto_library(
    name = "validators_config_cc_proto",
    deps = [":validators_config_proto"],
)

proto_library(
    name = "validators_config_proto",
    srcs = ["validators_config.proto"],
    # keep
    strip_import_prefix = "/intrinsic_motion_planning",
    deps = ["@com_google_protobuf//:any_proto"],
)

proto_library(
    name = "path_segment_proto",
    srcs = ["path_segment.proto"],
    # keep
    strip_import_prefix = "/intrinsic_motion_planning",
    deps = [
        "//intrinsic/world/collision:collision_context_proto",
        "//intrinsic_motion_planning/intrinsic/motion_planning/proto/v1:motion_events_proto",
        "@intrinsic_apis//intrinsic/icon/proto:cart_space_proto",
        "@intrinsic_apis//intrinsic/icon/proto:joint_space_proto",
        "@intrinsic_apis//intrinsic/kinematics/types:joint_limits_proto",
        "@intrinsic_apis//intrinsic/math/proto:pose_proto",
    ],
)

cc_proto_library(
    name = "path_segment_cc_proto",
    deps = [":path_segment_proto"],
)

go_proto_library(
    name = "path_segment_go_proto",
    importpath = "intrinsic/motion_planning/path_planning/path_segment_go_proto",
    proto = ":path_segment_proto",
    deps = [
        "//intrinsic/world/collision:collision_context_go_proto",
        "//intrinsic_motion_planning/intrinsic/motion_planning/proto/v1:motion_events_go_proto",
        "@intrinsic_apis//intrinsic/icon/proto:cart_space_go_proto",
        "@intrinsic_apis//intrinsic/icon/proto:joint_space_go_proto",
        "@intrinsic_apis//intrinsic/kinematics/types:joint_limits_go_proto",
        "@intrinsic_apis//intrinsic/math/proto:pose_go_proto",
    ],
)

cc_library(
    name = "world_kinematics_system_proxy",
    srcs = ["world_kinematics_system_proxy.cc"],
    hdrs = ["world_kinematics_system_proxy.h"],
    deps = [
        ":kinematics_system_proxy",
        "//intrinsic/eigenmath:pseudo_inverse",
        "//intrinsic/eigenmath:random_number_utils",
        "//intrinsic/math/numopt:constraint_interface",
        "//intrinsic/world",
        "//intrinsic/world:configuration_validation",
        "//intrinsic/world:grouping",
        "//intrinsic/world:kinematic_world_views",
        "//intrinsic/world/collision:collision_checker",
        "//intrinsic/world/collision:collision_context_cc_proto",
        "//intrinsic/world/collision:collision_types",
        "//intrinsic/world/collision/util:collision_world_util",
        "//intrinsic/world/objects:object_world",
        "//intrinsic/world/util:kinematic_world_util",
        "//intrinsic_control/intrinsic/icon/control/parts:feature_interfaces",
        "//intrinsic_control/intrinsic/icon/control/parts:new_manipulator_kinematics",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_kinematics/intrinsic/kinematics:group",
        "//intrinsic_kinematics/intrinsic/kinematics/ik:inverse_kinematics_factory",
        "//intrinsic_kinematics/intrinsic/kinematics/ik:inverse_kinematics_interface",
        "//intrinsic_kinematics/intrinsic/kinematics/types:check_joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits_xd",
        "//intrinsic_motion_planning/intrinsic/motion_planning/proto:motion_specification_proto_utils",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/eigenmath:clamp",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/util:macros",
        "//intrinsic_sdk/intrinsic/util/status:ret_check",
        "//intrinsic_sdk/intrinsic/util/status:status_builder",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world:entity_id",
        "//intrinsic_sdk/intrinsic/world:labels",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:optional",
        "@com_gitlab_libeigen_eigen//:eigen",
        "@intrinsic_apis//intrinsic/motion_planning/proto/v1:geometric_constraints_cc_proto",
        "@intrinsic_apis//intrinsic/motion_planning/proto/v1:motion_planning_error_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:collision_checker_config_cc_proto",
    ],
)

cc_library(
    name = "concurrent_proxy",
    srcs = ["concurrent_proxy.cc"],
    hdrs = ["concurrent_proxy.h"],
    deps = [
        ":kinematics_system_proxy",
        ":world_kinematics_system_proxy",
        "//intrinsic/math/numopt:constraint_interface",
        "//intrinsic/world",
        "//intrinsic/world:configuration_validation",
        "//intrinsic/world:grouping",
        "//intrinsic/world:kinematic_world_views",
        "//intrinsic/world/collision:collision_checker",
        "//intrinsic/world/collision:collision_context_cc_proto",
        "//intrinsic/world/objects:object_world",
        "//intrinsic_control/intrinsic/icon/control/parts:feature_interfaces",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits_xd",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/util/thread",
        "//intrinsic_sdk/intrinsic/util/thread:concurrent_queue",
        "//intrinsic_sdk/intrinsic/util/thread:stop_token",
        "//intrinsic_sdk/intrinsic/world:entity_id",
        "//intrinsic_sdk/intrinsic/world:labels",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/types:span",
        "@intrinsic_apis//intrinsic/motion_planning/proto/v1:geometric_constraints_cc_proto",
        "@intrinsic_apis//intrinsic/motion_planning/proto/v1:motion_planning_error_cc_proto",
    ],
)

cc_library(
    name = "dof_view_proxy",
    srcs = ["dof_view_proxy.cc"],
    hdrs = ["dof_view_proxy.h"],
    deps = [
        ":kinematics_system_proxy",
        "//intrinsic/eigenmath:random_number_utils",
        "//intrinsic/math/numopt:constraint_interface",
        "//intrinsic/world",
        "//intrinsic/world:grouping",
        "//intrinsic/world:kinematic_world_views",
        "//intrinsic/world/collision:collision_checker",
        "//intrinsic/world/collision:collision_context_cc_proto",
        "//intrinsic/world/collision:collision_types",
        "//intrinsic/world/collision/util:collision_world_util",
        "//intrinsic/world/objects:object_world",
        "//intrinsic/world/util:kinematic_world_util",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_kinematics/intrinsic/kinematics/types:check_joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits_xd",
        "//intrinsic_motion_planning/intrinsic/motion_planning/proto:motion_specification_proto_utils",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/util/status:ret_check",
        "//intrinsic_sdk/intrinsic/util/status:status_builder",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world:entity_id",
        "//intrinsic_sdk/intrinsic/world:labels",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:optional",
        "@intrinsic_apis//intrinsic/motion_planning/proto/v1:geometric_constraints_cc_proto",
        "@intrinsic_apis//intrinsic/motion_planning/proto/v1:motion_planning_error_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:collision_checker_config_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:entity_search_cc_proto",
    ],
)

cc_library(
    name = "all_state_samplers",
    deps = [
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning/data_structures:prm_heuristic_state_sampler",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning/data_structures:rrt_scoring_heuristic_state_sampler",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning/data_structures:uniform_state_sampler",
    ],
    alwayslink = 1,  # Required for go/registerer
)

cc_library(
    name = "all_path_planners",
    deps = [
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning/planners:joint_interpolation_path_planner",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning/planners:joint_shortcutter",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning/planners:linear_cartesian_motion_path_planner",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning/planners:rrt_connect_path_planner",
    ] + [],
    alwayslink = 1,  # Required for go/registerer
)

cc_library(
    name = "path_segment",
    srcs = ["path_segment.cc"],
    hdrs = ["path_segment.h"],
    deps = [
        ":path_segment_cc_proto",
        "//intrinsic/util:unique_id",
        "//intrinsic/world/collision:collision_context_cc_proto",
        "//intrinsic/world/collision:rule_matching",
        "//intrinsic_control/intrinsic/icon/proto:cart_space_conversion",
        "//intrinsic_control/intrinsic/icon/proto:eigen_conversion",
        "//intrinsic_kinematics/intrinsic/kinematics/types:cartesian_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_motion_planning/intrinsic/motion_planning/proto/v1:motion_events_cc_proto",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:proto_conversion",
        "//intrinsic_sdk/intrinsic/util:eigen",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:string_view",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/icon/proto:joint_space_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:collision_action_cc_proto",
    ],
)

cc_library(
    name = "interpolation",
    srcs = ["interpolation.cc"],
    hdrs = ["interpolation.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/util:eigen",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
)

go_proto_library(
    name = "validators_config_go_proto",
    importpath = "intrinsic/motion_planning/path_planning/validators_config_go_proto",
    proto = ":validators_config_proto",
)
