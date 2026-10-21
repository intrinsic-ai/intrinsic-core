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

"""Helpers for dealing with C++ docker images."""

load("@bazel_skylib//lib:paths.bzl", "paths")
load(
    "//bazel:container.bzl",
    "container_image",
    "container_layer",
)

def cc_oci_image(
        name,
        binary,
        base = None,
        extra_tars = None,
        symlinks = None,
        **kwargs):
    """Wrapper for creating a oci_image from a cc_binary target.

    Will create both an oci_image ($name) and a container_tarball ($name.tar) target.

    Args:
      name: name of the image.
      base: base image to use.
      binary: the cc_binary target.
      extra_tars: additional layers to add to the image with e.g. supporting files.
      symlinks: if specified, symlinks to add to the final image (analogous to rules_docker container_image#sylinks).
      **kwargs: extra arguments to pass on to the oci_image target.
    """

    if base == None:
        base = Label("//intrinsic/kubernetes:base-image-oci")  

    layer_kwargs = {key: value for key, value in kwargs.items() if key in ["compatible_with", "data_path", "directory", "testonly"]}
    container_layer(
        name = name + "_binary_layer",
        files = [binary],
        include_runfiles = True,  # Include dynamic libraries
        visibility = ["//visibility:private"],
        **layer_kwargs
    )
    layers = [name + "_binary_layer"]

    binary_label = native.package_relative_label(binary)

    # TODO(b/477580650): Remove after move to incode
    # Normalize the package path to ensure the container entrypoint
    # and structure tests remain identical before and after the migration.
    package_str = binary_label.package


    if package_str.startswith("google3/"):
        package_str = package_str.removeprefix("google3/")
    elif package_str == "google3":
        package_str = ""



    binary_path = paths.join("/", kwargs.get("directory", ""), package_str, binary_label.name)

    if kwargs.get("cmd") == None:
        kwargs["cmd"] = [binary_path]

    if extra_tars:
        layers.extend(extra_tars)

    container_image(
        name = name,
        base = base,
        layers = layers,
        symlinks = symlinks,
        **kwargs
    )
