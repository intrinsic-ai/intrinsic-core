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

# Object world utilities for the world service.

load("//bazel:cc_macros.bzl", "cc_library")

package(default_visibility = ["//intrinsic/world/service:__subpackages__"])

cc_library(
    name = "object_world_converter",
    srcs = ["object_world_converter.cc"],
    hdrs = ["object_world_converter.h"],
    visibility = [
        "//intrinsic/world/conversion:__subpackages__",
        "//intrinsic/world/service:__subpackages__",
        "//intrinsic_motion_planning/intrinsic/motion_planning/service:__pkg__",
    ],
    deps = [
        "//intrinsic/world",
        "//intrinsic/world:entity",
        "//intrinsic/world/component:attachment_component",
        "//intrinsic/world/component:ppr_component",
        "//intrinsic/world/component:simulation_component",
        "//intrinsic/world/objects:object_world",
        "//intrinsic/world/objects:object_world_creation_utils",
        "//intrinsic/world/proto:ppr_component_cc_proto",
        "//intrinsic/world/service:world_mutex",
        "//intrinsic_control/intrinsic/icon/proto:cart_space_conversion",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits_xd",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/geometry/storage:geometry_serializer",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:proto_conversion",
        "//intrinsic_sdk/intrinsic/util:eigen",
        "//intrinsic_sdk/intrinsic/util:proto_time",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world:entity_id",
        "//intrinsic_sdk/intrinsic/world/component:geometry_component",
        "//intrinsic_sdk/intrinsic/world/component:kinematics_component",
        "//intrinsic_sdk/intrinsic/world/component:physics_component",
        "//intrinsic_sdk/intrinsic/world/component:sensor_component",
        "//intrinsic_sdk/intrinsic/world/hashing",
        "//intrinsic_sdk/intrinsic/world/objects:object_entity_filter",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_ids",
        "//intrinsic_sdk/intrinsic/world/proto:object_world_service_cc_proto",
        "//intrinsic_sdk/intrinsic/world/proto:object_world_updates_cc_proto",
        "//intrinsic_sdk/intrinsic/world/robot_payload",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/kinematics/types:joint_limits_cc_proto",
        "@intrinsic_apis//intrinsic/math/proto:pose_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:object_world_refs_cc_proto",
    ],
)

cc_library(
    name = "object_world_updates_utils",
    srcs = ["object_world_updates_utils.cc"],
    hdrs = ["object_world_updates_utils.h"],
    visibility = [
        "//intrinsic/simulation/service:__subpackages__",
        "//intrinsic/simulation/world:__pkg__",
        "//intrinsic/world/service:__subpackages__",
        "//intrinsic/world/test:__subpackages__",
        "//intrinsic/world/tools:__subpackages__",
        "//intrinsic/world/util:__subpackages__",
    ],
    deps = [
        ":object_world_converter",
        "//intrinsic/stats:metrics_utils",
        "//intrinsic/world",
        "//intrinsic/world:entity",
        "//intrinsic/world/collision:collision_context_cc_proto",
        "//intrinsic/world/collision/util:collision_action_util",
        "//intrinsic/world/collision/util:make_rule_set",
        "//intrinsic/world/objects:object_world",
        "//intrinsic/world/objects:object_world_proto_utils",
        "//intrinsic/world/proto:world_fragment_cc_proto",
        "//intrinsic_control/intrinsic/icon/proto:cart_space_conversion",
        "//intrinsic_kinematics/intrinsic/kinematics/types:cartesian_limits",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/geometry/proto:geometry_service_cc_proto",
        "//intrinsic_sdk/intrinsic/geometry/storage:geometry_deserializer",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:proto_conversion",
        "//intrinsic_sdk/intrinsic/util:eigen",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world:entity_id",
        "//intrinsic_sdk/intrinsic/world:geometry_types",
        "//intrinsic_sdk/intrinsic/world/component:geometry_component",
        "//intrinsic_sdk/intrinsic/world/objects:object_entity_filter",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_ids",
        "//intrinsic_sdk/intrinsic/world/proto:object_world_updates_cc_proto",
        "//intrinsic_sdk/intrinsic/world/robot_payload",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/container:btree",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@com_google_protobuf//:protobuf",
        "@com_google_protobuf//:protobuf_lite",
        "@intrinsic_apis//intrinsic/scene/proto/v1:scene_object_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:collision_action_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:geometry_component_cc_proto",
        "@io_opentelemetry_cpp//api",
    ],
)

cc_library(
    name = "world_comparator",
    srcs = ["world_comparator.cc"],
    hdrs = ["world_comparator.h"],
    visibility = [
        "//intrinsic/world/conversion:__pkg__",
        "//intrinsic/world/service:__subpackages__",
        "//intrinsic_motion_planning/intrinsic/motion_planning/service:__pkg__",
    ],
    deps = [
        "//intrinsic/world/objects:object_world_creation_utils",
        "//intrinsic/world/proto:ppr_component_cc_proto",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:proto_conversion",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world:entity_id",
        "//intrinsic_sdk/intrinsic/world/hashing",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_ids",
        "//intrinsic_sdk/intrinsic/world/proto:object_world_service_cc_proto",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/functional:any_invocable",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/strings:string_view",
        "@com_google_protobuf//:protobuf_lite",
        "@intrinsic_apis//intrinsic/math/proto:matrix_cc_proto",
        "@intrinsic_apis//intrinsic/math/proto:pose_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:geometry_component_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:kinematics_component_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:object_world_refs_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:physics_component_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:sensor_component_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:simulation_component_cc_proto",
    ],
)
