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

"""Bazel rule to generate an OpenAPI spec from proto_library targets."""

load("@com_google_protobuf//bazel/common:proto_info.bzl", "ProtoInfo")

def _make_proto_path_arg(proto_path):
    # Create a --proto_path arg for use with with args.add_all(...map_each=)
    # Don't use before_each because it adds leading whitespace that causes an error like.
    # Could not map to virtual file: bazel-out/haswell-fastbuild/bin/external/protobuf+/src/google/protobuf/_virtual_imports/any_proto: Input file is a directory.
    return "--proto_path=" + proto_path

def _protoc_gen_openapi_impl(ctx):
    """Implementation for the protoc_gen_openapi rule."""

    # Create a directory so multiple targets in one BUILD file don't conflict
    output_file = ctx.actions.declare_file("_%s/openapi.yaml" % ctx.attr.name)

    # Collect ProtoInfo from the user-provided targets
    all_proto_infos = [p[ProtoInfo] for p in ctx.attr.protos]

    # Gather all transitive .proto files. These are the inputs to the action.
    transitive_sources = depset(
        # direct = ctx.files._well_known_protos,
        transitive = [
            info.transitive_sources
            for info in all_proto_infos
        ],
    )

    # Gather all proto source roots for the -I/--proto_path flags.
    transitive_proto_paths = depset(transitive = [
        info.transitive_proto_path
        for info in all_proto_infos
    ])

    # The direct sources from the user's targets are the files passed directly
    # to the protoc command line. Transitive dependencies are found via the import paths.
    direct_sources = []
    for proto in ctx.attr.protos:
        direct_sources.extend(proto[ProtoInfo].direct_sources)

    # Use an args object to build the command line arguments for protoc.
    args = ctx.actions.args()

    # Add the plugin command, specifying the plugin executable's path.
    args.add("--plugin=protoc-gen-openapi=" + ctx.executable._plugin.path)

    # Add the output flag for the openapi plugin. This specifies the output directory
    args.add("--openapi_out=" + output_file.dirname)

    args.add("--openapi_opt=naming=json")
    args.add("--openapi_opt=fq_schema_naming=True")
    args.add("--openapi_opt=enum_type=string")

    # Add all the necessary import paths.
    args.add_all(transitive_proto_paths, map_each = _make_proto_path_arg)

    # Add the .proto files to be processed.
    args.add_all(direct_sources)

    # Define the build action that runs protoc.
    ctx.actions.run(
        mnemonic = "ProtocGenOpenAPI",
        executable = ctx.executable._protoc,
        # The protoc executable and the openapi plugin are the tools.
        tools = [ctx.executable._protoc, ctx.executable._plugin],
        # All transitive .proto files are inputs to the action.
        inputs = transitive_sources,
        outputs = [output_file],
        arguments = [args],
        progress_message = "Generating OpenAPI spec from %d protos" % len(direct_sources),
    )

    return [
        DefaultInfo(
            files = depset([output_file]),
        ),
    ]

protoc_gen_openapi = rule(
    doc = "Generates an OpenAPI v3 specification from a set of proto_library targets.",
    implementation = _protoc_gen_openapi_impl,
    attrs = {
        "protos": attr.label_list(
            doc = "A list of proto_library targets to generate the OpenAPI spec from.",
            mandatory = True,
            providers = [ProtoInfo],
        ),
        "_plugin": attr.label(
            # The label for the protoc-gen-openapi executable from gnostic.
            default = Label("@com_github_google_gnostic//cmd/protoc-gen-openapi:protoc-gen-openapi"),
            executable = True,
            cfg = "exec",
        ),
        "_protoc": attr.label(
            default = Label("@com_google_protobuf//:protoc"),
            executable = True,
            cfg = "exec",
        ),
    },
)
