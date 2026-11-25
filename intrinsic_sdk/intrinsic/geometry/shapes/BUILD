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

""" Intrinsic package geometry/shapes. """

load("//bazel:cc_macros.bzl", "cc_library")

package(default_visibility = ["//visibility:public"])

licenses(["unencumbered"])

cc_library(
    name = "geometry_shape",
    hdrs = [
        "shape_base.h",
        "shape_ptrs.h",
    ],
    deps = [
        "//intrinsic_sdk/intrinsic/geometry/api:affine_transform_of",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "@abseil-cpp//absl/log:check",
    ],
)

cc_library(
    name = "shapes",
    hdrs = [
        "axes.h",
        "box.h",
        "capsule.h",
        "convex_hull.h",
        "cylinder.h",
        "discs.h",
        "ellipsoid.h",
        "frustum.h",
        "line_strip.h",
        "mesh_file.h",
        "point_cloud.h",
        "shapes.h",
        "sphere.h",
        "spheres.h",
        "triangle_mesh.h",
    ],
    deps = [
        ":geometry_shape",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
    ],
)
