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
load("//bazel:go_macros.bzl", "go_proto_library")
load("//bazel:protos.bzl", "proto_library")

package(default_visibility = ["//visibility:public"])

alias(
    name = "object_world_service_proto",
    actual = "//intrinsic_sdk/intrinsic/world/proto:object_world_service_proto",
    tags = ["go/intrinsic-ioc-transition-tdd"],
    visibility = ["//intrinsic:public_api_users"],
)

alias(
    name = "object_world_service_cc_proto",
    actual = "//intrinsic_sdk/intrinsic/world/proto:object_world_service_cc_proto",
    tags = ["go/intrinsic-ioc-transition-tdd"],
    visibility = ["//intrinsic:public_api_users"],
)

alias(
    name = "object_world_service_cc_grpc_proto",
    actual = "//intrinsic_sdk/intrinsic/world/proto:object_world_service_cc_grpc_proto",
    tags = ["go/intrinsic-ioc-transition-tdd"],
    visibility = ["//intrinsic:public_api_users"],
)

alias(
    name = "object_world_service_go_proto",
    actual = "//intrinsic_sdk/intrinsic/world/proto:object_world_service_go_proto",
    tags = ["go/intrinsic-ioc-transition-tdd"],
    visibility = ["//intrinsic:internal_api_users"],
)

alias(
    name = "object_world_updates_proto",
    actual = "//intrinsic_sdk/intrinsic/world/proto:object_world_updates_proto",
    tags = ["go/intrinsic-ioc-transition-tdd"],
    visibility = ["//intrinsic:public_api_users"],
)

alias(
    name = "object_world_updates_cc_proto",
    actual = "//intrinsic_sdk/intrinsic/world/proto:object_world_updates_cc_proto",
    tags = ["go/intrinsic-ioc-transition-tdd"],
    visibility = ["//intrinsic:public_api_users"],
)

alias(
    name = "object_world_updates_go_proto",
    actual = "//intrinsic_sdk/intrinsic/world/proto:object_world_updates_go_proto",
    tags = ["go/intrinsic-ioc-transition-tdd"],
    visibility = ["//intrinsic:public_api_users"],
)

proto_library(
    name = "ppr_component_proto",
    srcs = ["ppr_component.proto"],
    visibility = ["//intrinsic:internal_api_users"],
)

go_proto_library(
    name = "ppr_component_go_proto",
    importpath = "intrinsic/world/proto/ppr_component_go_proto",
    importpath_aliases = ["intrinsic/world/public/proto/ppr_component_go_proto"],
    protos = [":ppr_component_proto"],
    visibility = ["//intrinsic:internal_api_users"],
)

cc_proto_library(
    name = "ppr_component_cc_proto",
    visibility = ["//intrinsic:internal_api_users"],
    deps = [":ppr_component_proto"],
)

proto_library(
    name = "acl_spec_proto",
    srcs = ["acl_spec.proto"],
)

cc_proto_library(
    name = "acl_spec_cc_proto",
    deps = [":acl_spec_proto"],
)

proto_library(
    name = "world_entity_proto",
    srcs = ["world_entity.proto"],
    deps = [
        ":attachment_component_proto",
        ":collections_component_proto",
        ":collections_member_component_proto",
        ":collision_component_proto",
        ":ppr_component_proto",
        ":projector_component_proto",
        ":robot_component_proto",
        ":user_data_component_proto",
        "@intrinsic_apis//intrinsic/world/proto:geometry_component_proto",
        "@intrinsic_apis//intrinsic/world/proto:kinematics_component_proto",
        "@intrinsic_apis//intrinsic/world/proto:physics_component_proto",
        "@intrinsic_apis//intrinsic/world/proto:sensor_component_proto",
        "@intrinsic_apis//intrinsic/world/proto:simulation_component_proto",
    ],
)

go_proto_library(
    name = "world_entity_go_proto",
    importpath = "intrinsic/world/proto/world_entity_go_proto",
    importpath_aliases = ["intrinsic/world/public/proto/world_entity_go_proto"],
    protos = [":world_entity_proto"],
    deps = [
        ":attachment_component_go_proto",
        ":collections_component_go_proto",
        ":collections_member_component_go_proto",
        ":collision_component_go_proto",
        ":generic_action_go_proto",
        ":ppr_component_go_proto",
        ":projector_component_go_proto",
        ":robot_component_go_proto",
        ":user_data_component_go_proto",
        "@intrinsic_apis//intrinsic/world/proto:geometry_component_go_proto",
        "@intrinsic_apis//intrinsic/world/proto:kinematics_component_go_proto",
        "@intrinsic_apis//intrinsic/world/proto:physics_component_go_proto",
        "@intrinsic_apis//intrinsic/world/proto:sensor_component_go_proto",
        "@intrinsic_apis//intrinsic/world/proto:simulation_component_go_proto",
    ],
)

py_proto_library(
    name = "world_entity_py_pb2",
    deps = [":world_entity_proto"],
)

cc_proto_library(
    name = "world_entity_cc_proto",
    deps = [":world_entity_proto"],
)

proto_library(
    name = "attachment_component_proto",
    srcs = ["attachment_component.proto"],
    deps = [
        "@com_google_protobuf//:timestamp_proto",
        "@intrinsic_apis//intrinsic/math/proto:pose_proto",
    ],
)

cc_proto_library(
    name = "attachment_component_cc_proto",
    deps = [":attachment_component_proto"],
)

py_proto_library(
    name = "attachment_component_py_pb2",
    deps = [":attachment_component_proto"],
)

go_proto_library(
    name = "attachment_component_go_proto",
    importpath = "intrinsic/world/proto/attachment_component_go_proto",
    importpath_aliases = ["intrinsic/world/public/proto/attachment_component_go_proto"],
    protos = ["attachment_component_proto"],
    deps = [
        "@intrinsic_apis//intrinsic/math/proto:pose_go_proto",
    ],
)

proto_library(
    name = "collections_component_proto",
    srcs = ["collections_component.proto"],
)

cc_proto_library(
    name = "collections_component_cc_proto",
    deps = [":collections_component_proto"],
)

py_proto_library(
    name = "collections_component_py_pb2",
    deps = [":collections_component_proto"],
)

go_proto_library(
    name = "collections_component_go_proto",
    importpath = "intrinsic/world/proto/collections_component_go_proto",
    importpath_aliases = ["intrinsic/world/public/proto/collections_component_go_proto"],
    protos = ["collections_component_proto"],
)

proto_library(
    name = "collections_member_component_proto",
    srcs = ["collections_member_component.proto"],
    deps = [":collections_component_proto"],
)

cc_proto_library(
    name = "collections_member_component_cc_proto",
    deps = [":collections_member_component_proto"],
)

py_proto_library(
    name = "collections_member_component_py_pb2",
    deps = [":collections_member_component_proto"],
)

go_proto_library(
    name = "collections_member_component_go_proto",
    importpath = "intrinsic/world/proto/collections_member_component_go_proto",
    importpath_aliases = ["intrinsic/world/public/proto/collections_member_component_go_proto"],
    protos = ["collections_member_component_proto"],
    deps = [":collections_component_go_proto"],
)

proto_library(
    name = "collision_component_proto",
    srcs = ["collision_component.proto"],
)

cc_proto_library(
    name = "collision_component_cc_proto",
    deps = [":collision_component_proto"],
)

py_proto_library(
    name = "collision_component_py_pb2",
    deps = [":collision_component_proto"],
)

go_proto_library(
    name = "collision_component_go_proto",
    importpath = "intrinsic/world/proto/collision_component_go_proto",
    importpath_aliases = ["intrinsic/world/public/proto/collision_component_go_proto"],
    protos = ["collision_component_proto"],
)

proto_library(
    name = "robot_component_proto",
    srcs = ["robot_component.proto"],
    deps = [
        ":generic_action_proto",
        "@intrinsic_apis//intrinsic/icon/proto:cart_space_proto",
        "@intrinsic_apis//intrinsic/world/proto:robot_payload_proto",
    ],
)

cc_proto_library(
    name = "robot_component_cc_proto",
    deps = [":robot_component_proto"],
)

go_proto_library(
    name = "robot_component_go_proto",
    importpath = "intrinsic/world/proto/robot_component_go_proto",
    importpath_aliases = ["intrinsic/world/public/proto/robot_component_go_proto"],
    protos = ["robot_component_proto"],
    deps = [
        ":generic_action_go_proto",
        "@intrinsic_apis//intrinsic/icon/proto:cart_space_go_proto",
        "@intrinsic_apis//intrinsic/world/proto:robot_payload_go_proto",
    ],
)

proto_library(
    name = "user_data_component_proto",
    srcs = ["user_data_component.proto"],
    deps = ["@com_google_protobuf//:any_proto"],
)

cc_proto_library(
    name = "user_data_component_cc_proto",
    deps = [":user_data_component_proto"],
)

go_proto_library(
    name = "user_data_component_go_proto",
    importpath = "intrinsic/world/proto/user_data_component_go_proto",
    importpath_aliases = ["intrinsic/world/public/proto/user_data_component_go_proto"],
    protos = ["user_data_component_proto"],
)

proto_library(
    name = "projector_component_proto",
    srcs = ["projector_component.proto"],
)

cc_proto_library(
    name = "projector_component_cc_proto",
    deps = [":projector_component_proto"],
)

go_proto_library(
    name = "projector_component_go_proto",
    importpath = "intrinsic/world/proto/projector_component_go_proto",
    importpath_aliases = ["intrinsic/world/public/proto/projector_component_go_proto"],
    protos = ["projector_component_proto"],
)

proto_library(
    name = "world_fragment_proto",
    srcs = ["world_fragment.proto"],
    deps = [
        ":world_entity_proto",
        "//intrinsic/world/collision:collision_context_proto",
    ],
)

proto_library(
    name = "tf_associations_proto",
    srcs = ["tf_associations.proto"],
    deps = [
        "@intrinsic_apis//intrinsic/world/proto:geometry_component_proto",
    ],
)

cc_proto_library(
    name = "tf_associations_cc_proto",
    deps = [":tf_associations_proto"],
)

go_proto_library(
    name = "tf_associations_go_proto",
    importpath = "intrinsic/world/proto/tf_associations_go_proto",
    importpath_aliases = ["intrinsic/world/public/proto/tf_associations_go_proto"],
    protos = ["tf_associations_proto"],
    deps = [
        "@intrinsic_apis//intrinsic/world/proto:geometry_component_go_proto",
    ],
)

go_proto_library(
    name = "world_fragment_go_proto",
    importpath = "intrinsic/world/proto/world_fragment_go_proto",
    importpath_aliases = ["intrinsic/world/public/proto/world_fragment_go_proto"],
    protos = [":world_fragment_proto"],
    deps = [
        ":world_entity_go_proto",
        "//intrinsic/world/collision:collision_context_go_proto",
    ],
)

cc_proto_library(
    name = "world_fragment_cc_proto",
    deps = [":world_fragment_proto"],
)

py_proto_library(
    name = "world_fragment_py_pb2",
    deps = ["world_fragment_proto"],
)

proto_library(
    name = "world_updates_proto",
    srcs = ["world_updates.proto"],
    deps = [
        "@intrinsic_apis//intrinsic/math/proto:pose_proto",
        "@intrinsic_apis//intrinsic/world/proto:collision_settings_proto",
        "@intrinsic_apis//intrinsic/world/proto:entity_search_proto",
    ],
)

cc_proto_library(
    name = "world_updates_cc_proto",
    deps = [":world_updates_proto"],
)

go_proto_library(
    name = "world_updates_go_proto",
    importpath = "intrinsic/world/proto/world_updates_go_proto",
    importpath_aliases = ["intrinsic/world/public/proto/world_updates_go_proto"],
    protos = ["world_updates_proto"],
    deps = [
        "@intrinsic_apis//intrinsic/math/proto:pose_go_proto",
        "@intrinsic_apis//intrinsic/messages:messages_go_proto",
        "@intrinsic_apis//intrinsic/world/proto:collision_settings_go_proto",
        "@intrinsic_apis//intrinsic/world/proto:entity_search_go_proto",
    ],
)

proto_library(
    name = "generic_action_proto",
    srcs = ["generic_action.proto"],
    deps = ["@intrinsic_apis//intrinsic/hardware/gpio/v1:signal_proto"],
)

cc_proto_library(
    name = "generic_action_cc_proto",
    deps = [":generic_action_proto"],
)

go_proto_library(
    name = "generic_action_go_proto",
    importpath = "intrinsic/world/proto/generic_action_go_proto",
    importpath_aliases = ["intrinsic/world/public/proto/generic_action_go_proto"],
    protos = ["generic_action_proto"],
    deps = [
        "@intrinsic_apis//intrinsic/hardware/gpio/v1:signal_go_proto",
    ],
)

py_proto_library(
    name = "tf_associations_py_pb2",
    deps = [":tf_associations_proto"],
)
