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

# Plugins for modeling equipment behaviors in Gazebo simulation.

load("@com_github_grpc_grpc//bazel:cc_grpc_library.bzl", "cc_grpc_library")
load("@com_google_protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("//bazel:cc_macros.bzl", "cc_library", "cc_test")
load("//bazel:protos.bzl", "proto_library")

package(
    default_visibility = ["//visibility:public"],
)

cc_library(
    name = "plugins",
    deps = [
        ":digital_input_output_register",
        ":hardware_module_launcher",
        ":hardware_module_launcher_register",
        ":object_interaction_moderator_register",
        ":service_state_aggregator_register",
        ":set_model_state",
        ":set_model_state_register",
        "//intrinsic/simulation/gazebo/components:common_component_registers",
        "//intrinsic/simulation/gazebo/plugins/cameras:camera_plugin_register",
        "//intrinsic/simulation/gazebo/plugins/cameras:multi_camera_plugin",
        "//intrinsic/simulation/gazebo/plugins/generic_action:generic_action_plugin",
        "//intrinsic/simulation/gazebo/plugins/grippers:gripper_plugins",
        "//intrinsic/simulation/gazebo/plugins/sim_inputs:sim_inputs_system_register",
    ],
    alwayslink = 1,
)

cc_library(
    name = "infer_hardware_interfaces",
    srcs = ["infer_hardware_interfaces.cc"],
    hdrs = ["infer_hardware_interfaces.h"],
    deps = [
        "//intrinsic/simulation/gazebo:asset_instances_client",
        "//intrinsic/simulation/gazebo/components:digital_io_components",
        "//intrinsic/simulation/gazebo/components:ppr_component",
        "//intrinsic_control/intrinsic/icon/hal:hardware_module_config_cc_proto",
        "//intrinsic_sdk/intrinsic/scene/sdf:custom_tags",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/random",
        "@abseil-cpp//absl/random:distributions",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
        "@gz-sim",
        "@intrinsic_apis//intrinsic/icon/hardware_modules/sim_bus:sim_bus_hardware_module_cc_proto",
        "@intrinsic_apis//intrinsic/icon/server/config:dio_config_cc_proto",
        "@intrinsic_apis//intrinsic/simulation/gazebo/plugins:sim_hardware_module_config_cc_proto",
        "@sdformat",
    ],
)

cc_library(
    name = "world_model_config_plugin_constants",
    hdrs = ["world_model_config_plugin_constants.h"],
    deps = [
        "@abseil-cpp//absl/strings:string_view",
    ],
)

cc_library(
    name = "world_model_config_plugin",
    srcs = ["world_model_config_plugin.cc"],
    hdrs = ["world_model_config_plugin.h"],
    deps = [
        ":world_model_config_plugin_constants",
        "//intrinsic/simulation/gazebo/components:ppr_component",
        "//intrinsic/simulation/gazebo/components:world_component",
        "//intrinsic_sdk/intrinsic/util/status:status_builder",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world:entity_id",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_ids",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings:string_view",
        "@gz-sim",
        "@sdformat",
    ],
)

cc_library(
    name = "world_model_config_static",
    srcs = ["world_model_config_static_plugin_hook.cc"],
    deps = [
        ":world_model_config_plugin",
        "//intrinsic/simulation/gazebo/components:ppr_component_register",
        "//intrinsic/simulation/gazebo/components:world_component_register",
        "@gz-plugin//:register",
        "@gz-sim",
    ],
    alwayslink = 1,
)

cc_library(
    name = "object_interaction_moderator",
    srcs = ["object_interaction_moderator.cc"],
    hdrs = ["object_interaction_moderator.h"],
    deps = [
        ":device_util",
        "//intrinsic/simulation/gazebo/components:interaction_component",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/functional:any_invocable",
        "@abseil-cpp//absl/functional:bind_front",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/time",
        "@gloop//gloop/util/gtl:iterator_adaptors",
        "@gz-common//profiler",
        "@gz-math",
        "@gz-msgs",
        "@gz-sim",
        "@sdformat",
    ],
)

cc_library(
    name = "object_interaction_moderator_register",
    srcs = ["object_interaction_moderator_register.cc"],
    deps = [
        ":object_interaction_moderator",
        "@gz-plugin//:register",
        "@gz-sim",
    ],
    alwayslink = 1,
)

# Constants for controlling order of execution of Gazebo systems.
cc_library(
    name = "priority_constants",
    hdrs = ["priority_constants.h"],
    deps = [
        "@gz-sim",
    ],
)

cc_library(
    name = "hardware_module_launcher",
    srcs = ["hardware_module_launcher.cc"],
    hdrs = ["hardware_module_launcher.h"],
    visibility = ["//visibility:private"],
    deps = [
        ":ecm_hardware_interface_conversion",
        ":gazebo_hwm",
        ":gravity_compensator",
        ":infer_hardware_interfaces",
        ":priority_constants",
        ":sim_hardware_interface_data",
        "//intrinsic/resources/proto:resource_operational_status_cc_proto",
        "//intrinsic/simulation/gazebo:asset_instances_client",
        "//intrinsic/simulation/gazebo:asset_instances_service_address_flag",
        "//intrinsic/simulation/gazebo/components:ppr_component",
        "//intrinsic/simulation/gazebo/components:service_state_component",
        "//intrinsic/simulation/gazebo/components:service_state_component_register",
        "//intrinsic_control/intrinsic/icon/hal:command_validator",
        "//intrinsic_control/intrinsic/icon/hal:hardware_interface_handle",
        "//intrinsic_control/intrinsic/icon/hal:hardware_module_config_cc_proto",
        "//intrinsic_control/intrinsic/icon/hal:hardware_module_health_service",
        "//intrinsic_control/intrinsic/icon/hal:hardware_module_interface",
        "//intrinsic_control/intrinsic/icon/hal:hardware_module_runtime",
        "//intrinsic_control/intrinsic/icon/hal:hardware_module_util",
        "//intrinsic_control/intrinsic/icon/hal:module_config",
        "//intrinsic_control/intrinsic/icon/hal:realtime_clock",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:force_torque_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:io_controller_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:joint_command_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:joint_state_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:payload_command_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:payload_state_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:rangefinder_fbs_cc",
        "//intrinsic_control/intrinsic/icon/interprocess/shared_memory_manager",
        "//intrinsic_control/intrinsic/icon/utils:core_time",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_sdk/intrinsic/util/status:return",
        "//intrinsic_sdk/intrinsic/util/status:status_conversion_grpc",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/util/thread",
        "//intrinsic_sdk/intrinsic/util/thread:stop_token",
        "//intrinsic_sdk/intrinsic/util/thread:thread_options",
        "//intrinsic_sdk/intrinsic/util/thread:thread_utils",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
        "@com_github_grpc_grpc//:grpc++",
        "@gz-common//events",
        "@gz-common//profiler",
        "@gz-msgs",
        "@gz-sim",
        "@gz-transport",  # TODO(b/391456177) remove when sensors use ECM directly
        "@intrinsic_apis//intrinsic/assets/services/proto/v1:service_state_cc_proto",
        "@intrinsic_apis//intrinsic/icon/hardware_modules/sim_bus:sim_bus_hardware_module_cc_proto",
        "@intrinsic_apis//intrinsic/icon/server/config:dio_config_cc_proto",
        "@intrinsic_apis//intrinsic/icon/server/config:icon_main_config_cc_proto",
        "@intrinsic_apis//intrinsic/resources/proto:runtime_context_cc_proto",
        "@intrinsic_apis//intrinsic/simulation/gazebo/plugins:sim_hardware_module_config_cc_proto",
        "@sdformat",
    ],
)

cc_library(
    name = "gazebo_hwm",
    srcs = ["gazebo_hwm.cc"],
    hdrs = ["gazebo_hwm.h"],
    visibility = ["//intrinsic_control:__pkg__"],
    deps = [
        ":sim_hardware_interface_data",
        "//intrinsic_control/intrinsic/icon/control:realtime_clock_interface",
        "//intrinsic_control/intrinsic/icon/control/safety:safety_messages_fbs_cc",
        "//intrinsic_control/intrinsic/icon/control/safety/extern:safety_status_fbs_cc",
        "//intrinsic_control/intrinsic/icon/flatbuffers:transform_types_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal:command_validator",
        "//intrinsic_control/intrinsic/icon/hal:default_hardware_interfaces",
        "//intrinsic_control/intrinsic/icon/hal:hardware_interface_handle",
        "//intrinsic_control/intrinsic/icon/hal:hardware_interface_registry",
        "//intrinsic_control/intrinsic/icon/hal:hardware_module_init_context",
        "//intrinsic_control/intrinsic/icon/hal:hardware_module_interface",
        "//intrinsic_control/intrinsic/icon/hal:module_config",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:force_sensor_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:force_torque_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:io_controller_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:joint_command_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:joint_limits_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:joint_state_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:payload_command_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:payload_state_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:rangefinder_fbs_cc",
        "//intrinsic_control/intrinsic/icon/proto:safety_status_conversion",
        "//intrinsic_control/intrinsic/icon/utils:core_time",
        "//intrinsic_control/intrinsic/icon/utils:log",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/util/thread",
        "//intrinsic_sdk/intrinsic/util/thread:thread_options",
        "//intrinsic_sdk/intrinsic/util/thread:thread_utils",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@gz-common//profiler",
        "@gz-sim",
        "@intrinsic_apis//intrinsic/icon/hardware_modules/sim_bus:sim_bus_hardware_module_cc_proto",
        "@intrinsic_apis//intrinsic/icon/proto:safety_status_cc_proto",
        "@intrinsic_apis//intrinsic/icon/server/config:dio_config_cc_proto",
    ],
)

cc_library(
    name = "set_model_state",
    srcs = ["set_model_state.cc"],
    hdrs = ["set_model_state.h"],
    visibility = ["//visibility:private"],
    deps = [
        "@abseil-cpp//absl/log",
        "@gz-common//profiler",
        "@gz-math",
        "@gz-sim",
        "@sdformat",
    ],
)

cc_library(
    name = "hardware_module_launcher_register",
    srcs = ["hardware_module_launcher_register.cc"],
    deps = [
        ":hardware_module_launcher",
        "@gz-plugin//:register",
        "@gz-sim",
    ],
    alwayslink = True,
)

cc_library(
    name = "set_model_state_register",
    srcs = ["set_model_state_register.cc"],
    deps = [
        ":set_model_state",
        "@gz-plugin//:register",
        "@gz-sim",
    ],
    alwayslink = True,
)

cc_test(
    name = "gazebo_hwm_test",
    srcs = ["gazebo_hwm_test.cc"],
    deps = [
        ":gazebo_hwm",
        ":gravity_compensator",
        ":sim_hardware_interface_data",
        "//intrinsic_control/intrinsic/icon/control:mock_realtime_clock",
        "//intrinsic_control/intrinsic/icon/hal:hardware_interface_registry",
        "//intrinsic_control/intrinsic/icon/hal:hardware_module_config_cc_proto",
        "//intrinsic_control/intrinsic/icon/hal:hardware_module_init_context",
        "//intrinsic_control/intrinsic/icon/hal:icon_state_register",
        "//intrinsic_control/intrinsic/icon/hal:module_config",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:icon_state_fbs_cc",
        "//intrinsic_control/intrinsic/icon/interprocess/shared_memory_manager",
        "//intrinsic_control/intrinsic/icon/interprocess/shared_memory_manager/testing:unique_segment_name",
        "//intrinsic_control/intrinsic/icon/utils:core_time",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_matchers",
        "//intrinsic_sdk/intrinsic/util/thread",
        "@abseil-cpp//absl/cleanup",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_cel_cpp//internal:testing_no_main",
        "@com_google_googletest//:gtest_main",
        "@gz-sim",
    ],
)

cc_library(
    name = "digital_input_output",
    srcs = ["digital_input_output.cc"],
    hdrs = ["digital_input_output.h"],
    deps = [
        "//intrinsic/simulation/gazebo/components:digital_io_components",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/cleanup",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/synchronization",
        "@gz-msgs",
        "@gz-sim",
        "@gz-transport",
        "@sdformat",
    ],
)

cc_library(
    name = "digital_input_output_register",
    srcs = ["digital_input_output_register.cc"],
    deps = [
        ":digital_input_output",
        "//intrinsic/simulation/gazebo/components:digital_io_components_register",
        "@gz-plugin//:register",
        "@gz-sim",
    ],
    alwayslink = True,
)

proto_library(
    name = "aggregated_resource_health_proto",
    srcs = ["aggregated_resource_health.proto"],
)

cc_proto_library(
    name = "aggregated_resource_health_cc_proto",
    deps = [":aggregated_resource_health_proto"],
)

cc_grpc_library(
    name = "aggregated_resource_health_cc_grpc_proto",
    srcs = [":aggregated_resource_health_proto"],
    generate_mocks = True,
    grpc_only = True,
    deps = [":aggregated_resource_health_cc_proto"],
)

cc_library(
    name = "service_state_aggregator_register",
    srcs = ["service_state_aggregator_register.cc"],
    deps = [
        ":service_state_aggregator",
        "@gz-plugin//:register",
        "@gz-sim",
    ],
    alwayslink = True,
)

cc_library(
    name = "service_state_aggregator",
    srcs = ["service_state_aggregator.cc"],
    hdrs = ["service_state_aggregator.h"],
    deps = [
        ":aggregated_resource_health_cc_grpc_proto",
        ":aggregated_resource_health_cc_proto",
        ":priority_constants",
        "//intrinsic/simulation/gazebo/components:ppr_component",
        "//intrinsic/simulation/gazebo/components:service_state_component",
        "//intrinsic_sdk/intrinsic/util/status:status_conversion_grpc",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@com_github_grpc_grpc//:grpc++",
        "@gz-sim",
        "@intrinsic_apis//intrinsic/assets/services/proto/v1:service_state_cc_proto",
        "@sdformat",
    ],
)

cc_library(
    name = "ecm_hardware_interface_conversion",
    srcs = ["ecm_hardware_interface_conversion.cc"],
    hdrs = ["ecm_hardware_interface_conversion.h"],
    deps = [
        ":gravity_compensator",
        "//intrinsic/simulation/gazebo/components:digital_io_components",
        "//intrinsic/simulation/gazebo/components:digital_io_components_register",
        "//intrinsic/simulation/gazebo/components:ft_sensor_tare_components",
        "//intrinsic/simulation/gazebo/components:ft_sensor_tare_components_register",
        "//intrinsic/simulation/gazebo/components:joint_commanded_position",
        "//intrinsic/simulation/gazebo/components:kinematic_chain_payload_component",
        "//intrinsic/simulation/gazebo/components:kinematic_chain_payload_component_register",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:force_sensor_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:force_torque_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:io_controller_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:joint_command_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:joint_state_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:payload_command_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:payload_state_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:robot_payload_utils",
        "//intrinsic_control/intrinsic/icon/utils:sensor_utils",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world/robot_payload:robot_payload_base",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
        "@gz-msgs",
        "@gz-sim",
    ],
)

cc_library(
    name = "sim_hardware_interface_data",
    srcs = ["sim_hardware_interface_data.cc"],
    hdrs = ["sim_hardware_interface_data.h"],
    deps = [
        ":gravity_compensator",
        "//intrinsic/simulation/gazebo/components:digital_io_components",
        "//intrinsic/simulation/gazebo/components:digital_io_components_register",
        "//intrinsic/simulation/gazebo/components:ft_sensor_tare_components",
        "//intrinsic/simulation/gazebo/components:ft_sensor_tare_components_register",
        "//intrinsic/simulation/gazebo/components:joint_commanded_position_register",
        "//intrinsic/simulation/gazebo/components:ppr_component",
        "//intrinsic/simulation/gazebo/components:ppr_component_register",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
        "@gz-sim",
        "@intrinsic_apis//intrinsic/icon/hardware_modules/sim_bus:sim_bus_hardware_module_cc_proto",
        "@intrinsic_apis//intrinsic/simulation/gazebo/plugins:sim_hardware_module_config_cc_proto",
        "@sdformat",
    ],
)

cc_library(
    name = "add_hardware_module_launchers_system",
    srcs = ["add_hardware_module_launchers_system.cc"],
    hdrs = ["add_hardware_module_launchers_system.h"],
    deps = [
        ":hardware_module_launcher",
        "//intrinsic/simulation/gazebo:asset_instances_client",
        "//intrinsic/simulation/gazebo:asset_instances_service_address_flag",
        "//intrinsic/simulation/gazebo/components:ppr_component",
        "//intrinsic_control/intrinsic/icon/hal:hardware_module_config_cc_proto",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/functional:function_ref",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
        "@gz-sim",
        "@sdformat",
    ],
)

cc_library(
    name = "gravity_compensator",
    srcs = ["gravity_compensator.cc"],
    hdrs = ["gravity_compensator.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/util/status:status_builder",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@gz-math",
        "@gz-sim",
        "@sdformat",
    ],
)

cc_library(
    name = "device_util",
    srcs = [
        "joint_device_util.cc",
        "link_device_util.cc",
        "util.cc",
    ],
    hdrs = [
        "joint_device_util.h",
        "link_device_util.h",
        "util.h",
    ],
    deps = [
        "//intrinsic_sdk/intrinsic/util/status:status_builder",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@gz-math",
        "@gz-sim",
        "@sdformat",
    ],
)
