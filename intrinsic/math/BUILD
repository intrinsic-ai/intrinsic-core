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
    name = "pluecker_transform",
    hdrs = [
        "pluecker_transform.h",
    ],
    deps = [
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/eigenmath:skew_symmetric_matrix",
        "//intrinsic_sdk/intrinsic/math:pose3",
    ],
)

cc_library(
    name = "within_margin",
    hdrs = ["within_margin.h"],
)

cc_library(
    name = "symmetric_positive_definite_matrix_squareroot",
    srcs = ["symmetric_positive_definite_matrix_squareroot.cc"],
    hdrs = ["symmetric_positive_definite_matrix_squareroot.h"],
    deps = [
        "//intrinsic_control/intrinsic/icon/utils:fixed_str_cat",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:ipow",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@com_gitlab_libeigen_eigen//:eigen",
    ],
)

cc_library(
    name = "linspace",
    hdrs = ["linspace.h"],
    deps = [
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_library(
    name = "projection_matrix_utils",
    srcs = ["projection_matrix_utils.cc"],
    hdrs = [
        "projection_matrix_utils.h",
    ],
    deps = [
        "//intrinsic/util/status:status_macros",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_library(
    name = "tf2_convert_intrinsic",
    hdrs = ["tf2_convert_intrinsic.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "@abseil-cpp//absl/log",
        "@com_google_protobuf//:protobuf",
        "@geometry_msgs//:cc",
        "@intrinsic_apis//intrinsic/math/proto:header_cc_proto",
        "@intrinsic_apis//intrinsic/math/proto:quaternion_cc_proto",
        "@intrinsic_apis//intrinsic/math/proto:transform_cc_proto",
        "@intrinsic_apis//intrinsic/math/proto:transform_stamped_cc_proto",
        "@intrinsic_apis//intrinsic/math/proto:vector3_cc_proto",
    ],
)

cc_library(
    name = "time_series_utils",
    hdrs = ["time_series_utils.h"],
    deps = [
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
    ],
)
