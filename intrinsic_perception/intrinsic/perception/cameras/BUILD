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

load("//bazel:cc_macros.bzl", "cc_library")

package(default_visibility = [
    "//visibility:public",
])

cc_library(
    name = "camera",
    srcs = ["camera.cc"],
    hdrs = ["camera.h"],
    deps = [
        ":camera_config",
        ":camera_factory",
        ":camera_identifier",
        ":camera_lister",
        ":camera_setting",
        ":camera_setting_access",
        ":camera_setting_properties",
        ":capture_result",
        ":image_source_interface",
        ":remote_image_source",
        ":sensor_information",
        "//intrinsic/assets:interface_utils",
        "//intrinsic/util/grpc:connection_cache",
        "//intrinsic/util/grpc:connection_params",
        "//intrinsic/util/status:status_builder",
        "//intrinsic/util/status:status_conversion_grpc",
        "//intrinsic/util/status:status_macros",
        "//intrinsic_control/intrinsic/icon/release:grpc_time_support",
        "//intrinsic_perception/intrinsic/perception/proto_conversion/v1:camera_config",
        "//intrinsic_perception/intrinsic/perception/proto_conversion/v1:camera_identifier",
        "//intrinsic_sdk/intrinsic/connect/cc/grpc:channel",
        "//intrinsic_sdk/intrinsic/skills/cc:skill_utils",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:no_destructor",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/functional:any_invocable",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@com_github_grpc_grpc//:grpc++",
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_config_cc_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_config_service_cc_grpc",
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_service_cc_grpc",
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_service_cc_proto",
        "@intrinsic_apis//intrinsic/resources/proto:resource_handle_cc_proto",
    ],
)

cc_library(
    name = "camera_config",
    srcs = ["camera_config.cc"],
    hdrs = ["camera_config.h"],
    deps = [
        ":camera_setting",
        "//intrinsic_perception/intrinsic/perception/core:camera_params",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "camera_drivers",
    deps = [
        ":default_camera_drivers",
    ],
    alwayslink = 1,
)

cc_library(
    name = "camera_factory",
    srcs = ["camera_factory.cc"],
    hdrs = ["camera_factory.h"],
    deps = [
        ":camera_identifier",
        ":file_image_source",
        ":image_source",
        "//intrinsic/util/status:status_builder",
        "//intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/functional:overload",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "camera_identifier",
    srcs = ["camera_identifier.cc"],
    hdrs = ["camera_identifier.h"],
    deps = [
        "//intrinsic_perception/intrinsic/perception/core:image_file_reference",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/strings:string_view",
    ],
)

cc_library(
    name = "camera_lister",
    srcs = ["camera_lister.cc"],
    hdrs = ["camera_lister.h"],
    deps = [
        ":camera_identifier",
        ":image_source_lister",
        "//intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_library(
    name = "camera_setting",
    srcs = ["camera_setting.cc"],
    hdrs = ["camera_setting.h"],
    deps = ["@abseil-cpp//absl/functional:overload"],
)

cc_library(
    name = "camera_setting_access",
    hdrs = ["camera_setting_access.h"],
)

cc_library(
    name = "camera_setting_properties",
    srcs = ["camera_setting_properties.cc"],
    hdrs = ["camera_setting_properties.h"],
    deps = [
        "@abseil-cpp//absl/functional:overload",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "camera_setting_utils",
    srcs = ["camera_setting_utils.cc"],
    hdrs = ["camera_setting_utils.h"],
    deps = [
        ":camera_setting",
        ":camera_setting_access",
        ":camera_setting_properties",
        "//intrinsic/util/status:status_builder",
        "//intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status:statusor",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_library(
    name = "capture_result",
    hdrs = ["capture_result.h"],
    deps = [
        ":camera_config",
        ":sensor_image",
        "//intrinsic_perception/intrinsic/perception/core:post_processing",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/time",
    ],
)

cc_library(
    name = "capture_result_helper",
    srcs = ["capture_result_helper.cc"],
    hdrs = ["capture_result_helper.h"],
    deps = [
        ":capture_result",
        ":sensor_image",
        "//intrinsic/util/status:ret_check",
        "//intrinsic/util/status:status_macros",
        "//intrinsic_perception/intrinsic/perception/core:camera_params",
        "//intrinsic_perception/intrinsic/perception/core:conversion",
        "//intrinsic_perception/intrinsic/perception/core:dimensions",
        "//intrinsic_perception/intrinsic/perception/core:distortion_params",
        "//intrinsic_perception/intrinsic/perception/core:eigen_types",
        "//intrinsic_perception/intrinsic/perception/core:image",
        "//intrinsic_perception/intrinsic/perception/core:image_traits",
        "//intrinsic_perception/intrinsic/perception/core:intrinsic_params",
        "//intrinsic_perception/intrinsic/perception/core:operators",
        "//intrinsic_perception/intrinsic/perception/core:post_processing",
        "//intrinsic_perception/intrinsic/perception/core:range_tools",
        "//intrinsic_perception/intrinsic/perception/core:rectangle",
        "//intrinsic_perception/intrinsic/perception/core:undistortion",
        "//intrinsic_perception/intrinsic/perception/proto_conversion:eigen_conversions",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@opencv//:imgproc",
    ],
)

cc_library(
    name = "default_camera_drivers",
    deps = [
        ":file_image_source",
        ":genicam_image_source",
        ":genicam_image_source_lister",
    ],
    alwayslink = 1,
)

cc_library(
    name = "file_image_source",
    srcs = ["file_image_source.cc"],
    hdrs = [
        "file_image_source.h",
    ],
    deps = [
        ":camera_identifier",
        ":camera_setting",
        ":camera_setting_utils",
        ":capture_result",
        ":image_source",
        ":sensor_image",
        ":sensor_information",
        "//intrinsic/util/status:ret_check",
        "//intrinsic/util/status:status_builder",
        "//intrinsic/util/status:status_macros",
        "//third_party/imported/cpp_libraries/clock",
        "//third_party/strings_numbers:sort",
        "//intrinsic_perception/intrinsic/perception/cameras/genicam:feature_names",
        "//intrinsic_perception/intrinsic/perception/core:core_io",
        "//intrinsic_perception/intrinsic/perception/core:dimensions",
        "//intrinsic_perception/intrinsic/perception/core:image_file_reference",
        "//intrinsic_perception/intrinsic/perception/core:image_traits",
        "//intrinsic_perception/intrinsic/perception/core:pixel_type",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/functional:overload",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/time",
        "@magic_enum",
        "@or_tools//ortools/base:path",
    ],
    alwayslink = 1,  # Required for go/registerer
)

cc_library(
    name = "genicam_image_source",
    srcs = ["genicam_image_source.cc"],
    hdrs = ["genicam_image_source.h"],
    deps = [
        ":camera_identifier",
        ":camera_setting",
        ":camera_setting_access",
        ":camera_setting_properties",
        ":camera_setting_utils",
        ":capture_result",
        ":image_source",
        ":sensor_image",
        ":sensor_information",
        ":software_hdr",
        "//intrinsic/stats:scoped_span",
        "//intrinsic/util:version",
        "//intrinsic/util/status:ret_check",
        "//intrinsic/util/status:status_builder",
        "//intrinsic/util/status:status_macros",
        "//intrinsic_perception/intrinsic/perception/cameras/aravis:aravis_glib_utils",
        "//intrinsic_perception/intrinsic/perception/cameras/aravis:aravis_ptp_utils",
        "//intrinsic_perception/intrinsic/perception/cameras/aravis:aravis_software_trigger",
        "//intrinsic_perception/intrinsic/perception/cameras/aravis:aravis_utils",
        "//intrinsic_perception/intrinsic/perception/cameras/aravis:aravis_wrapper",
        "//intrinsic_perception/intrinsic/perception/cameras/genicam:feature_names",
        "//intrinsic_perception/intrinsic/perception/cameras/sensor_pixel_size",
        "//intrinsic_perception/intrinsic/perception/core:camera_params",
        "//intrinsic_perception/intrinsic/perception/core:dimensions",
        "//intrinsic_perception/intrinsic/perception/core:hdr_utils",
        "//intrinsic_perception/intrinsic/perception/core:image",
        "//intrinsic_perception/intrinsic/perception/core:image_traits",
        "//intrinsic_perception/intrinsic/perception/core:pixel_type",
        "//intrinsic_perception/intrinsic/perception/core:range_tools",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/util/thread:sysinfo",
        "//intrinsic_sdk/intrinsic/util/time:deadline_timeout",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:no_destructor",
        "@abseil-cpp//absl/cleanup",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/functional:overload",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
    ],
    alwayslink = 1,  # Required for go/registerer
)

cc_library(
    name = "genicam_image_source_lister",
    srcs = ["genicam_image_source_lister.cc"],
    hdrs = ["genicam_image_source_lister.h"],
    deps = [
        ":camera_identifier",
        ":image_source_lister",
        "//intrinsic_perception/intrinsic/perception/cameras/aravis:aravis_wrapper",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status:statusor",
    ],
    alwayslink = 1,  # Required for go/registerer
)

cc_library(
    name = "image_source",
    srcs = ["image_source.cc"],
    hdrs = ["image_source.h"],
    deps = [
        ":camera_identifier",
        ":camera_setting",
        ":camera_setting_access",
        ":camera_setting_properties",
        ":capture_result",
        ":capture_result_helper",
        ":image_source_interface",
        ":sensor_image",
        ":sensor_information",
        "//intrinsic/stats:scoped_span",
        "//intrinsic/util/status:ret_check",
        "//intrinsic/util/status:status_builder",
        "//intrinsic/util/status:status_macros",
        "//intrinsic/util/thread",
        "//intrinsic_perception/intrinsic/perception/cameras/genicam:feature_names",
        "//intrinsic_perception/intrinsic/perception/core:camera_params",
        "//intrinsic_perception/intrinsic/perception/core:coordinate",
        "//intrinsic_perception/intrinsic/perception/core:dimensions",
        "//intrinsic_perception/intrinsic/perception/core:range_tools",
        "//intrinsic_perception/intrinsic/perception/core:undistortion",
        "//intrinsic_sdk/intrinsic/util/thread:stop_token",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:no_destructor",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/functional:any_invocable",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@cppregpattern//:registry",
    ],
)

cc_library(
    name = "image_source_interface",
    hdrs = ["image_source_interface.h"],
    deps = [
        ":camera_setting",
        ":camera_setting_access",
        ":camera_setting_properties",
        ":capture_result",
        ":sensor_information",
        "//intrinsic/util:string_type",
        "@abseil-cpp//absl/functional:any_invocable",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "image_source_lister",
    hdrs = [
        "image_source_lister.h",
    ],
    deps = [
        ":camera_identifier",
        "@abseil-cpp//absl/base:no_destructor",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/status:statusor",
        "@cppregpattern//:registry",
    ],
)

cc_library(
    name = "remote_image_source",
    srcs = ["remote_image_source.cc"],
    hdrs = ["remote_image_source.h"],
    deps = [
        ":camera_identifier",
        ":camera_setting",
        ":camera_setting_access",
        ":camera_setting_properties",
        ":capture_result",
        ":image_source_interface",
        ":sensor_information",
        "//intrinsic/platform/pubsub",
        "//intrinsic/util:proto_time",
        "//intrinsic/util/proto:repeated_field_util",
        "//intrinsic/util/status:ret_check",
        "//intrinsic/util/status:status_builder",
        "//intrinsic/util/status:status_conversion_grpc",
        "//intrinsic/util/status:status_macros",
        "//intrinsic/util/status:status_macros_grpc",
        "//intrinsic_control/intrinsic/icon/release:grpc_time_support",
        "//intrinsic_perception/intrinsic/perception/proto_conversion/v1:camera_config",
        "//intrinsic_perception/intrinsic/perception/proto_conversion/v1:camera_identifier",
        "//intrinsic_perception/intrinsic/perception/proto_conversion/v1:camera_service",
        "//intrinsic_perception/intrinsic/perception/proto_conversion/v1:camera_settings",
        "//intrinsic_perception/intrinsic/perception/proto_conversion/v1:capture_result",
        "//intrinsic_perception/intrinsic/perception/proto_conversion/v1:post_processing",
        "//intrinsic_sdk/intrinsic/connect/cc/grpc:channel",
        "//intrinsic_sdk/intrinsic/platform/pubsub:subscription",
        "//intrinsic_sdk/intrinsic/util/time:deadline_timeout",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@com_github_grpc_grpc//:grpc++",
        "@intrinsic_apis//intrinsic/assets/services/proto/v1:service_state_cc_grpc_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_service_cc_grpc",
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_service_cc_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:capture_result_cc_proto",
    ],
)

cc_library(
    name = "ros_camera_drivers",
    deps = [
        ":ros_image_source",
        ":ros_image_source_lister",
    ],
    alwayslink = 1,
)

cc_library(
    name = "ros_image_source",
    srcs = [
        "ros_image_source.cc",
    ],
    hdrs = [
        "ros_image_source.h",
    ],
    copts = ["-fexceptions"],  # ROS can throw exceptions.
    features = ["-use_header_modules"],  # Incompatible with -fexceptions.
    deps = [
        ":camera_identifier",
        ":camera_setting",
        ":camera_setting_access",
        ":camera_setting_properties",
        ":camera_setting_utils",
        ":capture_result",
        ":image_source",
        ":sensor_image",
        ":sensor_information",
        "//intrinsic/platform/pubsub",
        "//intrinsic/util/status:ret_check",
        "//intrinsic/util/status:status_builder",
        "//intrinsic/util/status:status_macros",
        "//intrinsic_perception/intrinsic/perception/cameras/ros:snapshot_interfaces_cpp",
        "//intrinsic_perception/intrinsic/perception/core:camera_params",
        "//intrinsic_perception/intrinsic/perception/core:dimensions",
        "//intrinsic_perception/intrinsic/perception/core:distortion_params",
        "//intrinsic_perception/intrinsic/perception/core:image",
        "//intrinsic_perception/intrinsic/perception/core:image_traits",
        "//intrinsic_perception/intrinsic/perception/core:intrinsic_params",
        "//intrinsic_perception/intrinsic/perception/core:io_conversions",
        "//intrinsic_perception/intrinsic/perception/core:pixel_type",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/platform/pubsub:pubsub_ros",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:btree",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/functional:overload",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
        "@ros2_common_interfaces//:cpp_geometry_msgs",
        "@ros2_common_interfaces//:cpp_sensor_msgs",
        "@ros2_rcl_interfaces//:cpp_rcl_interfaces",
        "@ros2_rclcpp//:rclcpp",
        "@ros2_rcutils//:rcutils",
        "@ros2_rmw//:rmw",
        "@ros2_rosidl//:rosidl_runtime_cpp",
    ],
    alwayslink = 1,  # Required for go/registerer
)

cc_library(
    name = "ros_image_source_lister",
    srcs = ["ros_image_source_lister.cc"],
    hdrs = ["ros_image_source_lister.h"],
    copts = ["-fexceptions"],  # ROS can throw exceptions.
    features = ["-use_header_modules"],  # Incompatible with -fexceptions.
    deps = [
        ":camera_identifier",
        ":image_source_lister",
        "//intrinsic/assets/proto:installed_assets_cc_grpc_proto",
        "//intrinsic/assets/proto:installed_assets_cc_proto",
        "//intrinsic/platform/pubsub",
        "//intrinsic/util/grpc",
        "//intrinsic/util/status:status_conversion_grpc",
        "//intrinsic/util/status:status_macros",
        "//intrinsic_perception/intrinsic/perception/cameras/ros:snapshot_interfaces_cpp",
        "//intrinsic_sdk/intrinsic/connect/cc/grpc:channel",
        "//intrinsic_sdk/intrinsic/platform/pubsub:pubsub_ros",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@com_github_grpc_grpc//:grpc++",
        "@intrinsic_apis//intrinsic/assets/proto:view_cc_proto",
        "@ros2_rclcpp//:rclcpp",
        "@ros2_rmw//:rmw",
    ],
    alwayslink = 1,  # Required for go/registerer
)

cc_library(
    name = "sensor_image",
    srcs = ["sensor_image.cc"],
    hdrs = ["sensor_image.h"],
    deps = [
        "//intrinsic_perception/intrinsic/perception/core:camera_params",
        "//intrinsic_perception/intrinsic/perception/core:dimensions",
        "//intrinsic_perception/intrinsic/perception/core:image",
        "//intrinsic_perception/intrinsic/perception/core:image_traits",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/time",
    ],
)

cc_library(
    name = "sensor_information",
    srcs = ["sensor_information.cc"],
    hdrs = ["sensor_information.h"],
    deps = [
        "//intrinsic_perception/intrinsic/perception/core:camera_params",
        "//intrinsic_perception/intrinsic/perception/core:dimensions",
        "//intrinsic_perception/intrinsic/perception/core:pixel_type",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "software_hdr",
    srcs = ["software_hdr.cc"],
    hdrs = ["software_hdr.h"],
    deps = [
        ":camera_setting",
        ":camera_setting_access",
        ":camera_setting_properties",
        ":camera_setting_utils",
        "//intrinsic/util/status:status_builder",
        "//intrinsic/util/status:status_macros",
        "//intrinsic_perception/intrinsic/perception/cameras/genicam:feature_names",
        "//intrinsic_perception/intrinsic/perception/core:hdr_utils",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/container:btree",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/time",
        "@magic_enum",
    ],
)
