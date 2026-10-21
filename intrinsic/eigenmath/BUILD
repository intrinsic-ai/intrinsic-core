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
    name = "halton_sequence",
    hdrs = [
        "halton_sequence.h",
    ],
    deps = [
        "@boost.math",
        "@com_gitlab_libeigen_eigen//:eigen",
    ],
)

cc_library(
    name = "interpolation",
    srcs = [
        "interpolation.cc",
    ],
    hdrs = [
        "interpolation.h",
    ],
    deps = [
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/eigenmath:manifolds",
        "//intrinsic_sdk/intrinsic/eigenmath:pose2",
        "//intrinsic_sdk/intrinsic/eigenmath:so2",
        "//intrinsic_sdk/intrinsic/eigenmath:so3",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "matchers",
    testonly = True,
    hdrs = ["matchers.h"],
    deps = [
        ":type_checks",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:proto_conversion",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/strings",
        "@com_github_inazarenko_protobuf_matchers//protobuf-matchers",
        "@com_gitlab_libeigen_eigen//:eigen",
        "@com_google_googletest//:gtest",
        "@com_google_protobuf//:protobuf",
        "@gloop//gloop/util/gtl:iterator_adaptors",
        "@intrinsic_apis//intrinsic/messages:messages_cc_proto",
    ],
)

cc_library(
    name = "scalar_utils",
    hdrs = ["scalar_utils.h"],
)

cc_library(
    name = "type_checks",
    hdrs = [
        "type_checks.h",
    ],
    deps = [
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/eigenmath:pose2",
        "//intrinsic_sdk/intrinsic/eigenmath:so2",
        "//intrinsic_sdk/intrinsic/eigenmath:so3",
        "//intrinsic_sdk/intrinsic/math:pose3",
    ],
)

cc_library(
    name = "random_number_utils",
    srcs = ["random_number_utils.cc"],
    hdrs = ["random_number_utils.h"],
    deps = [
        ":halton_sequence",
        "//intrinsic_control/intrinsic/icon/testing:realtime_annotations",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/log:absl_check",
        "@abseil-cpp//absl/random",
        "@abseil-cpp//absl/random:seed_sequences",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:string_view",
    ],
)

cc_library(
    name = "invert_lower_triangular_matrix",
    srcs = [
        "invert_lower_triangular_matrix.cc",
    ],
    hdrs = [
        "invert_lower_triangular_matrix.h",
    ],
    deps = [
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_sdk/intrinsic/eigenmath",
    ],
)

cc_library(
    name = "pseudo_inverse",
    hdrs = ["pseudo_inverse.h"],
    deps = [
        "//intrinsic_control/intrinsic/icon/testing:realtime_annotations",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:almost_equals",
        "//intrinsic_sdk/intrinsic/math:ipow",
        "@com_gitlab_libeigen_eigen//:eigen",
    ],
)

cc_library(
    name = "swing_twist",
    hdrs = ["swing_twist.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/eigenmath:so3",
        "@com_gitlab_libeigen_eigen//:eigen",
    ],
)
