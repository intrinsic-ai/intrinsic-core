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
    name = "function_linearizer_numdiff",
    srcs = ["function_linearizer_numdiff.cc"],
    hdrs = ["function_linearizer_numdiff.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/eigenmath",
        "@abseil-cpp//absl/functional:any_invocable",
        "@abseil-cpp//absl/functional:function_ref",
        "@abseil-cpp//absl/log:check",
        "@com_gitlab_libeigen_eigen//:eigen",
    ],
)

cc_library(
    name = "costfunction_interface",
    hdrs = ["costfunction_interface.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/eigenmath",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "constraint_interface",
    hdrs = ["constraint_interface.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/eigenmath",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "nlopt_nonlinear_program",
    srcs = ["nlopt_nonlinear_program.cc"],
    hdrs = ["nlopt_nonlinear_program.h"],
    deps = [
        ":constraint_interface",
        ":costfunction_interface",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/util/status:ret_check",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@magic_enum",
        "@nlopt",
    ],
)

cc_library(
    name = "ruiz_equilibration",
    srcs = ["ruiz_equilibration.cc"],
    hdrs = ["ruiz_equilibration.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "simplex",
    srcs = ["simplex.cc"],
    hdrs = ["simplex.h"],
    deps = [
        ":ruiz_equilibration",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "linear_constraint_utils",
    hdrs = ["linear_constraint_utils.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/eigenmath",
        "@com_gitlab_libeigen_eigen//:eigen",
    ],
)

cc_library(
    name = "incremental_convex_hull_3d",
    srcs = ["incremental_convex_hull_3d.cc"],
    hdrs = ["incremental_convex_hull_3d.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
        "@com_gitlab_libeigen_eigen//:eigen",
    ],
)

cc_library(
    name = "polytope_utils",
    srcs = ["polytope_utils.cc"],
    hdrs = ["polytope_utils.h"],
    deps = [
        ":incremental_convex_hull_3d",
        ":linear_constraint_utils",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:almost_equals",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@cgal",
        "@com_gitlab_libeigen_eigen//:eigen",
        "@qhull//:libqhullcpp",
    ],
)
