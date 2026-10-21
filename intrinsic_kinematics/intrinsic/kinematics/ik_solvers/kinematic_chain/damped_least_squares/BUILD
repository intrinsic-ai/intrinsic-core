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
    name = "ik_damped_least_squares",
    hdrs = [
        "ik_base.h",
        "ik_damped_least_squares.h",
    ],
    # We don't plan to expand the ik_base interface. So we limit to current use.
    visibility = ["//visibility:private"],
    deps = [
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_kinematics/intrinsic/kinematics",
        "//intrinsic_kinematics/intrinsic/kinematics:joint_wrapping",
        "//intrinsic_kinematics/intrinsic/kinematics:math",
        "//intrinsic_kinematics/intrinsic/kinematics:model_interface",
        "//intrinsic_kinematics/intrinsic/kinematics:state",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_sdk/intrinsic/math:units",
        "//intrinsic_sdk/intrinsic/util:eigen",
        "//intrinsic_sdk/intrinsic/util/time",
        "//intrinsic_sdk/intrinsic/util/time:timer",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/time",
    ],
)

cc_library(
    name = "kinematic_chain_damped_least_squares_ik_solver",
    srcs = [
        "kinematic_chain_damped_least_squares_ik_solver.cc",
    ],
    hdrs = [
        "kinematic_chain_damped_least_squares_ik_solver.h",
    ],
    deps = [
        ":ik_damped_least_squares",
        "//intrinsic_control/intrinsic/icon/utils:realtime_stack_trace",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_kinematics/intrinsic/kinematics:group",
        "//intrinsic_kinematics/intrinsic/kinematics:joint_wrapping",
        "//intrinsic_kinematics/intrinsic/kinematics:state_generation",
        "//intrinsic_kinematics/intrinsic/kinematics/ik_solvers/kinematic_chain:kinematic_chain_ik_solver",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:units",
        "//intrinsic_sdk/intrinsic/util/time",
        "//intrinsic_sdk/intrinsic/util/time:timer",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/time",
    ],
)
