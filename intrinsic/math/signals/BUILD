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
    name = "adaptive_quadrature",
    srcs = ["adaptive_quadrature.cc"],
    hdrs = ["adaptive_quadrature.h"],
    deps = [
        ":nested_quadrature",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/functional:function_ref",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "gauss_kronrod_quadrature",
    srcs = ["gauss_kronrod_quadrature.cc"],
    hdrs = ["gauss_kronrod_quadrature.h"],
    deps = [
        ":nested_quadrature",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/functional:function_ref",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_library(
    name = "gauss_legendre_quadrature",
    srcs = ["gauss_legendre_quadrature.cc"],
    hdrs = ["gauss_legendre_quadrature.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/functional:function_ref",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "nested_quadrature",
    hdrs = ["nested_quadrature.h"],
    deps = [
        "@abseil-cpp//absl/functional:function_ref",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_library(
    name = "numerical_differentiation",
    hdrs = ["numerical_differentiation.h"],
    deps = [
        "//intrinsic_control/intrinsic/icon/utils:fixed_str_cat",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_sdk/intrinsic/math:ipow",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
    ],
)
