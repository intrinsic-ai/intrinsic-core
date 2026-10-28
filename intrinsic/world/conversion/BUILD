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

package(default_visibility = [
    "//visibility:public",
])

cc_library(
    name = "world_from_object_proto",
    srcs = ["world_from_object_proto.cc"],
    hdrs = ["world_from_object_proto.h"],
    deps = [
        "//intrinsic/world",
        "//intrinsic/world/collision:collision_context_cc_proto",
        "//intrinsic/world/collision/util:make_rule_set",
        "//intrinsic/world/component:attachment_component",
        "//intrinsic/world/component:collections_component",
        "//intrinsic/world/component:collections_member_component",
        "//intrinsic/world/component:collision_component",
        "//intrinsic/world/component:ppr_component",
        "//intrinsic/world/component:robot_component",
        "//intrinsic/world/component:simulation_component",
        "//intrinsic/world/component:user_data_component",
        "//intrinsic/world/objects:object_world",
        "//intrinsic/world/objects:object_world_creation_utils",
        "//intrinsic/world/proto:robot_component_cc_proto",
        "//intrinsic/world/proto:user_data_component_cc_proto",
        "//intrinsic_sdk/intrinsic/math:proto_conversion",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world:entity_id",
        "//intrinsic_sdk/intrinsic/world/component:geometry_component",
        "//intrinsic_sdk/intrinsic/world/component:kinematics_component",
        "//intrinsic_sdk/intrinsic/world/component:physics_component",
        "//intrinsic_sdk/intrinsic/world/component:sensor_component",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_ids",
        "//intrinsic_sdk/intrinsic/world/proto:object_world_service_cc_proto",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@intrinsic_apis//intrinsic/world/proto:collision_settings_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:object_world_refs_cc_proto",
    ],
)

cc_library(
    name = "scene_object_to_world_updates",
    srcs = ["scene_object_to_world_updates.cc"],
    hdrs = ["scene_object_to_world_updates.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/scene/util:geometry_update_util",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world:geometry_types",
        "//intrinsic_sdk/intrinsic/world/proto:object_world_service_cc_proto",
        "//intrinsic_sdk/intrinsic/world/proto:object_world_updates_cc_proto",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@intrinsic_apis//intrinsic/scene/proto/v1:scene_object_updates_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:collision_action_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:collision_settings_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:object_world_refs_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:user_data_cc_proto",
    ],
)
