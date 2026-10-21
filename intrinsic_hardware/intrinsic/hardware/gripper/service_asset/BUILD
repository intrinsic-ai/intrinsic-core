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

load("@com_google_protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("//bazel:cc_macros.bzl", "cc_binary", "cc_library")
load("//bazel:cc_oci_image.bzl", "cc_oci_image")
load("//bazel:protos.bzl", "proto_library")
load("//intrinsic_sdk/intrinsic/assets/services/build_defs:services.bzl", "intrinsic_service")

package(default_visibility = [
    "//visibility:public",
])

proto_library(
    name = "gripper_service_signals_proto",
    srcs = ["gripper_service_signals.proto"],
    visibility = ["//intrinsic:intrinsic_internal"],
)

cc_proto_library(
    name = "gripper_service_signals_cc_proto",
    visibility = ["//intrinsic:intrinsic_internal"],
    deps = [":gripper_service_signals_proto"],
)

proto_library(
    name = "pinch_gripper_realtime_control_service_config_proto",
    srcs = ["pinch_gripper_realtime_control_service_config.proto"],
    visibility = ["//intrinsic:intrinsic_internal"],
    deps = [":gripper_service_signals_proto"],
)

cc_proto_library(
    name = "pinch_gripper_realtime_control_service_config_cc_proto",
    visibility = ["//intrinsic:intrinsic_internal"],
    deps = [":pinch_gripper_realtime_control_service_config_proto"],
)

proto_library(
    name = "pinch_gripper_opcua_service_config_proto",
    srcs = ["pinch_gripper_opcua_service_config.proto"],
    visibility = ["//intrinsic:intrinsic_internal"],
    deps = [":gripper_service_signals_proto"],
)

cc_proto_library(
    name = "pinch_gripper_opcua_service_config_cc_proto",
    visibility = ["//intrinsic:intrinsic_internal"],
    deps = [":pinch_gripper_opcua_service_config_proto"],
)

proto_library(
    name = "suction_gripper_realtime_control_service_config_proto",
    srcs = ["suction_gripper_realtime_control_service_config.proto"],
    visibility = ["//intrinsic:intrinsic_internal"],
    deps = [":gripper_service_signals_proto"],
)

cc_proto_library(
    name = "suction_gripper_realtime_control_service_config_cc_proto",
    visibility = ["//intrinsic:intrinsic_internal"],
    deps = [":suction_gripper_realtime_control_service_config_proto"],
)

proto_library(
    name = "suction_gripper_opcua_service_config_proto",
    srcs = ["suction_gripper_opcua_service_config.proto"],
    visibility = ["//intrinsic:intrinsic_internal"],
    deps = [":gripper_service_signals_proto"],
)

cc_proto_library(
    name = "suction_gripper_opcua_service_config_cc_proto",
    visibility = ["//intrinsic:intrinsic_internal"],
    deps = [":suction_gripper_opcua_service_config_proto"],
)

cc_library(
    name = "gripper_service_utils",
    srcs = ["gripper_service_utils.cc"],
    hdrs = ["gripper_service_utils.h"],
    visibility = ["//intrinsic:intrinsic_internal"],
    deps = [
        ":gripper_service_signals_cc_proto",
        ":pinch_gripper_opcua_service_config_cc_proto",
        ":pinch_gripper_realtime_control_service_config_cc_proto",
        ":suction_gripper_opcua_service_config_cc_proto",
        ":suction_gripper_realtime_control_service_config_cc_proto",
        "//intrinsic_hardware/intrinsic/hardware/gpio:icon_gpio_service_config_cc_proto",
        "//intrinsic_hardware/intrinsic/hardware/gpio:opcua_gpio_service_config_cc_proto",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status:statusor",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/hardware/gpio/v1:signal_cc_proto",
        "@intrinsic_apis//intrinsic/hardware/gripper/eoat:eoat_service_cc_proto",
        "@intrinsic_apis//intrinsic/hardware/gripper/eoat:gripper_config_cc_proto",
        "@intrinsic_apis//intrinsic/resources/proto:runtime_context_cc_proto",
    ],
)

cc_binary(
    name = "gripper_service_main",
    srcs = ["gripper_service_main.cc"],
    deps = [
        ":gripper_service_utils",
        ":pinch_gripper_opcua_service_config_cc_proto",
        ":pinch_gripper_realtime_control_service_config_cc_proto",
        ":suction_gripper_opcua_service_config_cc_proto",
        ":suction_gripper_realtime_control_service_config_cc_proto",
        "//intrinsic/stats:opencensus",
        "//intrinsic_control/intrinsic/icon/release:file_helpers",
        "//intrinsic_control/intrinsic/icon/release/portable:init_intrinsic",
        "//intrinsic_hardware/intrinsic/hardware/gripper/eoat:gripper_services",
        "//intrinsic_sdk/intrinsic/util/grpc",
        "//intrinsic_sdk/intrinsic/util/proto:any",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/synchronization",
        "@com_github_grpc_grpc//:grpc++",
        "@intrinsic_apis//intrinsic/hardware/gripper/eoat:eoat_service_cc_proto",
        "@intrinsic_apis//intrinsic/resources/proto:runtime_context_cc_proto",
    ],
)

cc_oci_image(
    name = "gripper-service-image",
    binary = ":gripper_service_main",
    data_path = "/",
)

intrinsic_service(
    name = "pinch_gripper_realtime_control_service",
    default_config = "pinch_gripper_realtime_control_service_default_config.pbtxt",
    images = [
        ":gripper-service-image.tar",
    ],
    manifest = ":pinch_gripper_realtime_control_service_manifest.textproto",
    deps = [
        ":pinch_gripper_realtime_control_service_config_proto",
        "//intrinsic/resources/proto:resource_health_proto",
        "@intrinsic_apis//intrinsic/assets/services/proto/v1:service_state_proto",
        "@intrinsic_apis//intrinsic/hardware/gpio/v1:gpio_service_proto",
        "@intrinsic_apis//intrinsic/hardware/gripper/eoat:eoat_service_proto",
        "@intrinsic_apis//intrinsic/hardware/gripper/service/proto:generic_gripper_proto",
    ],
)
