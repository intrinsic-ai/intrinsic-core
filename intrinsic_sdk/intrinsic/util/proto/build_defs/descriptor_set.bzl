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

"""Defines a build rule for generating descriptor sets used by Skills."""

load("@com_google_protobuf//bazel/common:proto_info.bzl", "ProtoInfo")

# N.B. We provide both the `direct_descriptor_set` file and the
# `transitive_descriptor_sets` depset in the ProtoSourceCodeInfo to model this
# similarly to the ProtoInfo provided by proto_library. This makes it feasible
# to write rules that only create output files only containing the direct deps
# if desired, etc.
ProtoSourceCodeInfo = provider(
    "Provides a FileDescriptorSet with source_code_info",
    fields = ["direct_descriptor_set", "transitive_descriptor_sets"],
)

def _remove_prefix(a, prefix):
    """
    If `a` starts with `prefix`, returns a new string with the `prefix` removed.

    Otherwise, returns the string.
    """
    if a.startswith(prefix):
        return a[len(prefix):]
    return a

def _get_proto_src_paths(proto_source_root, direct_sources):
    """
    Returns the paths of the direct_sources relative to the proto_source_root.

    Checks if proto_source_root is something other than ".", if so removes
    it as prefix from the direct_sources short_parth.
    """

    # handle 'normal' case of repository layout
    if proto_source_root == ".":
        return [source.short_path for source in direct_sources]

    # handle case where repo layout uses, for example, _virtual_imports
    return [_remove_prefix(source.path, proto_source_root + "/") for source in direct_sources]

def _gen_source_code_info_descriptor_set_aspect_impl(target, ctx):
    """
    Aspect that generates a FileDescriptorSet with source_code_info for target.
    """
    input_descriptor_sets = depset(transitive = [
        dep[ProtoInfo].transitive_descriptor_sets
        for dep in ctx.rule.attr.deps
    ])

    output_file = ctx.actions.declare_file(
        target.label.name + "_descriptors_direct_set_sci.proto.bin",
    )

    args = ctx.actions.args()
    args.add_joined(
        "--descriptor_set_in",
        input_descriptor_sets.to_list(),
        join_with = ":",
    )

    proto_srcs = _get_proto_src_paths(
        target[ProtoInfo].proto_source_root,
        target[ProtoInfo].direct_sources,
    )
    args.add_all([
        "--include_source_info",
        "--descriptor_set_out=%s" % output_file.path,
        "--proto_path=%s" % target[ProtoInfo].proto_source_root,
    ] + proto_srcs)

    ctx.actions.run(
        executable = ctx.executable._protoc,
        arguments = [args],
        mnemonic = "GenDescriptorWithSourceInfo",
        inputs = input_descriptor_sets.to_list() +
                 target[ProtoInfo].direct_sources,
        outputs = [output_file],
        progress_message = "Generating proto descriptor set with source info",
    )

    return ProtoSourceCodeInfo(
        direct_descriptor_set = output_file,
        transitive_descriptor_sets = depset(
            direct = [output_file],
            transitive = [
                dep[ProtoSourceCodeInfo].transitive_descriptor_sets
                for dep in ctx.rule.attr.deps
            ],
        ),
    )

gen_source_code_info_descriptor_set = aspect(
    implementation = _gen_source_code_info_descriptor_set_aspect_impl,
    required_providers = [ProtoInfo],
    provides = [ProtoSourceCodeInfo],
    attr_aspects = ["deps"],
    attrs = {
        "_protoc": attr.label(
            executable = True,
            default = Label("@com_google_protobuf//:protoc"),
            cfg = "exec",
        ),
    },
)

def _proto_source_code_info_transitive_descriptor_set(ctx):
    transitive_descriptor_sets = depset(transitive = [
        dep[ProtoSourceCodeInfo].transitive_descriptor_sets
        for dep in ctx.attr.deps
    ])
    args = ctx.actions.args()
    args.use_param_file(param_file_arg = "--arg-file=%s")
    args.add_all(transitive_descriptor_sets)

    output_file = ctx.actions.declare_file(
        ctx.label.name + "_transitive_set_sci.proto.bin",
    )

    # Because `xargs` must take its arguments before the command to execute,
    # we cannot simply put a reference to the argument list at the end, as in
    # the case of param file spooling, since the entire argument list will get
    # replaced by "--arg-file=bazel-out/..." which needs to be an `xargs`
    # argument rather than a `cat` argument.
    #
    # We look to see if the first argument begins with a '--arg-file=' and
    # selectively choose xargs vs. just supplying the arguments to `cat`.
    ctx.actions.run_shell(
        outputs = [output_file],
        inputs = transitive_descriptor_sets,
        progress_message = "Joining descriptors.",
        command = (
            "if [[ \"$1\" =~ ^--arg-file=.* ]]; then xargs \"$1\" cat; " +
            "else cat \"$@\"; fi >{output}".format(output = output_file.path)
        ),
        arguments = [args],
    )
    return DefaultInfo(
        files = depset([output_file]),
        runfiles = ctx.runfiles(files = [output_file]),
    )

# proto_source_code_info_transitive_descriptor_set outputs a single file
# containing a binary FileDescriptorSet with transitive dependencies of the
# given proto dependencies. The FileDescriptorProtos in the set will each
# contain source_code_info.
#
# Example usage:
#
#     proto_source_code_info_transitive_descriptor_set(
#         name = "my_proto_descriptors",
#         deps = [":my_proto"],
#     )
#
# Outputs a file named: my_proto_descriptors_transitive_set_sci.proto.bin
proto_source_code_info_transitive_descriptor_set = rule(
    implementation = _proto_source_code_info_transitive_descriptor_set,
    attrs = {
        "deps": attr.label_list(
            aspects = [gen_source_code_info_descriptor_set],
        ),
    },
)
