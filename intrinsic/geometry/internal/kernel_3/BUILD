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

""" Intrinsic package geometry/processing/kernel_3. """

load("//bazel:cc_macros.bzl", "cc_library")

package(default_visibility = [
    "//intrinsic/geometry:__subpackages__",
    "//intrinsic/scene:__subpackages__",
    "//intrinsic_sdk/intrinsic/geometry:__subpackages__",
])

cc_library(
    name = "exact_predicates_inexact_constructions_kernel",
    hdrs = ["exact_predicates_inexact_constructions_kernel.h"],
    copts = ["-fexceptions"] + select({
        "@platforms//cpu:x86_32": ["-frounding-math"],
        "@platforms//cpu:x86_64": ["-frounding-math"],
        "//conditions:default": [],
    }),
    visibility = [
        "//intrinsic/geometry:__subpackages__",
        "//intrinsic/geometry/api:__pkg__",
        "//intrinsic/scene:__subpackages__",
        "//intrinsic_sdk/intrinsic/geometry:__subpackages__",
        "//intrinsic_sdk/intrinsic/scene/sdf:__pkg__",
    ],
    deps = [
        "@cgal",
    ],
)
