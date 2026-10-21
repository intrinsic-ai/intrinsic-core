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
load("//bazel:container.bzl", "container_image")
load("//bazel:python_oci_image.bzl", "python_oci_image")
load("//intrinsic_sdk/intrinsic/assets/services/build_defs:services.bzl", "intrinsic_service")

py_library(
    name = "inference_service",
    srcs = ["inference_service.py"],
    deps = [
        "//intrinsic_inference/assets/inference_service/v1:inference_service_config_py_pb2",
        "//intrinsic_inference/core:inference_runner",
        "//intrinsic_inference/core:model_controller_base",
        "//intrinsic_inference/core/v1:ml_model_py_pb2",
        "//intrinsic_sdk/intrinsic/assets/install:installed_assets_client",
        "//intrinsic_sdk/intrinsic/util/grpc:error_handling",
        "@ai_intrinsic_sdks_pip_deps//tritonclient",
        "@com_github_grpc_grpc//src/python/grpcio/grpc:grpcio",
        "@com_google_absl_py//absl:app",
        "@com_google_absl_py//absl/flags",
        "@com_google_absl_py//absl/logging",
        "@com_google_protobuf//:protobuf_python",
        "@intrinsic_apis//intrinsic/assets/services/proto/v1:service_state_py_pb2",
        "@intrinsic_apis//intrinsic/assets/services/proto/v1:service_state_py_pb2_grpc",
        "@intrinsic_apis//intrinsic/resources/proto:runtime_context_py_pb2",
    ] + [],
)

py_library(
    name = "model_assets_manager",
    srcs = ["model_assets_manager.py"],
    deps = [
        "//intrinsic/storage/content_addressable_storage/python:client_helpers",
        "//intrinsic_inference/assets/inference_service/v1:ml_model_asset_py_pb2",
        "//intrinsic_inference/core:model_assets_manager_base",
        "//intrinsic_inference/core:model_assets_managers_factory",
        "//intrinsic_inference/core/v1:ml_model_py_pb2",
        "//intrinsic_sdk/intrinsic/assets/install:installed_assets_client",
        "@com_github_grpc_grpc//src/python/grpcio/grpc:grpcio",
        "@com_google_absl_py//absl/logging",
        "@intrinsic_apis//intrinsic/assets/data/proto/v1:data_assets_py_pb2",
        "@intrinsic_apis//intrinsic/assets/data/proto/v1:data_assets_py_pb2_grpc",
        "@intrinsic_apis//intrinsic/assets/data/proto/v1:referenced_data_py_pb2",
        "@intrinsic_apis//intrinsic/assets/proto:asset_type_py_pb2",
        "@intrinsic_apis//intrinsic/assets/proto:id_py_pb2",
        "@intrinsic_apis//intrinsic/assets/proto:view_py_pb2",
        "@intrinsic_apis//intrinsic/storage/content_addressable_storage/proto:cas_service_py_pb2_grpc",
    ],
)

py_library(
    name = "factory_imports",
    srcs = ["factory_imports.py"],
    deps = [
        ":model_assets_manager",
        "//intrinsic_inference/core:model_assets_manager_local_repo",
    ],
)

py_binary(
    name = "inference_service_main",
    srcs = ["inference_service_main.py"],
    deps = [
        ":factory_imports",
        ":inference_service",
        "//intrinsic_inference/assets/inference_service/v1:inference_service_config_py_pb2",
        "//intrinsic_inference/core:model_assets_managers_factory",
        "//intrinsic_inference/core:model_controller_triton",
        "//intrinsic_inference/core:telemetry_otel",
        "//intrinsic_sdk/intrinsic/assets/dependencies:utils_py",
        "//intrinsic_sdk/intrinsic/assets/install:installed_assets_client",
        "@ai_intrinsic_sdks_pip_deps//tritonclient",
        "@com_github_grpc_grpc//src/python/grpcio/grpc:grpcio",
        "@com_google_absl_py//absl:app",
        "@com_google_absl_py//absl/flags",
        "@com_google_absl_py//absl/logging",
        "@intrinsic_apis//intrinsic/assets/data/proto/v1:data_assets_py_pb2_grpc",
        "@intrinsic_apis//intrinsic/assets/services/proto/v1:service_state_py_pb2_grpc",
        "@intrinsic_apis//intrinsic/resources/proto:runtime_context_py_pb2",
        "@intrinsic_apis//intrinsic/storage/content_addressable_storage/proto:cas_service_py_pb2_grpc",
    ] + [],
)

# Inference_service asset including either a multi-backend Triton inference service image
# or a minimal CPU-only image. The minimal image is part of the presubmit tests while the
# full image runs only in scheduled integration tests.
intrinsic_service(
    name = "inference_service_asset",
    default_config = "//intrinsic_inference/assets/inference_service/v1:default_inference_service_config_values.textproto",
    images = [
        ":inference_service_image.tar",
        ":triton_inference_server_image.tar",
    ],
    manifest = "inference_service.manifest.textproto",
    visibility = ["//visibility:public"],
    deps = [
        "//intrinsic_inference/assets/inference_service/v1:inference_service_config_proto",
        "@intrinsic_apis//intrinsic/assets/services/proto/v1:service_state_proto",
        "@open_inference_protocol//:open_inference_grpc_proto",
    ],
)

python_oci_image(
    name = "inference_service_image",
    base = "@distroless_python3",
    binary = "inference_service_main",
)

container_image(
    name = "triton_inference_server_image",
    base = "@tritonserver-onprem",
    env = {
        "TRITON_TEMP_DIR": "/tmp",
    },
)
