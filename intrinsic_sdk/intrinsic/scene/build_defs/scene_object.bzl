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

"""Custom rules for creating SceneObject from SDF."""

load("@com_google_protobuf//bazel/common:proto_info.bzl", "ProtoInfo")
load("//intrinsic_sdk/intrinsic/util/proto/build_defs:descriptor_set.bzl", "ProtoSourceCodeInfo", "gen_source_code_info_descriptor_set")

SceneObjectInfo = provider(
    "provided by the scene_object() rule",
    fields = {
        "fdsets": "The transitive descriptor sets needed to parse the user provided data in pbtxt",
        "gzf": "The gzf file, containing the extracted scene object and associated geometry",
        "pbtxt": "A pbtxt file representing a intrinsic_proto::scene_object::v1::SceneObject message",
    },
)

def _scene_object_impl(ctx):
    # Determine output files up front.
    output_gzf = ctx.actions.declare_file(ctx.label.name + ".gzf")
    output_pbtxt = ctx.actions.declare_file(ctx.label.name + ".pbtxt")

    ## Build the gzf from the proto with optional updates
    args = ctx.actions.args()
    args.add("--input_scene_object_pbtxt_file", ctx.file.src.path)
    if ctx.attr.updates_pbtxts:
        args.add_joined("--input_updates_proto_filenames", ctx.files.updates_pbtxts, join_with = ",")
    transitive_descriptor_sets = depset(transitive = [
        f[ProtoSourceCodeInfo].transitive_descriptor_sets
        for f in ctx.attr.deps
    ])
    args.add_joined("--file_descriptor_sets", transitive_descriptor_sets, join_with = ",")
    args.add("--output_scene_object_gzf_file", output_gzf.path)
    args.add("--output_scene_object_pbtxt_file", output_pbtxt.path)

    ctx.actions.run(
        outputs = [output_gzf, output_pbtxt],
        inputs = [ctx.file.src] + ctx.files.updates_pbtxts + transitive_descriptor_sets.to_list(),
        arguments = [args],
        mnemonic = "GenSceneObject",
        executable = ctx.file._update_scene_object,
        progress_message = "Generating SceneObject %s " % ctx.label.name,
    )

    return [
        DefaultInfo(
            files = depset([output_gzf, output_pbtxt]),
            runfiles = ctx.runfiles(files = [output_gzf, output_pbtxt]),
        ),
        SceneObjectInfo(
            gzf = output_gzf,
            pbtxt = output_pbtxt,
            fdsets = transitive_descriptor_sets,
        ),
    ]

_scene_object = rule(
    implementation = _scene_object_impl,
    doc = (
        "Generates a GZF from a scene object proto, to be used as a scene " +
        "object asset type (or anything which accepts a SceneObjectInfo " +
        "provider, which is returned by this rule)."
    ),
    attrs = {
        "deps": attr.label_list(
            providers = [ProtoInfo],
            aspects = [gen_source_code_info_descriptor_set],
        ),
        "src": attr.label(allow_single_file = [".textproto", ".pbtxt"]),
        "updates_pbtxts": attr.label_list(allow_files = [".textproto", ".pbtxt"]),
        "_update_scene_object": attr.label(
            default = Label("//intrinsic_sdk/intrinsic/scene/tools:update_scene_object"),
            cfg = "exec",
            allow_single_file = True,
            executable = True,
        ),
    },
)

def scene_object(
        name,
        src,
        updates_pbtxts = None,
        deps = [],
        compatible_with = [],
        testonly = False,
        visibility = None):
    """Creates a SceneObject GZF from a SceneObject proto and optionally SceneObjectUpdates proto file.

    Args:
      name: The name of the SceneObject to be created.
      src: Source scene object proto file (intrinsic_proto::scene_object::v1::SceneObject).
      updates_pbtxts: list of labels, Optional. Text proto files
        (intrinsic_proto::scene_object::v1::SceneObjectUpdate) that specify
        post-processings of the SDF converted SceneObject.
      deps: list of labels, Optional. Proto dependencies needed to parse the updates pbtxts.
      compatible_with: The list of environments this target can be built for,
        in addition to default-supported environments.
      testonly: If true, only testonly targets can depend on this target.
      visibility: Visibility of the build rule.
    """

    # Actually generate the scene object gzf/textproto.
    _scene_object(
        name = name,
        src = src,
        updates_pbtxts = updates_pbtxts,
        deps = deps,
        compatible_with = compatible_with,
        testonly = testonly,
        visibility = visibility,
    )
