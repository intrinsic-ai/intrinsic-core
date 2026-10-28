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

# --------------------------------------------------------------------------------------------------
# The targets below are not exported to the SDK.
# --------------------------------------------------------------------------------------------------

cc_library(
    name = "rt_trace",
    srcs = ["rt_trace.cc"],
    hdrs = ["rt_trace.h"],
    deps = [
        "//intrinsic_control/intrinsic/icon/release:source_location",
        "//intrinsic_control/intrinsic/icon/testing:realtime_annotations",
        "//intrinsic_control/intrinsic/icon/utils:fixed_str_cat",
        "//intrinsic_control/intrinsic/icon/utils:fixed_string",
        "//intrinsic_control/intrinsic/icon/utils:log",
        "//intrinsic_sdk/intrinsic/util/thread:thread_utils",
        "@abseil-cpp//absl/strings",
    ],
)
