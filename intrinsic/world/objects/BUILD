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

# Client libraries for the ObjectWorldService.

load("//bazel:cc_macros.bzl", "cc_library")

package(default_visibility = [
    "//intrinsic/apps:__subpackages__",
    "//intrinsic/executive:__subpackages__",
    "//intrinsic/scene:__subpackages__",
    "//intrinsic/simulation:__subpackages__",
    "//intrinsic/skills:__subpackages__",
    "//intrinsic/world:__subpackages__",
    "//intrinsic_control/intrinsic/icon:__subpackages__",
    "//intrinsic_hardware/intrinsic/hardware:__subpackages__",
    "//intrinsic_kinematics/intrinsic/kinematics:__subpackages__",
    "//intrinsic_motion_planning:__subpackages__",
])

cc_library(
    name = "user_data_conversion",
    srcs = ["user_data_conversion.cc"],
    hdrs = ["user_data_conversion.h"],
    deps = [
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@intrinsic_apis//intrinsic/scene/proto/v1:scene_object_updates_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:user_data_cc_proto",
    ],
)

cc_library(
    name = "object_world",
    srcs = [
        "frame_internal.cc",
        "kinematic_object_internal.cc",
        "object_world.cc",
        "object_world_creation.cc",
        "object_world_data.cc",
        "physical_object.cc",
        "root_object.cc",
        "transform_node_internal.cc",
        "world_object_internal.cc",
    ],
    hdrs = [
        "defaulting_world_object_visitor.h",
        "frame_internal.h",
        "kinematic_object_internal.h",
        "object_world.h",
        "object_world_data.h",
        "physical_object.h",
        "root_object.h",
        "simple_world_object_visitor.h",
        "transform_node_internal.h",
        "world_object_internal.h",
    ],
    deps = [
        ":object_world_creation_utils",
        ":user_data_conversion",
        "//intrinsic/resources/proto:geometric_resource_data_cc_proto",
        "//intrinsic/resources/proto:resource_instance_cc_proto",
        "//intrinsic/scene/config:scene_object_config",
        "//intrinsic/simulation/world:sim_plugins",
        "//intrinsic/stats:scoped_span",
        "//intrinsic/util:aggregate_type",
        "//intrinsic/util:macros",
        "//intrinsic/util/status:ret_check",
        "//intrinsic/util/status:return",
        "//intrinsic/util/status:status_builder",
        "//intrinsic/util/status:status_macros",
        "//intrinsic/world",
        "//intrinsic/world:entity",
        "//intrinsic/world:kinematic_world_views",
        "//intrinsic/world/collision:collision_context_cc_proto",
        "//intrinsic/world/component:attachment_component",
        "//intrinsic/world/component:collections_component",
        "//intrinsic/world/component:collections_member_component",
        "//intrinsic/world/component:collision_component",
        "//intrinsic/world/component:ppr_component",
        "//intrinsic/world/component:robot_component",
        "//intrinsic/world/component:simulation_component",
        "//intrinsic/world/component:user_data_component",
        "//intrinsic/world/proto:collections_component_cc_proto",
        "//intrinsic/world/proto:object_world_updates_cc_proto",
        "//intrinsic/world/proto:robot_component_cc_proto",
        "//intrinsic/world/proto:world_entity_cc_proto",
        "//intrinsic/world/proto:world_updates_cc_proto",
        "//intrinsic/world/util:walk_attachment_tree",
        "//intrinsic_kinematics/intrinsic/kinematics/types:cartesian_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits_xd",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/geometry/api:geometry",
        "//intrinsic_sdk/intrinsic/geometry/api:geometry_options",
        "//intrinsic_sdk/intrinsic/geometry/storage:geometry_deserializer",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:proto_conversion",
        "//intrinsic_sdk/intrinsic/scene:constants",
        "//intrinsic_sdk/intrinsic/scene/conversion:object_properties_conversion",
        "//intrinsic_sdk/intrinsic/scene/util:object_user_data",
        "//intrinsic_sdk/intrinsic/scene/validate:scene_object_validation",
        "//intrinsic_sdk/intrinsic/world:entity_id",
        "//intrinsic_sdk/intrinsic/world:geometry_types",
        "//intrinsic_sdk/intrinsic/world/component:geometry_component",
        "//intrinsic_sdk/intrinsic/world/component:kinematics_component",
        "//intrinsic_sdk/intrinsic/world/component:physics_component",
        "//intrinsic_sdk/intrinsic/world/component:sensor_component",
        "//intrinsic_sdk/intrinsic/world/hashing",
        "//intrinsic_sdk/intrinsic/world/objects:object_entity_filter",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_ids",
        "//intrinsic_sdk/intrinsic/world/robot_payload",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/crc:crc32c",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/random",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/kinematics/types:joint_limits_cc_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:collision_rules_cc_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:entity_cc_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:scene_object_cc_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:scene_object_config_cc_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:simulation_spec_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:geometry_component_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:kinematics_component_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:physics_component_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:simulation_component_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:user_data_cc_proto",
        "@tf2//:tf2_library",
    ],
)

cc_library(
    name = "simple_transform_node_visitor",
    hdrs = ["simple_transform_node_visitor.h"],
    deps = [
        ":object_world",
        "//intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
    ],
)

cc_library(
    name = "object_world_creation_utils",
    srcs = ["object_world_creation_utils.cc"],
    hdrs = ["object_world_creation_utils.h"],
    deps = [
        "//intrinsic/util/status:status_macros",
        "//intrinsic/world",
        "//intrinsic/world:entity",
        "//intrinsic/world/util:walk_attachment_tree",
        "//intrinsic_control/intrinsic/icon/release:source_location",
        "//intrinsic_sdk/intrinsic/world:entity_id",
        "//intrinsic_sdk/intrinsic/world/component:geometry_component",
        "//intrinsic_sdk/intrinsic/world/component:kinematics_component",
        "//intrinsic_sdk/intrinsic/world/component:physics_component",
        "//intrinsic_sdk/intrinsic/world/component:sensor_component",
        "//intrinsic_sdk/intrinsic/world/hashing",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_ids",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@com_googlesource_code_re2//:re2",
    ],
)

cc_library(
    name = "object_world_proto_utils",
    srcs = ["object_world_proto_utils.cc"],
    hdrs = ["object_world_proto_utils.h"],
    deps = [
        ":object_world",
        ":object_world_creation_utils",
        ":simple_transform_node_visitor",
        "//intrinsic/util/status:status_macros",
        "//intrinsic/world:entity",
        "//intrinsic/world/util:entity_search_util",
        "//intrinsic_sdk/intrinsic/world:entity_id",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_ids",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/math/proto:point_cc_proto",
        "@intrinsic_apis//intrinsic/math/proto:pose_cc_proto",
        "@intrinsic_apis//intrinsic/motion_planning/proto:motion_target_cc_proto",
        "@intrinsic_apis//intrinsic/skills/proto:motion_targets_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:entity_search_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:object_world_refs_cc_proto",
    ],
)

cc_library(
    name = "footprint_object_utils",
    srcs = ["footprint_object_utils.cc"],
    hdrs = ["footprint_object_utils.h"],
    deps = [
        ":object_world",
        ":object_world_creation_utils",
        "//intrinsic/util/status:status_macros",
        "//intrinsic/world:entity",
        "//intrinsic_sdk/intrinsic/world:entity_id",
        "//intrinsic_sdk/intrinsic/world/hashing",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_ids",
        "@abseil-cpp//absl/functional:any_invocable",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/skills/proto:footprint_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:entity_search_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:object_world_refs_cc_proto",
    ],
)

cc_library(
    name = "lazy_object_view",
    hdrs = ["lazy_object_view.h"],
    deps = [
        ":object_world",
        "//intrinsic/util/status:status_macros",
        "//intrinsic/world",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_library(
    name = "print_object_world",
    srcs = ["print_object_world.cc"],
    hdrs = ["print_object_world.h"],
    deps = [
        ":object_world",
        "//intrinsic/util:aggregate_type",
        "//intrinsic/util/status:status_macros",
        "//intrinsic/world",
        "//intrinsic/world:entity",
        "//intrinsic_sdk/intrinsic/world:entity_id",
        "//intrinsic_sdk/intrinsic/world/hashing",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_ids",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
)
