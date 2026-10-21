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

"""Bazel rules for Assets."""

load("@com_google_protobuf//bazel/common:proto_info.bzl", "ProtoInfo")

AssetInfo = provider(
    "Info about an asset.",
    fields = {
        "asset_info": "An AssetInfo proto",
        "transitive_descriptor_sets": "depset of file descriptor sets",
    },
)

AssetLocalInfo = provider(
    "Info about a built asset bundle file.",
    fields = {
        "bundle_path": "The full path to the asset's bundle file",
    },
)

AssetCatalogRefInfo = provider(
    "Info about an asset catalog reference.",
    fields = {
        "catalog_info": "An AssetCatalogRefInfo proto",
    },
)

AssetInstanceInfo = provider(
    "An asset instance.",
    fields = {
        "asset": "The asset ID of the instance",
        "config": "InstanceConfig textproto file",
        "name": "The name of the asset instance",
    },
)

def _intrinsic_asset_reference_impl(ctx):
    asset_info_output = ctx.actions.declare_file(ctx.label.name + ".asset_info.binpb")
    asset_catalog_ref_info_output = ctx.actions.declare_file(ctx.label.name + ".asset_catalog_ref_info.binpb")

    transitive_descriptor_sets = depset(transitive = [
        f[ProtoInfo].transitive_descriptor_sets
        for f in ctx.attr.deps
    ])

    args = ctx.actions.args().add(
        "--asset_type",
        ctx.attr.type,
    ).add(
        "--id",
        ctx.attr.id,
    ).add_all(
        transitive_descriptor_sets,
        before_each = "--file_descriptor_set",
    ).add(
        "--version",
        ctx.attr.version,
    ).add(
        "--output_asset_info",
        asset_info_output,
    ).add(
        "--output_asset_catalog_ref_info",
        asset_catalog_ref_info_output,
    )

    ctx.actions.run(
        inputs = transitive_descriptor_sets,
        outputs = [asset_info_output, asset_catalog_ref_info_output],
        executable = ctx.executable._assetcatalogrefinfogen,
        mnemonic = "AssetReference",
        progress_message = "Writing %{output} for %{label}",
        arguments = [args],
    )
    return [
        DefaultInfo(
            files = depset([asset_catalog_ref_info_output]),
            executable = asset_catalog_ref_info_output,
        ),
        AssetInfo(
            asset_info = asset_info_output,
            transitive_descriptor_sets = transitive_descriptor_sets,
        ),
        AssetCatalogRefInfo(
            catalog_info = asset_catalog_ref_info_output,
        ),
    ]

intrinsic_asset_reference = rule(
    implementation = _intrinsic_asset_reference_impl,
    attrs = {
        "deps": attr.label_list(
            providers = [ProtoInfo],
            doc = "Proto dependencies that are compatible with the catalog " +
                  "asset. These are optional but required to parse config " +
                  "files. Note that version skew or other errors may happen " +
                  "if the wrong protos are used.",
        ),
        "id": attr.string(
            mandatory = True,
        ),
        "type": attr.string(),
        "version": attr.string(
            mandatory = True,
        ),
        "_assetcatalogrefinfogen": attr.label(
            default = Label("//intrinsic_sdk/intrinsic/assets/build_defs:assetcatalogrefinfogen"),
            cfg = "exec",
            executable = True,
        ),
    },
    provides = [AssetInfo, AssetCatalogRefInfo],
)

def _intrinsic_asset_instance_impl(ctx):
    if ctx.file.config and (ctx.attr.required_node_hostname or ctx.file.service_config):
        fail("Cannot specify both 'config' and attributes to generate a config ('service_config', 'required_node_hostname')")

    name = ctx.attr.instance_name if ctx.attr.instance_name else ctx.label.name
    files = []
    config = None
    if ctx.file.config:
        config = ctx.file.config
        files.append(config)
    elif ctx.attr.required_node_hostname or ctx.file.service_config:
        config = ctx.actions.declare_file(ctx.label.name + ".txtpb")
        files.append(config)
        args = ctx.actions.args().add(
            "--output",
            config,
        ).add(
            "--required_node_hostname",
            ctx.attr.required_node_hostname,
        )
        inputs = []
        if ctx.file.service_config:
            args.add(
                "--service_config",
                ctx.file.service_config,
            )
            inputs.append(ctx.file.service_config)

        ctx.actions.run(
            inputs = inputs,
            outputs = [config],
            executable = ctx.executable._assetinstancegen,
            arguments = [args],
            mnemonic = "AssetInstance",
            progress_message = "Writing %{output} for %{label}",
        )

    return [
        DefaultInfo(
            files = depset(files),
        ),
        AssetInstanceInfo(
            asset = ctx.attr.asset,
            config = config,
            name = name,
        ),
    ]

intrinsic_asset_instance = rule(
    implementation = _intrinsic_asset_instance_impl,
    attrs = {
        "asset": attr.string(
            mandatory = True,
        ),
        "config": attr.label(
            allow_single_file = [".txtpb", ".textproto", ".pbtxt"],
            doc = "An InstanceConfig textproto file.",
        ),
        "instance_name": attr.string(
            doc = "Name of the instance, if it should be different than 'name'",
        ),
        "required_node_hostname": attr.string(
            mandatory = False,
        ),
        "service_config": attr.label(
            allow_single_file = [".pbtxt", ".txtpb", ".textproto"],
        ),
        "_assetinstancegen": attr.label(
            default = Label("//intrinsic_sdk/intrinsic/assets/build_defs:assetinstancegen"),
            cfg = "exec",
            executable = True,
        ),
    },
    provides = [AssetInstanceInfo],
)
