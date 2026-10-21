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

""" Intrinsic package geometry/processing/distance_3. """

load("//bazel:cc_macros.bzl", "cc_library")

cc_library(
    name = "hausdorff_optimization_state",
    hdrs = ["hausdorff_optimization_state.h"],
    visibility = [
        "//intrinsic/geometry/api:__pkg__",
    ],
    deps = [
        ":compute_bounded_hausdorff_distance",
        "//intrinsic/geometry/internal/surface_mesh_3:surface_mesh_3d",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_library(
    name = "hausdorff_stop_predicate",
    hdrs = ["hausdorff_stop_predicate.h"],
    visibility = [
        "//intrinsic/geometry/api:__pkg__",
    ],
    deps = [
        ":hausdorff_optimization_state",
        "//intrinsic/geometry/internal/surface_mesh_3:surface_mesh_3d",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_library(
    name = "compute_bounded_hausdorff_distance",
    srcs = ["compute_bounded_hausdorff_distance.cc"],
    hdrs = ["compute_bounded_hausdorff_distance.h"],
    copts = ["-fexceptions"] + select({
        "@platforms//cpu:x86_32": ["-frounding-math"],
        "@platforms//cpu:x86_64": ["-frounding-math"],
        "//conditions:default": [],
    }),
    visibility = ["//intrinsic/geometry/api:__pkg__"],
    deps = [
        "//intrinsic/geometry/internal/surface_mesh_3:surface_mesh_3d",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status:statusor",
        "@cgal",
    ],
)
