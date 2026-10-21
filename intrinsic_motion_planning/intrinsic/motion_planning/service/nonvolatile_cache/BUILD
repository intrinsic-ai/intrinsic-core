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
    name = "motion_planner_nonvolatile_cache",
    srcs = ["motion_planner_nonvolatile_cache.cc"],
    hdrs = ["motion_planner_nonvolatile_cache.h"],
    deps = [
        ":motion_planner_nonvolatile_cache_cc_proto",
        "//intrinsic/storage/content_addressable_storage/cpp:client_helpers_cc",
        "//intrinsic/util/proto:pb_hash",
        "//intrinsic/world:cc_proto",
        "//intrinsic/world/objects:object_world",
        "//intrinsic/world/objects:object_world_proto_utils",
        "//intrinsic_control/intrinsic/icon/proto:joint_trajectory_conversion",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_trajectories",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning:path_segment",
        "//intrinsic_motion_planning/intrinsic/motion_planning/proto/v1:motion_planner_service_cc_proto",
        "//intrinsic_motion_planning/intrinsic/motion_planning/proto/v1:motion_specification_cc_proto",
        "//intrinsic_motion_planning/intrinsic/motion_planning/service/cache:cache_utils",
        "//intrinsic_sdk/intrinsic/connect/cc/grpc:channel",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:proto_conversion",
        "//intrinsic_sdk/intrinsic/util:eigen",
        "//intrinsic_sdk/intrinsic/util:lru_cache",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_ids",
        "//third_party/simple_lru_cache",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/random",
        "@abseil-cpp//absl/random:distributions",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/motion_planning/proto/v1:motion_blending_parameter_cc_proto",
        "@intrinsic_apis//intrinsic/motion_planning/proto/v1:motion_planner_config_cc_proto",
        "@intrinsic_apis//intrinsic/motion_planning/proto/v1:motion_planning_limits_cc_proto",
        "@intrinsic_apis//intrinsic/motion_planning/proto/v1:robot_specification_cc_proto",
        "@intrinsic_apis//intrinsic/storage/content_addressable_storage/proto:cas_service_cc_grpc_proto",
        "@intrinsic_apis//intrinsic/world/proto:collision_settings_cc_proto",
    ],
)

cc_library(
    name = "motion_planner_nonvolatile_cache_utils",
    srcs = ["motion_planner_nonvolatile_cache_utils.cc"],
    hdrs = ["motion_planner_nonvolatile_cache_utils.h"],
    deps = [
        ":motion_planner_nonvolatile_cache",
        "//intrinsic/world/objects:object_world",
        "//intrinsic_motion_planning/intrinsic/motion_planning/motion_planner",
        "//intrinsic_motion_planning/intrinsic/motion_planning/motion_planner:motion_planning_utils",
        "//intrinsic_motion_planning/intrinsic/motion_planning/motion_planner:robot_specification",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning:path_segment",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning/planners:validation",
        "//intrinsic_motion_planning/intrinsic/motion_planning/proto/v1:motion_planner_service_cc_proto",
        "//intrinsic_motion_planning/intrinsic/motion_planning/proto/v1:motion_specification_cc_proto",
        "//intrinsic_sdk/intrinsic/util:eigen",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
        "@intrinsic_apis//intrinsic/motion_planning/proto/v1:geometric_constraints_cc_proto",
        "@intrinsic_apis//intrinsic/motion_planning/proto/v1:motion_planner_config_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:object_world_refs_cc_proto",
    ],
)

# keep
proto_library(
    name = "motion_planner_nonvolatile_cache_proto",
    srcs = ["motion_planner_nonvolatile_cache.proto"],
    strip_import_prefix = "/intrinsic_motion_planning",
    deps = [
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning:path_segment_proto",
        "//intrinsic_motion_planning/intrinsic/motion_planning/proto/v1:motion_events_proto",
        "//intrinsic_motion_planning/intrinsic/motion_planning/proto/v1:motion_specification_proto",
        "@intrinsic_apis//intrinsic/icon/proto:joint_space_proto",
        "@intrinsic_apis//intrinsic/math/proto:pose_proto",
        "@intrinsic_apis//intrinsic/motion_planning/proto/v1:robot_specification_proto",
    ],
)

cc_proto_library(
    name = "motion_planner_nonvolatile_cache_cc_proto",
    deps = [":motion_planner_nonvolatile_cache_proto"],
)

go_proto_library(
    name = "motion_planner_nonvolatile_cache_go_proto",
    importpath = "intrinsic_motion_planning/intrinsic/motion_planning/service/nonvolatile_cache/motion_planner_nonvolatile_cache_go_proto",
    proto = ":motion_planner_nonvolatile_cache_proto",
    deps = [
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning:path_segment_go_proto",
        "//intrinsic_motion_planning/intrinsic/motion_planning/proto/v1:motion_events_go_proto",
        "//intrinsic_motion_planning/intrinsic/motion_planning/proto/v1:motion_specification_go_proto",
        "@intrinsic_apis//intrinsic/icon/proto:joint_space_go_proto",
        "@intrinsic_apis//intrinsic/math/proto:pose_go_proto",
        "@intrinsic_apis//intrinsic/motion_planning/proto/v1:robot_specification_go_proto",
    ],
)
