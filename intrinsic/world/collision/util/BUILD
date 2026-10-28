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
    name = "collision_action_util",
    srcs = ["collision_action_util.cc"],
    hdrs = ["collision_action_util.h"],
    deps = [
        "//intrinsic/world/collision:collision_context_cc_proto",
        "//intrinsic_sdk/intrinsic/world:entity_id",
        "@intrinsic_apis//intrinsic/world/proto:collision_action_cc_proto",
    ],
)

cc_library(
    name = "collision_world_util",
    srcs = ["collision_world_util.cc"],
    hdrs = ["collision_world_util.h"],
    deps = [
        "//intrinsic/world",
        "//intrinsic/world:entity",
        "//intrinsic/world:kinematic_world_views",
        "//intrinsic/world/collision:collision_checker",
        "//intrinsic/world/collision:collision_context_cc_proto",
        "//intrinsic/world/util:walk_attachment_tree",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/geometry/api:affine_transform_of_geometry",
        "//intrinsic_sdk/intrinsic/geometry/api:axis_aligned_bounding_box_3d",
        "//intrinsic_sdk/intrinsic/geometry/api:compute_axis_aligned_bounding_box_3d",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world:entity_id",
        "//intrinsic_sdk/intrinsic/world:geometry_types",
        "//intrinsic_sdk/intrinsic/world/component:geometry_component",
        "//intrinsic_sdk/intrinsic/world/hashing",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@intrinsic_apis//intrinsic/world/proto:collision_action_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:collision_checker_config_cc_proto",
    ],
)

cc_library(
    name = "make_collision_settings",
    srcs = ["make_collision_settings.cc"],
    hdrs = ["make_collision_settings.h"],
    deps = [
        "//intrinsic/world",
        "//intrinsic/world/collision:collision_context_cc_proto",
        "//intrinsic/world/objects:object_world",
        "//intrinsic/world/objects:object_world_creation_utils",
        "//intrinsic_sdk/intrinsic/world:entity_id",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_ids",
        "@abseil-cpp//absl/log",
        "@intrinsic_apis//intrinsic/world/proto:collision_action_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:collision_settings_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:object_world_refs_cc_proto",
    ],
)

cc_library(
    name = "make_rule_set",
    srcs = ["make_rule_set.cc"],
    hdrs = ["make_rule_set.h"],
    deps = [
        "//intrinsic/world",
        "//intrinsic/world/collision:collision_context_cc_proto",
        "//intrinsic/world/objects:object_world",
        "//intrinsic/world/objects:object_world_creation_utils",
        "//intrinsic/world/objects:object_world_proto_utils",
        "//intrinsic_sdk/intrinsic/util:aggregate_type",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world:entity_id",
        "//intrinsic_sdk/intrinsic/world/hashing",
        "//intrinsic_sdk/intrinsic/world/objects:object_entity_filter",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_ids",
        "@abseil-cpp//absl/functional:any_invocable",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@com_google_protobuf//:protobuf_lite",
        "@gloop//gloop/util/gtl:flat_set",
        "@intrinsic_apis//intrinsic/world/proto:collision_action_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:collision_settings_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:object_world_refs_cc_proto",
    ],
)

cc_library(
    name = "rule_set_util",
    srcs = ["rule_set_util.cc"],
    hdrs = ["rule_set_util.h"],
    deps = [
        "@intrinsic_apis//intrinsic/world/proto:collision_action_cc_proto",
    ],
)
