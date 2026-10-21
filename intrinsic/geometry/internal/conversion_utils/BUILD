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

""" Intrinsic package geometry/processing/conversion_utils. """

load("//bazel:cc_macros.bzl", "cc_library")

package(default_visibility = ["//intrinsic/geometry:__subpackages__"])

cc_library(
    name = "extract_all_point_3d",
    hdrs = ["extract_all_point_3d.h"],
    copts = ["-fexceptions"] + select({
        "@platforms//cpu:x86_32": ["-frounding-math"],
        "@platforms//cpu:x86_64": ["-frounding-math"],
        "//conditions:default": [],
    }),
    deps = [
        "//intrinsic/geometry/internal/indexed_triangle_set_3",
        "//intrinsic/geometry/internal/kernel_3:exact_predicates_inexact_constructions_kernel",
        "//intrinsic/geometry/internal/surface_mesh_3:surface_mesh_3d",
        "//intrinsic_sdk/intrinsic/geometry/internal/mesh",
        "@cgal",
    ],
)

cc_library(
    name = "mesh_conversion",
    srcs = ["mesh_conversion.cc"],
    hdrs = ["mesh_conversion.h"],
    copts = ["-fexceptions"] + select({
        "@platforms//cpu:x86_32": ["-frounding-math"],
        "@platforms//cpu:x86_64": ["-frounding-math"],
        "//conditions:default": [],
    }),
    deps = [
        ":to_epic",
        "//intrinsic/geometry/internal/indexed_triangle_set_3",
        "//intrinsic/geometry/internal/surface_mesh_3:surface_mesh_3d",
        "//intrinsic_sdk/intrinsic/geometry/internal/mesh",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@cgal",
        "@coacd",
    ],
)

cc_library(
    name = "to_epic",
    hdrs = ["to_epic.h"],
    copts = ["-fexceptions"] + select({
        "@platforms//cpu:x86_32": ["-frounding-math"],
        "@platforms//cpu:x86_64": ["-frounding-math"],
        "//conditions:default": [],
    }),
    visibility = [
        "//intrinsic/geometry:__subpackages__",
        "//intrinsic/geometry/api:__pkg__",
        "//intrinsic/scene/service:__subpackages__",
        "//intrinsic/world/collision/util:__pkg__",
        "//intrinsic/world/util:__pkg__",
    ],
    deps = [
        "//intrinsic/geometry/internal/indexed_triangle_set_3",
        "//intrinsic/geometry/internal/kernel_3:exact_predicates_inexact_constructions_kernel",
        "//intrinsic/geometry/internal/surface_mesh_3:surface_mesh_3d",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/geometry/api:axis_aligned_bounding_box_3d",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "@abseil-cpp//absl/log:check",
        "@cgal",
    ],
)
