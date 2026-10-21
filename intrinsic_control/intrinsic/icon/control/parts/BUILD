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

package(default_visibility = ["//intrinsic_control/intrinsic/icon/control:icon_rtcl"])

cc_library(
    name = "feature_interfaces",
    hdrs = ["feature_interfaces.h"],
    visibility = [
        "//intrinsic_control/intrinsic/icon:rtcl_users",
    ],
    deps = [
        ":io_block",
        ":realtime_robot_payload",
        "//intrinsic_control/intrinsic/icon/control:joint_acceleration_command",
        "//intrinsic_control/intrinsic/icon/control:joint_position_command",
        "//intrinsic_control/intrinsic/icon/dynamics:rigid_body_interface",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_kinematics/intrinsic/kinematics",
        "//intrinsic_kinematics/intrinsic/kinematics:group",
        "//intrinsic_kinematics/intrinsic/kinematics:skeleton",
        "//intrinsic_kinematics/intrinsic/kinematics/ik:inverse_kinematics_interface",
        "//intrinsic_kinematics/intrinsic/kinematics/types:cartesian_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:twist",
        "//intrinsic_sdk/intrinsic/util:fixed_vector",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:optional",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_library(
    name = "fake_feature_interfaces",
    testonly = True,
    srcs = ["fake_feature_interfaces.cc"],
    hdrs = ["fake_feature_interfaces.h"],
    visibility = [
        "//intrinsic_control/intrinsic/icon:rtcl_users",
    ],
    deps = [
        ":feature_interfaces",
        ":io_block",
        ":realtime_robot_payload",
        "//intrinsic_control/intrinsic/icon/control:joint_position_command",
        "//intrinsic_control/intrinsic/icon/dynamics:mock_rigid_body_interface",
        "//intrinsic_control/intrinsic/icon/dynamics:rigid_body_interface",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_kinematics/intrinsic/kinematics",
        "//intrinsic_kinematics/intrinsic/kinematics:group",
        "//intrinsic_kinematics/intrinsic/kinematics:skeleton",
        "//intrinsic_kinematics/intrinsic/kinematics:test_utils",
        "//intrinsic_kinematics/intrinsic/kinematics/ik:fake_inverse_kinematics",
        "//intrinsic_kinematics/intrinsic/kinematics/ik:inverse_kinematics_interface",
        "//intrinsic_kinematics/intrinsic/kinematics/types:cartesian_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/eigenmath:manifolds",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:twist",
        "//intrinsic_sdk/intrinsic/util:fixed_vector",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_library(
    name = "realtime_part_interface",
    hdrs = ["realtime_part_interface.h"],
    visibility = ["//intrinsic_control/intrinsic/icon:rtcl_users"],
    deps = [
        ":feature_interface_registry",
        ":realtime_part_property_access",
        "//intrinsic_control/intrinsic/icon/control:realtime_bridge_types",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
    ],
)

cc_library(
    name = "hal_realtime_part_base",
    srcs = ["hal_realtime_part_base.cc"],
    hdrs = ["hal_realtime_part_base.h"],
    visibility = ["//intrinsic_control/intrinsic/icon:rtcl_users"],
    deps = [
        ":feature_interface_registry",
        ":realtime_part_interface",
        "//intrinsic_control/intrinsic/icon/cc_client:operational_status",
        "//intrinsic_control/intrinsic/icon/control:realtime_bridge_types",
        "//intrinsic_control/intrinsic/icon/control/parts/feature_interfaces:hal_feature_interface_base",
        "//intrinsic_control/intrinsic/icon/hal:hardware_module_manager",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
    ],
)

cc_library(
    name = "realtime_part",
    srcs = ["realtime_part.cc"],
    hdrs = ["realtime_part.h"],
    visibility = [
        "//intrinsic_control/intrinsic/icon:rtcl_users",
    ],
    deps = [
        ":feature_interface_registry",
        ":realtime_part_interface",
        "//intrinsic/util/thread:rt_trace",
        "//intrinsic_control/intrinsic/icon/control:realtime_bridge_types",
        "//intrinsic_control/intrinsic/icon/utils:fixed_str_cat",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "feature_interface_registry",
    srcs = ["feature_interface_registry.cc"],
    hdrs = ["feature_interface_registry.h"],
    visibility = [
        "//intrinsic_control/intrinsic/icon:rtcl_users",
    ],
    deps = [
        ":feature_interfaces",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_sdk/intrinsic/util:demangle",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@intrinsic_apis//intrinsic/icon/proto/v1:types_cc_proto",
    ],
)

cc_library(
    name = "new_manipulator_kinematics",
    srcs = [
        "new_manipulator_kinematics.cc",
    ],
    hdrs = [
        "new_manipulator_kinematics.h",
    ],
    visibility = ["//intrinsic_control/intrinsic/icon:rtcl_users"],
    deps = [
        ":feature_interfaces",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_kinematics/intrinsic/kinematics",
        "//intrinsic_kinematics/intrinsic/kinematics:group",
        "//intrinsic_kinematics/intrinsic/kinematics:model_interface",
        "//intrinsic_kinematics/intrinsic/kinematics:skeleton",
        "//intrinsic_kinematics/intrinsic/kinematics:state",
        "//intrinsic_kinematics/intrinsic/kinematics/ik:inverse_kinematics_interface",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_library(
    name = "realtime_part_from_proto_factory_registry",
    srcs = ["realtime_part_from_proto_factory_registry.cc"],
    hdrs = ["realtime_part_from_proto_factory_registry.h"],
    visibility = [
        "//intrinsic_control/intrinsic/icon:rtcl_users",
        "//intrinsic_control/intrinsic/icon/control:__pkg__",
    ],
    deps = [
        ":feature_interface_registry",
        ":realtime_part_factory_common",
        ":realtime_part_interface",
        "//intrinsic_control/intrinsic/icon/utils:realtime_guard",
        "//intrinsic_sdk/intrinsic/util/proto:descriptors",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/icon/proto:generic_part_config_cc_proto",
        "@intrinsic_apis//intrinsic/icon/proto/v1:types_cc_proto",
    ],
)

cc_library(
    name = "register_default_parts",
    visibility = ["//intrinsic_control/intrinsic/icon:rtcl_users"],
    deps = [
        "//intrinsic_control/intrinsic/icon/control/parts/hal/adio_part:hal_adio_part_register",
        "//intrinsic_control/intrinsic/icon/control/parts/hal/arm_part:hal_arm_part_register",
        "//intrinsic_control/intrinsic/icon/control/parts/hal/force_torque_sensor_part:hal_force_torque_sensor_part_register",
        "//intrinsic_control/intrinsic/icon/control/parts/hal/imu_part:hal_imu_part_register",
        "//intrinsic_control/intrinsic/icon/control/parts/hal/laser_tracker_part:hal_laser_tracker_part_register",
        "//intrinsic_control/intrinsic/icon/control/parts/hal/linear_gripper_part:hal_linear_gripper_part_register",
        "//intrinsic_control/intrinsic/icon/control/parts/hal/rangefinder_part:hal_rangefinder_part_register",
    ],
    alwayslink = True,  # When you are writing a custom part, don't forget this.
)

cc_library(
    name = "realtime_part_status",
    srcs = ["realtime_part_status.cc"],
    hdrs = ["realtime_part_status.h"],
    visibility = ["//intrinsic_control/intrinsic/icon:icon_client_users"],
    deps = [
        ":feature_interfaces",
        ":io_block",
        "//intrinsic_control/intrinsic/icon/cc_client:operational_status",
        "//intrinsic_control/intrinsic/icon/control:realtime_operational_status",
        "//intrinsic_control/intrinsic/icon/proto:cart_space_conversion",
        "//intrinsic_control/intrinsic/icon/proto:eigen_conversion",
        "//intrinsic_control/intrinsic/icon/utils:realtime_guard",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:proto_conversion",
        "//intrinsic_sdk/intrinsic/math:twist",
        "//intrinsic_sdk/intrinsic/util:fixed_vector",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
        "@intrinsic_apis//intrinsic/icon/proto:io_block_cc_proto",
        "@intrinsic_apis//intrinsic/icon/proto:part_status_cc_proto",
        "@magic_enum",
    ],
)

cc_library(
    name = "io_block",
    srcs = ["io_block.cc"],
    hdrs = ["io_block.h"],
    visibility = [
        "//intrinsic_control/intrinsic/icon:rtcl_users",
    ],
    deps = [
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_sdk/intrinsic/util:fixed_vector",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_library(
    name = "move_checker",
    srcs = ["move_checker.cc"],
    hdrs = ["move_checker.h"],
    visibility = ["//intrinsic_control/intrinsic/icon:rtcl_users"],
    deps = [
        ":feature_interfaces",
        "//intrinsic_control/intrinsic/icon/control:joint_position_command",
        "//intrinsic_control/intrinsic/icon/control:realtime_bridge_types",
        "//intrinsic_control/intrinsic/icon/control/algorithms:libjoint_stop_trajectory",
        "//intrinsic_control/intrinsic/icon/control/collision:robotcollision",
        "//intrinsic_control/intrinsic/icon/utils:fixed_str_cat",
        "//intrinsic_control/intrinsic/icon/utils:log",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_kinematics/intrinsic/kinematics/types:check_joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:dynamic_limits_check_mode",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state",
        "//intrinsic_kinematics/intrinsic/kinematics/types:to_fixed_string",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:string_view",
    ],
)

cc_library(
    name = "plane_checker",
    srcs = ["plane_checker.cc"],
    hdrs = ["plane_checker.h"],
    visibility = ["//intrinsic_control/intrinsic/icon:rtcl_users"],
    deps = [
        "//intrinsic_control/intrinsic/icon/utils:log",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_kinematics/intrinsic/kinematics/types:cartesian_limits",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_library(
    name = "realtime_part_factory_common",
    srcs = ["realtime_part_factory_common.cc"],
    hdrs = ["realtime_part_factory_common.h"],
    visibility = [
        "//intrinsic_control/intrinsic/icon/cc_client/testing:__subpackages__",
        "//intrinsic_control/intrinsic/icon/control:icon_rtcl",
    ],
    deps = [
        ":feature_interface_registry",
        ":feature_interfaces",
        ":part_property_registry",
        ":realtime_part_interface",
        "//intrinsic_control/intrinsic/icon/control:context",
        "//intrinsic_control/intrinsic/icon/control:joint_position_command",
        "//intrinsic_control/intrinsic/icon/proto:cart_space_conversion",
        "//intrinsic_kinematics/intrinsic/kinematics/proto:kinematics_conversion",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state",
        "//intrinsic_sdk/intrinsic/math:proto_conversion",
        "//intrinsic_sdk/intrinsic/util:fixed_vector",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
        "@intrinsic_apis//intrinsic/icon/proto:cart_space_cc_proto",
        "@intrinsic_apis//intrinsic/icon/proto:generic_part_config_cc_proto",
        "@intrinsic_apis//intrinsic/icon/proto/v1:types_cc_proto",
        "@intrinsic_apis//intrinsic/kinematics/types:joint_limits_cc_proto",
        "@intrinsic_apis//intrinsic/math/proto:pose_cc_proto",
    ],
)

cc_library(
    name = "part_property_registry",
    srcs = ["part_property_registry.cc"],
    hdrs = ["part_property_registry.h"],
    deps = [
        "//intrinsic_control/intrinsic/icon/common:part_properties",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "realtime_part_property_access",
    srcs = ["realtime_part_property_access.cc"],
    hdrs = ["realtime_part_property_access.h"],
    deps = [
        "//intrinsic_control/intrinsic/icon/common:part_properties",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:fixed_array",
    ],
)

cc_library(
    name = "realtime_log_context",
    srcs = ["realtime_log_context.cc"],
    hdrs = ["realtime_log_context.h"],
    visibility = ["//intrinsic_control/intrinsic/icon:icon_client_users"],
    deps = [
        "//intrinsic_control/intrinsic/icon/testing:realtime_annotations",
        "//intrinsic_control/intrinsic/icon/utils:fixed_string",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@intrinsic_apis//intrinsic/logging/proto:context_cc_proto",
    ],
)

cc_library(
    name = "realtime_robot_payload",
    srcs = ["realtime_robot_payload.cc"],
    hdrs = ["realtime_robot_payload.h"],
    visibility = ["//intrinsic_control/intrinsic/icon:__subpackages__"],
    deps = [
        "//intrinsic_control/intrinsic/icon/utils:fixed_str_cat",
        "//intrinsic_control/intrinsic/icon/utils:fixed_string",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_kinematics/intrinsic/kinematics:validate_link_parameters",
        "//intrinsic_kinematics/intrinsic/kinematics/types:to_fixed_string",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:almost_equals",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/world/robot_payload:robot_payload_base",
        "@intrinsic_apis//intrinsic/world/proto:robot_payload_cc_proto",
    ],
)

cc_library(
    name = "payload_property",
    srcs = ["payload_property.cc"],
    hdrs = ["payload_property.h"],
    visibility = [
        "//intrinsic/skills/apps/icon2:__pkg__",
        "//intrinsic_control/intrinsic/icon:__subpackages__",
    ],
    deps = [
        ":part_property_registry",
        ":realtime_part_property_access",
        ":realtime_robot_payload",
        "//intrinsic_control/intrinsic/icon/common:part_properties",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world/robot_payload",
        "//intrinsic_sdk/intrinsic/world/robot_payload:robot_payload_base",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
        "@cppitertools",
    ],
)
