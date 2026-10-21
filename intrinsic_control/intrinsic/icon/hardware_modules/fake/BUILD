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
load("@com_google_protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("//bazel:cc_macros.bzl", "cc_library")
load("//bazel:protos.bzl", "proto_library")
load("//intrinsic_control/intrinsic/icon/hal/bzl:hardware_module_image.bzl", "hardware_module_image")
load("//intrinsic_control/intrinsic/icon/testing:cc_test_and_malloc_test.bzl", "cc_test_and_malloc_test")

package(
    default_visibility = ["//visibility:public"],
)

proto_library(
    name = "fake_module_config_proto",
    srcs = ["fake_module_config.proto"],
    deps = [
        "@intrinsic_apis//intrinsic/icon/proto:cart_space_proto",
        "@intrinsic_apis//intrinsic/icon/proto:io_block_proto",
        "@intrinsic_apis//intrinsic/icon/proto:part_status_proto",
        "@intrinsic_apis//intrinsic/icon/proto:safety_status_proto",
    ],
)

cc_proto_library(
    name = "fake_config_cc_proto",
    deps = [":fake_module_config_proto"],
)

proto_library(
    name = "dummy_grpc_service_proto",
    srcs = ["hardware_module_dummy_service.proto"],
    deps = [
        "@com_google_googleapis//google/rpc:code_proto",
        "@com_google_protobuf//:descriptor_proto",
    ],
)

cc_proto_library(
    name = "dummy_grpc_service_cc_proto",
    deps = [":dummy_grpc_service_proto"],
)

cc_grpc_library(
    name = "dummy_grpc_service_cc_grpc_proto",
    srcs = [":dummy_grpc_service_proto"],
    generate_mocks = True,
    grpc_only = True,
    deps = [":dummy_grpc_service_cc_proto"],
)

cc_library(
    name = "fake_module",
    srcs = ["fake_module.cc"],
    hdrs = ["fake_module.h"],
    deps = [
        ":dummy_grpc_service_cc_grpc_proto",
        ":dummy_grpc_service_cc_proto",
        ":fake_config_cc_proto",
        "//intrinsic_control/intrinsic/icon/control:realtime_clock_interface",
        "//intrinsic_control/intrinsic/icon/control/safety:safety_messages_fbs_cc",
        "//intrinsic_control/intrinsic/icon/control/safety/extern:safety_status_fbs_cc",
        "//intrinsic_control/intrinsic/icon/flatbuffers:control_types_utils",
        "//intrinsic_control/intrinsic/icon/flatbuffers:flatbuffer_utils",
        "//intrinsic_control/intrinsic/icon/flatbuffers:transform_types_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal:default_hardware_interfaces",
        "//intrinsic_control/intrinsic/icon/hal:hardware_interface_handle",
        "//intrinsic_control/intrinsic/icon/hal:hardware_interface_registry",
        "//intrinsic_control/intrinsic/icon/hal:hardware_module_init_context",
        "//intrinsic_control/intrinsic/icon/hal:hardware_module_inspection_cc_proto",
        "//intrinsic_control/intrinsic/icon/hal:hardware_module_interface",
        "//intrinsic_control/intrinsic/icon/hal:hardware_module_registry",
        "//intrinsic_control/intrinsic/icon/hal:module_config",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:adio_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:control_mode_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:force_sensor_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:force_torque_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:io_controller_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:joint_command_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:joint_limits_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:joint_state_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:payload_command_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:payload_state_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:rangefinder_fbs_cc",
        "//intrinsic_control/intrinsic/icon/proto:cart_space_conversion",
        "//intrinsic_control/intrinsic/icon/proto:safety_status_conversion",
        "//intrinsic_control/intrinsic/icon/testing:realtime_annotations",
        "//intrinsic_control/intrinsic/icon/utils:async_buffer",
        "//intrinsic_control/intrinsic/icon/utils:core_time",
        "//intrinsic_control/intrinsic/icon/utils:inspection_publisher",
        "//intrinsic_control/intrinsic/icon/utils:log",
        "//intrinsic_control/intrinsic/icon/utils:realtime_guard",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_sdk/intrinsic/util:invalid_until_set",
        "//intrinsic_sdk/intrinsic/util/status:status_conversion_grpc",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/util/thread",
        "//intrinsic_sdk/intrinsic/util/thread:thread_options",
        "//intrinsic_sdk/intrinsic/util/thread:thread_utils",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@com_github_google_flatbuffers//:runtime_cc",
        "@com_github_grpc_grpc//:grpc++",
        "@intrinsic_apis//intrinsic/icon/proto:part_status_cc_proto",
        "@intrinsic_apis//intrinsic/icon/proto:safety_status_cc_proto",
        "@intrinsic_apis//intrinsic/kinematics/types:joint_limits_cc_proto",
        "@magic_enum",
    ],
)

cc_library(
    name = "fake_module_register",
    srcs = ["fake_module_register.cc"],
    deps = [
        ":fake_module",
        "//intrinsic_control/intrinsic/icon/hal:hardware_module_registry",
    ],
    alwayslink = 1,
)

cc_test_and_malloc_test(
    name = "fake_module_test",
    srcs = ["fake_module_test.cc"],
    deps = [
        ":fake_config_cc_proto",
        ":fake_module",
        "//intrinsic_control/intrinsic/icon/control:realtime_clock_interface",
        "//intrinsic_control/intrinsic/icon/control/safety:safety_messages_fbs_cc",
        "//intrinsic_control/intrinsic/icon/control/safety/extern:safety_status_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal:default_hardware_interfaces",
        "//intrinsic_control/intrinsic/icon/hal:hardware_module_config_cc_proto",
        "//intrinsic_control/intrinsic/icon/hal:hardware_module_inspection_cc_proto",
        "//intrinsic_control/intrinsic/icon/hal:hardware_module_proxy",
        "//intrinsic_control/intrinsic/icon/hal:hardware_module_runtime",
        "//intrinsic_control/intrinsic/icon/hal:icon_state_register",
        "//intrinsic_control/intrinsic/icon/hal:module_config",
        "//intrinsic_control/intrinsic/icon/hal:realtime_clock",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:adio_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:control_mode_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:force_torque_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:icon_state_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:io_controller_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:joint_command_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:joint_state_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:rangefinder_fbs_cc",
        "//intrinsic_control/intrinsic/icon/interprocess/shared_memory_manager",
        "//intrinsic_control/intrinsic/icon/interprocess/shared_memory_manager/testing:unique_segment_name",
        "//intrinsic_control/intrinsic/icon/testing:malloc_test",
        "//intrinsic_control/intrinsic/icon/utils:core_time",
        "//intrinsic_control/intrinsic/icon/utils:current_cycle",
        "//intrinsic_control/intrinsic/icon/utils:inspection_publisher",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_matchers",
        "//intrinsic_sdk/intrinsic/platform/pubsub",
        "//intrinsic_sdk/intrinsic/util/proto:any",
        "//intrinsic_sdk/intrinsic/util/proto:parse_text_proto",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_cel_cpp//internal:testing_no_main",
        "@com_google_googletest//:gtest",
    ],
)

hardware_module_image(
    name = "fake_module_image",
    hardware_module_lib = ":fake_module_register",
)
