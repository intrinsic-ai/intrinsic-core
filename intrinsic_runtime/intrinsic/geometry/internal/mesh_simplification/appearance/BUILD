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

"""Appearance-preserving simplification of renderable GLBs."""

load("//bazel:cc_macros.bzl", "cc_library")

package(default_visibility = ["//visibility:private"])

cc_library(
    name = "glb_appearance_simplifier",
    srcs = ["glb_appearance_simplifier.cc"],
    hdrs = ["glb_appearance_simplifier.h"],
    visibility = [
        "//intrinsic/geometry:__subpackages__",
        "//intrinsic_runtime/intrinsic/geometry:__subpackages__",
    ],
    deps = [
        ":remapped_triangles",
        ":uv_seams",
        ":weld_vertices",
        "//intrinsic_sdk/intrinsic/geometry/gltf:gltf_accessor",
        "//intrinsic_sdk/intrinsic/geometry/gltf:gltf_buffer_reader",
        "//intrinsic_sdk/intrinsic/geometry/gltf:gltf_buffer_writer",
        "//intrinsic_sdk/intrinsic/geometry/gltf:gltf_constants",
        "//intrinsic_sdk/intrinsic/geometry/gltf:gltf_io",
        "//intrinsic_sdk/intrinsic/geometry/gltf:gltf_validation",
        "//intrinsic_sdk/intrinsic/geometry/gltf:unpack_renderable",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/util/thread:stop_token",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/types:span",
        "@meshoptimizer",
        "@tinygltf",
    ],
)

cc_library(
    name = "remapped_triangles",
    hdrs = ["remapped_triangles.h"],
    deps = ["@abseil-cpp//absl/types:span"],
)

cc_library(
    name = "weld_vertices",
    srcs = ["weld_vertices.cc"],
    hdrs = ["weld_vertices.h"],
    deps = [
        ":remapped_triangles",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/types:span",
        "@meshoptimizer",
    ],
)

cc_library(
    name = "uv_seams",
    srcs = ["uv_seams.cc"],
    hdrs = ["uv_seams.h"],
    deps = [
        ":remapped_triangles",
        "@meshoptimizer",
    ],
)
