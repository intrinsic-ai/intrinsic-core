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

package(default_visibility = [
    "//visibility:public",
])

cc_library(
    name = "bspline_base",
    srcs = [
        "bspline_base.cc",
    ],
    hdrs = [
        "bspline_base.h",
    ],
    deps = [
        "//intrinsic_control/intrinsic/icon/utils:log",
        "//intrinsic_control/intrinsic/icon/utils:realtime_guard",
        "//intrinsic_sdk/intrinsic/math:almost_equals",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings:str_format",
    ],
)

cc_library(
    name = "bspline",
    hdrs = [
        "bspline.h",
    ],
    deps = [
        ":bspline_base",
        "//intrinsic/math/proto:bspline_cc_proto",
        "//intrinsic_control/intrinsic/icon/utils:log",
        "//intrinsic_control/intrinsic/icon/utils:realtime_guard",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_library(
    name = "bsplineq",
    srcs = [
        "bsplineq.cc",
    ],
    hdrs = [
        "bsplineq.h",
    ],
    deps = [
        ":bspline_base",
        "//intrinsic_control/intrinsic/icon/utils:log",
        "//intrinsic_control/intrinsic/icon/utils:realtime_guard",
        "//intrinsic_sdk/intrinsic/eigenmath",
    ],
)

cc_library(
    name = "bspline_utils",
    srcs = [
        "bspline_utils.cc",
    ],
    hdrs = [
        "bspline_utils.h",
    ],
    deps = [
        ":bspline",
        ":bspline_base",
        "//intrinsic_control/intrinsic/icon/utils:realtime_guard",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/eigenmath:manifolds",
        "//intrinsic_sdk/intrinsic/math:almost_equals",
        "//intrinsic_sdk/intrinsic/math:ipow",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_library(
    name = "bspline_sampler",
    hdrs = ["bspline_sampler.h"],
    deps = [
        ":bspline",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_library(
    name = "uniform_bspline_sampler",
    srcs = ["uniform_bspline_sampler.cc"],
    hdrs = ["uniform_bspline_sampler.h"],
    deps = [
        ":bspline",
        ":bspline_sampler",
        "//intrinsic/math:linspace",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_test(
    name = "uniform_bspline_sampler_test",
    srcs = ["uniform_bspline_sampler_test.cc"],
    deps = [
        ":bspline",
        ":uniform_bspline_sampler",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@com_google_cel_cpp//internal:testing_no_main",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "uniform_between_knots_bspline_sampler",
    srcs = ["uniform_between_knots_bspline_sampler.cc"],
    hdrs = ["uniform_between_knots_bspline_sampler.h"],
    deps = [
        ":bspline",
        ":bspline_sampler",
        ":bspline_utils",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_test(
    name = "uniform_between_knots_bspline_sampler_test",
    srcs = ["uniform_between_knots_bspline_sampler_test.cc"],
    deps = [
        ":bspline",
        ":uniform_between_knots_bspline_sampler",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@com_google_cel_cpp//internal:testing_no_main",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "polynomial_spline",
    srcs = [
        "polynomial_spline.cc",
    ],
    hdrs = [
        "polynomial_spline.h",
    ],
    deps = [
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_kinematics/intrinsic/kinematics/types:cart_state",
        "//intrinsic_kinematics/intrinsic/kinematics/types:state_rn",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/eigenmath:rotation_utils",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "@abseil-cpp//absl/functional:function_ref",
    ],
)

cc_library(
    name = "spline_parameter_transform_function",
    hdrs = ["spline_parameter_transform_function.h"],
    deps = [
        ":monotonically_increasing_spline",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_library(
    name = "spline_parameter_converter",
    srcs = ["spline_parameter_converter.cc"],
    hdrs = ["spline_parameter_converter.h"],
    deps = [
        ":monotonically_increasing_spline",
        ":spline_parameter_transform_function",
        "//intrinsic/math:linspace",
        "//intrinsic/math:within_margin",
        "//intrinsic_sdk/intrinsic/math:almost_equals",
        "//intrinsic_sdk/intrinsic/math:ipow",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_library(
    name = "bspline_parameter_integral_transform_function",
    srcs = ["bspline_parameter_integral_transform_function.cc"],
    hdrs = ["bspline_parameter_integral_transform_function.h"],
    deps = [
        ":bspline",
        ":bspline_parameter_transform_function_integrand",
        ":bspline_utils",
        ":monotonically_increasing_spline",
        ":spline_parameter_transform_function",
        "//intrinsic/math:linspace",
        "//intrinsic/math:time_series_utils",
        "//intrinsic/math/signals:adaptive_quadrature",
        "//intrinsic/math/signals:gauss_kronrod_quadrature",
        "//intrinsic/math/signals:gauss_legendre_quadrature",
        "//intrinsic_sdk/intrinsic/math:almost_equals",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/functional:any_invocable",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_library(
    name = "bspline_parameter_transform_function_integrand",
    srcs = ["bspline_parameter_transform_function_integrand.cc"],
    hdrs = ["bspline_parameter_transform_function_integrand.h"],
    deps = [
        ":bspline",
        ":bspline_utils",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_sdk/intrinsic/math:almost_equals",
        "//intrinsic_sdk/intrinsic/math:ipow",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "monotonically_increasing_spline",
    srcs = ["monotonically_increasing_spline.cc"],
    hdrs = ["monotonically_increasing_spline.h"],
    deps = [
        "//intrinsic/math:time_series_utils",
        "//intrinsic_sdk/intrinsic/math:almost_equals",
        "//intrinsic_sdk/intrinsic/math:ipow",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
    ],
)
