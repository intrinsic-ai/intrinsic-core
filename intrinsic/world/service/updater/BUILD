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

load("@com_github_grpc_grpc//bazel:cc_grpc_library.bzl", "cc_grpc_library")
load("@com_google_protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("//bazel:cc_macros.bzl", "cc_binary", "cc_library")
load("//bazel:cc_oci_image.bzl", "cc_oci_image")
load("//bazel:go_macros.bzl", "go_proto_library")
load("//bazel:protos.bzl", "proto_library")

package(default_visibility = [
    "//visibility:public",
])

filegroup(
    name = "world_updater_template",
    srcs = ["world_updater.yaml"],
    visibility = [
        "//intrinsic:intrinsic_internal",
    ],
)

cc_library(
    name = "world_updater",
    srcs = ["world_updater.cc"],
    hdrs = ["world_updater.h"],
    deps = [
        ":world_updater_cc_grpc_proto",
        ":world_updater_cc_proto",
        ":world_updater_config_cc_proto",
        "//intrinsic/logging/proto:log_item_cc_proto",
        "//intrinsic/platform/pubsub",
        "//intrinsic/platform/pubsub:subscription",
        "//intrinsic/resources/client:resource_registry_client_interface",
        "//intrinsic/stats:metrics_utils",
        "//intrinsic/util:proto_time",
        "//intrinsic/util/status:ret_check",
        "//intrinsic/util/status:status_macros",
        "//intrinsic/util/status:status_macros_grpc",
        "//intrinsic/world:print_world_lib",
        "//intrinsic/world/objects:object_world",
        "//intrinsic/world/proto/v1:pubsub_world_updates_cc_proto",
        "//intrinsic/world/pubsub:tf_echo_guard_interfaces",
        "//intrinsic/world/service:world_storage",
        "//intrinsic/world/service/objects:object_world_updates_utils",
        "//intrinsic/world/util:ros_joint_state_util",
        "//intrinsic/world/util:tf_frame_util",
        "//intrinsic_control/intrinsic/icon/cc_client:client",
        "//intrinsic_control/intrinsic/icon/common:topic_names",
        "//intrinsic_control/intrinsic/icon/equipment:channel_factory",
        "//intrinsic_control/intrinsic/icon/equipment:equipment_utils",
        "//intrinsic_control/intrinsic/icon/utils:async_buffer",
        "//intrinsic_sdk/intrinsic/skills/cc:skill_utils",
        "//intrinsic_sdk/intrinsic/util/thread",
        "//third_party/ros2/ros_interfaces/jazzy/sensor_msgs/msg:joint_state_cc_proto",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/cleanup",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@com_github_grpc_grpc//:grpc++",
        "@intrinsic_apis//intrinsic/icon/equipment:icon_equipment_cc_proto",
        "@intrinsic_apis//intrinsic/icon/proto:part_status_cc_proto",
        "@intrinsic_apis//intrinsic/resources/proto:resource_registry_cc_proto",
        "@io_opentelemetry_cpp//api",
    ],
)

proto_library(
    name = "world_updater_proto",
    srcs = ["world_updater.proto"],
)

cc_proto_library(
    name = "world_updater_cc_proto",
    deps = [
        ":world_updater_proto",
    ],
)

cc_grpc_library(
    name = "world_updater_cc_grpc_proto",
    srcs = [":world_updater_proto"],
    generate_mocks = True,
    grpc_only = True,
    deps = [
        ":world_updater_cc_proto",
    ],
)

go_proto_library(
    name = "world_updater_go_proto",
    compilers = [
        "@io_bazel_rules_go//proto:go_grpc_v2",
        "@io_bazel_rules_go//proto:go_proto",
    ],
    importpath = "intrinsic/world/service/updater/world_updater_go_proto",
    protos = [":world_updater_proto"],
)

proto_library(
    name = "world_updater_config_proto",
    srcs = ["world_updater_config.proto"],
)

cc_proto_library(
    name = "world_updater_config_cc_proto",
    deps = [
        ":world_updater_config_proto",
    ],
)

cc_binary(
    name = "world_updater_main",
    srcs = ["world_updater_main.cc"],
    deps = [
        ":world_updater",
        ":world_updater_config_cc_proto",
        "//intrinsic/resources/client:resource_registry_client_cc",
        "//intrinsic/stats:opencensus",
        "//intrinsic/util/status:status_macros",
        "//intrinsic/world/proto:object_world_service_cc_grpc_proto",
        "//intrinsic/world/service:world_service_cc_grpc_proto",
        "//intrinsic/world/service/robot_calibration:robot_kinematics_updater",
        "//intrinsic_control/intrinsic/icon/equipment:channel_factory",
        "//intrinsic_control/intrinsic/icon/release/portable:init_intrinsic",
        "//intrinsic_sdk/intrinsic/connect/cc/grpc:channel",
        "//intrinsic_sdk/intrinsic/util/thread",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/log:flags",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@com_github_grpc_grpc//:grpc++",
        "@com_github_grpc_grpc//:grpc_security_base",
        "@com_google_googleapis//google/longrunning:longrunning_cc_grpc",
        "@com_google_googleapis//google/longrunning:longrunning_cc_proto",
        "@intrinsic_apis//intrinsic/assets/proto:asset_deployment_cc_grpc_proto",
    ],
)

cc_oci_image(
    name = "world-updater-image",
    binary = ":world_updater_main",
    data_path = "/",
    visibility = [
        "//intrinsic_runtime/kubernetes/intrinsic_base:__pkg__",
    ],
)
