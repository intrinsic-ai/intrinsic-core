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
load("//intrinsic_control/intrinsic/icon/hal/bzl:hardware_module_binary.bzl", "hardware_module_binary")
load("//intrinsic_control/intrinsic/icon/hal/bzl:hardware_module_image.bzl", "hardware_module_image")

package(default_visibility = ["//visibility:public"])

exports_files(
    srcs = [
        "loopback_module_config.textproto",
        "loopback_module_driven_config.textproto",
    ],
    visibility = ["//intrinsic:public_api_users"],
)

cc_library(
    name = "loopback_hardware_module",
    srcs = ["loopback_hardware_module.cc"],
    hdrs = ["loopback_hardware_module.h"],
    deps = [
        "//intrinsic_control/intrinsic/icon/control:realtime_clock_interface",
        "//intrinsic_control/intrinsic/icon/control/safety:safety_messages_fbs_cc",
        "//intrinsic_control/intrinsic/icon/control/safety:safety_messages_fbs_utils",
        "//intrinsic_control/intrinsic/icon/control/safety/extern:safety_status_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal:hardware_interface_handle",
        "//intrinsic_control/intrinsic/icon/hal:hardware_interface_registry",
        "//intrinsic_control/intrinsic/icon/hal:hardware_interface_traits",
        "//intrinsic_control/intrinsic/icon/hal:hardware_module_init_context",
        "//intrinsic_control/intrinsic/icon/hal:hardware_module_interface",
        "//intrinsic_control/intrinsic/icon/hal:hardware_module_registry",
        "//intrinsic_control/intrinsic/icon/hal:module_config",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:joint_command_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:joint_command_fbs_utils",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:joint_limits_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:joint_limits_fbs_utils",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:joint_state_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:joint_state_fbs_utils",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:payload_command_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:payload_command_utils",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:payload_state_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:payload_state_utils",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:robot_payload_utils",
        "//intrinsic_control/intrinsic/icon/utils:core_time",
        "//intrinsic_control/intrinsic/icon/utils:realtime_guard",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/utils:time",
        "//intrinsic_sdk/intrinsic/math:gaussian_noise",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/util/thread",
        "//intrinsic_sdk/intrinsic/util/thread:thread_options",
        "//intrinsic_sdk/intrinsic/util/thread:thread_utils",
        "//intrinsic_sdk/intrinsic/world/robot_payload:robot_payload_base",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/random",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@intrinsic_apis//intrinsic/icon/hardware_modules/loopback:loopback_config_cc_proto",
    ],
    alwayslink = 1,
)

hardware_module_binary(
    name = "loopback_hardware_module_main",
    hardware_module_lib = ":loopback_hardware_module",
)

hardware_module_image(
    name = "loopback_hardware_module_image",
    hardware_module_binary = "loopback_hardware_module_main",
)
