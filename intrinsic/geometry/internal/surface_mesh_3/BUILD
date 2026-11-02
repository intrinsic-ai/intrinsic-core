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

""" Intrinsic package geometry/processing/surface_mesh_3. """

load("//bazel:cc_macros.bzl", "cc_library")

package(default_visibility = [
    "//intrinsic/geometry:__subpackages__",
    "//intrinsic_sdk/intrinsic/geometry:__subpackages__",
])

cc_library(
    name = "surface_mesh_3d",
    hdrs = ["surface_mesh_3d.h"],
    copts = ["-fexceptions"] + select({
        "@platforms//cpu:x86_32": ["-frounding-math"],
        "@platforms//cpu:x86_64": ["-frounding-math"],
        "//conditions:default": [],
    }),
    deps = [
        "//intrinsic/geometry/internal/kernel_3:exact_predicates_inexact_constructions_kernel",
        "@cgal",
    ],
)

cc_library(
    name = "unary_predicates",
    srcs = ["unary_predicates.cc"],
    hdrs = ["unary_predicates.h"],
    copts = ["-fexceptions"] + select({
        "@platforms//cpu:x86_32": ["-frounding-math"],
        "@platforms//cpu:x86_64": ["-frounding-math"],
        "//conditions:default": [],
    }),
    deps = [
        ":surface_mesh_3d",
        "@cgal",
    ],
)
