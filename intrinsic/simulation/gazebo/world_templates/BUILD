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

# World template files and utilities used in world to sdf conversion.
load("//bazel:cc_macros.bzl", "cc_library")

package(default_visibility = ["//visibility:public"])

filegroup(
    name = "default_template",
    srcs = ["default.sdf.tpl"],
)

cc_library(
    name = "world_template",
    srcs = ["world_template.cc"],
    hdrs = ["world_template.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@or_tools//ortools/base:file",
        "@tinyxml2",
    ],
)
