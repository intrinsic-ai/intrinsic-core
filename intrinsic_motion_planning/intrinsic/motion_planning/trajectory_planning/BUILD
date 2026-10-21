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
    name = "interpolate_joint_trajectories",
    srcs = ["interpolate_joint_trajectories.cc"],
    hdrs = ["interpolate_joint_trajectories.h"],
    deps = [
        "//intrinsic/eigenmath:interpolation",
        "//intrinsic/math/spline:polynomial_spline",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_kinematics/intrinsic/kinematics/types:check_joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_trajectories",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
    ],
)

cc_library(
    name = "trajectory_utils",
    srcs = ["trajectory_utils.cc"],
    hdrs = ["trajectory_utils.h"],
    deps = [
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_kinematics/intrinsic/kinematics:group",
        "//intrinsic_kinematics/intrinsic/kinematics:state",
        "//intrinsic_kinematics/intrinsic/kinematics/types:cartesian_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:check_cartesian_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:check_joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:dynamic_limits_check_mode",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_trajectories",
        "//intrinsic_motion_planning/intrinsic/motion_planning/trajectory_planning/topp:path_sample",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:ipow",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_library(
    name = "optimized_toppra",
    srcs = ["optimized_toppra.cc"],
    hdrs = ["optimized_toppra.h"],
    deps = [
        "//intrinsic_control/intrinsic/icon/dynamics:rigid_body_interface",
        "//intrinsic_control/intrinsic/icon/dynamics:validate_rigid_body_interface",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_kinematics/intrinsic/kinematics:group",
        "//intrinsic_kinematics/intrinsic/kinematics/types:check_joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:dynamic_limits_check_mode",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_trajectories",
        "//intrinsic_kinematics/intrinsic/kinematics/types:to_fixed_string",
        "//intrinsic_motion_planning/intrinsic/motion_planning/trajectory_planning/topp:bspline_squared_path_velocity",
        "//intrinsic_motion_planning/intrinsic/motion_planning/trajectory_planning/topp:joint_optimization_options_cc_proto",
        "//intrinsic_motion_planning/intrinsic/motion_planning/trajectory_planning/topp:path_sample",
        "//intrinsic_motion_planning/intrinsic/motion_planning/trajectory_planning/topp:path_velocity_propagator",
        "//intrinsic_motion_planning/intrinsic/motion_planning/trajectory_planning/topp:topp_options",
        "//intrinsic_motion_planning/intrinsic/motion_planning/trajectory_planning/topp:topp_solver_commons",
        "//intrinsic_motion_planning/intrinsic/motion_planning/trajectory_planning/topp:topp_trajectory_result",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
        "@com_gitlab_libeigen_eigen//:eigen",
    ],
)

cc_library(
    name = "validation",
    srcs = ["validation.cc"],
    hdrs = ["validation.h"],
    deps = [
        "//intrinsic/world",
        "//intrinsic/world/collision:collision_context_cc_proto",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_trajectories",
        "//intrinsic_motion_planning/intrinsic/motion_planning/motion_planner:motion_planning_error_utils",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning:interpolation",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning:kinematics_system_proxy",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning:kinematics_system_proxy_util",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning/planners:validation",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/util:eigen",
        "//intrinsic_sdk/intrinsic/util/status:annotate",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world:entity_id",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings:str_format",
        "@intrinsic_apis//intrinsic/motion_planning/proto/v1:motion_planning_error_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:collision_checker_config_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:collision_settings_cc_proto",
    ],
)
