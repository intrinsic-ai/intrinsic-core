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

alias(
    name = "pointer",
    actual = "//intrinsic_sdk/intrinsic/util/go:pointer",
    tags = ["go/intrinsic-ioc-transition-tdd"],
    visibility = ["//intrinsic:internal_api_users"],
)

alias(
    name = "set",
    actual = "//intrinsic_sdk/intrinsic/util/go:set",
    tags = ["go/intrinsic-ioc-transition-tdd"],
    visibility = ["//intrinsic:internal_api_users"],
)

alias(
    name = "validate",
    actual = "//intrinsic_sdk/intrinsic/util/go:validate",
    tags = ["go/intrinsic-ioc-transition-tdd"],
    visibility = ["//intrinsic:internal_api_users"],
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
