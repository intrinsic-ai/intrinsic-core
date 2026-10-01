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

# Generic language utilities for golang.

load("//bazel:go_macros.bzl", "go_library")

go_library(
    name = "pointer",
    srcs = ["pointer.go"],
    importpath = "intrinsic/util/go/pointer",
    visibility = ["//intrinsic:internal_api_users"],
)

go_library(
    name = "set",
    srcs = ["set.go"],
    importpath = "intrinsic/util/go/set",
    visibility = ["//intrinsic:internal_api_users"],
)

go_library(
    name = "validate",
    srcs = ["validate.go"],
    importpath = "intrinsic/util/go/validate",
    visibility = ["//intrinsic:internal_api_users"],
    deps = ["@intrinsic_apis//intrinsic/kubernetes/workcell_spec/proto:image_go_proto"],
)

go_library(
    name = "build",
    srcs = ["build.go"],
    importpath = "intrinsic/util/go/build",
    visibility = ["//visibility:public"],
    x_defs = {"label": "{BUILD_EMBED_LABEL}"},
)

go_library(
    name = "shutdown",
    srcs = ["shutdown.go"],
    importpath = "intrinsic/util/go/shutdown",
    visibility = ["//intrinsic:internal_api_users"],
)

go_library(
    name = "xiter",
    srcs = ["xiter.go"],
    importpath = "intrinsic/util/go/xiter",
    visibility = ["//intrinsic:internal_api_users"],
)
