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

load("//bazel:python.bzl", "py_library")

package(default_visibility = ["//visibility:public"])

py_library(
    name = "object_world_resources",
    srcs = ["object_world_resources.py"],
    visibility = ["//intrinsic:public_api_users"],
    deps = [
        ":object_world_ids",
        "//intrinsic_sdk/intrinsic/math/python:data_types",
        "//intrinsic_sdk/intrinsic/math/python:proto_conversion",
        "//intrinsic_sdk/intrinsic/util/grpc:error_handling",
        "//intrinsic_sdk/intrinsic/world/proto:object_world_service_py_pb2",
        "//intrinsic_sdk/intrinsic/world/proto:object_world_service_py_pb2_grpc",
        "//intrinsic_sdk/intrinsic/world/proto:object_world_updates_py_pb2",
        "//intrinsic_sdk/intrinsic/world/robot_payload/python:robot_payload",
        "@com_google_protobuf//:protobuf_python",
        "@intrinsic_apis//intrinsic/icon/proto:cart_space_py_pb2",
        "@intrinsic_apis//intrinsic/kinematics/types:joint_limits_py_pb2",
        "@intrinsic_apis//intrinsic/world/proto:object_world_refs_py_pb2",
    ],
)

py_library(
    name = "object_world_ids",
    srcs = ["object_world_ids.py"],
    visibility = ["//intrinsic:public_api_users"],
)

py_library(
    name = "object_world_client",
    srcs = ["object_world_client.py"],
    visibility = ["//intrinsic:public_api_users"],
    deps = [
        ":object_world_ids",
        ":object_world_resources",
        "//intrinsic_sdk/intrinsic/geometry/proto:geometry_service_py_pb2",
        "//intrinsic_sdk/intrinsic/geometry/proto:geometry_service_py_pb2_grpc",
        "//intrinsic_sdk/intrinsic/math/python:data_types",
        "//intrinsic_sdk/intrinsic/math/python:proto_conversion",
        "//intrinsic_sdk/intrinsic/util/grpc:error_handling",
        "//intrinsic_sdk/intrinsic/world/proto:object_world_service_py_pb2",
        "//intrinsic_sdk/intrinsic/world/proto:object_world_service_py_pb2_grpc",
        "//intrinsic_sdk/intrinsic/world/proto:object_world_updates_py_pb2",
        "//intrinsic_sdk/intrinsic/world/robot_payload/python:robot_payload",
        "@com_github_grpc_grpc//src/python/grpcio/grpc:grpcio",
        "@com_google_protobuf//:protobuf_python",
        "@intrinsic_apis//intrinsic/geometry/proto:geometry_storage_refs_py_pb2",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:geometry_storage_refs_py_pb2",
        "@intrinsic_apis//intrinsic/icon/equipment:icon_equipment_py_pb2",
        "@intrinsic_apis//intrinsic/icon/proto:cart_space_py_pb2",
        "@intrinsic_apis//intrinsic/kinematics/types:joint_limits_py_pb2",
        "@intrinsic_apis//intrinsic/resources/proto:resource_handle_py_pb2",
        "@intrinsic_apis//intrinsic/scene/proto/v1:scene_object_py_pb2",
        "@intrinsic_apis//intrinsic/world/proto:geometry_component_py_pb2",
        "@intrinsic_apis//intrinsic/world/proto:object_world_refs_py_pb2",
    ],
)
