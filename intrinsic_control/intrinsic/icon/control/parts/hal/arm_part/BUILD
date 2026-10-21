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
    name = "hal_arm_part",
    srcs = ["hal_arm_part.cc"],
    hdrs = ["hal_arm_part.h"],
    deps = [
        "//intrinsic_control/intrinsic/icon/control:context",
        "//intrinsic_control/intrinsic/icon/control:realtime_collision_world_from_world",
        "//intrinsic_control/intrinsic/icon/control/collision:robotcollision",
        "//intrinsic_control/intrinsic/icon/control/parts:feature_interface_registry",
        "//intrinsic_control/intrinsic/icon/control/parts:feature_interfaces",
        "//intrinsic_control/intrinsic/icon/control/parts:hal_realtime_part_base",
        "//intrinsic_control/intrinsic/icon/control/parts:new_manipulator_kinematics",
        "//intrinsic_control/intrinsic/icon/control/parts:payload_property",
        "//intrinsic_control/intrinsic/icon/control/parts:realtime_part_factory_common",
        "//intrinsic_control/intrinsic/icon/control/parts/feature_interfaces:cartesian_limits_feature",
        "//intrinsic_control/intrinsic/icon/control/parts/feature_interfaces:control_mode_state_feature",
        "//intrinsic_control/intrinsic/icon/control/parts/feature_interfaces:handguiding_command_feature",
        "//intrinsic_control/intrinsic/icon/control/parts/feature_interfaces:homing_feature",
        "//intrinsic_control/intrinsic/icon/control/parts/feature_interfaces:joint_acceleration_command_feature",
        "//intrinsic_control/intrinsic/icon/control/parts/feature_interfaces:joint_acceleration_estimated_state_feature",
        "//intrinsic_control/intrinsic/icon/control/parts/feature_interfaces:joint_acceleration_state_feature",
        "//intrinsic_control/intrinsic/icon/control/parts/feature_interfaces:joint_limits_feature",
        "//intrinsic_control/intrinsic/icon/control/parts/feature_interfaces:joint_position_command_feature",
        "//intrinsic_control/intrinsic/icon/control/parts/feature_interfaces:joint_position_pid_torque_controller_feature",
        "//intrinsic_control/intrinsic/icon/control/parts/feature_interfaces:joint_position_state_feature",
        "//intrinsic_control/intrinsic/icon/control/parts/feature_interfaces:joint_torque_command_feature",
        "//intrinsic_control/intrinsic/icon/control/parts/feature_interfaces:joint_torque_state_feature",
        "//intrinsic_control/intrinsic/icon/control/parts/feature_interfaces:joint_velocity_command_feature",
        "//intrinsic_control/intrinsic/icon/control/parts/feature_interfaces:joint_velocity_state_feature",
        "//intrinsic_control/intrinsic/icon/control/parts/feature_interfaces:move_ok_feature",
        "//intrinsic_control/intrinsic/icon/control/parts/feature_interfaces:payload_command",
        "//intrinsic_control/intrinsic/icon/control/parts/feature_interfaces:payload_state",
        "//intrinsic_control/intrinsic/icon/control/parts/feature_interfaces:process_wrench_at_endeffector_feature",
        "//intrinsic_control/intrinsic/icon/control/safety/extern:safety_status_fbs_cc",
        "//intrinsic_control/intrinsic/icon/control/services:assembly_service",
        "//intrinsic_control/intrinsic/icon/control/services:dynamics_service",
        "//intrinsic_control/intrinsic/icon/control/services:geometry_library_service",
        "//intrinsic_control/intrinsic/icon/control/services:kinematics_service",
        "//intrinsic_control/intrinsic/icon/control/services:world_service",
        "//intrinsic_control/intrinsic/icon/dynamics:rigid_body_interface",
        "//intrinsic_control/intrinsic/icon/flatbuffers:transform_types_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal:default_hardware_interfaces",
        "//intrinsic_control/intrinsic/icon/hal:hardware_interface_handle",
        "//intrinsic_control/intrinsic/icon/hal:hardware_module_manager",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:control_mode_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:electrical_motor_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:electrical_motor_hardware_interfaces",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:joint_command_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:joint_limits_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:joint_state_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:payload_command_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:payload_state_fbs_cc",
        "//intrinsic_control/intrinsic/icon/proto:cart_space_conversion",
        "//intrinsic_control/intrinsic/icon/proto:safety_status_conversion",
        "//intrinsic_control/intrinsic/icon/utils:log",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_kinematics/intrinsic/kinematics",
        "//intrinsic_kinematics/intrinsic/kinematics:skeleton",
        "//intrinsic_kinematics/intrinsic/kinematics/ik:inverse_kinematics_interface",
        "//intrinsic_kinematics/intrinsic/kinematics/types:cartesian_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits_xd",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world/objects:kinematic_object",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_client",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_ids",
        "//intrinsic_sdk/intrinsic/world/robot_payload",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@intrinsic_apis//intrinsic/icon/control/parts/hal/arm_part:hal_arm_part_config_cc_proto",
        "@intrinsic_apis//intrinsic/icon/control/parts/hal/v1:hal_part_config_cc_proto",
        "@intrinsic_apis//intrinsic/icon/proto:cart_space_cc_proto",
        "@intrinsic_apis//intrinsic/icon/proto:generic_part_config_cc_proto",
        "@intrinsic_apis//intrinsic/icon/proto:safety_status_cc_proto",
    ],
)

cc_library(
    name = "hal_arm_part_register",
    srcs = ["hal_arm_part_register.cc"],
    visibility = ["//intrinsic_control/intrinsic/icon:rtcl_users"],
    deps = [
        ":hal_arm_part",
        "//intrinsic_control/intrinsic/icon/control/parts:realtime_part_from_proto_factory_registry",
        "@intrinsic_apis//intrinsic/icon/control/parts/hal/arm_part:hal_arm_part_config_cc_proto",
    ],
    alwayslink = True,
)
