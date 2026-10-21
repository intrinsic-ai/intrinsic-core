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
    name = "inverse_kinematics_interface",
    hdrs = [
        "inverse_kinematics_interface.h",
    ],
    deps = [
        "//intrinsic_control/intrinsic/icon/testing:realtime_annotations",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_kinematics/intrinsic/kinematics",
        "//intrinsic_kinematics/intrinsic/kinematics:group",
        "//intrinsic_kinematics/intrinsic/kinematics:model_interface",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:optional",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_library(
    name = "chain_inverse_kinematics",
    srcs = [
        "chain_inverse_kinematics.cc",
    ],
    hdrs = [
        "chain_inverse_kinematics.h",
    ],
    deps = [
        ":inverse_kinematics_interface",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_kinematics/intrinsic/kinematics",
        "//intrinsic_kinematics/intrinsic/kinematics:group",
        "//intrinsic_kinematics/intrinsic/kinematics/ik_solvers:ik_solver_utils",
        "//intrinsic_kinematics/intrinsic/kinematics/ik_solvers/kinematic_chain:kinematic_chain_ik_solver",
        "//intrinsic_kinematics/intrinsic/kinematics/ik_solvers/kinematic_chain:kinematic_chain_newton_raphson_ik_solver",
        "//intrinsic_kinematics/intrinsic/kinematics/ik_solvers/kinematic_chain:kinematic_chain_qp_ik_solver",
        "//intrinsic_kinematics/intrinsic/kinematics/ik_solvers/kinematic_chain/damped_least_squares:kinematic_chain_damped_least_squares_ik_solver",
        "//intrinsic_kinematics/intrinsic/kinematics/types:check_joint_limits",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/types:optional",
    ],
)

cc_library(
    name = "spherical_wrist_inverse_kinematics",
    srcs = [
        "spherical_wrist_inverse_kinematics.cc",
    ],
    hdrs = [
        "spherical_wrist_inverse_kinematics.h",
    ],
    deps = [
        ":inverse_kinematics_interface",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_kinematics/intrinsic/kinematics",
        "//intrinsic_kinematics/intrinsic/kinematics:group",
        "//intrinsic_kinematics/intrinsic/kinematics:joint_wrapping",
        "//intrinsic_kinematics/intrinsic/kinematics:model_interface",
        "//intrinsic_kinematics/intrinsic/kinematics/ik_solvers:spherical_wrist_ik_solver",
        "//intrinsic_kinematics/intrinsic/kinematics/types:check_joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_library(
    name = "ur_inverse_kinematics",
    srcs = [
        "ur_inverse_kinematics.cc",
    ],
    hdrs = [
        "ur_inverse_kinematics.h",
    ],
    deps = [
        ":inverse_kinematics_interface",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_kinematics/intrinsic/kinematics",
        "//intrinsic_kinematics/intrinsic/kinematics:group",
        "//intrinsic_kinematics/intrinsic/kinematics:joint_wrapping",
        "//intrinsic_kinematics/intrinsic/kinematics:model_interface",
        "//intrinsic_kinematics/intrinsic/kinematics/ik_solvers:ur_ik_solver",
        "//intrinsic_kinematics/intrinsic/kinematics/types:check_joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/eigenmath:rotation_utils",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_library(
    name = "calibrated_ur_inverse_kinematics",
    srcs = ["calibrated_ur_inverse_kinematics.cc"],
    hdrs = ["calibrated_ur_inverse_kinematics.h"],
    data = [
        "//intrinsic_kinematics/intrinsic/kinematics/ur_proto_skeletons:robot_skeletons",
        # TODO(b/264577959): Remove this data dependency and pull from the world at runtime.
    ],
    deps = [
        ":inverse_kinematics_interface",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_kinematics/intrinsic/kinematics",
        "//intrinsic_kinematics/intrinsic/kinematics:group",
        "//intrinsic_kinematics/intrinsic/kinematics:joint_wrapping",
        "//intrinsic_kinematics/intrinsic/kinematics:model_interface",
        "//intrinsic_kinematics/intrinsic/kinematics:skeleton",
        "//intrinsic_kinematics/intrinsic/kinematics/ik_solvers:ur_ik_solver",
        "//intrinsic_kinematics/intrinsic/kinematics/ik_solvers/kinematic_chain:kinematic_chain_ik_solver",
        "//intrinsic_kinematics/intrinsic/kinematics/ik_solvers/kinematic_chain:kinematic_chain_newton_raphson_ik_solver",
        "//intrinsic_kinematics/intrinsic/kinematics/proto:kinematics_conversion",
        "//intrinsic_kinematics/intrinsic/kinematics/types:check_joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/util:eigen",
        "//intrinsic_sdk/intrinsic/util/path_resolver",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
        "@or_tools//ortools/base:file",
    ],
)

cc_library(
    name = "sanitize_ik",
    srcs = [
        "sanitize_ik.cc",
    ],
    hdrs = [
        "sanitize_ik.h",
    ],
    deps = [
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/types:span",
        "@com_gitlab_libeigen_eigen//:eigen",
    ],
)

cc_library(
    name = "inverse_kinematics_factory",
    srcs = [
        "inverse_kinematics_factory.cc",
    ],
    hdrs = [
        "inverse_kinematics_factory.h",
    ],
    deps = [
        ":inverse_kinematics_interface",
        "//intrinsic_control/intrinsic/icon/utils:realtime_guard",
        "//intrinsic_kinematics/intrinsic/kinematics:group",
        "//intrinsic_kinematics/intrinsic/kinematics/ik/constrained:constrained_ik_interface",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/synchronization",
    ],
)

cc_library(
    name = "ik_solvers",
    deps = [
        ":register_calibrated_ur_inverse_kinematics",
        ":register_chain_inverse_kinematics",
        ":register_rt_calibrated_ur_inverse_kinematics",
        ":register_spherical_wrist_inverse_kinematics",
        ":register_ur_inverse_kinematics",
    ] + [],
)

cc_library(
    name = "register_chain_inverse_kinematics",
    srcs = ["register_chain_inverse_kinematics.cc"],
    deps = [
        ":chain_inverse_kinematics",
        ":inverse_kinematics_factory",
    ],
    alwayslink = True,
)

cc_library(
    name = "register_spherical_wrist_inverse_kinematics",
    srcs = ["register_spherical_wrist_inverse_kinematics.cc"],
    deps = [
        ":inverse_kinematics_factory",
        ":inverse_kinematics_interface",
        ":spherical_wrist_inverse_kinematics",
        "//intrinsic_kinematics/intrinsic/kinematics:group",
        "@abseil-cpp//absl/log:check",
    ],
    alwayslink = True,
)

cc_library(
    name = "register_ur_inverse_kinematics",
    srcs = ["register_ur_inverse_kinematics.cc"],
    deps = [
        ":inverse_kinematics_factory",
        ":ur_inverse_kinematics",
    ],
    alwayslink = True,
)

cc_library(
    name = "register_calibrated_ur_inverse_kinematics",
    srcs = ["register_calibrated_ur_inverse_kinematics.cc"],
    deps = [
        ":calibrated_ur_inverse_kinematics",
        ":inverse_kinematics_factory",
    ],
    alwayslink = True,
)

cc_library(
    name = "fake_inverse_kinematics",
    testonly = True,
    srcs = [
        "fake_inverse_kinematics.cc",
    ],
    hdrs = [
        "fake_inverse_kinematics.h",
    ],
    deps = [
        ":inverse_kinematics_interface",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_kinematics/intrinsic/kinematics",
        "//intrinsic_kinematics/intrinsic/kinematics:group",
        "//intrinsic_kinematics/intrinsic/kinematics:model_interface",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/types:optional",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_library(
    name = "rt_calibrated_ur_inverse_kinematics",
    srcs = ["rt_calibrated_ur_inverse_kinematics.cc"],
    hdrs = ["rt_calibrated_ur_inverse_kinematics.h"],
    data = ["//intrinsic_kinematics/intrinsic/kinematics/ur_proto_skeletons:robot_skeletons"],
    deps = [
        ":inverse_kinematics_interface",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_kinematics/intrinsic/kinematics",
        "//intrinsic_kinematics/intrinsic/kinematics:group",
        "//intrinsic_kinematics/intrinsic/kinematics:joint_wrapping",
        "//intrinsic_kinematics/intrinsic/kinematics:model_interface",
        "//intrinsic_kinematics/intrinsic/kinematics:skeleton",
        "//intrinsic_kinematics/intrinsic/kinematics/ik_solvers:ur_ik_solver",
        "//intrinsic_kinematics/intrinsic/kinematics/ik_solvers/kinematic_chain:kinematic_chain_ik_solver",
        "//intrinsic_kinematics/intrinsic/kinematics/ik_solvers/kinematic_chain:kinematic_chain_newton_raphson_ik_solver",
        "//intrinsic_kinematics/intrinsic/kinematics/proto:kinematics_conversion",
        "//intrinsic_kinematics/intrinsic/kinematics/types:check_joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/util:eigen",
        "//intrinsic_sdk/intrinsic/util/path_resolver",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
        "@intrinsic_apis//intrinsic/kinematics/proto:skeleton_cc_proto",
        "@or_tools//ortools/base:file",
    ],
)

cc_library(
    name = "register_rt_calibrated_ur_inverse_kinematics",
    srcs = ["register_rt_calibrated_ur_inverse_kinematics.cc"],
    deps = [
        ":inverse_kinematics_factory",
        ":rt_calibrated_ur_inverse_kinematics",
    ],
    alwayslink = True,
)
