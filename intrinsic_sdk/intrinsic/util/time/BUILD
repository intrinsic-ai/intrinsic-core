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
    name = "clock",
    srcs = ["clock.cc"],
    hdrs = ["clock.h"],
    deps = ["@abseil-cpp//absl/time"],
)

cc_library(
    name = "deadline_timeout",
    srcs = ["deadline_timeout.cc"],
    hdrs = ["deadline_timeout.h"],
    deps = ["@abseil-cpp//absl/time"],
)

cc_library(
    name = "time",
    srcs = [
        "clock_steady.cc",
        "time.cc",
    ],
    hdrs = [
        "clock_steady.h",
        "clock_steady_interface.h",
        "time.h",
    ],
    deps = [
        "//intrinsic_control/intrinsic/icon/testing:realtime_annotations",
        "@abseil-cpp//absl/time",
        "@com_google_googletest//:gtest_prod",
    ],
)

cc_library(
    name = "timer",
    srcs = [
        "elapsed_timer.cc",
    ],
    hdrs = [
        "elapsed_timer.h",
    ],
    deps = [
        ":time",
        "@abseil-cpp//absl/time",
    ],
)
