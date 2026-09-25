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

exports_files(
    srcs = [
        "cc_test_and_malloc_test.bzl",
        "mainloop_loopback_hwm_integration_test_helper.cc",
        "mainloop_loopback_hwm_integration_test_helper.h",
        "mainloop_loopback_hwm_integration_test_helper_test.cc",
        "malloc_test.cc",
        "malloc_test.h",
        "malloc_test_main.cc",
        "malloc_test_test.cc",
        "mock_tcp_client.h",
        "realtime_annotations.h",
    ],
    visibility = [
        "//intrinsic_control/intrinsic/icon/testing/static_analysis:__subpackages__",
    ],
)

cc_library(
    name = "realtime_annotations",
    hdrs = ["realtime_annotations.h"],
    visibility = [
        "//intrinsic:intrinsic_internal",
        "//intrinsic_control/intrinsic/icon:hardware_module_authors",
        "//intrinsic_control/intrinsic/icon:rtcl_users",
    ],
)

cc_library(
    name = "malloc_test",
    testonly = True,
    srcs = ["malloc_test.cc"],
    hdrs = ["malloc_test.h"],
    visibility = ["//intrinsic_control/intrinsic/icon:rtcl_users"],
    deps = [
        "//intrinsic_control/intrinsic/icon/utils:malloc_guard",
        "//intrinsic_control/intrinsic/icon/utils:realtime_guard",
        "//intrinsic_control/intrinsic/icon/utils:realtime_stack_trace",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log:check",
        "@com_google_googletest//:gtest",
    ],
)

cc_library(
    name = "mock_tcp_client",
    testonly = True,
    hdrs = ["mock_tcp_client.h"],
    deps = [
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:tcp_server_client",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
        "@com_google_googletest//:gtest",
    ],
)

cc_library(
    name = "malloc_test_main",
    testonly = True,
    srcs = ["malloc_test_main.cc"],
    visibility = ["//intrinsic_control/intrinsic/icon:rtcl_users"],
    deps = [
        "//intrinsic_control/intrinsic/icon/utils:log",
        "@com_google_googletest//:gtest",
    ],
)
