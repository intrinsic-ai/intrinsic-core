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
load(":defs.bzl", "ros_camera_service_workdir")

package(default_visibility = [
    "//visibility:public",
])

cc_library(
    name = "camera_config_service",
    srcs = ["camera_config_service.cc"],
    hdrs = ["camera_config_service.h"],
    deps = [
        "//intrinsic/util/status:status_macros_grpc",
        "//intrinsic_perception/intrinsic/perception/cameras:camera",
        "//intrinsic_perception/intrinsic/perception/cameras:camera_config",
        "//intrinsic_perception/intrinsic/perception/cameras:camera_identifier",
        "@abseil-cpp//absl/base:nullability",
        "@com_github_grpc_grpc//:grpc++",
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_config_cc_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_config_service_cc_grpc",
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_config_service_cc_proto",
    ],
)

cc_library(
    name = "camera_dynamic_reconfiguration_service",
    srcs = ["camera_dynamic_reconfiguration_service.cc"],
    hdrs = ["camera_dynamic_reconfiguration_service.h"],
    deps = [
        "//intrinsic/util/status:status_macros_grpc",
        "//intrinsic_perception/intrinsic/perception/cameras:camera",
        "//intrinsic_perception/intrinsic/perception/cameras:camera_config",
        "//intrinsic_perception/intrinsic/perception/cameras:camera_identifier",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/log",
        "@com_github_grpc_grpc//:grpc++",
        "@intrinsic_apis//intrinsic/assets/services/proto/v1:dynamic_reconfiguration_cc_grpc_proto",
        "@intrinsic_apis//intrinsic/assets/services/proto/v1:dynamic_reconfiguration_cc_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_config_cc_proto",
    ],
)

cc_library(
    name = "camera_health_service",
    srcs = ["camera_health_service.cc"],
    hdrs = ["camera_health_service.h"],
    deps = [
        "//intrinsic/util/status:status_builder_grpc",
        "//intrinsic/util/status:status_conversion_grpc",
        "//intrinsic/util/status:status_macros_grpc",
        "//intrinsic_perception/intrinsic/perception/cameras:camera",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@com_github_grpc_grpc//:grpc++",  # buildcleaner: keep
        "@com_github_grpc_grpc//:grpc++_reflection",  # This is needed when using grpc_cli.
        "@intrinsic_apis//intrinsic/assets/services/proto/v1:service_state_cc_grpc_proto",
        "@intrinsic_apis//intrinsic/assets/services/proto/v1:service_state_cc_proto",
    ],
)

cc_library(
    name = "camera_service_utils",
    srcs = ["camera_service_utils.cc"],
    hdrs = ["camera_service_utils.h"],
    deps = [
        "//intrinsic/platform/pubsub",
        "//intrinsic/stats:scoped_span",
        "//intrinsic/util/status:status_macros",
        "//intrinsic_perception/intrinsic/perception/cameras:capture_result",
        "//intrinsic_perception/intrinsic/perception/proto_conversion/v1:capture_result",
        "//intrinsic_perception/intrinsic/perception/proto_conversion/v1:post_processing",
        "//intrinsic_sdk/intrinsic/platform/pubsub:kvstore",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_service_cc_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:capture_result_cc_proto",
    ],
)

cc_library(
    name = "camera_service_impl",
    srcs = ["camera_service_impl.cc"],
    hdrs = ["camera_service_impl.h"],
    deps = [
        ":camera_service_utils",
        "//intrinsic/platform/pubsub",
        "//intrinsic/production/external/googleinit",
        "//intrinsic/stats:scoped_span",
        "//intrinsic/util:proto_time",
        "//intrinsic/util/status:ret_check",
        "//intrinsic/util/status:status_builder_grpc",
        "//intrinsic/util/status:status_macros",
        "//intrinsic/util/status:status_macros_grpc",
        "//intrinsic_perception/intrinsic/perception/cameras:camera",
        "//intrinsic_perception/intrinsic/perception/cameras:camera_identifier",
        "//intrinsic_perception/intrinsic/perception/cameras:camera_setting",
        "//intrinsic_perception/intrinsic/perception/cameras:camera_setting_access",
        "//intrinsic_perception/intrinsic/perception/cameras:camera_setting_properties",
        "//intrinsic_perception/intrinsic/perception/cameras:capture_result",
        "//intrinsic_perception/intrinsic/perception/cameras:image_source_interface",
        "//intrinsic_perception/intrinsic/perception/cameras:sensor_information",
        "//intrinsic_perception/intrinsic/perception/core:single_thread_executor",
        "//intrinsic_perception/intrinsic/perception/logging:sensor_image",
        "//intrinsic_perception/intrinsic/perception/proto_conversion/v1:camera_config",
        "//intrinsic_perception/intrinsic/perception/proto_conversion/v1:camera_identifier",
        "//intrinsic_perception/intrinsic/perception/proto_conversion/v1:camera_service",
        "//intrinsic_perception/intrinsic/perception/proto_conversion/v1:camera_settings",
        "//intrinsic_perception/intrinsic/perception/proto_conversion/v1:capture_result",
        "//intrinsic_perception/intrinsic/perception/proto_conversion/v1:post_processing",
        "//intrinsic_sdk/intrinsic/platform/pubsub:publisher",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@com_github_grpc_grpc//:grpc++",  # buildcleaner: keep
        "@com_github_grpc_grpc//:grpc++_reflection",  # This is needed when using grpc_cli.
        "@com_google_protobuf//:differencer",
        "@com_google_protobuf//:protobuf_lite",
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_config_cc_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_drivers_cc_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_identifier_cc_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_service_cc_grpc",
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_service_cc_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_settings_cc_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:capture_result_cc_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:image_buffer_cc_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:post_processing_cc_proto",
        "@io_opencensus_cpp//opencensus/stats",
    ],
)

cc_library(
    name = "camera_server_main",
    srcs = ["camera_server_main.cc"],
    deps = [
        ":camera_config_service",
        ":camera_dynamic_reconfiguration_service",
        ":camera_health_service",
        ":camera_service_impl",
        "//intrinsic/stats:opencensus",
        "//intrinsic/util/grpc",
        "//intrinsic/util/status:status_builder",
        "//intrinsic/util/status:status_macros",
        "//intrinsic_control/intrinsic/icon/release:file_helpers",
        "//intrinsic_control/intrinsic/icon/release/portable:init_intrinsic",
        "//intrinsic_perception/intrinsic/perception/cameras:camera",
        "//intrinsic_perception/intrinsic/perception/proto_conversion/v1:camera_config",
        "//intrinsic_sdk/intrinsic/connect/cc/grpc:channel",
        "//intrinsic_sdk/intrinsic/logging:data_logger_client",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
        "@com_github_grpc_grpc//:grpc++",
        "@com_github_grpc_grpc//:grpc++_reflection",  # This is needed when using grpc_cli.
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_config_cc_proto",
        "@intrinsic_apis//intrinsic/resources/proto:runtime_context_cc_proto",
    ],
)

cc_binary(
    name = "camera_server",
    malloc = "@tcmalloc//tcmalloc",
    deps = [
        ":camera_server_main",
        "//intrinsic_perception/intrinsic/perception/cameras:camera_drivers",
    ],
)

cc_oci_image(
    name = "camera-server-image",
    base = "//intrinsic/kubernetes:base-image-cc-oci",
    binary = ":camera_server",
    cmd = [
        "/intrinsic_perception/intrinsic/perception/cameras/services/v1/camera_server",
        "--opencensus_tracing=true",
        "--data_logger_grpc_service_address=logger.app-intrinsic-base.svc.cluster.local:8080",
    ],
    data_path = "/",
)

cc_binary(
    name = "ros_camera_server",
    deps = [
        ":camera_server_main",
        "//intrinsic_perception/intrinsic/perception/cameras:ros_camera_drivers",
    ],
)

cc_oci_image(
    name = "ros_camera_service_image",
    base = "//intrinsic/kubernetes:base-image-cc-oci",
    binary = ":ros_camera_server",
    cmd = [
        "/intrinsic_perception/intrinsic/perception/cameras/services/v1/ros_camera_server",
        "--opencensus_tracing=true",
        "--data_logger_grpc_service_address=logger.app-intrinsic-base.svc.cluster.local:8080",
    ],
    data_path = "/",
    workdir = ros_camera_service_workdir(package_relative_label(":ros_camera_server")),
)
