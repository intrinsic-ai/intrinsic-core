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
    name = "path_refinement_utils",
    srcs = ["path_refinement_utils.cc"],
    hdrs = ["path_refinement_utils.h"],
    deps = [
        "//intrinsic/eigenmath:interpolation",
        "//intrinsic/math:linspace",
        "//intrinsic/math:time_series_utils",
        "//intrinsic/math/spline:bspline",
        "//intrinsic/math/spline:bspline_utils",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_kinematics/intrinsic/kinematics:group",
        "//intrinsic_kinematics/intrinsic/kinematics:state",
        "//intrinsic_kinematics/intrinsic/kinematics/types:cartesian_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:check_joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_trajectories",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning:path_segment",
        "//intrinsic_motion_planning/intrinsic/motion_planning/proto/v1:motion_events_cc_proto",
        "//intrinsic_motion_planning/intrinsic/motion_planning/trajectory_planning/topp:path_sample",
        "//intrinsic_motion_planning/intrinsic/motion_planning/trajectory_planning/topp:topp_trajectory_result",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/eigenmath:pose3_utils",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/util:proto_time",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_library(
    name = "spline_based_path_refinement",
    srcs = ["spline_based_path_refinement.cc"],
    hdrs = ["spline_based_path_refinement.h"],
    deps = [
        ":path_polyline",
        ":path_refinement_utils",
        "//intrinsic/math:linspace",
        "//intrinsic/math/spline:bspline",
        "//intrinsic/math/spline:bspline_parameter_integral_transform_function",
        "//intrinsic/math/spline:bspline_parameter_transform_function_integrand",
        "//intrinsic/math/spline:bspline_sampler",
        "//intrinsic/math/spline:bspline_utils",
        "//intrinsic/math/spline:spline_parameter_converter",
        "//intrinsic/math/spline:spline_parameter_transform_function",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning:path_segment",
        "//intrinsic_motion_planning/intrinsic/motion_planning/proto/v1:motion_events_cc_proto",
        "//intrinsic_motion_planning/intrinsic/motion_planning/trajectory_planning/topp:path_sample",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:almost_equals",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_library(
    name = "path_polyline",
    srcs = ["path_polyline.cc"],
    hdrs = ["path_polyline.h"],
    deps = [
        ":path_refinement_utils",
        "//intrinsic/math:time_series_utils",
        "//intrinsic/math/spline:bspline_utils",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_kinematics/intrinsic/kinematics/types:cartesian_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:check_joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning:path_segment",
        "//intrinsic_motion_planning/intrinsic/motion_planning/proto/v1:motion_events_cc_proto",
        "//intrinsic_motion_planning/intrinsic/motion_planning/trajectory_planning/topp:path_sample",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:almost_equals",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/util:fixed_vector",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
    ],
)
