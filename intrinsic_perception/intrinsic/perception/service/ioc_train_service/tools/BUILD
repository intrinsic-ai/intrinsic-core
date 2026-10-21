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

load("@rules_python//python:defs.bzl", "py_binary")

package(default_visibility = ["//visibility:public"])

py_binary(
    name = "register_using_ioc_train_service",
    srcs = ["register_using_ioc_train_service.py"],
    deps = [
        "//intrinsic_perception/intrinsic/perception/service/ioc_train_service:ioc_train_service_lib",
        "//intrinsic_perception/intrinsic/perception/service/ioc_train_service/proto:ioc_pose_estimator_params_py_pb2",
        "//intrinsic_sdk/intrinsic/assets/proto:installed_assets_pb2_grpc",
        "//intrinsic_sdk/intrinsic/assets/proto:installed_assets_py_pb2",
        "//intrinsic_sdk/intrinsic/solutions:deployments",
        "@com_github_grpc_grpc//src/python/grpcio/grpc:grpcio",
        "@com_google_absl_py//absl:app",
        "@com_google_absl_py//absl/flags",
        "@com_google_googleapis//google/longrunning:operations_py_proto",
        "@com_google_protobuf//:protobuf_python",
        "@intrinsic_apis//intrinsic/assets/data/proto/v1:data_asset_py_pb2",
        "@intrinsic_apis//intrinsic/assets/proto:id_py_pb2",
        "@intrinsic_apis//intrinsic/assets/proto:metadata_py_pb2",
        "@intrinsic_apis//intrinsic/assets/proto:view_py_pb2",
        "@intrinsic_apis//intrinsic/perception/proto/v1:pose_estimation_config_py_pb2",
        "@intrinsic_apis//intrinsic/perception/proto/v1:pose_range_py_pb2",
        "@intrinsic_apis//intrinsic/perception/proto/v1:target_py_pb2",
        "@intrinsic_apis//intrinsic/perception/proto/v1:train_service_py_pb2",
        "@intrinsic_apis//intrinsic/perception/proto/v1:train_service_py_pb2_grpc",
        "@intrinsic_apis//intrinsic/scene/proto/v1:scene_object_py_pb2",
    ],
)
