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
Exec Properties used for executors.
"""

# @unsorted-dict-items
DEFAULT = {
    # LINT.IfChange
    "container-image": "docker://us-central1-docker.pkg.dev/intrinsic-mirror/intrinsic-build-images/bazel-rbe-executor@sha256:d65e14cff14c8e81a759b3cea05a6ab48e8d35231cab240a46e1566d023f576f",
    # LINT.ThenChange(//.bazelrc)
    "OSFamily": "Linux",
    "Pool": "default_pool",  
}

REQUIRES_NETWORK = {
    "dockerNetwork": "standard",
}


# label:<field> is used to select a pool. The pools are configured using
# `gcloud alpha remote-build-execution worker-pools`. See
# https://developers.google.com/remote-build-execution/docs/multipool#setting_up_multiple_pools
GPU_NVIDIA_TESLA_T4 = DEFAULT | {
    "Pool": "gpu_tesla_t4_pool",
    "dockerRuntime": "nvidia",
    "label:gpu": "nvidia-tesla-t4",
}

HIGHMEM_POOL_4 = DEFAULT | {
    "Pool": "highmem_pool_4",
}

# TODO: b/510995254 - Restore execution context when RBE pool is available again or switch to another GPU.
# GPU_NVIDIA_TESLA_V100 = DEFAULT | {
#     "dockerRuntime": "nvidia",
#     "label:gpu": "nvidia-tesla-v100",
#     "Pool": "gpu_tesla_v100_pool",
# }

