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

load("@rules_oci//oci:defs.bzl", "oci_image")
load("@rules_pkg//:pkg.bzl", "pkg_tar")
load("//intrinsic_sdk/bazel:cc_oci_image.bzl", "cc_oci_image")
load("//intrinsic_sdk/bazel:container.bzl", "container_image")

package(
    default_visibility = ["//visibility:public"],
)

# When GL and X libraries are installed from apt package layer, the following
# symlinks are not created automatically. But they are required for Python
# graphics libraries to load correctly.
pkg_tar(
    name = "gl_symlinks",
    symlinks = {
        "/usr/lib/x86_64-linux-gnu/libEGL.so": "libEGL.so.1",
        "/usr/lib/x86_64-linux-gnu/libGL.so": "libGL.so.1",
        "/usr/lib/x86_64-linux-gnu/libGLX.so": "libGLX.so.0",
        "/usr/lib/x86_64-linux-gnu/libX11.so": "libX11.so.6",
        "/usr/lib/x86_64-linux-gnu/libXext.so": "libXext.so.6",
        "/usr/lib/x86_64-linux-gnu/libXfixes.so": "libXfixes.so.3",
        "/usr/lib/x86_64-linux-gnu/libXrender.so": "libXrender.so.1",
    },
)

# Gzserver uses a custom base image to support sensor rendering on non-GPU hosts.
# TODO(b/458481026): Drop this image once mesa llvmpipe is available in the
# bazel build.
oci_image(
    name = "bullseye_mesa_base_image",
    architecture = "amd64",
    entrypoint = [],
    os = "linux",
    tars = [
        "@bullseye_mesa//:packages",
        ":gl_symlinks",
    ],
)

container_image(
    name = "bullseye_mesa_with_zenoh",
    base = ":bullseye_mesa_base_image",
    entrypoint = [
        "/dumb-init",
        "--",
    ],
    files = ["@dumb-init"],
)

cc_oci_image(
    name = "gzserver-proxy-oci-image",
    binary = "//intrinsic/simulation/gazebo/grpc_proxy:proxy_server_main",
    data_path = "/",
    visibility = [
        "//intrinsic_runtime/kubernetes/intrinsic_base:__pkg__",
    ],
)
