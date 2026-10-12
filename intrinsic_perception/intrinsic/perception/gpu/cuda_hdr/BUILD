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

load("//bazel:cc_macros.bzl", "cc_library", "cuda_library")

package(default_visibility = [
    "//visibility:public",
])

cc_library(
    name = "hdr_mertens",
    srcs = ["hdr_mertens.cc"],
    hdrs = ["hdr_mertens.h"],
    deps = [
        ":hdr_mertens_weight_map",
        "//intrinsic/util/status:ret_check",
        "//intrinsic/util/status:status_macros",
        "//intrinsic_perception/intrinsic/perception/core:dimensions",
        "//intrinsic_perception/intrinsic/perception/core:image",
        "//intrinsic_perception/intrinsic/perception/core:image_traits",
        "//intrinsic_perception/intrinsic/perception/gpu/cuda_image_proc:border_extrapolation",
        "//intrinsic_perception/intrinsic/perception/gpu/cuda_image_proc:pixelwise",
        "//intrinsic_perception/intrinsic/perception/gpu/cuda_image_proc:pyramid",
        "//intrinsic_perception/intrinsic/perception/gpu/cuda_utils",
        "//intrinsic_perception/intrinsic/perception/gpu/cuda_utils:cuda_image_utils",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cuda_library(
    name = "hdr_mertens_weight_map",
    srcs = ["hdr_mertens_weight_map.cu.cc"],
    hdrs = ["hdr_mertens_weight_map.h"],
    deps = [
        "//intrinsic/util/status:status_macros",
        "//intrinsic_perception/intrinsic/perception/core:dimensions",
        "//intrinsic_perception/intrinsic/perception/core:image_traits",
        "//intrinsic_perception/intrinsic/perception/gpu/cuda_image_proc:border_extrapolation",
        "//intrinsic_perception/intrinsic/perception/gpu/cuda_image_proc:color_conversion_device",
        "//intrinsic_perception/intrinsic/perception/gpu/cuda_image_proc:laplacian_device",
        "//intrinsic_perception/intrinsic/perception/gpu/cuda_utils",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@cuda//:cuda_headers",
    ],
)
