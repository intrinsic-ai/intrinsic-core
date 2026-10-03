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
load("//bazel:container.bzl", "container_image")

package(default_visibility = ["//visibility:public"])

# Async server code (callback-based)

cc_library(
    name = "pinch_gripper_server_async_callback_impl",
    srcs = ["pinch_gripper_server_async_callback_impl.cc"],
    hdrs = ["pinch_gripper_server_async_callback_impl.h"],
    deps = [
        "//intrinsic/resources/health:health_state_machine",
        "//intrinsic/resources/health:operational_status",
        "//intrinsic/util:proto_time",
        "//intrinsic/util/status:status_macros",
        "//intrinsic/util/status:status_macros_grpc",
        "//intrinsic_hardware/intrinsic/hardware/gripper:gripper_interface",
        "//intrinsic_hardware/intrinsic/hardware/gripper/robotiq:robotiq_gripper",
        "//intrinsic_hardware/intrinsic/hardware/gripper/service/proto:generic_pinch_gripper_configs_cc_proto",
        "//intrinsic_hardware/intrinsic/hardware/gripper/service/proto:pinch_gripper_server_cc_grpc_proto",
        "//intrinsic_hardware/intrinsic/hardware/gripper/service/proto:pinch_gripper_server_cc_proto",
        "//intrinsic_hardware/intrinsic/hardware/gripper/wsg32:wsg32_gripper",
        "//intrinsic_sdk/intrinsic/util/thread:thread_pool",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@com_github_grpc_grpc//:grpc++",
        "@intrinsic_apis//intrinsic/assets/services/proto/v1:service_state_cc_proto",
    ],
)

cc_library(
    name = "health_service",
    srcs = ["health_service.cc"],
    hdrs = ["health_service.h"],
    deps = [
        ":pinch_gripper_server_async_callback_impl",
        "//intrinsic/resources/proto:resource_health_cc_grpc_proto",
        "//intrinsic/resources/proto:resource_health_cc_proto",
        "@abseil-cpp//absl/strings",
        "@com_github_grpc_grpc//:grpc++",
        "@intrinsic_apis//intrinsic/assets/services/proto/v1:service_state_cc_grpc_proto",
        "@intrinsic_apis//intrinsic/assets/services/proto/v1:service_state_cc_proto",
    ],
)

cc_library(
    name = "generic_gripper_service",
    srcs = ["generic_gripper_service.cc"],
    hdrs = ["generic_gripper_service.h"],
    deps = [
        ":pinch_gripper_client",
        "//intrinsic/util/grpc:channel",
        "//intrinsic/util/grpc:connection_params",
        "//intrinsic/util/status:status_macros",
        "//intrinsic/util/status:status_macros_grpc",
        "//intrinsic_hardware/intrinsic/hardware/gripper/service/proto:pinch_gripper_server_cc_proto",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@com_github_grpc_grpc//:grpc++",
        "@intrinsic_apis//intrinsic/hardware/gripper:gripper_cc_proto",
        "@intrinsic_apis//intrinsic/hardware/gripper/service/proto:generic_gripper_cc_grpc_proto",
    ],
)

cc_binary(
    name = "pinch_gripper_server_async_callback_resource_main",
    srcs = ["pinch_gripper_server_async_callback_resource_main.cc"],
    deps = [
        ":generic_gripper_service",
        ":health_service",
        ":pinch_gripper_server_async_callback_impl",
        "//intrinsic/stats:opencensus",
        "//intrinsic/util:proto_time",
        "//intrinsic/util/grpc",
        "//intrinsic/util/proto:any",
        "//intrinsic_control/intrinsic/icon/release:file_helpers",
        "//intrinsic_control/intrinsic/icon/release/portable:init_intrinsic",
        "//intrinsic_hardware/intrinsic/hardware/gripper:gripper_equipment_cc_proto",
        "//intrinsic_hardware/intrinsic/hardware/gripper/service/sim:health_service",
        "//intrinsic_hardware/intrinsic/hardware/gripper/service/sim:pinch_gripper_service",
        "//intrinsic_hardware/intrinsic/hardware/gripper/service/sim:sim_pinch_gripper_impl",
        "//intrinsic_sdk/intrinsic/connect/cc/grpc:channel",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@intrinsic_apis//intrinsic/hardware/gripper/service/proto:generic_gripper_cc_grpc_proto",
        "@intrinsic_apis//intrinsic/resources/proto:resource_registry_cc_proto",
        "@intrinsic_apis//intrinsic/resources/proto:runtime_context_cc_proto",
    ],
)

cc_library(
    name = "generic_pinch_gripper_utils",
    srcs = ["generic_pinch_gripper_utils.cc"],
    hdrs = ["generic_pinch_gripper_utils.h"],
    deps = [
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:str_format",
        "@intrinsic_apis//intrinsic/hardware/gripper:gripper_cc_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:settings_cc_proto",
    ],
)

# Client code

cc_library(
    name = "pinch_gripper_client",
    srcs = ["pinch_gripper_client.cc"],
    hdrs = ["pinch_gripper_client.h"],
    deps = [
        "//intrinsic/util/grpc:channel",
        "//intrinsic/util/status:status_conversion_grpc",
        "//intrinsic/util/status:status_macros",
        "//intrinsic_hardware/intrinsic/hardware/gripper/service/proto:pinch_gripper_server_cc_grpc_proto",
        "//intrinsic_hardware/intrinsic/hardware/gripper/service/proto:pinch_gripper_server_cc_proto",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@com_github_grpc_grpc//:grpc++",
        "@intrinsic_apis//intrinsic/hardware/gripper:gripper_cc_proto",
    ],
)

container_image(
    name = "resource-pinch-gripper-server-image",
    base = "//intrinsic/kubernetes:base-image-oci",
    cmd = [
        "intrinsic/hardware/gripper/service/pinch_gripper_server_async_callback_resource_main",
    ],
    data_path = "/",
    env = {
        "LD_LIBRARY_PATH": ":".join([
            # NOTE we are only using the workspace_root here since package is empty.
            "/%s" % package_relative_label(label_name).workspace_root
            # Each typesupport library belongs to a separate RCR repository.
            for label_name in [
                "@libmodbus//:modbus_shared",
                "@rosidl_typesupport_introspection_c//:rosidl_typesupport_introspection_c",
                "@rosidl_typesupport_introspection_cpp//:rosidl_typesupport_introspection_cpp",
            ]
        ]),
    },
    files = [
        ":pinch_gripper_server_async_callback_resource_main",
        "@rosidl_typesupport_introspection_c",
        "@rosidl_typesupport_introspection_cpp",
    ],
    layers = [
        "//intrinsic_hardware/intrinsic/hardware/modbus_tcp:modbus-runfiles",
    ],
)
