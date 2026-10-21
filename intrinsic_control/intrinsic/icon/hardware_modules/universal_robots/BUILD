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
load("//bazel:cc_macros.bzl", "cc_library")
load("//bazel:protos.bzl", "proto_library")
load("//intrinsic_control/intrinsic/icon/hal/bzl:hardware_module_binary.bzl", "hardware_module_binary")
load("//intrinsic_control/intrinsic/icon/hal/bzl:hardware_module_image.bzl", "hardware_module_image")
load("//intrinsic_control/intrinsic/icon/hal/bzl:resources.bzl", "wrap_in_any_textproto")
load("//intrinsic_control/intrinsic/icon/hardware_modules/universal_robots:ur_hardware_device.bzl", "ur_hardware_device")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "log_handler",
    hdrs = ["log_handler.h"],
    copts = [
        "-fexceptions",
    ],
    features = ["-use_header_modules"],  # Incompatible with -fexceptions.
    visibility = [
        "//visibility:private",
    ],
    deps = [
        "//intrinsic_control/intrinsic/icon/testing:realtime_annotations",
        "//intrinsic_control/intrinsic/icon/utils:log",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@universal-robots-client-library//:ur_client_library",
    ],
)

cc_library(
    name = "ur_error_codes",
    srcs = ["ur_error_codes.cc"],
    hdrs = [
        "ur_error_codes.h",
    ],
    deps = [
        "@abseil-cpp//absl/base:no_destructor",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "ur_calibration_utils",
    srcs = ["ur_calibration_utils.cc"],
    hdrs = [
        "ur_calibration_utils.h",
    ],
    deps = [
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
        "@com_gitlab_libeigen_eigen//:eigen",
        "@gloop//gloop/util/gtl:flat_map",
        "@universal-robots-client-library//:ur_client_library",
        "@ur_calibration//:calibration",
    ],
)

cc_library(
    name = "ur_kinematics_calibration_service",
    srcs = ["ur_kinematics_calibration_service.cc"],
    hdrs = ["ur_kinematics_calibration_service.h"],
    deps = [
        ":ur_calibration_utils",
        "//intrinsic/world/service/robot_calibration:robot_calibration_data_service_cc_grpc_proto",
        "//intrinsic/world/service/robot_calibration:robot_calibration_data_service_cc_proto",
        "//intrinsic_sdk/intrinsic/math:proto_conversion",
        "//intrinsic_sdk/intrinsic/util:invalid_until_set",
        "//intrinsic_sdk/intrinsic/util/status:status_builder_grpc",
        "//intrinsic_sdk/intrinsic/world/proto:object_world_updates_cc_proto",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/synchronization",
        "@com_github_grpc_grpc//:grpc++",
        "@intrinsic_apis//intrinsic/resources/proto:runtime_context_cc_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:object_properties_cc_proto",
    ],
)

filegroup(
    name = "rtde_files",
    srcs = [
        "rtde_files/rtde_input_recipe.txt",
        "rtde_files/rtde_output_recipe.txt",
    ],
)

exports_files([
    "rtde_files/rtde_input_recipe.txt",
    "rtde_files/rtde_output_recipe.txt",
])

cc_library(
    name = "ur_driver_interface",
    hdrs = [
        "ur_driver_interface.h",
    ],
)

cc_library(
    name = "rtde_ur_driver",
    srcs = ["rtde_ur_driver.cc"],
    hdrs = [
        "rtde_ur_driver.h",
    ],
    copts = [
        "-fexceptions",
    ],
    features = ["-use_header_modules"],  # Incompatible with -fexceptions.
    deps = [
        ":ur_driver_interface",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@universal-robots-client-library//:ur_client_library",
    ],
)

cc_library(
    name = "prefilter_interface",
    hdrs = ["prefilter_interface.h"],
    deps = [
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_sdk/intrinsic/eigenmath",
    ],
)

cc_library(
    name = "pass_through_filter",
    hdrs = ["pass_through_filter.h"],
    deps = [
        ":prefilter_interface",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_sdk/intrinsic/eigenmath",
    ],
)

cc_library(
    name = "ur_prefilter_factory",
    srcs = [
        "ur_prefilter_factory_core.cc",
    ],
    hdrs = ["ur_prefilter_factory.h"],
    deps = [
        ":config_cc_proto",
        ":pass_through_filter",
        ":prefilter_interface",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ] + [],
)

cc_library(
    name = "ur_module",
    srcs = ["ur_module.cc"],
    hdrs = [
        "ur_module.h",
    ],
    copts = [
        "-fexceptions",
    ],
    data = [
        ":rtde_files",
        "@universal-robots-client-library//:rtde_files",
    ],
    features = ["-use_header_modules"],  # Incompatible with -fexceptions.
    visibility = [
        "//visibility:private",
    ],
    deps = [
        ":config_cc_proto",
        ":log_handler",
        ":pass_through_filter",
        ":prefilter_interface",
        ":rtde_ur_driver",
        ":ur_calibration_utils",
        ":ur_error_codes",
        ":ur_kinematics_calibration_service",
        ":ur_prefilter_factory",
        "//intrinsic_control/intrinsic/icon/control:realtime_bridge_types",
        "//intrinsic_control/intrinsic/icon/control:realtime_clock_interface",
        "//intrinsic_control/intrinsic/icon/control/algorithms:cartesian_impedance_commons",
        "//intrinsic_control/intrinsic/icon/control/safety:safety_messages_fbs_cc",
        "//intrinsic_control/intrinsic/icon/control/safety:safety_messages_fbs_utils",
        "//intrinsic_control/intrinsic/icon/control/safety/extern:safety_status_fbs_cc",
        "//intrinsic_control/intrinsic/icon/flatbuffers:transform_types",
        "//intrinsic_control/intrinsic/icon/flatbuffers:transform_types_fbs_cc",
        "//intrinsic_control/intrinsic/icon/flatbuffers:transform_utils",
        "//intrinsic_control/intrinsic/icon/hal:command_validator",
        "//intrinsic_control/intrinsic/icon/hal:default_hardware_interfaces",
        "//intrinsic_control/intrinsic/icon/hal:hardware_interface_handle",
        "//intrinsic_control/intrinsic/icon/hal:hardware_interface_registry",
        "//intrinsic_control/intrinsic/icon/hal:hardware_interface_traits",
        "//intrinsic_control/intrinsic/icon/hal:hardware_module_init_context",
        "//intrinsic_control/intrinsic/icon/hal:hardware_module_interface",
        "//intrinsic_control/intrinsic/icon/hal:hardware_module_registry",
        "//intrinsic_control/intrinsic/icon/hal:module_config",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:force_sensor_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:force_torque_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:force_torque_fbs_utils",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:io_controller_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:io_controller_fbs_utils",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:joint_command_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:joint_command_fbs_utils",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:joint_state_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:joint_state_fbs_utils",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:payload_command_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:payload_command_utils",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:payload_state_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:payload_state_utils",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:robot_controller_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:robot_controller_fbs_utils",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:robot_payload_utils",
        "//intrinsic_control/intrinsic/icon/proto:joint_collection_prefilter_config_cc_proto",
        "//intrinsic_control/intrinsic/icon/proto:safety_status_conversion",
        "//intrinsic_control/intrinsic/icon/utils:async_buffer",
        "//intrinsic_control/intrinsic/icon/utils:constants",
        "//intrinsic_control/intrinsic/icon/utils:core_time",
        "//intrinsic_control/intrinsic/icon/utils:fixed_str_cat",
        "//intrinsic_control/intrinsic/icon/utils:log",
        "//intrinsic_control/intrinsic/icon/utils:realtime_guard",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_control/intrinsic/icon/utils:time",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:twist",
        "//intrinsic_sdk/intrinsic/util:invalid_until_set",
        "//intrinsic_sdk/intrinsic/util:proto_time",
        "//intrinsic_sdk/intrinsic/util/path_resolver",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/util/thread",
        "//intrinsic_sdk/intrinsic/util/thread:thread_options",
        "//intrinsic_sdk/intrinsic/util/thread:thread_utils",
        "//intrinsic_sdk/intrinsic/world/robot_payload:robot_payload_base",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/functional:bind_front",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@com_github_google_flatbuffers//:runtime_cc",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/icon/proto:safety_status_cc_proto",
        "@intrinsic_apis//intrinsic/resources/proto:runtime_context_cc_proto",
        "@universal-robots-client-library//:ur_client_library",
    ] + [],
    alwayslink = 1,
)

proto_library(
    name = "config_proto",
    srcs = ["config.proto"],
    deps = [
        "//intrinsic_control/intrinsic/icon/proto:joint_collection_prefilter_config_proto",
    ],
)

cc_proto_library(
    name = "config_cc_proto",
    deps = [":config_proto"],
)

hardware_module_binary(
    name = "ur_module_main",
    hardware_module_lib = ":ur_module",
)

# Generic image
hardware_module_image(
    name = "ur_module_image",
    extra_files = [
        "@universal-robots-client-library//:examples/resources/rtde_input_recipe.txt",
        "@universal-robots-client-library//:examples/resources/rtde_output_recipe.txt",
        "@universal-robots-client-library//:resources/external_control.urscript",
    ],
    hardware_module_binary = "ur_module_main",
)

# robot_ip: "192.170.10.1"
wrap_in_any_textproto(
    name = "default_config_with_scene_object",
    src = "default_config_with_scene_object.pbtxt",
)

# ==============================================================================
# Universal Robots Target Registrations (Standard and Core)
# ==============================================================================

# --- intrinsic-core Robot Devices ---
ur_hardware_device(
    name = "ur3e_hardware_module_core",
    display_name = "UR3e",
    is_core_variant = True,
)

ur_hardware_device(
    name = "ur5e_hardware_module_core",
    display_name = "UR5e",
    is_core_variant = True,
)

ur_hardware_device(
    name = "ur10e_hardware_module_core",
    display_name = "UR10e",
    is_core_variant = True,
)

filegroup(
    name = "universal_robots_core",
    srcs = [
        "README.md",
        ":ur10e_hardware_module_core",
        ":ur3e_hardware_module_core",
        ":ur5e_hardware_module_core",
    ],
)
