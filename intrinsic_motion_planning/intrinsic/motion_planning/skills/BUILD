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

# Skills for motion planning.

package(default_visibility = [
    "//visibility:public",
])

cc_library(
    name = "motion_planning_util",
    srcs = ["motion_planning_util.cc"],
    hdrs = ["motion_planning_util.h"],
    deps = [
        "//intrinsic_control/intrinsic/icon/proto:eigen_conversion",
        "//intrinsic_kinematics/intrinsic/kinematics/types:cartesian_limits",
        "//intrinsic_motion_planning/intrinsic/motion_planning/proto/v1:motion_planner_service_cc_proto",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/world/objects:kinematic_object",
        "//intrinsic_sdk/intrinsic/world/proto:object_world_service_cc_proto",
        "@intrinsic_apis//intrinsic/motion_planning/proto/v1:robot_specification_cc_proto",
    ],
)

cc_library(
    name = "motion_planning_error_util",
    srcs = ["motion_planning_error_util.cc"],
    hdrs = ["motion_planning_error_util.h"],
    deps = [
        "//intrinsic_control/intrinsic/icon/proto:eigen_conversion",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/util:eigen",
        "//intrinsic_sdk/intrinsic/util/status:get_extended_status",
        "//intrinsic_sdk/intrinsic/util/status:status_builder",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:cord",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/time",
        "@intrinsic_apis//intrinsic/icon/proto:joint_space_cc_proto",
        "@intrinsic_apis//intrinsic/math/proto:point_cc_proto",
        "@intrinsic_apis//intrinsic/math/proto:pose_cc_proto",
        "@intrinsic_apis//intrinsic/math/proto:quaternion_cc_proto",
        "@intrinsic_apis//intrinsic/motion_planning/proto/v1:geometric_constraints_cc_proto",
        "@intrinsic_apis//intrinsic/motion_planning/proto/v1:motion_planning_error_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:object_world_refs_cc_proto",
    ],
)

proto_library(
    name = "move_robot_proto",
    srcs = ["move_robot.proto"],
    # keep
    strip_import_prefix = "/intrinsic_motion_planning",
    visibility = [
        ":__pkg__",
        "//intrinsic:public_asset_users",
        "//intrinsic/manipulation/skills:__pkg__",
        "//intrinsic/skills/apps:__pkg__",
        "//intrinsic/skills/apps/icon2:__pkg__",
    ],
    deps = [
        "//intrinsic_control/intrinsic/icon/proto/v1:motion_event_types_proto",
        "//intrinsic_motion_planning/intrinsic/motion_planning/proto/v1:motion_events_proto",
        "@intrinsic_apis//intrinsic/assets/proto:field_metadata_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:resolved_dependency_proto",
        "@intrinsic_apis//intrinsic/icon/proto:joint_space_proto",
        "@intrinsic_apis//intrinsic/icon/proto/v1:condition_types_proto",
        "@intrinsic_apis//intrinsic/kinematics/types:joint_limits_proto",
        "@intrinsic_apis//intrinsic/motion_planning/proto/v1:geometric_constraints_proto",
        "@intrinsic_apis//intrinsic/motion_planning/proto/v1:motion_blending_parameter_proto",
        "@intrinsic_apis//intrinsic/motion_planning/proto/v1:motion_planner_config_proto",
        "@intrinsic_apis//intrinsic/motion_planning/proto/v1:motion_planning_limits_proto",
        "@intrinsic_apis//intrinsic/skills/proto:skill_parameter_metadata_proto",
        "@intrinsic_apis//intrinsic/world/proto:collision_checker_config_proto",
        "@intrinsic_apis//intrinsic/world/proto:collision_settings_proto",
        "@intrinsic_apis//intrinsic/world/proto:object_world_refs_proto",
    ] + [],
)

cc_proto_library(
    name = "move_robot_cc_proto",
    visibility = [
        ":__pkg__",
        "//intrinsic/manipulation/skills:__pkg__",
        "//intrinsic/skills/apps:__pkg__",
        "//intrinsic/skills/apps/icon2:__pkg__",
        "//intrinsic_perception/intrinsic/perception/skills/calibration:__pkg__",
        "//incode/motion_planning/skills:__pkg__",
    ],
    deps = [":move_robot_proto"],
)

go_proto_library(
    name = "move_robot_go_proto",
    importpath = "intrinsic/motion_planning/skills/move_robot_go_proto",
    proto = ":move_robot_proto",
    visibility = [
        "//intrinsic/logging:__subpackages__",
        "//intrinsic_sdk/intrinsic/logging:__subpackages__",
    ],
    deps = [
        "//intrinsic_control/intrinsic/icon/proto/v1:motion_event_types_go_proto",
        "//intrinsic_motion_planning/intrinsic/motion_planning/proto/v1:motion_events_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:field_metadata_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:resolved_dependency_go_proto",
        "@intrinsic_apis//intrinsic/icon/proto:joint_space_go_proto",
        "@intrinsic_apis//intrinsic/icon/proto/v1:condition_types_go_proto",
        "@intrinsic_apis//intrinsic/kinematics/types:joint_limits_go_proto",
        "@intrinsic_apis//intrinsic/motion_planning/proto/v1:geometric_constraints_go_proto",
        "@intrinsic_apis//intrinsic/motion_planning/proto/v1:motion_blending_parameter_go_proto",
        "@intrinsic_apis//intrinsic/motion_planning/proto/v1:motion_planner_config_go_proto",
        "@intrinsic_apis//intrinsic/motion_planning/proto/v1:motion_planning_limits_go_proto",
        "@intrinsic_apis//intrinsic/skills/proto:skill_parameter_metadata_go_proto",
        "@intrinsic_apis//intrinsic/world/proto:collision_checker_config_go_proto",
        "@intrinsic_apis//intrinsic/world/proto:collision_settings_go_proto",
        "@intrinsic_apis//intrinsic/world/proto:object_world_refs_go_proto",
    ] + [],
)

py_proto_library(
    name = "move_robot_py_pb2",
    deps = [":move_robot_proto"],
)
