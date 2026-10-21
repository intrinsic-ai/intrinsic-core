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
load("//intrinsic_sdk/intrinsic/platform:flatbuffers.bzl", "cc_flatbuffers_library", "flatbuffers_library")

package(default_visibility = [
    "//intrinsic_control/intrinsic/icon:__subpackages__",
    "//visibility:public",
])

flatbuffers_library(
    name = "transform_types_fbs",
    srcs = ["transform_types.fbs"],
)

cc_flatbuffers_library(
    name = "transform_types_fbs_cc",
    deps = [":transform_types_fbs"],
)

flatbuffers_library(
    name = "matrix_types_fbs",
    srcs = ["matrix_types.fbs"],
)

cc_flatbuffers_library(
    name = "matrix_types_fbs_cc",
    deps = [":matrix_types_fbs"],
)

cc_library(
    name = "matrix_types_utils",
    srcs = ["matrix_types_utils.cc"],
    hdrs = ["matrix_types_utils.h"],
    deps = [
        ":matrix_types_fbs_cc",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "@com_github_google_flatbuffers//:runtime_cc",
    ],
)

cc_library(
    name = "transform_types",
    srcs = [
        "transform_types.cc",
    ],
    hdrs = [
        "transform_types.h",
    ],
    deps = [
        ":transform_types_fbs_cc",
        "@com_github_google_flatbuffers//:flatbuffers",
        "@com_gitlab_libeigen_eigen//:eigen",
    ],
)

cc_library(
    name = "control_types_utils",
    srcs = [
        "control_types_copy.cc",
        "control_types_view.cc",
    ],
    hdrs = [
        "control_types_copy.h",
        "control_types_view.h",
    ],
    deps = [
        ":transform_types",
        ":transform_types_fbs_cc",
        ":transform_utils",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:twist",
        "@com_github_google_flatbuffers//:runtime_cc",
    ],
)

cc_library(
    name = "transform_utils",
    srcs = [
        "transform_copy.cc",
        "transform_view.cc",
    ],
    hdrs = [
        "transform_copy.h",
        "transform_view.h",
    ],
    deps = [
        ":transform_types",
        ":transform_types_fbs_cc",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "@com_github_google_flatbuffers//:runtime_cc",
        "@com_gitlab_libeigen_eigen//:eigen",
    ],
)

cc_library(
    name = "flatbuffer_utils",
    hdrs = ["flatbuffer_utils.h"],
    deps = [
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "@com_github_google_flatbuffers//:runtime_cc",
    ],
)

cc_library(
    name = "fixed_string",
    hdrs = ["fixed_string.h"],
    deps = [
        "//intrinsic_control/intrinsic/icon/utils:fixed_string",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_control/intrinsic/icon/utils:strerror",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/strings:string_view",
        "@com_github_google_flatbuffers//:runtime_cc",
    ],
)
