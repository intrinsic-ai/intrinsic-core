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

""" Intrinsic package geometry/api. """

load("//bazel:cc_macros.bzl", "cc_library")

package(default_visibility = ["//intrinsic:intrinsic_internal"])

cc_library(
    name = "compute_convex_hull",
    srcs = ["compute_convex_hull.cc"],
    hdrs = ["compute_convex_hull.h"],
    copts = ["-fexceptions"] + select({
        "@platforms//cpu:x86_32": ["-frounding-math"],
        "@platforms//cpu:x86_64": ["-frounding-math"],
        "//conditions:default": [],
    }),
    deps = [
        "//intrinsic/geometry/internal/conversion_utils:extract_all_point_3d",
        "//intrinsic/geometry/internal/conversion_utils:mesh_conversion",
        "//intrinsic/geometry/internal/kernel_3:exact_predicates_inexact_constructions_kernel",
        "//intrinsic/geometry/internal/surface_mesh_3:surface_mesh_3d",
        "//intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/geometry/api:exact_geometry",
        "//intrinsic_sdk/intrinsic/geometry/api:geometry",
        "//intrinsic_sdk/intrinsic/geometry/api:geometry_options",
        "//intrinsic_sdk/intrinsic/geometry/internal/mesh",
        "//intrinsic_sdk/intrinsic/util/object_store:memoize",
        "//intrinsic_sdk/intrinsic/util/object_store:object_ref",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@cgal",
    ],
)

cc_library(
    name = "compute_coacd",
    srcs = ["compute_coacd.cc"],
    hdrs = ["compute_coacd.h"],
    copts = ["-fexceptions"] + select({
        "@platforms//cpu:x86_32": ["-frounding-math"],
        "@platforms//cpu:x86_64": ["-frounding-math"],
        "//conditions:default": [],
    }),
    visibility = ["//intrinsic/geometry/processing/processors:__pkg__"],
    deps = [
        "//intrinsic/geometry/internal/conversion_utils:mesh_conversion",
        "//intrinsic/geometry/internal/indexed_triangle_set_3",
        "//intrinsic/geometry/internal/surface_mesh_3:surface_mesh_3d",
        "//intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/geometry/api:exact_geometry",
        "//intrinsic_sdk/intrinsic/geometry/api:geometry",
        "//intrinsic_sdk/intrinsic/geometry/internal/mesh",
        "//intrinsic_sdk/intrinsic/util/object_store",
        "//intrinsic_sdk/intrinsic/util/object_store:memoize",
        "//intrinsic_sdk/intrinsic/util/object_store:object_ref",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@cgal",
        "@coacd",
    ],
)

cc_library(
    name = "compute_mesh_stats",
    srcs = ["compute_mesh_stats.cc"],
    hdrs = ["compute_mesh_stats.h"],
    copts = ["-fexceptions"] + select({
        "@platforms//cpu:x86_32": ["-frounding-math"],
        "@platforms//cpu:x86_64": ["-frounding-math"],
        "//conditions:default": [],
    }),
    visibility = [
        "//intrinsic/scene/service:__pkg__",
    ],
    deps = [
        ":mesh_stats",
        "//intrinsic/geometry/internal/conversion_utils:mesh_conversion",
        "//intrinsic/geometry/internal/kernel_3:exact_predicates_inexact_constructions_kernel",
        "//intrinsic/geometry/internal/surface_mesh_3:surface_mesh_3d",
        "//intrinsic/geometry/internal/surface_mesh_3:unary_predicates",
        "//intrinsic/geometry/internal/util:cgal_utils",
        "//intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/geometry/api:axis_aligned_bounding_box_3d",
        "//intrinsic_sdk/intrinsic/geometry/api:exact_geometry",
        "//intrinsic_sdk/intrinsic/geometry/api:geometry",
        "//intrinsic_sdk/intrinsic/geometry/internal/mesh",
        "//intrinsic_sdk/intrinsic/geometry/shapes",
        "//intrinsic_sdk/intrinsic/util/object_store:memoize",
        "//intrinsic_sdk/intrinsic/util/object_store:object_ref",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@cgal",
    ],
)

cc_library(
    name = "mesh_stats",
    srcs = ["mesh_stats.cc"],
    hdrs = ["mesh_stats.h"],
    copts = ["-fexceptions"] + select({
        "@platforms//cpu:x86_32": ["-frounding-math"],
        "@platforms//cpu:x86_64": ["-frounding-math"],
        "//conditions:default": [],
    }),
    deps = [
        "//intrinsic/geometry/internal/kernel_3:exact_predicates_inexact_constructions_kernel",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/geometry/api:axis_aligned_bounding_box_3d",
        "//intrinsic_sdk/intrinsic/marshal:riegeli_proto_coder",
        "@abseil-cpp//absl/status:statusor",
        "@com_gitlab_libeigen_eigen//:eigen",
        "@intrinsic_apis//intrinsic/math/proto:point_cc_proto",
        "@intrinsic_apis//intrinsic/math/proto:vector3_cc_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:geometry_stats_cc_proto",
    ],
)

cc_library(
    name = "construct_alpha_hull",
    srcs = ["construct_alpha_hull.cc"],
    hdrs = ["construct_alpha_hull.h"],
    copts = ["-fexceptions"] + select({
        "@platforms//cpu:x86_32": ["-frounding-math"],
        "@platforms//cpu:x86_64": ["-frounding-math"],
        "//conditions:default": [],
    }),
    deps = [
        "//intrinsic/geometry/internal/alpha_hull_3",
        "//intrinsic/geometry/internal/conversion_utils:mesh_conversion",
        "//intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/geometry/api:exact_geometry",
        "//intrinsic_sdk/intrinsic/geometry/api:geometry",
        "//intrinsic_sdk/intrinsic/geometry/api:geometry_options",
        "//intrinsic_sdk/intrinsic/geometry/internal/mesh",
        "//intrinsic_sdk/intrinsic/util/object_store:memoize",
        "//intrinsic_sdk/intrinsic/util/object_store:object_ref",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_library(
    name = "construct_simplified_mesh",
    srcs = ["construct_simplified_mesh.cc"],
    hdrs = ["construct_simplified_mesh.h"],
    copts = ["-fexceptions"] + select({
        "@platforms//cpu:x86_32": ["-frounding-math"],
        "@platforms//cpu:x86_64": ["-frounding-math"],
        "//conditions:default": [],
    }),
    deps = [
        "//intrinsic/geometry/internal/conversion_utils:mesh_conversion",
        "//intrinsic/geometry/internal/distance_3:hausdorff_optimization_state",
        "//intrinsic/geometry/internal/distance_3:hausdorff_stop_predicate",
        "//intrinsic/geometry/internal/surface_mesh_3:surface_mesh_3d",
        "//intrinsic/geometry/internal/util:timeout_predicate",
        "//intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/geometry/api:exact_geometry",
        "//intrinsic_sdk/intrinsic/geometry/api:geometry",
        "//intrinsic_sdk/intrinsic/geometry/internal/mesh",
        "//intrinsic_sdk/intrinsic/util/object_store:memoize",
        "//intrinsic_sdk/intrinsic/util/object_store:object_ref",
        "//intrinsic_sdk/intrinsic/util/thread:stop_token",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:str_format",
        "@cgal",
    ],
)

cc_library(
    name = "construct_isotropic_remeshing",
    srcs = ["construct_isotropic_remeshing.cc"],
    hdrs = ["construct_isotropic_remeshing.h"],
    copts = ["-fexceptions"] + select({
        "@platforms//cpu:x86_32": ["-frounding-math"],
        "@platforms//cpu:x86_64": ["-frounding-math"],
        "//conditions:default": [],
    }),
    deps = [
        "//intrinsic/geometry/internal/conversion_utils:mesh_conversion",
        "//intrinsic/geometry/internal/surface_mesh_3:surface_mesh_3d",
        "//intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/geometry/api:exact_geometry",
        "//intrinsic_sdk/intrinsic/geometry/api:geometry",
        "//intrinsic_sdk/intrinsic/geometry/internal/mesh",
        "//intrinsic_sdk/intrinsic/util/object_store:memoize",
        "//intrinsic_sdk/intrinsic/util/object_store:object_ref",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:str_format",
        "@cgal",
    ],
)

cc_library(
    name = "distance_stats",
    hdrs = ["distance_stats.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/world:entity_id",
        "//intrinsic_sdk/intrinsic/world/hashing",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/time",
    ],
)

cc_library(
    name = "file_io",
    srcs = ["file_io.cc"],
    hdrs = ["file_io.h"],
    deps = [
        "//intrinsic/geometry/internal/legacy/point_cloud:ai_scene_to_point_cloud",
        "//intrinsic/geometry/internal/legacy/point_cloud:load_point_cloud_from_buffer",
        "//intrinsic/util/status:status_builder",
        "//intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/geometry/api:exact_geometry",
        "//intrinsic_sdk/intrinsic/geometry/api:geometry",
        "//intrinsic_sdk/intrinsic/geometry/api:renderable",
        "//intrinsic_sdk/intrinsic/geometry/internal/mesh",
        "//intrinsic_sdk/intrinsic/geometry/internal/mesh/io:ai_scene_to_mesh",
        "//intrinsic_sdk/intrinsic/geometry/internal/mesh/io:load_ai_scene_from_buffer",
        "//intrinsic_sdk/intrinsic/geometry/internal/mesh/io:restrict_importer",
        "//intrinsic_sdk/intrinsic/geometry/internal/point_cloud:pts_to_point_cloud",
        "//intrinsic_sdk/intrinsic/geometry/internal/util:export_as_gltf",
        "//intrinsic_sdk/intrinsic/geometry/shapes",
        "//intrinsic_sdk/intrinsic/util/object_store:object_ref",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@assimp",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:exact_geometry_cc_proto",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:geometric_transform_cc_proto",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:geometry_cc_proto",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:geometry_options_cc_proto",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:geometry_storage_refs_cc_proto",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:inline_geometry_cc_proto",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:octree_cc_proto",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:octree_wrapping_cc_proto",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:point_cloud_cc_proto",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:primitive_shape_cc_proto",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:primitives_cc_proto",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:renderable_cc_proto",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:transformed_geometry_cc_proto",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:transformed_primitive_shape_cc_proto",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:transformed_primitive_shape_set_cc_proto",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:triangle_mesh_cc_proto",
        "@intrinsic_apis//intrinsic/math/proto:vector3_cc_proto",
        "@or_tools//ortools/base:file",
        "@or_tools//ortools/base:path",
    ],
)

cc_library(
    name = "fuse_geometries",
    srcs = ["fuse_geometries.cc"],
    hdrs = ["fuse_geometries.h"],
    deps = [
        "//intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/geometry/api:affine_transform_of",
        "//intrinsic_sdk/intrinsic/geometry/api:affine_transform_of_geometry",
        "//intrinsic_sdk/intrinsic/geometry/api:exact_geometry",
        "//intrinsic_sdk/intrinsic/geometry/api:geometry",
        "//intrinsic_sdk/intrinsic/geometry/api:geometry_options",
        "//intrinsic_sdk/intrinsic/geometry/internal/mesh",
        "//intrinsic_sdk/intrinsic/geometry/internal/mesh:concatenate_meshes",
        "//intrinsic_sdk/intrinsic/util/object_store",
        "//intrinsic_sdk/intrinsic/util/object_store:object_ref",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_library(
    name = "geometric_transform",
    srcs = ["geometric_transform.cc"],
    hdrs = ["geometric_transform.h"],
    deps = [
        "//intrinsic/util:eigen",
        "//intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/eigenmath:rotation_utils",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:proto_conversion",
        "@abseil-cpp//absl/status:statusor",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:geometric_transform_cc_proto",
    ],
)
