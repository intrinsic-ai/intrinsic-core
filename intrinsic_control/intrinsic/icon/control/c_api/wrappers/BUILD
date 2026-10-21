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

# This package holds wrappers that make ICON classes available to users of the RTCL C API
# (see parent directory).

load("//bazel:cc_macros.bzl", "cc_library")

package(
    default_visibility = [
        "//intrinsic_control/intrinsic/icon/control:icon_rtcl",
    ],
)

cc_library(
    name = "string_wrapper",
    srcs = ["string_wrapper.cc"],
    hdrs = ["string_wrapper.h"],
    deps = [
        "//intrinsic_control/intrinsic/icon/control/c_api:icon_c_api",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "action_factory_context_wrapper",
    srcs = ["action_factory_context_wrapper.cc"],
    hdrs = ["action_factory_context_wrapper.h"],
    deps = [
        ":string_wrapper",
        "//intrinsic_control/intrinsic/icon/control:action_factory_context",
        "//intrinsic_control/intrinsic/icon/control:realtime_signal_types",
        "//intrinsic_control/intrinsic/icon/control:slot_types",
        "//intrinsic_control/intrinsic/icon/control:streaming_io_registry",
        "//intrinsic_control/intrinsic/icon/control/c_api:convert_c_realtime_status",
        "//intrinsic_control/intrinsic/icon/control/c_api:icon_c_api",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/icon/proto/v1:types_cc_proto",
    ],
)

cc_library(
    name = "joint_position_wrapper",
    srcs = ["joint_position_wrapper.cc"],
    hdrs = ["joint_position_wrapper.h"],
    deps = [
        "//intrinsic_control/intrinsic/icon/control:joint_position_command",
        "//intrinsic_control/intrinsic/icon/control/c_api:convert_c_realtime_status",
        "//intrinsic_control/intrinsic/icon/control/c_api:convert_c_types",
        "//intrinsic_control/intrinsic/icon/control/c_api:icon_c_api",
        "//intrinsic_control/intrinsic/icon/control/parts:feature_interfaces",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
    ],
)

cc_library(
    name = "joint_position_sensor_wrapper",
    srcs = ["joint_position_sensor_wrapper.cc"],
    hdrs = ["joint_position_sensor_wrapper.h"],
    deps = [
        "//intrinsic_control/intrinsic/icon/control/c_api:convert_c_types",
        "//intrinsic_control/intrinsic/icon/control/c_api:icon_c_api",
        "//intrinsic_control/intrinsic/icon/control/parts:feature_interfaces",
    ],
)

cc_library(
    name = "joint_velocity_estimator_wrapper",
    srcs = ["joint_velocity_estimator_wrapper.cc"],
    hdrs = ["joint_velocity_estimator_wrapper.h"],
    deps = [
        "//intrinsic_control/intrinsic/icon/control/c_api:convert_c_types",
        "//intrinsic_control/intrinsic/icon/control/c_api:icon_c_api",
        "//intrinsic_control/intrinsic/icon/control/parts:feature_interfaces",
    ],
)

cc_library(
    name = "joint_limits_wrapper",
    srcs = ["joint_limits_wrapper.cc"],
    hdrs = ["joint_limits_wrapper.h"],
    deps = [
        "//intrinsic_control/intrinsic/icon/control/c_api:convert_c_types",
        "//intrinsic_control/intrinsic/icon/control/c_api:icon_c_api",
        "//intrinsic_control/intrinsic/icon/control/parts:feature_interfaces",
    ],
)

cc_library(
    name = "streaming_io_realtime_access_wrapper",
    srcs = ["streaming_io_realtime_access_wrapper.cc"],
    hdrs = ["streaming_io_realtime_access_wrapper.h"],
    deps = [
        ":action_factory_context_wrapper",
        "//intrinsic_control/intrinsic/icon/control:streaming_io_realtime",
        "//intrinsic_control/intrinsic/icon/control:streaming_io_types",
        "//intrinsic_control/intrinsic/icon/control/c_api:convert_c_realtime_status",
        "//intrinsic_control/intrinsic/icon/control/c_api:icon_c_api",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
    ],
)

cc_library(
    name = "realtime_slot_map_wrapper",
    srcs = ["realtime_slot_map_wrapper.cc"],
    hdrs = ["realtime_slot_map_wrapper.h"],
    deps = [
        ":force_torque_sensor_wrapper",
        ":joint_limits_wrapper",
        ":joint_position_sensor_wrapper",
        ":joint_position_wrapper",
        ":joint_velocity_estimator_wrapper",
        ":manipulator_kinematics_wrapper",
        "//intrinsic_control/intrinsic/icon/control:realtime_slot_map",
        "//intrinsic_control/intrinsic/icon/control:slot_types",
        "//intrinsic_control/intrinsic/icon/control/c_api:icon_c_api",
        "//intrinsic_control/intrinsic/icon/control/parts:feature_interfaces",
    ],
)

cc_library(
    name = "realtime_signal_access_wrapper",
    srcs = ["realtime_signal_access_wrapper.cc"],
    hdrs = ["realtime_signal_access_wrapper.h"],
    deps = [
        "//intrinsic_control/intrinsic/icon/control:realtime_signal_access",
        "//intrinsic_control/intrinsic/icon/control:realtime_signal_types",
        "//intrinsic_control/intrinsic/icon/control/c_api:convert_c_realtime_status",
        "//intrinsic_control/intrinsic/icon/control/c_api:convert_c_types",
        "//intrinsic_control/intrinsic/icon/control/c_api:icon_c_api",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
    ],
)

cc_library(
    name = "force_torque_sensor_wrapper",
    srcs = ["force_torque_sensor_wrapper.cc"],
    hdrs = ["force_torque_sensor_wrapper.h"],
    deps = [
        "//intrinsic_control/intrinsic/icon/control/c_api:convert_c_realtime_status",
        "//intrinsic_control/intrinsic/icon/control/c_api:convert_c_types",
        "//intrinsic_control/intrinsic/icon/control/c_api:icon_c_api",
        "//intrinsic_control/intrinsic/icon/control/parts:feature_interfaces",
    ],
)

cc_library(
    name = "manipulator_kinematics_wrapper",
    srcs = ["manipulator_kinematics_wrapper.cc"],
    hdrs = ["manipulator_kinematics_wrapper.h"],
    deps = [
        "//intrinsic_control/intrinsic/icon/control/c_api:convert_c_realtime_status",
        "//intrinsic_control/intrinsic/icon/control/c_api:convert_c_types",
        "//intrinsic_control/intrinsic/icon/control/c_api:icon_c_api",
        "//intrinsic_control/intrinsic/icon/control/parts:feature_interfaces",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:pose3",
    ],
)

cc_library(
    name = "feature_interface_wrappers",
    srcs = ["feature_interface_wrappers.cc"],
    hdrs = ["feature_interface_wrappers.h"],
    deps = [
        ":force_torque_sensor_wrapper",
        ":joint_limits_wrapper",
        ":joint_position_sensor_wrapper",
        ":joint_position_wrapper",
        ":joint_velocity_estimator_wrapper",
        ":manipulator_kinematics_wrapper",
        "//intrinsic_control/intrinsic/icon/control/c_api:icon_c_api",
    ],
)
