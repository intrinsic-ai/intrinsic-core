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
load("@com_google_protobuf//bazel:proto_library.bzl", "proto_library")
load("@com_google_protobuf//bazel:py_proto_library.bzl", "py_proto_library")
load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")
load("//bazel:go_macros.bzl", "go_proto_library")
load("//bazel:python.bzl", "py_library", "py_test")
load("//intrinsic/skills/build_defs:skill.bzl", "cc_skill", "py_skill", "skill_manifest")

# Skills for motion planning.

package(default_visibility = [
    "//visibility:public",
])

proto_library(
    name = "preplan_motion_proto",
    srcs = ["preplan_motion.proto"],
    visibility = [
        "//intrinsic:public_asset_users",
        "//intrinsic/manipulation/skills:__subpackages__",
    ],
    deps = [
        "//intrinsic/skills/apps:attach_object_to_robot_proto",
        "//intrinsic/skills/apps:detach_object_proto",
        "//intrinsic/skills/apps:update_world_proto",
        "//intrinsic_motion_planning/intrinsic/motion_planning/skills:move_robot_proto",
        "@intrinsic_apis//intrinsic/assets/proto:field_metadata_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:resolved_dependency_proto",
    ],
)

cc_proto_library(
    name = "preplan_motion_cc_proto",
    visibility = [
        "//intrinsic:public_asset_users",
        "//intrinsic/manipulation/skills:__subpackages__",
    ],
    deps = [
        ":preplan_motion_proto",
    ],
)

cc_library(
    name = "preplan_motion",
    srcs = ["preplan_motion.cc"],
    hdrs = ["preplan_motion.h"],
    visibility = [
        "//intrinsic/manipulation/skills:__subpackages__",
    ],
    deps = [
        ":move_robot",
        ":preplan_motion_cc_proto",
        "//intrinsic/assets/dependencies:utils_cc",
        "//intrinsic/skills/apps:attach_object_to_robot",
        "//intrinsic/skills/apps:attach_object_to_robot_cc_proto",
        "//intrinsic/skills/apps:detach_object",
        "//intrinsic/skills/apps:detach_object_cc_proto",
        "//intrinsic/skills/apps:update_world_cc_proto",
        "//intrinsic/skills/internal:kinematic_object_for_position_part",
        "//intrinsic/skills/proto:skill_service_cc_proto",
        "//intrinsic/stats:scoped_span",
        "//intrinsic/util:eigen",
        "//intrinsic/util:proto_time",
        "//intrinsic/util/status:status_macros",
        "//intrinsic/world/proto:object_world_updates_cc_proto",
        "//intrinsic_motion_planning/intrinsic/motion_planning/proto/v1:motion_planner_service_cc_grpc_proto",
        "//intrinsic_motion_planning/intrinsic/motion_planning/proto/v1:motion_planner_service_cc_proto",
        "//intrinsic_motion_planning/intrinsic/motion_planning/service:motion_planner_service_asset_utils",
        "//intrinsic_motion_planning/intrinsic/motion_planning/service/debug:timing_debug_logger",
        "//intrinsic_sdk/intrinsic/connect/cc/grpc:channel",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/motion_planning:motion_planner_client",
        "//intrinsic_sdk/intrinsic/skills/cc:skill_interface",
        "//intrinsic_sdk/intrinsic/skills/cc:skill_interface_utils",
        "//intrinsic_sdk/intrinsic/world/objects:kinematic_object",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_client",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/assets/proto/v1:resolved_dependency_cc_proto",
        "@intrinsic_apis//intrinsic/skills/proto:footprint_cc_proto",
    ],
)

cc_test(
    name = "preplan_motion_test",
    srcs = ["preplan_motion_test.cc"],
    data = [
        ":preplan_motion_manifest",
        "//intrinsic/world/test_data:4_robots_cube_world",
    ],
    deps = [
        ":preplan_motion",
        ":preplan_motion_cc_proto",
        "//intrinsic/eigenmath:matchers",
        "//intrinsic/skills/apps:attach_object_to_robot_cc_proto",
        "//intrinsic/skills/apps:detach_object_cc_proto",
        "//intrinsic/skills/apps:update_world_cc_proto",
        "//intrinsic/skills/proto:skill_service_cc_proto",
        "//intrinsic/skills/testing:skill_test_utils_cc",
        "//intrinsic/util/path_resolver",
        "//intrinsic/util/proto:parse_text_proto",
        "//intrinsic/util/status:status_macros",
        "//intrinsic/world",
        "//intrinsic/world/objects/test:object_world_test_utils",
        "//intrinsic/world/proto:object_world_service_cc_grpc_proto",
        "//intrinsic/world/proto:object_world_updates_cc_proto",
        "//intrinsic/world/service/test:world_service_fake",
        "//intrinsic_control/intrinsic/icon/cc_client/testing:channel_fake",
        "//intrinsic_control/intrinsic/icon/equipment/testing:equipment_test_utils",
        "//intrinsic_motion_planning/intrinsic/motion_planning/service:motion_planner_service",
        "//intrinsic_motion_planning/intrinsic/motion_planning/service:motion_planner_service_asset_utils",
        "//intrinsic_motion_planning/intrinsic/motion_planning/service:motion_planner_service_in_process",
        "//intrinsic_motion_planning/intrinsic/motion_planning/service:motion_planner_service_proxy",
        "//intrinsic_motion_planning/intrinsic/motion_planning/skills:move_robot_cc_proto",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/skills/cc:equipment_pack",
        "//intrinsic_sdk/intrinsic/skills/cc:skill_interface",
        "//intrinsic_sdk/intrinsic/world/gzfile",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_client",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_ids",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/log:scoped_mock_log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:string_view",
        "@com_google_cel_cpp//internal:testing_no_main",
        "@com_google_googletest//:gtest_main",
        "@intrinsic_apis//intrinsic/assets/proto:id_cc_proto",
        "@intrinsic_apis//intrinsic/icon/proto:part_status_cc_proto",
        "@intrinsic_apis//intrinsic/math/proto:quaternion_cc_proto",
        "@intrinsic_apis//intrinsic/skills/proto:equipment_cc_proto",
        "@intrinsic_apis//intrinsic/skills/proto:skill_manifest_cc_proto",
    ],
)

skill_manifest(
    name = "preplan_motion_manifest",
    src = "preplan_motion_manifest.textproto",
    incompatible_disallow_manifest_dependencies = False,
    visibility = [
        "//intrinsic:public_asset_users",
        "//intrinsic:release_tooling",
    ],
    deps = [
        ":preplan_motion_proto",
    ],
)

cc_skill(
    name = "preplan_motion_skill",
    manifest = ":preplan_motion_manifest",
    visibility = [
        "//intrinsic:public_asset_users",
        "//intrinsic:release_tooling",
    ],
    deps = [
        ":preplan_motion",
        ":preplan_motion_cc_proto",
    ],
)

# uncomment when preplan_motion_go_proto is needed
# gazelle:proto disable

cc_library(
    name = "move_robot",
    srcs = ["move_robot.cc"],
    hdrs = ["move_robot.h"],
    visibility = [
        "//intrinsic/skills/apps/icon2:__pkg__",
        "//intrinsic_motion_planning/intrinsic/motion_planning/skills:__pkg__",
    ],
    deps = [
        ":move_robot_util",
        "//intrinsic/assets/dependencies:utils_cc",
        "//intrinsic/choreographer/footprints:footprint",
        "//intrinsic/choreographer/footprints:object_world_reservation",
        "//intrinsic/logging:log_item_builder",
        "//intrinsic/skills/proto:skill_service_cc_proto",
        "//intrinsic/stats:scoped_span",
        "//intrinsic/util:eigen",
        "//intrinsic/util:proto_time",
        "//intrinsic/util/grpc:channel_interface",
        "//intrinsic/util/status:status_macros",
        "//intrinsic/world/proto:object_world_updates_cc_proto",
        "//intrinsic_control/intrinsic/icon/cc_client:client",
        "//intrinsic_control/intrinsic/icon/equipment:channel_factory",
        "//intrinsic_control/intrinsic/icon/equipment:equipment_utils",
        "//intrinsic_control/intrinsic/icon/proto:cart_space_conversion",
        "//intrinsic_control/intrinsic/icon/proto:joint_state_conversion",
        "//intrinsic_control/intrinsic/icon/skills/util:position_part_util",
        "//intrinsic_control/intrinsic/icon/utils:arm_utils",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_kinematics/intrinsic/kinematics/types:cartesian_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:check_cartesian_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:check_joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits_xd",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state",
        "//intrinsic_kinematics/intrinsic/kinematics/types:to_string",
        "//intrinsic_motion_planning/intrinsic/motion_planning:trajectory_execution_utils",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning:path_planner_definitions",
        "//intrinsic_motion_planning/intrinsic/motion_planning/proto/v1:motion_planner_service_cc_grpc_proto",
        "//intrinsic_motion_planning/intrinsic/motion_planning/proto/v1:motion_planner_service_cc_proto",
        "//intrinsic_motion_planning/intrinsic/motion_planning/proto/v1:motion_specification_cc_proto",
        "//intrinsic_motion_planning/intrinsic/motion_planning/service:motion_planner_service_asset_utils",
        "//intrinsic_motion_planning/intrinsic/motion_planning/service/debug:timing_debug_logger",
        "//intrinsic_motion_planning/intrinsic/motion_planning/skills:motion_planning_error_util",
        "//intrinsic_motion_planning/intrinsic/motion_planning/skills:motion_planning_util",
        "//intrinsic_motion_planning/intrinsic/motion_planning/skills:move_robot_cc_proto",
        "//intrinsic_sdk/intrinsic/connect/cc/grpc:channel",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/logging:data_logger_client",
        "//intrinsic_sdk/intrinsic/motion_planning:motion_planner_client",
        "//intrinsic_sdk/intrinsic/skills/cc:equipment_pack",
        "//intrinsic_sdk/intrinsic/skills/cc:skill_data",
        "//intrinsic_sdk/intrinsic/skills/cc:skill_interface",
        "//intrinsic_sdk/intrinsic/skills/cc:skill_utils",
        "//intrinsic_sdk/intrinsic/world/objects:kinematic_object",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_client",
        "@abseil-cpp//absl/base:no_destructor",
        "@abseil-cpp//absl/cleanup",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/time",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/assets/proto/v1:resolved_dependency_cc_proto",
        "@intrinsic_apis//intrinsic/icon/equipment:icon_equipment_cc_proto",
        "@intrinsic_apis//intrinsic/icon/proto:generic_part_config_cc_proto",
        "@intrinsic_apis//intrinsic/icon/proto:joint_space_cc_proto",
        "@intrinsic_apis//intrinsic/icon/proto:part_status_cc_proto",
        "@intrinsic_apis//intrinsic/math/proto:quaternion_cc_proto",
        "@intrinsic_apis//intrinsic/motion_planning/proto/v1:geometric_constraints_cc_proto",
        "@intrinsic_apis//intrinsic/motion_planning/proto/v1:motion_blending_parameter_cc_proto",
        "@intrinsic_apis//intrinsic/motion_planning/proto/v1:motion_planning_limits_cc_proto",
        "@intrinsic_apis//intrinsic/skills/proto:equipment_cc_proto",
        "@intrinsic_apis//intrinsic/skills/proto:skills_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:collision_settings_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:object_world_refs_cc_proto",
    ] + [],
)

cc_test(
    name = "move_robot_test",
    timeout = "moderate",
    srcs = ["move_robot_test.cc"],
    data = [
        "//intrinsic/world/test_data:4_robots_cube_world",
        "//incode/motion_planning/skills:move_robot_manifest",
    ],
    flaky = 1,
    shard_count = 10,
    tags = [
        "nomsan",
    ],
    deps = [
        ":move_robot",
        "//intrinsic/assets/testing:id_test_utils",
        "//intrinsic/skills/apps/testing:test_world",
        "//intrinsic/skills/internal:skill_service_client",
        "//intrinsic/skills/proto:skill_service_cc_proto",
        "//intrinsic/skills/testing:skill_test_utils_cc",
        "//intrinsic/util:eigen",
        "//intrinsic/util:proto_time",
        "//intrinsic/util/proto:parse_text_proto",
        "//intrinsic/util/proto:type_url",
        "//intrinsic/util/status:status_macros",
        "//intrinsic/util/thread",
        "//intrinsic/world",
        "//intrinsic/world:entity",
        "//intrinsic/world:grouping",
        "//intrinsic/world/objects/test:object_world_test_utils",
        "//intrinsic/world/proto:object_world_service_cc_grpc_proto",
        "//intrinsic/world/service/test:world_service_fake",
        "//intrinsic_control/intrinsic/icon/actions:adio_info",
        "//intrinsic_control/intrinsic/icon/actions:trajectory_tracking_action_info",
        "//intrinsic_control/intrinsic/icon/cc_client:client",
        "//intrinsic_control/intrinsic/icon/cc_client:condition",
        "//intrinsic_control/intrinsic/icon/cc_client:state_variable_path",
        "//intrinsic_control/intrinsic/icon/cc_client/testing:channel_fake",
        "//intrinsic_control/intrinsic/icon/cc_client/testing:rtcl_controller_channel_fake",
        "//intrinsic_control/intrinsic/icon/cc_client/testing:rtcl_controller_fake_session",
        "//intrinsic_control/intrinsic/icon/common:builtins",
        "//intrinsic_control/intrinsic/icon/common:id_types",
        "//intrinsic_control/intrinsic/icon/equipment:channel_factory",
        "//intrinsic_control/intrinsic/icon/equipment:equipment_utils",
        "//intrinsic_control/intrinsic/icon/equipment/testing:equipment_test_utils",
        "//intrinsic_control/intrinsic/icon/equipment/testing:fake_channel_factory",
        "//intrinsic_control/intrinsic/icon/proto:cart_space_conversion",
        "//intrinsic_control/intrinsic/icon/skills:update_robot_joint_positions_cc_proto",
        "//intrinsic_kinematics/intrinsic/kinematics/types:cartesian_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits_matcher",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits_xd",
        "//intrinsic_motion_planning/intrinsic/motion_planning/service:motion_planner_service",
        "//intrinsic_motion_planning/intrinsic/motion_planning/service:motion_planner_service_asset_utils",
        "//intrinsic_motion_planning/intrinsic/motion_planning/service:motion_planner_service_in_process",
        "//intrinsic_motion_planning/intrinsic/motion_planning/service:motion_planner_service_proxy",
        "//intrinsic_motion_planning/intrinsic/motion_planning/skills:move_robot_cc_proto",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:proto_conversion",
        "//intrinsic_sdk/intrinsic/skills/cc:equipment_pack",
        "//intrinsic_sdk/intrinsic/skills/cc:skill_data",
        "//intrinsic_sdk/intrinsic/skills/cc:skill_interface",
        "//intrinsic_sdk/intrinsic/world:entity_id",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_client",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_ids",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/log:scoped_mock_log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:cord",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@com_google_cel_cpp//internal:testing_no_main",
        "@com_google_googletest//:gtest_main",
        "@com_google_protobuf//:differencer",
        "@intrinsic_apis//intrinsic/assets/proto:id_cc_proto",
        "@intrinsic_apis//intrinsic/icon/equipment:icon_equipment_cc_proto",
        "@intrinsic_apis//intrinsic/icon/proto:generic_part_config_cc_proto",
        "@intrinsic_apis//intrinsic/icon/proto:io_block_cc_proto",
        "@intrinsic_apis//intrinsic/icon/proto:part_status_cc_proto",
        "@intrinsic_apis//intrinsic/icon/proto/v1:service_cc_proto",
        "@intrinsic_apis//intrinsic/skills/proto:equipment_cc_proto",
        "@intrinsic_apis//intrinsic/skills/proto:skill_manifest_cc_proto",
    ] + [],
)

cc_skill(
    name = "move_robot_skill",
    manifest = ":move_robot_manifest",
    visibility = [
        "//intrinsic:public_asset_users",
        "//intrinsic:release_tooling",
        "//intrinsic/skills/apps:__pkg__",
        "//intrinsic/skills/apps/icon2:__pkg__",
    ],
    deps = [
        ":move_robot",
        "//intrinsic_motion_planning/intrinsic/motion_planning/skills:move_robot_cc_proto",
    ],
)

cc_library(
    name = "move_robot_util",
    srcs = ["move_robot_util.cc"],
    hdrs = ["move_robot_util.h"],
    deps = [
        "//intrinsic/util/status:status_macros",
        "//intrinsic_control/intrinsic/icon/cc_client:state_variable_path",
        "//intrinsic_control/intrinsic/icon/equipment:equipment_utils",
        "//intrinsic_motion_planning/intrinsic/motion_planning/proto/v1:motion_specification_cc_proto",
        "//intrinsic_motion_planning/intrinsic/motion_planning/skills:move_robot_cc_proto",
        "//intrinsic_sdk/intrinsic/skills/cc:equipment_pack",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:string_view",
        "@intrinsic_apis//intrinsic/icon/equipment:icon_equipment_cc_proto",
        "@intrinsic_apis//intrinsic/icon/proto:joint_space_cc_proto",
        "@intrinsic_apis//intrinsic/math/proto:pose_cc_proto",
        "@intrinsic_apis//intrinsic/math/proto:quaternion_cc_proto",
        "@intrinsic_apis//intrinsic/motion_planning/proto/v1:geometric_constraints_cc_proto",
        "@intrinsic_apis//intrinsic/motion_planning/proto/v1:motion_blending_parameter_cc_proto",
        "@intrinsic_apis//intrinsic/motion_planning/proto/v1:motion_planning_limits_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:collision_settings_cc_proto",
    ] + [],
)

skill_manifest(
    name = "move_robot_manifest",
    src = "move_robot_manifest.textproto",
    incompatible_disallow_manifest_dependencies = False,
    visibility = ["//intrinsic_perception/intrinsic/perception/skills/calibration:__pkg__"],
    deps = [
        "//intrinsic_motion_planning/intrinsic/motion_planning/skills:move_robot_proto",
    ],
)

proto_library(
    name = "clear_motion_planner_service_cache_proto",
    srcs = ["clear_motion_planner_service_cache.proto"],
    deps = [
        "@intrinsic_apis//intrinsic/assets/proto:field_metadata_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:resolved_dependency_proto",
    ],
)

py_proto_library(
    name = "clear_motion_planner_service_cache_py_pb2",
    deps = [
        ":clear_motion_planner_service_cache_proto",
    ],
)

go_proto_library(
    name = "clear_motion_planner_service_cache_go_proto",
    importpath = "incode/motion_planning/skills/clear_motion_planner_service_cache_go_proto",
    protos = [":clear_motion_planner_service_cache_proto"],
    deps = [
        "@intrinsic_apis//intrinsic/assets/proto:field_metadata_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:resolved_dependency_go_proto",
    ],
)

cc_proto_library(
    name = "clear_motion_planner_service_cache_cc_proto",
    deps = [
        ":clear_motion_planner_service_cache_proto",
    ],
)

skill_manifest(
    name = "clear_motion_planner_service_cache_manifest",
    src = "clear_motion_planner_service_cache_manifest.textproto",
    visibility = [
        "//intrinsic:public_asset_users",
        "//intrinsic:release_tooling",
    ],
    deps = [
        ":clear_motion_planner_service_cache_proto",
    ],
)

py_library(
    name = "clear_motion_planner_service_cache",
    srcs = ["clear_motion_planner_service_cache.py"],
    deps = [
        ":clear_motion_planner_service_cache_py_pb2",
        "//intrinsic/util:decorators",
        "//intrinsic_sdk/intrinsic/motion_planning:motion_planner_client_py",
        "//intrinsic_sdk/intrinsic/skills/python:skill_interface",
        "//intrinsic_sdk/intrinsic/skills/python:skill_interface_utils",
        "@com_google_absl_py//absl/logging",
        "@intrinsic_apis//intrinsic/skills/proto:footprint_py_pb2",
    ],
)

py_skill(
    name = "clear_motion_planner_service_cache_skill",
    manifest = ":clear_motion_planner_service_cache_manifest",
    visibility = [
        "//intrinsic:public_asset_users",
        "//intrinsic:release_tooling",
    ],
    deps = [
        ":clear_motion_planner_service_cache",
        ":clear_motion_planner_service_cache_py_pb2",
    ],
)

py_test(
    name = "clear_motion_planner_service_cache_test",
    srcs = ["clear_motion_planner_service_cache_test.py"],
    data = [
        ":clear_motion_planner_service_cache_manifest",
    ],
    deps = [
        ":clear_motion_planner_service_cache",
        ":clear_motion_planner_service_cache_py_pb2",
        "//intrinsic/skills/internal:preview_context_impl_py",
        "//intrinsic/skills/testing:skill_test_utils",
        "//intrinsic_sdk/intrinsic/motion_planning:motion_planner_client_py",
        "@com_github_grpc_grpc//src/python/grpcio/grpc:grpcio",
        "@com_google_absl_py//absl/logging",
        "@com_google_absl_py//absl/testing:absltest",
        "@intrinsic_apis//intrinsic/assets/proto/v1:resolved_dependency_py_pb2",
        "@rules_python//python/runfiles",
    ],
)
