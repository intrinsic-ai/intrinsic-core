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
load("//bazel:cc_macros.bzl", "cc_library")
load("//bazel:go_macros.bzl", "go_proto_library")
load("//bazel:protos.bzl", "proto_library")

package(default_visibility = [
    "//visibility:public",
])

cc_library(
    name = "motion_planner_cache",
    srcs = ["motion_planner_cache.cc"],
    hdrs = ["motion_planner_cache.h"],
    deps = [
        ":cache_entry_features",
        ":cache_key_normalization",
        ":cache_utils",
        ":motion_planner_cache_cc_proto",
        "//intrinsic/util/proto:pb_hash",
        "//intrinsic/world:cc_proto",
        "//intrinsic/world/collision:collision_context_cc_proto",
        "//intrinsic/world/collision/util:make_collision_settings",
        "//intrinsic/world/objects:object_world",
        "//intrinsic/world/proto:attachment_component_cc_proto",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits_xd",
        "//intrinsic_motion_planning/intrinsic/motion_planning/motion_planner",
        "//intrinsic_motion_planning/intrinsic/motion_planning/motion_planner:robot_specification",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning/planners:validation",
        "//intrinsic_motion_planning/intrinsic/motion_planning/proto:motion_planner_service_proto_utils",
        "//intrinsic_motion_planning/intrinsic/motion_planning/proto/v1:motion_planner_service_cc_proto",
        "//intrinsic_motion_planning/intrinsic/motion_planning/proto/v1:motion_specification_cc_proto",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/geometry/api:affine_transform_of_geometry",
        "//intrinsic_sdk/intrinsic/geometry/api:geometry_fingerprint",
        "//intrinsic_sdk/intrinsic/logging:data_logger_client",
        "//intrinsic_sdk/intrinsic/logging/proto:log_item_cc_proto",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:proto_conversion",
        "//intrinsic_sdk/intrinsic/util:eigen",
        "//intrinsic_sdk/intrinsic/util/status:ret_check",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world:entity_id",
        "//intrinsic_sdk/intrinsic/world:geometry_types",
        "//intrinsic_sdk/intrinsic/world/component:geometry_component",
        "//intrinsic_sdk/intrinsic/world/component:kinematics_component",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_ids",
        "//third_party/simple_lru_cache",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/synchronization",
        "@intrinsic_apis//intrinsic/icon/proto:joint_space_cc_proto",
        "@intrinsic_apis//intrinsic/motion_planning/proto/v1:geometric_constraints_cc_proto",
        "@intrinsic_apis//intrinsic/motion_planning/proto/v1:robot_specification_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:collision_settings_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:object_world_refs_cc_proto",
    ],
)

cc_library(
    name = "cache_key_normalization",
    srcs = ["cache_key_normalization.cc"],
    hdrs = ["cache_key_normalization.h"],
    deps = [
        "//intrinsic/util/proto:pb_hash",
        "//intrinsic/world",
        "//intrinsic/world/collision/util:make_collision_settings",
        "//intrinsic/world/objects:object_world",
        "//intrinsic/world/objects:object_world_creation_utils",
        "//intrinsic/world/objects:object_world_proto_utils",
        "//intrinsic_motion_planning/intrinsic/motion_planning/proto/v1:motion_planner_service_cc_proto",
        "//intrinsic_motion_planning/intrinsic/motion_planning/proto/v1:motion_specification_cc_proto",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/geometry/api:exact_geometry",
        "//intrinsic_sdk/intrinsic/geometry/api:geometry",
        "//intrinsic_sdk/intrinsic/geometry/api:geometry_fingerprint",
        "//intrinsic_sdk/intrinsic/geometry/api:geometry_options",
        "//intrinsic_sdk/intrinsic/geometry/shapes",
        "//intrinsic_sdk/intrinsic/util:hash",
        "//intrinsic_sdk/intrinsic/util/object_store:memoize_fingerprint",
        "//intrinsic_sdk/intrinsic/util/object_store:object_ref",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world:entity_id",
        "//intrinsic_sdk/intrinsic/world:geometry_types",
        "//intrinsic_sdk/intrinsic/world/component:geometry_component",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_ids",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
        "@intrinsic_apis//intrinsic/motion_planning/proto/v1:geometric_constraints_cc_proto",
        "@intrinsic_apis//intrinsic/motion_planning/proto/v1:robot_specification_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:collision_settings_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:object_world_refs_cc_proto",
    ],
)

cc_library(
    name = "cache_utils",
    srcs = ["cache_utils.cc"],
    hdrs = ["cache_utils.h"],
    deps = [
        "//intrinsic/world:cc_proto",
        "//intrinsic/world/collision:collision_context_cc_proto",
        "//intrinsic/world/objects:object_world",
        "//intrinsic/world/objects:object_world_proto_utils",
        "//intrinsic/world/proto:attachment_component_cc_proto",
        "//intrinsic_motion_planning/intrinsic/motion_planning/proto/v1:motion_planner_service_cc_proto",
        "//intrinsic_motion_planning/intrinsic/motion_planning/proto/v1:motion_specification_cc_proto",
        "//intrinsic_sdk/intrinsic/logging/proto:log_item_cc_proto",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_ids",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@intrinsic_apis//intrinsic/icon/proto:joint_space_cc_proto",
        "@intrinsic_apis//intrinsic/motion_planning/proto/v1:geometric_constraints_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:collision_settings_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:object_world_refs_cc_proto",
    ],
)

cc_library(
    name = "cache_entry_features",
    srcs = ["cache_entry_features.cc"],
    hdrs = ["cache_entry_features.h"],
    deps = [
        ":cache_key_normalization",
        ":cache_utils",
        "//intrinsic/world",
        "//intrinsic/world/collision/util:make_collision_settings",
        "//intrinsic/world/objects:object_world",
        "//intrinsic/world/objects:object_world_proto_utils",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits_xd",
        "//intrinsic_motion_planning/intrinsic/motion_planning/proto:motion_planner_service_proto_utils",
        "//intrinsic_motion_planning/intrinsic/motion_planning/proto/v1:motion_planner_service_cc_proto",
        "//intrinsic_motion_planning/intrinsic/motion_planning/proto/v1:motion_specification_cc_proto",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/geometry/api:affine_transform_of_geometry",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:proto_conversion",
        "//intrinsic_sdk/intrinsic/util:eigen",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world:entity_id",
        "//intrinsic_sdk/intrinsic/world:geometry_types",
        "//intrinsic_sdk/intrinsic/world/component:geometry_component",
        "//intrinsic_sdk/intrinsic/world/component:kinematics_component",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_ids",
        "//intrinsic_sdk/intrinsic/world/objects:world_object",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/types:span",
        "@intrinsic_apis//intrinsic/world/proto:collision_settings_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:object_world_refs_cc_proto",
    ],
)

# keep
proto_library(
    name = "motion_planner_cache_proto",
    srcs = ["motion_planner_cache.proto"],
    strip_import_prefix = "/intrinsic_motion_planning",
    deps = [
        "//intrinsic_motion_planning/intrinsic/motion_planning/proto/v1:motion_specification_proto",
        "@intrinsic_apis//intrinsic/icon/proto:joint_space_proto",
        "@intrinsic_apis//intrinsic/kinematics/types:joint_limits_proto",
        "@intrinsic_apis//intrinsic/math/proto:pose_proto",
        "@intrinsic_apis//intrinsic/motion_planning/proto/v1:robot_specification_proto",
        "@intrinsic_apis//intrinsic/world/proto:collision_settings_proto",
    ],
)

go_proto_library(
    name = "motion_planner_cache_go_proto",
    importpath = "intrinsic_motion_planning/intrinsic/motion_planning/service/cache/motion_planner_cache_go_proto",
    proto = ":motion_planner_cache_proto",
    deps = [
        "//intrinsic_motion_planning/intrinsic/motion_planning/proto/v1:motion_specification_go_proto",
        "@intrinsic_apis//intrinsic/icon/proto:joint_space_go_proto",
        "@intrinsic_apis//intrinsic/kinematics/types:joint_limits_go_proto",
        "@intrinsic_apis//intrinsic/math/proto:pose_go_proto",
        "@intrinsic_apis//intrinsic/motion_planning/proto/v1:robot_specification_go_proto",
        "@intrinsic_apis//intrinsic/world/proto:collision_settings_go_proto",
    ],
)

cc_proto_library(
    name = "motion_planner_cache_cc_proto",
    deps = [
        ":motion_planner_cache_proto",
    ],
)
