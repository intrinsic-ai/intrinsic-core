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

load("@rules_python//python:defs.bzl", "py_binary", "py_library")
load("//bazel:python_oci_image.bzl", "python_oci_image")
load("//intrinsic_sdk/intrinsic/assets/services/build_defs:services.bzl", "intrinsic_service")

package(default_visibility = [
    "//intrinsic_perception/intrinsic/perception/service/ioc_train_service:__subpackages__",
    "//visibility:public",
])

py_library(
    name = "ioc_train_service_lib",
    srcs = ["service/ioc_train_service.py"],
    deps = [
        "//intrinsic/util/proto:descriptors_py",
        "//intrinsic_perception/intrinsic/perception/service/ioc_train_service/proto:ioc_pose_estimator_params_py_pb2",
        "//intrinsic_perception/intrinsic/perception/service/ioc_train_service/proto:ioc_train_service_config_py_pb2",
        "//intrinsic_sdk/intrinsic/assets:interface_utils_py",
        "//intrinsic_sdk/intrinsic/assets/dependencies:utils_py",
        "//intrinsic_sdk/intrinsic/assets/proto:installed_assets_pb2_grpc",
        "//intrinsic_sdk/intrinsic/assets/proto:installed_assets_py_pb2",
        "//intrinsic_sdk/intrinsic/math/python:ros_proto_conversion",
        "@ai_intrinsic_sdks_pip_deps//numpy",
        "@com_github_grpc_grpc//src/python/grpcio/grpc:grpcio",
        "@com_google_googleapis//google/longrunning:operations_py_proto",
        "@com_google_protobuf//:protobuf_python",
        "@intrinsic_apis//intrinsic/assets/data/proto/v1:data_asset_py_pb2",
        "@intrinsic_apis//intrinsic/assets/data/proto/v1:referenced_data_py_pb2",
        "@intrinsic_apis//intrinsic/assets/proto:metadata_py_pb2",
        "@intrinsic_apis//intrinsic/perception/proto/v1:perception_model_py_pb2",
        "@intrinsic_apis//intrinsic/perception/proto/v1:train_service_py_pb2",
        "@intrinsic_apis//intrinsic/perception/proto/v1:train_service_py_pb2_grpc",
        "@intrinsic_apis//intrinsic/scene/proto/v1:scene_object_py_pb2",
    ],
)

py_binary(
    name = "ioc_train_service_main",
    srcs = ["service/ioc_train_service_main.py"],
    deps = [
        ":ioc_train_service_lib",
        "//intrinsic_perception/intrinsic/perception/service/ioc_train_service/proto:ioc_train_service_config_py_pb2",
        "@com_github_grpc_grpc//src/python/grpcio/grpc:grpcio",
        "@com_google_absl_py//absl:app",
        "@com_google_absl_py//absl/flags",
        "@intrinsic_apis//intrinsic/perception/proto/v1:train_service_py_pb2_grpc",
        "@intrinsic_apis//intrinsic/resources/proto:runtime_context_py_pb2",
    ],
)

python_oci_image(
    name = "ioc_train_service_image",
    binary = ":ioc_train_service_main",
)

intrinsic_service(
    name = "ioc_train_service_asset",
    default_config = "configs/default_config.textproto",
    images = [":ioc_train_service_image.tar"],
    manifest = "service_manifest.textproto",
    deps = [
        "//intrinsic_perception/intrinsic/perception/service/ioc_train_service/proto:ioc_pose_estimator_params_proto",
        "//intrinsic_perception/intrinsic/perception/service/ioc_train_service/proto:ioc_train_service_config_proto",
        "//intrinsic_sdk/intrinsic/assets/proto:installed_assets_proto",
        "@com_google_protobuf//:empty_proto",
        "@intrinsic_apis//intrinsic/assets/proto:field_metadata_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:resolved_dependency_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:train_service_proto",
    ],
)
