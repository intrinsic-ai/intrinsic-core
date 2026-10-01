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

load("//bazel:cc_macros.bzl", "cc_binary", "cc_library")
load("//bazel:cc_oci_image.bzl", "cc_oci_image")
load("//intrinsic/assets/services/build_defs:services.bzl", "intrinsic_service")

package(default_visibility = [
    "//visibility:public",
])

cc_library(
    name = "calibration_service_impl",
    srcs = ["calibration_service_impl.cc"],
    hdrs = ["calibration_service_impl.h"],
    deps = [
        "//intrinsic/assets/dependencies:utils_cc",
        "//intrinsic/util:proto_time",
        "//intrinsic/util/grpc",
        "//intrinsic/util/grpc:connection_params",
        "//intrinsic/util/status:status_macros",
        "//intrinsic/util/status:status_macros_grpc",
        "//intrinsic/world/proto:object_world_service_cc_grpc_proto",
        "//intrinsic_perception/intrinsic/perception/calibration:camera_to_robot_calibration",
        "//intrinsic_perception/intrinsic/perception/calibration:charuco_pattern_detector_register",  # fixdeps: keep go/registerer
        "//intrinsic_perception/intrinsic/perception/calibration:charuco_pattern_utils",
        "//intrinsic_perception/intrinsic/perception/calibration:image_quality_check",
        "//intrinsic_perception/intrinsic/perception/calibration:intrinsic_calibration",
        "//intrinsic_perception/intrinsic/perception/calibration:intrinsic_calibration_utils",
        "//intrinsic_perception/intrinsic/perception/calibration:pattern_detector",
        "//intrinsic_perception/intrinsic/perception/calibration:stereo_calibration_cc",
        "//intrinsic_perception/intrinsic/perception/cameras:camera",
        "//intrinsic_perception/intrinsic/perception/cameras:capture_result",
        "//intrinsic_perception/intrinsic/perception/cameras:capture_result_helper",
        "//intrinsic_perception/intrinsic/perception/core:camera_params",
        "//intrinsic_perception/intrinsic/perception/core:config_name_utils",
        "//intrinsic_perception/intrinsic/perception/core:dimensions",
        "//intrinsic_perception/intrinsic/perception/core:drawing",
        "//intrinsic_perception/intrinsic/perception/core:eigen_types",
        "//intrinsic_perception/intrinsic/perception/core:image",
        "//intrinsic_perception/intrinsic/perception/core:image_traits",
        "//intrinsic_perception/intrinsic/perception/core:operators",
        "//intrinsic_perception/intrinsic/perception/core:post_processing",
        "//intrinsic_perception/intrinsic/perception/core:range_tools",
        "//intrinsic_perception/intrinsic/perception/core:single_thread_executor",
        "//intrinsic_perception/intrinsic/perception/logging:sensor_image",
        "//intrinsic_perception/intrinsic/perception/proto_conversion/v1:camera_params",
        "//intrinsic_perception/intrinsic/perception/proto_conversion/v1:capture_result",
        "//intrinsic_perception/intrinsic/perception/proto_conversion/v1:dimensions",
        "//intrinsic_perception/intrinsic/perception/proto_conversion/v1:distortion_params",
        "//intrinsic_perception/intrinsic/perception/proto_conversion/v1:intrinsic_params",
        "//intrinsic_perception/intrinsic/perception/proto_conversion/v1:post_processing",
        "//intrinsic_sdk/intrinsic/connect/cc/grpc:channel",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:proto_conversion",
        "//intrinsic_sdk/intrinsic/platform/pubsub:kvstore",
        "//intrinsic_sdk/intrinsic/skills/cc:skill_utils",
        "//intrinsic_sdk/intrinsic/world/objects:kinematic_object",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_client",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_ids",
        "//intrinsic_sdk/intrinsic/world/objects:transform_node",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/cleanup",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_googleapis//google/longrunning:longrunning_cc_grpc",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/assets/proto:asset_deployment_cc_grpc_proto",
        "@intrinsic_apis//intrinsic/assets/proto:asset_deployment_cc_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:asset_instances_cc_grpc",
        "@intrinsic_apis//intrinsic/assets/proto/v1:resolved_dependency_cc_proto",
        "@intrinsic_apis//intrinsic/logging/proto:context_cc_proto",
        "@intrinsic_apis//intrinsic/math/proto:pose_cc_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:calibration_service_cc_grpc_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:calibration_service_cc_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_params_cc_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_setup_cc_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_to_robot_calibration_cc_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:pattern_detection_config_cc_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:pattern_detection_result_cc_proto",
        "@intrinsic_apis//intrinsic/platform/pubsub:storage_location_cc_proto",
        "@intrinsic_apis//intrinsic/resources/proto:resource_handle_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:object_world_refs_cc_proto",
    ],
)

cc_binary(
    name = "calibration_service_main",
    srcs = ["calibration_service_main.cc"],
    deps = [
        ":calibration_service_impl",
        "//intrinsic/assets/dependencies:utils_cc",
        "//intrinsic/logging/proto:logger_service_cc_grpc",
        "//intrinsic/util/proto:any",
        "//intrinsic/util/status:status_macros",
        "//intrinsic/world/proto:object_world_service_cc_grpc_proto",
        "//intrinsic_control/intrinsic/icon/release:file_helpers",
        "//intrinsic_control/intrinsic/icon/release/portable:init_intrinsic",
        "//intrinsic_sdk/intrinsic/connect/cc/grpc:channel",
        "//intrinsic_sdk/intrinsic/logging:data_logger_client",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
        "@com_github_grpc_grpc//:grpc++",
        "@com_github_grpc_grpc//:grpc_security_base",
        "@com_google_googleapis//google/longrunning:longrunning_cc_grpc",
        "@intrinsic_apis//intrinsic/assets/proto/v1:asset_instances_cc_grpc",
        "@intrinsic_apis//intrinsic/perception/proto/v1:calibration_service_cc_proto",
        "@intrinsic_apis//intrinsic/resources/proto:runtime_context_cc_proto",
    ],
)

cc_oci_image(
    name = "calibration-service-image",
    binary = ":calibration_service_main",
    data_path = "/",
)

intrinsic_service(
    name = "calibration_service",
    default_config = "calibration_service_default_config.textproto",
    images = [
        ":calibration-service-image.tar",
    ],
    manifest = ":calibration_service_manifest.textproto",
    deps = [
        "@intrinsic_apis//intrinsic/perception/proto/v1:calibration_service_proto",
    ],
)
