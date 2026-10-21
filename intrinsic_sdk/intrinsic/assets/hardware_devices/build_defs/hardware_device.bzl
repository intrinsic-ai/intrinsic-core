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

"""Build rules for HardwareDevice assets."""

load("//intrinsic_sdk/intrinsic/assets/build_defs:asset.bzl", "AssetCatalogRefInfo", "AssetInfo", "AssetLocalInfo")

HardwareDeviceAssetInfo = provider(
    doc = "Provided by intrinsic_hardware_device() rule.",
    fields = ["bundle_tar"],
)

def _intrinsic_hardware_device_impl(ctx):
    asset_bundles = [
        a[AssetLocalInfo].bundle_path
        for a in ctx.attr.assets
        if AssetLocalInfo in a
    ]
    local_asset_infos = [
        a[AssetInfo].asset_info
        for a in ctx.attr.assets
        if AssetLocalInfo in a
    ]
    catalog_assets = [
        a[AssetCatalogRefInfo].catalog_info
        for a in ctx.attr.assets
        if AssetCatalogRefInfo in a
    ]

    args = ctx.actions.args().add(
        "--manifest",
        ctx.file.manifest,
    )
    for a in ctx.attr.assets:
        if AssetLocalInfo in a:
            args.add(
                "--local_asset",
                "%s=%s" % (a[AssetInfo].asset_info.path, a[AssetLocalInfo].bundle_path.path),
            )
    args.add_all(
        catalog_assets,
        format_each = "--catalog_asset=%s",
    ).add(
        "--output_bundle",
        ctx.outputs.bundle_out,
    )

    ctx.actions.run(
        inputs = asset_bundles + local_asset_infos + catalog_assets + [ctx.file.manifest],
        outputs = [ctx.outputs.bundle_out],
        executable = ctx.executable._hardwaredevicegen,
        arguments = [args],
        mnemonic = "HardwareDeviceBundle",
        progress_message = "HardwareDevice bundle %s" % ctx.outputs.bundle_out.short_path,
    )

    transitive_descriptor_sets = depset(transitive = [
        f[AssetInfo].transitive_descriptor_sets
        for f in ctx.attr.assets
    ])

    transitive_inputs = [transitive_descriptor_sets]
    asset_info_output = ctx.actions.declare_file(ctx.label.name + ".asset_info.binpb")
    local_info_args = ctx.actions.args().add(
        "--manifest",
        ctx.file.manifest,
    ).add(
        "--asset_type",
        "ASSET_TYPE_HARDWARE_DEVICE",
    ).add_all(
        transitive_descriptor_sets,
        before_each = "--file_descriptor_set",
        uniquify = True,
    ).add(
        # This enables the same logic installed assets and the catalog do when
        # processing the file descriptor sets of hardware devices to construct a
        # single unified file descriptor set.
        "--merge_fds",
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
        ),
        HardwareDeviceAssetInfo(
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

intrinsic_hardware_device = rule(
    implementation = _intrinsic_hardware_device_impl,
    attrs = {
        "assets": attr.label_list(
            providers = [
                [AssetInfo, AssetLocalInfo],
                [AssetInfo, AssetCatalogRefInfo],
            ],
            doc = """Assets to add to the HardwareDeviceManifest saved in the bundle. These assets
                  must not already be listed in the manifest.""",
        ),
        "manifest": attr.label(
            allow_single_file = [".textproto"],
            mandatory = True,
            doc = "A manifest that provides the HardwareDevice definition.",
        ),
        "_assetlocalinfogen": attr.label(
            default = Label("//intrinsic_sdk/intrinsic/assets/build_defs:assetlocalinfogen"),
            cfg = "exec",
            executable = True,
        ),
        "_hardwaredevicegen": attr.label(
            default = Label("//intrinsic_sdk/intrinsic/assets/hardware_devices/build_defs:hardwaredevicegen_main"),
            cfg = "exec",
            executable = True,
        ),
    },
    outputs = {
        "bundle_out": "%{name}.bundle.tar",
    },
    provides = [HardwareDeviceAssetInfo, AssetInfo, AssetLocalInfo],
    doc = "Bundles a HardwareDevice asset into a tar file.",
)
