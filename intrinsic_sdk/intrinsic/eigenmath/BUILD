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
    name = "ceres",
    hdrs = ["ceres_poses.h"],
    deps = [
        ":so2",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "@com_gitlab_libeigen_eigen//:eigen",
        "@com_google_ceres_solver//:ceres",
    ],
)

cc_library(
    name = "eigenmath",
    hdrs = [
        "types.h",
    ],
    deps = ["@com_gitlab_libeigen_eigen//:eigen"],
)

cc_library(
    name = "eigen_matrix_hash",
    hdrs = ["eigen_matrix_hash.h"],
    deps = [
        "@com_gitlab_libeigen_eigen//:eigen",
    ],
)

cc_library(
    name = "manifolds",
    hdrs = [
        "manifolds.h",
        "su2_impl.h",
    ],
    deps = [
        ":eigenmath",
        ":so3",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "@abseil-cpp//absl/log:absl_check",
        "@com_gitlab_libeigen_eigen//:eigen",
    ],
)

cc_library(
    name = "pose2",
    hdrs = ["pose2.h"],
    deps = [
        ":eigenmath",
        ":so2",
        "@abseil-cpp//absl/base:core_headers",
    ],
)

cc_library(
    name = "pose3_utils",
    hdrs = [
        "pose3_utils.h",
    ],
    deps = [
        ":eigenmath",
        ":manifolds",
        ":pose2",
        ":rotation_utils",
        ":skew_symmetric_matrix",
        "//intrinsic_sdk/intrinsic/math:pose3",
    ],
)

cc_library(
    name = "rotation_utils",
    hdrs = ["rotation_utils.h"],
    deps = [
        ":eigenmath",
        "@abseil-cpp//absl/log:absl_check",
        "@com_gitlab_libeigen_eigen//:eigen",
    ],
)

cc_library(
    name = "so2",
    hdrs = ["so2.h"],
    deps = [
        ":eigenmath",
        ":rotation_utils",
        "@abseil-cpp//absl/log:absl_check",
        "@com_gitlab_libeigen_eigen//:eigen",
    ],
)

cc_library(
    name = "so3",
    hdrs = ["so3.h"],
    deps = [
        ":eigenmath",
        ":rotation_utils",
        "//intrinsic_control/intrinsic/icon/testing:realtime_annotations",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_sdk/intrinsic/util/status:status_builder",
        "@abseil-cpp//absl/log:absl_check",
        "@abseil-cpp//absl/status",
        "@com_gitlab_libeigen_eigen//:eigen",
    ],
)

cc_library(
    name = "clamp",
    hdrs = [
        "clamp.h",
    ],
    deps = [
        "@abseil-cpp//absl/base:core_headers",
    ],
)

cc_library(
    name = "skew_symmetric_matrix",
    hdrs = ["skew_symmetric_matrix.h"],
    deps = [
        "@com_gitlab_libeigen_eigen//:eigen",
    ],
)
