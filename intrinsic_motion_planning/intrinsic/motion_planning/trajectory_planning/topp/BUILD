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

load("@com_google_protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("@io_bazel_rules_go//proto:def.bzl", "go_proto_library")
load("//bazel:cc_macros.bzl", "cc_library")
load("//bazel:protos.bzl", "proto_library")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "elliptic_integral",
    srcs = ["elliptic_integral.cc"],
    hdrs = ["elliptic_integral.h"],
    deps = [
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_sdk/intrinsic/math:almost_equals",
    ],
)

cc_library(
    name = "topp_polynomial_roots",
    srcs = ["topp_polynomial_roots.cc"],
    hdrs = ["topp_polynomial_roots.h"],
    deps = [
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_sdk/intrinsic/math:almost_equals",
    ],
)

cc_library(
    name = "topp_polynomials",
    srcs = ["topp_polynomials.cc"],
    hdrs = ["topp_polynomials.h"],
    deps = [
        ":topp_polynomial_roots",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:almost_equals",
        "//intrinsic_sdk/intrinsic/math:ipow",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
    ],
)

cc_library(
    name = "topp_analytical_polynomial_integrals",
    srcs = ["topp_analytical_polynomial_integrals.cc"],
    hdrs = ["topp_analytical_polynomial_integrals.h"],
    deps = [
        ":elliptic_integral",
        ":topp_polynomial_roots",
        ":topp_polynomials",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:ipow",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
    ],
)

proto_library(
    name = "bspline_squared_path_velocity_proto",
    srcs = ["bspline_squared_path_velocity.proto"],
    # keep
    strip_import_prefix = "/intrinsic_motion_planning",
    deps = [
        "//intrinsic/math/proto:bspline_proto",
    ],
)

cc_proto_library(
    name = "bspline_squared_path_velocity_cc_proto",
    deps = [
        ":bspline_squared_path_velocity_proto",
    ],
)

cc_library(
    name = "squared_path_velocity_interface",
    hdrs = ["squared_path_velocity_interface.h"],
    deps = [
        ":bspline_squared_path_velocity_cc_proto",
        ":topp_trajectory_result_cc_proto",
        "//intrinsic_sdk/intrinsic/util:fixed_vector",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_library(
    name = "bspline_squared_path_velocity",
    srcs = ["bspline_squared_path_velocity.cc"],
    hdrs = ["bspline_squared_path_velocity.h"],
    deps = [
        ":bspline_squared_path_velocity_cc_proto",
        ":integrate_cubic_squared_path_velocity_polynomial",
        ":integrate_squared_path_velocity_polynomial",
        ":squared_path_velocity_interface",
        ":topp_analytical_polynomial_integrals",
        ":topp_polynomials",
        ":topp_solver_commons",
        ":topp_trajectory_result_cc_proto",
        "//intrinsic/math:time_series_utils",
        "//intrinsic/math/signals:gauss_legendre_quadrature",
        "//intrinsic/math/spline:bspline",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:almost_equals",
        "//intrinsic_sdk/intrinsic/math:ipow",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
        "@com_gitlab_libeigen_eigen//:eigen",
    ],
)

cc_library(
    name = "integrate_cubic_squared_path_velocity_polynomial",
    srcs = ["integrate_cubic_squared_path_velocity_polynomial.cc"],
    hdrs = ["integrate_cubic_squared_path_velocity_polynomial.h"],
    deps = [
        ":topp_analytical_polynomial_integrals",
        ":topp_polynomials",
        "//intrinsic/math/signals:gauss_legendre_quadrature",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "integrate_squared_path_velocity_polynomial",
    srcs = ["integrate_squared_path_velocity_polynomial.cc"],
    hdrs = ["integrate_squared_path_velocity_polynomial.h"],
    deps = [
        ":topp_polynomials",
        "//intrinsic_kinematics/intrinsic/kinematics/types:to_string",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:ipow",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
)

proto_library(
    name = "joint_optimization_options_proto",
    srcs = ["joint_optimization_options.proto"],
    # keep
    strip_import_prefix = "/intrinsic_motion_planning",
)

cc_proto_library(
    name = "joint_optimization_options_cc_proto",
    deps = [
        ":joint_optimization_options_proto",
    ],
)

cc_library(
    name = "path_and_trajectory",
    hdrs = ["path_and_trajectory.h"],
    deps = [
        ":topp_trajectory_result",
        "//intrinsic_motion_planning/intrinsic/motion_planning/trajectory_planning/path_refinement:spline_based_path_refinement",
    ],
)

proto_library(
    name = "path_sample_proto",
    srcs = ["path_sample.proto"],
    # keep
    strip_import_prefix = "/intrinsic_motion_planning",
    deps = [
        "@intrinsic_apis//intrinsic/icon/proto:cart_space_proto",
        "@intrinsic_apis//intrinsic/kinematics/types:joint_limits_proto",
        "@intrinsic_apis//intrinsic/math/proto:pose_proto",
    ],
)

cc_proto_library(
    name = "path_sample_cc_proto",
    deps = [
        ":path_sample_proto",
    ],
)

cc_library(
    name = "path_sample",
    srcs = ["path_sample.cc"],
    hdrs = ["path_sample.h"],
    deps = [
        ":path_sample_cc_proto",
        "//intrinsic_control/intrinsic/icon/proto:cart_space_conversion",
        "//intrinsic_control/intrinsic/icon/proto:eigen_conversion",
        "//intrinsic_kinematics/intrinsic/kinematics/types:cartesian_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:proto_conversion",
        "//intrinsic_sdk/intrinsic/util:eigen",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@intrinsic_apis//intrinsic/icon/proto:joint_space_cc_proto",
    ],
)

cc_library(
    name = "refine_path_and_compute_acceleration_limited_trajectory",
    srcs = ["refine_path_and_compute_acceleration_limited_trajectory.cc"],
    hdrs = ["refine_path_and_compute_acceleration_limited_trajectory.h"],
    deps = [
        ":joint_optimization_options_cc_proto",
        ":path_and_trajectory",
        ":path_sample",
        ":topp_solver_commons",
        ":topp_trajectory_result",
        "//intrinsic/math/spline:bspline_sampler",
        "//intrinsic/math/spline:uniform_between_knots_bspline_sampler",
        "//intrinsic_control/intrinsic/icon/dynamics:rigid_body_interface",
        "//intrinsic_kinematics/intrinsic/kinematics:group",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning:path_segment",
        "//intrinsic_motion_planning/intrinsic/motion_planning/trajectory_planning:optimized_toppra",
        "//intrinsic_motion_planning/intrinsic/motion_planning/trajectory_planning/path_refinement:path_refinement_utils",
        "//intrinsic_motion_planning/intrinsic/motion_planning/trajectory_planning/path_refinement:spline_based_path_refinement",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_library(
    name = "topp_options",
    srcs = ["topp_options.cc"],
    hdrs = ["topp_options.h"],
    deps = [
        ":joint_optimization_options_cc_proto",
    ],
)

proto_library(
    name = "piecewise_linear_squared_path_velocity_proto",
    srcs = ["piecewise_linear_squared_path_velocity.proto"],
    # keep
    strip_import_prefix = "/intrinsic_motion_planning",
)

cc_proto_library(
    name = "piecewise_linear_squared_path_velocity_cc_proto",
    deps = [
        ":piecewise_linear_squared_path_velocity_proto",
    ],
)

proto_library(
    name = "topp_trajectory_result_proto",
    srcs = ["topp_trajectory_result.proto"],
    # keep
    strip_import_prefix = "/intrinsic_motion_planning",
    deps = [
        "//intrinsic_motion_planning/intrinsic/motion_planning/proto/v1:motion_events_proto",
        "//intrinsic_motion_planning/intrinsic/motion_planning/trajectory_planning/topp:bspline_squared_path_velocity_proto",
        "//intrinsic_motion_planning/intrinsic/motion_planning/trajectory_planning/topp:piecewise_linear_squared_path_velocity_proto",
        "@intrinsic_apis//intrinsic/icon/proto:joint_space_proto",
    ],
)

cc_proto_library(
    name = "topp_trajectory_result_cc_proto",
    deps = [
        ":topp_trajectory_result_proto",
    ],
)

cc_library(
    name = "piecewise_linear_squared_path_velocity",
    srcs = ["piecewise_linear_squared_path_velocity.cc"],
    hdrs = ["piecewise_linear_squared_path_velocity.h"],
    deps = [
        ":phase_space_target_generators",
        ":piecewise_linear_squared_path_velocity_cc_proto",
        ":squared_path_velocity_interface",
        ":topp_trajectory_result_cc_proto",
        "//intrinsic/math:time_series_utils",
        "//intrinsic_sdk/intrinsic/util:fixed_vector",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_library(
    name = "topp_trajectory_result",
    srcs = ["topp_trajectory_result.cc"],
    hdrs = ["topp_trajectory_result.h"],
    deps = [
        ":bspline_squared_path_velocity",
        ":piecewise_linear_squared_path_velocity",
        ":reachability_node",
        ":squared_path_velocity_interface",
        ":topp_trajectory_result_cc_proto",
        "//intrinsic_control/intrinsic/icon/proto:joint_trajectory_conversion",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_trajectories",
        "//intrinsic_motion_planning/intrinsic/motion_planning/proto/v1:motion_events_cc_proto",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_library(
    name = "trajectory_with_optional_path_samples",
    hdrs = ["trajectory_with_optional_path_samples.h"],
    deps = [
        ":path_sample",
        ":topp_trajectory_result",
    ],
)

cc_library(
    name = "topp_solver_commons",
    srcs = ["topp_solver_commons.cc"],
    hdrs = ["topp_solver_commons.h"],
    deps = [
        ":integrate_squared_path_velocity_polynomial",
        ":joint_optimization_options_cc_proto",
        ":path_sample",
        ":phase_space_cartesian_acceleration_two_norm_constraint",
        "//intrinsic/math:within_margin",
        "//intrinsic_control/intrinsic/icon/control/algorithms:joint_limit_checker",
        "//intrinsic_control/intrinsic/icon/dynamics:rigid_body_interface",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_kinematics/intrinsic/kinematics",
        "//intrinsic_kinematics/intrinsic/kinematics:group",
        "//intrinsic_kinematics/intrinsic/kinematics:state",
        "//intrinsic_kinematics/intrinsic/kinematics/ik:ik_solvers",
        "//intrinsic_kinematics/intrinsic/kinematics/types:cartesian_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:dynamic_limits_check_mode",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_trajectories",
        "//intrinsic_kinematics/intrinsic/kinematics/types:to_fixed_string",
        "//intrinsic_motion_planning/intrinsic/motion_planning/trajectory_planning:interpolate_joint_trajectories",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:almost_equals",
        "//intrinsic_sdk/intrinsic/math:ipow",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_library(
    name = "phase_space_cartesian_acceleration_two_norm_constraint",
    srcs = ["phase_space_cartesian_acceleration_two_norm_constraint.cc"],
    hdrs = ["phase_space_cartesian_acceleration_two_norm_constraint.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/eigenmath",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_library(
    name = "path_velocity_propagator",
    srcs = ["path_velocity_propagator.cc"],
    hdrs = ["path_velocity_propagator.h"],
    deps = [
        ":topp_solver_commons",
        "//intrinsic/math/numopt:simplex",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_library(
    name = "reachability_node",
    hdrs = ["reachability_node.h"],
    deps = [
        "//intrinsic/math/numopt:polytope_utils",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_library(
    name = "phase_space_target_generators",
    srcs = ["phase_space_target_generators.cc"],
    hdrs = ["phase_space_target_generators.h"],
    deps = [
        "//intrinsic/math/numopt:polytope_utils",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:almost_equals",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
    ],
)

go_proto_library(
    name = "bspline_squared_path_velocity_go_proto",
    importpath = "intrinsic/motion_planning/trajectory_planning/topp/bspline_squared_path_velocity_go_proto",
    proto = ":bspline_squared_path_velocity_proto",
    deps = ["//intrinsic/math/proto:bspline_go_proto"],
)

go_proto_library(
    name = "joint_optimization_options_go_proto",
    importpath = "intrinsic/motion_planning/trajectory_planning/topp/joint_optimization_options_go_proto",
    proto = ":joint_optimization_options_proto",
)

go_proto_library(
    name = "path_sample_go_proto",
    importpath = "intrinsic/motion_planning/trajectory_planning/topp/path_sample_go_proto",
    proto = ":path_sample_proto",
    deps = [
        "@intrinsic_apis//intrinsic/icon/proto:cart_space_go_proto",
        "@intrinsic_apis//intrinsic/kinematics/types:joint_limits_go_proto",
        "@intrinsic_apis//intrinsic/math/proto:pose_go_proto",
    ],
)

go_proto_library(
    name = "piecewise_linear_squared_path_velocity_go_proto",
    importpath = "intrinsic/motion_planning/trajectory_planning/topp/piecewise_linear_squared_path_velocity_go_proto",
    proto = ":piecewise_linear_squared_path_velocity_proto",
)

go_proto_library(
    name = "topp_trajectory_result_go_proto",
    importpath = "intrinsic/motion_planning/trajectory_planning/topp/topp_trajectory_result_go_proto",
    proto = ":topp_trajectory_result_proto",
    deps = [
        ":bspline_squared_path_velocity_go_proto",
        ":piecewise_linear_squared_path_velocity_go_proto",
        "//intrinsic_motion_planning/intrinsic/motion_planning/proto/v1:motion_events_go_proto",
        "@intrinsic_apis//intrinsic/icon/proto:joint_space_go_proto",
    ],
)
