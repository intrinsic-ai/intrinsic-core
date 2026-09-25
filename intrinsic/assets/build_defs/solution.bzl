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

"""Bazel rules for solutions."""

load("//intrinsic/assets/build_defs:asset.bzl", "AssetCatalogRefInfo", "AssetInfo", "AssetInstanceInfo", "AssetLocalInfo")
load("//intrinsic/util/path_resolver:paths.bzl", "WRAPPER_HEADER", "to_rlocation_path")

SolutionInfo = provider(
    "provided by the intrinsic_solution() rule",
    fields = {
        "asset_bundles": "asset bundle files used in the solution",
        "instance_configs": "instance config files used in the solution",
        "solution": "binary proto file containing a LocalSolution message",
    },
)

def _display_name(f):
    """
    Generates a display name from a bazel label.

    E.g. //intrinsic/apps/bluebird_caw:bb04 --> bluebird_caw:bb04
    """
    return f.package.split("/")[-1] + ":" + f.name

def _intrinsic_solution_impl(ctx):
    out = ctx.actions.declare_file(ctx.label.name + ".local_solution.binpb")
    assets = [
        a[AssetInfo].asset_info
        for a in ctx.attr.assets
        if AssetInfo in a
    ]
    catalog_assets = [
        a[AssetCatalogRefInfo].catalog_info
        for a in ctx.attr.assets
        if AssetCatalogRefInfo in a
    ]
    instance_configs = [
        i[AssetInstanceInfo].config
        for i in ctx.attr.instances
        if AssetInstanceInfo in i and i[AssetInstanceInfo].config
    ]

    inputs = assets + catalog_assets + instance_configs + ctx.files.object_world_updates
    args = ctx.actions.args().add(
        "--output",
        out,
    ).add_all(
        assets,
        format_each = "--assets=%s",
    ).add_all(
        catalog_assets,
        format_each = "--catalog_assets=%s",
    ).add_all(
        ctx.files.object_world_updates,
        format_each = "--object_world_updates=%s",
    ).add(
        "--default_operation_mode",
        ctx.attr.default_operation_mode,
    ).add(
        "--display_name",
        _display_name(ctx.label),
    )
    for a in ctx.attr.assets:
        if AssetLocalInfo in a:
            args.add(
                "--local_assets",
                "%s=%s" % (a[AssetInfo].asset_info.path, to_rlocation_path(ctx, a[AssetLocalInfo].bundle_path)),
            )
    for i in ctx.attr.instances:
        if AssetInstanceInfo in i:
            inst = i[AssetInstanceInfo]
            if inst.config:
                args.add(
                    "--instances",
                    "%s=%s=%s=%s" % (inst.name, inst.asset, inst.config.path, to_rlocation_path(ctx, inst.config)),
                )
            else:
                args.add(
                    "--instances",
                    "%s=%s" % (inst.name, inst.asset),
                )

    ctx.actions.run(
        inputs = inputs,
        outputs = [out],
        executable = ctx.executable._localsolutiongen,
        arguments = [args],
        mnemonic = "LocalSolution",
        progress_message = "Writing solution proto %{output} for %{label}",
    )

    asset_bundles = [
        a[AssetLocalInfo].bundle_path
        for a in ctx.attr.assets
        if AssetLocalInfo in a
    ]
    transitive_default_infos = [
        # file references from resource/asset instances need this, as well as
        # geometries from resource instances.
        i[DefaultInfo]
        for i in ctx.attr.instances
    ]

    # We want the runfiles tree of the resources output to contain the
    # transitive runfiles of the upstream helm_chart and file_reference rules.
    transitive_runfiles = depset(
        asset_bundles + [out],
        transitive = [
            p.default_runfiles.files
            for p in transitive_default_infos
        ] + [
            p.files
            for p in transitive_default_infos
        ],
    )
    runfiles = ctx.runfiles(
        transitive_files = transitive_runfiles,
    ).merge_all([
        ctx.attr._runfiles_dep[DefaultInfo].default_runfiles,
        ctx.attr._solution_update[DefaultInfo].default_runfiles,
    ])

    out_executable = ctx.actions.declare_file(ctx.label.name)
    ctx.actions.write(
        out_executable,
        content = """{header}
exec "$(rlocation "{target}")" "$(rlocation "{app}")" "$@"
""".format(
            header = WRAPPER_HEADER,
            target = to_rlocation_path(ctx, ctx.executable._solution_update),
            app = to_rlocation_path(ctx, out),
        ),
        is_executable = True,
    )

    return [
        DefaultInfo(
            files = depset([out]),
            executable = out_executable,
            runfiles = runfiles,
        ),
        SolutionInfo(
            solution = out,
            asset_bundles = asset_bundles,
            instance_configs = instance_configs,
        ),
    ]

intrinsic_solution = rule(
    implementation = _intrinsic_solution_impl,
    doc = "solution",
    attrs = {
        "assets": attr.label_list(
            providers = [
                [AssetInfo, AssetLocalInfo],
                [AssetInfo, AssetCatalogRefInfo],
            ],
            doc = "The assets of the solution.",
        ),
        "default_operation_mode": attr.string(
            default = "sim",
            doc = "Can be 'sim' or 'real'.",
        ),
        "display_name": attr.string(
            doc = "The display name of the solution. If not set, the name of the build target will be used.",
        ),
        "instances": attr.label_list(
            providers = [
                [AssetInstanceInfo],
            ],
            doc = "The asset instances of the solution.",
        ),
        "object_world_updates": attr.label_list(
            allow_files = [".txtpb", ".pbtxt", ".textproto"],
            doc = "Updates on the used world.",
        ),
        "_localsolutiongen": attr.label(
            default = Label("//intrinsic/assets/build_defs:localsolutiongen"),
            cfg = "exec",
            executable = True,
        ),
        "_runfiles_dep": attr.label(
            default = Label("@rules_shell//shell/runfiles"),
        ),
        "_solution_update": attr.label(
            default = Label("//intrinsic/assets/build_defs:solution_update"),
            cfg = "target",
            executable = True,
        ),
    },
    executable = True,
    provides = [SolutionInfo],
)
