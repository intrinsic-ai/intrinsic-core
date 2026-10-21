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
    name = "_camera_utils",
    srcs = ["_camera_utils.py"],
    deps = [
        "//intrinsic_sdk/intrinsic/skills/python:proto_utils",
        "@ai_intrinsic_sdks_pip_deps//numpy",
        "@com_github_grpc_grpc//src/python/grpcio/grpc:grpcio",
        "@com_google_absl_py//absl/logging",
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_config_py_pb2",
        "@intrinsic_apis//intrinsic/perception/proto/v1:dimensions_py_pb2",
        "@intrinsic_apis//intrinsic/perception/proto/v1:distortion_params_py_pb2",
        "@intrinsic_apis//intrinsic/perception/proto/v1:intrinsic_params_py_pb2",
        "@intrinsic_apis//intrinsic/resources/proto:resource_handle_py_pb2",
    ],
)

py_library(
    name = "data_classes",
    srcs = ["data_classes.py"],
    deps = [
        ":_camera_utils",
        "//intrinsic_perception/intrinsic/perception/client/v1/python:image_utils",
        "//intrinsic_sdk/intrinsic/math/python:pose3",
        "//intrinsic_sdk/intrinsic/math/python:proto_conversion",
        "@ai_intrinsic_sdks_pip_deps//numpy",
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_config_py_pb2",
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_params_py_pb2",
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_service_py_pb2",
        "@intrinsic_apis//intrinsic/perception/proto/v1:capture_result_py_pb2",
        "@intrinsic_apis//intrinsic/perception/proto/v1:sensor_config_py_pb2",
        "@intrinsic_apis//intrinsic/perception/proto/v1:sensor_image_py_pb2",
    ],
)

py_library(
    name = "camera_client",
    srcs = ["camera_client.py"],
    deps = [
        "//intrinsic_sdk/intrinsic/assets:interface_utils_py",
        "//intrinsic_sdk/intrinsic/assets/dependencies:utils_py",
        "//intrinsic_sdk/intrinsic/util/grpc:connection",
        "//intrinsic_sdk/intrinsic/util/grpc:error_handling",
        "//intrinsic_sdk/intrinsic/util/grpc:interceptor",
        "@com_github_grpc_grpc//src/python/grpcio/grpc:grpcio",
        "@intrinsic_apis//intrinsic/assets/proto/v1:resolved_dependency_py_pb2",
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_config_py_pb2",
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_identifier_py_pb2",
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_service_py_pb2",
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_service_py_pb2_grpc",
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_settings_py_pb2",
        "@intrinsic_apis//intrinsic/perception/proto/v1:capture_result_py_pb2",
    ],
)

py_library(
    name = "cameras",
    srcs = ["cameras.py"],
    deps = [
        ":_camera_utils",
        ":camera_client",
        ":data_classes",
        "//intrinsic_sdk/intrinsic/assets:interface_utils_py",
        "//intrinsic_sdk/intrinsic/assets/dependencies:utils_py",
        "//intrinsic_sdk/intrinsic/math/python:pose3",
        "//intrinsic_sdk/intrinsic/resources/client:resource_registry_client",
        "//intrinsic_sdk/intrinsic/skills/python:skill_interface",
        "//intrinsic_sdk/intrinsic/util/grpc:connection",
        "//intrinsic_sdk/intrinsic/util/grpc:error_handling",
        "//intrinsic_sdk/intrinsic/world/python:object_world_client",
        "//intrinsic_sdk/intrinsic/world/python:object_world_resources",
        "@ai_intrinsic_sdks_pip_deps//numpy",
        "@com_github_grpc_grpc//src/python/grpcio/grpc:grpcio",
        "@com_google_absl_py//absl/logging",
        "@com_google_protobuf//:protobuf_python",
        "@intrinsic_apis//intrinsic/assets/proto/v1:resolved_dependency_py_pb2",
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_config_py_pb2",
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_config_service_py_pb2",
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_config_service_py_pb2_grpc",
        "@intrinsic_apis//intrinsic/perception/proto/v1:settings_py_pb2",
        "@intrinsic_apis//intrinsic/resources/proto:resource_handle_py_pb2",
        "@intrinsic_apis//intrinsic/skills/proto:equipment_py_pb2",
    ],
)
