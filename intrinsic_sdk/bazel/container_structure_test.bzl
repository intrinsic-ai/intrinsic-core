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

"""Lightweight container structure test rule.

Validates image layer archives, manifests, and OCI directories directly without
requiring monolithic container tarballs, docker daemons, or rules_python overhead.

Supported Test Types:
Only `fileExistenceTests` are supported. Archive metadata is streamed directly without
unpacking layers to disk.

Unsupported Test Types (Fail-Fast):
`commandTests`, `fileContentTests`, `metadataTest`, and `licenseTests` are
intentionally excluded and unsupported. Executing commands requires a container
runtime (such as dockerd, containerd, or runc) and an unpacked root filesystem,
which contradicts the zero-unpacking streaming architecture. Configurations containing
unsupported test sections will fail fast with an error.

Note on Layer Stacking and Whiteouts:
Layers and OCI directory blobs are inspected directly by streaming archive metadata
without unpacking file contents to disk. Overlay whiteout markers (e.g. .wh.<filename>
or .wh..wh..opq) and multi-layer application ordering are not interpreted, which makes
execution fast while covering standard file existence and permission checks.
"""

def _container_structure_test_impl(ctx):
    configs = ctx.files.configs
    if not configs:
        fail("At least one config file must be provided in 'configs'")

    layers = list(ctx.files.layers)
    if ctx.attr.image:
        layers.extend(ctx.files.image)

    if not layers:
        fail("At least one of 'image' or 'layers' must be provided in '%s'" % ctx.label)

    executable = ctx.actions.declare_file(ctx.label.name + ".sh")
    runner = ctx.executable._runner

    ctx.actions.write(
        output = executable,
        content = """#!/usr/bin/env bash
set -euo pipefail
exec "${{RUNFILES_DIR:-${{TEST_SRCDIR:-$0.runfiles}}}}/{workspace_name}/{runner}" -target="{target}" -configs="{configs}" -layers="{layers}"
""".format(
            configs = ",".join([config.short_path for config in configs]),
            layers = ",".join([layer.short_path for layer in layers]),
            runner = runner.short_path,
            target = str(ctx.label),
            workspace_name = ctx.workspace_name,
        ),
        is_executable = True,
    )

    runfiles = ctx.runfiles(files = configs + layers).merge(
        ctx.attr._runner[DefaultInfo].default_runfiles,
    )

    return DefaultInfo(
        executable = executable,
        runfiles = runfiles,
    )

container_structure_test = rule(
    doc = """Runs lightweight structural tests on container image layers.

Only `fileExistenceTests` are supported. `commandTests`, `fileContentTests`,
`metadataTest`, and `licenseTests` are unsupported and will fail fast.
""",
    implementation = _container_structure_test_impl,
    test = True,
    attrs = {
        "configs": attr.label_list(
            allow_files = [".yaml", ".yml", ".json"],
            mandatory = True,
            doc = "List of YAML/JSON structure test configuration files.",
        ),
        "image": attr.label(
            allow_files = True,
            doc = "The oci_image target, layer target, or tarball to validate.",
        ),
        "layers": attr.label_list(
            allow_files = True,
            default = [],
            doc = "Optional explicit list of layer tarballs / specs.",
        ),
        "_runner": attr.label(
            default = Label("//intrinsic_sdk/bazel:container_structure_test_runner"),
            executable = True,
            cfg = "exec",
        ),
    },
)
