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
load("//bazel:python.bzl", "py_library")
load("//intrinsic_sdk/intrinsic/assets/build_defs:solution.bzl", "intrinsic_solution")
load("//intrinsic_sdk/bazel:go_macros.bzl", "go_library")

# Application, process and resource set describe all user-accessible configuration of a
# solution.

package(default_visibility = ["//visibility:public"])

# Deprecated (b/584140057): do not add new dependencies on environments_cc.
cc_library(
    name = "environments_cc",
    srcs = ["environments.cc"],
    hdrs = ["environments.h"],
    visibility = [
        "//intrinsic_sdk/intrinsic/util/grpc:__pkg__",
    ],
    deps = [
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/types:span",
    ],
)

# Deprecated (b/584140057): do not add new dependencies on environments_py.
py_library(
    name = "environments_py",
    srcs = ["environments.py"],
    visibility = ["//intrinsic_sdk/intrinsic/util/grpc:__pkg__"],
)

# Deprecated (b/584140057): do not add new dependencies on environments.
go_library(
    name = "environments",
    srcs = ["environments.go"],
    importpath = "intrinsic/config/environments",
    visibility = [
        "//intrinsic/kubernetes:__pkg__",
        "//intrinsic/tools/inctl/cmd:__pkg__",
        "//intrinsic/tools/inctl/cmd/cluster:__pkg__",
        "//intrinsic/tools/inctl/util:__pkg__",
        "//intrinsic_sdk/intrinsic/assets:__pkg__",
        "//intrinsic_sdk/intrinsic/tools/inctl/auth:__pkg__",
        "//intrinsic_sdk/intrinsic/tools/inctl/cmd/auth:__pkg__",
        "//intrinsic_sdk/intrinsic/tools/inctl/cmd/cluster:__pkg__",
        "//intrinsic_sdk/intrinsic/tools/inctl/cmd/solution_version:__pkg__",
        "//intrinsic_sdk/intrinsic/tools/inctl/cmd/vm:__pkg__",
        "//intrinsic_sdk/intrinsic/tools/inctl/util:__pkg__",
        "//intrinsic_sdk/intrinsic/tools/inctl/util/accounts:__pkg__",
    ],
)

go_library(
    name = "operationmode",
    srcs = ["operation_mode.go"],
    importpath = "intrinsic/config/operationmode",
    deps = ["@intrinsic_apis//intrinsic/config/proto:operation_mode_go_proto"],
)

intrinsic_solution(
    name = "empty_application",
    display_name = "Empty solution",
    visibility = ["//intrinsic:intrinsic_internal"],
)
