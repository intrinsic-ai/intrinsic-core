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

load("@com_github_grpc_grpc//bazel:cc_grpc_library.bzl", "cc_grpc_library")
load("@com_github_grpc_grpc//bazel:python_rules.bzl", "py_grpc_library")
load("@com_google_protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("@com_google_protobuf//bazel:proto_library.bzl", "proto_library")
load("@com_google_protobuf//bazel:py_proto_library.bzl", "py_proto_library")
load("@io_bazel_rules_go//proto:def.bzl", "go_proto_library")

package(default_visibility = ["//visibility:public"])

proto_library(
    name = "distortion_params_proto",
    srcs = ["distortion_params.proto"],
    visibility = ["//visibility:public"],
)

cc_proto_library(
    name = "distortion_params_cc_proto",
    visibility = ["//visibility:public"],
    deps = [":distortion_params_proto"],
)

go_proto_library(
    name = "distortion_params_go_proto",
    importpath = "intrinsic/perception/proto/v1/distortion_params_go_proto",
    protos = [":distortion_params_proto"],
    visibility = ["//visibility:public"],
)

py_proto_library(
    name = "distortion_params_py_pb2",
    visibility = ["//visibility:public"],
    deps = [":distortion_params_proto"],
)

proto_library(
    name = "dimensions_proto",
    srcs = ["dimensions.proto"],
    visibility = ["//visibility:public"],
)

cc_proto_library(
    name = "dimensions_cc_proto",
    visibility = ["//visibility:public"],
    deps = [":dimensions_proto"],
)

go_proto_library(
    name = "dimensions_go_proto",
    importpath = "intrinsic/perception/proto/v1/dimensions_go_proto",
    protos = [":dimensions_proto"],
    visibility = ["//visibility:public"],
)

py_proto_library(
    name = "dimensions_py_pb2",
    visibility = ["//visibility:public"],
    deps = [":dimensions_proto"],
)

proto_library(
    name = "intrinsic_params_proto",
    srcs = ["intrinsic_params.proto"],
    visibility = ["//visibility:public"],
    deps = [":dimensions_proto"],
)

cc_proto_library(
    name = "intrinsic_params_cc_proto",
    visibility = ["//visibility:public"],
    deps = [":intrinsic_params_proto"],
)

go_proto_library(
    name = "intrinsic_params_go_proto",
    importpath = "intrinsic/perception/proto/v1/intrinsic_params_go_proto",
    protos = [":intrinsic_params_proto"],
    visibility = ["//visibility:public"],
    deps = [":dimensions_go_proto"],
)

py_proto_library(
    name = "intrinsic_params_py_pb2",
    visibility = ["//visibility:public"],
    deps = [":intrinsic_params_proto"],
)

proto_library(
    name = "charuco_pattern_proto",
    srcs = ["charuco_pattern.proto"],
    visibility = ["//visibility:public"],
)

cc_proto_library(
    name = "charuco_pattern_cc_proto",
    visibility = ["//visibility:public"],
    deps = [":charuco_pattern_proto"],
)

go_proto_library(
    name = "charuco_pattern_go_proto",
    importpath = "intrinsic/perception/proto/v1/charuco_pattern_go_proto",
    protos = [":charuco_pattern_proto"],
    visibility = ["//visibility:public"],
)

py_proto_library(
    name = "charuco_pattern_py_pb2",
    visibility = ["//visibility:public"],
    deps = [":charuco_pattern_proto"],
)

proto_library(
    name = "pattern_detection_config_proto",
    srcs = ["pattern_detection_config.proto"],
    visibility = ["//visibility:public"],
    deps = [":charuco_pattern_proto"],
)

cc_proto_library(
    name = "pattern_detection_config_cc_proto",
    visibility = ["//visibility:public"],
    deps = [":pattern_detection_config_proto"],
)

go_proto_library(
    name = "pattern_detection_config_go_proto",
    importpath = "intrinsic/perception/proto/v1/pattern_detection_config_go_proto",
    protos = [":pattern_detection_config_proto"],
    visibility = ["//visibility:public"],
    deps = [
        ":charuco_pattern_go_proto",
    ],
)

py_proto_library(
    name = "pattern_detection_config_py_pb2",
    visibility = ["//visibility:public"],
    deps = [":pattern_detection_config_proto"],
)

proto_library(
    name = "calibration_service_proto",
    srcs = ["calibration_service.proto"],
    deps = [
        ":camera_setup_proto",
        ":camera_to_robot_calibration",
        ":capture_data_proto",
        ":distortion_params_proto",
        ":intrinsic_params_proto",
        ":pattern_detection_config_proto",
        "@com_google_protobuf//:empty_proto",
        "@intrinsic_apis//intrinsic/assets/proto:field_metadata_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:resolved_dependency_proto",
        "@intrinsic_apis//intrinsic/math/proto:pose_proto",
        "@intrinsic_apis//intrinsic/platform/pubsub:storage_location",
        "@intrinsic_apis//intrinsic/resources/proto:resource_handle_proto",
        "@intrinsic_apis//intrinsic/world/proto:object_world_refs_proto",
    ],
)

cc_proto_library(
    name = "calibration_service_cc_proto",
    deps = [":calibration_service_proto"],
)

cc_grpc_library(
    name = "calibration_service_cc_grpc_proto",
    srcs = [":calibration_service_proto"],
    grpc_only = True,
    deps = [":calibration_service_cc_proto"],
)

go_proto_library(
    name = "calibration_service_go_proto",
    compilers = [
        "@io_bazel_rules_go//proto:go_grpc_v2",
        "@io_bazel_rules_go//proto:go_proto",
    ],
    importpath = "intrinsic/perception/proto/v1/calibration_service_go_proto",
    protos = [":calibration_service_proto"],
    deps = [
        ":camera_setup_go_proto",
        ":camera_to_robot_calibration_go_proto",
        ":capture_data_go_proto",
        ":distortion_params_go_proto",
        ":intrinsic_params_go_proto",
        ":pattern_detection_config_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:field_metadata_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:resolved_dependency_go_proto",
        "@intrinsic_apis//intrinsic/math/proto:pose_go_proto",
        "@intrinsic_apis//intrinsic/platform/pubsub:storage_location_go_proto",
        "@intrinsic_apis//intrinsic/resources/proto:resource_handle_go_proto",
        "@intrinsic_apis//intrinsic/world/proto:object_world_refs_go_proto",
        "@org_golang_google_protobuf//types/known/emptypb",
    ],
)

py_proto_library(
    name = "calibration_service_py_pb2",
    deps = [":calibration_service_proto"],
)

py_grpc_library(
    name = "calibration_service_py_pb2_grpc",
    srcs = [":calibration_service_proto"],
    deps = [":calibration_service_py_pb2"],
)

proto_library(
    name = "image_buffer_proto",
    srcs = ["image_buffer.proto"],
    visibility = ["//visibility:public"],
    deps = [":dimensions_proto"],
)

cc_proto_library(
    name = "image_buffer_cc_proto",
    visibility = ["//visibility:public"],
    deps = ["image_buffer_proto"],
)

go_proto_library(
    name = "image_buffer_go_proto",
    importpath = "intrinsic/perception/proto/v1/image_buffer_go_proto",
    protos = [":image_buffer_proto"],
    visibility = ["//visibility:public"],
    deps = [":dimensions_go_proto"],
)

py_proto_library(
    name = "image_buffer_py_pb2",
    visibility = ["//visibility:public"],
    deps = [":image_buffer_proto"],
)

proto_library(
    name = "image_file_reference_proto",
    srcs = ["image_file_reference.proto"],
    deps = [":image_buffer_proto"],
)

cc_proto_library(
    name = "image_file_reference_cc_proto",
    deps = [":image_file_reference_proto"],
)

go_proto_library(
    name = "image_file_reference_go_proto",
    importpath = "intrinsic/perception/proto/v1/image_file_reference_go_proto",
    protos = [":image_file_reference_proto"],
    deps = [":image_buffer_go_proto"],
)

py_proto_library(
    name = "image_file_reference_py_pb2",
    deps = [":image_file_reference_proto"],
)

proto_library(
    name = "camera_drivers_proto",
    srcs = ["camera_drivers.proto"],
    deps = [":image_file_reference_proto"],
)

cc_proto_library(
    name = "camera_drivers_cc_proto",
    deps = ["camera_drivers_proto"],
)

go_proto_library(
    name = "camera_drivers_go_proto",
    importpath = "intrinsic/perception/proto/v1/camera_drivers_go_proto",
    protos = [":camera_drivers_proto"],
    deps = [":image_file_reference_go_proto"],
)

py_proto_library(
    name = "camera_drivers_py_pb2",
    deps = [":camera_drivers_proto"],
)

proto_library(
    name = "camera_identifier_proto",
    srcs = ["camera_identifier.proto"],
    deps = [":camera_drivers_proto"],
)

cc_proto_library(
    name = "camera_identifier_cc_proto",
    deps = [":camera_identifier_proto"],
)

go_proto_library(
    name = "camera_identifier_go_proto",
    importpath = "intrinsic/perception/proto/v1/camera_identifier_go_proto",
    protos = [":camera_identifier_proto"],
    deps = [":camera_drivers_go_proto"],
)

py_proto_library(
    name = "camera_identifier_py_pb2",
    deps = [":camera_identifier_proto"],
)

proto_library(
    name = "settings_proto",
    srcs = ["settings.proto"],
    visibility = ["//visibility:public"],
)

cc_proto_library(
    name = "settings_cc_proto",
    visibility = ["//visibility:public"],
    deps = [":settings_proto"],
)

go_proto_library(
    name = "settings_go_proto",
    importpath = "intrinsic/perception/proto/v1/settings_go_proto",
    protos = [":settings_proto"],
    visibility = ["//visibility:public"],
)

py_proto_library(
    name = "settings_py_pb2",
    visibility = ["//visibility:public"],
    deps = [":settings_proto"],
)

proto_library(
    name = "camera_settings_proto",
    srcs = ["camera_settings.proto"],
    visibility = ["//visibility:public"],
    deps = [
        ":settings_proto",
        "@com_google_protobuf//:empty_proto",
    ],
)

cc_proto_library(
    name = "camera_settings_cc_proto",
    visibility = ["//visibility:public"],
    deps = [":camera_settings_proto"],
)

go_proto_library(
    name = "camera_settings_go_proto",
    importpath = "intrinsic/perception/proto/v1/camera_settings_go_proto",
    protos = [":camera_settings_proto"],
    visibility = ["//visibility:public"],
    deps = [":settings_go_proto"],
)

py_proto_library(
    name = "camera_settings_py_pb2",
    visibility = ["//visibility:public"],
    deps = [":camera_settings_proto"],
)

proto_library(
    name = "camera_params_proto",
    srcs = ["camera_params.proto"],
    visibility = ["//visibility:public"],
    deps = [
        ":distortion_params_proto",
        ":intrinsic_params_proto",
    ],
)

cc_proto_library(
    name = "camera_params_cc_proto",
    visibility = ["//visibility:public"],
    deps = [":camera_params_proto"],
)

go_proto_library(
    name = "camera_params_go_proto",
    importpath = "intrinsic/perception/proto/v1/camera_params_go_proto",
    protos = [":camera_params_proto"],
    visibility = ["//visibility:public"],
    deps = [
        ":distortion_params_go_proto",
        ":intrinsic_params_go_proto",
    ],
)

py_proto_library(
    name = "camera_params_py_pb2",
    visibility = ["//visibility:public"],
    deps = [":camera_params_proto"],
)

proto_library(
    name = "sensor_config_proto",
    srcs = ["sensor_config.proto"],
    visibility = ["//visibility:public"],
    deps = [
        ":camera_params_proto",
        "@intrinsic_apis//intrinsic/math/proto:pose_proto",
    ],
)

cc_proto_library(
    name = "sensor_config_cc_proto",
    visibility = ["//visibility:public"],
    deps = ["sensor_config_proto"],
)

go_proto_library(
    name = "sensor_config_go_proto",
    importpath = "intrinsic/perception/proto/v1/sensor_config_go_proto",
    protos = [":sensor_config_proto"],
    visibility = ["//visibility:public"],
    deps = [
        ":camera_params_go_proto",
        "@intrinsic_apis//intrinsic/math/proto:pose_go_proto",
    ],
)

py_proto_library(
    name = "sensor_config_py_pb2",
    visibility = ["//visibility:public"],
    deps = [":sensor_config_proto"],
)

proto_library(
    name = "camera_config_proto",
    srcs = ["camera_config.proto"],
    deps = [
        ":camera_identifier_proto",
        ":camera_settings_proto",
        ":sensor_config_proto",
        "@intrinsic_apis//intrinsic/assets/proto:field_metadata_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:resolved_dependency_proto",
    ],
)

cc_proto_library(
    name = "camera_config_cc_proto",
    deps = ["camera_config_proto"],
)

go_proto_library(
    name = "camera_config_go_proto",
    importpath = "intrinsic/perception/proto/v1/camera_config_go_proto",
    protos = [":camera_config_proto"],
    deps = [
        ":camera_identifier_go_proto",
        ":camera_settings_go_proto",
        ":sensor_config_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:field_metadata_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:resolved_dependency_go_proto",
    ],
)

py_proto_library(
    name = "camera_config_py_pb2",
    deps = [":camera_config_proto"],
)

proto_library(
    name = "camera_config_service_proto",
    srcs = ["camera_config_service.proto"],
    deps = [":camera_config_proto"],
)

cc_proto_library(
    name = "camera_config_service_cc_proto",
    deps = [":camera_config_service_proto"],
)

cc_grpc_library(
    name = "camera_config_service_cc_grpc",
    srcs = [":camera_config_service_proto"],
    grpc_only = True,
    deps = [":camera_config_service_cc_proto"],
)

py_proto_library(
    name = "camera_config_service_py_pb2",
    deps = [":camera_config_service_proto"],
)

py_grpc_library(
    name = "camera_config_service_py_pb2_grpc",
    srcs = [":camera_config_service_proto"],
    deps = [":camera_config_service_py_pb2"],
)

proto_library(
    name = "sensor_image_proto",
    srcs = ["sensor_image.proto"],
    visibility = ["//visibility:public"],
    deps = [
        ":image_buffer_proto",
        ":sensor_config_proto",
        "@com_google_protobuf//:timestamp_proto",
    ],
)

cc_proto_library(
    name = "sensor_image_cc_proto",
    visibility = ["//visibility:public"],
    deps = ["sensor_image_proto"],
)

go_proto_library(
    name = "sensor_image_go_proto",
    importpath = "intrinsic/perception/proto/v1/sensor_image_go_proto",
    protos = [":sensor_image_proto"],
    visibility = ["//visibility:public"],
    deps = [
        ":image_buffer_go_proto",
        ":sensor_config_go_proto",
    ],
)

py_proto_library(
    name = "sensor_image_py_pb2",
    visibility = ["//visibility:public"],
    deps = [":sensor_image_proto"],
)

proto_library(
    name = "capture_result_proto",
    srcs = ["capture_result.proto"],
    visibility = ["//visibility:public"],
    deps = [
        ":sensor_image_proto",
        "@com_google_protobuf//:duration_proto",
        "@com_google_protobuf//:timestamp_proto",
    ],
)

cc_proto_library(
    name = "capture_result_cc_proto",
    visibility = ["//visibility:public"],
    deps = [":capture_result_proto"],
)

go_proto_library(
    name = "capture_result_go_proto",
    importpath = "intrinsic/perception/proto/v1/capture_result_go_proto",
    protos = [":capture_result_proto"],
    visibility = ["//visibility:public"],
    deps = [
        ":sensor_image_go_proto",
    ],
)

py_proto_library(
    name = "capture_result_py_pb2",
    visibility = ["//visibility:public"],
    deps = [":capture_result_proto"],
)

proto_library(
    name = "post_processing_proto",
    srcs = ["post_processing.proto"],
    visibility = ["//visibility:public"],
    deps = [
        ":dimensions_proto",
        ":image_buffer_proto",
    ],
)

cc_proto_library(
    name = "post_processing_cc_proto",
    visibility = ["//visibility:public"],
    deps = ["post_processing_proto"],
)

go_proto_library(
    name = "post_processing_go_proto",
    importpath = "intrinsic/perception/proto/v1/post_processing_go_proto",
    protos = [":post_processing_proto"],
    visibility = ["//visibility:public"],
    deps = [
        ":dimensions_go_proto",
        ":image_buffer_go_proto",
    ],
)

py_proto_library(
    name = "post_processing_py_pb2",
    visibility = ["//visibility:public"],
    deps = [":post_processing_proto"],
)

proto_library(
    name = "camera_service_proto",
    srcs = ["camera_service.proto"],
    deps = [
        ":camera_config_proto",
        ":camera_identifier_proto",
        ":camera_params_proto",
        ":camera_settings_proto",
        ":capture_result_proto",
        ":dimensions_proto",
        ":image_buffer_proto",
        ":post_processing_proto",
        "@com_google_googleapis//google/api:annotations_proto",
        "@com_google_protobuf//:duration_proto",
        "@intrinsic_apis//intrinsic/kubernetes/generator/proto:auth_options_proto",
        "@intrinsic_apis//intrinsic/logging/proto:context_proto",
        "@intrinsic_apis//intrinsic/math/proto:pose_proto",
        "@intrinsic_apis//intrinsic/platform/pubsub:storage_location",
    ],
)

cc_proto_library(
    name = "camera_service_cc_proto",
    deps = [":camera_service_proto"],
)

cc_grpc_library(
    name = "camera_service_cc_grpc",
    srcs = [":camera_service_proto"],
    grpc_only = True,
    deps = [":camera_service_cc_proto"],
)

go_proto_library(
    name = "camera_service_go_proto",
    compilers = [
        "@io_bazel_rules_go//proto:go_grpc_v2",
        "@io_bazel_rules_go//proto:go_proto",
        "@com_github_grpc_ecosystem_grpc_gateway_v2//protoc-gen-grpc-gateway:go_gen_grpc_gateway",
    ],
    importpath = "intrinsic/perception/proto/v1/camera_service_go_proto",
    protos = [":camera_service_proto"],
    deps = [
        ":camera_config_go_proto",
        ":camera_identifier_go_proto",
        ":camera_params_go_proto",
        ":camera_settings_go_proto",
        ":capture_result_go_proto",
        ":dimensions_go_proto",
        ":image_buffer_go_proto",
        ":post_processing_go_proto",
        "@intrinsic_apis//intrinsic/kubernetes/generator/proto:auth_options_go_proto",
        "@intrinsic_apis//intrinsic/logging/proto:context_go_proto",
        "@intrinsic_apis//intrinsic/math/proto:pose_go_proto",
        "@intrinsic_apis//intrinsic/platform/pubsub:storage_location_go_proto",
        "@org_golang_google_genproto_googleapis_api//annotations",
    ],
)

py_proto_library(
    name = "camera_service_py_pb2",
    deps = [":camera_service_proto"],
)

py_grpc_library(
    name = "camera_service_py_pb2_grpc",
    srcs = [":camera_service_proto"],
    deps = [":camera_service_py_pb2"],
)

proto_library(
    name = "camera_setup_proto",
    srcs = ["camera_setup.proto"],
    visibility = ["//visibility:public"],
)

cc_proto_library(
    name = "camera_setup_cc_proto",
    visibility = ["//visibility:public"],
    deps = [":camera_setup_proto"],
)

go_proto_library(
    name = "camera_setup_go_proto",
    importpath = "intrinsic/perception/proto/v1/camera_setup_go_proto",
    protos = [":camera_setup_proto"],
    visibility = ["//visibility:public"],
)

py_proto_library(
    name = "camera_setup_py_pb2",
    visibility = ["//visibility:public"],
    deps = [":camera_setup_proto"],
)

proto_library(
    name = "camera_to_robot_calibration",
    srcs = ["camera_to_robot_calibration.proto"],
    visibility = ["//visibility:public"],
    exports = [":camera_setup_proto"],
    deps = [
        ":camera_setup_proto",
        "@intrinsic_apis//intrinsic/math/proto:pose_proto",
    ],
)

cc_proto_library(
    name = "camera_to_robot_calibration_cc_proto",
    visibility = ["//visibility:public"],
    deps = ["camera_to_robot_calibration"],
)

go_proto_library(
    name = "camera_to_robot_calibration_go_proto",
    importpath = "intrinsic/perception/proto/v1/camera_to_robot_calibration_go_proto",
    protos = [":camera_to_robot_calibration"],
    visibility = ["//visibility:public"],
    deps = [
        ":camera_setup_go_proto",
        "@intrinsic_apis//intrinsic/math/proto:pose_go_proto",
    ],
)

py_proto_library(
    name = "camera_to_robot_calibration_py_pb2",
    visibility = ["//visibility:public"],
    deps = [":camera_to_robot_calibration"],
)

proto_library(
    name = "capture_data_proto",
    srcs = ["capture_data.proto"],
    visibility = ["//visibility:public"],
    deps = [
        "@intrinsic_apis//intrinsic/math/proto:pose_proto",
        "@intrinsic_apis//intrinsic/platform/pubsub:storage_location",
    ],
)

cc_proto_library(
    name = "capture_data_cc_proto",
    visibility = ["//visibility:public"],
    deps = [":capture_data_proto"],
)

go_proto_library(
    name = "capture_data_go_proto",
    importpath = "intrinsic/perception/proto/v1/capture_data_go_proto",
    protos = [":capture_data_proto"],
    visibility = ["//visibility:public"],
    deps = [
        "@intrinsic_apis//intrinsic/math/proto:pose_go_proto",
        "@intrinsic_apis//intrinsic/platform/pubsub:storage_location_go_proto",
    ],
)

py_proto_library(
    name = "capture_data_py_pb2",
    visibility = ["//visibility:public"],
    deps = [":capture_data_proto"],
)

proto_library(
    name = "pattern_detection_result_proto",
    srcs = ["pattern_detection_result.proto"],
    deps = ["@intrinsic_apis//intrinsic/perception/proto:vector_proto"],
)

cc_proto_library(
    name = "pattern_detection_result_cc_proto",
    deps = [":pattern_detection_result_proto"],
)

go_proto_library(
    name = "pattern_detection_result_go_proto",
    importpath = "intrinsic/perception/proto/v1/pattern_detection_result_go_proto",
    protos = [":pattern_detection_result_proto"],
    deps = [
        "@intrinsic_apis//intrinsic/perception/proto:vector_go_proto",
    ],
)

py_proto_library(
    name = "pattern_detection_result_py_pb2",
    deps = [":pattern_detection_result_proto"],
)

proto_library(
    name = "perception_model_proto",
    srcs = ["perception_model.proto"],
    deps = [
        "@intrinsic_apis//intrinsic/assets/data/proto/v1:referenced_data_proto",
        "@intrinsic_apis//intrinsic/perception/proto/pose_estimators/v1:one_shot_base_model_config_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:pose_estimation_config_proto",
    ],
)

cc_proto_library(
    name = "perception_model_cc_proto",
    deps = [":perception_model_proto"],
)

go_proto_library(
    name = "perception_model_go_proto",
    importpath = "intrinsic/perception/proto/v1/perception_model_go_proto",
    protos = [":perception_model_proto"],
    deps = [
        "@intrinsic_apis//intrinsic/assets/data/proto/v1:referenced_data_go_proto",
        "@intrinsic_apis//intrinsic/perception/proto/pose_estimators/v1:one_shot_base_model_config_go_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:pose_estimation_config_go_proto",
    ],
)

py_proto_library(
    name = "perception_model_py_pb2",
    deps = [":perception_model_proto"],
)

proto_library(
    name = "pose_estimate_proto",
    srcs = ["pose_estimate.proto"],
    deps = ["@intrinsic_apis//intrinsic/math/proto:pose_proto"],
)

cc_proto_library(
    name = "pose_estimate_cc_proto",
    deps = [":pose_estimate_proto"],
)

go_proto_library(
    name = "pose_estimate_go_proto",
    importpath = "intrinsic/perception/proto/v1/pose_estimate_go_proto",
    protos = [":pose_estimate_proto"],
    deps = [
        "@intrinsic_apis//intrinsic/math/proto:pose_go_proto",
    ],
)

py_proto_library(
    name = "pose_estimate_py_pb2",
    deps = [":pose_estimate_proto"],
)

proto_library(
    name = "pose_estimate_in_root_proto",
    srcs = ["pose_estimate_in_root.proto"],
    deps = ["@intrinsic_apis//intrinsic/math/proto:pose_proto"],
)

cc_proto_library(
    name = "pose_estimate_in_root_cc_proto",
    deps = [":pose_estimate_in_root_proto"],
)

go_proto_library(
    name = "pose_estimate_in_root_go_proto",
    importpath = "intrinsic/perception/proto/v1/pose_estimate_in_root_go_proto",
    protos = [":pose_estimate_in_root_proto"],
    deps = [
        "@intrinsic_apis//intrinsic/math/proto:pose_go_proto",
    ],
)

py_proto_library(
    name = "pose_estimate_in_root_py_pb2",
    deps = [":pose_estimate_in_root_proto"],
)

proto_library(
    name = "pose_range_proto",
    srcs = ["pose_range.proto"],
    visibility = ["//visibility:public"],
)

cc_proto_library(
    name = "pose_range_cc_proto",
    visibility = ["//visibility:public"],
    deps = [":pose_range_proto"],
)

go_proto_library(
    name = "pose_range_go_proto",
    importpath = "intrinsic/perception/proto/v1/pose_range_go_proto",
    protos = [":pose_range_proto"],
    visibility = ["//visibility:public"],
)

py_proto_library(
    name = "pose_range_py_pb2",
    visibility = ["//visibility:public"],
    deps = [":pose_range_proto"],
)

proto_library(
    name = "symmetry_proto",
    srcs = ["symmetry.proto"],
    deps = ["@intrinsic_apis//intrinsic/math/proto:vector3_proto"],
)

cc_proto_library(
    name = "symmetry_cc_proto",
    deps = [":symmetry_proto"],
)

go_proto_library(
    name = "symmetry_go_proto",
    importpath = "intrinsic/perception/proto/v1/symmetry_go_proto",
    protos = [":symmetry_proto"],
    deps = ["@intrinsic_apis//intrinsic/math/proto:vector3_go_proto"],
)

py_proto_library(
    name = "symmetry_py_pb2",
    deps = [":symmetry_proto"],
)

proto_library(
    name = "target_proto",
    srcs = ["target.proto"],
    deps = [
        ":charuco_pattern_proto",
        ":pose_range_proto",
        ":symmetry_proto",
        "@intrinsic_apis//intrinsic/math/proto:affine_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:scene_object_proto",
    ],
)

cc_proto_library(
    name = "target_cc_proto",
    deps = [":target_proto"],
)

go_proto_library(
    name = "target_go_proto",
    importpath = "intrinsic/perception/proto/v1/target_go_proto",
    protos = [":target_proto"],
    deps = [
        ":charuco_pattern_go_proto",
        ":pose_range_go_proto",
        ":symmetry_go_proto",
        "@intrinsic_apis//intrinsic/math/proto:affine_go_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:scene_object_go_proto",
    ],
)

py_proto_library(
    name = "target_py_pb2",
    deps = [":target_proto"],
)

proto_library(
    name = "pose_estimation_config",
    srcs = ["pose_estimation_config.proto"],
    deps = [
        ":intrinsic_params_proto",
        ":target_proto",
        "@com_google_protobuf//:any_proto",
    ],
)

cc_proto_library(
    name = "pose_estimation_config_cc_proto",
    deps = ["pose_estimation_config"],
)

go_proto_library(
    name = "pose_estimation_config_go_proto",
    importpath = "intrinsic/perception/proto/v1/pose_estimation_config_go_proto",
    protos = [":pose_estimation_config"],
    deps = [
        ":intrinsic_params_go_proto",
        ":target_go_proto",
    ],
)

py_proto_library(
    name = "pose_estimation_config_py_pb2",
    deps = [":pose_estimation_config"],
)

alias(
    name = "pose_estimation_config_proto",
    actual = ":pose_estimation_config",
)

proto_library(
    name = "pose_priors_proto",
    srcs = ["pose_priors.proto"],
    deps = [
        ":pose_estimate_proto",
        "@intrinsic_apis//intrinsic/geometry/proto:oriented_bounding_box_proto",
        "@intrinsic_apis//intrinsic/math/proto:pose_proto",
    ],
)

cc_proto_library(
    name = "pose_priors_cc_proto",
    deps = [":pose_priors_proto"],
)

go_proto_library(
    name = "pose_priors_go_proto",
    importpath = "intrinsic/perception/proto/v1/pose_priors_go_proto",
    protos = [":pose_priors_proto"],
    deps = [
        ":pose_estimate_go_proto",
        "@intrinsic_apis//intrinsic/geometry/proto:oriented_bounding_box_go_proto",
        "@intrinsic_apis//intrinsic/math/proto:pose_go_proto",
    ],
)

py_proto_library(
    name = "pose_priors_py_pb2",
    deps = [":pose_priors_proto"],
)

proto_library(
    name = "pose_estimation_service_proto",
    srcs = ["pose_estimation_service.proto"],
    deps = [
        ":capture_data_proto",
        ":capture_result_proto",
        ":pose_estimate_proto",
        ":pose_priors_proto",
        "@com_google_protobuf//:any_proto",
        "@com_google_protobuf//:duration_proto",
        "@com_google_protobuf//:empty_proto",
        "@intrinsic_apis//intrinsic/assets/proto:id_proto",
        "@intrinsic_apis//intrinsic/logging/proto:context_proto",
    ],
)

cc_proto_library(
    name = "pose_estimation_service_cc_proto",
    deps = [":pose_estimation_service_proto"],
)

cc_grpc_library(
    name = "pose_estimation_service_cc_grpc_proto",
    srcs = [":pose_estimation_service_proto"],
    grpc_only = True,
    deps = [":pose_estimation_service_cc_proto"],
)

go_proto_library(
    name = "pose_estimation_service_go_proto",
    compilers = [
        "@io_bazel_rules_go//proto:go_grpc_v2",
        "@io_bazel_rules_go//proto:go_proto",
        "@com_github_grpc_ecosystem_grpc_gateway_v2//protoc-gen-grpc-gateway:go_gen_grpc_gateway",
    ],
    importpath = "intrinsic/perception/proto/v1/pose_estimation_service_go_proto",
    protos = [":pose_estimation_service_proto"],
    deps = [
        ":capture_data_go_proto",
        ":capture_result_go_proto",
        ":pose_estimate_go_proto",
        ":pose_priors_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:id_go_proto",
        "@intrinsic_apis//intrinsic/logging/proto:context_go_proto",
        "@org_golang_google_protobuf//types/known/anypb",
        "@org_golang_google_protobuf//types/known/durationpb",
        "@org_golang_google_protobuf//types/known/emptypb",
    ],
)

py_proto_library(
    name = "pose_estimation_service_py_pb2",
    deps = [":pose_estimation_service_proto"],
)

py_grpc_library(
    name = "pose_estimation_service_py_pb2_grpc",
    srcs = [":pose_estimation_service_proto"],
    deps = [":pose_estimation_service_py_pb2"],
)

proto_library(
    name = "pose_estimator_id_proto",
    srcs = ["pose_estimator_id.proto"],
    visibility = ["//visibility:public"],
)

cc_proto_library(
    name = "pose_estimator_id_cc_proto",
    visibility = ["//visibility:public"],
    deps = [":pose_estimator_id_proto"],
)

go_proto_library(
    name = "pose_estimator_id_go_proto",
    importpath = "intrinsic/perception/proto/v1/pose_estimator_id_go_proto",
    protos = [":pose_estimator_id_proto"],
    visibility = ["//visibility:public"],
)

py_proto_library(
    name = "pose_estimator_id_py_pb2",
    visibility = ["//visibility:public"],
    deps = [":pose_estimator_id_proto"],
)

proto_library(
    name = "train_service_proto",
    srcs = ["train_service.proto"],
    deps = [
        ":pose_estimation_config",
        "@com_google_googleapis//google/api:annotations_proto",
        "@com_google_googleapis//google/longrunning:operations_proto",
        "@com_google_protobuf//:any_proto",
        "@com_google_protobuf//:empty_proto",
        "@com_google_protobuf//:timestamp_proto",
        "@intrinsic_apis//intrinsic/assets/proto:id_proto",
        "@intrinsic_apis//intrinsic/kubernetes/generator/proto:auth_options_proto",
        "@intrinsic_apis//intrinsic/util/status:extended_status_proto",
    ],
)

cc_proto_library(
    name = "train_service_cc_proto",
    deps = [":train_service_proto"],
)

cc_grpc_library(
    name = "train_service_cc_grpc_proto",
    srcs = [":train_service_proto"],
    grpc_only = True,
    deps = [":train_service_cc_proto"],
)

go_proto_library(
    name = "train_service_go_proto",
    compilers = [
        "@io_bazel_rules_go//proto:go_grpc_v2",
        "@io_bazel_rules_go//proto:go_proto",
        "@com_github_grpc_ecosystem_grpc_gateway_v2//protoc-gen-grpc-gateway:go_gen_grpc_gateway",
    ],
    importpath = "intrinsic/perception/proto/v1/train_service_go_proto",
    protos = [":train_service_proto"],
    deps = [
        ":pose_estimation_config_go_proto",
        "@com_google_cloud_go_longrunning//autogen/longrunningpb",
        "@intrinsic_apis//intrinsic/assets/proto:id_go_proto",
        "@intrinsic_apis//intrinsic/kubernetes/generator/proto:auth_options_go_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:scene_object_go_proto",
        "@intrinsic_apis//intrinsic/util/status:extended_status_go_proto",
        "@org_golang_google_genproto_googleapis_api//annotations",
        "@org_golang_google_protobuf//types/known/anypb",
        "@org_golang_google_protobuf//types/known/durationpb",
        "@org_golang_google_protobuf//types/known/emptypb",
        "@org_golang_google_protobuf//types/known/timestamppb",
    ],
)

py_proto_library(
    name = "train_service_py_pb2",
    deps = [":train_service_proto"],
)

py_grpc_library(
    name = "train_service_py_pb2_grpc",
    srcs = [":train_service_proto"],
    deps = [":train_service_py_pb2"],
)

alias(
    name = "camera_to_robot_calibration_proto",
    actual = ":camera_to_robot_calibration",
)
