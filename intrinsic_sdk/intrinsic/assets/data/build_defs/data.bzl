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

"""Bazel rules for Data assets."""

load("@bazel_lib//lib:paths.bzl", "to_rlocation_path")
load("@bazel_skylib//lib:paths.bzl", "paths")
load("@com_google_protobuf//bazel/common:proto_info.bzl", "ProtoInfo")
load("//intrinsic_sdk/intrinsic/assets/build_defs:asset.bzl", "AssetInfo", "AssetLocalInfo")
load("//intrinsic_sdk/intrinsic/util/proto/build_defs:descriptor_set.bzl", "ProtoSourceCodeInfo", "gen_source_code_info_descriptor_set")

DataAssetInfo = provider(
    "Provided by intrinsic_data() rule.",
    fields = ["bundle_tar"],
)

def _intrinsic_data_impl(ctx):
    inputs = []
    transitive_inputs = []

    inputs.append(ctx.file.manifest)
    inputs.extend(ctx.files.data)

    transitive_descriptor_sets = depset(transitive = [
        f[ProtoSourceCodeInfo].transitive_descriptor_sets
        for f in ctx.attr.deps
    ])
    transitive_inputs.append(transitive_descriptor_sets)

    args = ctx.actions.args().add(
        "--manifest_path",
        ctx.file.manifest,
    ).add(
        "--output_bundle_path",
        ctx.outputs.bundle_out,
    ).add_all(
        transitive_descriptor_sets,
        before_each = "--file_descriptor_set_path",
        uniquify = True,
    ).add_all(
        # Map manifest-relative paths to execroot paths so datagen can locate generated files.
        [
            paths.relativize(f.short_path, paths.dirname(ctx.file.manifest.short_path)) + "=" + f.path
            for f in ctx.files.data
        ],
        before_each = "--reference_to_path",
    ).add_all(
        [
            f.path + "=" + to_rlocation_path(ctx, f)
            for f in ctx.files.exclude_data_from_bundle
        ],
        before_each = "--replace_with_external_reference",
    )

    ctx.actions.run(
        inputs = depset(inputs, transitive = transitive_inputs),
        outputs = [ctx.outputs.bundle_out],
        executable = ctx.executable._datagen,
        arguments = [args],
        mnemonic = "Databundle",
        progress_message = "Data bundle %s" % ctx.outputs.bundle_out.short_path,
    )

    asset_info_output = ctx.actions.declare_file(ctx.label.name + ".asset_info.binpb")
    local_info_args = ctx.actions.args().add(
        "--manifest",
        ctx.file.manifest,
    ).add(
        "--asset_type",
        "ASSET_TYPE_DATA",
    ).add_all(
        transitive_descriptor_sets,
        before_each = "--file_descriptor_set",
        uniquify = True,
    ).add(
        "--output_asset_info",
        asset_info_output,
    )
    ctx.actions.run(
        inputs = depset([ctx.file.manifest], transitive = transitive_inputs),
        outputs = [asset_info_output],
        executable = ctx.executable._assetlocalinfogen,
        arguments = [local_info_args],
        mnemonic = "AssetLocalInfo",
        progress_message = "Writing asset info %{output} for %{label}",
    )

    return [
        DefaultInfo(
            executable = ctx.outputs.bundle_out,
            runfiles = ctx.runfiles(ctx.files.exclude_data_from_bundle),
        ),
        DataAssetInfo(
            bundle_tar = ctx.outputs.bundle_out,
        ),
        AssetInfo(
            asset_info = asset_info_output,
            transitive_descriptor_sets = transitive_descriptor_sets,
        ),
        AssetLocalInfo(
            bundle_path = ctx.outputs.bundle_out,
        ),
    ]

intrinsic_data = rule(
    implementation = _intrinsic_data_impl,
    attrs = {
        "data": attr.label_list(
            allow_empty = True,
            allow_files = True,
            mandatory = False,
            doc = "Data files that are referenced via ReferencedData in the data payload.",
        ),
        "deps": attr.label_list(
            mandatory = True,
            providers = [ProtoInfo],
            doc = "Proto dependencies needed to construct the data payload's FileDescriptorSet.",
            aspects = [gen_source_code_info_descriptor_set],
        ),
        "exclude_data_from_bundle": attr.label_list(
            allow_empty = True,
            allow_files = True,
            mandatory = False,
            doc = "Referenced data files to exclude from the output tar bundle.",
        ),
        "manifest": attr.label(
            allow_single_file = [".textproto"],
            mandatory = True,
            doc = "A manifest that provides the data payload and metadata.",
        ),
        "_assetlocalinfogen": attr.label(
            default = Label("//intrinsic_sdk/intrinsic/assets/build_defs:assetlocalinfogen"),
            cfg = "exec",
            executable = True,
        ),
        "_datagen": attr.label(
            default = Label("//intrinsic_sdk/intrinsic/assets/data/build_defs:datagen_main"),
            cfg = "exec",
            executable = True,
        ),
    },
    outputs = {
        "bundle_out": "%{name}.bundle.tar",
    },
    provides = [DataAssetInfo, AssetInfo, AssetLocalInfo],
    doc = "Bundles a Data asset into a tar file.",
)
