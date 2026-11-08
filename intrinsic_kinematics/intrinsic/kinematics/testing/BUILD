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
load("//bazel:python.bzl", "py_binary")

cc_library(
    name = "trajectory_plot",
    testonly = True,
    srcs = ["trajectory_plot.cc"],
    hdrs = ["trajectory_plot.h"],
    data = [
        ":timeseries_plot",
        ":trajectory_plotter",
        ":xy_plotter",
    ],
    visibility = [
        "//intrinsic:intrinsic_internal",
    ],
    deps = [
        "//intrinsic/eigenmath:eigen_file_export",
        "//intrinsic/testing/undeclared:undeclared_outputs",
        "//intrinsic/util:subprocess",
        "//intrinsic_kinematics/intrinsic/kinematics/types:cart_state",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_trajectories",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/util/path_resolver",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/container:btree",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
        "@or_tools//ortools/base:file",
        "@or_tools//ortools/base:path",
        "@rules_cc//cc/runfiles",
    ],
)

py_binary(
    name = "trajectory_plotter",
    srcs = ["trajectory_plotter.py"],
    deps = [
        "@ai_intrinsic_sdks_pip_deps//matplotlib",
        "@ai_intrinsic_sdks_pip_deps//numpy",
        "@com_google_absl_py//absl:app",
        "@com_google_absl_py//absl/flags",
    ],
)

py_binary(
    name = "timeseries_plot",
    srcs = ["timeseries_plot.py"],
    deps = [
        "@ai_intrinsic_sdks_pip_deps//matplotlib",
        "@ai_intrinsic_sdks_pip_deps//pandas",
        "@com_google_absl_py//absl:app",
        "@com_google_absl_py//absl/flags",
    ],
)

py_binary(
    name = "xy_plotter",
    srcs = ["xy_plotter.py"],
    deps = [
        "@ai_intrinsic_sdks_pip_deps//matplotlib",
        "@ai_intrinsic_sdks_pip_deps//numpy",
        "@com_google_absl_py//absl:app",
        "@com_google_absl_py//absl/flags",
    ],
)
