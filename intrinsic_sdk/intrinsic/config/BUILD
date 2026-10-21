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
load("//bazel:go_macros.bzl", "go_library")
load("//bazel:python.bzl", "py_library")

# Application, process and resource set describe all user-accessible configuration of a
# solution.

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "environments_cc",
    srcs = ["environments.cc"],
    hdrs = ["environments.h"],
    visibility = [
        "//visibility:public",
    ],
    deps = [
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/types:span",
    ],
)

py_library(
    name = "environments_py",
    srcs = ["environments.py"],
    visibility = [
        "//visibility:public",
    ],
)

go_library(
    name = "environments",
    srcs = ["environments.go"],
    importpath = "intrinsic/config/environments",
    visibility = [
        "//visibility:public",
    ],
)

go_library(
    name = "operationmode",
    srcs = ["operation_mode.go"],
    importpath = "intrinsic/config/operationmode",
    deps = ["@intrinsic_apis//intrinsic/config/proto:operation_mode_go_proto"],
)
