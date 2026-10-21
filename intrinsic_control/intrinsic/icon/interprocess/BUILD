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

package(default_visibility = [
    "//intrinsic/util/thread:__subpackages__",
    "//intrinsic_control/intrinsic/icon:__subpackages__",
    "//intrinsic_hardware/intrinsic/hardware:__subpackages__",
    "//intrinsic_sdk/intrinsic/util/thread:__subpackages__",
])

cc_library(
    name = "binary_futex",
    srcs = ["binary_futex.cc"],
    hdrs = [
        "binary_futex.h",
    ],
    visibility = [
        "//intrinsic/util/thread:__subpackages__",
        "//intrinsic_control/intrinsic/icon:__subpackages__",
        "//intrinsic_hardware/intrinsic/hardware:__subpackages__",
        "//intrinsic_sdk/intrinsic/platform/common/buffers:__pkg__",
        "//intrinsic_sdk/intrinsic/util/thread:__subpackages__",
    ],
    deps = [
        "//intrinsic_control/intrinsic/icon/testing:realtime_annotations",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:strerror",
        "@abseil-cpp//absl/time",
    ],
)

cc_library(
    name = "lockable_binary_futex",
    hdrs = [
        "lockable_binary_futex.h",
    ],
    visibility = [
        "//intrinsic/util/thread:__subpackages__",
        "//intrinsic_control/intrinsic/icon:__subpackages__",
        "//intrinsic_hardware/intrinsic/hardware:__subpackages__",
        "//intrinsic_sdk/intrinsic/platform/common/buffers:__pkg__",
        "//intrinsic_sdk/intrinsic/util/thread:__subpackages__",
    ],
    deps = [
        ":binary_futex",
        "//intrinsic_control/intrinsic/icon/testing:realtime_annotations",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/time",
    ],
)

cc_library(
    name = "binary_futex_condition_variable",
    srcs = ["binary_futex_condition_variable.cc"],
    hdrs = [
        "binary_futex_condition_variable.h",
    ],
    deps = [
        ":binary_futex",
        ":lockable_binary_futex",
        "//intrinsic_control/intrinsic/icon/testing:realtime_annotations",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
    ],
)
