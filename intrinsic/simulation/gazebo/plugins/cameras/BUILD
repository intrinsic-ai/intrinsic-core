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

# Camera-related plugins

load("//bazel:cc_macros.bzl", "cc_library")

package(
    default_visibility = ["//visibility:public"],
)

cc_library(
    name = "point_cloud_from_depth_buffer",
    srcs = ["point_cloud_from_depth_buffer.cc"],
    hdrs = ["point_cloud_from_depth_buffer.h"],
    deps = [
        "//intrinsic_perception/intrinsic/perception/core:dimensions",
        "//intrinsic_perception/intrinsic/perception/core:eigen_types",
        "//intrinsic_perception/intrinsic/perception/core:image",
        "//intrinsic_perception/intrinsic/perception/core:intrinsic_params",
    ],
)

cc_library(
    name = "camera_plugin",
    srcs = ["camera_plugin.cc"],
    hdrs = ["camera_plugin.h"],
    deps = [
        ":camera_service",
        ":camera_services",
        ":point_cloud_from_depth_buffer",
        ":simulation_camera_utils",
        ":triggered_camera_client",
        "//intrinsic_perception/intrinsic/perception/cameras:camera_identifier",
        "//intrinsic_perception/intrinsic/perception/cameras:image_source_interface",
        "//intrinsic_perception/intrinsic/perception/cameras:sensor_image",
        "//intrinsic_perception/intrinsic/perception/cameras:sensor_information",
        "//intrinsic_perception/intrinsic/perception/core:camera_params",
        "//intrinsic_perception/intrinsic/perception/core:compute_normals_least_sqr",
        "//intrinsic_perception/intrinsic/perception/core:dimensions",
        "//intrinsic_perception/intrinsic/perception/core:eigen_types",
        "//intrinsic_perception/intrinsic/perception/core:image_traits",
        "//intrinsic_perception/intrinsic/perception/core:intrinsic_params",
        "//intrinsic_perception/intrinsic/perception/core:pixel_type",
        "//intrinsic_perception/intrinsic/perception/proto_conversion/v1:camera_identifier",
        "//intrinsic_perception/intrinsic/perception/proto_conversion/v1:camera_params",
        "//intrinsic_perception/intrinsic/perception/proto_conversion/v1:camera_service",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:proto_conversion",
        "//intrinsic_sdk/intrinsic/platform/pubsub",
        "//intrinsic_sdk/intrinsic/platform/pubsub:publisher",
        "//intrinsic_sdk/intrinsic/util:macros",
        "//intrinsic_sdk/intrinsic/util/status:ret_check",
        "//intrinsic_sdk/intrinsic/util/status:return",
        "//intrinsic_sdk/intrinsic/util/status:status_builder",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//third_party/ros2/ros_interfaces/jazzy/sensor_msgs/msg:image_cc_proto",
        "//third_party/ros2/ros_interfaces/jazzy/std_msgs/msg:header_cc_proto",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
        "@boost.bimap",
        "@com_google_protobuf//:protobuf",
        "@gz-msgs",
        "@gz-sim",
        "@gz-transport",
        "@intrinsic_apis//intrinsic/math/proto:header_cc_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_config_cc_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_service_cc_proto",
        "@sdformat",
    ],
)

cc_library(
    name = "camera_plugin_register",
    srcs = [
        "camera_plugin_register.cc",
    ],
    deps = [
        ":camera_plugin",
        "@gz-plugin//:register",
        "@gz-sim",
    ],
    alwayslink = 1,
)

cc_library(
    name = "multi_camera",
    srcs = ["multi_camera_plugin.cc"],
    hdrs = ["multi_camera_plugin.h"],
    deps = [
        ":camera_service",
        ":camera_services",
        ":point_cloud_from_depth_buffer",
        ":simulation_camera_utils",
        ":triggered_camera_client",
        "//intrinsic_perception/intrinsic/perception/cameras:camera_identifier",
        "//intrinsic_perception/intrinsic/perception/cameras:sensor_image",
        "//intrinsic_perception/intrinsic/perception/cameras:sensor_information",
        "//intrinsic_perception/intrinsic/perception/core:camera_params",
        "//intrinsic_perception/intrinsic/perception/core:compute_normals_least_sqr",
        "//intrinsic_perception/intrinsic/perception/core:dimensions",
        "//intrinsic_perception/intrinsic/perception/core:eigen_types",
        "//intrinsic_perception/intrinsic/perception/core:image_traits",
        "//intrinsic_perception/intrinsic/perception/core:pixel_type",
        "//intrinsic_perception/intrinsic/perception/proto_conversion/v1:camera_identifier",
        "//intrinsic_perception/intrinsic/perception/proto_conversion/v1:camera_params",
        "//intrinsic_perception/intrinsic/perception/proto_conversion/v1:camera_service",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:proto_conversion",
        "//intrinsic_sdk/intrinsic/util/status:return",
        "//intrinsic_sdk/intrinsic/util/status:status_builder",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/container:node_hash_map",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@gz-msgs",
        "@gz-sim",
        "@gz-transport",
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_config_cc_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_service_cc_proto",
        "@sdformat",
    ],
)

cc_library(
    name = "multi_camera_plugin",
    srcs = [
        "multi_camera_plugin_register.cc",
    ],
    deps = [
        ":multi_camera",
        "@gz-plugin//:register",
        "@gz-sim",
    ],
    alwayslink = 1,
)

cc_library(
    name = "simulation_camera_utils",
    srcs = ["simulation_camera_utils.cc"],
    hdrs = ["simulation_camera_utils.h"],
    deps = [
        "//intrinsic_perception/intrinsic/perception/cameras:camera_identifier",
        "//intrinsic_perception/intrinsic/perception/core:camera_params",
        "//intrinsic_perception/intrinsic/perception/core:dimensions",
        "//intrinsic_perception/intrinsic/perception/core:intrinsic_params",
        "//intrinsic_perception/intrinsic/perception/core:pixel_type",
        "//intrinsic_perception/intrinsic/perception/proto_conversion/v1:camera_identifier",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@com_google_protobuf//:protobuf",
        "@gz-math",
        "@gz-math//eigen3",
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_identifier_cc_proto",
        "@sdformat",
    ],
)

cc_library(
    name = "simulation_camera_settings",
    srcs = ["simulation_camera_settings.cc"],
    hdrs = ["simulation_camera_settings.h"],
    deps = [
        "//intrinsic_perception/intrinsic/perception/cameras/genicam:feature_names",
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_settings_cc_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:intrinsic_params_cc_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:settings_cc_proto",
    ],
)

cc_library(
    name = "triggered_camera_client",
    srcs = ["triggered_camera_client.cc"],
    hdrs = ["triggered_camera_client.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/util:macros",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@gz-common//events",
        "@gz-msgs",
        "@gz-sim",
        "@gz-transport",
    ],
)

cc_library(
    name = "camera_service",
    srcs = ["camera_service.cc"],
    hdrs = ["camera_service.h"],
    deps = [
        ":simulation_camera_settings",
        "//intrinsic/util/proto:pb_hash",
        "//intrinsic_perception/intrinsic/perception/cameras:camera",
        "//intrinsic_perception/intrinsic/perception/cameras:camera_identifier",
        "//intrinsic_perception/intrinsic/perception/cameras:camera_setting",
        "//intrinsic_perception/intrinsic/perception/cameras:camera_setting_properties",
        "//intrinsic_perception/intrinsic/perception/cameras:capture_result",
        "//intrinsic_perception/intrinsic/perception/cameras:capture_result_helper",
        "//intrinsic_perception/intrinsic/perception/cameras:image_source_interface",
        "//intrinsic_perception/intrinsic/perception/cameras:sensor_image",
        "//intrinsic_perception/intrinsic/perception/cameras/genicam:feature_names",
        "//intrinsic_perception/intrinsic/perception/cameras/services/v1:camera_service_utils",
        "//intrinsic_perception/intrinsic/perception/core:camera_params",
        "//intrinsic_perception/intrinsic/perception/core:intrinsic_params",
        "//intrinsic_perception/intrinsic/perception/core:pixel_type",
        "//intrinsic_perception/intrinsic/perception/core:single_thread_executor",
        "//intrinsic_perception/intrinsic/perception/core:undistortion",
        "//intrinsic_perception/intrinsic/perception/logging:sensor_image",
        "//intrinsic_perception/intrinsic/perception/proto_conversion/v1:camera_config",
        "//intrinsic_perception/intrinsic/perception/proto_conversion/v1:camera_identifier",
        "//intrinsic_perception/intrinsic/perception/proto_conversion/v1:camera_settings",
        "//intrinsic_perception/intrinsic/perception/proto_conversion/v1:capture_result",
        "//intrinsic_perception/intrinsic/perception/proto_conversion/v1:post_processing",
        "//intrinsic_sdk/intrinsic/platform/pubsub",
        "//intrinsic_sdk/intrinsic/platform/pubsub:publisher",
        "//intrinsic_sdk/intrinsic/util/status:ret_check_grpc",
        "//intrinsic_sdk/intrinsic/util/status:status_builder",
        "//intrinsic_sdk/intrinsic/util/status:status_builder_grpc",
        "//intrinsic_sdk/intrinsic/util/status:status_conversion_grpc",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/util/status:status_macros_grpc",
        "//intrinsic_sdk/intrinsic/util/thread",
        "//intrinsic_sdk/intrinsic/util/thread:stop_token",
        "//third_party/ros2/ros_interfaces/jazzy/builtin_interfaces/msg:time_cc_proto",
        "//third_party/ros2/ros_interfaces/jazzy/sensor_msgs/msg:image_cc_proto",
        "//third_party/ros2/ros_interfaces/jazzy/std_msgs/msg:header_cc_proto",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/cleanup",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/functional:bind_front",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@com_github_grpc_grpc//:grpc++",
        "@com_github_grpc_grpc//:grpc++_reflection",  # Needed when using grpc_cli.
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_config_cc_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_identifier_cc_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_service_cc_grpc",
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_service_cc_proto",
    ],
)

cc_library(
    name = "camera_services",
    srcs = ["camera_services.cc"],
    hdrs = ["camera_services.h"],
    deps = [
        ":camera_service",
        "//intrinsic_sdk/intrinsic/util/thread",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:no_destructor",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/synchronization",
        "@com_github_grpc_grpc//:grpc",
        "@com_github_grpc_grpc//:grpc++",
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_service_cc_grpc",
    ],
)
