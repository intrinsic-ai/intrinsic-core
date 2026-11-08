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

cc_library(
    name = "cartesian_impedance_controller_test_utils",
    testonly = True,
    srcs = ["cartesian_impedance_controller_test_utils.cc"],
    hdrs = ["cartesian_impedance_controller_test_utils.h"],
    visibility = ["//intrinsic_control/intrinsic/icon:rtcl_users"],
    deps = [
        "//intrinsic_control/intrinsic/icon/actions:cartesian_impedance_parameters",
        "//intrinsic_control/intrinsic/icon/control/algorithms:cartesian_impedance_commons",
        "//intrinsic_kinematics/intrinsic/kinematics/types:cartesian_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/eigenmath:matchers",
        "//intrinsic_sdk/intrinsic/eigenmath:rotation_utils",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:twist",
        "@com_gitlab_libeigen_eigen//:eigen",
        "@com_google_googletest//:gtest",
    ],
)

cc_library(
    name = "cartesian_rangefinder_distance_controller_test_utils",
    testonly = True,
    hdrs = ["cartesian_rangefinder_distance_controller_test_utils.h"],
    visibility = ["//intrinsic_control/intrinsic/icon:rtcl_users"],
    deps = [
        "//intrinsic_control/intrinsic/icon/control/algorithms:cartesian_rangefinder_distance_controller",
        "//intrinsic_control/intrinsic/icon/control/algorithms:compute_critical_damping",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_kinematics/intrinsic/kinematics/types:cartesian_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:pose3",
    ],
)
