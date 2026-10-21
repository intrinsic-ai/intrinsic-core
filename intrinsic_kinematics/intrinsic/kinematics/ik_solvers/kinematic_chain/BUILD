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

package(default_visibility = ["//intrinsic:public_api_users"])

cc_library(
    name = "kinematic_chain_ik_solver",
    hdrs = [
        "kinematic_chain_ik_solver.h",
    ],
    deps = [
        "//intrinsic_kinematics/intrinsic/kinematics:group",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:pose3",
    ],
)

cc_library(
    name = "kinematic_chain_newton_raphson_ik_solver",
    srcs = [
        "kinematic_chain_newton_raphson_ik_solver.cc",
    ],
    hdrs = [
        "kinematic_chain_newton_raphson_ik_solver.h",
    ],
    deps = [
        ":kinematic_chain_ik_solver",
        ":velocity_ik",
        "//intrinsic_control/intrinsic/icon/utils:log",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_kinematics/intrinsic/kinematics",
        "//intrinsic_kinematics/intrinsic/kinematics:group",
        "//intrinsic_kinematics/intrinsic/kinematics:joint_wrapping",
        "//intrinsic_kinematics/intrinsic/kinematics:math",
        "//intrinsic_kinematics/intrinsic/kinematics:state",
        "//intrinsic_kinematics/intrinsic/kinematics:state_generation",
        "//intrinsic_kinematics/intrinsic/kinematics/types:check_joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/util/time",
        "//intrinsic_sdk/intrinsic/util/time:timer",
        "@abseil-cpp//absl/functional:function_ref",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/time",
    ],
)

cc_library(
    name = "velocity_ik",
    hdrs = [
        "velocity_ik.h",
    ],
    deps = [
        "//intrinsic_kinematics/intrinsic/kinematics:math",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings:str_format",
    ],
)

cc_library(
    name = "kinematic_chain_qp_ik_solver",
    srcs = [
        "kinematic_chain_qp_ik_solver.cc",
    ],
    hdrs = [
        "kinematic_chain_qp_ik_solver.h",
    ],
    deps = [
        ":kinematic_chain_ik_solver",
        "//intrinsic/eigenmath:random_number_utils",
        "//intrinsic_control/intrinsic/icon/utils:log",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_kinematics/intrinsic/kinematics",
        "//intrinsic_kinematics/intrinsic/kinematics:group",
        "//intrinsic_kinematics/intrinsic/kinematics:joint_wrapping",
        "//intrinsic_kinematics/intrinsic/kinematics:math",
        "//intrinsic_kinematics/intrinsic/kinematics:state",
        "//intrinsic_kinematics/intrinsic/kinematics/ik_solvers:ik_solver_utils",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/util:eigen",
        "//intrinsic_sdk/intrinsic/util/time",
        "//intrinsic_sdk/intrinsic/util/time:timer",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/random",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/time",
        "@osqp-cpp//:osqp++",
    ],
)
