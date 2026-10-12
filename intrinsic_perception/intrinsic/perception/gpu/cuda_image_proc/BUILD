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

cuda_library(
    name = "border_extrapolation",
    hdrs = [
        "border_extrapolation.h",
        "border_extrapolation_device.h",
    ],
    deps = [
        "@cuda//:cuda_headers",
    ],
)

cuda_library(
    name = "color_conversion_device",
    hdrs = ["color_conversion_device.h"],
    deps = [
        "@cuda//:cuda_headers",
    ],
)

cuda_library(
    name = "laplacian_device",
    hdrs = ["laplacian_device.h"],
    deps = [
        "@cuda//:cuda_headers",
    ],
)

cuda_library(
    name = "pixelwise",
    srcs = ["pixelwise.cu.cc"],
    hdrs = ["pixelwise.h"],
    deps = [
        "//intrinsic/util/status:status_macros",
        "//intrinsic_perception/intrinsic/perception/core:image_traits",
        "//intrinsic_perception/intrinsic/perception/gpu/cuda_utils",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@cuda//:cuda_headers",
    ],
)

cc_library(
    name = "pyramid",
    srcs = ["pyramid.cc"],
    hdrs = ["pyramid.h"],
    deps = [
        ":border_extrapolation",
        ":pixelwise",
        ":resize",
        ":smooth",
        "//intrinsic/util/status:ret_check",
        "//intrinsic/util/status:status_macros",
        "//intrinsic_perception/intrinsic/perception/core:dimensions",
        "//intrinsic_perception/intrinsic/perception/core:image_traits",
        "//intrinsic_perception/intrinsic/perception/gpu/cuda_utils",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:str_format",
    ],
)

cuda_library(
    name = "resize",
    srcs = ["resize.cu.cc"],
    hdrs = ["resize.h"],
    deps = [
        "//intrinsic/util/status:status_macros",
        "//intrinsic_perception/intrinsic/perception/core:dimensions",
        "//intrinsic_perception/intrinsic/perception/core:image_traits",
        "//intrinsic_perception/intrinsic/perception/gpu/cuda_utils",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:str_format",
        "@cuda//:cuda_headers",
    ],
)

cuda_library(
    name = "smooth",
    srcs = ["smooth.cu.cc"],
    hdrs = ["smooth.h"],
    deps = [
        ":border_extrapolation",
        ":smooth_device",
        "//intrinsic/util/status:status_macros",
        "//intrinsic_perception/intrinsic/perception/core:dimensions",
        "//intrinsic_perception/intrinsic/perception/core:image_traits",
        "//intrinsic_perception/intrinsic/perception/gpu/cuda_utils",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:str_format",
        "@cuda//:cuda_headers",
    ],
)

cuda_library(
    name = "smooth_device",
    hdrs = ["smooth_device.h"],
    deps = [
        "@cuda//:cuda_headers",
    ],
)
