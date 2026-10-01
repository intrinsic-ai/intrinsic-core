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

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "entity_search_util",
    srcs = ["entity_search_util.cc"],
    hdrs = ["entity_search_util.h"],
    deps = [
        "//intrinsic/util/status:status_builder",
        "//intrinsic/util/status:status_macros",
        "//intrinsic/world",
        "//intrinsic/world:entity",
        "//intrinsic_sdk/intrinsic/world:entity_id",
        "//intrinsic_sdk/intrinsic/world:labels",
        "//intrinsic_sdk/intrinsic/world/hashing",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@intrinsic_apis//intrinsic/world/proto:entity_search_cc_proto",
    ],
)

cc_library(
    name = "walk_attachment_tree",
    srcs = ["walk_attachment_tree.cc"],
    hdrs = ["walk_attachment_tree.h"],
    deps = [
        "//intrinsic/util:aggregate_type",
        "//intrinsic/util/status:status_macros",
        "//intrinsic/world",
        "//intrinsic/world:entity",
        "//intrinsic_sdk/intrinsic/world:entity_id",
        "//intrinsic_sdk/intrinsic/world/component:kinematics_component",
        "//intrinsic_sdk/intrinsic/world/hashing",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@intrinsic_apis//intrinsic/world/proto:kinematics_component_cc_proto",
    ],
)

cc_library(
    name = "kinematic_world_util",
    srcs = ["kinematic_world_util.cc"],
    hdrs = ["kinematic_world_util.h"],
    deps = [
        "//intrinsic/eigenmath:random_number_utils",
        "//intrinsic/util:macros",
        "//intrinsic/world:kinematic_world_views",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "remove_redundant_updates",
    srcs = ["remove_redundant_updates.cc"],
    hdrs = ["remove_redundant_updates.h"],
    deps = [
        "//intrinsic/world/objects:object_world_proto_utils",
        "//intrinsic/world/proto:object_world_updates_cc_proto",
        "@abseil-cpp//absl/status:statusor",
        "@intrinsic_apis//intrinsic/kinematics/types:joint_limits_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:object_world_refs_cc_proto",
    ],
)

cc_library(
    name = "compose_world_util",
    srcs = ["compose_world_util.cc"],
    hdrs = ["compose_world_util.h"],
    deps = [
        "//intrinsic/config/proto:resource_set_cc_proto",
        "//intrinsic/resources/proto:geometric_resource_data_cc_proto",
        "//intrinsic/stats:scoped_span",
        "//intrinsic/util/proto:pb_hash",
        "//intrinsic/util/status:status_macros",
        "//intrinsic/world",
        "//intrinsic/world/objects:object_world",
        "//intrinsic/world/objects:object_world_proto_utils",
        "//intrinsic/world/objects:simple_transform_node_visitor",
        "//intrinsic/world/proto:object_world_service_cc_proto",
        "//intrinsic/world/proto:object_world_updates_cc_proto",
        "//intrinsic/world/service/objects:object_world_updates_utils",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/geometry/api:geometry_options",
        "//intrinsic_sdk/intrinsic/geometry/storage:geometry_library",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/scene/util:scene_object_updates",
        "//intrinsic_sdk/intrinsic/util/thread:thread_pool",
        "//intrinsic_sdk/intrinsic/world:entity_id",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_ids",
        "@abseil-cpp//absl/cleanup",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
        "@intrinsic_apis//intrinsic/geometry/proto:geometry_storage_refs_cc_proto",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:geometry_cc_proto",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:geometry_storage_refs_cc_proto",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:transformed_geometry_cc_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:entity_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:object_world_refs_cc_proto",
    ],
)

cc_library(
    name = "tf_frame_util",
    srcs = ["tf_frame_util.cc"],
    hdrs = ["tf_frame_util.h"],
    deps = [
        "//intrinsic/math:tf2_convert_intrinsic",
        "//intrinsic/util:proto_time",
        "//intrinsic/util/status:ret_check",
        "//intrinsic/util/status:status_macros",
        "//intrinsic/world",
        "//intrinsic/world/objects:object_world",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/world:entity_id",
        "//intrinsic_sdk/intrinsic/world/objects:object_entity_filter",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_ids",
        "//intrinsic_sdk/intrinsic/world/objects:world_object",
        "@abseil-cpp//absl/status:statusor",
        "@intrinsic_apis//intrinsic/math/proto:transform_stamped_cc_proto",
    ],
)

cc_library(
    name = "ros_joint_state_util",
    srcs = ["ros_joint_state_util.cc"],
    hdrs = ["ros_joint_state_util.h"],
    deps = [
        "//intrinsic/util/status:status_macros",
        "//intrinsic/world/objects:object_world",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//third_party/ros2/ros_interfaces/jazzy/sensor_msgs/msg:joint_state_cc_proto",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "download_world_util",
    srcs = ["download_world_util.cc"],
    hdrs = ["download_world_util.h"],
    deps = [
        "//intrinsic/util/grpc",
        "//intrinsic/util/status:status_conversion_grpc",
        "//intrinsic/util/status:status_macros",
        "//intrinsic/world",
        "//intrinsic/world/conversion:world_from_object_proto",
        "//intrinsic/world/proto:object_world_service_cc_grpc_proto",
        "//intrinsic/world/proto:object_world_service_cc_proto",
        "//intrinsic_sdk/intrinsic/geometry/storage:geometry_deserializer",
        "//intrinsic_sdk/intrinsic/geometry/storage:geometry_library",
        "//intrinsic_sdk/intrinsic/world/component:geometry_component",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
        "@com_github_grpc_grpc//:grpc++",
        "@intrinsic_apis//intrinsic/world/proto:collision_settings_cc_proto",
    ],
)
