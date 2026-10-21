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

"""Provides the proto_data rule."""

load("//intrinsic_sdk/intrinsic/util/proto/build_defs:descriptor_set.bzl", "proto_source_code_info_transitive_descriptor_set")

def _convert_text_proto_to_binary_impl(ctx):
    ctx.actions.run(
        inputs = [ctx.file.in_text_proto, ctx.file.transitive_descriptor_set],
        outputs = [ctx.outputs.out_binary_proto],
        executable = ctx.executable._proto_converter,
        arguments = [
            "--message_full_name=%s" % ctx.attr.message_full_name,
            "--in_text_proto=%s" % ctx.file.in_text_proto.path,
            "--out_binary_proto=%s" % ctx.outputs.out_binary_proto.path,
            "--transitive_descriptor_set=%s" % ctx.file.transitive_descriptor_set.path,
        ],
        mnemonic = "ConvertTextProtoToBinary",
        progress_message = "Convert %s" % ctx.outputs.out_binary_proto.short_path,
    )

    return [DefaultInfo(runfiles = ctx.runfiles(files = [ctx.outputs.out_binary_proto]))]

_convert_text_proto_to_binary = rule(
    implementation = _convert_text_proto_to_binary_impl,
    doc = "Creates a binary proto from a text proto using a given file descriptor set.",
    attrs = {
        "in_text_proto": attr.label(
            allow_single_file = True,
            doc = "The text proto to convert to binary.",
        ),
        "message_full_name": attr.string(
            doc = "The full name of the proto message to convert, e.g., 'intrinsic_proto.Pose3d'.",
        ),
        "out_binary_proto": attr.output(
            doc = "The binary proto to generate.",
        ),
        "transitive_descriptor_set": attr.label(
            allow_single_file = True,
            doc = "The file descriptor set with all transitive dependencies of the message type " +
                  "to convert.",
        ),
        "_proto_converter": attr.label(
            default = Label("//intrinsic/util/proto/tools:proto_converter"),
            cfg = "exec",
            executable = True,
        ),
    },
)

def proto_data(
        name,
        src,
        proto_deps,
        proto_name,
        out = None,
        testonly = None,
        visibility = None):
    """Creates a binary proto from a text proto.

    Args:
      name: The name of the target.
      src: The text proto to convert to binary.
      proto_deps: A list of proto_library targets where 'proto_name' is defined. Typically only
            a single proto_library target is needed - transitive dependencies are pulled in
            automatically.
      proto_name: The full name of the proto message to convert, e.g., 'intrinsic_proto.Pose3d'.
      out: (optional) The name of the output file. Defaults to <name>.binarypb.
      testonly: (optional) Whether the target is testonly.
      visibility: (optional) The visibility of the target.
    """
    transitive_descriptor_set = "_%s_tds" % name
    proto_source_code_info_transitive_descriptor_set(
        name = transitive_descriptor_set,
        deps = proto_deps,
        testonly = testonly,
        visibility = visibility,
    )
    _convert_text_proto_to_binary(
        name = name,
        in_text_proto = src,
        out_binary_proto = out or name + ".binarypb",
        message_full_name = proto_name,
        transitive_descriptor_set = transitive_descriptor_set,
        testonly = testonly,
        visibility = visibility,
    )
