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

package_group(
    name = "icon",
    packages = [
        "//intrinsic_control/intrinsic/icon/...",
    ],
)

cc_library(
    name = "compute_critical_damping",
    srcs = ["compute_critical_damping.cc"],
    hdrs = [
        "compute_critical_damping.h",
    ],
    visibility = ["//intrinsic_control/intrinsic/icon:rtcl_users"],
    deps = [
        "//intrinsic/math:symmetric_positive_definite_matrix_squareroot",
        "//intrinsic_control/intrinsic/icon/utils:fixed_str_cat",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_sdk/intrinsic/eigenmath",
    ],
)

cc_library(
    name = "selection_matrix_helpers",
    srcs = ["selection_matrix_helpers.cc"],
    hdrs = [
        "selection_matrix_helpers.h",
    ],
    visibility = ["//intrinsic_control/intrinsic/icon:rtcl_users"],
    deps = [
        "//intrinsic/math:projection_matrix_utils",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/types:optional",
    ],
)

cc_library(
    name = "settling_time_counter",
    srcs = ["settling_time_counter.cc"],
    hdrs = ["settling_time_counter.h"],
    visibility = ["//visibility:private"],
    deps = [
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:twist",
    ],
)

cc_library(
    name = "linear_kalman_filter",
    srcs = ["linear_kalman_filter.cc"],
    hdrs = [
        "linear_kalman_filter.h",
    ],
    visibility = ["//visibility:private"],
    deps = [
        "//intrinsic/math/linear_systems:discrete_algebraic_riccati_equation",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_library(
    name = "linear_joint_acceleration_filter",
    srcs = ["linear_joint_acceleration_filter.cc"],
    hdrs = [
        "linear_joint_acceleration_filter.h",
    ],
    visibility = [":icon"],
    deps = [
        ":linear_kalman_filter",
        "//intrinsic/math/linear_systems:discrete_algebraic_riccati_equation",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@intrinsic_apis//intrinsic/icon/proto:linear_joint_acceleration_filter_config_cc_proto",
    ],
)

cc_library(
    name = "cartesian_impedance_reference_generator_interface",
    hdrs = ["cartesian_impedance_reference_generator_interface.h"],
    visibility = ["//intrinsic_control/intrinsic/icon:rtcl_users"],
    deps = [
        ":cartesian_impedance_commons",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_kinematics/intrinsic/kinematics/types:cart_state",
        "//intrinsic_kinematics/intrinsic/kinematics/types:cartesian_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_sdk/intrinsic/eigenmath",
    ],
)

cc_library(
    name = "simple_cartesian_impedance_reference_generator",
    srcs = ["simple_cartesian_impedance_reference_generator.cc"],
    hdrs = ["simple_cartesian_impedance_reference_generator.h"],
    visibility = ["//visibility:private"],
    deps = [
        ":cartesian_impedance_commons",
        ":cartesian_impedance_reference_generator_interface",
        "//intrinsic_control/intrinsic/icon/actions:cartesian_impedance_parameters",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_kinematics/intrinsic/kinematics/types:cart_state",
        "//intrinsic_kinematics/intrinsic/kinematics/types:cartesian_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:check_cartesian_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:check_joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:ipow",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:twist",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
    ],
)

cc_library(
    name = "impedance_reference_generator_holder",
    srcs = ["impedance_reference_generator_holder.cc"],
    hdrs = ["impedance_reference_generator_holder.h"],
    visibility = ["//intrinsic_control/intrinsic/icon:rtcl_users"],
    deps = [
        ":cartesian_impedance_reference_generator_interface",
        ":position_reflexxes_cartesian_impedance_reference_generator",
        ":quintic_spline_cartesian_impedance_reference_generator",
        ":reflexxes_nullspace_reference_generator",
        ":simple_cartesian_impedance_reference_generator",
        ":velocity_reflexxes_cartesian_impedance_reference_generator",
        "//intrinsic_control/intrinsic/icon/actions:cartesian_impedance_parameters",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "@abseil-cpp//absl/types:variant",
    ],
)

cc_library(
    name = "position_reflexxes_cartesian_impedance_reference_generator",
    srcs = ["position_reflexxes_cartesian_impedance_reference_generator.cc"],
    hdrs = ["position_reflexxes_cartesian_impedance_reference_generator.h"],
    visibility = ["//visibility:private"],
    deps = [
        ":cartesian_impedance_commons",
        ":cartesian_impedance_reference_generator_interface",
        ":cartesian_position_reflexxes",
        ":cartesian_velocity_reflexxes",
        "//intrinsic_control/intrinsic/icon/reflexxes",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_kinematics/intrinsic/kinematics/types:cart_state",
        "//intrinsic_kinematics/intrinsic/kinematics/types:cartesian_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:check_cartesian_limits",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:ipow",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:twist",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
    ],
)

cc_library(
    name = "velocity_reflexxes_cartesian_impedance_reference_generator",
    srcs = ["velocity_reflexxes_cartesian_impedance_reference_generator.cc"],
    hdrs = ["velocity_reflexxes_cartesian_impedance_reference_generator.h"],
    visibility = ["//visibility:private"],
    deps = [
        ":cartesian_impedance_commons",
        ":cartesian_impedance_reference_generator_interface",
        ":cartesian_target_scaling",
        ":cartesian_velocity_reflexxes",
        ":reference_limit_settings",
        "//intrinsic_control/intrinsic/icon/reflexxes",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_kinematics/intrinsic/kinematics/types:cart_state",
        "//intrinsic_kinematics/intrinsic/kinematics/types:cartesian_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:check_cartesian_limits",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:ipow",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:twist",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
    ],
)

cc_library(
    name = "reflexxes_nullspace_reference_generator",
    srcs = ["reflexxes_nullspace_reference_generator.cc"],
    hdrs = ["reflexxes_nullspace_reference_generator.h"],
    visibility = ["//visibility:private"],
    deps = [
        ":cartesian_impedance_commons",
        ":cartesian_impedance_reference_generator_interface",
        ":joint_position_reflexxes",
        ":joint_velocity_reflexxes",
        "//intrinsic_control/intrinsic/icon/reflexxes",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_kinematics/intrinsic/kinematics/types:check_joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:ipow",
    ],
)

cc_library(
    name = "quintic_spline_cartesian_impedance_reference_generator",
    srcs = ["quintic_spline_cartesian_impedance_reference_generator.cc"],
    hdrs = ["quintic_spline_cartesian_impedance_reference_generator.h"],
    visibility = ["//visibility:private"],
    deps = [
        ":cartesian_impedance_commons",
        ":cartesian_impedance_reference_generator_interface",
        "//intrinsic/math/spline:polynomial_spline",
        "//intrinsic_control/intrinsic/icon/actions:cartesian_impedance_parameters",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_kinematics/intrinsic/kinematics/types:cart_state",
        "//intrinsic_kinematics/intrinsic/kinematics/types:cartesian_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:check_cartesian_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:check_joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:ipow",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:twist",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/log:check",
    ],
)

cc_library(
    name = "cartesian_impedance_commons",
    srcs = ["cartesian_impedance_commons.cc"],
    hdrs = ["cartesian_impedance_commons.h"],
    visibility = ["//intrinsic_control/intrinsic/icon:rtcl_users"],
    deps = [
        ":compute_critical_damping",
        "//intrinsic_control/intrinsic/icon/actions:cartesian_admittance_action_cc_proto",
        "//intrinsic_control/intrinsic/icon/actions:cartesian_admittance_info",
        "//intrinsic_control/intrinsic/icon/actions:cartesian_impedance_cc_proto",
        "//intrinsic_control/intrinsic/icon/actions:cartesian_impedance_parameters",
        "//intrinsic_control/intrinsic/icon/proto:cart_space_conversion",
        "//intrinsic_control/intrinsic/icon/testing:realtime_annotations",
        "//intrinsic_control/intrinsic/icon/utils:realtime_guard",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_kinematics/intrinsic/kinematics/types:cartesian_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:check_joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:proto_conversion",
        "//intrinsic_sdk/intrinsic/math:twist",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
        "@com_gitlab_libeigen_eigen//:eigen",
        "@intrinsic_apis//intrinsic/math/proto:vector3_cc_proto",
    ],
)

cc_library(
    name = "overdamping",
    srcs = ["overdamping.cc"],
    hdrs = ["overdamping.h"],
    visibility = ["//intrinsic_control/intrinsic/icon:rtcl_users"],
    deps = [
        ":compute_critical_damping",
        "//intrinsic_control/intrinsic/icon/utils:fixed_str_cat",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_sdk/intrinsic/eigenmath",
    ],
)

cc_library(
    name = "admittance_controller_interface",
    hdrs = [
        "admittance_controller_interface.h",
    ],
    visibility = ["//intrinsic_control/intrinsic/icon:rtcl_users"],
    deps = [
        ":cartesian_impedance_commons",
        "//intrinsic_control/intrinsic/icon/actions:cartesian_impedance_parameters",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_kinematics/intrinsic/kinematics",
        "//intrinsic_kinematics/intrinsic/kinematics:state",
        "//intrinsic_kinematics/intrinsic/kinematics/ik:inverse_kinematics_interface",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:twist",
    ],
)

cc_library(
    name = "cartesian_admittance_controller",
    srcs = ["cartesian_admittance_controller.cc"],
    hdrs = [
        "cartesian_admittance_controller.h",
    ],
    visibility = ["//intrinsic_control/intrinsic/icon:rtcl_users"],
    deps = [
        ":admittance_controller_interface",
        ":cartesian_impedance_commons",
        ":settling_time_counter",
        "//intrinsic/eigenmath:pseudo_inverse",
        "//intrinsic/math:pluecker_transform",
        "//intrinsic_control/intrinsic/icon/actions:cartesian_impedance_parameters",
        "//intrinsic_control/intrinsic/icon/utils:realtime_guard",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_kinematics/intrinsic/kinematics",
        "//intrinsic_kinematics/intrinsic/kinematics:state",
        "//intrinsic_kinematics/intrinsic/kinematics/ik:inverse_kinematics_interface",
        "//intrinsic_kinematics/intrinsic/kinematics/types:cart_state",
        "//intrinsic_kinematics/intrinsic/kinematics/types:check_cartesian_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/eigenmath:clamp",
        "//intrinsic_sdk/intrinsic/eigenmath:manifolds",
        "//intrinsic_sdk/intrinsic/eigenmath:so3",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:twist",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/types:optional",
    ],
)

cc_library(
    name = "cartesian_impedance_controller",
    srcs = ["cartesian_impedance_controller.cc"],
    hdrs = [
        "cartesian_impedance_controller.h",
    ],
    visibility = ["//intrinsic_control/intrinsic/icon:rtcl_users"],
    deps = [
        ":cartesian_impedance_commons",
        ":settling_time_counter",
        "//intrinsic/eigenmath:pseudo_inverse",
        "//intrinsic/math:pluecker_transform",
        "//intrinsic_control/intrinsic/icon/actions:cartesian_impedance_parameters",
        "//intrinsic_control/intrinsic/icon/control/parts:feature_interfaces",
        "//intrinsic_control/intrinsic/icon/dynamics:rigid_body_interface",
        "//intrinsic_control/intrinsic/icon/utils:realtime_guard",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_kinematics/intrinsic/kinematics",
        "//intrinsic_kinematics/intrinsic/kinematics:model_interface",
        "//intrinsic_kinematics/intrinsic/kinematics:state",
        "//intrinsic_kinematics/intrinsic/kinematics/types:cart_state",
        "//intrinsic_kinematics/intrinsic/kinematics/types:cartesian_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/eigenmath:clamp",
        "//intrinsic_sdk/intrinsic/eigenmath:manifolds",
        "//intrinsic_sdk/intrinsic/eigenmath:so3",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:twist",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/types:optional",
    ],
)

cc_library(
    name = "reference_limit_settings",
    hdrs = ["reference_limit_settings.h"],
    visibility = ["//intrinsic_control/intrinsic/icon/control/actions:__pkg__"],
)

cc_library(
    name = "cartesian_target_scaling",
    srcs = ["cartesian_target_scaling.cc"],
    hdrs = ["cartesian_target_scaling.h"],
    visibility = ["//intrinsic_control/intrinsic/icon/control/actions:__pkg__"],
    deps = [
        ":reference_limit_settings",
        "//intrinsic_control/intrinsic/icon/utils:log",
        "//intrinsic_kinematics/intrinsic/kinematics/types:cart_state",
        "//intrinsic_kinematics/intrinsic/kinematics/types:cartesian_limits",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/eigenmath:rotation_utils",
        "//intrinsic_sdk/intrinsic/math:pose3",
    ],
)

cc_library(
    name = "speed_override_factor_types",
    hdrs = ["speed_override_factor_types.h"],
)

cc_library(
    name = "speed_override_factor_limits_utils",
    srcs = ["speed_override_factor_limits_utils.cc"],
    hdrs = ["speed_override_factor_limits_utils.h"],
    deps = [
        ":speed_override_factor_types",
        "//intrinsic_control/intrinsic/icon/dynamics:rigid_body_interface",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:almost_equals",
        "//intrinsic_sdk/intrinsic/math:ipow",
    ],
)

cc_library(
    name = "speed_override_factor_limits_computation",
    srcs = ["speed_override_factor_limits_computation.cc"],
    hdrs = ["speed_override_factor_limits_computation.h"],
    deps = [
        ":speed_override_factor_limits_utils",
        ":speed_override_factor_types",
        "//intrinsic_control/intrinsic/icon/dynamics:rigid_body_interface",
        "//intrinsic_control/intrinsic/icon/dynamics:validate_rigid_body_interface",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_kinematics/intrinsic/kinematics/types:dynamic_limits_check_mode",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_trajectories",
        "//intrinsic_motion_planning/intrinsic/motion_planning/trajectory_planning:interpolate_joint_trajectories",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
    ],
)

cc_library(
    name = "speed_override_factor_reflexxes_utils",
    srcs = ["speed_override_factor_reflexxes_utils.cc"],
    hdrs = ["speed_override_factor_reflexxes_utils.h"],
    deps = [
        ":speed_override_factor_types",
        "//intrinsic_control/intrinsic/icon/reflexxes",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_sdk/intrinsic/math:almost_equals",
        "//intrinsic_sdk/intrinsic/math:ipow",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "speed_override_factor_dynamics_utils",
    srcs = ["speed_override_factor_dynamics_utils.cc"],
    hdrs = ["speed_override_factor_dynamics_utils.h"],
    deps = [
        ":speed_override_factor_limits_computation",
        ":speed_override_factor_reflexxes_utils",
        ":speed_override_factor_types",
        "//intrinsic_control/intrinsic/icon/dynamics:rigid_body_interface",
        "//intrinsic_control/intrinsic/icon/reflexxes",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_kinematics/intrinsic/kinematics/types:check_joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:dynamic_limits_check_mode",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_trajectories",
        "//intrinsic_motion_planning/intrinsic/motion_planning/trajectory_planning:interpolate_joint_trajectories",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:almost_equals",
        "//intrinsic_sdk/intrinsic/math:ipow",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
    ],
)

cc_library(
    name = "speed_override_factor_dynamics",
    srcs = ["speed_override_factor_dynamics.cc"],
    hdrs = ["speed_override_factor_dynamics.h"],
    deps = [
        ":speed_override_factor_dynamics_utils",
        ":speed_override_factor_limits_computation",
        ":speed_override_factor_reflexxes_utils",
        ":speed_override_factor_types",
        "//intrinsic_control/intrinsic/icon/dynamics:rigid_body_interface",
        "//intrinsic_control/intrinsic/icon/reflexxes",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_kinematics/intrinsic/kinematics/types:check_joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:dynamic_limits_check_mode",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_trajectories",
        "//intrinsic_motion_planning/intrinsic/motion_planning/trajectory_planning:interpolate_joint_trajectories",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:almost_equals",
        "//intrinsic_sdk/intrinsic/math:ipow",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
    ],
)

cc_library(
    name = "trajectory_player",
    srcs = [
        "trajectory_player.cc",
    ],
    hdrs = [
        "trajectory_player.h",
    ],
    visibility = ["//intrinsic_control/intrinsic/icon:rtcl_users"],
    deps = [
        ":speed_override_factor_dynamics",
        ":speed_override_factor_reflexxes_utils",
        ":speed_override_factor_types",
        "//intrinsic_control/intrinsic/icon/dynamics:rigid_body_interface",
        "//intrinsic_control/intrinsic/icon/dynamics:validate_rigid_body_interface",
        "//intrinsic_control/intrinsic/icon/reflexxes",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_kinematics/intrinsic/kinematics/types:check_joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:dynamic_limits_check_mode",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_trajectories",
        "//intrinsic_motion_planning/intrinsic/motion_planning/trajectory_planning:interpolate_joint_trajectories",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/eigenmath:clamp",
        "//intrinsic_sdk/intrinsic/math:ipow",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
    ],
)

cc_library(
    name = "trajectory_residual_controller",
    srcs = [
        "trajectory_residual_controller.cc",
    ],
    hdrs = [
        "trajectory_residual_controller.h",
    ],
    visibility = ["//intrinsic_control/intrinsic/icon:rtcl_users"],
    deps = [
        ":joint_position_reflexxes",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "is_settled_criterion",
    srcs = ["is_settled_criterion.cc"],
    hdrs = ["is_settled_criterion.h"],
    visibility = ["//intrinsic_control/intrinsic/icon:rtcl_users"],
    deps = [
        "//intrinsic_control/intrinsic/icon/utils:fixed_str_cat",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:ipow",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "distance_tracker",
    srcs = ["distance_tracker.cc"],
    hdrs = ["distance_tracker.h"],
    visibility = ["//intrinsic_control/intrinsic/icon:rtcl_users"],
    deps = [
        "//intrinsic_control/intrinsic/icon/utils:realtime_guard",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "joint_position_pid_torque_controller",
    srcs = [
        "joint_position_pid_torque_controller.cc",
    ],
    hdrs = [
        "joint_position_pid_torque_controller.h",
    ],
    visibility = [
        ":icon",
    ],
    deps = [
        "//intrinsic_control/intrinsic/icon/control:joint_position_command",
        "//intrinsic_control/intrinsic/icon/utils:log",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@intrinsic_apis//intrinsic/icon/proto:joint_position_pid_torque_controller_config_cc_proto",
    ],
)

cc_library(
    name = "joint_position_pid_velocity_controller",
    srcs = [
        "joint_position_pid_velocity_controller.cc",
    ],
    hdrs = [
        "joint_position_pid_velocity_controller.h",
    ],
    visibility = ["//third_party/abb_hardware_module/abb_hwm:__subpackages__"],
    deps = [
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/eigenmath:clamp",
        "//intrinsic_sdk/intrinsic/math/signals:butter_filter2",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status:statusor",
        "@intrinsic_apis//intrinsic/icon/proto:joint_position_pid_velocity_controller_config_cc_proto",
    ],
)

cc_library(
    name = "axis_angle_kinematics",
    srcs = ["axis_angle_kinematics.cc"],
    hdrs = ["axis_angle_kinematics.h"],
    visibility = ["//intrinsic:intrinsic_internal"],
    deps = [
        "//intrinsic_sdk/intrinsic/eigenmath",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
    ],
)

cc_library(
    name = "cartesian_position_reflexxes",
    srcs = ["cartesian_position_reflexxes.cc"],
    hdrs = ["cartesian_position_reflexxes.h"],
    visibility = ["//intrinsic_control/intrinsic/icon:rtcl_users"],
    deps = [
        ":axis_angle_kinematics",
        "//intrinsic/eigenmath:scalar_utils",
        "//intrinsic_control/intrinsic/icon/reflexxes",
        "//intrinsic_control/intrinsic/icon/utils:log",
        "//intrinsic_kinematics/intrinsic/kinematics/types:cart_state",
        "//intrinsic_kinematics/intrinsic/kinematics/types:cartesian_limits",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/eigenmath:manifolds",
        "//intrinsic_sdk/intrinsic/eigenmath:so3",
    ],
)

cc_library(
    name = "cartesian_velocity_reflexxes",
    srcs = ["cartesian_velocity_reflexxes.cc"],
    hdrs = ["cartesian_velocity_reflexxes.h"],
    visibility = ["//intrinsic_control/intrinsic/icon:rtcl_users"],
    deps = [
        "//intrinsic/eigenmath:scalar_utils",
        "//intrinsic_control/intrinsic/icon/reflexxes",
        "//intrinsic_control/intrinsic/icon/utils:log",
        "//intrinsic_kinematics/intrinsic/kinematics/types:cart_state",
        "//intrinsic_kinematics/intrinsic/kinematics/types:cartesian_limits",
        "//intrinsic_sdk/intrinsic/eigenmath",
    ],
)

cc_library(
    name = "joint_position_reflexxes",
    srcs = ["joint_position_reflexxes.cc"],
    hdrs = ["joint_position_reflexxes.h"],
    visibility = [":icon"],
    deps = [
        "//intrinsic/eigenmath:scalar_utils",
        "//intrinsic_control/intrinsic/icon/reflexxes",
        "//intrinsic_control/intrinsic/icon/utils:log",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "@abseil-cpp//absl/log:check",
    ],
)

cc_library(
    name = "joint_velocity_reflexxes",
    srcs = ["joint_velocity_reflexxes.cc"],
    hdrs = ["joint_velocity_reflexxes.h"],
    visibility = [":icon"],
    deps = [
        "//intrinsic/eigenmath:scalar_utils",
        "//intrinsic_control/intrinsic/icon/reflexxes",
        "//intrinsic_control/intrinsic/icon/utils:log",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "@abseil-cpp//absl/log:check",
    ],
)

cc_library(
    name = "libjoint_stop_trajectory",
    srcs = ["joint_stop_trajectory.cc"],
    hdrs = ["joint_stop_trajectory.h"],
    visibility = [":icon"],
    deps = [
        "//intrinsic_control/intrinsic/icon/utils:log",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state",
        "//intrinsic_sdk/intrinsic/eigenmath",
    ],
)

cc_library(
    name = "joint_limit_checker",
    srcs = ["joint_limit_checker.cc"],
    hdrs = ["joint_limit_checker.h"],
    visibility = [
        "//intrinsic_control/intrinsic/icon:rtcl_users",
    ],
    deps = [
        "//intrinsic_control/intrinsic/icon/utils:fixed_str_cat",
        "//intrinsic_control/intrinsic/icon/utils:realtime_stack_trace",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_kinematics/intrinsic/kinematics/types:check_joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state",
        "//intrinsic_kinematics/intrinsic/kinematics/types:to_fixed_string",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_library(
    name = "directional_stiffness",
    srcs = ["directional_stiffness.cc"],
    hdrs = ["directional_stiffness.h"],
    visibility = ["//intrinsic_control/intrinsic/icon:rtcl_users"],
    deps = [
        "//intrinsic/math:projection_matrix_utils",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_library(
    name = "wrench_stability_monitor",
    hdrs = ["wrench_stability_monitor.h"],
    visibility = ["//intrinsic_control/intrinsic/icon:rtcl_users"],
    deps = [
        "//intrinsic_control/intrinsic/icon/testing:realtime_annotations",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_sdk/intrinsic/math:twist",
    ],
)

cc_library(
    name = "wrench_stability_monitor_factory",
    srcs = ["wrench_stability_monitor_factory_ioc.cc"],
    hdrs = ["wrench_stability_monitor_factory.h"],
    visibility = ["//intrinsic_control/intrinsic/icon:rtcl_users"],
    deps = [
        ":wrench_stability_monitor",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_sdk/intrinsic/math:twist",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@intrinsic_apis//intrinsic/icon/equipment:force_control_settings_cc_proto",
    ] + [],
)

cc_library(
    name = "admittance_controller_factory",
    srcs = ["admittance_controller_factory_core.cc"],
    hdrs = ["admittance_controller_factory.h"],
    deps = [
        ":admittance_controller_interface",
        ":cartesian_admittance_controller",
        "//intrinsic_control/intrinsic/icon/actions:cartesian_impedance_parameters",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status:statusor",
    ] + [],
)
