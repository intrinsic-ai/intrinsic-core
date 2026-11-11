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

"""Bazel macros for OpenGL cc_test targets."""

load("@rules_python//python:defs.bzl", "py_binary", "py_test")
load("//bazel:cc_macros.bzl", "cc_test")

def cc_opengl_test(name, srcs, deps = [], tags = [], env = {}, **kwargs):
    """Wrapper around cc_test for tests requiring OpenGL and NVIDIA drivers.

    Args:
      name: The name of the test.
      srcs: A list of C++ source files for the test.
      deps: A list of dependencies for the test.
      tags: A list of tags for the test.
      env: A dictionary of key-value pairs to be passed to the test environment.
      **kwargs: Additional arguments to pass to cc_test.
    """

    # Needed so that the test can use the NVIDIA driver.
    env = env | {
        "NVIDIA_DRIVER_CAPABILITIES": "compute,graphics,utility",
    }

    deps = deps + [
        "//intrinsic/production/external/glvnd_stubs",
    ]

    cc_test(
        name = name,
        srcs = srcs,
        tags = tags,
        deps = deps,
        env = env,
        **kwargs
    )

def _glvnd_env(pkg_label, arch, existing_ld_path):
    pkg = Label(pkg_label).repo_name
    ld_path = "../{}/usr/lib/{}-linux-gnu/".format(pkg, arch)
    if existing_ld_path:
        ld_path = "{}:{}".format(ld_path, existing_ld_path)
    return {
        "LD_LIBRARY_PATH": ld_path,
    }

def _wrap_py_opengl_rule(
        py_rule,
        name,
        srcs,
        data = [],
        env = {},
        **kwargs):
    """Wrapper around Python rules for targets requiring OpenGL and NVIDIA drivers.

    Args:
      py_rule: Actual Python rule to instantiate.
      name: The name of the target.
      srcs: A list of Python source files.
      data: A list of data dependencies.
      env: A dictionary of key-value pairs to be passed to the environment.
      **kwargs: Additional arguments to pass.
    """

    data = data + [
        "//intrinsic/production/external/glvnd_stubs:files",
    ]

    # Needed so that the test can use the NVIDIA driver.
    base_env = env | {
        "NVIDIA_DRIVER_CAPABILITIES": "compute,graphics,utility",
    }
    existing_ld_path = base_env.get("LD_LIBRARY_PATH") or ""
    env = select({
        "@platforms//cpu:aarch64": base_env | _glvnd_env(
            "@glvnd_stubs_arm64//:files",
            "aarch64",
            existing_ld_path,
        ),
        "//conditions:default": base_env | _glvnd_env(
            "@glvnd_stubs_amd64//:files",
            "x86_64",
            existing_ld_path,
        ),
    })

    py_rule(
        name = name,
        srcs = srcs,
        data = data,
        env = env,
        **kwargs
    )

def py_opengl_binary(**kwargs):
    _wrap_py_opengl_rule(py_binary, **kwargs)

def py_opengl_test(**kwargs):
    _wrap_py_opengl_rule(py_test, **kwargs)
