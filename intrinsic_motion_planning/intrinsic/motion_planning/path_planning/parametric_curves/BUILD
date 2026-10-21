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
    name = "parametric_curve",
    hdrs = ["parametric_curve.h"],
    deps = [
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_library(
    name = "parametric_curve_utils",
    srcs = ["parametric_curve_utils.cc"],
    hdrs = ["parametric_curve_utils.h"],
    deps = [
        ":joint_parametric_curve",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning/planners:joint_sampling_utils",
        "//intrinsic_motion_planning/intrinsic/motion_planning/trajectory_planning/path_refinement:path_refinement_utils",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_library(
    name = "cartesian_parametric_curve",
    hdrs = ["cartesian_parametric_curve.h"],
    deps = [
        ":parametric_curve",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_library(
    name = "cartesian_parametric_curve_utils",
    srcs = ["cartesian_parametric_curve_utils.cc"],
    hdrs = ["cartesian_parametric_curve_utils.h"],
    deps = [
        ":cartesian_parametric_curve",
        "//intrinsic_sdk/intrinsic/math:almost_equals",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "cartesian_polyline_bsplines_curve",
    srcs = ["cartesian_polyline_bsplines_curve.cc"],
    hdrs = ["cartesian_polyline_bsplines_curve.h"],
    deps = [
        ":cartesian_parametric_curve",
        ":parametric_curve_utils",
        "//intrinsic/math/spline:bspline",
        "//intrinsic/math/spline:bspline_base",
        "//intrinsic/math/spline:bspline_utils",
        "//intrinsic/math/spline:bsplineq",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/eigenmath:pose3_utils",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_library(
    name = "joint_parametric_curve",
    hdrs = ["joint_parametric_curve.h"],
    deps = [
        ":parametric_curve",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "@abseil-cpp//absl/status:statusor",
    ],
)
