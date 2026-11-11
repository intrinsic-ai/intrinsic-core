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

load("//bazel:cc_macros.bzl", "cc_library", "cc_test")
load("//intrinsic_control/intrinsic/icon/testing:cc_test_and_malloc_test.bzl", "cc_test_and_malloc_test")

package(default_visibility = ["//visibility:public"])

package_group(
    name = "icon",
    packages = [
        "//intrinsic_control/intrinsic/icon/...",
    ],
)

cc_library(
    name = "chirp_signal",
    srcs = ["chirp_signal.cc"],
    hdrs = [
        "chirp_signal.h",
    ],
    visibility = ["//intrinsic_control/intrinsic/icon:rtcl_users"],
    deps = [
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_test(
    name = "chirp_signal_test",
    srcs = ["chirp_signal_test.cc"],
    deps = [
        ":chirp_signal",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_matchers",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@com_google_cel_cpp//internal:testing_no_main",
        "@com_google_googletest//:gtest_main",
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

cc_test(
    name = "compute_critical_damping_test",
    srcs = ["compute_critical_damping_test.cc"],
    deps = [
        ":compute_critical_damping",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_matchers",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/eigenmath:matchers",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@com_google_cel_cpp//internal:testing_no_main",
        "@com_google_googletest//:gtest_main",
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

cc_test(
    name = "selection_matrix_helpers_test",
    srcs = ["selection_matrix_helpers_test.cc"],
    deps = [
        ":selection_matrix_helpers",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/eigenmath:matchers",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@com_google_cel_cpp//internal:testing_no_main",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "lissajous_search_pattern_generator",
    srcs = ["lissajous_search_pattern_generator.cc"],
    hdrs = ["lissajous_search_pattern_generator.h"],
    visibility = ["//intrinsic_control/intrinsic/icon:rtcl_users"],
    deps = [
        ":cartesian_impedance_commons",
        ":cartesian_impedance_trajectory",
        ":compute_critical_damping",
        ":overdamping",
        "//intrinsic/math:lissajous_pattern_utils",
        "//intrinsic_control/intrinsic/icon/actions:impedance_trajectory_generators_cc_proto",
        "//intrinsic_control/intrinsic/icon/proto:cart_space_conversion",
        "//intrinsic_control/intrinsic/icon/utils:realtime_guard",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_kinematics/intrinsic/kinematics/types:cart_state",
        "//intrinsic_kinematics/intrinsic/kinematics/types:state_rn",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/eigenmath:rotation_utils",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:twist",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
)

cc_test_and_malloc_test(
    name = "lissajous_search_pattern_generator_test",
    srcs = ["lissajous_search_pattern_generator_test.cc"],
    deps = [
        ":cartesian_impedance_commons",
        ":compute_critical_damping",
        ":lissajous_search_pattern_generator",
        "//intrinsic_control/intrinsic/icon/actions:impedance_trajectory_generators_cc_proto",
        "//intrinsic_control/intrinsic/icon/proto:cart_space_conversion",
        "//intrinsic_control/intrinsic/icon/testing:malloc_test",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_matchers",
        "//intrinsic_kinematics/intrinsic/kinematics/types:cart_state",
        "//intrinsic_kinematics/intrinsic/kinematics/types:state_rn",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/eigenmath:matchers",
        "//intrinsic_sdk/intrinsic/eigenmath:pose3_utils",
        "//intrinsic_sdk/intrinsic/eigenmath:rotation_utils",
        "//intrinsic_sdk/intrinsic/eigenmath:test_utils",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:twist",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@com_github_inazarenko_protobuf_matchers//protobuf-matchers",
        "@com_google_cel_cpp//internal:testing_no_main",
        "@com_google_googletest//:gtest",
    ],
)

cc_library(
    name = "linear_impedance_setpoint_interpolator",
    srcs = ["linear_impedance_setpoint_interpolator.cc"],
    hdrs = ["linear_impedance_setpoint_interpolator.h"],
    visibility = ["//visibility:private"],
    deps = [
        ":cartesian_impedance_commons",
        ":cartesian_impedance_trajectory",
        "//intrinsic/eigenmath:interpolation",
        "//intrinsic/math:symmetric_positive_definite_matrix_squareroot",
        "//intrinsic/math:time_series_utils",
        "//intrinsic_control/intrinsic/icon/actions:cartesian_impedance_parameters",
        "//intrinsic_control/intrinsic/icon/actions:impedance_trajectory_generators_cc_proto",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_kinematics/intrinsic/kinematics/types:cart_state",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:ipow",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:twist",
        "//intrinsic_sdk/intrinsic/util:proto_time",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_test_and_malloc_test(
    name = "linear_impedance_setpoint_interpolator_test",
    srcs = ["linear_impedance_setpoint_interpolator_test.cc"],
    deps = [
        ":cartesian_impedance_commons",
        ":linear_impedance_setpoint_interpolator",
        "//intrinsic_control/intrinsic/icon/actions:cartesian_impedance_parameters",
        "//intrinsic_control/intrinsic/icon/actions:impedance_trajectory_generators_cc_proto",
        "//intrinsic_control/intrinsic/icon/control/algorithms/testing:cartesian_impedance_controller_test_utils",
        "//intrinsic_control/intrinsic/icon/testing:malloc_test",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_matchers",
        "//intrinsic_kinematics/intrinsic/kinematics/types:cart_state",
        "//intrinsic_kinematics/intrinsic/kinematics/types:to_fixed_string",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/eigenmath:rotation_utils",
        "//intrinsic_sdk/intrinsic/math:ipow",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:twist",
        "//intrinsic_sdk/intrinsic/util:proto_time",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@com_github_inazarenko_protobuf_matchers//protobuf-matchers",
        "@com_google_cel_cpp//internal:testing_no_main",
        "@com_google_googletest//:gtest",
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

cc_test_and_malloc_test(
    name = "settling_time_counter_test",
    srcs = ["settling_time_counter_test.cc"],
    deps = [
        ":settling_time_counter",
        "//intrinsic_control/intrinsic/icon/testing:malloc_test",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state",
        "//intrinsic_sdk/intrinsic/math:twist",
        "@abseil-cpp//absl/status:status_matchers",
        "@com_google_cel_cpp//internal:testing_no_main",
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

cc_test_and_malloc_test(
    name = "linear_kalman_filter_test",
    srcs = ["linear_kalman_filter_test.cc"],
    deps = [
        ":linear_kalman_filter",
        "//intrinsic/eigenmath:random_number_utils",
        "//intrinsic/math/linear_systems:discrete_algebraic_riccati_equation",
        "//intrinsic/math/linear_systems:kalman_criterions",
        "//intrinsic/math/linear_systems/testing:harmonic_oscillator_test_system",
        "//intrinsic/math/linear_systems/testing:linear_test_problem",
        "//intrinsic_control/intrinsic/icon/testing:malloc_test",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/eigenmath:matchers",
        "//intrinsic_sdk/intrinsic/math:gaussian_noise",
        "@abseil-cpp//absl/random",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@com_google_cel_cpp//internal:testing_no_main",
        "@com_google_googletest//:gtest",
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

cc_test(
    name = "linear_joint_acceleration_filter_test",
    srcs = ["linear_joint_acceleration_filter_test.cc"],
    deps = [
        ":linear_joint_acceleration_filter",
        "//intrinsic/eigenmath:random_number_utils",
        "//intrinsic/math:symmetric_positive_definite_matrix_squareroot",
        "//intrinsic/math/linear_systems:discrete_algebraic_riccati_equation",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_matchers",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:gaussian_noise",
        "@abseil-cpp//absl/random",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@com_google_cel_cpp//internal:testing_no_main",
        "@com_google_googletest//:gtest_main",
        "@intrinsic_apis//intrinsic/icon/proto:linear_joint_acceleration_filter_config_cc_proto",
    ],
)

cc_test(
    name = "linear_joint_acceleration_filter_tuning",
    srcs = ["linear_joint_acceleration_filter_tuning.cc"],
    deps = [
        ":chirp_signal",
        ":linear_joint_acceleration_filter",
        "//intrinsic/eigenmath:eigen_file_export",
        "//intrinsic/eigenmath:random_number_utils",
        "//intrinsic/math:symmetric_positive_definite_matrix_squareroot",
        "//intrinsic/math/linear_systems:discrete_algebraic_riccati_equation",
        "//intrinsic_control/intrinsic/icon/release/portable:init_intrinsic",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:gaussian_noise",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/random",
        "@abseil-cpp//absl/status",
    ],
)

cc_library(
    name = "cartesian_impedance_trajectory",
    srcs = ["cartesian_impedance_trajectory.cc"],
    hdrs = ["cartesian_impedance_trajectory.h"],
    visibility = [
        ":icon",
    ],
    deps = [
        ":cartesian_impedance_commons",
        "//intrinsic_control/intrinsic/icon/actions:cartesian_impedance_cc_proto",
        "//intrinsic_control/intrinsic/icon/actions:cartesian_impedance_parameters",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_kinematics/intrinsic/kinematics/types:cart_state",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state",
        "//intrinsic_sdk/intrinsic/eigenmath:pose3_utils",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status:statusor",
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

cc_test(
    name = "simple_cartesian_impedance_reference_generator_test",
    srcs = ["simple_cartesian_impedance_reference_generator_test.cc"],
    deps = [
        ":cartesian_impedance_commons",
        ":simple_cartesian_impedance_reference_generator",
        "//intrinsic_control/intrinsic/icon/actions:cartesian_impedance_parameters",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_matchers",
        "//intrinsic_kinematics/intrinsic/kinematics/types:cart_state",
        "//intrinsic_kinematics/intrinsic/kinematics/types:cartesian_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/eigenmath:matchers",
        "//intrinsic_sdk/intrinsic/eigenmath:rotation_utils",
        "//intrinsic_sdk/intrinsic/math:ipow",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:twist",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/strings",
        "@com_google_cel_cpp//internal:testing_no_main",
        "@com_google_googletest//:gtest_main",
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

cc_test(
    name = "impedance_reference_generator_holder_test",
    srcs = ["impedance_reference_generator_holder_test.cc"],
    deps = [
        ":impedance_reference_generator_holder",
        ":position_reflexxes_cartesian_impedance_reference_generator",
        ":quintic_spline_cartesian_impedance_reference_generator",
        ":reflexxes_nullspace_reference_generator",
        ":simple_cartesian_impedance_reference_generator",
        ":velocity_reflexxes_cartesian_impedance_reference_generator",
        "//intrinsic_control/intrinsic/icon/actions:cartesian_impedance_parameters",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@com_google_cel_cpp//internal:testing_no_main",
        "@com_google_googletest//:gtest_main",
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

cc_test(
    name = "position_reflexxes_cartesian_impedance_reference_generator_test",
    srcs = ["position_reflexxes_cartesian_impedance_reference_generator_test.cc"],
    deps = [
        ":cartesian_impedance_commons",
        ":position_reflexxes_cartesian_impedance_reference_generator",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_matchers",
        "//intrinsic_kinematics/intrinsic/kinematics/types:cart_state",
        "//intrinsic_kinematics/intrinsic/kinematics/types:cartesian_limits",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/eigenmath:matchers",
        "//intrinsic_sdk/intrinsic/math:ipow",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:twist",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/strings",
        "@com_google_cel_cpp//internal:testing_no_main",
        "@com_google_googletest//:gtest_main",
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

cc_test(
    name = "velocity_reflexxes_cartesian_impedance_reference_generator_test",
    srcs = ["velocity_reflexxes_cartesian_impedance_reference_generator_test.cc"],
    deps = [
        ":cartesian_impedance_commons",
        ":velocity_reflexxes_cartesian_impedance_reference_generator",
        "//intrinsic_control/intrinsic/icon/testing:malloc_test",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_matchers",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_kinematics/intrinsic/kinematics/types:cart_state",
        "//intrinsic_kinematics/intrinsic/kinematics/types:cartesian_limits",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/eigenmath:matchers",
        "//intrinsic_sdk/intrinsic/eigenmath:rotation_utils",
        "//intrinsic_sdk/intrinsic/math:ipow",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:twist",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/strings",
        "@com_google_cel_cpp//internal:testing_no_main",
        "@com_google_googletest//:gtest_main",
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

cc_test(
    name = "reflexxes_nullspace_reference_generator_test",
    srcs = ["reflexxes_nullspace_reference_generator_test.cc"],
    deps = [
        ":cartesian_impedance_commons",
        ":reflexxes_nullspace_reference_generator",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_matchers",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/eigenmath:matchers",
        "//intrinsic_sdk/intrinsic/math:ipow",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/strings",
        "@com_google_cel_cpp//internal:testing_no_main",
        "@com_google_googletest//:gtest_main",
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

cc_test(
    name = "quintic_spline_cartesian_impedance_reference_generator_test",
    srcs = ["quintic_spline_cartesian_impedance_reference_generator_test.cc"],
    deps = [
        ":cartesian_impedance_commons",
        ":quintic_spline_cartesian_impedance_reference_generator",
        "//intrinsic_control/intrinsic/icon/actions:cartesian_impedance_parameters",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_matchers",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_kinematics/intrinsic/kinematics/types:cart_state",
        "//intrinsic_kinematics/intrinsic/kinematics/types:cartesian_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/eigenmath:matchers",
        "//intrinsic_sdk/intrinsic/math:ipow",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:twist",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/strings",
        "@com_google_cel_cpp//internal:testing_no_main",
        "@com_google_googletest//:gtest_main",
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

cc_test_and_malloc_test(
    name = "overdamping_test",
    srcs = ["overdamping_test.cc"],
    deps = [
        ":overdamping",
        "//intrinsic_control/intrinsic/icon/testing:malloc_test",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_matchers",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/eigenmath:matchers",
        "@abseil-cpp//absl/status",
        "@com_google_cel_cpp//internal:testing_no_main",
        "@com_google_googletest//:gtest",
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
    name = "feedforward_wrench_interpolator_settings",
    hdrs = ["feedforward_wrench_interpolator_settings.h"],
    visibility = ["//intrinsic_control/intrinsic/icon/control/actions:__pkg__"],
    deps = [
        "//intrinsic_sdk/intrinsic/math:twist",
    ],
)

cc_library(
    name = "ensure_clamped",
    hdrs = ["ensure_clamped.h"],
    deps = [
        "//intrinsic_control/intrinsic/icon/utils:log",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings:str_format",
    ],
)

cc_library(
    name = "feedforward_wrench_interpolator_linear",
    srcs = ["feedforward_wrench_interpolator_linear.cc"],
    hdrs = ["feedforward_wrench_interpolator_linear.h"],
    visibility = ["//intrinsic_control/intrinsic/icon:rtcl_users"],
    deps = [
        ":ensure_clamped",
        ":feedforward_wrench_interpolator_interface",
        ":feedforward_wrench_interpolator_settings",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state",
        "//intrinsic_sdk/intrinsic/math:twist",
    ],
)

cc_library(
    name = "feedforward_wrench_interpolator_interface",
    hdrs = [
        "feedforward_wrench_interpolator_interface.h",
    ],
    visibility = ["//visibility:private"],
    deps = [
        ":feedforward_wrench_interpolator_settings",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state",
        "//intrinsic_sdk/intrinsic/math:twist",
        "@abseil-cpp//absl/base:core_headers",
    ],
)

cc_test_and_malloc_test(
    name = "feedforward_wrench_interpolator_linear_test",
    size = "small",
    srcs = [
        "feedforward_wrench_interpolator_linear_test.cc",
    ],
    deps = [
        ":feedforward_wrench_interpolator_linear",
        ":feedforward_wrench_interpolator_settings",
        "//intrinsic_control/intrinsic/icon/testing:malloc_test",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:twist",
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

cc_test(
    name = "cartesian_target_scaling_test",
    srcs = ["cartesian_target_scaling_test.cc"],
    deps = [
        ":cartesian_target_scaling",
        ":reference_limit_settings",
        "//intrinsic_kinematics/intrinsic/kinematics/types:cart_state",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/eigenmath:matchers",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "@com_google_googletest//:gtest_main",
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

cc_test_and_malloc_test(
    name = "trajectory_residual_controller_test",
    srcs = [
        "trajectory_residual_controller_test.cc",
    ],
    deps = [
        ":joint_limit_checker",
        ":trajectory_residual_controller",
        "//intrinsic_control/intrinsic/icon/testing:malloc_test",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_matchers",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state_matcher",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:ipow",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
        "@com_google_cel_cpp//internal:testing_no_main",
        "@com_google_googletest//:gtest",
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

cc_test_and_malloc_test(
    name = "is_settled_criterion_test",
    srcs = ["is_settled_criterion_test.cc"],
    deps = [
        ":is_settled_criterion",
        "//intrinsic/eigenmath:random_number_utils",
        "//intrinsic/math/spline:polynomial_spline",
        "//intrinsic_control/intrinsic/icon/testing:malloc_test",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_matchers",
        "//intrinsic_kinematics/intrinsic/kinematics/types:state_rn",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:gaussian_noise",
        "@abseil-cpp//absl/random",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@com_google_cel_cpp//internal:testing_no_main",
        "@com_google_googletest//:gtest",
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

cc_test(
    name = "distance_tracker_test",
    srcs = ["distance_tracker_test.cc"],
    deps = [
        ":distance_tracker",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@com_google_cel_cpp//internal:testing_no_main",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "cartesian_rangefinder_distance_setpoint_generator",
    srcs = ["cartesian_rangefinder_distance_setpoint_generator.cc"],
    hdrs = ["cartesian_rangefinder_distance_setpoint_generator.h"],
    visibility = ["//intrinsic_control/intrinsic/icon:rtcl_users"],
    deps = [
        ":cartesian_bspline",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_kinematics/intrinsic/kinematics/types:cart_state",
        "//intrinsic_kinematics/intrinsic/kinematics/types:cartesian_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:check_cartesian_limits",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/eigenmath:manifolds",
        "//intrinsic_sdk/intrinsic/eigenmath:so3",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/util:eigen",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_test(
    name = "cartesian_rangefinder_distance_setpoint_generator_test",
    srcs = ["cartesian_rangefinder_distance_setpoint_generator_test.cc"],
    deps = [
        ":cartesian_rangefinder_distance_setpoint_generator",
        "//intrinsic_control/intrinsic/icon/control/algorithms/testing:cartesian_rangefinder_distance_controller_test_utils",
        "//intrinsic_control/intrinsic/icon/testing:malloc_test",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_matchers",
        "//intrinsic_kinematics/intrinsic/kinematics/testing:trajectory_plot",
        "//intrinsic_kinematics/intrinsic/kinematics/types:cart_state",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@com_google_cel_cpp//internal:testing_no_main",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "cartesian_rangefinder_distance_controller",
    srcs = ["cartesian_rangefinder_distance_controller.cc"],
    hdrs = [
        "cartesian_rangefinder_distance_controller.h",
    ],
    visibility = ["//intrinsic_control/intrinsic/icon:rtcl_users"],
    deps = [
        ":cartesian_rangefinder_distance_setpoint_generator",
        ":settling_time_counter",
        "//intrinsic/eigenmath:pseudo_inverse",
        "//intrinsic/math:pluecker_transform",
        "//intrinsic_control/intrinsic/icon/utils:realtime_guard",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_kinematics/intrinsic/kinematics",
        "//intrinsic_kinematics/intrinsic/kinematics:group",
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
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@com_gitlab_libeigen_eigen//:eigen",
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

cc_test(
    name = "joint_position_pid_torque_controller_test",
    srcs = [
        "joint_position_pid_torque_controller_test.cc",
    ],
    deps = [
        ":joint_position_pid_torque_controller",
        "//intrinsic_control/intrinsic/icon/control:joint_position_command",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@com_google_cel_cpp//internal:testing_no_main",
        "@com_google_googletest//:gtest_main",
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

cc_test_and_malloc_test(
    name = "joint_position_pid_velocity_controller_test",
    srcs = [
        "joint_position_pid_velocity_controller_test.cc",
    ],
    deps = [
        ":joint_position_pid_velocity_controller",
        "//intrinsic_control/intrinsic/icon/testing:malloc_test",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_matchers",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/eigenmath:matchers",
        "//intrinsic_sdk/intrinsic/math/signals:butter_filter2",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@com_google_cel_cpp//internal:testing_no_main",
        "@com_google_googletest//:gtest",
        "@intrinsic_apis//intrinsic/icon/proto:joint_position_pid_velocity_controller_config_cc_proto",
    ],
)

cc_library(
    name = "nullspace_stiffness_control",
    hdrs = ["nullspace_stiffness_control.h"],
    visibility = ["//intrinsic_control/intrinsic/icon/control:__subpackages__"],
    deps = [
        "//intrinsic/eigenmath:pseudo_inverse",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings:str_format",
    ],
)

cc_test_and_malloc_test(
    name = "nullspace_stiffness_control_test",
    srcs = ["nullspace_stiffness_control_test.cc"],
    deps = [
        ":nullspace_stiffness_control",
        "//intrinsic/eigenmath:pseudo_inverse",
        "//intrinsic_control/intrinsic/icon/testing:malloc_test",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "@abseil-cpp//absl/status:status_matchers",
        "@com_google_cel_cpp//internal:testing_no_main",
        "@com_google_googletest//:gtest",
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
    name = "cartesian_bspline",
    srcs = ["cartesian_bspline.cc"],
    hdrs = ["cartesian_bspline.h"],
    visibility = [":icon"],
    deps = [
        "//intrinsic/math/spline:bspline",
        "//intrinsic/math/spline:bspline_base",
        "//intrinsic/math/spline:bsplineq",
        "//intrinsic_control/intrinsic/icon/utils:log",
        "//intrinsic_kinematics/intrinsic/kinematics/types:cart_state",
        "//intrinsic_kinematics/intrinsic/kinematics/types:cartesian_limits",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings:str_format",
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
    name = "joint_limit_avoidance_potential",
    srcs = ["joint_limit_avoidance_potential.cc"],
    hdrs = ["joint_limit_avoidance_potential.h"],
    deps = [
        "//intrinsic_control/intrinsic/icon/utils:log",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
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

cc_test(
    name = "axis_angle_kinematics_test",
    srcs = ["axis_angle_kinematics_test.cc"],
    deps = [
        ":axis_angle_kinematics",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "@com_gitlab_libeigen_eigen//:eigen",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "joint_limit_avoidance_potential_test",
    srcs = ["joint_limit_avoidance_potential_test.cc"],
    deps = [
        ":joint_limit_avoidance_potential",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test_and_malloc_test(
    name = "eigen_real_time_alloc_test",
    srcs = ["eigen_real_time_alloc_test.cc"],
    deps = [
        "//intrinsic_control/intrinsic/icon/testing:malloc_test",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "@com_gitlab_libeigen_eigen//:eigen",
        "@com_google_googletest//:gtest",
    ],
)

cc_test_and_malloc_test(
    name = "cartesian_bspline_test",
    timeout = "long",
    srcs = ["cartesian_bspline_test.cc"],
    deps = [
        ":cartesian_bspline",
        "//intrinsic_control/intrinsic/icon/testing:malloc_test",
        "//intrinsic_kinematics/intrinsic/kinematics/types:cart_state",
        "//intrinsic_kinematics/intrinsic/kinematics/types:cartesian_limits",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/eigenmath:matchers",
        "//intrinsic_sdk/intrinsic/eigenmath:so3",
        "//intrinsic_sdk/intrinsic/math:pose3",
    ],
)

cc_test_and_malloc_test(
    name = "cartesian_position_reflexxes_test",
    timeout = "long",
    srcs = [
        "cartesian_position_reflexxes_test.cc",
        "finite_difference_test.h",
    ],
    deps = [
        ":cartesian_position_reflexxes",
        "//intrinsic_control/intrinsic/icon/reflexxes",
        "//intrinsic_control/intrinsic/icon/testing:malloc_test",
        "//intrinsic_kinematics/intrinsic/kinematics/types:cart_state",
        "//intrinsic_kinematics/intrinsic/kinematics/types:cartesian_limits",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/eigenmath:manifolds",
        "//intrinsic_sdk/intrinsic/eigenmath:so3",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "@com_gitlab_libeigen_eigen//:eigen",
        "@com_google_googletest//:gtest",
    ],
)

cc_test_and_malloc_test(
    name = "cartesian_velocity_reflexxes_test",
    timeout = "long",
    srcs = [
        "cartesian_velocity_reflexxes_test.cc",
        "finite_difference_test.h",
    ],
    deps = [
        ":cartesian_velocity_reflexxes",
        "//intrinsic_control/intrinsic/icon/reflexxes",
        "//intrinsic_control/intrinsic/icon/testing:malloc_test",
        "//intrinsic_kinematics/intrinsic/kinematics/types:cart_state",
        "//intrinsic_kinematics/intrinsic/kinematics/types:cartesian_limits",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/eigenmath:manifolds",
        "//intrinsic_sdk/intrinsic/eigenmath:matchers",
        "//intrinsic_sdk/intrinsic/eigenmath:so3",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "@com_gitlab_libeigen_eigen//:eigen",
        "@com_google_googletest//:gtest",
    ],
)

cc_library(
    name = "reflexxes_test_utils",
    testonly = True,
    hdrs = ["reflexxes_test_utils.h"],
    deps = [
        ":joint_position_reflexxes",
        ":joint_velocity_reflexxes",
        "//intrinsic_control/intrinsic/icon/reflexxes",
        "//intrinsic_control/intrinsic/icon/testing:malloc_test",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "@com_google_cel_cpp//internal:testing_no_main",
        "@com_google_googletest//:gtest",
    ],
)

cc_test_and_malloc_test(
    name = "joint_position_reflexxes_test",
    srcs = ["joint_position_reflexxes_test.cc"],
    deps = [
        ":joint_limit_checker",
        ":joint_position_reflexxes",
        ":reflexxes_test_utils",
        "//intrinsic/math/signals:numerical_differentiation",
        "//intrinsic_control/intrinsic/icon/reflexxes",
        "//intrinsic_control/intrinsic/icon/testing:malloc_test",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_matchers",
        "//intrinsic_kinematics/intrinsic/kinematics/testing:trajectory_plot",
        "//intrinsic_kinematics/intrinsic/kinematics/types:check_joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:dynamic_limits_check_mode",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_trajectories",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/eigenmath:matchers",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
        "@com_google_cel_cpp//internal:testing_no_main",
        "@com_google_googletest//:gtest",
    ],
)

cc_test_and_malloc_test(
    name = "joint_velocity_reflexxes_test",
    srcs = ["joint_velocity_reflexxes_test.cc"],
    deps = [
        ":joint_velocity_reflexxes",
        ":reflexxes_test_utils",
        "//intrinsic_control/intrinsic/icon/reflexxes",
        "//intrinsic_control/intrinsic/icon/testing:malloc_test",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_matchers",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/eigenmath:matchers",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@com_gitlab_libeigen_eigen//:eigen",
        "@com_google_cel_cpp//internal:testing_no_main",
        "@com_google_googletest//:gtest",
    ],
)

cc_test_and_malloc_test(
    name = "joint_stop_trajectory_test",
    srcs = ["joint_stop_trajectory_test.cc"],
    deps = [
        ":libjoint_stop_trajectory",
        "//intrinsic_control/intrinsic/icon/testing:malloc_test",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "@com_google_googletest//:gtest",
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

cc_test_and_malloc_test(
    name = "joint_limit_checker_test",
    srcs = ["joint_limit_checker_test.cc"],
    deps = [
        ":joint_limit_checker",
        "//intrinsic_control/intrinsic/icon/testing:malloc_test",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_matchers",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/eigenmath:matchers",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/time",
        "@com_google_cel_cpp//internal:testing_no_main",
        "@com_google_googletest//:gtest",
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

cc_test(
    name = "directional_stiffness_test",
    srcs = ["directional_stiffness_test.cc"],
    deps = [
        ":directional_stiffness",
        "//intrinsic/math/testing:fuzz_math_utils",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/eigenmath:matchers",
        "@abseil-cpp//absl/flags:parse",
        "@com_google_cel_cpp//internal:testing_no_main",
        "@com_google_fuzztest//fuzztest",
        "@com_google_fuzztest//fuzztest:init_fuzztest",
        "@com_google_googletest//:gtest",
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

cc_test_and_malloc_test(
    name = "wrench_stability_monitor_factory_test",
    srcs = ["wrench_stability_monitor_factory_ioc_test.cc"],
    deps = [
        ":wrench_stability_monitor",
        ":wrench_stability_monitor_factory",
        "//intrinsic_control/intrinsic/icon/testing:malloc_test",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_matchers",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:twist",
        "@abseil-cpp//absl/status",
        "@com_google_googletest//:gtest",
        "@intrinsic_apis//intrinsic/icon/equipment:force_control_settings_cc_proto",
    ],
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
