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
load("//bazel:cc_macros.bzl", "cc_library", "cc_test")
load("//bazel:protos.bzl", "proto_library")

package(
    default_visibility = [
        "//visibility:public",
    ],
)

proto_library(
    name = "opcua_gpio_service_config_proto",
    srcs = ["opcua_gpio_service_config.proto"],
    visibility = ["//intrinsic:intrinsic_internal"],
)

cc_proto_library(
    name = "opcua_gpio_service_config_cc_proto",
    visibility = ["//intrinsic:intrinsic_internal"],
    deps = [":opcua_gpio_service_config_proto"],
)

proto_library(
    name = "icon_gpio_service_config_proto",
    srcs = ["icon_gpio_service_config.proto"],
    visibility = ["//intrinsic:intrinsic_internal"],
)

cc_proto_library(
    name = "icon_gpio_service_config_cc_proto",
    visibility = ["//intrinsic:intrinsic_internal"],
    deps = [":icon_gpio_service_config_proto"],
)

cc_library(
    name = "gpio_service_proto_utils",
    srcs = ["gpio_service_proto_utils.cc"],
    hdrs = ["gpio_service_proto_utils.h"],
    visibility = ["//intrinsic:public_api_users"],
    deps = [
        "@abseil-cpp//absl/container:flat_hash_map",
        "@intrinsic_apis//intrinsic/hardware/gpio/v1:gpio_service_cc_proto",
        "@intrinsic_apis//intrinsic/hardware/gpio/v1:signal_cc_proto",
    ],
)

cc_test(
    name = "gpio_service_proto_utils_test",
    srcs = ["gpio_service_proto_utils_test.cc"],
    deps = [
        ":gpio_service_proto_utils",
        "@abseil-cpp//absl/strings",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "icon_gpio_service",
    srcs = ["icon_gpio_service.cc"],
    hdrs = ["icon_gpio_service.h"],
    visibility = ["//intrinsic:intrinsic_internal"],
    deps = [
        ":gpio_service_proto_utils",
        ":icon_gpio_service_config_cc_proto",
        "//intrinsic_control/intrinsic/icon/actions:adio_info",
        "//intrinsic_control/intrinsic/icon/cc_client:client",
        "//intrinsic_control/intrinsic/icon/cc_client:condition",
        "//intrinsic_control/intrinsic/icon/cc_client:operational_status",
        "//intrinsic_control/intrinsic/icon/cc_client:session",
        "//intrinsic_control/intrinsic/icon/cc_client:state_variable_path",
        "//intrinsic_control/intrinsic/icon/common:id_types",
        "//intrinsic_sdk/intrinsic/logging/proto:log_item_cc_proto",
        "//intrinsic_sdk/intrinsic/platform/pubsub",
        "//intrinsic_sdk/intrinsic/platform/pubsub:publisher",
        "//intrinsic_sdk/intrinsic/platform/pubsub:subscription",
        "//intrinsic_sdk/intrinsic/util/grpc:channel_interface",
        "//intrinsic_sdk/intrinsic/util/status:status_conversion_grpc",
        "//intrinsic_sdk/intrinsic/util/status:status_conversion_rpc",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/util/status:status_macros_grpc",
        "//intrinsic_sdk/intrinsic/util/thread",
        "//intrinsic_sdk/intrinsic/util/thread:util",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/container:btree",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/container:node_hash_map",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_protobuf//:protobuf_lite",
        "@intrinsic_apis//intrinsic/hardware/gpio/v1:gpio_service_cc_grpc_proto",
        "@intrinsic_apis//intrinsic/hardware/gpio/v1:gpio_service_cc_proto",
        "@intrinsic_apis//intrinsic/hardware/gpio/v1:signal_cc_proto",
        "@intrinsic_apis//intrinsic/icon/proto:io_block_cc_proto",
        "@intrinsic_apis//intrinsic/icon/proto:part_status_cc_proto",
    ],
)

cc_library(
    name = "opcua_gpio_data_types",
    srcs = ["opcua_gpio_data_types.cc"],
    hdrs = ["opcua_gpio_data_types.h"],
    deps = [
        "//intrinsic_hardware/intrinsic/hardware/opcua:opcua_raii_types",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@intrinsic_apis//intrinsic/hardware/gpio/v1:gpio_service_cc_proto",
        "@intrinsic_apis//intrinsic/hardware/gpio/v1:signal_cc_proto",
        "@open62541",
        "@open62541//:types",
    ],
)

cc_library(
    name = "opcua_node_map",
    hdrs = ["opcua_node_map.h"],
    deps = [
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/strings",
        "@open62541",
    ],
)

cc_library(
    name = "opcua_gpio_service",
    srcs = ["opcua_gpio_service.cc"],
    hdrs = ["opcua_gpio_service.h"],
    deps = [
        ":gpio_service_proto_utils",
        ":opcua_gpio_data_types",
        ":opcua_gpio_service_config_cc_proto",
        ":opcua_node_map",
        "//intrinsic_hardware/intrinsic/hardware/opcua:opcua_client",
        "//intrinsic_hardware/intrinsic/hardware/opcua:opcua_raii_types",
        "//intrinsic_sdk/intrinsic/platform/pubsub",
        "//intrinsic_sdk/intrinsic/platform/pubsub:publisher",
        "//intrinsic_sdk/intrinsic/util:proto_time",
        "//intrinsic_sdk/intrinsic/util/status:status_conversion_grpc",
        "//intrinsic_sdk/intrinsic/util/status:status_conversion_rpc",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/util/status:status_macros_grpc",
        "//intrinsic_sdk/intrinsic/util/thread",
        "//intrinsic_sdk/intrinsic/util/thread:util",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/container:node_hash_map",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@com_github_grpc_grpc//:grpc++",
        "@intrinsic_apis//intrinsic/hardware/gpio/v1:gpio_service_cc_grpc_proto",
        "@intrinsic_apis//intrinsic/hardware/gpio/v1:gpio_service_cc_proto",
        "@open62541",
        "@open62541//:types",
    ],
)

cc_test(
    name = "opcua_gpio_service_test",
    srcs = ["opcua_gpio_service_test.cc"],
    deps = [
        ":gpio_client",
        ":opcua_gpio_service",
        "//intrinsic_hardware/intrinsic/hardware/opcua:opcua_server",
        "//intrinsic_sdk/intrinsic/connect/cc/grpc:channel",
        "//intrinsic_sdk/intrinsic/util/grpc",
        "//intrinsic_sdk/intrinsic/util/grpc:connection_params",
        "//intrinsic_sdk/intrinsic/util/status:status_conversion_grpc",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@com_github_grpc_grpc//:grpc++",
        "@com_github_inazarenko_protobuf_matchers//protobuf-matchers",
        "@com_google_cel_cpp//internal:testing_no_main",
        "@com_google_googletest//:gtest_main",
        "@intrinsic_apis//intrinsic/hardware/gpio/v1:gpio_service_cc_grpc_proto",
        "@intrinsic_apis//intrinsic/hardware/gpio/v1:gpio_service_cc_proto",
        "@open62541",
    ],
)

cc_test(
    name = "opcua_gpio_service_delayed_connection_test",
    srcs = ["opcua_gpio_service_delayed_connection_test.cc"],
    deps = [
        ":gpio_client",
        ":gpio_service_proto_utils",
        ":opcua_gpio_service",
        ":opcua_gpio_service_config_cc_proto",
        "//intrinsic_hardware/intrinsic/hardware/opcua:opcua_server",
        "//intrinsic_sdk/intrinsic/util/grpc",
        "//intrinsic_sdk/intrinsic/util/grpc:connection_params",
        "//intrinsic_sdk/intrinsic/util/testing:status_payload_matchers",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@com_github_grpc_grpc//:grpc++",
        "@com_github_inazarenko_protobuf_matchers//protobuf-matchers",
        "@com_google_cel_cpp//internal:testing_no_main",
        "@com_google_googletest//:gtest_main",
        "@intrinsic_apis//intrinsic/hardware/gpio/v1:gpio_service_cc_grpc_proto",
        "@intrinsic_apis//intrinsic/hardware/gpio/v1:gpio_service_cc_proto",
        "@open62541",
    ],
)

cc_library(
    name = "gpio_service_factory",
    srcs = ["gpio_service_factory.cc"],
    hdrs = ["gpio_service_factory.h"],
    visibility = ["//intrinsic:intrinsic_internal"],
    deps = [
        ":icon_gpio_service",
        ":icon_gpio_service_config_cc_proto",
        ":opcua_gpio_service",
        ":opcua_gpio_service_config_cc_proto",
        "//intrinsic_sdk/intrinsic/util/grpc:channel",
        "//intrinsic_sdk/intrinsic/util/grpc:connection_params",
        "//intrinsic_sdk/intrinsic/util/proto:any",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/hardware/gpio/v1:gpio_service_cc_grpc_proto",
    ],
)

cc_library(
    name = "gpio_client",
    srcs = ["gpio_client.cc"],
    hdrs = ["gpio_client.h"],
    visibility = ["//intrinsic:public_api_users"],
    deps = [
        ":gpio_service_proto_utils",
        "//intrinsic_sdk/intrinsic/connect/cc/grpc:channel",
        "//intrinsic_sdk/intrinsic/platform/pubsub",
        "//intrinsic_sdk/intrinsic/platform/pubsub:subscription",
        "//intrinsic_sdk/intrinsic/util/grpc",
        "//intrinsic_sdk/intrinsic/util/grpc:channel",
        "//intrinsic_sdk/intrinsic/util/grpc:channel_interface",
        "//intrinsic_sdk/intrinsic/util/grpc:connection_params",
        "//intrinsic_sdk/intrinsic/util/status:status_conversion_grpc",
        "//intrinsic_sdk/intrinsic/util/status:status_conversion_rpc",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@com_github_grpc_grpc//:grpc++",
        "@intrinsic_apis//intrinsic/hardware/gpio/v1:gpio_service_cc_grpc_proto",
        "@intrinsic_apis//intrinsic/hardware/gpio/v1:gpio_service_cc_proto",
    ],
)

cc_test(
    name = "gpio_client_test",
    srcs = ["gpio_client_test.cc"],
    deps = [
        ":gpio_client",
        ":gpio_service_proto_utils",
        "//intrinsic_sdk/intrinsic/platform/pubsub",
        "//intrinsic_sdk/intrinsic/platform/pubsub:subscription",
        "//intrinsic_sdk/intrinsic/util/grpc",
        "//intrinsic_sdk/intrinsic/util/grpc:connection_params",
        "//intrinsic_sdk/intrinsic/util/status:status_conversion_grpc",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/util/thread",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/time",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_cel_cpp//internal:testing_no_main",
        "@com_google_googletest//:gtest_main",
        "@intrinsic_apis//intrinsic/hardware/gpio/v1:gpio_service_cc_grpc_proto",
        "@intrinsic_apis//intrinsic/hardware/gpio/v1:gpio_service_cc_proto",
    ],
)
