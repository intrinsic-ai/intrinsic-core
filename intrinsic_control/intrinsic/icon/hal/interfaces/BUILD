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
load("//intrinsic_sdk/intrinsic/platform:flatbuffers.bzl", "cc_flatbuffers_library", "flatbuffers_library")

package(default_visibility = ["//visibility:public"])

flatbuffers_library(
    name = "adio_fbs",
    srcs = ["adio.fbs"],
)

cc_flatbuffers_library(
    name = "adio_fbs_cc",
    deps = [":adio_fbs"],
)

flatbuffers_library(
    name = "control_mode_fbs",
    srcs = [
        "control_mode.fbs",
    ],
)

cc_flatbuffers_library(
    name = "control_mode_fbs_cc",
    deps = [":control_mode_fbs"],
)

cc_library(
    name = "control_mode_fbs_utils",
    srcs = [
        "control_mode_utils.cc",
    ],
    hdrs = [
        "control_mode_utils.h",
    ],
    deps = [
        ":control_mode_fbs_cc",
        "@com_github_google_flatbuffers//:runtime_cc",
    ],
)

flatbuffers_library(
    name = "control_period_fbs",
    srcs = [
        "control_period.fbs",
    ],
)

cc_flatbuffers_library(
    name = "control_period_fbs_cc",
    deps = [":control_period_fbs"],
)

cc_library(
    name = "control_period_fbs_utils",
    srcs = [
        "control_period_utils.cc",
    ],
    hdrs = [
        "control_period_utils.h",
    ],
    deps = [
        ":control_period_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal:hardware_interface_handle",
        "//intrinsic_control/intrinsic/icon/utils:core_time",
        "//intrinsic_control/intrinsic/icon/utils:time",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@com_github_google_flatbuffers//:runtime_cc",
    ],
)

flatbuffers_library(
    name = "electrical_motor_fbs",
    srcs = [
        "electrical_motor.fbs",
    ],
)

cc_flatbuffers_library(
    name = "electrical_motor_fbs_cc",
    deps = [":electrical_motor_fbs"],
)

cc_library(
    name = "electrical_motor_fbs_utils",
    srcs = ["electrical_motor_utils.cc"],
    hdrs = ["electrical_motor_utils.h"],
    deps = [
        ":electrical_motor_fbs_cc",
        "//intrinsic_control/intrinsic/icon/flatbuffers:fixed_string",
        "//intrinsic_control/intrinsic/icon/utils:log",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/strings:string_view",
        "@com_github_google_flatbuffers//:flatbuffers",
    ],
)

cc_library(
    name = "electrical_motor_hardware_interfaces",
    hdrs = ["electrical_motor_hardware_interfaces.h"],
    deps = [
        ":electrical_motor_fbs_cc",
        ":electrical_motor_fbs_utils",
        "//intrinsic_control/intrinsic/icon/hal:hardware_interface_traits",
    ],
)

flatbuffers_library(
    name = "encoder_fbs",
    srcs = [
        "encoder.fbs",
    ],
)

cc_flatbuffers_library(
    name = "encoder_fbs_cc",
    deps = [":encoder_fbs"],
)

cc_library(
    name = "encoder_fbs_utils",
    srcs = ["encoder_utils.cc"],
    hdrs = [
        "encoder_utils.h",
    ],
    deps = [
        ":encoder_fbs_cc",
        "@com_github_google_flatbuffers//:flatbuffers",
    ],
)

flatbuffers_library(
    name = "force_sensor_fbs",
    srcs = [
        "force_sensor.fbs",
    ],
    deps = [
        "//intrinsic_control/intrinsic/icon/flatbuffers:transform_types_fbs",
    ],
)

cc_flatbuffers_library(
    name = "force_sensor_fbs_cc",
    deps = [":force_sensor_fbs"],
)

flatbuffers_library(
    name = "force_torque_fbs",
    srcs = [
        "force_torque.fbs",
    ],
    deps = [
        ":force_sensor_fbs",
        "//intrinsic_control/intrinsic/icon/flatbuffers:transform_types_fbs",
    ],
)

cc_flatbuffers_library(
    name = "force_torque_fbs_cc",
    deps = [":force_torque_fbs"],
)

cc_library(
    name = "force_torque_fbs_utils",
    srcs = [
        "force_torque_utils.cc",
    ],
    hdrs = [
        "force_torque_utils.h",
    ],
    deps = [
        ":force_sensor_fbs_cc",
        ":force_torque_fbs_cc",
        "//intrinsic_control/intrinsic/icon/flatbuffers:transform_types",  # fixdeps: keep
        "//intrinsic_control/intrinsic/icon/flatbuffers:transform_types_fbs_cc",
        "//intrinsic_control/intrinsic/icon/utils:fixed_str_cat",
        "//intrinsic_control/intrinsic/icon/utils:fixed_string",
        "@com_github_google_flatbuffers//:flatbuffers",
    ],
)

flatbuffers_library(
    name = "gripper_fbs",
    srcs = [
        "gripper.fbs",
    ],
)

cc_flatbuffers_library(
    name = "gripper_fbs_cc",
    deps = [":gripper_fbs"],
)

cc_library(
    name = "gripper_fbs_utils",
    srcs = ["gripper_utils.cc"],
    hdrs = ["gripper_utils.h"],
    deps = [
        ":gripper_fbs_cc",
        "@com_github_google_flatbuffers//:flatbuffers",
    ],
)

flatbuffers_library(
    name = "hardware_module_state_fbs",
    srcs = [
        "hardware_module_state.fbs",
    ],
)

cc_flatbuffers_library(
    name = "hardware_module_state_fbs_cc",
    deps = [
        ":hardware_module_state_fbs",
    ],
)

cc_library(
    name = "hardware_module_state_fbs_utils",
    srcs = ["hardware_module_state_utils.cc"],
    hdrs = [
        "hardware_module_state_utils.h",
    ],
    deps = [
        ":hardware_module_state_fbs_cc",
        "@com_github_google_flatbuffers//:flatbuffers",
    ],
)

flatbuffers_library(
    name = "icon_state_fbs",
    srcs = [
        "icon_state.fbs",
    ],
)

cc_flatbuffers_library(
    name = "icon_state_fbs_cc",
    deps = [":icon_state_fbs"],
)

cc_library(
    name = "icon_state_fbs_utils",
    srcs = ["icon_state_utils.cc"],
    hdrs = [
        "icon_state_utils.h",
    ],
    deps = [
        ":icon_state_fbs_cc",
        "@com_github_google_flatbuffers//:flatbuffers",
    ],
)

flatbuffers_library(
    name = "imu_fbs",
    srcs = [
        "imu.fbs",
    ],
    deps = [
        "//intrinsic_control/intrinsic/icon/flatbuffers:transform_types_fbs",
    ],
)

cc_flatbuffers_library(
    name = "imu_fbs_cc",
    deps = [":imu_fbs"],
)

cc_library(
    name = "imu_fbs_utils",
    srcs = [
        "imu_utils.cc",
    ],
    hdrs = [
        "imu_utils.h",
    ],
    deps = [
        ":imu_fbs_cc",
        "//intrinsic_control/intrinsic/icon/flatbuffers:transform_types_fbs_cc",
        "@com_github_google_flatbuffers//:runtime_cc",
    ],
)

flatbuffers_library(
    name = "internal_command_fbs",
    srcs = ["internal_command.fbs"],
)

cc_flatbuffers_library(
    name = "internal_command_fbs_cc",
    deps = [":internal_command_fbs"],
)

cc_library(
    name = "internal_command_fbs_utils",
    srcs = ["internal_command_utils.cc"],
    hdrs = ["internal_command_utils.h"],
    deps = [
        ":internal_command_fbs_cc",
        "@com_github_google_flatbuffers//:flatbuffers",
    ],
)

flatbuffers_library(
    name = "io_controller_fbs",
    srcs = ["io_controller.fbs"],
    deps = [
        ":adio_fbs",
    ],
)

cc_flatbuffers_library(
    name = "io_controller_fbs_cc",
    deps = [":io_controller_fbs"],
)

cc_library(
    name = "io_controller_fbs_utils",
    srcs = ["io_controller_utils.cc"],
    hdrs = ["io_controller_utils.h"],
    deps = [
        ":adio_fbs_cc",
        ":io_controller_fbs_cc",
        "@com_github_google_flatbuffers//:runtime_cc",
    ],
)

flatbuffers_library(
    name = "joint_command_fbs",
    srcs = [
        "joint_command.fbs",
    ],
)

cc_flatbuffers_library(
    name = "joint_command_fbs_cc",
    deps = [":joint_command_fbs"],
)

cc_library(
    name = "joint_command_fbs_utils",
    srcs = ["joint_command_utils.cc"],
    hdrs = [
        "joint_command_utils.h",
    ],
    deps = [
        ":joint_command_fbs_cc",
        "//intrinsic_control/intrinsic/icon/flatbuffers:flatbuffer_utils",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "@com_github_google_flatbuffers//:runtime_cc",
    ],
)

flatbuffers_library(
    name = "joint_limits_fbs",
    srcs = [
        "joint_limits.fbs",
    ],
)

cc_flatbuffers_library(
    name = "joint_limits_fbs_cc",
    deps = [":joint_limits_fbs"],
)

cc_library(
    name = "joint_limits_fbs_utils",
    srcs = ["joint_limits_utils.cc"],
    hdrs = [
        "joint_limits_utils.h",
    ],
    deps = [
        ":joint_limits_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal:hardware_interface_handle",
        "//intrinsic_control/intrinsic/icon/utils:fixed_str_cat",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/types:span",
        "@com_github_google_flatbuffers//:flatbuffers",
        "@intrinsic_apis//intrinsic/kinematics/types:joint_limits_cc_proto",
    ],
)

flatbuffers_library(
    name = "joint_state_fbs",
    srcs = [
        "joint_state.fbs",
    ],
)

cc_flatbuffers_library(
    name = "joint_state_fbs_cc",
    deps = [":joint_state_fbs"],
)

cc_library(
    name = "joint_state_fbs_utils",
    srcs = ["joint_state_utils.cc"],
    hdrs = [
        "joint_state_utils.h",
    ],
    deps = [
        ":joint_state_fbs_cc",
        "@com_github_google_flatbuffers//:flatbuffers",
    ],
)

flatbuffers_library(
    name = "rangefinder_fbs",
    srcs = [
        "rangefinder.fbs",
    ],
)

cc_flatbuffers_library(
    name = "rangefinder_fbs_cc",
    deps = [":rangefinder_fbs"],
)

cc_library(
    name = "rangefinder_fbs_utils",
    srcs = ["rangefinder_utils.cc"],
    hdrs = ["rangefinder_utils.h"],
    deps = [
        ":rangefinder_fbs_cc",
        "@com_github_google_flatbuffers//:flatbuffers",
    ],
)

flatbuffers_library(
    name = "robot_controller_fbs",
    srcs = [
        "robot_controller.fbs",
    ],
)

cc_flatbuffers_library(
    name = "robot_controller_fbs_cc",
    deps = [":robot_controller_fbs"],
)

cc_library(
    name = "robot_controller_fbs_utils",
    srcs = ["robot_controller_utils.cc"],
    hdrs = [
        "robot_controller_utils.h",
    ],
    deps = [
        ":robot_controller_fbs_cc",
        "@com_github_google_flatbuffers//:flatbuffers",
    ],
)

flatbuffers_library(
    name = "robot_payload_fbs",
    srcs = [
        "robot_payload.fbs",
    ],
    deps = [
        "//intrinsic_control/intrinsic/icon/flatbuffers:matrix_types_fbs",
        "//intrinsic_control/intrinsic/icon/flatbuffers:transform_types_fbs",
    ],
)

cc_flatbuffers_library(
    name = "robot_payload_fbs_cc",
    deps = [":robot_payload_fbs"],
)

cc_library(
    name = "robot_payload_utils",
    srcs = ["robot_payload_utils.cc"],
    hdrs = ["robot_payload_utils.h"],
    deps = [
        ":robot_payload_fbs_cc",
        "//intrinsic_control/intrinsic/icon/control/parts:realtime_robot_payload",
        "//intrinsic_control/intrinsic/icon/flatbuffers:control_types_utils",
        "//intrinsic_control/intrinsic/icon/flatbuffers:matrix_types_fbs_cc",
        "//intrinsic_control/intrinsic/icon/flatbuffers:matrix_types_utils",
        "//intrinsic_control/intrinsic/icon/flatbuffers:transform_types_fbs_cc",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/world/robot_payload",
        "//intrinsic_sdk/intrinsic/world/robot_payload:robot_payload_base",
        "@com_github_google_flatbuffers//:runtime_cc",
    ],
)

flatbuffers_library(
    name = "payload_command_fbs",
    srcs = [
        "payload_command.fbs",
    ],
    deps = [
        ":robot_payload_fbs",
    ],
)

cc_flatbuffers_library(
    name = "payload_command_fbs_cc",
    deps = [":payload_command_fbs"],
)

cc_library(
    name = "payload_command_utils",
    srcs = ["payload_command_utils.cc"],
    hdrs = ["payload_command_utils.h"],
    deps = [
        ":payload_command_fbs_cc",
        ":robot_payload_fbs_cc",
        ":robot_payload_utils",
        "@com_github_google_flatbuffers//:runtime_cc",
    ],
)

flatbuffers_library(
    name = "payload_state_fbs",
    srcs = [
        "payload_state.fbs",
    ],
    deps = [
        ":robot_payload_fbs",
    ],
)

cc_flatbuffers_library(
    name = "payload_state_fbs_cc",
    deps = [":payload_state_fbs"],
)

cc_library(
    name = "payload_state_utils",
    srcs = ["payload_state_utils.cc"],
    hdrs = ["payload_state_utils.h"],
    deps = [
        ":payload_state_fbs_cc",
        ":robot_payload_fbs_cc",
        ":robot_payload_utils",
        "@com_github_google_flatbuffers//:runtime_cc",
    ],
)

flatbuffers_library(
    name = "laser_tracker_state_fbs",
    srcs = ["laser_tracker_state.fbs"],
    deps = ["//intrinsic_control/intrinsic/icon/flatbuffers:transform_types_fbs"],
)

cc_flatbuffers_library(
    name = "laser_tracker_state_cc_fbs",
    deps = [":laser_tracker_state_fbs"],
)

cc_library(
    name = "laser_tracker_state_utils",
    srcs = ["laser_tracker_state_utils.cc"],
    hdrs = ["laser_tracker_state_utils.h"],
    deps = [
        ":laser_tracker_state_cc_fbs",
        "//intrinsic_control/intrinsic/icon/flatbuffers:control_types_utils",
        "//intrinsic_control/intrinsic/icon/flatbuffers:transform_types_fbs_cc",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "@com_github_google_flatbuffers//:runtime_cc",
    ],
)
