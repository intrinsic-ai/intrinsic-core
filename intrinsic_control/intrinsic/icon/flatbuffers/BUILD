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

cc_test(
    name = "matrix_types_utils_test",
    srcs = ["matrix_types_utils_test.cc"],
    deps = [
        ":matrix_types_fbs_cc",
        ":matrix_types_utils",
        "//intrinsic/math/testing:fuzz_math_utils",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "@com_google_fuzztest//fuzztest",
        "@com_google_fuzztest//fuzztest:init_fuzztest",
        "@com_google_googletest//:gtest",
    ],
)

flatbuffers_library(
    name = "limit_types_fbs",
    srcs = [
        "limit_types.fbs",
    ],
    deps = [
        ":transform_types_fbs",
    ],
)

cc_flatbuffers_library(
    name = "limit_types_fbs_cc",
    deps = [":limit_types_fbs"],
)

flatbuffers_library(
    name = "state_types_fbs",
    srcs = [
        "state_types.fbs",
    ],
    deps = [
        ":transform_types_fbs",
    ],
)

cc_flatbuffers_library(
    name = "state_types_fbs_cc",
    deps = [":state_types_fbs"],
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
    name = "limit_types",
    srcs = [
        "limit_types.cc",
    ],
    hdrs = [
        "limit_types.h",
    ],
    deps = [
        ":limit_types_fbs_cc",
        ":transform_types",  # fixdeps: keep
        ":transform_types_fbs_cc",
        "@com_github_google_flatbuffers//:runtime_cc",
    ],
)

cc_library(
    name = "state_types",
    srcs = [
        "state_types.cc",
    ],
    hdrs = [
        "state_types.h",
    ],
    deps = [
        ":state_types_fbs_cc",
        ":transform_types",  # fixdeps: keep
        ":transform_types_fbs_cc",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings:str_format",
        "@com_github_google_flatbuffers//:runtime_cc",
    ],
)

cc_test_and_malloc_test(
    name = "test_transform_types",
    srcs = ["test_transform_types.cc"],
    deps = [
        ":transform_types",
        ":transform_types_fbs_cc",
        "@com_github_google_flatbuffers//:runtime_cc",
        "@com_google_googletest//:gtest",
    ],
)

cc_test_and_malloc_test(
    name = "test_transform_eigen",
    srcs = ["test_transform_eigen.cc"],
    deps = [
        ":transform_types",
        ":transform_types_fbs_cc",
        ":transform_utils",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "@com_github_google_flatbuffers//:runtime_cc",
        "@com_google_googletest//:gtest",
    ],
)

cc_test_and_malloc_test(
    name = "test_limit_types",
    srcs = ["test_limit_types.cc"],
    deps = [
        ":limit_types",
        ":limit_types_fbs_cc",
        "@com_github_google_flatbuffers//:runtime_cc",
        "@com_google_googletest//:gtest",
    ],
)

cc_test_and_malloc_test(
    name = "test_state_types",
    srcs = ["test_state_types.cc"],
    deps = [
        ":state_types",
        ":state_types_fbs_cc",
        "@com_github_google_flatbuffers//:runtime_cc",
        "@com_google_googletest//:gtest",
    ],
)

cc_test_and_malloc_test(
    name = "test_control_types",
    srcs = ["test_control_types.cc"],
    deps = [
        ":control_types_utils",
        ":transform_types_fbs_cc",
        "//intrinsic_control/intrinsic/icon/testing:malloc_test",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:twist",
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

cc_test(
    name = "flatbuffer_utils_test",
    srcs = ["flatbuffer_utils_test.cc"],
    deps = [
        ":flatbuffer_utils",
        ":transform_types_fbs_cc",
        "//intrinsic_control/intrinsic/icon/interprocess/shared_memory_manager:segment_info_fbs_cc",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@com_github_google_flatbuffers//:runtime_cc",
        "@com_google_cel_cpp//internal:testing_no_main",
        "@com_google_googletest//:gtest_main",
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

flatbuffers_library(
    name = "fixed_string_test_fbs",
    srcs = [
        "fixed_string_test.fbs",
    ],
)

cc_flatbuffers_library(
    name = "fixed_string_test_fbs_cc",
    deps = [":fixed_string_test_fbs"],
)

cc_test(
    name = "fixed_string_test",
    srcs = ["fixed_string_test.cc"],
    deps = [
        ":fixed_string",
        ":fixed_string_test_fbs_cc",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_matchers",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/types:span",
        "@com_github_google_flatbuffers//:runtime_cc",
        "@com_google_cel_cpp//internal:testing_no_main",
        "@com_google_googletest//:gtest_main",
    ],
)
