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

load("@com_google_protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("@io_bazel_rules_go//proto:def.bzl", "go_proto_library")
load("//bazel:cc_macros.bzl", "cc_library")
load("//bazel:protos.bzl", "proto_library")

package(default_visibility = ["//visibility:public"])

cc_proto_library(
    name = "joint_interpolation_configs_cc_proto",
    deps = [
        ":joint_interpolation_configs_proto",
    ],
)

proto_library(
    name = "joint_interpolation_configs_proto",
    srcs = ["joint_interpolation_configs.proto"],
    # keep
    strip_import_prefix = "/intrinsic_motion_planning",
    deps = [
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning:validators_config_proto",
    ],
)

cc_proto_library(
    name = "linear_cartesian_motion_path_planner_configs_cc_proto",
    deps = [
        ":linear_cartesian_motion_path_planner_configs_proto",
    ],
)

proto_library(
    name = "linear_cartesian_motion_path_planner_configs_proto",
    srcs = ["linear_cartesian_motion_path_planner_configs.proto"],
    # keep
    strip_import_prefix = "/intrinsic_motion_planning",
    deps = [
        "@intrinsic_apis//intrinsic/math/proto:pose_proto",
    ],
)

cc_proto_library(
    name = "pipeline_configs_cc_proto",
    deps = [
        ":pipeline_configs_proto",
    ],
)

proto_library(
    name = "pipeline_configs_proto",
    srcs = ["pipeline_configs.proto"],
    # keep
    strip_import_prefix = "/intrinsic_motion_planning",
    deps = [
        "@com_google_protobuf//:any_proto",
    ],
)

cc_proto_library(
    name = "rrt_configs_cc_proto",
    deps = [
        ":rrt_configs_proto",
    ],
)

proto_library(
    name = "rrt_configs_proto",
    srcs = ["rrt_configs.proto"],
    # keep
    strip_import_prefix = "/intrinsic_motion_planning",
    deps = [
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning:validators_config_proto",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning/data_structures:state_sampler_configs_proto",
    ],
)

cc_proto_library(
    name = "shortcutter_configs_cc_proto",
    deps = [
        ":shortcutter_configs_proto",
    ],
)

proto_library(
    name = "shortcutter_configs_proto",
    srcs = ["shortcutter_configs.proto"],
    # keep
    strip_import_prefix = "/intrinsic_motion_planning",
    deps = [
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning:validators_config_proto",
    ],
)

cc_library(
    name = "joint_shortcutter",
    srcs = ["joint_shortcutter.cc"],
    hdrs = ["joint_shortcutter.h"],
    deps = [
        ":shortcutter_configs_cc_proto",
        "//intrinsic/stats:scoped_span",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning:kinematics_system_proxy",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning:path_planner",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning:path_planner_cc_proto",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning:path_planner_definitions",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning:path_planning_utils",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning:validators",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning:validators_config_cc_proto",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning/analytic_geometry:intersections",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning/data_structures:path_planner_graph",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/time",
        "@com_google_protobuf//:protobuf",
    ],
    alwayslink = 1,
)

cc_library(
    name = "pipeline",
    srcs = ["pipeline.cc"],
    hdrs = ["pipeline.h"],
    deps = [
        ":pipeline_configs_cc_proto",
        ":rrt_configs_cc_proto",
        ":shortcutter_configs_cc_proto",
        "//intrinsic_motion_planning/intrinsic/motion_planning/motion_planner:motion_planner_flags",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning:path_planner",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning:validators",
        "//intrinsic_sdk/intrinsic/util/proto:get_text_proto",
        "//intrinsic_sdk/intrinsic/util/proto:parse_text_proto",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status:statusor",
        "@com_google_protobuf//:protobuf",
    ],
    alwayslink = 1,
)

cc_library(
    name = "pipeline_path_planner",
    srcs = ["pipeline_path_planner.cc"],
    hdrs = ["pipeline_path_planner.h"],
    deps = [
        ":pipeline",
        ":pipeline_configs_cc_proto",
        "//intrinsic/stats:scoped_span",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning:kinematics_system_proxy",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning:path_planner",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning:path_planner_cc_proto",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning:path_planner_definitions",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning/data_structures:path_planner_graph",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/types:optional",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_library(
    name = "rrt_connect",
    srcs = ["rrt_connect.cc"],
    hdrs = ["rrt_connect.h"],
    deps = [
        ":rrt_configs_cc_proto",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning:path_planner_definitions",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning:path_planning_utils",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning/data_structures:path_planner_graph",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning/data_structures:planner_graph_cc_proto",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning/data_structures:state_sampler",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/util:eigen",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:optional",
    ],
)

cc_library(
    name = "rrt_connect_path_planner",
    srcs = ["rrt_connect_path_planner.cc"],
    hdrs = ["rrt_connect_path_planner.h"],
    deps = [
        ":pipeline_configs_cc_proto",
        ":rrt_configs_cc_proto",
        ":rrt_connect",
        ":validation",
        "//intrinsic/stats:scoped_span",
        "//intrinsic_motion_planning/intrinsic/motion_planning/motion_planner:motion_planner_flags",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning:kinematics_system_proxy",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning:kinematics_system_proxy_util",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning:path_planner",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning:path_planner_cc_proto",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning:path_planner_definitions",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning:path_planning_utils",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning:robot_chain",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning:validators",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning/data_structures:path_planner_graph",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning/data_structures:planner_graph_cc_proto",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning/data_structures:state_sampler",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning/data_structures:state_sampler_utils",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/util:eigen",
        "//intrinsic_sdk/intrinsic/util/proto:parse_text_proto",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/types:optional",
        "@com_google_protobuf//:protobuf",
    ],
    alwayslink = 1,
)

cc_library(
    name = "validation",
    srcs = ["validation.cc"],
    hdrs = ["validation.h"],
    deps = [
        "//intrinsic/world/collision:collision_checker",
        "//intrinsic/world/collision:collision_context_cc_proto",
        "//intrinsic/world/collision/util:make_rule_set",
        "//intrinsic/world/objects:object_world",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits_xd",
        "//intrinsic_motion_planning/intrinsic/motion_planning/motion_planner:motion_planning_error_utils",
        "//intrinsic_motion_planning/intrinsic/motion_planning/motion_planner:motion_planning_utils",
        "//intrinsic_motion_planning/intrinsic/motion_planning/motion_planner:robot_specification",
        "//intrinsic_motion_planning/intrinsic/motion_planning/motion_planner:trajectory_segment",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning:kinematics_system_proxy",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning:kinematics_system_proxy_util",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning:path_planner_definitions",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning:path_segment",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning:validators",
        "//intrinsic_motion_planning/intrinsic/motion_planning/proto:motion_specification_proto_utils",
        "//intrinsic_motion_planning/intrinsic/motion_planning/proto/v1:motion_planner_service_cc_proto",
        "//intrinsic_motion_planning/intrinsic/motion_planning/proto/v1:motion_specification_cc_proto",
        "//intrinsic_motion_planning/intrinsic/motion_planning/skills:motion_planning_error_util",
        "//intrinsic_motion_planning/intrinsic/motion_planning/trajectory_planning/topp:path_sample",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/util:eigen",
        "//intrinsic_sdk/intrinsic/util/status:ret_check",
        "//intrinsic_sdk/intrinsic/util/status:status_builder",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/types:span",
        "@intrinsic_apis//intrinsic/motion_planning/proto/v1:motion_planning_error_cc_proto",
        "@intrinsic_apis//intrinsic/motion_planning/proto/v1:robot_specification_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:collision_checker_config_cc_proto",
    ],
)

cc_library(
    name = "joint_sampling_utils",
    srcs = ["joint_sampling_utils.cc"],
    hdrs = ["joint_sampling_utils.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_library(
    name = "linear_cartesian_motion_path_planner",
    srcs = ["linear_cartesian_motion_path_planner.cc"],
    hdrs = ["linear_cartesian_motion_path_planner.h"],
    deps = [
        ":linear_cartesian_motion_path_planner_configs_cc_proto",
        ":validation",
        "//intrinsic/stats:scoped_span",
        "//intrinsic_control/intrinsic/icon/control/parts:feature_interfaces",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_kinematics/intrinsic/kinematics/types:dynamic_limits_check_mode",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_trajectories",
        "//intrinsic_motion_planning/intrinsic/motion_planning/motion_planner:motion_planning_error_utils",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning:kinematics_system_proxy",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning:path_planner",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning:path_planner_definitions",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning/data_structures:path_planner_graph",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning/parametric_curves:cartesian_parametric_curve",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning/parametric_curves:cartesian_polyline_bsplines_curve",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning/parametric_curves:parametric_curve_utils",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning/path_ik",
        "//intrinsic_motion_planning/intrinsic/motion_planning/trajectory_planning:validation",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:ipow",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:proto_conversion",
        "//intrinsic_sdk/intrinsic/util:eigen",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
        "@com_gitlab_libeigen_eigen//:eigen",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/icon/proto:joint_space_cc_proto",
        "@intrinsic_apis//intrinsic/motion_planning/proto/v1:motion_planning_error_cc_proto",
    ],
    alwayslink = 1,
)

cc_library(
    name = "joint_interpolation_path_planner",
    srcs = ["joint_interpolation_path_planner.cc"],
    hdrs = ["joint_interpolation_path_planner.h"],
    deps = [
        ":joint_interpolation_configs_cc_proto",
        ":validation",
        "//intrinsic/stats:scoped_span",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning:kinematics_system_proxy",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning:path_planner",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning:path_planner_cc_proto",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning:path_planner_definitions",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning:validators",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning/data_structures:path_planner_graph",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning/data_structures:planner_graph_cc_proto",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/util:eigen",
        "//intrinsic_sdk/intrinsic/util/proto:parse_text_proto",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/time",
        "@com_google_protobuf//:protobuf",
    ],
    alwayslink = 1,
)

go_proto_library(
    name = "joint_interpolation_configs_go_proto",
    importpath = "intrinsic/motion_planning/path_planning/planners/joint_interpolation_configs_go_proto",
    proto = ":joint_interpolation_configs_proto",
    deps = ["//intrinsic_motion_planning/intrinsic/motion_planning/path_planning:validators_config_go_proto"],
)

go_proto_library(
    name = "linear_cartesian_motion_path_planner_configs_go_proto",
    importpath = "intrinsic/motion_planning/path_planning/planners/linear_cartesian_motion_path_planner_configs_go_proto",
    proto = ":linear_cartesian_motion_path_planner_configs_proto",
    deps = ["@intrinsic_apis//intrinsic/math/proto:pose_go_proto"],
)

go_proto_library(
    name = "pipeline_configs_go_proto",
    importpath = "intrinsic/motion_planning/path_planning/planners/pipeline_configs_go_proto",
    proto = ":pipeline_configs_proto",
)

go_proto_library(
    name = "rrt_configs_go_proto",
    importpath = "intrinsic/motion_planning/path_planning/planners/rrt_configs_go_proto",
    proto = ":rrt_configs_proto",
    deps = [
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning:validators_config_go_proto",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning/data_structures:state_sampler_configs_go_proto",
    ],
)

go_proto_library(
    name = "shortcutter_configs_go_proto",
    importpath = "intrinsic/motion_planning/path_planning/planners/shortcutter_configs_go_proto",
    proto = ":shortcutter_configs_proto",
    deps = ["//intrinsic_motion_planning/intrinsic/motion_planning/path_planning:validators_config_go_proto"],
)
