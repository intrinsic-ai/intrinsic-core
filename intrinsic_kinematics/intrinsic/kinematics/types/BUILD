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

package(default_visibility = [
    "//visibility:public",
])

cc_library(
    name = "state_rn",
    hdrs = ["state_rn.h"],
    deps = [
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/util:aggregate_type",
        "@abseil-cpp//absl/log:check",
        "@com_gitlab_libeigen_eigen//:eigen",
    ],
)

cc_library(
    name = "joint_state",
    hdrs = ["joint_state.h"],
    deps = [":state_rn"],
)

cc_library(
    name = "to_fixed_string",
    srcs = ["to_fixed_string.cc"],
    hdrs = ["to_fixed_string.h"],
    deps = [
        ":cartesian_limits",
        ":joint_limits",
        ":state_rn",
        "//intrinsic_control/intrinsic/icon/utils:fixed_str_cat",
        "//intrinsic_control/intrinsic/icon/utils:fixed_string",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "cart_state",
    hdrs = [
        "cart_state.h",
    ],
    deps = [
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/util:aggregate_type",
    ],
)

cc_library(
    name = "joint_limits",
    srcs = ["joint_limits.cc"],
    hdrs = ["joint_limits.h"],
    deps = [
        "//intrinsic_control/intrinsic/icon/proto:eigen_conversion",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@intrinsic_apis//intrinsic/kinematics/types:joint_limits_cc_proto",
    ],
)

cc_library(
    name = "joint_limits_xd",
    srcs = ["joint_limits_xd.cc"],
    hdrs = ["joint_limits_xd.h"],
    deps = [
        ":joint_limits",
        "//intrinsic_control/intrinsic/icon/proto:eigen_conversion",
        "//intrinsic_control/intrinsic/icon/testing:realtime_annotations",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@intrinsic_apis//intrinsic/kinematics/types:joint_limits_cc_proto",
    ],
)

cc_library(
    name = "dynamic_limits_check_mode",
    srcs = ["dynamic_limits_check_mode.cc"],
    hdrs = ["dynamic_limits_check_mode.h"],
    deps = [
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@intrinsic_apis//intrinsic/kinematics/types:dynamic_limits_check_mode_cc_proto",
    ],
)

cc_library(
    name = "joint_limits_matcher",
    testonly = True,
    hdrs = ["joint_limits_matcher.h"],
    deps = [
        ":is_approx_with_inf",
        ":to_fixed_string",
        "//intrinsic/eigenmath:type_checks",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "@com_google_googletest//:gtest",
    ],
)

cc_library(
    name = "cartesian_limits",
    srcs = ["cartesian_limits.cc"],
    hdrs = ["cartesian_limits.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/eigenmath",
    ],
)

cc_library(
    name = "check_joint_limits",
    srcs = ["check_joint_limits.cc"],
    hdrs = ["check_joint_limits.h"],
    deps = [
        ":joint_state",
        "//intrinsic_control/intrinsic/icon/utils:fixed_string",
        "//intrinsic_control/intrinsic/icon/utils:log",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/strings:string_view",
    ],
)

cc_library(
    name = "check_cartesian_limits",
    srcs = ["check_cartesian_limits.cc"],
    hdrs = ["check_cartesian_limits.h"],
    deps = [
        ":cart_state",
        ":cartesian_limits",
        "//intrinsic_control/intrinsic/icon/utils:log",
    ],
)

cc_library(
    name = "joint_trajectories",
    srcs = ["joint_trajectories.cc"],
    hdrs = ["joint_trajectories.h"],
    deps = [
        ":dynamic_limits_check_mode",
        ":joint_state",
        "//intrinsic/math:linspace",
        "//intrinsic/math:time_series_utils",
        "//intrinsic/math/trajectory:discretized_trajectory",
        "//intrinsic_control/intrinsic/icon/utils:fixed_str_cat",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_sdk/intrinsic/math:almost_equals",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_library(
    name = "is_approx_with_inf",
    testonly = True,
    srcs = ["is_approx_with_inf.cc"],
    hdrs = ["is_approx_with_inf.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/eigenmath",
        "@com_google_googletest//:gtest",
    ],
)

cc_library(
    name = "to_string",
    srcs = ["to_string.cc"],
    hdrs = ["to_string.h"],
    deps = ["//intrinsic_sdk/intrinsic/eigenmath"],
)
