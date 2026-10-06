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
load("//bazel:cc_macros.bzl", "cc_library")
load("//bazel:go_macros.bzl", "go_proto_library")
load("//bazel:protos.bzl", "proto_library")

package(default_visibility = [
    "//visibility:public",
])

alias(
    name = "geometry_types",
    actual = "//intrinsic_sdk/intrinsic/world:geometry_types",
    tags = ["go/intrinsic-ioc-transition-tdd"],
)

cc_library(
    name = "grouping",
    hdrs = ["grouping.h"],
    deps = [
        "//intrinsic/util:string_type",
        "//intrinsic_sdk/intrinsic/world:entity_id",
        "//intrinsic_sdk/intrinsic/world:labels",
        "//intrinsic_sdk/intrinsic/world/hashing",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/status",
    ],
)

cc_library(
    name = "kinematic_world_views",
    hdrs = [
        "cartesian_kinematic_view.h",
        "dof_kinematic_view.h",
    ],
    deps = [
        ":configuration_validation",
        "//intrinsic/util:string_type",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits_xd",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/world:entity_id",
        "//intrinsic_sdk/intrinsic/world:labels",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_library(
    name = "world",
    srcs = ["world.cc"],
    hdrs = ["world.h"],
    copts = ["-fexceptions"],
    features = ["-use_header_modules"],
    deps = [
        ":cc_proto",
        ":entity",
        ":grouping",
        ":kinematic_world_views",
        ":kinematics_builder",
        "//intrinsic/geometry/api:fuse_geometries",
        "//intrinsic/math:tf2_convert_intrinsic",
        "//intrinsic/util:aggregate_type",
        "//intrinsic/util:macros",
        "//intrinsic/util:proto_time",
        "//intrinsic/util/status:status_builder",
        "//intrinsic/util/status:status_macros",
        "//intrinsic/world/aspects:chained_robot_cartesian_kinematic_view",
        "//intrinsic/world/aspects:entity_dof_kinematic_view",
        "//intrinsic/world/aspects:entity_grouping",
        "//intrinsic/world/aspects:entity_robot_cartesian_kinematic_view",
        "//intrinsic/world/aspects:entity_world_interface",
        "//intrinsic/world/collision:coal_collision_checker",
        "//intrinsic/world/collision:collision_check_cache",
        "//intrinsic/world/collision:collision_checker",
        "//intrinsic/world/collision:collision_checker_utils",
        "//intrinsic/world/collision:collision_checker_world",
        "//intrinsic/world/collision:collision_context_cc_proto",
        "//intrinsic/world/collision:rule_matching",
        "//intrinsic/world/collision/util:rule_set_util",
        "//intrinsic/world/component:attachment_component",
        "//intrinsic/world/component:collections_component",
        "//intrinsic/world/component:collections_member_component",
        "//intrinsic/world/component:collision_component",
        "//intrinsic/world/component:ppr_component",
        "//intrinsic/world/component:robot_component",
        "//intrinsic/world/internal:inner_world",
        "//intrinsic/world/proto:collections_component_cc_proto",
        "//intrinsic/world/proto:tf_associations_cc_proto",
        "//intrinsic/world/proto:world_entity_cc_proto",
        "//intrinsic/world/proto:world_fragment_cc_proto",
        "//intrinsic_kinematics/intrinsic/kinematics:skeleton",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/geometry/api:affine_transform_of",
        "//intrinsic_sdk/intrinsic/geometry/api:affine_transform_of_geometry",
        "//intrinsic_sdk/intrinsic/geometry/api:exact_geometry",
        "//intrinsic_sdk/intrinsic/geometry/api:geometry",
        "//intrinsic_sdk/intrinsic/geometry/api:geometry_options",
        "//intrinsic_sdk/intrinsic/geometry/shapes",
        "//intrinsic_sdk/intrinsic/geometry/storage:dummy_storage",
        "//intrinsic_sdk/intrinsic/geometry/storage:geometry_deserializer",
        "//intrinsic_sdk/intrinsic/geometry/storage:geometry_serializer",
        "//intrinsic_sdk/intrinsic/geometry/storage:gzf_storage",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:proto_conversion",
        "//intrinsic_sdk/intrinsic/util/object_store:object_ref",
        "//intrinsic_sdk/intrinsic/world:entity_id",
        "//intrinsic_sdk/intrinsic/world:geometry_types",
        "//intrinsic_sdk/intrinsic/world:labels",
        "//intrinsic_sdk/intrinsic/world/component:geometry_component",
        "//intrinsic_sdk/intrinsic/world/component:kinematics_component",
        "//intrinsic_sdk/intrinsic/world/component:physics_component",
        "//intrinsic_sdk/intrinsic/world/component:sensor_component",
        "//intrinsic_sdk/intrinsic/world/gzfile",
        "//intrinsic_sdk/intrinsic/world/hashing",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_ids",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:btree",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/log:die_if_null",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@com_google_googletest//:gtest_prod",
        "@com_google_protobuf//:protobuf",
        "@com_google_protobuf//src/google/protobuf/io",
        "@geometry_msgs//:cc",
        "@intrinsic_apis//intrinsic/math/proto:header_cc_proto",
        "@intrinsic_apis//intrinsic/math/proto:quaternion_cc_proto",
        "@intrinsic_apis//intrinsic/math/proto:tf_message_cc_proto",
        "@intrinsic_apis//intrinsic/math/proto:transform_cc_proto",
        "@intrinsic_apis//intrinsic/math/proto:transform_stamped_cc_proto",
        "@intrinsic_apis//intrinsic/math/proto:vector3_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:collision_checker_config_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:kinematics_component_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:physics_component_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:sensor_component_cc_proto",
        "@rclcpp//:rclcpp_library",
        "@tf2//:tf2_library",
        "@tf2_ros//:tf2_ros_library",
    ],
)

cc_library(
    name = "world_acl_spec",
    srcs = ["world_acl_spec.cc"],
    hdrs = ["world_acl_spec.h"],
    deps = [
        ":world_entity_acl_spec",
        "//intrinsic/util/status:status_macros",
        "//intrinsic/world/proto:acl_spec_cc_proto",
        "//intrinsic_sdk/intrinsic/world:entity_id",
        "//intrinsic_sdk/intrinsic/world/hashing",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "world_entity_acl_spec",
    srcs = ["world_entity_acl_spec.cc"],
    hdrs = ["world_entity_acl_spec.h"],
    deps = [
        "//intrinsic/world/proto:acl_spec_cc_proto",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
)

proto_library(
    name = "proto",
    srcs = ["world.proto"],
    deps = [
        "//intrinsic/world/collision:collision_context_proto",
        "//intrinsic/world/proto:world_entity_proto",
    ],
)

py_proto_library(
    name = "world_py_pb2",
    deps = [
        ":proto",
    ],
)

go_proto_library(
    name = "go_proto",
    importpath = "intrinsic/world/go_proto",
    protos = [":proto"],
    deps = [
        "//intrinsic/world/collision:collision_context_go_proto",
        "//intrinsic/world/proto:world_entity_go_proto",
    ],
)

alias(
    name = "world_go_proto",
    actual = ":go_proto",
)

cc_proto_library(
    name = "cc_proto",
    deps = [
        ":proto",
    ],
)

cc_library(
    name = "update_world",
    srcs = ["update_world.cc"],
    hdrs = ["update_world.h"],
    deps = [
        ":entity",
        ":grouping",
        ":kinematic_world_views",
        ":world",
        "//intrinsic/geometry/compatibility:io",
        "//intrinsic/util:aggregate_type",
        "//intrinsic/util:eigen",
        "//intrinsic/util/status:status_builder",
        "//intrinsic/util/status:status_macros",
        "//intrinsic/world/collision:coal_collision_checker",
        "//intrinsic/world/collision/util:make_rule_set",
        "//intrinsic/world/component:attachment_component",
        "//intrinsic/world/component:collections_component",
        "//intrinsic/world/component:robot_component",
        "//intrinsic/world/objects:object_world",
        "//intrinsic/world/proto:world_updates_cc_proto",
        "//intrinsic/world/service:world_service_cc_proto",
        "//intrinsic/world/util:entity_search_util",
        "//intrinsic/world/util:walk_attachment_tree",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning:kinematics_system_proxy_util",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/geometry/api:affine_transform_of_geometry",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:proto_conversion",
        "//intrinsic_sdk/intrinsic/world:entity_id",
        "//intrinsic_sdk/intrinsic/world:geometry_types",
        "//intrinsic_sdk/intrinsic/world:labels",
        "//intrinsic_sdk/intrinsic/world/component:geometry_component",
        "//intrinsic_sdk/intrinsic/world/component:kinematics_component",
        "//intrinsic_sdk/intrinsic/world/hashing",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@intrinsic_apis//intrinsic/world/proto:entity_search_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:kinematics_component_cc_proto",
    ],
)

cc_library(
    name = "configuration_validation",
    hdrs = ["configuration_validation.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/eigenmath",
    ],
)

cc_library(
    name = "entity",
    srcs = ["entity.cc"],
    hdrs = ["entity.h"],
    deps = [
        ":world_entity_acl_spec",
        "//intrinsic/util/status:status_builder",
        "//intrinsic/util/status:status_macros",
        "//intrinsic/world/component:attachment_component",
        "//intrinsic/world/component:collections_component",
        "//intrinsic/world/component:collections_member_component",
        "//intrinsic/world/component:collision_component",
        "//intrinsic/world/component:ppr_component",
        "//intrinsic/world/component:projector_component",
        "//intrinsic/world/component:robot_component",
        "//intrinsic/world/component:simulation_component",
        "//intrinsic/world/component:user_data_component",
        "//intrinsic/world/proto:attachment_component_cc_proto",
        "//intrinsic/world/proto:collections_component_cc_proto",
        "//intrinsic/world/proto:collections_member_component_cc_proto",
        "//intrinsic/world/proto:collision_component_cc_proto",
        "//intrinsic/world/proto:ppr_component_cc_proto",
        "//intrinsic/world/proto:robot_component_cc_proto",
        "//intrinsic/world/proto:user_data_component_cc_proto",
        "//intrinsic/world/proto:world_entity_cc_proto",
        "//intrinsic_sdk/intrinsic/geometry/storage:dummy_storage",
        "//intrinsic_sdk/intrinsic/geometry/storage:geometry_deserializer",
        "//intrinsic_sdk/intrinsic/geometry/storage:geometry_serializer",
        "//intrinsic_sdk/intrinsic/world:entity_id",
        "//intrinsic_sdk/intrinsic/world:labels",
        "//intrinsic_sdk/intrinsic/world/component:geometry_component",
        "//intrinsic_sdk/intrinsic/world/component:kinematics_component",
        "//intrinsic_sdk/intrinsic/world/component:physics_component",
        "//intrinsic_sdk/intrinsic/world/component:sensor_component",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@com_google_protobuf//:differencer",
        "@intrinsic_apis//intrinsic/world/proto:geometry_component_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:kinematics_component_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:physics_component_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:sensor_component_cc_proto",
    ],
)

cc_library(
    name = "print_world_lib",
    srcs = ["print_world.cc"],
    hdrs = ["print_world.h"],
    deps = [
        ":entity",
        ":grouping",
        ":kinematic_world_views",
        ":world",
        "//intrinsic/util:eigen",
        "//intrinsic/util:macros",
        "//intrinsic/world/collision:coal_collision_checker",
        "//intrinsic/world/component:attachment_component",
        "//intrinsic/world/component:collections_component",
        "//intrinsic/world/component:ppr_component",
        "//intrinsic/world/component:robot_component",
        "//intrinsic/world/component:user_data_component",
        "//intrinsic/world/proto:robot_component_cc_proto",
        "//intrinsic_sdk/intrinsic/geometry/api:geometry",
        "//intrinsic_sdk/intrinsic/world:entity_id",
        "//intrinsic_sdk/intrinsic/world:labels",
        "//intrinsic_sdk/intrinsic/world/component:geometry_component",
        "//intrinsic_sdk/intrinsic/world/hashing",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_library(
    name = "kinematics_builder",
    srcs = ["kinematics_builder.cc"],
    hdrs = ["kinematics_builder.h"],
    deps = [
        ":entity",
        "//intrinsic/util:aggregate_type",
        "//intrinsic/util/status:status_builder",
        "//intrinsic/util/status:status_macros",
        "//intrinsic/world/aspects:entity_world_interface",
        "//intrinsic/world/component:attachment_component",
        "//intrinsic/world/component:collections_component",
        "//intrinsic/world/component:robot_component",
        "//intrinsic_kinematics/intrinsic/kinematics",
        "//intrinsic_kinematics/intrinsic/kinematics:skeleton",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/world:entity_id",
        "//intrinsic_sdk/intrinsic/world/component:kinematics_component",
        "//intrinsic_sdk/intrinsic/world/component:physics_component",
        "//intrinsic_sdk/intrinsic/world/hashing",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/types:span",
        "@intrinsic_apis//intrinsic/world/proto:kinematics_component_cc_proto",
        "@or_tools//ortools/base:strong_int",
    ],
)

cc_library(
    name = "build_tree_skeleton_from_world",
    srcs = ["build_tree_skeleton_from_world.cc"],
    hdrs = ["build_tree_skeleton_from_world.h"],
    deps = [
        ":entity",
        ":world",
        "//intrinsic/util/status:status_builder",
        "//intrinsic/util/status:status_macros",
        "//intrinsic/world/component:attachment_component",
        "//intrinsic/world/component:robot_component",
        "//intrinsic/world/util:walk_attachment_tree",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_kinematics/intrinsic/kinematics",
        "//intrinsic_kinematics/intrinsic/kinematics:skeleton",
        "//intrinsic_sdk/intrinsic/world:entity_id",
        "//intrinsic_sdk/intrinsic/world/component:kinematics_component",
        "//intrinsic_sdk/intrinsic/world/component:physics_component",
        "//intrinsic_sdk/intrinsic/world/hashing",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@intrinsic_apis//intrinsic/world/proto:kinematics_component_cc_proto",
    ],
)
