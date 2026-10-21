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
    name = "dynamics_factory",
    srcs = [
        "dynamics_factory.cc",
    ],
    hdrs = [
        "dynamics_factory.h",
    ],
    deps = [
        ":rigid_body_interface",
        "//intrinsic_control/intrinsic/icon/utils:realtime_guard",
        "//intrinsic_kinematics/intrinsic/kinematics:skeleton",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
    ],
)

cc_library(
    name = "rigid_body_interface",
    hdrs = ["rigid_body_interface.h"],
    deps = [
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_kinematics/intrinsic/kinematics",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:twist",
    ],
)

cc_library(
    name = "mock_rigid_body_interface",
    testonly = True,
    hdrs = ["mock_rigid_body_interface.h"],
    deps = [
        ":rigid_body_interface",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_kinematics/intrinsic/kinematics",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:twist",
        "@abseil-cpp//absl/strings",
        "@com_google_googletest//:gtest",
    ],
)

cc_library(
    name = "robotics_library_dynamics_creator",
    srcs = ["robotics_library_dynamics_creator.cc"],
    hdrs = ["robotics_library_dynamics_creator.h"],
    deps = [
        ":rigid_body_interface",
        ":robotics_library_dynamics",
        ":validate_inertial_parameters",
        "//intrinsic_control/intrinsic/icon/utils:constants",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_kinematics/intrinsic/kinematics",
        "//intrinsic_kinematics/intrinsic/kinematics:model_interface",
        "//intrinsic_kinematics/intrinsic/kinematics:skeleton",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@roboticslibrary.rl//:mdl",
    ],
)

cc_library(
    name = "register_robotics_library_dynamics_creator",
    srcs = ["register_robotics_library_dynamics_creator.cc"],
    visibility = ["//intrinsic_control/intrinsic/icon:icon_client_users"],
    deps = [
        ":dynamics_factory",
        ":robotics_library_dynamics_creator",
    ],
    alwayslink = True,
)

cc_library(
    name = "robotics_library_dynamics",
    srcs = ["robotics_library_dynamics.cc"],
    hdrs = ["robotics_library_dynamics.h"],
    deps = [
        ":rigid_body_interface",
        "//intrinsic/math:pluecker_transform",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_kinematics/intrinsic/kinematics",
        "//intrinsic_kinematics/intrinsic/kinematics:model_interface",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:twist",
        "@abseil-cpp//absl/strings",
        "@com_gitlab_libeigen_eigen//:eigen",
        "@roboticslibrary.rl//:math",
        "@roboticslibrary.rl//:mdl",
    ],
)

cc_library(
    name = "validate_inertial_parameters",
    srcs = ["validate_inertial_parameters.cc"],
    hdrs = ["validate_inertial_parameters.h"],
    deps = [
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_kinematics/intrinsic/kinematics",
        "//intrinsic_kinematics/intrinsic/kinematics:model_interface",
        "//intrinsic_kinematics/intrinsic/kinematics:validate_link_parameters",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "validate_rigid_body_interface",
    srcs = ["validate_rigid_body_interface.cc"],
    hdrs = ["validate_rigid_body_interface.h"],
    deps = [
        ":rigid_body_interface",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
    ],
)
