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

package(default_visibility = [
    "//visibility:public",
])

py_library(
    name = "pose_estimation_service_client",
    srcs = ["pose_estimation_service_client.py"],
    deps = [
        "//intrinsic/util/grpc:error_handling",
        "@com_github_grpc_grpc//src/python/grpcio/grpc:grpcio",
        "@intrinsic_apis//intrinsic/perception/proto/v1:pose_estimation_service_py_pb2",
        "@intrinsic_apis//intrinsic/perception/proto/v1:pose_estimation_service_py_pb2_grpc",
    ],
)

py_library(
    name = "pose_estimation_client_utils",
    srcs = ["pose_estimation_client_utils.py"],
    deps = [
        "//intrinsic/assets:id_utils_py",
        "//intrinsic/assets:interface_utils_py",
        "//intrinsic/assets/dependencies:utils_py",
        "//intrinsic/util/grpc:connection",
        "//intrinsic/util/grpc:interceptor",
        "//intrinsic_perception/intrinsic/perception/client/v1/python/camera:data_classes",
        "//intrinsic_sdk/intrinsic/math/python:data_types",
        "//intrinsic_sdk/intrinsic/math/python:proto_conversion",
        "@com_github_grpc_grpc//src/python/grpcio/grpc:grpcio",
        "@com_google_protobuf//:protobuf_python",
        "@intrinsic_apis//intrinsic/assets/proto:id_py_pb2",
        "@intrinsic_apis//intrinsic/assets/proto/v1:resolved_dependency_py_pb2",
        "@intrinsic_apis//intrinsic/logging/proto:context_py_pb2",
        "@intrinsic_apis//intrinsic/perception/proto/v1:capture_data_py_pb2",
        "@intrinsic_apis//intrinsic/perception/proto/v1:capture_result_py_pb2",
        "@intrinsic_apis//intrinsic/perception/proto/v1:pose_estimation_service_py_pb2",
        "@intrinsic_apis//intrinsic/perception/proto/v1:pose_estimator_id_py_pb2",
        "@intrinsic_apis//intrinsic/perception/proto/v1:pose_priors_py_pb2",
        "@intrinsic_apis//intrinsic/resources/proto:resource_handle_py_pb2",
    ],
)
