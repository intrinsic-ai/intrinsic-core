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
    name = "path_ik",
    srcs = ["path_ik.cc"],
    hdrs = ["path_ik.h"],
    deps = [
        "//intrinsic_control/intrinsic/icon/control/parts:feature_interfaces",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_kinematics/intrinsic/kinematics",
        "//intrinsic_kinematics/intrinsic/kinematics:group",
        "//intrinsic_kinematics/intrinsic/kinematics/ik:inverse_kinematics_interface",
        "//intrinsic_kinematics/intrinsic/kinematics/ik_solvers/kinematic_chain:kinematic_chain_newton_raphson_ik_solver",
        "//intrinsic_kinematics/intrinsic/kinematics/types:check_joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits_xd",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state",
        "//intrinsic_kinematics/intrinsic/kinematics/types:to_fixed_string",
        "//intrinsic_kinematics/intrinsic/kinematics/utils:compute_ik_util",
        "//intrinsic_motion_planning/intrinsic/motion_planning/motion_planner:motion_planning_error_utils",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning:kinematics_system_proxy",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning:path_planner_definitions",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning:path_planning_utils",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning/parametric_curves:cartesian_parametric_curve",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning/parametric_curves:cartesian_parametric_curve_utils",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning/parametric_curves:parametric_curve_utils",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning/planners:joint_sampling_utils",
        "//intrinsic_motion_planning/intrinsic/motion_planning/trajectory_planning/path_refinement:path_refinement_utils",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/eigenmath:clamp",
        "//intrinsic_sdk/intrinsic/eigenmath:pose3_utils",
        "//intrinsic_sdk/intrinsic/math:ipow",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/util:eigen",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
        "@intrinsic_apis//intrinsic/motion_planning/proto/v1:motion_planning_error_cc_proto",
    ],
)
