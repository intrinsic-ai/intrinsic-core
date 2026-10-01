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
    name = "math",
    srcs = [
        "math.cc",
    ],
    hdrs = [
        "math.h",
    ],
    deps = [
        "//intrinsic_control/intrinsic/icon/utils:log",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/eigenmath:manifolds",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "@abseil-cpp//absl/log",
    ],
)

cc_library(
    name = "kinematics",
    srcs = [
        "coordinate_frame.cc",
        "elements.cc",
        "joint.cc",
        "link.cc",
    ],
    hdrs = [
        "coordinate_frame.h",
        "elements.h",
        "joint.h",
        "link.h",
    ],
    deps = [
        "//intrinsic_control/intrinsic/icon/testing:realtime_annotations",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:almost_equals",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
        "@or_tools//ortools/base:strong_int",
    ],
)

cc_library(
    name = "model_interface",
    srcs = [
        "model_interface.cc",
        "model_interface_details.cc",
    ],
    hdrs = [
        "model_interface.h",
        "model_interface_details.h",
    ],
    deps = [
        ":kinematics",
        ":state_values",
        "//intrinsic/eigenmath:invert_lower_triangular_matrix",
        "//intrinsic/util:fixed_vector",
        "//intrinsic/util/status:status_builder",
        "//intrinsic/util/status:status_macros",
        "//intrinsic_control/intrinsic/icon/testing:realtime_annotations",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state",
        "//intrinsic_kinematics/intrinsic/kinematics/types:to_fixed_string",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/eigenmath:clamp",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:node_hash_map",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_library(
    name = "state_values",
    hdrs = ["state_values.h"],
    deps = [
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state",
        "//intrinsic_kinematics/intrinsic/kinematics/types:to_fixed_string",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "analytic_jacobian_utils",
    srcs = ["analytic_jacobian_utils.cc"],
    hdrs = ["analytic_jacobian_utils.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/eigenmath:manifolds",
        "//intrinsic_sdk/intrinsic/eigenmath:skew_symmetric_matrix",
        "//intrinsic_sdk/intrinsic/eigenmath:so3",
        "//intrinsic_sdk/intrinsic/math:ipow",
    ],
)

cc_library(
    name = "state",
    srcs = ["state.cc"],
    hdrs = ["state.h"],
    deps = [
        ":analytic_jacobian_utils",
        ":kinematics",
        ":model_interface",
        ":state_values",
        "//intrinsic/math:pluecker_transform",
        "//intrinsic/util:fixed_vector",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_kinematics/intrinsic/kinematics/types:check_joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/eigenmath:skew_symmetric_matrix",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "@abseil-cpp//absl/log:check",
    ],
)

cc_library(
    name = "skeleton",
    srcs = ["skeleton.cc"],
    hdrs = ["skeleton.h"],
    deps = [
        ":kinematics",
        ":model_interface",
        "//intrinsic/util/status:status_macros",
        "//intrinsic_control/intrinsic/icon/testing:realtime_annotations",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "skeleton_util",
    srcs = ["skeleton_util.cc"],
    hdrs = ["skeleton_util.h"],
    deps = [
        ":skeleton",
        "//intrinsic/util/status:status_macros",
        "//intrinsic/world/objects:object_world",
        "//intrinsic_sdk/intrinsic/world:entity_id",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "group",
    srcs = [
        "chain.cc",
        "group.cc",
    ],
    hdrs = [
        "chain.h",
        "group.h",
    ],
    deps = [
        ":kinematics",
        ":model_interface",
        "//intrinsic/util/status:status_builder",
        "//intrinsic/util/status:status_macros",
        "//intrinsic_control/intrinsic/icon/testing:realtime_annotations",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:optional",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_library(
    name = "test_utils",
    testonly = 1,
    srcs = ["test_utils.cc"],
    hdrs = ["test_utils.h"],
    deps = [
        ":kinematics",
        ":model_interface",
        ":skeleton",
        "//intrinsic/util/status:status_macros",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_matchers",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
        "@com_google_cel_cpp//internal:testing_no_main",
        "@com_google_googletest//:gtest",
    ],
)

cc_library(
    name = "state_generation",
    hdrs = ["state_generation.h"],
    deps = [
        "//intrinsic/eigenmath:random_number_utils",
        "//intrinsic/util:macros",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
    ],
)

cc_library(
    name = "joint_wrapping",
    srcs = ["joint_wrapping.cc"],
    hdrs = ["joint_wrapping.h"],
    deps = [
        ":kinematics",
        ":model_interface",
        "//intrinsic_control/intrinsic/icon/utils:log",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_kinematics/intrinsic/kinematics/types:check_joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state",
        "//intrinsic_kinematics/intrinsic/kinematics/types:to_fixed_string",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_library(
    name = "validate_link_parameters",
    srcs = ["validate_link_parameters.cc"],
    hdrs = ["validate_link_parameters.h"],
    deps = [
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:inertia_utils",
    ],
)
