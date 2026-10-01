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

"""Helpers for dealing with the rules_docker->rules_oci transition."""

load("@bazel_skylib//rules:write_file.bzl", "write_file")
load(
    "@rules_oci//oci:defs.bzl",
    "oci_image",
    "oci_load",
)
load("@rules_pkg//pkg:tar.bzl", "pkg_tar")
load("//bazel:container_structure_test.bzl", "container_structure_test")
load("//bazel:migration_locations.bzl", "GOOGLE3_OR_IOC_LOCATIONS")  

def _container_import_impl(ctx):
    output = ctx.actions.declare_directory(ctx.label.name)
    regctl = ctx.toolchains["@rules_oci//oci:regctl_toolchain_type"].regctl_info.binary
    ctx.actions.run_shell(
        command = """
            set -euo pipefail
            "{regctl}" image import "ocidir://{output}:latest" "{tarball}"
            "{regctl}" image mod "ocidir://{output}:latest" --layer-compress "{compression}" --replace
        """.format(
            regctl = regctl.path,
            output = output.path,
            tarball = ctx.file.tarball.path,
            compression = ctx.attr.compression,
        ),
        tools = [regctl],
        inputs = [ctx.file.tarball],
        mnemonic = "ExtractContainerTarball",
        outputs = [output],
    )
    return DefaultInfo(
        files = depset([output]),
        runfiles = ctx.runfiles(files = [output]),
    )

container_import = rule(
    attrs = {
        "compression": attr.string(
            default = "zstd",
            doc = "Layer compression type for imported image layers (gzip, zstd, none).",
            values = [
                "gzip",
                "zstd",
                "none",
            ],
        ),
        "tarball": attr.label(
            allow_single_file = [".tar"],
        ),
    },
    doc = "Imports an image tarball into an oci-layout directory",
    implementation = _container_import_impl,
    toolchains = [
        "@rules_oci//oci:regctl_toolchain_type",
    ],
)

def _symlink_tarball_impl(ctx):
    ctx.actions.symlink(output = ctx.outputs.output, target_file = ctx.attr.src[OutputGroupInfo].tarball.to_list()[0])

_symlink_tarball = rule(
    implementation = _symlink_tarball_impl,
    doc = "Creates a symlink to tarball.tar in src's DefaultInfo at output",
    attrs = {
        "output": attr.output(),
        "src": attr.label(
            providers = [OutputGroupInfo],
            mandatory = True,
        ),
    },
)

def _container_tarball(name, image, **kwargs):
    oci_load(
        name = name,
        image = image,
        **kwargs
    )

    # TODO replace with filegroup() as suggested in https://github.com/bazel-contrib/rules_oci/pull/548
    # intrinsic_service() currently doesn't allow multiple images to have the same basename. The tarball is always called tarball.tar.
    _symlink_tarball(
        name = "%s_symlink" % name,
        src = name,
        output = "%s.tar" % image,
        compatible_with = kwargs.get("compatible_with"),
        visibility = kwargs.get("visibility"),
        tags = kwargs.get("tags"),
        testonly = kwargs.get("testonly"),
    )

def container_layer(name, **kwargs):
    pkg_tar(
        name = name,
        compressor = Label("//bazel:zstd"),
        compressor_args = "-3 -q",
        deps = kwargs.pop("tars", None),
        extension = "tar.zst",
        package_dir = kwargs.pop("directory", None),

        # Support the google3/ sub-workspace. This is required for starting helm chart.
        # NOTE This currently doesn't apply to "deps": https://github.com/bazelbuild/rules_pkg/issues/955
        remap_paths = {
            "external/intrinsic-core+/": "",
            "external/intrinsic-core~/": "",
            "external/intrinsic-core~override/": "",
            "external/insrc+/": "",
            "external/insrc~/": "",
            "external/insrc~override/": "",
            "external/ioc+/": "",
            "external/ioc~/": "",
            "external/ioc~override/": "",
            "external/ai_intrinsic_sdks+/": "",
            "external/ai_intrinsic_sdks~/": "",
            "external/ai_intrinsic_sdks~override/": "",
        },

        strip_prefix = kwargs.pop("data_path", None),
        srcs = kwargs.pop("files", None),
        **kwargs
    )

# buildozer: disable=function-docstring-args
def container_image(
        name,
        base = None,
        cmd = None,
        data_path = None,
        directory = None,
        entrypoint = None,
        layers = None,
        tars = None,
        files = None,
        symlinks = None,
        labels = None,
        test_layers = None,
        **kwargs):
    """Wrapper for creating an oci_image from a rules_docker container_image target.

    Will create both an oci_image ($name) and a _container_tarball ($name.tar) target.

    Note that it does not support the experimental_tarball_format attribute:
    - All tarballs created by this macro will be in .tar.gz format.
    - Existing tarballs won't be compressed if they are not already compressed.

    See https://docs.aspect.build/guides/rules_oci_migration/#container_image for the official conversion documentation.
    """
    if not layers:
        layers = []


    # TODO (b/477580650): Remove after google3/ folder moved to incode/
    # Seamless transition for when google3/MODULE.bazel is removed.
    #  symlinks to provide the expected /intrinsic/... alias fo the yaml files
    symlinks = symlinks or {}

    _pkg_name = native.package_name()
    for location in GOOGLE3_OR_IOC_LOCATIONS:
        if _pkg_name.startswith(location + "/") or _pkg_name == location:
            symlinks["/intrinsic"] = "/" + location + "/intrinsic"
            symlinks["intrinsic"] = location + "/intrinsic"
            symlinks["/::skills::/intrinsic"] = location + "/intrinsic"
            break



    if tars:
        container_layer(
            name = name + "_tar_layer",
            tars = tars,
            data_path = data_path,
            directory = directory,
            compatible_with = kwargs.get("compatible_with"),
            visibility = kwargs.get("visibility"),
            tags = kwargs.get("tags"),
            testonly = kwargs.get("testonly"),
        )
        layers.append(name + "_tar_layer")

    if files:
        container_layer(
            name = name + "_files_layer",
            files = files,
            data_path = data_path,
            directory = directory,
            compatible_with = kwargs.get("compatible_with"),
            visibility = kwargs.get("visibility"),
            tags = kwargs.get("tags"),
            testonly = kwargs.get("testonly"),
        )
        layers.append(name + "_files_layer")

    if symlinks:
        container_layer(
            name = name + "_symlink_layer",
            symlinks = symlinks,
            data_path = "/",
            compatible_with = kwargs.get("compatible_with"),
            visibility = kwargs.get("visibility"),
            tags = kwargs.get("tags"),
            testonly = kwargs.get("testonly"),
        )
        layers.append(name + "_symlink_layer")

    oci_image(
        name = name,
        base = base,
        tars = layers,
        entrypoint = entrypoint,
        cmd = cmd,
        labels = labels,
        **kwargs
    )

    tag = "%s:latest" % name
    package = native.package_name()
    if package:
        tag = "%s/%s" % (package, tag)

    tarball_name = "_%s_tarball" % name
    _container_tarball(
        name = tarball_name,
        image = name,
        compatible_with = kwargs.get("compatible_with"),
        repo_tags = [tag],
        visibility = kwargs.get("visibility"),
        tags = kwargs.get("tags"),
        testonly = kwargs.get("testonly"),
    )

    if test_layers == None:
        test_layers = list(layers)
        if base != None:
            test_layers.append(base)
    else:
        test_layers = list(test_layers)

    path_under_test = None
    if entrypoint != None and len(entrypoint) > 0 and (entrypoint[0].startswith("/") or entrypoint[0].startswith("intrinsic")):
        path_under_test = entrypoint[0]
    if cmd != None and len(cmd) > 0 and (cmd[0].startswith("/") or cmd[0].startswith("intrinsic")):
        path_under_test = cmd[0]

    if test_layers and path_under_test != None:
        test_config_name = "_%s_test_config" % name
        test_config_template = """schemaVersion: "2.0.0"

fileExistenceTests:
- name: "Entrypoint existence test"
  path: "{}"
  shouldExist: true
  isExecutableBy: "owner"
""".format(path_under_test)
        write_file(
            name = test_config_name,
            out = "_%s_test_config.yaml" % name,
            content = [test_config_template],
            tags = kwargs.get("tags"),
            testonly = kwargs.get("testonly"),
            compatible_with = kwargs.get("compatible_with"),
            target_compatible_with = kwargs.get("target_compatible_with"),
        )
        container_structure_test(
            name = "_%s_test" % name,
            configs = [test_config_name],
            layers = test_layers,
            tags = kwargs.get("tags"),
            testonly = kwargs.get("testonly"),
            compatible_with = kwargs.get("compatible_with"),
            target_compatible_with = kwargs.get("target_compatible_with"),
        )
