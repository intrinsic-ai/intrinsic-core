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
load("@com_github_grpc_grpc//bazel:python_rules.bzl", "py_grpc_library")
load("@com_google_protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("@com_google_protobuf//bazel:py_proto_library.bzl", "py_proto_library")
load("//bazel:cc_macros.bzl", "cc_library")
load("//bazel:go_macros.bzl", "go_proto_library")
load("//bazel:protos.bzl", "proto_library")

package(default_visibility = ["//visibility:public"])

proto_library(
    name = "robot_calibration_data_service_proto",
    srcs = ["robot_calibration_data_service.proto"],
    deps = [
        "@intrinsic_apis//intrinsic/scene/proto/v1:scene_object_updates_proto",
    ],
)

cc_proto_library(
    name = "robot_calibration_data_service_cc_proto",
    deps = [":robot_calibration_data_service_proto"],
)

go_proto_library(
    name = "robot_calibration_data_service_go_proto",
    importpath = "intrinsic/world/service/robot_calibration/robot_calibration_data_service_go_proto",
    protos = [":robot_calibration_data_service_proto"],
    deps = [
        "@intrinsic_apis//intrinsic/icon/proto:cart_space_go_proto",
        "@intrinsic_apis//intrinsic/kinematics/types:joint_limits_go_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:scene_object_updates_go_proto",
    ],
)

cc_grpc_library(
    name = "robot_calibration_data_service_cc_grpc_proto",
    srcs = [":robot_calibration_data_service_proto"],
    generate_mocks = True,
    grpc_only = True,
    deps = [":robot_calibration_data_service_cc_proto"],
)

proto_library(
    name = "robot_update_service_proto",
    srcs = ["robot_update_service.proto"],
    deps = [
        ":robot_calibration_data_service_proto",
        "@intrinsic_apis//intrinsic/icon/proto:cart_space_proto",
        "@intrinsic_apis//intrinsic/kinematics/types:joint_limits_proto",
    ],
)

cc_proto_library(
    name = "robot_update_service_cc_proto",
    deps = [":robot_update_service_proto"],
)

cc_grpc_library(
    name = "robot_update_service_cc_grpc_proto",
    srcs = [":robot_update_service_proto"],
    generate_mocks = True,
    grpc_only = True,
    deps = [":robot_update_service_cc_proto"],
)

go_proto_library(
    name = "robot_update_service_go_proto",
    compilers = [
        "@io_bazel_rules_go//proto:go_grpc_v2",
        "@io_bazel_rules_go//proto:go_proto",
    ],
    importpath = "intrinsic/world/service/robot_calibration/robot_update_service_go_proto",
    protos = [":robot_update_service_proto"],
    visibility = ["//intrinsic:intrinsic_internal"],
    deps = [
        ":robot_calibration_data_service_go_proto",
        "//intrinsic_sdk/intrinsic/world/proto:object_world_updates_go_proto",
        "@intrinsic_apis//intrinsic/icon/proto:cart_space_go_proto",
        "@intrinsic_apis//intrinsic/kinematics/types:joint_limits_go_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:scene_object_updates_go_proto",
    ],
)

py_proto_library(
    name = "robot_update_service_py_pb2",
    deps = [":robot_update_service_proto"],
)

py_grpc_library(
    name = "robot_update_service_py_pb2_grpc",
    srcs = [":robot_update_service_proto"],
    deps = [":robot_update_service_py_pb2"],
)

cc_library(
    name = "robot_kinematics_updater",
    srcs = ["robot_kinematics_updater.cc"],
    hdrs = ["robot_kinematics_updater.h"],
    deps = [
        ":robot_calibration_data_service_cc_grpc_proto",
        ":robot_calibration_data_service_cc_proto",
        ":robot_update_service_cc_grpc_proto",
        ":robot_update_service_cc_proto",
        "//intrinsic/scene/config:scene_object_config_updater",
        "//intrinsic/scene/util:scene_object_creation",
        "//intrinsic_control/intrinsic/icon/cc_client:client",
        "//intrinsic_control/intrinsic/icon/equipment:channel_factory",
        "//intrinsic_control/intrinsic/icon/equipment:equipment_utils",
        "//intrinsic_kinematics/intrinsic/kinematics/types:check_joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits_xd",
        "//intrinsic_sdk/intrinsic/connect/cc/grpc:channel",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:proto_conversion",
        "//intrinsic_sdk/intrinsic/resources/client:resource_registry_client_interface",
        "//intrinsic_sdk/intrinsic/skills/cc:skill_utils",
        "//intrinsic_sdk/intrinsic/util/grpc:channel",
        "//intrinsic_sdk/intrinsic/util/status:status_conversion_grpc",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/util/status:status_macros_grpc",
        "//intrinsic_sdk/intrinsic/world/hashing",
        "//intrinsic_sdk/intrinsic/world/objects:kinematic_object",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_client",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_ids",
        "//intrinsic_sdk/intrinsic/world/proto:object_world_service_cc_grpc_proto",
        "//intrinsic_sdk/intrinsic/world/proto:object_world_service_cc_proto",
        "//intrinsic_sdk/intrinsic/world/proto:object_world_updates_cc_proto",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/time",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_googleapis//google/longrunning:longrunning_cc_grpc",
        "@com_google_googleapis//google/longrunning:longrunning_cc_proto",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/assets/proto:asset_deployment_cc_grpc_proto",
        "@intrinsic_apis//intrinsic/assets/proto:asset_deployment_cc_proto",
        "@intrinsic_apis//intrinsic/icon/control/parts/proto/v1:realtime_part_config_cc_proto",
        "@intrinsic_apis//intrinsic/icon/proto:limit_provider_service_cc_grpc_proto",
        "@intrinsic_apis//intrinsic/icon/proto:limit_provider_service_cc_proto",
        "@intrinsic_apis//intrinsic/icon/server/config:icon_main_config_cc_proto",
        "@intrinsic_apis//intrinsic/icon/server/config:realtime_control_config_cc_proto",
        "@intrinsic_apis//intrinsic/resources/proto:resource_registry_cc_proto",
        "@intrinsic_apis//intrinsic/resources/proto:runtime_context_cc_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:object_properties_cc_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:scene_object_config_cc_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:scene_object_updates_cc_proto",
    ],
)
