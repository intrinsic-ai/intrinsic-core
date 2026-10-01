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

"""Helpers for dealing with Python docker images."""

load("@bazel_skylib//lib:paths.bzl", "paths")
load("@tar.bzl", "mtree_mutate", "mtree_spec", "tar")
load("//bazel:container.bzl", "container_image")
load("//bazel:migration_locations.bzl", "GOOGLE3_OR_IOC_LOCATIONS")  

# LINT.IfChange(python_version)
_INTERPRETER_SYMLINKS = {
    "/bin/2to3": "2to3-3.11",
    "/bin/idle3": "idle3.11",
    "/bin/pip": "pip3.11",
    "/bin/pip3": "pip3.11",
    "/bin/pydoc3": "pydoc3.11",
    "/bin/python": "python3.11",
    "/bin/python3": "python3.11",
    "/bin/python3-config": "python3.11-config",
    "/libpython3.11.so": "libpython3.11.so.1.0",
}
# LINT.ThenChange(//MODULE.bazel:python_version)

# By default, py_binary runfiles manifests list aliases like /bin/python, /bin/python3,
# and /libpython3.11.so as duplicate regular files (`type=file`) rather than true symlinks.
# If archived without modification, the image tarball contains multiple 55+ MB copies of
# identical binaries and shared libraries. Furthermore, using the standard `symlinks`
# attribute on `container_image` only layers symlink entries on top of lower layers
# without removing the physical duplicate files archived inside `_interpreter_layer`.
# To prevent this bloating, we intercept and rewrite the mtree manifest lines with `sed`
# before the tarball is built, converting duplicate file entries into lightweight `type=link`
# symlink headers directly inside `_interpreter_layer`.
def _build_interpreter_manifest_cmd(interpreter_regex):
    """Returns the shell command to build the interpreter tar manifest with symlinks restored."""
    sed_exprs = [
        "-e 's|{path} uid=.*|{path} uid=0 gid=0 time=1672560000 mode=0777 type=link link={target}|'".format(
            path = path,
            target = target,
        )
        for path, target in _INTERPRETER_SYMLINKS.items()
    ]
    return "grep '{}' $< | sed {} >$@".format(interpreter_regex, " ".join(sed_exprs))

def python_layers(name, binary, **kwargs):
    """Create list of layers for a py_binary target.

    We use three layers for the interpreter, third-party dependencies, and application code.

    The setup is adapted from https://github.com/aspect-build/bazel-examples/blob/main/oci_python_image/py_layer.bzl.

    Args:
        name: prefix for generated targets, to ensure they are unique within the package
        binary: the py_binary target.
        **kwargs: extra arguments to pass on to the layers.
    Returns:
        a list of labels for the layers, which are tar files
    """

    layers = []

    # Produce the manifest for a tar file of our py_binary, but don't tar it up yet, so we can split
    # into fine-grained layers for better docker performance.
    mtree_spec(
        name = name + "_tar_manifest_raw",
        srcs = [binary],
        include_runfiles = True,
        compatible_with = kwargs.get("compatible_with"),
        testonly = kwargs.get("testonly"),
        tags = kwargs.get("tags"),
    )

    # ADDITION: Handle local_repository sub repos by removing '../' and ' external/' from paths.
    # Without this the resulting image manifest is malformed and tools like dive cannot open the image.
    native.genrule(
        name = name + "_tar_manifest_filtered",
        srcs = [":" + name + "_tar_manifest_raw"],
        outs = [name + "_tar_manifest_filtered.spec"],
        cmd = "sed -e 's#^\\.\\./##' $< | sed -e 's# external/##g' >$@",
        compatible_with = kwargs.get("compatible_with"),
        testonly = kwargs.get("testonly"),
        tags = kwargs.get("tags"),
    )

    # Apply mutations for path prefixes
    mtree_mutate(
        name = name + "_tar_manifest_prefix",
        mtree = ":" + name + "_tar_manifest_filtered",
        # BUG: https://github.com/bazel-contrib/bazel-lib/issues/946
        # strip_prefix = kwargs.pop("data_path", None),
        package_dir = kwargs.pop("directory", None),
        compatible_with = kwargs.get("compatible_with"),
        testonly = kwargs.get("testonly"),
        tags = kwargs.get("tags"),
    )

    # Version-agnostic pattern matching the bundled rules_python interpreter directory in runfiles
    # (e.g., `rules_python++python+python_3_11_x86_64-unknown-linux-gnu/`).
    PY_INTERPRETER_DIR_REGEX = "rules_python\\S*_x86_64-unknown-linux-gnu/"

    # In rules_python 2.0+, `bootstrap_impl=system_python` creates a venv interpreter entry in runfiles:
    #   <runfiles_dir>/_main/<pkg>/_<bin>.venv/bin/python3 uid=0 ... type=file content=bazel-out/.../_<bin>.venv/bin/python3
    # Because that file in `bazel-out` is a relative symlink that only resolves inside `.runfiles/`,
    # archiving it as `type=file` fails in bsdtar.
    # Instead, we extract the bundled interpreter path (`$PY_BIN`) from the manifest itself using
    # `PY_INTERPRETER_DIR_REGEX` and replace `.venv/bin/python3 uid=.*` with a `type=link` entry.
    venv_symlink_sed = "s|\\.venv/bin/python3 uid=.*|.venv/bin/python3 uid=0 gid=0 time=1672560000 mode=0777 type=link link='\"$$PY_BIN\"'|"

    # Workaround unsupported "strip_prefix" (`s,^/,,`) and rewrite `.venv/bin/python3` to a symlink.
    native.genrule(
        name = name + "_tar_manifest",
        srcs = [":" + name + "_tar_manifest_prefix"],
        outs = [name + "_tar_manifest.spec"],
        cmd = (
            "PY_BIN=/$$(grep -m1 '{interp_dir}bin/python3 ' $< | sed -e 's,^/,,' -e 's, .*,,'); " +
            "sed -e 's,^/,,' -e '{venv_symlink_sed}' $< >$@"
        ).format(
            interp_dir = PY_INTERPRETER_DIR_REGEX,
            venv_symlink_sed = venv_symlink_sed,
        ),
        compatible_with = kwargs.get("compatible_with"),
        testonly = kwargs.get("testonly"),
        tags = kwargs.get("tags"),
    )

    # One layer with only the python interpreter.
    PY_INTERPRETER_REGEX = "^\\S*\\.runfiles/\\S*\\({interp_dir}\\|.*rules_Upython++python+python.*libpython.*\\)".format(
        interp_dir = PY_INTERPRETER_DIR_REGEX,
    )

    native.genrule(
        name = name + "_interpreter_tar_manifest",
        srcs = [":" + name + "_tar_manifest"],
        outs = [name + "_interpreter_tar_manifest.spec"],
        cmd = _build_interpreter_manifest_cmd(PY_INTERPRETER_REGEX),
        compatible_with = kwargs.get("compatible_with"),
        testonly = kwargs.get("testonly"),
        tags = kwargs.get("tags"),
    )

    tar(
        name = name + "_interpreter_layer",
        srcs = [binary],
        mtree = ":" + name + "_interpreter_tar_manifest",
        compress = "zstd",
        compatible_with = kwargs.get("compatible_with"),
        compute_unused_inputs = 1,
        testonly = kwargs.get("testonly"),
        tags = kwargs.get("tags"),
    )
    layers.append(":" + name + "_interpreter_layer")

    # Match all external (3P) repositories under `.runfiles/` (both `pip` wheels like
    # `rules_python++pip+...` and Bzlmod deps like `protobuf+`, `abseil-py+`, etc.).
    # In Bzlmod, first-party runfiles are always under `.runfiles/_main/` (plus `.runfiles/_repo_mapping`),
    # whereas external repositories never start with `_`. This also avoids falsely matching
    # `.runfiles/_main/.../_<bin>.venv/lib/python3.11/site-packages/bazel.pth` as a 3P package.
    PACKAGES_REGEX = "^\\S*\\.runfiles/[^_ ]"

    # One layer with the third-party pip packages.
    # To make sure some dependencies with surprising paths are not included twice, exclude the interpreter from the site-packages layer.
    native.genrule(
        name = name + "_packages_tar_manifest",
        srcs = [":" + name + "_tar_manifest"],
        outs = [name + "_packages_tar_manifest.spec"],
        cmd = "if ! grep -v '{}' $< | grep '{}' >$@; then touch $@; fi".format(PY_INTERPRETER_REGEX, PACKAGES_REGEX),
        compatible_with = kwargs.get("compatible_with"),
        testonly = kwargs.get("testonly"),
        tags = kwargs.get("tags"),
    )

    tar(
        name = name + "_packages_layer",
        srcs = [binary],
        mtree = ":" + name + "_packages_tar_manifest",
        compress = "zstd",
        compatible_with = kwargs.get("compatible_with"),
        compute_unused_inputs = 1,
        testonly = kwargs.get("testonly"),
        tags = kwargs.get("tags"),
    )
    layers.append(":" + name + "_packages_layer")

    # Any lines that didn't match one of the two grep above...
    native.genrule(
        name = name + "_app_tar_manifest",
        srcs = [":" + name + "_tar_manifest"],
        outs = [name + "_app_tar_manifest.spec"],
        cmd = "grep -v '{}' $< | grep -v '{}' >$@".format(PACKAGES_REGEX, PY_INTERPRETER_REGEX),
        compatible_with = kwargs.get("compatible_with"),
        testonly = kwargs.get("testonly"),
        tags = kwargs.get("tags"),
    )

    # ... go into the third layer which is the application. We assume it changes the most frequently.
    tar(
        name = name + "_app_layer",
        srcs = [binary],
        mtree = ":" + name + "_app_tar_manifest",
        compress = "zstd",
        compatible_with = kwargs.get("compatible_with"),
        compute_unused_inputs = 1,
        testonly = kwargs.get("testonly"),
        tags = kwargs.get("tags"),
    )
    layers.append(":" + name + "_app_layer")

    return layers

def python_oci_image(
        name,
        binary,
        base = None,
        extra_tars = None,
        symlinks = None,
        test_layers = None,
        **kwargs):
    """Wrapper for creating a oci_image from a py_binary target.

    Will create both an oci_image ($name) and a container_tarball ($name.tar) target.

    The setup is inspired by https://github.com/aspect-build/bazel-examples/blob/main/oci_python_image/hello_world/BUILD.bazel.

    Args:
      name: name of the image.
      base: base image to use.
      binary: the py_binary target.
      extra_tars: additional layers to add to the image with e.g. supporting files.
      symlinks: if specified, symlinks to add to the final image (analogous to rules_docker container_image#sylinks).
      test_layers: optional list of layers to test with container_structure_test.
        Defaults to [':' + name + '_app_layer', ':' + name + '_interpreter_layer']
        (plus extra_tars), skipping the heavy pip packages layer for fast testing.
      **kwargs: extra arguments to pass on to the oci_image target.
    """

    if base == None:
        base = Label("//intrinsic/kubernetes:python3-image-oci")  

    layer_kwargs = {key: value for key, value in kwargs.items() if key in ["compatible_with", "directory", "tags", "testonly"]}
    layers = python_layers(
        name = name,
        binary = binary,
        visibility = ["//visibility:private"],
        **layer_kwargs
    )

    binary_label = native.package_relative_label(binary)
    package_str = binary_label.package


    if package_str.startswith("google3/"):
        package_str = package_str.removeprefix("google3/")
    elif package_str == "google3":
        package_str = ""



    binary_path = paths.join("/", kwargs.get("directory", ""), package_str, binary_label.name)

    if kwargs.get("cmd") == None:
        kwargs["cmd"] = [binary_path]

    # By default, the runfiles path is prefixed by the repo name while the binary path is not. Adding a symlink to fix the runfiles detection logic in case the repo is not empty.
    if binary_label.repo_name:
        binary_runfiles_path = paths.join("/", kwargs.get("directory", ""), binary_label.repo_name, binary_label.package, binary_label.name + ".runfiles")
        symlinks = (symlinks or {}) | {
            binary_path + ".runfiles": binary_runfiles_path,
        }

    if test_layers == None:
        test_layers = [":" + name + "_app_layer", ":" + name + "_interpreter_layer"]
        if extra_tars:
            test_layers.extend(extra_tars)
        _pkg = native.package_name()


        for location in GOOGLE3_OR_IOC_LOCATIONS:
            if _pkg.startswith(location + "/") or _pkg == location:
                test_layers.append(":" + name + "_symlink_layer")
                break


        if symlinks and ":" + name + "_symlink_layer" not in test_layers:
            test_layers.append(":" + name + "_symlink_layer")
        if kwargs.get("files"):
            test_layers.append(":" + name + "_files_layer")
        if kwargs.get("tars"):
            test_layers.append(":" + name + "_tar_layer")
    else:
        test_layers = list(test_layers)

    if extra_tars:
        layers.extend(extra_tars)

    container_image(
        name = name,
        base = base,
        layers = layers,
        symlinks = symlinks,
        test_layers = test_layers,
        **kwargs
    )
