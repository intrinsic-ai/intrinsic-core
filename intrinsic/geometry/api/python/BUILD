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

load("//bazel:cc_macros.bzl", "pybind_extension")
load("//bazel:python.bzl", "py_library")

package(default_visibility = ["//intrinsic:intrinsic_internal"])

pybind_extension(
    name = "io",
    srcs = ["io.cc"],
    deps = [
        "//intrinsic/geometry/compatibility:io",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/geometry/api:apply_material_properties",
        "//intrinsic_sdk/intrinsic/geometry/api:exact_geometry",
        "//intrinsic_sdk/intrinsic/geometry/api:io",
        "//intrinsic_sdk/intrinsic/geometry/internal/mesh",
        "//intrinsic_sdk/intrinsic/math:proto_conversion",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:exact_geometry_cc_proto",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:transformed_geometry_cc_proto",
        "@intrinsic_apis//intrinsic/math/proto:matrix_cc_proto",
        "@pybind11_abseil//pybind11_abseil:absl_casters",
        "@pybind11_abseil//pybind11_abseil:status_casters",
        "@pybind11_protobuf//pybind11_protobuf:native_proto_caster",
    ],
)

py_library(
    name = "conversion_utils",
    srcs = ["conversion_utils.py"],
    deps = [
        ":io",
        "//intrinsic/storage/content_addressable_storage/python:client_helpers",
        "//intrinsic_sdk/intrinsic/geometry/proto:geometry_service_py_pb2",
        "//intrinsic_sdk/intrinsic/geometry/proto:geometry_service_py_pb2_grpc",
        "//intrinsic_sdk/intrinsic/math/python:data_types",
        "//intrinsic_sdk/intrinsic/math/python:proto_conversion",
        "@ai_intrinsic_sdks_pip_deps//numpy",
        "@com_github_grpc_grpc//src/python/grpcio/grpc:grpcio",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:exact_geometry_py_pb2",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:geometric_transform_py_pb2",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:geometry_py_pb2",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:inline_geometry_py_pb2",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:renderable_py_pb2",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:triangle_mesh_py_pb2",
        "@intrinsic_apis//intrinsic/storage/content_addressable_storage/proto:cas_service_py_pb2_grpc",
        "@intrinsic_apis//intrinsic/world/proto:geometry_component_py_pb2",
    ],
)
