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
load("//bazel:go_macros.bzl", "go_library", "go_proto_library")
load("//bazel:protos.bzl", "proto_library")

package(default_visibility = ["//visibility:public"])

proto_library(
    name = "log_dispatcher_service",
    srcs = ["log_dispatcher_service.proto"],
    deps = [
        ":log_item_proto",
        "@com_google_protobuf//:empty_proto",
        "@com_google_protobuf//:timestamp_proto",
        "@intrinsic_apis//intrinsic/logging/proto:blob_proto",
        "@intrinsic_apis//intrinsic/logging/proto:downsampler_proto",
    ],
)

cc_proto_library(
    name = "log_dispatcher_service_cc_proto",
    deps = [":log_dispatcher_service"],
)

cc_grpc_library(
    name = "log_dispatcher_service_cc_grpc",
    srcs = [":log_dispatcher_service"],
    generate_mocks = True,
    grpc_only = True,
    deps = [":log_dispatcher_service_cc_proto"],
)

go_proto_library(
    name = "log_dispatcher_service_go_proto",
    compilers = [
        "@io_bazel_rules_go//proto:go_grpc_v2",
        "@io_bazel_rules_go//proto:go_proto",
    ],
    importpath = "intrinsic/logging/proto/log_dispatcher_service_go_proto",
    protos = [":log_dispatcher_service"],
    deps = [
        ":log_item_go_proto",
        "@intrinsic_apis//intrinsic/logging/proto:bag_metadata_go_proto",
        "@intrinsic_apis//intrinsic/logging/proto:blob_go_proto",
        "@intrinsic_apis//intrinsic/logging/proto:downsampler_go_proto",
    ],
)

py_proto_library(
    name = "log_dispatcher_service_py_pb2",
    deps = [":log_dispatcher_service"],
)

py_grpc_library(
    name = "log_dispatcher_service_py_pb2_grpc",
    srcs = [":log_dispatcher_service"],
    deps = [":log_dispatcher_service_py_pb2"],
)

proto_library(
    name = "log_item_proto",
    srcs = ["log_item.proto"],
    deps = [
        "//intrinsic_sdk/intrinsic/skills/proto:skill_service_proto",
        "//third_party/ros2/ros_interfaces/jazzy/visualization_msgs/msg:marker_array_proto",
        "@com_google_googleapis//google/api:annotations_proto",
        "@com_google_protobuf//:any_proto",
        "@com_google_protobuf//:timestamp_proto",
        "@intrinsic_apis//intrinsic/executive/proto:log_items_proto",
        "@intrinsic_apis//intrinsic/icon/proto:cart_space_proto",
        "@intrinsic_apis//intrinsic/icon/proto:joint_space_proto",
        "@intrinsic_apis//intrinsic/icon/proto:part_status_proto",
        "@intrinsic_apis//intrinsic/logging/errors/proto:error_report_proto",
        "@intrinsic_apis//intrinsic/logging/proto:blob_proto",
        "@intrinsic_apis//intrinsic/logging/proto:context_proto",
        "@intrinsic_apis//intrinsic/logging/proto:critical_event_log",
        "@intrinsic_apis//intrinsic/logging/proto:flowstate_event",
        "@intrinsic_apis//intrinsic/util/status:extended_status_proto",
    ],
)

cc_proto_library(
    name = "log_item_cc_proto",
    deps = [":log_item_proto"],
)

go_proto_library(
    name = "log_item_go_proto",
    importpath = "intrinsic/logging/proto/log_item_go_proto",
    protos = [":log_item_proto"],
    deps = [
        ":visualization_context_go_proto",
        "//intrinsic_sdk/intrinsic/skills/proto:prediction_go_proto",
        "//intrinsic_sdk/intrinsic/skills/proto:skill_service_go_proto",
        "//intrinsic_sdk/intrinsic/world/proto:object_world_updates_go_proto",
        "//third_party/ros2/ros_interfaces/jazzy/visualization_msgs/msg:marker_array_go_proto",
        "@intrinsic_apis//intrinsic/executive/proto:log_items_go_proto",
        "@intrinsic_apis//intrinsic/hardware/gpio/v1:signal_go_proto",
        "@intrinsic_apis//intrinsic/hardware/gripper/eoat:eoat_service_go_proto",
        "@intrinsic_apis//intrinsic/hardware/gripper/eoat:gripper_config_go_proto",
        "@intrinsic_apis//intrinsic/hardware/gripper/service/proto:generic_gripper_go_proto",
        "@intrinsic_apis//intrinsic/icon/proto:cart_space_go_proto",
        "@intrinsic_apis//intrinsic/icon/proto:generic_part_config_go_proto",
        "@intrinsic_apis//intrinsic/icon/proto:io_block_go_proto",
        "@intrinsic_apis//intrinsic/icon/proto:joint_space_go_proto",
        "@intrinsic_apis//intrinsic/icon/proto:part_status_go_proto",
        "@intrinsic_apis//intrinsic/icon/proto:safety_status_go_proto",
        "@intrinsic_apis//intrinsic/icon/proto/v1:types_go_proto",
        "@intrinsic_apis//intrinsic/kinematics/proto:kinematics_go_proto",
        "@intrinsic_apis//intrinsic/kinematics/proto:skeleton_go_proto",
        "@intrinsic_apis//intrinsic/kinematics/types:dynamic_limits_check_mode_go_proto",
        "@intrinsic_apis//intrinsic/kinematics/types:joint_limits_go_proto",
        "@intrinsic_apis//intrinsic/kubernetes/generator/proto:auth_options_go_proto",
        "@intrinsic_apis//intrinsic/logging/errors/proto:error_report_go_proto",
        "@intrinsic_apis//intrinsic/logging/proto:bag_metadata_go_proto",
        "@intrinsic_apis//intrinsic/logging/proto:blob_go_proto",
        "@intrinsic_apis//intrinsic/logging/proto:context_go_proto",
        "@intrinsic_apis//intrinsic/logging/proto:critical_event_log_go_proto",
        "@intrinsic_apis//intrinsic/logging/proto:flowstate_event_go_proto",
        "@intrinsic_apis//intrinsic/math/proto:affine_go_proto",
        "@intrinsic_apis//intrinsic/math/proto:array_go_proto",
        "@intrinsic_apis//intrinsic/math/proto:matrix_go_proto",
        "@intrinsic_apis//intrinsic/math/proto:point_go_proto",
        "@intrinsic_apis//intrinsic/math/proto:pose_go_proto",
        "@intrinsic_apis//intrinsic/math/proto:quaternion_go_proto",
        "@intrinsic_apis//intrinsic/math/proto:tf_message_go_proto",
        "@intrinsic_apis//intrinsic/math/proto:twist_go_proto",
        "@intrinsic_apis//intrinsic/math/proto:vector2_go_proto",
        "@intrinsic_apis//intrinsic/math/proto:vector3_go_proto",
        "@intrinsic_apis//intrinsic/resources/proto:resource_handle_go_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:entity_go_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:object_properties_go_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:scene_object_go_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:simulation_spec_go_proto",
        "@intrinsic_apis//intrinsic/skills/proto:equipment_go_proto",
        "@intrinsic_apis//intrinsic/skills/proto:error_go_proto",
        "@intrinsic_apis//intrinsic/skills/proto:footprint_go_proto",
        "@intrinsic_apis//intrinsic/skills/proto:skill_parameter_metadata_go_proto",
        "@intrinsic_apis//intrinsic/skills/proto:skills_go_proto",
        "@intrinsic_apis//intrinsic/util/status:extended_status_go_proto",
        "@intrinsic_apis//intrinsic/world/proto:collision_action_go_proto",
        "@intrinsic_apis//intrinsic/world/proto:collision_settings_go_proto",
        "@intrinsic_apis//intrinsic/world/proto:geometry_component_go_proto",
        "@intrinsic_apis//intrinsic/world/proto:object_world_refs_go_proto",
    ],
)

py_proto_library(
    name = "log_item_py_pb2",
    deps = [":log_item_proto"],
)

proto_library(
    name = "logger_service",
    srcs = ["logger_service.proto"],
    deps = [
        ":log_item_proto",
        "@com_google_googleapis//google/api:annotations_proto",
        "@com_google_protobuf//:duration_proto",
        "@com_google_protobuf//:empty_proto",
        "@com_google_protobuf//:timestamp_proto",
        "@intrinsic_apis//intrinsic/kubernetes/generator/proto:auth_options_proto",
        "@intrinsic_apis//intrinsic/logging/proto:bag_metadata_proto",
        "@intrinsic_apis//intrinsic/logging/proto:downsampler_proto",
    ],
)

cc_proto_library(
    name = "logger_service_cc_proto",
    deps = [":logger_service"],
)

cc_grpc_library(
    name = "logger_service_cc_grpc",
    srcs = [":logger_service"],
    generate_mocks = True,
    grpc_only = True,
    deps = [":logger_service_cc_proto"],
)

go_proto_library(
    name = "logger_service_go_proto",
    compilers = [
        "@io_bazel_rules_go//proto:go_grpc_v2",
        "@io_bazel_rules_go//proto:go_proto",
        "//bazel:go_gen_grpc_gateway",
    ],
    importpath = "intrinsic/logging/proto/logger_service_go_proto",
    protos = [":logger_service"],
    deps = [
        ":log_item_go_proto",
        "@intrinsic_apis//intrinsic/kubernetes/generator/proto:auth_options_go_proto",
        "@intrinsic_apis//intrinsic/logging/proto:bag_metadata_go_proto",
        "@intrinsic_apis//intrinsic/logging/proto:downsampler_go_proto",
        "@org_golang_google_genproto_googleapis_api//annotations",
    ],
)

py_proto_library(
    name = "logger_service_py_pb2",
    deps = [":logger_service"],
)

py_grpc_library(
    name = "logger_service_py_pb2_grpc",
    srcs = [":logger_service"],
    deps = [":logger_service_py_pb2"],
)

go_library(
    name = "recording_data_categories_go",
    srcs = ["recording_data_categories.go"],
    embedsrcs = ["recording_data_categories.pbtxt"],
    importpath = "intrinsic/logging/proto",
    visibility = ["//visibility:public"],
    deps = [
        "@intrinsic_apis//intrinsic/logging/proto:recording_data_categories_go_proto",
        "@org_golang_google_protobuf//encoding/prototext",
    ],
)

proto_library(
    name = "visualization_context_proto",
    srcs = ["visualization_context.proto"],
    deps = [
        "//intrinsic/config/proto:cluster_proto",
        "//intrinsic/world/service:world_service_proto",
        "@intrinsic_apis//intrinsic/geometry/proto:renderable_proto",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:renderable_proto",
        "@intrinsic_apis//intrinsic/logging/proto:blob_proto",
        "@intrinsic_apis//intrinsic/world/proto:geometry_component_proto",
    ],
)

cc_proto_library(
    name = "visualization_context_cc_proto",
    deps = [":visualization_context_proto"],
)

go_proto_library(
    name = "visualization_context_go_proto",
    importpath = "intrinsic/logging/proto/visualization_context_go_proto",
    protos = [":visualization_context_proto"],
    deps = [
        "//intrinsic/config/proto:cluster_go_proto",
        "//intrinsic/world/service:world_service_go_proto",
        "@intrinsic_apis//intrinsic/geometry/proto:renderable_go_proto",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:renderable_go_proto",
        "@intrinsic_apis//intrinsic/logging/proto:blob_go_proto",
        "@intrinsic_apis//intrinsic/world/proto:geometry_component_go_proto",
    ],
)

py_proto_library(
    name = "visualization_context_py_pb2",
    deps = [":visualization_context_proto"],
)
