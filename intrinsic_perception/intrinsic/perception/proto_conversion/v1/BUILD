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
    name = "camera_config",
    srcs = ["camera_config.cc"],
    hdrs = ["camera_config.h"],
    deps = [
        ":camera_identifier",
        ":camera_params",
        ":camera_settings",
        "//intrinsic_perception/intrinsic/perception/cameras:camera_config",
        "//intrinsic_perception/intrinsic/perception/cameras:camera_identifier",
        "//intrinsic_perception/intrinsic/perception/cameras:camera_setting",
        "//intrinsic_perception/intrinsic/perception/core:camera_params",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:proto_conversion",
        "//intrinsic_sdk/intrinsic/util/proto:repeated_field_util",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/status:statusor",
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_config_cc_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:sensor_config_cc_proto",
    ],
)

cc_library(
    name = "camera_drivers",
    srcs = ["camera_drivers.cc"],
    hdrs = ["camera_drivers.h"],
    deps = [
        ":image_file_reference",
        "//intrinsic_perception/intrinsic/perception/cameras:camera_identifier",
        "//intrinsic_perception/intrinsic/perception/core:image_file_reference",
        "@abseil-cpp//absl/functional:overload",
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_drivers_cc_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:image_file_reference_cc_proto",
    ],
)

cc_library(
    name = "camera_identifier",
    srcs = ["camera_identifier.cc"],
    hdrs = ["camera_identifier.h"],
    deps = [
        ":camera_drivers",
        "//intrinsic_perception/intrinsic/perception/cameras:camera_identifier",
        "@abseil-cpp//absl/functional:overload",
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_identifier_cc_proto",
    ],
)

cc_library(
    name = "camera_params",
    srcs = ["camera_params.cc"],
    hdrs = ["camera_params.h"],
    deps = [
        ":distortion_params",
        ":intrinsic_params",
        "//intrinsic_perception/intrinsic/perception/core:camera_params",
        "//intrinsic_perception/intrinsic/perception/core:distortion_params",
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_params_cc_proto",
    ],
)

cc_library(
    name = "camera_service",
    srcs = ["camera_service.cc"],
    hdrs = ["camera_service.h"],
    deps = [
        ":camera_params",
        ":dimensions",
        ":image_buffer",
        "//intrinsic_perception/intrinsic/perception/cameras:sensor_information",
        "//intrinsic_perception/intrinsic/perception/core:camera_params",
        "//intrinsic_perception/intrinsic/perception/core:dimensions",
        "//intrinsic_perception/intrinsic/perception/core:pixel_type",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:proto_conversion",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status:statusor",
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_service_cc_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:image_buffer_cc_proto",
    ],
)

cc_library(
    name = "camera_settings",
    srcs = ["camera_settings.cc"],
    hdrs = ["camera_settings.h"],
    deps = [
        ":settings",
        "//intrinsic_perception/intrinsic/perception/cameras:camera_setting",
        "//intrinsic_perception/intrinsic/perception/cameras:camera_setting_access",
        "//intrinsic_perception/intrinsic/perception/cameras:camera_setting_properties",
        "@abseil-cpp//absl/functional:overload",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_settings_cc_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:settings_cc_proto",
    ],
)

cc_library(
    name = "capture_result",
    srcs = ["capture_result.cc"],
    hdrs = ["capture_result.h"],
    deps = [
        ":sensor_image",
        "//intrinsic_perception/intrinsic/perception/cameras:capture_result",
        "//intrinsic_perception/intrinsic/perception/cameras:sensor_image",
        "//intrinsic_perception/intrinsic/perception/core:encoding",
        "//intrinsic_sdk/intrinsic/util:proto_time",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/time",
        "@intrinsic_apis//intrinsic/perception/proto/v1:capture_result_cc_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:sensor_image_cc_proto",
    ],
)

cc_library(
    name = "dimensions",
    srcs = ["dimensions.cc"],
    hdrs = ["dimensions.h"],
    deps = [
        "//intrinsic_perception/intrinsic/perception/core:dimensions",
        "@intrinsic_apis//intrinsic/perception/proto/v1:dimensions_cc_proto",
    ],
)

cc_library(
    name = "distortion_params",
    srcs = ["distortion_params.cc"],
    hdrs = ["distortion_params.h"],
    deps = [
        "//intrinsic_perception/intrinsic/perception/core:distortion_params",
        "@intrinsic_apis//intrinsic/perception/proto/v1:distortion_params_cc_proto",
    ],
)

cc_library(
    name = "pixel_type",
    srcs = ["pixel_type.cc"],
    hdrs = ["pixel_type.h"],
    deps = [
        "//intrinsic_perception/intrinsic/perception/core:pixel_type",
        "@intrinsic_apis//intrinsic/perception/proto/v1:image_buffer_cc_proto",
    ],
)

cc_library(
    name = "image_buffer",
    srcs = ["image_buffer.cc"],
    hdrs = ["image_buffer.h"],
    deps = [
        ":dimensions",
        ":pixel_type",
        "//intrinsic_perception/intrinsic/perception/core:color_gradient",
        "//intrinsic_perception/intrinsic/perception/core:conversion",
        "//intrinsic_perception/intrinsic/perception/core:encoding",
        "//intrinsic_perception/intrinsic/perception/core:image",
        "//intrinsic_perception/intrinsic/perception/core:image_traits",
        "//intrinsic_perception/intrinsic/perception/core:opencv_wrapper",
        "//intrinsic_perception/intrinsic/perception/core:operators",
        "//intrinsic_perception/intrinsic/perception/core:pixel_type",
        "//intrinsic_sdk/intrinsic/util/status:ret_check",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/strings:string_view",
        "@intrinsic_apis//intrinsic/perception/proto/v1:dimensions_cc_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:image_buffer_cc_proto",
        "@libyuv",
        "@opencv//:core",
        "@opencv//:imgcodecs",
        "@opencv//:imgproc",
    ],
)

cc_library(
    name = "image_file_reference",
    srcs = ["image_file_reference.cc"],
    hdrs = ["image_file_reference.h"],
    deps = [
        ":pixel_type",
        "//intrinsic_perception/intrinsic/perception/core:image_file_reference",
        "@intrinsic_apis//intrinsic/perception/proto/v1:image_file_reference_cc_proto",
    ],
)

cc_library(
    name = "intrinsic_params",
    srcs = ["intrinsic_params.cc"],
    hdrs = ["intrinsic_params.h"],
    deps = [
        ":dimensions",
        "//intrinsic_perception/intrinsic/perception/core:intrinsic_params",
        "@intrinsic_apis//intrinsic/perception/proto/v1:intrinsic_params_cc_proto",
    ],
)

cc_library(
    name = "post_processing",
    srcs = ["post_processing.cc"],
    hdrs = ["post_processing.h"],
    deps = [
        ":dimensions",
        ":image_buffer",
        "//intrinsic_perception/intrinsic/perception/core:coordinate",
        "//intrinsic_perception/intrinsic/perception/core:encoding",
        "//intrinsic_perception/intrinsic/perception/core:post_processing",
        "//intrinsic_perception/intrinsic/perception/core:rectangle",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@com_google_protobuf//:protobuf_lite",
        "@intrinsic_apis//intrinsic/perception/proto/v1:post_processing_cc_proto",
    ],
)

cc_library(
    name = "sensor_image",
    srcs = ["sensor_image.cc"],
    hdrs = ["sensor_image.h"],
    deps = [
        ":camera_params",
        ":image_buffer",
        "//intrinsic_perception/intrinsic/perception/cameras:sensor_image",
        "//intrinsic_perception/intrinsic/perception/core:camera_params",
        "//intrinsic_perception/intrinsic/perception/core:encoding",
        "//intrinsic_perception/intrinsic/perception/core:image",
        "//intrinsic_perception/intrinsic/perception/core:image_traits",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:proto_conversion",
        "//intrinsic_sdk/intrinsic/util:proto_time",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/time",
        "@intrinsic_apis//intrinsic/perception/proto/v1:image_buffer_cc_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:sensor_config_cc_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:sensor_image_cc_proto",
    ],
)

cc_library(
    name = "settings",
    srcs = ["settings.cc"],
    hdrs = ["settings.h"],
    deps = [
        "//intrinsic_perception/intrinsic/perception/cameras:camera_setting_properties",
        "@intrinsic_apis//intrinsic/perception/proto/v1:settings_cc_proto",
    ],
)
