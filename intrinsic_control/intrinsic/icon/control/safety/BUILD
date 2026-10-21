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
    "//intrinsic_control/intrinsic/icon/control:icon_rtcl",
    "//intrinsic_control/intrinsic/icon/hardware_modules:__subpackages__",
    "//visibility:public",
])

package_group(
    name = "icon",
    packages = [
        "//intrinsic_control/intrinsic/icon/...",
    ],
)

package_group(
    name = "safety",
    packages = [
        "//intrinsic_control/intrinsic/icon/control/safety/...",
    ],
)

flatbuffers_library(
    name = "safety_messages_fbs",
    srcs = [
        "safety_messages.fbs",
    ],
    deps = [
        "//intrinsic_control/intrinsic/icon/control/safety/extern:safety_status_fbs",
    ],
)

cc_flatbuffers_library(
    name = "safety_messages_fbs_cc",
    deps = [":safety_messages_fbs"],
)

cc_library(
    name = "safety_messages_fbs_utils",
    srcs = ["safety_messages_utils.cc"],
    hdrs = ["safety_messages_utils.h"],
    deps = [
        ":safety_messages_fbs_cc",
        "//intrinsic_control/intrinsic/icon/control/safety/extern:safety_status_fbs_cc",
        "@abseil-cpp//absl/strings",
        "@com_github_google_flatbuffers//:runtime_cc",
    ],
)
