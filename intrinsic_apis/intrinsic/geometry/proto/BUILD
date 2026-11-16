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

load("@com_google_protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("@com_google_protobuf//bazel:proto_library.bzl", "proto_library")
load("@com_google_protobuf//bazel:py_proto_library.bzl", "py_proto_library")
load("@io_bazel_rules_go//proto:def.bzl", "go_proto_library")

package(default_visibility = ["//visibility:public"])

proto_library(
    name = "axis_aligned_bounding_box_proto",
    srcs = ["axis_aligned_bounding_box.proto"],
    deps = [
        "@intrinsic_apis//intrinsic/math/proto:vector2_proto",
        "@intrinsic_apis//intrinsic/math/proto:vector3_proto",
    ],
)

cc_proto_library(
    name = "axis_aligned_bounding_box_cc_proto",
    deps = [":axis_aligned_bounding_box_proto"],
)

go_proto_library(
    name = "axis_aligned_bounding_box_go_proto",
    importpath = "intrinsic/geometry/proto/axis_aligned_bounding_box_go_proto",
    protos = [":axis_aligned_bounding_box_proto"],
    deps = [
        "@intrinsic_apis//intrinsic/math/proto:vector2_go_proto",
        "@intrinsic_apis//intrinsic/math/proto:vector3_go_proto",
    ],
)

py_proto_library(
    name = "axis_aligned_bounding_box_py_pb2",
    deps = [":axis_aligned_bounding_box_proto"],
)

proto_library(
    name = "mesh_proto",
    srcs = ["mesh.proto"],
)

cc_proto_library(
    name = "mesh_cc_proto",
    deps = [":mesh_proto"],
)

go_proto_library(
    name = "mesh_go_proto",
    importpath = "intrinsic/geometry/proto/mesh_go_proto",
    protos = [":mesh_proto"],
)

py_proto_library(
    name = "mesh_py_pb2",
    deps = [":mesh_proto"],
)

proto_library(
    name = "point_cloud_proto",
    srcs = ["point_cloud.proto"],
)

cc_proto_library(
    name = "point_cloud_cc_proto",
    deps = [":point_cloud_proto"],
)

go_proto_library(
    name = "point_cloud_go_proto",
    importpath = "intrinsic/geometry/proto/point_cloud_go_proto",
    protos = [":point_cloud_proto"],
)

py_proto_library(
    name = "point_cloud_py_pb2",
    deps = [":point_cloud_proto"],
)

proto_library(
    name = "primitives_proto",
    srcs = ["primitives.proto"],
    deps = ["@intrinsic_apis//intrinsic/math/proto:vector3_proto"],
)

cc_proto_library(
    name = "primitives_cc_proto",
    deps = [":primitives_proto"],
)

go_proto_library(
    name = "primitives_go_proto",
    importpath = "intrinsic/geometry/proto/primitives_go_proto",
    protos = [":primitives_proto"],
    deps = [
        "@intrinsic_apis//intrinsic/math/proto:vector3_go_proto",
    ],
)

py_proto_library(
    name = "primitives_py_pb2",
    deps = [":primitives_proto"],
)

proto_library(
    name = "triangle_mesh_proto",
    srcs = ["triangle_mesh.proto"],
)

cc_proto_library(
    name = "triangle_mesh_cc_proto",
    deps = [":triangle_mesh_proto"],
)

go_proto_library(
    name = "triangle_mesh_go_proto",
    importpath = "intrinsic/geometry/proto/triangle_mesh_go_proto",
    protos = [":triangle_mesh_proto"],
)

py_proto_library(
    name = "triangle_mesh_py_pb2",
    deps = [":triangle_mesh_proto"],
)

proto_library(
    name = "lazy_exact_geometry_proto",
    srcs = ["lazy_exact_geometry.proto"],
    deps = [
        ":mesh_proto",
        ":point_cloud_proto",
        ":primitives_proto",
        ":triangle_mesh_proto",
    ],
)

cc_proto_library(
    name = "lazy_exact_geometry_cc_proto",
    deps = [":lazy_exact_geometry_proto"],
)

go_proto_library(
    name = "lazy_exact_geometry_go_proto",
    importpath = "intrinsic/geometry/proto/lazy_exact_geometry_go_proto",
    protos = [":lazy_exact_geometry_proto"],
    deps = [
        ":mesh_go_proto",
        ":point_cloud_go_proto",
        ":primitives_go_proto",
        ":triangle_mesh_go_proto",
    ],
)

py_proto_library(
    name = "lazy_exact_geometry_py_pb2",
    deps = [":lazy_exact_geometry_proto"],
)

proto_library(
    name = "renderable_proto",
    srcs = ["renderable.proto"],
)

cc_proto_library(
    name = "renderable_cc_proto",
    deps = [":renderable_proto"],
)

go_proto_library(
    name = "renderable_go_proto",
    importpath = "intrinsic/geometry/proto/renderable_go_proto",
    protos = [":renderable_proto"],
)

py_proto_library(
    name = "renderable_py_pb2",
    deps = [":renderable_proto"],
)

proto_library(
    name = "geometry_proto",
    srcs = ["geometry.proto"],
    deps = [
        ":lazy_exact_geometry_proto",
        ":renderable_proto",
    ],
)

cc_proto_library(
    name = "geometry_cc_proto",
    deps = [":geometry_proto"],
)

go_proto_library(
    name = "geometry_go_proto",
    importpath = "intrinsic/geometry/proto/geometry_go_proto",
    protos = [":geometry_proto"],
    deps = [
        ":lazy_exact_geometry_go_proto",
        ":renderable_go_proto",
    ],
)

py_proto_library(
    name = "geometry_py_pb2",
    deps = [":geometry_proto"],
)

proto_library(
    name = "geometry_storage_refs_proto",
    srcs = ["geometry_storage_refs.proto"],
)

cc_proto_library(
    name = "geometry_storage_refs_cc_proto",
    deps = [":geometry_storage_refs_proto"],
)

go_proto_library(
    name = "geometry_storage_refs_go_proto",
    importpath = "intrinsic/geometry/proto/geometry_storage_refs_go_proto",
    protos = [":geometry_storage_refs_proto"],
)

py_proto_library(
    name = "geometry_storage_refs_py_pb2",
    deps = [":geometry_storage_refs_proto"],
)

proto_library(
    name = "geometry_service_types_proto",
    srcs = ["geometry_service_types.proto"],
    deps = [
        ":geometry_proto",
        ":geometry_storage_refs_proto",
        ":lazy_exact_geometry_proto",
        ":renderable_proto",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:geometry_storage_refs_proto",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:inline_geometry_proto",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:renderable_proto",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:transformed_primitive_shape_set_proto",
    ],
)

cc_proto_library(
    name = "geometry_service_types_cc_proto",
    deps = [":geometry_service_types_proto"],
)

go_proto_library(
    name = "geometry_service_types_go_proto",
    importpath = "intrinsic/geometry/proto/geometry_service_types_go_proto",
    protos = [":geometry_service_types_proto"],
    deps = [
        ":geometry_go_proto",
        ":geometry_storage_refs_go_proto",
        ":lazy_exact_geometry_go_proto",
        ":renderable_go_proto",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:geometry_storage_refs_go_proto",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:inline_geometry_go_proto",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:renderable_go_proto",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:transformed_primitive_shape_set_go_proto",
    ],
)

py_proto_library(
    name = "geometry_service_types_py_pb2",
    deps = [":geometry_service_types_proto"],
)

proto_library(
    name = "octree_proto",
    srcs = ["octree.proto"],
    deps = [":axis_aligned_bounding_box_proto"],
)

cc_proto_library(
    name = "octree_cc_proto",
    deps = [":octree_proto"],
)

go_proto_library(
    name = "octree_go_proto",
    importpath = "intrinsic/geometry/proto/octree_go_proto",
    protos = [":octree_proto"],
    deps = [
        ":axis_aligned_bounding_box_go_proto",
    ],
)

py_proto_library(
    name = "octree_py_pb2",
    deps = [":octree_proto"],
)

proto_library(
    name = "mesh_octree_wrapping_proto",
    srcs = ["mesh_octree_wrapping.proto"],
    deps = [":octree_proto"],
)

cc_proto_library(
    name = "mesh_octree_wrapping_cc_proto",
    deps = [":mesh_octree_wrapping_proto"],
)

go_proto_library(
    name = "mesh_octree_wrapping_go_proto",
    importpath = "intrinsic/geometry/proto/mesh_octree_wrapping_go_proto",
    protos = [":mesh_octree_wrapping_proto"],
    deps = [
        ":octree_go_proto",
    ],
)

py_proto_library(
    name = "mesh_octree_wrapping_py_pb2",
    deps = [":mesh_octree_wrapping_proto"],
)

proto_library(
    name = "oriented_bounding_box_proto",
    srcs = ["oriented_bounding_box.proto"],
    deps = [
        ":axis_aligned_bounding_box_proto",
        "@intrinsic_apis//intrinsic/math/proto:pose_proto",
        "@intrinsic_apis//intrinsic/math/proto:vector3_proto",
    ],
)

cc_proto_library(
    name = "oriented_bounding_box_cc_proto",
    deps = [":oriented_bounding_box_proto"],
)

go_proto_library(
    name = "oriented_bounding_box_go_proto",
    importpath = "intrinsic/geometry/proto/oriented_bounding_box_go_proto",
    protos = [":oriented_bounding_box_proto"],
    deps = [
        ":axis_aligned_bounding_box_go_proto",
        "@intrinsic_apis//intrinsic/math/proto:point_go_proto",
        "@intrinsic_apis//intrinsic/math/proto:pose_go_proto",
        "@intrinsic_apis//intrinsic/math/proto:quaternion_go_proto",
        "@intrinsic_apis//intrinsic/math/proto:vector2_go_proto",
        "@intrinsic_apis//intrinsic/math/proto:vector3_go_proto",
    ],
)

py_proto_library(
    name = "oriented_bounding_box_py_pb2",
    deps = [":oriented_bounding_box_proto"],
)

proto_library(
    name = "oriented_bounding_box_with_surfaces_proto",
    srcs = ["oriented_bounding_box_with_surfaces.proto"],
    deps = [
        "@intrinsic_apis//intrinsic/math/proto:pose_proto",
        "@intrinsic_apis//intrinsic/math/proto:vector3_proto",
    ],
)

cc_proto_library(
    name = "oriented_bounding_box_with_surfaces_cc_proto",
    deps = [":oriented_bounding_box_with_surfaces_proto"],
)

go_proto_library(
    name = "oriented_bounding_box_with_surfaces_go_proto",
    importpath = "intrinsic/geometry/proto/oriented_bounding_box_with_surfaces_go_proto",
    protos = [":oriented_bounding_box_with_surfaces_proto"],
    deps = [
        "@intrinsic_apis//intrinsic/math/proto:point_go_proto",
        "@intrinsic_apis//intrinsic/math/proto:pose_go_proto",
        "@intrinsic_apis//intrinsic/math/proto:quaternion_go_proto",
        "@intrinsic_apis//intrinsic/math/proto:vector3_go_proto",
    ],
)

py_proto_library(
    name = "oriented_bounding_box_with_surfaces_py_pb2",
    deps = [":oriented_bounding_box_with_surfaces_proto"],
)

proto_library(
    name = "transformed_geometry_storage_refs_proto",
    srcs = ["transformed_geometry_storage_refs.proto"],
    deps = [
        ":geometry_storage_refs_proto",
        "@intrinsic_apis//intrinsic/math/proto:matrix_proto",
    ],
)

cc_proto_library(
    name = "transformed_geometry_storage_refs_cc_proto",
    deps = [":transformed_geometry_storage_refs_proto"],
)

go_proto_library(
    name = "transformed_geometry_storage_refs_go_proto",
    importpath = "intrinsic/geometry/proto/transformed_geometry_storage_refs_go_proto",
    protos = [":transformed_geometry_storage_refs_proto"],
    deps = [
        ":geometry_storage_refs_go_proto",
        "@intrinsic_apis//intrinsic/math/proto:matrix_go_proto",
    ],
)

py_proto_library(
    name = "transformed_geometry_storage_refs_py_pb2",
    deps = [":transformed_geometry_storage_refs_proto"],
)
