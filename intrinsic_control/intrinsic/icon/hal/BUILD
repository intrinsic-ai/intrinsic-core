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
load("//bazel:go_macros.bzl", "go_proto_library")

package(default_visibility = [
    "//intrinsic/simulation/gazebo/plugins:__pkg__",
    "//intrinsic/util/thread:__subpackages__",
    "//intrinsic_sdk/intrinsic/util/thread:__subpackages__",
    "//visibility:public",
])

cc_proto_library(
    name = "hardware_module_config_cc_proto",
    deps = ["@intrinsic_apis//intrinsic/icon/hal/proto:hardware_module_config_proto"],
)

go_proto_library(
    name = "hardware_module_config_go_proto",
    importpath = "intrinsic/icon/hal/hardware_module_config_go_proto",
    protos = ["@intrinsic_apis//intrinsic/icon/hal/proto:hardware_module_config_proto"],
    deps = [
        "@intrinsic_apis//intrinsic/assets/proto:field_metadata_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:resolved_dependency_go_proto",
        "@intrinsic_apis//intrinsic/icon/hardware_modules/sim_bus:sim_bus_hardware_module_go_proto",
    ],
)

cc_proto_library(
    name = "hardware_module_inspection_cc_proto",
    deps = ["@intrinsic_apis//intrinsic/icon/hal/proto:hardware_module_inspection_proto"],
)

go_proto_library(
    name = "hardware_module_inspection_go_proto",
    importpath = "intrinsic/icon/hal/hardware_module_inspection_go_proto",
    protos = ["@intrinsic_apis//intrinsic/icon/hal/proto:hardware_module_inspection_proto"],
    deps = [
        "@intrinsic_apis//intrinsic/icon/proto:safety_status_go_proto",
    ],
)

cc_library(
    name = "hardware_interface_traits",
    hdrs = ["hardware_interface_traits.h"],
)

cc_library(
    name = "hardware_interface_handle",
    hdrs = ["hardware_interface_handle.h"],
    deps = [
        ":icon_state_register",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:icon_state_fbs_cc",
        "//intrinsic_control/intrinsic/icon/interprocess/shared_memory_manager:memory_segment",
        "//intrinsic_control/intrinsic/icon/utils:core_time",
        "//intrinsic_control/intrinsic/icon/utils:current_cycle",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "@com_github_google_flatbuffers//:flatbuffers",
    ],
)

cc_library(
    name = "hardware_interface_registry",
    srcs = ["hardware_interface_registry.cc"],
    hdrs = [
        "get_hardware_interface.h",
        "hardware_interface_registry.h",
    ],
    deps = [
        ":hardware_interface_handle",
        ":hardware_interface_traits",
        ":hardware_module_config_cc_proto",
        ":icon_state_register",
        ":module_config",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:icon_state_fbs_cc",
        "//intrinsic_control/intrinsic/icon/interprocess/shared_memory_manager",
        "//intrinsic_control/intrinsic/icon/interprocess/shared_memory_manager:domain_socket_utils",
        "//intrinsic_control/intrinsic/icon/interprocess/shared_memory_manager:memory_segment",
        "//intrinsic_control/intrinsic/icon/interprocess/shared_memory_manager:segment_header",
        "//intrinsic_control/intrinsic/icon/interprocess/shared_memory_manager:segment_info_fbs_cc",
        "//intrinsic_control/intrinsic/icon/interprocess/shared_memory_manager:segment_info_utils",
        "//intrinsic_control/intrinsic/icon/testing:realtime_annotations",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@com_github_google_flatbuffers//:flatbuffers",
    ],
)

cc_library(
    name = "hardware_module_init_context",
    hdrs = [
        "hardware_module_init_context.h",
    ],
    deps = [
        ":hardware_interface_registry",
        ":module_config",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@com_github_grpc_grpc//:grpc++",
    ],
)

cc_library(
    name = "hardware_module_interface",
    hdrs = [
        "hardware_module_interface.h",
    ],
    deps = [
        ":hardware_module_init_context",
        ":hardware_module_inspection_cc_proto",
        ":module_config",
        "//intrinsic_control/intrinsic/icon/control:realtime_clock_interface",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "@abseil-cpp//absl/status",
    ],
)

cc_library(
    name = "icon_state_register",
    hdrs = ["icon_state_register.h"],
    deps = [
        ":hardware_interface_traits",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:icon_state_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:icon_state_fbs_utils",
        "@abseil-cpp//absl/strings:string_view",
    ],
)

cc_library(
    name = "control_period_register",
    hdrs = ["control_period_register.h"],
    deps = [
        ":hardware_interface_traits",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:control_period_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:control_period_fbs_utils",
        "@abseil-cpp//absl/strings:string_view",
    ],
)

cc_library(
    name = "command_validator",
    srcs = ["command_validator.cc"],
    hdrs = [
        "command_validator.h",
    ],
    deps = [
        ":hardware_interface_handle",
        ":hardware_interface_registry",
        ":icon_state_register",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:icon_state_fbs_cc",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:string_view",
    ],
)

cc_library(
    name = "hardware_module_registry",
    hdrs = ["hardware_module_registry.h"],
    deps = [
        ":hardware_module_config_cc_proto",
        ":hardware_module_interface",
    ],
    alwayslink = 1,
)

cc_library(
    name = "default_hardware_interfaces",
    hdrs = ["default_hardware_interfaces.h"],
    deps = [
        ":control_period_register",
        ":hardware_interface_traits",
        "//intrinsic_control/intrinsic/icon/control/safety:safety_messages_fbs_cc",
        "//intrinsic_control/intrinsic/icon/control/safety:safety_messages_fbs_utils",
        "//intrinsic_control/intrinsic/icon/flatbuffers:transform_types",
        "//intrinsic_control/intrinsic/icon/flatbuffers:transform_types_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:control_mode_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:control_mode_fbs_utils",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:electrical_motor_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:electrical_motor_fbs_utils",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:encoder_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:encoder_fbs_utils",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:force_torque_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:force_torque_fbs_utils",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:hardware_module_state_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:hardware_module_state_fbs_utils",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:imu_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:imu_fbs_utils",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:internal_command_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:internal_command_fbs_utils",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:io_controller_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:io_controller_fbs_utils",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:joint_command_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:joint_command_fbs_utils",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:joint_limits_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:joint_limits_fbs_utils",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:joint_state_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:joint_state_fbs_utils",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:laser_tracker_state_cc_fbs",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:laser_tracker_state_utils",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:payload_command_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:payload_command_utils",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:payload_state_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:payload_state_utils",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:rangefinder_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:rangefinder_fbs_utils",
    ],
    alwayslink = 1,
)

cc_library(
    name = "hardware_module_util",
    srcs = ["hardware_module_util.cc"],
    hdrs = [
        "hardware_module_util.h",
    ],
    deps = [
        "//intrinsic_control/intrinsic/icon/hal/interfaces:hardware_module_state_fbs_cc",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/synchronization",
    ],
)

cc_library(
    name = "hardware_module_runtime",
    srcs = ["hardware_module_runtime.cc"],
    hdrs = ["hardware_module_runtime.h"],
    deps = [
        ":control_period_register",
        ":hardware_interface_handle",
        ":hardware_interface_registry",
        ":hardware_interface_traits",
        ":hardware_module_init_context",
        ":hardware_module_inspection_cc_proto",
        ":hardware_module_interface",
        ":hardware_module_registry",
        ":hardware_module_util",
        ":icon_state_register",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:hardware_module_state_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:hardware_module_state_fbs_utils",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:icon_state_fbs_cc",
        "//intrinsic_control/intrinsic/icon/interprocess/remote_trigger:remote_trigger_server",
        "//intrinsic_control/intrinsic/icon/interprocess/shared_memory_manager",
        "//intrinsic_control/intrinsic/icon/interprocess/shared_memory_manager:domain_socket_server",
        "//intrinsic_control/intrinsic/icon/interprocess/shared_memory_manager:domain_socket_utils",
        "//intrinsic_control/intrinsic/icon/testing:realtime_annotations",
        "//intrinsic_control/intrinsic/icon/utils:async_buffer",
        "//intrinsic_control/intrinsic/icon/utils:async_request",
        "//intrinsic_control/intrinsic/icon/utils:core_time",
        "//intrinsic_control/intrinsic/icon/utils:fixed_string",
        "//intrinsic_control/intrinsic/icon/utils:inspection_publisher",
        "//intrinsic_control/intrinsic/icon/utils:malloc_guard",
        "//intrinsic_control/intrinsic/icon/utils:realtime_guard",
        "//intrinsic_control/intrinsic/icon/utils:realtime_metrics",
        "//intrinsic_control/intrinsic/icon/utils:realtime_stack_trace",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_sdk/intrinsic/platform/common/buffers:rt_promise",
        "//intrinsic_sdk/intrinsic/platform/common/buffers:rt_queue",
        "//intrinsic_sdk/intrinsic/platform/common/buffers:rt_queue_multi_writer",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/util/thread",
        "//intrinsic_sdk/intrinsic/util/thread:stop_token",
        "//intrinsic_sdk/intrinsic/util/thread:thread_options",
        "//intrinsic_sdk/intrinsic/util/thread:thread_utils",
        "//intrinsic_sdk/intrinsic/util/time",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:nullability",
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
    ] + [],
)

cc_library(
    name = "module_config",
    srcs = ["module_config.cc"],
    hdrs = ["module_config.h"],
    deps = [
        ":hardware_module_config_cc_proto",
        "//intrinsic_control/intrinsic/icon/control:realtime_clock_interface",
        "//intrinsic_control/intrinsic/icon/utils:realtime_guard",
        "//intrinsic_sdk/intrinsic/util/proto:any",
        "//intrinsic_sdk/intrinsic/util/thread:thread_options",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@intrinsic_apis//intrinsic/icon/hardware_modules/sim_bus:sim_bus_hardware_module_cc_proto",
    ],
    alwayslink = 1,
)

cc_library(
    name = "realtime_clock",
    srcs = ["realtime_clock.cc"],
    hdrs = ["realtime_clock.h"],
    deps = [
        "//intrinsic_control/intrinsic/icon/control:realtime_clock_interface",
        "//intrinsic_control/intrinsic/icon/interprocess/shared_memory_lockstep",
        "//intrinsic_control/intrinsic/icon/interprocess/shared_memory_manager",
        "//intrinsic_control/intrinsic/icon/interprocess/shared_memory_manager:memory_segment",
        "//intrinsic_control/intrinsic/icon/utils:core_time",
        "//intrinsic_control/intrinsic/icon/utils:realtime_guard",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/utils:time",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/util/thread:lockstep",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
    ],
)

filegroup(
    name = "hardware_module_main",
    srcs = ["hardware_module_main.cc"],
)

cc_library(
    name = "hardware_module_health_service",
    srcs = ["hardware_module_health_service.cc"],
    hdrs = ["hardware_module_health_service.h"],
    deps = [
        ":hardware_module_runtime",
        ":hardware_module_util",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:hardware_module_state_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:hardware_module_state_fbs_utils",
        "//intrinsic_sdk/intrinsic/util/status:status_macros_grpc",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/synchronization",
        "@com_github_grpc_grpc//:grpc++",
        "@intrinsic_apis//intrinsic/assets/services/proto/v1:service_state_cc_grpc_proto",
        "@intrinsic_apis//intrinsic/assets/services/proto/v1:service_state_cc_proto",
    ],
)

cc_library(
    name = "hardware_module_main_util",
    srcs = ["hardware_module_main_util.cc"],
    hdrs = ["hardware_module_main_util.h"],
    deps = [
        ":hardware_module_config_cc_proto",
        ":hardware_module_health_service",
        ":hardware_module_registry",
        ":hardware_module_runtime",
        ":hardware_module_util",
        ":module_config",
        ":realtime_clock",
        "//intrinsic_control/intrinsic/icon/interprocess/shared_memory_manager",
        "//intrinsic_control/intrinsic/icon/release:file_helpers",
        "//intrinsic_control/intrinsic/icon/utils:core_time",
        "//intrinsic_control/intrinsic/icon/utils:shutdown_signals",
        "//intrinsic_control/intrinsic/icon/utils:time",
        "//intrinsic_sdk/intrinsic/assets/dependencies:utils_cc",
        "//intrinsic_sdk/intrinsic/connect/cc/grpc:channel",
        "//intrinsic_sdk/intrinsic/logging:data_logger_client",
        "//intrinsic_sdk/intrinsic/logging/proto:logger_service_cc_grpc",
        "//intrinsic_sdk/intrinsic/util/proto:any",
        "//intrinsic_sdk/intrinsic/util/proto:get_text_proto",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/util/thread:thread_options",
        "//intrinsic_sdk/intrinsic/util/thread:util",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@com_github_grpc_grpc//:grpc++",
        "@com_github_grpc_grpc//:grpc_security_base",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/assets/services/proto/v1:service_state_cc_grpc_proto",
        "@intrinsic_apis//intrinsic/resources/proto:runtime_context_cc_proto",
    ],
)

cc_library(
    name = "hardware_module_proxy",
    srcs = ["hardware_module_proxy.cc"],
    hdrs = [
        "get_hardware_interface.h",
        "hardware_module_proxy.h",
    ],
    deps = [
        ":control_period_register",
        ":hardware_interface_handle",
        ":hardware_interface_traits",
        ":icon_state_register",
        ":module_config",
        "//intrinsic/util/thread:rt_trace",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:hardware_module_state_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:hardware_module_state_fbs_utils",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:icon_state_fbs_cc",
        "//intrinsic_control/intrinsic/icon/interprocess/remote_trigger:remote_trigger_client",
        "//intrinsic_control/intrinsic/icon/interprocess/shared_memory_manager",
        "//intrinsic_control/intrinsic/icon/interprocess/shared_memory_manager:domain_socket_utils",
        "//intrinsic_control/intrinsic/icon/interprocess/shared_memory_manager:memory_segment",
        "//intrinsic_control/intrinsic/icon/interprocess/shared_memory_manager:segment_header",
        "//intrinsic_control/intrinsic/icon/interprocess/shared_memory_manager:segment_info_fbs_cc",
        "//intrinsic_control/intrinsic/icon/interprocess/shared_memory_manager:segment_info_utils",
        "//intrinsic_control/intrinsic/icon/utils:core_time",
        "//intrinsic_control/intrinsic/icon/utils:current_cycle",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@com_github_google_flatbuffers//:runtime_cc",
        "@intrinsic_apis//intrinsic/icon/server/config:icon_main_config_cc_proto",
    ],
)

cc_library(
    name = "hardware_module_manager",
    srcs = ["hardware_module_manager.cc"],
    hdrs = [
        "hardware_module_manager.h",
    ],
    deps = [
        ":hardware_module_proxy",
        ":hardware_module_util",
        "//intrinsic/util/thread:rt_trace",
        "//intrinsic_control/intrinsic/icon/control:realtime_bridge_types",
        "//intrinsic_control/intrinsic/icon/control:realtime_operational_status",
        "//intrinsic_control/intrinsic/icon/control:realtime_state_manager_interface",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:hardware_module_state_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:hardware_module_state_fbs_utils",
        "//intrinsic_control/intrinsic/icon/interprocess/remote_trigger:remote_trigger_client",
        "//intrinsic_control/intrinsic/icon/testing:realtime_annotations",
        "//intrinsic_control/intrinsic/icon/utils:core_time",
        "//intrinsic_control/intrinsic/icon/utils:fixed_str_cat",
        "//intrinsic_control/intrinsic/icon/utils:fixed_string",
        "//intrinsic_control/intrinsic/icon/utils:log",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_control/intrinsic/icon/utils:time",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
        "@intrinsic_apis//intrinsic/icon/server/config:icon_main_config_cc_proto",
    ],
)

cc_library(
    name = "hardware_interface_utils",
    srcs = ["hardware_interface_utils.cc"],
    hdrs = ["hardware_interface_utils.h"],
    deps = [
        "//intrinsic_control/intrinsic/icon/interprocess/shared_memory_manager:domain_socket_utils",
        "//intrinsic_control/intrinsic/icon/interprocess/shared_memory_manager:segment_header",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
    ],
)
