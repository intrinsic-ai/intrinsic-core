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

load("@bazel_skylib//rules:common_settings.bzl", "bool_flag")
load("@bazel_skylib//rules:write_file.bzl", "write_file")
load("@nvidia_pose_estimator_models//:files.bzl", "FOUNDATIONPOSE_PLANS", "FOUNDATIONSTEREO_PLANS")
load("@rules_pkg//pkg:tar.bzl", "pkg_tar")
load("@rules_python//python:defs.bzl", "py_binary", "py_library")
load("//bazel:python_oci_image.bzl", "python_oci_image")
load("//intrinsic/assets/services/build_defs:services.bzl", "intrinsic_service")
load(":model_data_asset.bzl", "perception_model_data_asset", "sam3_model_data_asset")

package(default_visibility = ["//visibility:public"])

# Pass --//intrinsic_perception/intrinsic/perception/service/nvidia_pose_estimator:build_full_image=True
# to build the real TensorRT image. By default the image uses the default Python base and omits the
# FoundationPose library, so the ~5.8 GB TensorRT image is never fetched.
bool_flag(
    name = "build_full_image",
    build_setting_default = False,
)

config_setting(
    name = "build_full_image_setting",
    flag_values = {":build_full_image": "True"},
)

# Pass --//intrinsic_perception/intrinsic/perception/service/nvidia_pose_estimator:download_weights=True
# to build the FoundationPose / FoundationStereo model data assets. By default their ONNX weights
# (~480 MB from NGC) are not depended on, so analysing or building these targets without the flag
# downloads nothing and skips building the bundle with a warning.
bool_flag(
    name = "download_weights",
    build_setting_default = False,
)

config_setting(
    name = "download_weights_setting",
    flag_values = {":download_weights": "True"},
)

py_library(
    name = "nvidia_pose_estimator_env_lib",
    srcs = ["util/environment.py"],
    data = select({
        ":build_full_image_setting": ["@foundation_pose_inference_library//:libfoundation_pose_nvidia.so.0.1.0"],
        "//conditions:default": [],
    }),
    deps = [
        "//incode/util/runfiles",
    ],
)

py_library(
    name = "nvidia_pose_estimator_pose_utils_lib",
    srcs = ["util/pose_utils.py"],
    deps = [
        "@ai_intrinsic_sdks_pip_deps//numpy",
    ],
)

py_library(
    name = "nvidia_pose_estimator_data_classes_lib",
    srcs = ["util/data_classes.py"],
    deps = [
        "@ai_intrinsic_sdks_pip_deps//numpy",
    ],
)

py_library(
    name = "nvidia_pose_estimator_image_utils_lib",
    srcs = ["util/image_utils.py"],
    deps = [
        "//intrinsic_perception/intrinsic/perception/client/v1/python/camera:data_classes",
        "//intrinsic_perception/intrinsic/perception/service/nvidia_pose_estimator/proto:nvidia_service_config_py_pb2",
        "//intrinsic_perception/intrinsic/perception/skills/multi_view:multi_view_pose_utils",
        "//intrinsic_sdk/intrinsic/math/python:pose3",
        "//intrinsic_sdk/intrinsic/platform/pubsub/python:pubsub",
        "@ai_intrinsic_sdks_pip_deps//numpy",
        "@intrinsic_apis//intrinsic/perception/proto/v1:image_buffer_py_pb2",
        "@intrinsic_apis//intrinsic/perception/proto/v1:pose_estimation_service_py_pb2",
    ],
)

py_library(
    name = "nvidia_pose_estimator_logging_utils_lib",
    srcs = ["util/logging_utils.py"],
    deps = [
        "@ai_intrinsic_sdks_pip_deps//numpy",
    ],
)

py_library(
    name = "nvidia_pose_estimator_debug_utils_lib",
    srcs = ["util/debug_utils.py"],
    deps = [
        ":nvidia_pose_estimator_path_resolver_lib",
        ":nvidia_pose_estimator_pose_utils_lib",
        "//intrinsic_perception/intrinsic/perception/client/v1/python:image_utils",
        "@ai_intrinsic_sdks_pip_deps//numpy",
        "@ai_intrinsic_sdks_pip_deps//opencv_python_headless",
        "@intrinsic_apis//intrinsic/perception/proto/v1:image_buffer_py_pb2",
    ],
)

py_library(
    name = "nvidia_pose_estimator_asset_utils_lib",
    srcs = ["util/asset_utils.py"],
    deps = [
        ":nvidia_pose_estimator_model_asset_utils_lib",
        "//intrinsic/assets:id_utils_py",
        "//intrinsic/assets/proto:installed_assets_py_pb2",
        "//intrinsic/storage/content_addressable_storage/python:client_helpers",
        "//intrinsic_perception/intrinsic/perception/service/ioc_train_service/proto:ioc_pose_estimator_params_py_pb2",
        "//intrinsic_sdk/intrinsic/assets/install:installed_assets_client",
        "@ai_intrinsic_sdks_pip_deps//numpy",
        "@ai_intrinsic_sdks_pip_deps//trimesh",
        "@com_github_grpc_grpc//src/python/grpcio/grpc:grpcio",
        "@com_google_protobuf//:protobuf_python",
        "@intrinsic_apis//intrinsic/assets/proto:id_py_pb2",
        "@intrinsic_apis//intrinsic/assets/proto:view_py_pb2",
        "@intrinsic_apis//intrinsic/perception/proto/v1:perception_model_py_pb2",
        "@intrinsic_apis//intrinsic/storage/content_addressable_storage/proto:cas_service_py_pb2_grpc",
    ],
)

py_library(
    name = "nvidia_pose_estimator_path_resolver_lib",
    srcs = ["util/path_resolver.py"],
)

py_library(
    name = "nvidia_pose_estimator_model_asset_utils_lib",
    srcs = ["util/model_asset_utils.py"],
    deps = [
        "//intrinsic/assets:id_utils_py",
        "//intrinsic/storage/content_addressable_storage/python:client_helpers",
        "//intrinsic_perception/intrinsic/perception/service/nvidia_pose_estimator/proto:nvidia_service_config_py_pb2",
        "//intrinsic_sdk/intrinsic/assets/install:installed_assets_client",
        "@com_github_grpc_grpc//src/python/grpcio/grpc:grpcio",
        "@intrinsic_apis//intrinsic/assets/data/proto/v1:referenced_data_py_pb2",
        "@intrinsic_apis//intrinsic/assets/proto:id_py_pb2",
        "@intrinsic_apis//intrinsic/assets/proto:view_py_pb2",
        "@intrinsic_apis//intrinsic/assets/proto/v1:resolved_dependency_py_pb2",
        "@intrinsic_apis//intrinsic/perception/proto/v1:perception_model_py_pb2",
        "@intrinsic_apis//intrinsic/storage/content_addressable_storage/proto:cas_service_py_pb2_grpc",
    ],
)

py_library(
    name = "nvidia_pose_estimator_config_utils_lib",
    srcs = ["util/config_utils.py"],
    data = [
        "configs/default_config.textproto",
    ],
    deps = [
        ":perception_pipeline_lib",
        "//intrinsic_perception/intrinsic/perception/service/nvidia_pose_estimator/proto:nvidia_service_config_py_pb2",
        "//incode/util/runfiles",
        "@com_google_protobuf//:protobuf_python",
    ],
)

py_library(
    name = "nvidia_pose_estimator_model_warmup_lib",
    srcs = ["util/model_warmup.py"],
    deps = [
        "//intrinsic_perception/intrinsic/perception/service/nvidia_pose_estimator/proto:nvidia_service_config_py_pb2",
        "@ai_intrinsic_sdks_pip_deps//numpy",
        "@ai_intrinsic_sdks_pip_deps//trimesh",
    ],
)

py_library(
    name = "nvidia_pose_estimator_service",
    srcs = ["service/nvidia_pose_estimator_service.py"],
    data = [
        "configs/default_config.textproto",
    ],
    deps = [
        ":nvidia_pose_estimator_asset_utils_lib",
        ":nvidia_pose_estimator_config_utils_lib",
        ":nvidia_pose_estimator_data_classes_lib",
        ":nvidia_pose_estimator_debug_utils_lib",
        ":nvidia_pose_estimator_env_lib",
        ":nvidia_pose_estimator_image_utils_lib",
        ":nvidia_pose_estimator_logging_utils_lib",
        ":nvidia_pose_estimator_model_asset_utils_lib",
        ":nvidia_pose_estimator_model_warmup_lib",
        ":nvidia_pose_estimator_path_resolver_lib",
        ":nvidia_pose_estimator_pose_utils_lib",
        ":perception_pipeline_lib",
        "//intrinsic/assets:id_utils_py",
        "//intrinsic_perception/intrinsic/perception/service/nvidia_pose_estimator/proto:nvidia_service_config_py_pb2",
        "//intrinsic_perception/intrinsic/perception/utils:point_cloud_utils",
        "//intrinsic_sdk/intrinsic/assets/dependencies:utils_py",
        "//intrinsic_sdk/intrinsic/assets/install:installed_assets_client",
        "//intrinsic_sdk/intrinsic/math/python:pose3",
        "//intrinsic_sdk/intrinsic/math/python:proto_conversion",
        "//intrinsic_sdk/intrinsic/math/python:rotation3",
        "//intrinsic_sdk/intrinsic/platform/pubsub/python:pubsub",
        "@ai_intrinsic_sdks_pip_deps//ftfy",
        "@ai_intrinsic_sdks_pip_deps//numpy",
        "@ai_intrinsic_sdks_pip_deps//opencv_python_headless",
        "@ai_intrinsic_sdks_pip_deps//pillow",
        "@ai_intrinsic_sdks_pip_deps//psutil",
        "@ai_intrinsic_sdks_pip_deps//pycocotools",
        "@ai_intrinsic_sdks_pip_deps//pyyaml",
        "@ai_intrinsic_sdks_pip_deps//regex",
        "@ai_intrinsic_sdks_pip_deps//scipy",
        "@ai_intrinsic_sdks_pip_deps//tqdm",
        "@ai_intrinsic_sdks_pip_deps//trimesh",
        "@ai_intrinsic_sdks_pip_deps//wcwidth",
        "@com_github_grpc_grpc//src/python/grpcio/grpc:grpcio",
        "@com_google_protobuf//:protobuf_python",
        "@intrinsic_apis//intrinsic/assets/proto:id_py_pb2",
        "@intrinsic_apis//intrinsic/perception/proto/v1:pose_estimation_service_py_pb2",
        "@intrinsic_apis//intrinsic/perception/proto/v1:pose_estimation_service_py_pb2_grpc",
        "@intrinsic_apis//intrinsic/storage/content_addressable_storage/proto:cas_service_py_pb2_grpc",
    ],
)

py_binary(
    name = "nvidia_service_main",
    srcs = ["service/nvidia_service_main.py"],
    deps = [
        ":nvidia_pose_estimator_service",
        ":tensorrt_shim",
        "//intrinsic_perception/intrinsic/perception/service/nvidia_pose_estimator/proto:nvidia_service_config_py_pb2",
        "@com_github_grpc_grpc//src/python/grpcio/grpc:grpcio",
        "@com_google_absl_py//absl:app",
        "@com_google_absl_py//absl/flags",
        "@com_google_protobuf//:protobuf_python",
        "@intrinsic_apis//intrinsic/perception/proto/v1:pose_estimation_service_py_pb2_grpc",
        "@intrinsic_apis//intrinsic/resources/proto:runtime_context_py_pb2",
    ],
)

pkg_tar(
    name = "nvidia_pose_estimator_fp_library_layer",
    srcs = select({
        ":build_full_image_setting": ["@foundation_pose_inference_library//:libfoundation_pose_nvidia.so.0.1.0"],
        "//conditions:default": [],
    }),
    compressor_args = "--fast",
    extension = "tar.gz",
    package_dir = "/usr/lib/x86_64-linux-gnu",
    symlinks = {
        "/usr/lib/x86_64-linux-gnu/libfoundation_pose_nvidia.so": "libfoundation_pose_nvidia.so.1",
        "/usr/lib/x86_64-linux-gnu/libfoundation_pose_nvidia.so.1": "libfoundation_pose_nvidia.so.0.1.0",
    },
)

# Model weights and optional precompiled TensorRT plans packaged as Data assets.
_FOUNDATIONPOSE_VERSION = "tao-deployable-v1.0"

_FOUNDATIONSTEREO_VERSION = "tao-deployable-s-dynamic-v2.0"

# LINT.IfChange(sam3_version)
# Pinned upstream SAM 3 commit used to export the ONNX graphs.
_SAM3_VERSION = "sam3-96914d2425f90a64f45ca977c2b5165418099543"
# LINT.ThenChange(//intrinsic_perception/intrinsic/perception/service/nvidia_pose_estimator/scripts/prepare_sam3_models.sh:sam3_commit)

perception_model_data_asset(
    name = "foundationpose_model_data_asset",
    asset_name = "foundationpose_models",
    description = "FoundationPose refiner and score networks (NVIDIA TAO deployable v1.0).",
    display_name = "FoundationPose models",
    download_flag = "//intrinsic_perception/intrinsic/perception/service/nvidia_pose_estimator:download_weights",
    download_setting = ":download_weights_setting",
    plans = FOUNDATIONPOSE_PLANS,
    tags = ["manual"],
    version = _FOUNDATIONPOSE_VERSION,
    weights = {
        "refiner_net.onnx": "@foundationpose_refine_onnx//file",
        "score_net.onnx": "@foundationpose_score_onnx//file",
    },
)

perception_model_data_asset(
    name = "foundationstereo_model_data_asset",
    asset_name = "foundationstereo_models",
    description = "FoundationStereo disparity network (NVIDIA TAO deployable s-dynamic v2.0).",
    display_name = "FoundationStereo models",
    download_flag = "//intrinsic_perception/intrinsic/perception/service/nvidia_pose_estimator:download_weights",
    download_setting = ":download_weights_setting",
    plans = FOUNDATIONSTEREO_PLANS,
    tags = ["manual"],
    version = _FOUNDATIONSTEREO_VERSION,
    weights = {
        "deployable_foundation_stereo_s_dynamic_v2.0.onnx": "@foundationstereo_onnx//file",
    },
)

sam3_model_data_asset(
    name = "sam3_model_data_asset",
    version = _SAM3_VERSION,
)

python_oci_image(
    name = "nvidia_pose_estimator_image",
    base = select({
        ":build_full_image_setting": "@tensorrt",
        "//conditions:default": "//intrinsic/kubernetes:python3-image-oci",
    }),
    binary = ":nvidia_service_main",
    extra_tars = [
        ":nvidia_pose_estimator_fp_library_layer",
    ],
)

intrinsic_service(
    name = "nvidia_pose_estimator_service_asset",
    default_config = "configs/default_config.textproto",
    images = [":nvidia_pose_estimator_image.tar"],
    manifest = "service_manifest.textproto",
    deps = [
        "//intrinsic_perception/intrinsic/perception/service/nvidia_pose_estimator/proto:nvidia_service_config_proto",
        "@com_google_protobuf//:wrappers_proto",
        "@intrinsic_apis//intrinsic/assets/proto:field_metadata_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:resolved_dependency_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:perception_model_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:pose_estimation_service_proto",
    ],
)

write_file(
    name = "tensorrt_shim_init",
    out = "tensorrt_shim/tensorrt/__init__.py",
    content = [
        '"""Re-exports the tensorrt-cu13-bindings wheel under its conventional name."""',
        "",
        "from tensorrt_bindings import *  # pylint: disable=wildcard-import",
        "from tensorrt_bindings import __version__",
    ],
)

# Production: fails at import if the TensorRT native libraries are missing.
py_library(
    name = "tensorrt_shim",
    srcs = [":tensorrt_shim_init"],
    imports = ["tensorrt_shim"],
    deps = [
        "@ai_intrinsic_sdks_pip_deps//tensorrt_cu13_bindings",
    ],
)

py_library(
    name = "foundation_pose_nvidia_lib",
    deps = [
        "@ai_intrinsic_sdks_pip_deps//numpy",
        "@foundation_pose_inference_library//:foundation_pose_nvidia",
    ],
)

py_library(
    name = "perception_pipeline_lib",
    deps = [
        ":foundation_pose_nvidia_lib",
        "@ai_intrinsic_sdks_pip_deps//cuda_bindings",
        "@ai_intrinsic_sdks_pip_deps//ftfy",
        "@ai_intrinsic_sdks_pip_deps//numpy",
        "@ai_intrinsic_sdks_pip_deps//opencv_python_headless",
        "@ai_intrinsic_sdks_pip_deps//pillow",
        "@ai_intrinsic_sdks_pip_deps//psutil",
        "@ai_intrinsic_sdks_pip_deps//pycocotools",
        "@ai_intrinsic_sdks_pip_deps//pyyaml",
        "@ai_intrinsic_sdks_pip_deps//regex",
        "@ai_intrinsic_sdks_pip_deps//scipy",
        "@ai_intrinsic_sdks_pip_deps//tqdm",
        "@ai_intrinsic_sdks_pip_deps//trimesh",
        "@ai_intrinsic_sdks_pip_deps//wcwidth",
        "@foundationpose_perception_pipeline//:perception_pipeline",
    ],
)
