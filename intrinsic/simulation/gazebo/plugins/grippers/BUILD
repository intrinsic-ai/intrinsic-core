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

# Gripper functionalities in simulation using Gazebo

load("@com_google_protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("//bazel:cc_macros.bzl", "cc_library")
load("//bazel:protos.bzl", "proto_library")

package(
    default_visibility = ["//visibility:public"],
)

proto_library(
    name = "gripper_proto",
    srcs = ["gripper.proto"],
)

cc_proto_library(
    name = "gripper_cc_proto",
    deps = [":gripper_proto"],
)

cc_library(
    name = "link_articulation_util",
    srcs = ["link_articulation_util.cc"],
    hdrs = ["link_articulation_util.h"],
    deps = [
        "//intrinsic/simulation/gazebo/components:articulation_component",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:cord",
        "@gloop//gloop/util/gtl:flat_map",
        "@gloop//gloop/util/gtl:flat_set",
        "@gz-sim",
        "@sdformat",
    ],
)

cc_library(
    name = "fixed_joint_gripper",
    srcs = [
        "fixed_joint_gripper.cc",
    ],
    hdrs = [
        "fixed_joint_gripper.h",
    ],
    deps = [
        ":gripper_cc_proto",
        ":link_articulation_util",
        "//intrinsic/simulation/gazebo/components:articulation_component",
        "//intrinsic/simulation/gazebo/components:interaction_component",
        "//intrinsic/simulation/gazebo/plugins:device_util",
        "//intrinsic_sdk/intrinsic/util/status:return",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/base:log_severity",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@gz-sim",
    ],
)

cc_library(
    name = "fixed_joint_gripper_plugin",
    srcs = [
        "fixed_joint_gripper_plugin.cc",
    ],
    hdrs = [
        "fixed_joint_gripper_plugin.h",
    ],
    deps = [
        ":fixed_joint_gripper",
        ":gpio_gripper_plugin_connection",
        ":gripper_cc_proto",
        "//intrinsic/simulation/gazebo/plugins/gpio:gpio_service",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@com_google_protobuf//:protobuf",
        "@gz-msgs",
        "@gz-sim",
        "@gz-transport",
        "@intrinsic_apis//intrinsic/hardware/gripper/eoat:eoat_service_cc_proto",
        "@intrinsic_apis//intrinsic/hardware/gripper/eoat:gripper_config_cc_proto",
        "@sdformat",
    ],
)

cc_library(
    name = "actuated_gripper_plugin",
    srcs = [
        "actuated_gripper_plugin.cc",
    ],
    hdrs = [
        "actuated_gripper_plugin.h",
    ],
    deps = [
        ":actuated_gripper_plugin_connection",
        ":fixed_joint_gripper",
        ":gpio_gripper_plugin_connection",
        ":gripper_cc_proto",
        ":pid_joint_util",
        ":pinch_gripper_server_impl",
        "//intrinsic/simulation/gazebo/plugins:device_util",
        "//intrinsic/simulation/gazebo/plugins:gravity_compensator",
        "//intrinsic/simulation/gazebo/plugins/gpio:gpio_service",
        "//intrinsic_hardware/intrinsic/hardware/gripper/service/proto:pinch_gripper_server_cc_proto",
        "//intrinsic_sdk/intrinsic/platform/pubsub",
        "//intrinsic_sdk/intrinsic/platform/pubsub:publisher",
        "//intrinsic_sdk/intrinsic/util:macros",
        "//intrinsic_sdk/intrinsic/util/status:status_builder",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@com_google_googletest//:gtest_prod",
        "@com_google_protobuf//:protobuf",
        "@gz-common//profiler",
        "@gz-math",
        "@gz-sim",
        "@gz-transport",
        "@intrinsic_apis//intrinsic/hardware/gripper:gripper_cc_proto",
        "@intrinsic_apis//intrinsic/hardware/gripper/eoat:eoat_service_cc_proto",
        "@intrinsic_apis//intrinsic/hardware/gripper/eoat:gripper_config_cc_proto",
        "@sdformat",
    ],
)

cc_library(
    name = "actuated_gripper_plugin_connection",
    srcs = [
        "actuated_gripper_plugin_connection.cc",
    ],
    hdrs = [
        "actuated_gripper_plugin_connection.h",
    ],
    deps = [
        "//intrinsic_hardware/intrinsic/hardware/gripper/service/proto:pinch_gripper_server_cc_proto",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@intrinsic_apis//intrinsic/hardware/gripper:gripper_cc_proto",
    ],
)

cc_library(
    name = "gpio_gripper_plugin_connection",
    srcs = ["gpio_gripper_plugin_connection.cc"],
    hdrs = ["gpio_gripper_plugin_connection.h"],
    deps = [
        ":gripper_cc_proto",
        "//intrinsic/simulation/gazebo/plugins/gpio:gpio_connection_interface",
        "//intrinsic/util/proto:pb_hash",
        "//intrinsic_hardware/intrinsic/hardware/gpio:gpio_service_proto_utils",
        "//intrinsic_hardware/intrinsic/hardware/gripper/eoat:gripper_utils",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@intrinsic_apis//intrinsic/hardware/gpio/v1:gpio_service_cc_proto",
        "@intrinsic_apis//intrinsic/hardware/gpio/v1:signal_cc_proto",
        "@intrinsic_apis//intrinsic/hardware/gripper/eoat:gripper_config_cc_proto",
    ],
)

cc_library(
    name = "pinch_gripper_server_impl",
    srcs = ["pinch_gripper_server_impl.cc"],
    hdrs = ["pinch_gripper_server_impl.h"],
    deps = [
        ":actuated_gripper_plugin_connection",
        "//intrinsic_sdk/intrinsic/platform/pubsub",
        "//intrinsic_sdk/intrinsic/platform/pubsub:publisher",
        "//intrinsic_sdk/intrinsic/platform/pubsub:subscription",
        "//intrinsic_sdk/intrinsic/platform/pubsub:topic_config",
        "//intrinsic_sdk/intrinsic/util/status:status_builder",
        "//intrinsic_sdk/intrinsic/util/status:status_conversion_grpc",
        "//intrinsic_sdk/intrinsic/util/status:status_macros_grpc",
        "//intrinsic_hardware/intrinsic/hardware/gripper:gripper_equipment_cc_proto",
        "//intrinsic_hardware/intrinsic/hardware/gripper/service/proto:pinch_gripper_server_cc_grpc_proto",
        "//intrinsic_hardware/intrinsic/hardware/gripper/service/proto:pinch_gripper_server_cc_proto",
        "//intrinsic_sdk/intrinsic/util/thread",
        "//intrinsic_sdk/intrinsic/util/thread:stop_token",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:no_destructor",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        # This is needed when using grpc_cli.
        "@com_github_grpc_grpc//:grpc++",
        "@com_github_grpc_grpc//:grpc++_reflection",
        "@com_github_grpc_grpc//:grpc_security_base",
        "@intrinsic_apis//intrinsic/hardware/gripper:gripper_cc_proto",
    ],
)

cc_library(
    name = "gripper_plugins",
    srcs = [
        "actuated_gripper_plugin_register.cc",
        "fixed_joint_gripper_plugin_register.cc",
    ],
    deps = [
        ":actuated_gripper_plugin",
        ":fixed_joint_gripper_plugin",
        "@gz-plugin//:register",
        "@gz-sim",
    ],
    alwayslink = 1,
)

cc_library(
    name = "pid_joint_util",
    srcs = ["pid_joint_util.cc"],
    hdrs = [
        "pid_joint_util.h",
    ],
    deps = [
        "//intrinsic/simulation/gazebo/plugins:device_util",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@gz-math",
        "@gz-sim",
        "@sdformat",
    ],
)
