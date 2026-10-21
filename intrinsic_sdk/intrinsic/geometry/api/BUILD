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
    name = "affine_transform_of",
    hdrs = ["affine_transform_of.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:pose3",
    ],
)

cc_library(
    name = "affine_transform_of_geometry",
    hdrs = ["affine_transform_of_geometry.h"],
    deps = [
        ":affine_transform_of",
        ":geometry",
    ],
)

cc_library(
    name = "apply_material_properties",
    srcs = ["apply_material_properties.cc"],
    hdrs = ["apply_material_properties.h"],
    deps = [
        ":material",
        ":renderable",
        "//intrinsic_sdk/intrinsic/geometry/internal/util:gltf_helpers",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@assimp",
        "@com_google_googleapis//google/type:color_cc_proto",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:material_cc_proto",
        "@tinygltf",
    ],
)

cc_library(
    name = "material_properties",
    srcs = ["material_properties.cc"],
    hdrs = ["material_properties.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/eigenmath",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:material_cc_proto",
    ],
)

cc_library(
    name = "apply_transform",
    srcs = ["apply_transform.cc"],
    hdrs = ["apply_transform.h"],
    copts = [
        "-fexceptions",
        "-frounding-math",
    ],
    deps = [
        ":exact_geometry",
        ":geometry",
        ":geometry_options",
        ":renderable",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/geometry/internal/mesh",
        "//intrinsic_sdk/intrinsic/geometry/internal/util:export_as_gltf",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/util/object_store",
        "//intrinsic_sdk/intrinsic/util/object_store:object_ref",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_library(
    name = "triangle",
    hdrs = ["triangle.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/eigenmath",
    ],
)

cc_library(
    name = "axis_aligned_bounding_box_3d",
    srcs = ["axis_aligned_bounding_box_3d.cc"],
    hdrs = ["axis_aligned_bounding_box_3d.h"],
    deps = [
        ":triangle",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/marshal:riegeli_proto_coder",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@intrinsic_apis//intrinsic/geometry/proto:axis_aligned_bounding_box_cc_proto",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:axis_aligned_bounding_box_cc_proto",
        "@intrinsic_apis//intrinsic/math/proto:vector3_cc_proto",
    ],
)

cc_library(
    name = "compute_axis_aligned_bounding_box_3d",
    srcs = ["compute_axis_aligned_bounding_box_3d.cc"],
    hdrs = ["compute_axis_aligned_bounding_box_3d.h"],
    copts = [
        "-fexceptions",
        "-frounding-math",
    ],
    deps = [
        ":apply_transform",
        ":axis_aligned_bounding_box_3d",
        ":exact_geometry",
        ":geometry",
        "//intrinsic_sdk/intrinsic/geometry/internal/mesh",
        "//intrinsic_sdk/intrinsic/geometry/internal/point_cloud:get_bounding_box_from_point_cloud",
        "//intrinsic_sdk/intrinsic/geometry/shapes",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/util/object_store:object_ref",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_library(
    name = "exact_geometry",
    srcs = ["exact_geometry.cc"],
    hdrs = ["exact_geometry.h"],
    deps = [
        ":affine_transform_of",
        ":geometry_options",
        "//intrinsic_sdk/intrinsic/geometry/internal/mesh",
        "//intrinsic_sdk/intrinsic/geometry/internal/mesh:mesh_from_primitives",
        "//intrinsic_sdk/intrinsic/geometry/internal/mesh:remove_duplicate_vertices_from_mesh",
        "//intrinsic_sdk/intrinsic/geometry/internal/point_cloud:point_cloud_riegeli_coder",
        "//intrinsic_sdk/intrinsic/geometry/shapes",
        "//intrinsic_sdk/intrinsic/geometry/shapes:geometry_shape",
        "//intrinsic_sdk/intrinsic/util:macros",
        "//intrinsic_sdk/intrinsic/util/object_store",
        "//intrinsic_sdk/intrinsic/util/object_store:object_ref",
        "//intrinsic_sdk/intrinsic/util/status:status_builder",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@intrinsic_apis//intrinsic/geometry/proto:mesh_cc_proto",
    ],
)

cc_library(
    name = "geometry",
    srcs = ["geometry.cc"],
    hdrs = ["geometry.h"],
    deps = [
        ":exact_geometry",
        ":renderable",
        "//intrinsic_sdk/intrinsic/util/object_store:object_ref",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log:die_if_null",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:material_cc_proto",
    ],
)

cc_library(
    name = "geometry_fingerprint",
    srcs = ["geometry_fingerprint.cc"],
    hdrs = ["geometry_fingerprint.h"],
    deps = [
        ":exact_geometry",
        ":geometry",
        ":io",
        ":renderable",
        "//intrinsic_sdk/intrinsic/util:hash",
        "//intrinsic_sdk/intrinsic/util:macros",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
    ],
)

cc_library(
    name = "geometry_options",
    hdrs = ["geometry_options.h"],
)

cc_library(
    name = "io",
    srcs = ["io.cc"],
    hdrs = ["io.h"],
    deps = [
        ":affine_transform_of_geometry",
        ":exact_geometry",
        ":geometry",
        ":geometry_options",
        ":renderable",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/eigenmath:rotation_utils",
        "//intrinsic_sdk/intrinsic/geometry/internal/mesh",
        "//intrinsic_sdk/intrinsic/geometry/internal/point_cloud:point_cloud_riegeli_coder",
        "//intrinsic_sdk/intrinsic/geometry/shapes",
        "//intrinsic_sdk/intrinsic/geometry/shapes:geometry_shape",
        "//intrinsic_sdk/intrinsic/geometry/storage:geometry_deserializer",
        "//intrinsic_sdk/intrinsic/geometry/storage:geometry_serializer",
        "//intrinsic_sdk/intrinsic/math:proto_conversion",
        "//intrinsic_sdk/intrinsic/util/object_store",
        "//intrinsic_sdk/intrinsic/util/object_store:object_ref",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:exact_geometry_cc_proto",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:geometric_transform_cc_proto",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:geometry_cc_proto",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:geometry_options_cc_proto",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:geometry_storage_refs_cc_proto",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:inline_geometry_cc_proto",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:point_cloud_cc_proto",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:primitive_shape_cc_proto",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:primitives_cc_proto",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:renderable_cc_proto",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:transformed_geometry_cc_proto",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:transformed_primitive_shape_cc_proto",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:transformed_primitive_shape_set_cc_proto",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:triangle_mesh_cc_proto",
        "@intrinsic_apis//intrinsic/math/proto:vector3_cc_proto",
    ],
)

cc_library(
    name = "material",
    hdrs = ["material.h"],
)

cc_library(
    name = "renderable",
    srcs = ["renderable.cc"],
    hdrs = ["renderable.h"],
)

cc_library(
    name = "renderable_generation",
    srcs = ["renderable_generation.cc"],
    hdrs = ["renderable_generation.h"],
    deps = [
        ":apply_material_properties",
        ":geometry",
        ":material_properties",
        ":renderable",
        "//intrinsic_sdk/intrinsic/geometry/internal/util:export_as_gltf",
        "//intrinsic_sdk/intrinsic/util/status:ret_check",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_library(
    name = "shapes",
    srcs = ["shapes.cc"],
    hdrs = ["shapes.h"],
    deps = [
        ":affine_transform_of_geometry",
        ":apply_material_properties",
        ":exact_geometry",
        ":geometry",
        ":geometry_options",
        ":material",
        ":renderable",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/geometry/internal/mesh:load_mesh_file_with_scale",
        "//intrinsic_sdk/intrinsic/geometry/internal/mesh:memoized_mesh_from_triangle_mesh",
        "//intrinsic_sdk/intrinsic/geometry/internal/mesh/io:load_ai_scene_from_file",
        "//intrinsic_sdk/intrinsic/geometry/internal/point_cloud:pts_to_point_cloud",
        "//intrinsic_sdk/intrinsic/geometry/internal/util:export_as_gltf",
        "//intrinsic_sdk/intrinsic/geometry/shapes",
        "//intrinsic_sdk/intrinsic/geometry/shapes:geometry_shape",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@assimp",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:material_cc_proto",
        "@or_tools//ortools/base:file",
        "@or_tools//ortools/base:path",
    ],
)

cc_library(
    name = "validate_mesh",
    srcs = ["validate_mesh.cc"],
    hdrs = ["validate_mesh.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/geometry/internal/mesh/io:load_ai_scene_from_buffer",
        "//intrinsic_sdk/intrinsic/geometry/internal/mesh/io:load_ai_scene_from_file",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@assimp",
        "@or_tools//ortools/base:file",
        "@or_tools//ortools/base:path",
    ],
)

cc_library(
    name = "shape_factory",
    srcs = ["shape_factory.cc"],
    hdrs = ["shape_factory.h"],
    deps = [
        ":affine_transform_of_geometry",
        ":exact_geometry",
        ":geometry",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/geometry/shapes",
        "//intrinsic_sdk/intrinsic/math:pose3",
    ],
)
