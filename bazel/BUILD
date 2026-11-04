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

# Shared Bazel macros for use in external workspaces and general Bazel helpers.

load("@bazel_skylib//:bzl_library.bzl", "bzl_library")

package(default_visibility = ["//visibility:public"])

# gazelle:ignore

alias(
    name = "use_host_glib",
    actual = "//intrinsic_sdk/bazel:use_host_glib",
    tags = ["go/intrinsic-ioc-transition-tdd"],
)

alias(
    name = "zstd",
    actual = "//intrinsic_sdk/bazel:zstd",
    tags = ["go/intrinsic-ioc-transition-tdd"],
    visibility = ["//visibility:public"],
)

exports_files(glob(["content_mirror/*"]))

alias(
    name = "container_bzl",
    actual = "//intrinsic_sdk/bazel:container_bzl",
    tags = ["go/intrinsic-ioc-transition-tdd"],
)

alias(
    name = "linux_x86_64",
    actual = "//intrinsic_sdk/bazel:linux_x86_64",
    tags = ["go/intrinsic-ioc-transition-tdd"],
)

alias(
    name = "linux_arm64",
    actual = "//intrinsic_sdk/bazel:linux_arm64",
    tags = ["go/intrinsic-ioc-transition-tdd"],
)

alias(
    name = "go_gen_grpc_gateway",
    actual = "//intrinsic_sdk/bazel:go_gen_grpc_gateway",
    tags = ["go/intrinsic-ioc-transition-tdd"],
    visibility = ["//visibility:public"],
)

bzl_library(
    name = "xacro_bzl",
    srcs = ["xacro.bzl"],
)

bzl_library(
    name = "migration_locations_bzl",
    srcs = ["migration_locations.bzl"],
)

bzl_library(
    name = "cc_macros",
    srcs = ["cc_macros.bzl"],
    deps = [
        ":migration_locations_bzl",
        "@bazel_skylib//lib:paths",
        "@rules_cc//cc:core_rules",
    ],
)

bzl_library(
    name = "sh_macros",
    srcs = ["sh_macros.bzl"],
    deps = [
        "@rules_shell//shell:rules_bzl",
    ],
)
