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
load("@bazel_skylib//rules:common_settings.bzl", "bool_flag")
load("@io_bazel_rules_go//proto:compiler.bzl", "go_proto_compiler")
load("//bazel:go_macros.bzl", "go_binary", "go_test")
load("//bazel:sh_macros.bzl", "sh_binary")
load(":exec_properties.bzl", "DEFAULT")

package(default_visibility = ["//visibility:public"])

# gazelle:ignore

# If True, forces downstream dependencies (like Aravis) to link against the
# host system's glib libraries rather than the hermetic @glib//glib target.
# This prevents GLib version conflicts and SIGABRTs when mixing hermetic glib
# targets with tools that must use the host system's libraries (e.g. Qt6 or
# system-linked OpenCV).
bool_flag(
    name = "use_host_glib",
    build_setting_default = False,
)

# rules_pkg does not yet natively support zstd compression (see
# https://github.com/bazelbuild/rules_pkg/issues/481), so we pull in a hermetic
# prebuilt zstd binary from @aspect_bazel_lib.
#
# We wrap it in an sh_binary because pkg_tar's `compressor` attribute requires
# an executable rule target (rather than a raw source file). Setting
# `use_bash_launcher = False` is required because build_tar executes the
# compressor as a bare subprocess without a Bazel runfiles tree, which breaks
# the default bash launcher's rlocation resolution.
sh_binary(
    name = "zstd",
    srcs = ["@zstd_linux_amd64//:zstd"],
    use_bash_launcher = False,
    visibility = ["//visibility:public"],
)

go_binary(
    name = "container_structure_test_runner",
    srcs = ["container_structure_test_runner.go"],
    visibility = ["//visibility:public"],
    deps = [
        "@com_github_klauspost_compress//zstd",
        "@in_gopkg_yaml_v3//:yaml_v3",
    ],
)

go_test(
    name = "container_structure_test_runner_test",
    srcs = [
        "container_structure_test_runner.go",
        "container_structure_test_runner_test.go",
    ],
    deps = [
        "@com_github_klauspost_compress//zstd",
        "@in_gopkg_yaml_v3//:yaml_v3",
    ],
)

exports_files(glob(["content_mirror/*"]))

bzl_library(
    name = "container_bzl",
    srcs = ["container.bzl"],
    deps = [
        "@rules_oci//oci:defs",
        "@rules_pkg//pkg:rules_pkg_lib",
    ],
)

bzl_library(
    name = "cc_oci_image_bzl",
    srcs = ["cc_oci_image.bzl"],
    deps = [
        ":container_bzl",
        "@bazel_skylib//lib:paths",
    ],
)

bzl_library(
    name = "python_oci_image_bzl",
    srcs = ["python_oci_image.bzl"],
    deps = [
        ":container_bzl",
        "@bazel_skylib//lib:paths",
        "@tar.bzl//tar",
    ],
)

bzl_library(
    name = "go_macros",
    srcs = ["go_macros.bzl"],
    deps = [
        "@bazel_skylib//lib:paths",
        "@io_bazel_rules_go//go:def",
        "@io_bazel_rules_go//proto:def",
    ],
)

# For exec_properties, see https://developers.google.com/remote-build-execution/docs/remote-execution-properties
platform(
    name = "linux_x86_64",
    constraint_values = [
        "@platforms//os:linux",
        "@platforms//cpu:x86_64",
        "@bazel_tools//tools/cpp:clang",
    ],
    exec_properties = DEFAULT,
    parents = ["@platforms//host"],
)

go_proto_compiler(
    name = "go_gen_grpc_gateway",
    options = [
        "paths=source_relative",
        "logtostderr=true",
        "allow_delete_body=true",
    ],
    plugin = "@com_github_grpc_ecosystem_grpc_gateway_v2//protoc-gen-grpc-gateway",
    suffix = ".pb.gw.go",
    visibility = ["//visibility:public"],
    deps = [
        "@com_github_grpc_ecosystem_grpc_gateway_v2//runtime:go_default_library",
        "@com_github_grpc_ecosystem_grpc_gateway_v2//utilities:go_default_library",
        "@org_golang_google_grpc//grpclog:go_default_library",
        "@org_golang_google_grpc//metadata:go_default_library",
    ],
)

go_binary(
    name = "checkimportpathmatchesgopackage",
    testonly = True,
    srcs = ["checkimportpathmatchesgopackage.go"],
    visibility = ["//intrinsic:internal_api_users"],
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
