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

"""This file contains starlark rules for building golang targets."""

load("@com_google_protobuf//bazel/common:proto_info.bzl", "ProtoInfo")
load("@io_bazel_rules_go//go:def.bzl", "GoInfo", _go_binary = "go_binary", _go_library = "go_library", _go_test = "go_test")
load("@io_bazel_rules_go//proto:def.bzl", _go_proto_library = "go_proto_library")

def go_binary(name, **kwargs):
    """go_binary modifies the binary out path, by creating the output binary at "name" instead of "name_/name"

    Args:
        name: The name of the target.
        **kwargs: Other arguments passed to the macro
    """
    if kwargs.get("linkmode", None) in (None, "normal", "pie"):  # executables
        # Create the output binary at "name" instead of "name_/name"
        # TODO(b/418266220): Remove when fixed upstream.
        kwargs["out"] = name

    _go_binary(
        name = name,
        **kwargs
    )

def go_library(name, **kwargs):
    if not kwargs.get("importpath", None):
        fail("{} is missing 'importpath'".format(name))
    _go_library(
        name = name,
        **kwargs
    )

def go_grpc_library(name):
    fail(
        "❌ DEPRECATED: rule 'go_grpc_library' is banned.\n" +
        "Please migrate target '{}' to use 'go_proto_library' with " +
        "compilers=['@io_bazel_rules_go//proto:go_grpc_v2', ...].".format(name),
    )

def go_proto_library(name, proto = None, protos = None, **kwargs):
    if not protos:
        protos = [proto]
    _go_proto_library(
        name = name,
        protos = protos,
        **kwargs
    )

    importpath_matches_go_package_test(
        name = "importpath_matches_go_package_" + name + "_test",
        # Some proto_library files contain multiple proto files; however, protoc-gen-go requires
        # all proto files in a proto_library target have the same go_package. We only need to
        # check the go_package of one of the files.
        proto = protos[0],
        go_proto_library = name,
    )

def go_test(name, **kwargs):
    _go_test(
        name = name,
        **kwargs
    )

def _importpath_matches_go_package_test_impl(ctx):
    """Implementation for the importpath_matches_go_package_test rule."""

    # The executable shell script that Bazel will run as the test.
    test_script = ctx.actions.declare_file(ctx.label.name + "_test.sh")

    # Call the checker binary with the import path and the proto file path.
    # The 'set -e' ensures that the script will exit with a non-zero status
    # if the checker fails, which then fails the test.
    ctx.actions.write(
        output = test_script,
        content = """#!/bin/bash
set -e
checker="{checker_path}"
import_path="{import_path}"
proto_file="{proto_file_path}"
"$checker" "$import_path" "$proto_file"
""".format(
            checker_path = ctx.executable._checker.short_path,
            import_path = ctx.attr.go_proto_library[GoInfo].importpath,
            proto_file_path = ctx.attr.proto[ProtoInfo].direct_sources[0].short_path,
        ),
        is_executable = True,
    )

    proto_runfiles = ctx.runfiles(ctx.attr.proto[ProtoInfo].direct_sources)

    return [DefaultInfo(
        executable = test_script,
        runfiles = ctx.attr._checker.default_runfiles.merge(proto_runfiles),
    )]

importpath_matches_go_package_test = rule(
    implementation = _importpath_matches_go_package_test_impl,
    test = True,
    attrs = {
        "go_proto_library": attr.label(
            mandatory = True,
            providers = [GoInfo],
            doc = "The go_proto_library target to check.",
        ),
        "proto": attr.label(
            mandatory = True,
            providers = [ProtoInfo],
            doc = "The proto_library target that has a go_package option.",
        ),
        "_checker": attr.label(
            default = Label("//intrinsic_sdk/bazel:checkimportpathmatchesgopackage"),
            cfg = "exec",
            executable = True,
            doc = "Internal: The binary that performs the check.",
        ),
    },
    doc = "A test rule that ensures a go_proto_library's importpath matches the go_package option in its .proto file.",
)
