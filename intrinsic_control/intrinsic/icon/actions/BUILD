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
load("@com_google_protobuf//bazel:py_proto_library.bzl", "py_proto_library")
load("//bazel:cc_macros.bzl", "cc_library", "cc_test")
load("//bazel:protos.bzl", "proto_library")
load("//bazel:python.bzl", "py_library", "py_test")
load("//intrinsic_sdk/bazel:go_macros.bzl", "go_proto_library")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "action_utils",
    srcs = ["action_utils.cc"],
    hdrs = ["action_utils.h"],
    deps = [
        "//intrinsic_control/intrinsic/icon/release:source_location",
        "//intrinsic_sdk/intrinsic/util/proto:descriptors",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/icon/proto/v1:types_cc_proto",
    ],
)

cc_test(
    name = "action_utils_test",
    srcs = ["action_utils_test.cc"],
    deps = [
        ":action_utils",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
        "@com_github_inazarenko_protobuf_matchers//protobuf-matchers",
        "@com_google_cel_cpp//internal:testing_no_main",
        "@com_google_googletest//:gtest_main",
        "@intrinsic_apis//intrinsic/icon/proto:cart_space_cc_proto",
        "@intrinsic_apis//intrinsic/icon/proto:joint_space_cc_proto",
        "@intrinsic_apis//intrinsic/icon/proto/v1:types_cc_proto",
    ],
)

proto_library(
    name = "streaming_joint_position_proto",
    srcs = ["streaming_joint_position.proto"],
    deps = [
        "@intrinsic_apis//intrinsic/icon/proto:joint_space_proto",
        "@intrinsic_apis//intrinsic/kinematics/types:joint_limits_proto",
    ],
)

cc_proto_library(
    name = "streaming_joint_position_cc_proto",
    deps = [":streaming_joint_position_proto"],
)

py_proto_library(
    name = "streaming_joint_position_py_pb2",
    deps = [":streaming_joint_position_proto"],
)

go_proto_library(
    name = "streaming_joint_position_go_proto",
    importpath = "intrinsic/icon/actions/streaming_joint_position_go_proto",
    protos = [":streaming_joint_position_proto"],
    deps = [
        "@intrinsic_apis//intrinsic/icon/proto:joint_space_go_proto",
        "@intrinsic_apis//intrinsic/kinematics/types:joint_limits_go_proto",
    ],
)

cc_library(
    name = "streaming_joint_position_info",
    srcs = ["streaming_joint_position_info.cc"],
    hdrs = ["streaming_joint_position_info.h"],
    deps = [
        ":streaming_joint_position_cc_proto",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "@abseil-cpp//absl/types:span",
        "@intrinsic_apis//intrinsic/icon/proto:joint_space_cc_proto",
    ],
)

proto_library(
    name = "joint_impedance_action_proto",
    srcs = ["joint_impedance_action.proto"],
    deps = [
        "@intrinsic_apis//intrinsic/icon/proto:joint_space_proto",
        "@intrinsic_apis//intrinsic/kinematics/types:joint_limits_proto",
    ],
)

cc_proto_library(
    name = "joint_impedance_action_cc_proto",
    deps = [":joint_impedance_action_proto"],
)

cc_library(
    name = "joint_impedance_action_info",
    hdrs = ["joint_impedance_action_info.h"],
    deps = [":joint_impedance_action_cc_proto"],
)

proto_library(
    name = "cartesian_impedance_proto",
    srcs = ["cartesian_impedance.proto"],
    deps = [
        ":cartesian_impedance_reference_generators_proto",
        "@intrinsic_apis//intrinsic/icon/proto:cart_space_proto",
        "@intrinsic_apis//intrinsic/icon/proto:joint_space_proto",
        "@intrinsic_apis//intrinsic/icon/proto:matrix_proto",
        "@intrinsic_apis//intrinsic/kinematics/types:joint_limits_proto",
        "@intrinsic_apis//intrinsic/math/proto:vector3_proto",
    ],
)

cc_proto_library(
    name = "cartesian_impedance_cc_proto",
    deps = [":cartesian_impedance_proto"],
)

py_proto_library(
    name = "cartesian_impedance_py_pb2",
    deps = [":cartesian_impedance_proto"],
)

proto_library(
    name = "cartesian_impedance_reference_generators_proto",
    srcs = ["cartesian_impedance_reference_generators.proto"],
)

py_proto_library(
    name = "cartesian_impedance_reference_generators_py_pb2",
    deps = [":cartesian_impedance_reference_generators_proto"],
)

cc_proto_library(
    name = "cartesian_impedance_reference_generators_cc_proto",
    deps = [":cartesian_impedance_reference_generators_proto"],
)

proto_library(
    name = "cartesian_admittance_action_proto",
    srcs = ["cartesian_admittance_action.proto"],
    deps = [
        ":cartesian_impedance_proto",
        "@intrinsic_apis//intrinsic/icon/proto:cart_space_proto",
        "@intrinsic_apis//intrinsic/math/proto:pose_proto",
        "@intrinsic_apis//intrinsic/math/proto:twist_proto",
        "@intrinsic_apis//intrinsic/math/proto:vector3_proto",
    ],
)

cc_proto_library(
    name = "cartesian_admittance_action_cc_proto",
    deps = [":cartesian_admittance_action_proto"],
)

py_proto_library(
    name = "cartesian_admittance_action_py_pb2",
    deps = [":cartesian_admittance_action_proto"],
)

proto_library(
    name = "impedance_trajectory_generators_proto",
    srcs = ["impedance_trajectory_generators.proto"],
    deps = [
        ":cartesian_impedance_proto",
        "@com_google_protobuf//:duration_proto",
        "@intrinsic_apis//intrinsic/icon/proto:cart_space_proto",
    ],
)

py_proto_library(
    name = "impedance_trajectory_generators_py_pb2",
    deps = [":impedance_trajectory_generators_proto"],
)

cc_proto_library(
    name = "impedance_trajectory_generators_cc_proto",
    deps = [":impedance_trajectory_generators_proto"],
)

cc_library(
    name = "cartesian_impedance_parameters",
    srcs = ["cartesian_impedance_parameters.cc"],
    hdrs = ["cartesian_impedance_parameters.h"],
    deps = [
        ":cartesian_impedance_cc_proto",
        ":cartesian_impedance_reference_generators_cc_proto",
        "//intrinsic_control/intrinsic/icon/proto:cart_space_conversion",
        "//intrinsic_control/intrinsic/icon/proto:eigen_conversion",
        "//intrinsic_control/intrinsic/icon/utils:realtime_guard",
        "//intrinsic_kinematics/intrinsic/kinematics/types:cartesian_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:proto_conversion",
        "//intrinsic_sdk/intrinsic/math:twist",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@intrinsic_apis//intrinsic/icon/proto:cart_space_cc_proto",
        "@intrinsic_apis//intrinsic/icon/proto:joint_space_cc_proto",
        "@intrinsic_apis//intrinsic/icon/proto:matrix_cc_proto",
    ],
)

cc_test(
    name = "cartesian_impedance_parameters_test",
    srcs = ["cartesian_impedance_parameters_test.cc"],
    deps = [
        ":cartesian_impedance_cc_proto",
        ":cartesian_impedance_parameters",
        ":cartesian_impedance_reference_generators_cc_proto",
        "//intrinsic_kinematics/intrinsic/kinematics/types:cartesian_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/eigenmath:matchers",
        "//intrinsic_sdk/intrinsic/eigenmath:rotation_utils",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:twist",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@com_github_inazarenko_protobuf_matchers//protobuf-matchers",
        "@com_google_cel_cpp//internal:testing_no_main",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "cartesian_admittance_info",
    hdrs = ["cartesian_admittance_info.h"],
    deps = [
        ":cartesian_admittance_action_cc_proto",
        ":compliance_info",
    ],
)

cc_library(
    name = "cartesian_impedance_info",
    hdrs = ["cartesian_impedance_info.h"],
    deps = [
        ":cartesian_admittance_action_cc_proto",
        ":compliance_info",
    ],
)

cc_library(
    name = "point_to_point_move_info",
    srcs = ["point_to_point_move_info.cc"],
    hdrs = ["point_to_point_move_info.h"],
    deps = [
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "@abseil-cpp//absl/types:optional",
        "@abseil-cpp//absl/types:span",
        "@intrinsic_apis//intrinsic/icon/actions:point_to_point_move_cc_proto",
        "@intrinsic_apis//intrinsic/icon/proto:joint_space_cc_proto",
    ],
)

cc_test(
    name = "point_to_point_move_info_test",
    srcs = ["point_to_point_move_info_test.cc"],
    deps = [
        ":point_to_point_move_info",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "@abseil-cpp//absl/status:status_matchers",
        "@com_google_cel_cpp//internal:testing_no_main",
        "@com_google_googletest//:gtest_main",
        "@intrinsic_apis//intrinsic/icon/proto:joint_space_cc_proto",
    ],
)

cc_library(
    name = "point_to_point_move_signature",
    srcs = ["point_to_point_move_signature.cc"],
    hdrs = ["point_to_point_move_signature.h"],
    deps = [
        ":action_utils",
        ":point_to_point_move_info",
        "//intrinsic_control/intrinsic/icon/common:builtins",
        "@abseil-cpp//absl/log:check",
        "@intrinsic_apis//intrinsic/icon/proto/v1:types_cc_proto",
    ],
)

cc_test(
    name = "point_to_point_move_signature_test",
    srcs = ["point_to_point_move_signature_test.cc"],
    deps = [
        ":point_to_point_move_info",
        ":point_to_point_move_signature",
        "@com_google_googletest//:gtest_main",
        "@intrinsic_apis//intrinsic/icon/proto/v1:types_cc_proto",
    ],
)

proto_library(
    name = "linear_gripper_move_proto",
    srcs = ["linear_gripper_move.proto"],
)

cc_proto_library(
    name = "linear_gripper_move_cc_proto",
    deps = [":linear_gripper_move_proto"],
)

py_proto_library(
    name = "linear_gripper_move_py_pb2",
    deps = [":linear_gripper_move_proto"],
)

cc_library(
    name = "linear_gripper_move_info",
    srcs = ["linear_gripper_move_info.cc"],
    hdrs = ["linear_gripper_move_info.h"],
    deps = [
        ":action_utils",
        ":linear_gripper_move_cc_proto",
        "//intrinsic_control/intrinsic/icon/common:builtins",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/types:optional",
        "@intrinsic_apis//intrinsic/icon/proto/v1:types_cc_proto",
    ],
)

cc_test(
    name = "linear_gripper_move_info_test",
    srcs = ["linear_gripper_move_info_test.cc"],
    deps = [
        ":linear_gripper_move_info",
        "@com_google_googletest//:gtest_main",
        "@intrinsic_apis//intrinsic/icon/proto/v1:types_cc_proto",
    ],
)

cc_library(
    name = "tare_force_torque_sensor_info",
    hdrs = ["tare_force_torque_sensor_info.h"],
    deps = ["@intrinsic_apis//intrinsic/icon/actions:tare_force_torque_sensor_cc_proto"],
)

cc_library(
    name = "tare_force_torque_sensor_signature",
    srcs = ["tare_force_torque_sensor_signature.cc"],
    hdrs = ["tare_force_torque_sensor_signature.h"],
    deps = [
        ":action_utils",
        ":tare_force_torque_sensor_info",
        "//intrinsic_control/intrinsic/icon/common:builtins",
        "@abseil-cpp//absl/log:check",
        "@intrinsic_apis//intrinsic/icon/proto/v1:types_cc_proto",
    ],
)

cc_test(
    name = "tare_force_torque_sensor_signature_test",
    srcs = ["tare_force_torque_sensor_signature_test.cc"],
    deps = [
        ":tare_force_torque_sensor_info",
        ":tare_force_torque_sensor_signature",
        "@com_google_googletest//:gtest_main",
        "@intrinsic_apis//intrinsic/icon/proto/v1:types_cc_proto",
    ],
)

proto_library(
    name = "joint_jogging_proto",
    srcs = ["joint_jogging.proto"],
    deps = [
        "@intrinsic_apis//intrinsic/icon/proto:joint_space_proto",
        "@intrinsic_apis//intrinsic/kinematics/types:joint_limits_proto",
    ],
)

cc_proto_library(
    name = "joint_jogging_cc_proto",
    deps = [":joint_jogging_proto"],
)

py_proto_library(
    name = "joint_jogging_py_pb2",
    deps = [":joint_jogging_proto"],
)

go_proto_library(
    name = "joint_jogging_go_proto",
    importpath = "intrinsic/icon/actions/joint_jogging_go_proto",
    protos = [":joint_jogging_proto"],
    deps = [
        "@intrinsic_apis//intrinsic/icon/proto:joint_space_go_proto",
        "@intrinsic_apis//intrinsic/kinematics/types:joint_limits_go_proto",
    ],
)

cc_library(
    name = "joint_jogging_info",
    hdrs = ["joint_jogging_info.h"],
    deps = [
        ":joint_jogging_cc_proto",
    ],
)

cc_library(
    name = "adio_info",
    hdrs = ["adio_info.h"],
    deps = ["@intrinsic_apis//intrinsic/icon/actions:adio_cc_proto"],
)

cc_library(
    name = "simple_gripper_info",
    hdrs = ["simple_gripper_info.h"],
    deps = ["@intrinsic_apis//intrinsic/icon/actions:simple_gripper_cc_proto"],
)

cc_library(
    name = "native_hand_guiding_info",
    hdrs = ["native_hand_guiding_info.h"],
    deps = ["@com_google_protobuf//:protobuf"],
)

cc_library(
    name = "simple_gripper_signature",
    srcs = ["simple_gripper_signature.cc"],
    hdrs = ["simple_gripper_signature.h"],
    deps = [
        ":action_utils",
        ":simple_gripper_info",
        "//intrinsic_control/intrinsic/icon/common:builtins",
        "@abseil-cpp//absl/log:check",
        "@intrinsic_apis//intrinsic/icon/proto/v1:types_cc_proto",
    ],
)

cc_library(
    name = "joint_jogging_signature",
    srcs = ["joint_jogging_signature.cc"],
    hdrs = ["joint_jogging_signature.h"],
    deps = [
        ":action_utils",
        ":joint_jogging_info",
        "@abseil-cpp//absl/log:check",
        "@intrinsic_apis//intrinsic/icon/proto/v1:types_cc_proto",
    ],
)

cc_library(
    name = "native_hand_guiding_signature",
    srcs = ["native_hand_guiding_signature.cc"],
    hdrs = ["native_hand_guiding_signature.h"],
    deps = [
        ":action_utils",
        ":native_hand_guiding_info",
        "@abseil-cpp//absl/log:check",
        "@intrinsic_apis//intrinsic/icon/proto/v1:types_cc_proto",
    ],
)

cc_test(
    name = "native_hand_guiding_signature_test",
    srcs = ["native_hand_guiding_signature_test.cc"],
    deps = [
        ":native_hand_guiding_info",
        ":native_hand_guiding_signature",
        "@com_google_googletest//:gtest_main",
        "@intrinsic_apis//intrinsic/icon/proto/v1:types_cc_proto",
    ],
)

cc_test(
    name = "simple_gripper_signature_test",
    srcs = ["simple_gripper_signature_test.cc"],
    deps = [
        ":simple_gripper_info",
        ":simple_gripper_signature",
        "@com_google_googletest//:gtest_main",
        "@intrinsic_apis//intrinsic/icon/proto/v1:types_cc_proto",
    ],
)

cc_library(
    name = "empty_action_signature",
    srcs = ["empty_action_signature.cc"],
    hdrs = ["empty_action_signature.h"],
    deps = [
        ":action_utils",
        "//intrinsic_control/intrinsic/icon/common:builtins",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings",
        "@intrinsic_apis//intrinsic/icon/proto/v1:types_cc_proto",
    ],
)

cc_library(
    name = "streaming_joint_position_signature",
    srcs = ["streaming_joint_position_signature.cc"],
    hdrs = ["streaming_joint_position_signature.h"],
    deps = [
        ":action_utils",
        ":streaming_joint_position_info",
        "//intrinsic_control/intrinsic/icon/common:builtins",
        "@abseil-cpp//absl/log:check",
        "@intrinsic_apis//intrinsic/icon/proto/v1:types_cc_proto",
    ],
)

cc_library(
    name = "joint_impedance_action_signature",
    srcs = ["joint_impedance_action_signature.cc"],
    hdrs = ["joint_impedance_action_signature.h"],
    deps = [
        ":action_utils",
        ":joint_impedance_action_info",
        "//intrinsic_control/intrinsic/icon/common:builtins",
        "@abseil-cpp//absl/log:check",
        "@intrinsic_apis//intrinsic/icon/proto/v1:types_cc_proto",
    ],
)

cc_library(
    name = "trajectory_tracking_action_signature",
    srcs = ["trajectory_tracking_action_signature.cc"],
    hdrs = ["trajectory_tracking_action_signature.h"],
    deps = [
        ":action_utils",
        ":trajectory_tracking_action_info",
        "//intrinsic_control/intrinsic/icon/common:builtins",
        "@abseil-cpp//absl/log:check",
        "@intrinsic_apis//intrinsic/icon/proto/v1:types_cc_proto",
    ],
)

cc_test(
    name = "trajectory_tracking_action_signature_test",
    srcs = ["trajectory_tracking_action_signature_test.cc"],
    deps = [
        ":trajectory_tracking_action_info",
        ":trajectory_tracking_action_signature",
        "@com_google_googletest//:gtest_main",
        "@intrinsic_apis//intrinsic/icon/proto/v1:types_cc_proto",
    ],
)

proto_library(
    name = "homing_action_proto",
    srcs = ["homing_action.proto"],
)

cc_proto_library(
    name = "homing_action_cc_proto",
    deps = [":homing_action_proto"],
)

py_proto_library(
    name = "homing_action_py_pb2",
    deps = [":homing_action_proto"],
)

cc_library(
    name = "homing_info",
    hdrs = ["homing_info.h"],
    deps = [":homing_action_cc_proto"],
)

cc_library(
    name = "homing_signature",
    srcs = ["homing_signature.cc"],
    hdrs = ["homing_signature.h"],
    deps = [
        ":action_utils",
        ":homing_info",
        "@abseil-cpp//absl/log:check",
        "@intrinsic_apis//intrinsic/icon/proto/v1:types_cc_proto",
    ],
)

cc_test(
    name = "homing_signature_test",
    srcs = ["homing_signature_test.cc"],
    deps = [
        ":homing_info",
        ":homing_signature",
        "@com_google_googletest//:gtest_main",
        "@intrinsic_apis//intrinsic/icon/proto/v1:types_cc_proto",
    ],
)

cc_library(
    name = "trajectory_tracking_action_info",
    hdrs = ["trajectory_tracking_action_info.h"],
    deps = ["@intrinsic_apis//intrinsic/icon/actions:trajectory_tracking_action_cc_proto"],
)

cc_library(
    name = "stop_info",
    srcs = [],
    hdrs = ["stop_info.h"],
)

py_library(
    name = "trajectory_tracking_action_utils",
    srcs = ["trajectory_tracking_action_utils.py"],
    visibility = ["//intrinsic_control/intrinsic/icon/python:__subpackages__"],
    deps = [
        "//intrinsic_control/intrinsic/icon/python:icon",
        "@intrinsic_apis//intrinsic/icon/actions:trajectory_tracking_action_py_pb2",
        "@intrinsic_apis//intrinsic/icon/proto:joint_space_py_pb2",
    ],
)

py_library(
    name = "tare_force_torque_sensor_utils",
    srcs = ["tare_force_torque_sensor_utils.py"],
    visibility = ["//intrinsic_control/intrinsic/icon/python:__subpackages__"],
    deps = [
        "//intrinsic_control/intrinsic/icon/python:icon",
        "@intrinsic_apis//intrinsic/icon/actions:tare_force_torque_sensor_py_pb2",
    ],
)

py_library(
    name = "point_to_point_move_utils",
    srcs = ["point_to_point_move_utils.py"],
    visibility = ["//intrinsic_control/intrinsic/icon/python:__subpackages__"],
    deps = [
        "//intrinsic_control/intrinsic/icon/python:icon",
        "@intrinsic_apis//intrinsic/icon/actions:point_to_point_move_py_pb2",
        "@intrinsic_apis//intrinsic/icon/proto:joint_space_py_pb2",
        "@intrinsic_apis//intrinsic/kinematics/types:joint_limits_py_pb2",
    ],
)

py_library(
    name = "stop_utils",
    srcs = ["stop_utils.py"],
    visibility = ["//intrinsic:intrinsic_internal"],
    deps = ["//intrinsic_control/intrinsic/icon/python:icon"],
)

py_library(
    name = "wait_for_settling_utils",
    srcs = ["wait_for_settling_utils.py"],
    visibility = ["//intrinsic_control/intrinsic/icon/python:__subpackages__"],
    deps = [
        "//intrinsic_control/intrinsic/icon/python:icon",
        "@intrinsic_apis//intrinsic/icon/actions:wait_for_settling_action_py_pb2",
    ],
)

py_library(
    name = "adio_utils",
    srcs = ["adio_utils.py"],
    deps = [
        "//intrinsic_control/intrinsic/icon/python:icon",
        "@intrinsic_apis//intrinsic/icon/actions:adio_py_pb2",
    ],
)

py_test(
    name = "adio_utils_test",
    srcs = ["adio_utils_test.py"],
    deps = [
        ":adio_utils",
        "@com_google_absl_py//absl/testing:absltest",
        "@intrinsic_apis//intrinsic/icon/actions:adio_py_pb2",
    ],
)

py_library(
    name = "cartesian_admittance_utils",
    srcs = ["cartesian_admittance_utils.py"],
    visibility = ["//intrinsic:intrinsic_internal"],
    deps = [
        ":cartesian_admittance_action_py_pb2",
        ":cartesian_impedance_py_pb2",
        ":cartesian_impedance_reference_generators_py_pb2",
        "//intrinsic/manipulation/skills/force:utils",
        "//intrinsic_control/intrinsic/icon/proto:matrix_conversions",
        "//intrinsic_control/intrinsic/icon/python:icon",
        "//intrinsic_sdk/intrinsic/math/python:data_types",
        "@ai_intrinsic_sdks_pip_deps//numpy",
        "@intrinsic_apis//intrinsic/icon/proto:cart_space_py_pb2",
        "@intrinsic_apis//intrinsic/kinematics/types:joint_limits_py_pb2",
    ],
)

py_test(
    name = "cartesian_admittance_utils_test",
    srcs = ["cartesian_admittance_utils_test.py"],
    deps = [
        ":cartesian_admittance_action_py_pb2",
        ":cartesian_admittance_utils",
        ":cartesian_impedance_py_pb2",
        ":cartesian_impedance_reference_generators_py_pb2",
        "//intrinsic_control/intrinsic/icon/proto:matrix_conversions",
        "//intrinsic_sdk/intrinsic/math/python:data_types",
        "@ai_intrinsic_sdks_pip_deps//numpy",
        "@com_google_absl_py//absl/testing:absltest",
        "@intrinsic_apis//intrinsic/icon/proto:cart_space_py_pb2",
        "@intrinsic_apis//intrinsic/kinematics/types:joint_limits_py_pb2",
    ],
)

py_test(
    name = "wait_for_settling_utils_test",
    srcs = ["wait_for_settling_utils_test.py"],
    deps = [
        ":wait_for_settling_utils",
        "@com_google_absl_py//absl/testing:absltest",
        "@intrinsic_apis//intrinsic/icon/actions:wait_for_settling_action_py_pb2",
    ],
)

py_test(
    name = "point_to_point_move_utils_test",
    srcs = ["point_to_point_move_utils_test.py"],
    deps = [
        ":point_to_point_move_utils",
        "@com_google_absl_py//absl/testing:absltest",
        "@intrinsic_apis//intrinsic/icon/actions:point_to_point_move_py_pb2",
        "@intrinsic_apis//intrinsic/kinematics/types:joint_limits_py_pb2",
    ],
)

py_test(
    name = "trajectory_tracking_action_utils_test",
    srcs = ["trajectory_tracking_action_utils_test.py"],
    deps = [
        ":trajectory_tracking_action_utils",
        "@com_google_absl_py//absl/testing:absltest",
        "@com_google_protobuf//:protobuf_python",
        "@intrinsic_apis//intrinsic/icon/actions:trajectory_tracking_action_py_pb2",
        "@intrinsic_apis//intrinsic/icon/proto:joint_space_py_pb2",
    ],
)

py_test(
    name = "tare_force_torque_sensor_utils_test",
    srcs = ["tare_force_torque_sensor_utils_test.py"],
    deps = [
        ":tare_force_torque_sensor_utils",
        "@com_google_absl_py//absl/testing:absltest",
        "@intrinsic_apis//intrinsic/icon/actions:tare_force_torque_sensor_py_pb2",
    ],
)

py_test(
    name = "stop_utils_test",
    srcs = ["stop_utils_test.py"],
    deps = [
        ":stop_utils",
        "@com_google_absl_py//absl/testing:absltest",
        "@com_google_protobuf//:protobuf_python",
    ],
)

proto_library(
    name = "force_primitive_proto",
    srcs = ["force_primitive.proto"],
    deps = [
        "//intrinsic/manipulation/skills/force:contact_stiffness_proto",
        "//intrinsic_control/intrinsic/icon/control/primitives/force_control/proto:force_primitives_proto",
        "@intrinsic_apis//intrinsic/icon/equipment:force_control_settings_proto",
        "@intrinsic_apis//intrinsic/icon/proto:cart_space_proto",
        "@intrinsic_apis//intrinsic/kinematics/types:joint_limits_proto",
        "@intrinsic_apis//intrinsic/math/proto:pose_proto",
    ],
)

py_proto_library(
    name = "force_primitive_py_pb2",
    deps = [":force_primitive_proto"],
)

cc_proto_library(
    name = "force_primitive_cc_proto",
    deps = [":force_primitive_proto"],
)

cc_library(
    name = "force_primitive_info",
    hdrs = ["force_primitive_info.h"],
    deps = [
        ":cartesian_admittance_action_cc_proto",
        ":compliance_info",
        ":force_primitive_cc_proto",
    ],
)

cc_library(
    name = "compliance_info",
    hdrs = ["compliance_info.h"],
)

py_library(
    name = "force_primitive_utils",
    srcs = ["force_primitive_utils.py"],
    deps = [
        ":cartesian_admittance_action_py_pb2",
        ":cartesian_admittance_utils",
        ":force_primitive_py_pb2",
        "//intrinsic/manipulation/skills/force:contact_stiffness_py_pb2",
        "//intrinsic/manipulation/skills/force:utils",
        "//intrinsic_control/intrinsic/icon/control/primitives/force_control/proto:force_primitives_py_pb2",
        "//intrinsic_control/intrinsic/icon/python:icon",
        "//intrinsic_sdk/intrinsic/math/python:data_types",
        "//intrinsic_sdk/intrinsic/math/python:proto_conversion",
    ],
)

py_test(
    name = "force_primitive_utils_test",
    srcs = ["force_primitive_utils_test.py"],
    deps = [
        ":force_primitive_py_pb2",
        ":force_primitive_utils",
        "//intrinsic/manipulation/skills/force:contact_stiffness_py_pb2",
        "//intrinsic/manipulation/skills/force:utils",
        "//intrinsic_control/intrinsic/icon/control/primitives/force_control/proto:force_primitives_py_pb2",
        "//intrinsic_sdk/intrinsic/math/python:data_types",
        "//intrinsic_sdk/intrinsic/solutions/testing:compare",
        "@com_google_absl_py//absl/testing:absltest",
        "@intrinsic_apis//intrinsic/math/proto:point_py_pb2",
        "@intrinsic_apis//intrinsic/math/proto:pose_py_pb2",
        "@intrinsic_apis//intrinsic/math/proto:quaternion_py_pb2",
    ],
)
