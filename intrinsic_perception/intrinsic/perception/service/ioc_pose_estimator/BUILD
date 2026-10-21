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
    "//intrinsic_perception/intrinsic/perception/service/ioc_pose_estimator:__subpackages__",
    "//visibility:public",
])

py_library(
    name = "segmentation_model",
    srcs = ["service/segmentation_model.py"],
    data = ["model_files/segmentation/config.pbtxt"],
    deps = [
        "//intrinsic_inference/core/utils:oip_mappings",
        "//intrinsic_inference/core/utils:oip_utils",
        "//intrinsic_sdk/intrinsic/assets:id_utils_py",
        "//incode/util/runfiles",
        "@ai_intrinsic_sdks_pip_deps//numpy",
        "@ai_intrinsic_sdks_pip_deps//opencv_python_headless",
        "@com_github_grpc_grpc//src/python/grpcio/grpc:grpcio",
        "@com_google_protobuf//:protobuf_python",
        "@intrinsic_apis//intrinsic/assets/proto:id_py_pb2",
        "@intrinsic_apis//intrinsic/assets/proto/v1:resolved_dependency_py_pb2",
        "@open_inference_protocol//:open_inference_grpc_py_pb2",
        "@open_inference_protocol//:open_inference_grpc_py_pb2_grpc",
        "@triton-inference-server-common//protobuf:common_py_pb2",
    ],
)

py_library(
    name = "pose_estimator_model",
    srcs = ["service/pose_estimator_model.py"],
    data = ["model_files/pose/config.pbtxt"],
    deps = [
        "//intrinsic_inference/core/utils:oip_mappings",
        "//intrinsic_inference/core/utils:oip_utils",
        "//intrinsic_sdk/intrinsic/assets:id_utils_py",
        "//incode/util/runfiles",
        "@ai_intrinsic_sdks_pip_deps//numpy",
        "@ai_intrinsic_sdks_pip_deps//opencv_python_headless",
        "@ai_intrinsic_sdks_pip_deps//trimesh",
        "@com_github_grpc_grpc//src/python/grpcio/grpc:grpcio",
        "@com_google_protobuf//:protobuf_python",
        "@intrinsic_apis//intrinsic/assets/proto:id_py_pb2",
        "@intrinsic_apis//intrinsic/assets/proto/v1:resolved_dependency_py_pb2",
        "@open_inference_protocol//:open_inference_grpc_py_pb2",
        "@open_inference_protocol//:open_inference_grpc_py_pb2_grpc",
        "@triton-inference-server-common//protobuf:common_py_pb2",
    ],
)

py_library(
    name = "ioc_pose_estimator_service",
    srcs = ["service/ioc_pose_estimator_service.py"],
    deps = [
        ":pose_estimator_model",
        ":segmentation_model",
        "//intrinsic/storage/content_addressable_storage/python:client_helpers",
        "//intrinsic_perception/intrinsic/perception/client/v1/python:image_utils",
        "//intrinsic_perception/intrinsic/perception/client/v1/python/camera:data_classes",
        "//intrinsic_perception/intrinsic/perception/service/ioc_pose_estimator/proto:ioc_service_config_py_pb2",
        "//intrinsic_perception/intrinsic/perception/service/ioc_train_service/proto:ioc_pose_estimator_params_py_pb2",
        "//intrinsic_perception/intrinsic/perception/skills/multi_view:multi_view_pose_utils",
        "//intrinsic_sdk/intrinsic/assets:interface_utils_py",
        "//intrinsic_sdk/intrinsic/assets/dependencies:utils_py",
        "//intrinsic_sdk/intrinsic/assets/install:installed_assets_client",
        "//intrinsic_sdk/intrinsic/assets/proto:installed_assets_pb2_grpc",
        "//intrinsic_sdk/intrinsic/assets/proto:installed_assets_py_pb2",
        "//intrinsic_sdk/intrinsic/math/python:pose3",
        "//intrinsic_sdk/intrinsic/math/python:proto_conversion",
        "//intrinsic_sdk/intrinsic/math/python:rotation3",
        "//intrinsic_sdk/intrinsic/platform/pubsub/python:pubsub",
        "//intrinsic_sdk/intrinsic/util/grpc:interceptor",
        "@ai_intrinsic_sdks_pip_deps//numpy",
        "@ai_intrinsic_sdks_pip_deps//opencv_python_headless",
        "@ai_intrinsic_sdks_pip_deps//trimesh",
        "@com_github_grpc_grpc//src/python/grpcio/grpc:grpcio",
        "@com_google_googleapis//google/longrunning:operations_py_proto",
        "@com_google_protobuf//:protobuf_python",
        "@intrinsic_apis//intrinsic/assets/proto:id_py_pb2",
        "@intrinsic_apis//intrinsic/assets/proto/v1:resolved_dependency_py_pb2",
        "@intrinsic_apis//intrinsic/perception/proto/v1:capture_result_py_pb2",
        "@intrinsic_apis//intrinsic/perception/proto/v1:image_buffer_py_pb2",
        "@intrinsic_apis//intrinsic/perception/proto/v1:perception_model_py_pb2",
        "@intrinsic_apis//intrinsic/perception/proto/v1:pose_estimation_service_py_pb2",
        "@intrinsic_apis//intrinsic/perception/proto/v1:pose_estimation_service_py_pb2_grpc",
        "@intrinsic_apis//intrinsic/storage/content_addressable_storage/proto:cas_service_py_pb2_grpc",
        "@open_inference_protocol//:open_inference_grpc_py_pb2_grpc",
    ],
)

py_binary(
    name = "ioc_service_main",
    srcs = ["service/ioc_service_main.py"],
    deps = [
        ":ioc_pose_estimator_service",
        "//intrinsic_perception/intrinsic/perception/service/ioc_pose_estimator/proto:ioc_service_config_py_pb2",
        "@com_github_grpc_grpc//src/python/grpcio/grpc:grpcio",
        "@com_google_absl_py//absl:app",
        "@com_google_absl_py//absl/flags",
        "@com_google_protobuf//:protobuf_python",
        "@intrinsic_apis//intrinsic/perception/proto/v1:pose_estimation_service_py_pb2_grpc",
        "@intrinsic_apis//intrinsic/resources/proto:runtime_context_py_pb2",
    ],
)

python_oci_image(
    name = "ioc_pose_estimator_image",
    binary = ":ioc_service_main",
)

intrinsic_service(
    name = "ioc_pose_estimator_service_asset",
    default_config = "configs/default_config.textproto",
    images = [":ioc_pose_estimator_image.tar"],
    manifest = "service_manifest.textproto",
    deps = [
        "//intrinsic_inference/assets/inference_service/v1:ml_model_asset_proto",
        "//intrinsic_perception/intrinsic/perception/service/ioc_pose_estimator/proto:ioc_service_config_proto",
        "//intrinsic_sdk/intrinsic/assets/proto:installed_assets_proto",
        "@com_google_googleapis//google/longrunning:operations_proto",
        "@com_google_protobuf//:wrappers_proto",
        "@intrinsic_apis//intrinsic/assets/proto:field_metadata_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:resolved_dependency_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:pose_estimation_service_proto",
    ],
)

py_binary(
    name = "run_pose_estimation",
    srcs = ["run_pose_estimation.py"],
    deps = [
        "//intrinsic_perception/intrinsic/perception/skills/multi_view:estimate_pose_multi_view_py_pb2",
        "//intrinsic_sdk/intrinsic/assets:id_utils_py",
        "//intrinsic_sdk/intrinsic/math/python:proto_conversion",
        "//intrinsic_sdk/intrinsic/solutions:deployments",
        "//intrinsic_sdk/intrinsic/solutions:execution",
        "//intrinsic_sdk/intrinsic/solutions:provided",
        "@com_github_grpc_grpc//src/python/grpcio/grpc:grpcio",
        "@com_google_absl_py//absl:app",
        "@com_google_absl_py//absl/flags",
        "@intrinsic_apis//intrinsic/perception/proto/v1:pose_estimator_id_py_pb2",
    ],
)
