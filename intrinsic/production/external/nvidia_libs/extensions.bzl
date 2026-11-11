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

"""
Bazel module extension for NVIDIA CUDA and CUDNN libraries.

Example usage in MODULE.bazel:
nvidia_libs_ext = use_extension(
    "//intrinsic/production/external/nvidia_libs:extensions.bzl",
    "nvidia_libs_ext",
)
nvidia_libs_ext.cuda_libs(
    name = "cuda_libs",
    components = [
        "cuda_cudart",
        "cuda_nvcc",
        "libcublas",
        "libcufft",
        "libcusolver",
        "libcusparse",
        "libnvjitlink",
    ],
    cuda_version = "12.5.1",
    manifest_sha256 = "7ab9c76014ae4907fa1b51738af599607a5fd8ca3a5c4bb4c3b31338cc642a93",
)
nvidia_libs_ext.cudnn_libs(
    name = "cudnn_libs",
    components = ["cudnn"],
    cudnn_cuda_version = "12",
    cudnn_version = "9.3.0",
    manifest_sha256 = "d17d9a7878365736758550294f03e633a0b023bec879bf173349bfb34781972e",
)
nvidia_libs_ext.nccl_libs(
    name = "nccl_libs",
    cuda_version = "12.4",
    nccl_version = "2.27.7",
    sha256 = "944e1ba3a5ca9bbd449bbaed9796626122670e58dea3031837a3910a90ffd5ee",
)
use_repo(nvidia_libs_ext, "cuda_libs", "cudnn_libs", "nccl_libs")
"""

load("//intrinsic/production/external/nvidia_libs/private:cuda_from_redist.bzl", "cuda_from_redist")
load("//intrinsic/production/external/nvidia_libs/private:cudnn_from_redist.bzl", "cudnn_from_redist")
load("//intrinsic/production/external/nvidia_libs/private:nccl_from_redist.bzl", "nccl_from_redist")

def _nvidia_libs_ext_impl(module_ctx):
    """Implementation of the nvidia_libs_ext module extension."""
    for module in module_ctx.modules:
        for tag in module.tags.cuda_libs:
            cuda_from_redist(
                name = tag.name,
                manifest_sha256 = tag.manifest_sha256,
                cuda_version = tag.cuda_version,
                components = tag.components,
                arch = tag.arch,
                os_name = tag.os_name,
            )

        for tag in module.tags.cudnn_libs:
            cudnn_from_redist(
                name = tag.name,
                manifest_sha256 = tag.manifest_sha256,
                cudnn_version = tag.cudnn_version,
                cudnn_cuda_version = tag.cudnn_cuda_version,
                components = tag.components,
                arch = tag.arch,
                os_name = tag.os_name,
            )

        for tag in module.tags.nccl_libs:
            nccl_from_redist(
                name = tag.name,
                nccl_version = tag.nccl_version,
                cuda_version = tag.cuda_version,
                sha256 = tag.sha256,
                arch = tag.arch,
            )

    return module_ctx.extension_metadata(reproducible = True)

_cuda_libs_tag = tag_class(
    attrs = {
        "arch": attr.string(default = "x86_64", doc = "Architecture (e.g., x86_64)."),
        "components": attr.string_list(mandatory = True, doc = "List of CUDA components to download."),
        "cuda_version": attr.string(mandatory = True, doc = "NVIDIA CUDA version, e.g., '12.6.2'"),
        "manifest_sha256": attr.string(mandatory = True, doc = "The SHA256 checksum of the redistrib JSON manifest."),
        "name": attr.string(mandatory = True, doc = "Name for the CUDA repository."),
        "os_name": attr.string(default = "linux", doc = "OS name, e.g., 'linux'."),
    },
)

_cudnn_libs_tag = tag_class(
    attrs = {
        "arch": attr.string(default = "x86_64", doc = "Architecture (e.g., x86_64)."),
        "components": attr.string_list(mandatory = True, doc = "List of CUDNN components to download."),
        "cudnn_cuda_version": attr.string(default = "12", doc = "The CUDA major version variant for CUDNN."),
        "cudnn_version": attr.string(mandatory = True, doc = "NVIDIA CUDNN version, e.g., '8.9.7'"),
        "manifest_sha256": attr.string(mandatory = True, doc = "The SHA256 checksum of the redistrib JSON manifest."),
        "name": attr.string(mandatory = True, doc = "Name for the CUDNN repository."),
        "os_name": attr.string(default = "linux", doc = "OS name, e.g., 'linux'."),
    },
)

_nccl_libs_tag = tag_class(
    attrs = {
        "arch": attr.string(default = "x86_64", doc = "Architecture (e.g., x86_64)."),
        "cuda_version": attr.string(mandatory = True, doc = "The target CUDA version, e.g., '12.9'. Provide only <major>.<minor> version number"),
        "name": attr.string(mandatory = True, doc = "Name for the NCCL repository."),
        "nccl_version": attr.string(mandatory = True, doc = "NVIDIA NCCL version, e.g., '2.27.5'"),
        "sha256": attr.string(mandatory = True, doc = "The SHA256 checksum of the NCCL .txz archive."),
    },
)

nvidia_libs_ext = module_extension(
    implementation = _nvidia_libs_ext_impl,
    tag_classes = {
        "cuda_libs": _cuda_libs_tag,
        "cudnn_libs": _cudnn_libs_tag,
        "nccl_libs": _nccl_libs_tag,
    },
)
