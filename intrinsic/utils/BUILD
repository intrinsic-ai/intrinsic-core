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
    name = "ik_helper",
    srcs = ["ik_helper.cc"],
    hdrs = ["ik_helper.h"],
    deps = [
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_kinematics/intrinsic/kinematics:group",
        "//intrinsic_kinematics/intrinsic/kinematics:skeleton",
        "//intrinsic_kinematics/intrinsic/kinematics/ik:ik_solvers",
        "//intrinsic_kinematics/intrinsic/kinematics/ik:inverse_kinematics_factory",
        "//intrinsic_kinematics/intrinsic/kinematics/ik:inverse_kinematics_interface",
        "//intrinsic_kinematics/intrinsic/kinematics/ik:sanitize_ik",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/util:eigen",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@or_tools//ortools/base",
    ],
)
