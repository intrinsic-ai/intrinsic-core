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
load("//intrinsic_sdk/intrinsic/skills/build_defs:skill.bzl", "cc_skill", "skill_manifest")

# ----------------------------------------------------------------------------
# Skill definition for 'dio_read_input'

# Allow use per application.
dio_read_input_visibility = [
    "//intrinsic:public_asset_users",
]

proto_library(
    name = "dio_read_input_proto",
    srcs = ["dio_read_input.proto"],
    visibility = dio_read_input_visibility,
    deps = [
        "@com_google_protobuf//:duration_proto",
    ],
)

cc_proto_library(
    name = "dio_read_input_cc_proto",
    visibility = dio_read_input_visibility,
    deps = [
        ":dio_read_input_proto",
    ],
)

go_proto_library(
    name = "dio_read_input_go_proto",
    importpath = "intrinsic/icon/skills/dio_read_input_go_proto",
    protos = [":dio_read_input_proto"],
    visibility = dio_read_input_visibility,
)

skill_manifest(
    name = "dio_read_input_cc_manifest",
    src = "dio_read_input_cc_manifest.textproto",
    incompatible_disallow_manifest_dependencies = False,
    visibility = [
        "//intrinsic:public_asset_users",
        "//intrinsic:release_tooling",
        "//intrinsic/skills/apps:__subpackages__",
    ],
    deps = [
        ":dio_read_input_proto",
    ],
)

cc_library(
    name = "dio_read_input",
    srcs = ["dio_read_input.cc"],
    hdrs = ["dio_read_input.h"],
    deps = [
        ":dio_read_input_cc_proto",
        "//intrinsic_control/intrinsic/icon/cc_client:client",
        "//intrinsic_control/intrinsic/icon/equipment:channel_factory",
        "//intrinsic_control/intrinsic/icon/equipment:equipment_utils",
        "//intrinsic_control/intrinsic/icon/skills/util:adio_util",
        "//intrinsic_sdk/intrinsic/skills/cc:skill_interface",
        "//intrinsic_sdk/intrinsic/skills/cc:skill_utils",
        "//intrinsic_sdk/intrinsic/util:proto_time",
        "//intrinsic_sdk/intrinsic/util/grpc:channel_interface",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/icon/equipment:icon_equipment_cc_proto",
        "@intrinsic_apis//intrinsic/icon/proto:io_block_cc_proto",
        "@intrinsic_apis//intrinsic/icon/proto:part_status_cc_proto",
    ],
)

cc_skill(
    name = "dio_read_input_skill",
    manifest = ":dio_read_input_cc_manifest",
    visibility = dio_read_input_visibility + ["//intrinsic:release_tooling"],
    deps = [
        ":dio_read_input",
        ":dio_read_input_cc_proto",
    ],
)

# ----------------------------------------------------------------------------
# Skill definition for 'aio_read_input'

# Allow use per application.
aio_read_input_visibility = [
    "//intrinsic:public_asset_users",
    "//intrinsic/apps/bluebird_caw:__subpackages__",
]

proto_library(
    name = "aio_read_input_proto",
    srcs = ["aio_read_input.proto"],
    visibility = aio_read_input_visibility,
    deps = [
        "@com_google_protobuf//:duration_proto",
    ],
)

cc_proto_library(
    name = "aio_read_input_cc_proto",
    visibility = aio_read_input_visibility,
    deps = [
        ":aio_read_input_proto",
    ],
)

go_proto_library(
    name = "aio_read_input_go_proto",
    importpath = "intrinsic/icon/skills/aio_read_input_go_proto",
    protos = [":aio_read_input_proto"],
    visibility = aio_read_input_visibility,
)

skill_manifest(
    name = "aio_read_input_cc_manifest",
    src = "aio_read_input_cc_manifest.textproto",
    incompatible_disallow_manifest_dependencies = False,
    visibility = [
        "//intrinsic:public_asset_users",
        "//intrinsic:release_tooling",
        "//intrinsic/skills/apps:__subpackages__",
    ],
    deps = [
        ":aio_read_input_proto",
    ],
)

cc_library(
    name = "aio_read_input",
    srcs = ["aio_read_input.cc"],
    hdrs = ["aio_read_input.h"],
    deps = [
        ":aio_read_input_cc_proto",
        "//intrinsic_control/intrinsic/icon/cc_client:client",
        "//intrinsic_control/intrinsic/icon/equipment:channel_factory",
        "//intrinsic_control/intrinsic/icon/equipment:equipment_utils",
        "//intrinsic_control/intrinsic/icon/skills/util:adio_util",
        "//intrinsic_sdk/intrinsic/skills/cc:skill_interface",
        "//intrinsic_sdk/intrinsic/skills/cc:skill_utils",
        "//intrinsic_sdk/intrinsic/util:proto_time",
        "//intrinsic_sdk/intrinsic/util/grpc:channel_interface",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/icon/equipment:icon_equipment_cc_proto",
        "@intrinsic_apis//intrinsic/icon/proto:io_block_cc_proto",
        "@intrinsic_apis//intrinsic/icon/proto:part_status_cc_proto",
    ],
)

cc_skill(
    name = "aio_read_input_skill",
    manifest = ":aio_read_input_cc_manifest",
    visibility = aio_read_input_visibility + ["//intrinsic:release_tooling"],
    deps = [
        ":aio_read_input",
        ":aio_read_input_cc_proto",
    ],
)

# ----------------------------------------------------------------------------
# Skill definition for 'dio_set_output'

# Allow use per application.
dio_set_output_visibility = [
    "//intrinsic:public_asset_users",
]

proto_library(
    name = "dio_set_output_proto",
    srcs = ["dio_set_output.proto"],
    visibility = dio_set_output_visibility,
)

cc_proto_library(
    name = "dio_set_output_cc_proto",
    visibility = dio_set_output_visibility,
    deps = [
        ":dio_set_output_proto",
    ],
)

go_proto_library(
    name = "dio_set_output_go_proto",
    importpath = "intrinsic/icon/skills/dio_set_output_go_proto",
    protos = [":dio_set_output_proto"],
    visibility = dio_set_output_visibility,
)

skill_manifest(
    name = "dio_set_output_cc_manifest",
    src = "dio_set_output_cc_manifest.textproto",
    incompatible_disallow_manifest_dependencies = False,
    visibility = [
        "//intrinsic:public_asset_users",
        "//intrinsic:release_tooling",
        "//intrinsic/skills/apps:__subpackages__",
    ],
    deps = [
        ":dio_set_output_proto",
    ],
)

cc_library(
    name = "dio_set_output",
    srcs = ["dio_set_output.cc"],
    hdrs = ["dio_set_output.h"],
    deps = [
        ":dio_set_output_cc_proto",
        "//intrinsic_control/intrinsic/icon/equipment:channel_factory",
        "//intrinsic_control/intrinsic/icon/skills/util:analog_digital_io",
        "//intrinsic_control/intrinsic/icon/skills/util:analog_digital_io_factory",
        "//intrinsic_sdk/intrinsic/skills/cc:skill_interface",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/skills/proto:footprint_cc_proto",
    ],
)

cc_skill(
    name = "dio_set_output_skill",
    manifest = ":dio_set_output_cc_manifest",
    visibility = dio_set_output_visibility + ["//intrinsic:release_tooling"],
    deps = [
        ":dio_set_output",
        ":dio_set_output_cc_proto",
    ],
)

# ----------------------------------------------------------------------------
# Skill definition for 'aio_set_output'

aio_set_output_visibility = [
    "//intrinsic:public_asset_users",
    "//intrinsic/apps/bluebird_caw:__subpackages__",
]

proto_library(
    name = "aio_set_output_proto",
    srcs = ["aio_set_output.proto"],
    visibility = aio_set_output_visibility,
)

cc_proto_library(
    name = "aio_set_output_cc_proto",
    visibility = aio_set_output_visibility,
    deps = [
        ":aio_set_output_proto",
    ],
)

go_proto_library(
    name = "aio_set_output_go_proto",
    importpath = "intrinsic/icon/skills/aio_set_output_go_proto",
    protos = [":aio_set_output_proto"],
    visibility = aio_set_output_visibility,
    deps = [
        "@intrinsic_apis//intrinsic/motion_planning/proto:motion_target_go_proto",
    ],
)

skill_manifest(
    name = "aio_set_output_cc_manifest",
    src = "aio_set_output_cc_manifest.textproto",
    incompatible_disallow_manifest_dependencies = False,
    visibility = [
        "//intrinsic:public_asset_users",
        "//intrinsic:release_tooling",
        "//intrinsic/skills/apps:__subpackages__",
    ],
    deps = [
        ":aio_set_output_proto",
    ],
)

cc_library(
    name = "aio_set_output",
    srcs = ["aio_set_output.cc"],
    hdrs = ["aio_set_output.h"],
    deps = [
        ":aio_set_output_cc_proto",
        "//intrinsic_control/intrinsic/icon/equipment:channel_factory",
        "//intrinsic_control/intrinsic/icon/skills/util:analog_digital_io",
        "//intrinsic_control/intrinsic/icon/skills/util:analog_digital_io_factory",
        "//intrinsic_sdk/intrinsic/skills/cc:skill_interface",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_skill(
    name = "aio_set_output_skill",
    manifest = ":aio_set_output_cc_manifest",
    visibility = aio_set_output_visibility + ["//intrinsic:release_tooling"],
    deps = [
        ":aio_set_output",
        ":aio_set_output_cc_proto",
    ],
)

# ----------------------------------------------------------------------------
# Skill definition for 'dio_wait_for_input'

# Allow use per application.
dio_wait_for_input_visibility = [
    "//intrinsic:public_asset_users",
]

proto_library(
    name = "dio_wait_for_input_proto",
    srcs = ["dio_wait_for_input.proto"],
    visibility = dio_wait_for_input_visibility,
    deps = [
        "@com_google_protobuf//:duration_proto",
    ],
)

cc_proto_library(
    name = "dio_wait_for_input_cc_proto",
    visibility = dio_wait_for_input_visibility,
    deps = [
        ":dio_wait_for_input_proto",
    ],
)

go_proto_library(
    name = "dio_wait_for_input_go_proto",
    importpath = "intrinsic/icon/skills/dio_wait_for_input_go_proto",
    protos = [":dio_wait_for_input_proto"],
    visibility = dio_wait_for_input_visibility,
)

skill_manifest(
    name = "dio_wait_for_input_cc_manifest",
    src = "dio_wait_for_input_cc_manifest.textproto",
    incompatible_disallow_manifest_dependencies = False,
    visibility = [
        "//intrinsic:public_asset_users",
        "//intrinsic:release_tooling",
        "//intrinsic/skills/apps:__subpackages__",
    ],
    deps = [
        ":dio_wait_for_input_proto",
    ],
)

cc_library(
    name = "dio_wait_for_input",
    srcs = ["dio_wait_for_input.cc"],
    hdrs = ["dio_wait_for_input.h"],
    deps = [
        ":dio_wait_for_input_cc_proto",
        "//intrinsic_control/intrinsic/icon/equipment:channel_factory",
        "//intrinsic_control/intrinsic/icon/skills/util:analog_digital_io",
        "//intrinsic_control/intrinsic/icon/skills/util:analog_digital_io_factory",
        "//intrinsic_sdk/intrinsic/skills/cc:skill_interface",
        "//intrinsic_sdk/intrinsic/util:proto_time",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/time",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/skills/proto:footprint_cc_proto",
    ],
)

cc_skill(
    name = "dio_wait_for_input_skill",
    manifest = ":dio_wait_for_input_cc_manifest",
    visibility = dio_wait_for_input_visibility + ["//intrinsic:release_tooling"],
    deps = [
        ":dio_wait_for_input",
        ":dio_wait_for_input_cc_proto",
    ],
)

skill_manifest(
    name = "enable_realtime_control_manifest",
    src = "enable_realtime_control_manifest.textproto",
    incompatible_disallow_manifest_dependencies = False,
    visibility = [
        "//intrinsic:public_asset_users",
        "//intrinsic:release_tooling",
        "//intrinsic/skills/apps/icon2:__subpackages__",
    ],
    deps = [
        ":enable_realtime_control_proto",
    ],
)

cc_skill(
    name = "enable_realtime_control_skill",
    manifest = ":enable_realtime_control_manifest",
    visibility = [
        "//intrinsic:public_asset_users",
        "//intrinsic:release_tooling",
    ],
    deps = [
        ":enable_realtime_control",
        ":enable_realtime_control_cc_proto",
    ],
)

proto_library(
    name = "enable_realtime_control_proto",
    srcs = ["enable_realtime_control.proto"],
    visibility = [
        "//intrinsic:public_asset_users",
    ],
)

cc_proto_library(
    name = "enable_realtime_control_cc_proto",
    deps = [
        ":enable_realtime_control_proto",
    ],
)

go_proto_library(
    name = "enable_realtime_control_go_proto",
    importpath = "intrinsic/icon/skills/enable_realtime_control_go_proto",
    protos = [":enable_realtime_control_proto"],
)

cc_library(
    name = "enable_realtime_control",
    srcs = ["enable_realtime_control.cc"],
    hdrs = ["enable_realtime_control.h"],
    deps = [
        ":enable_realtime_control_cc_proto",
        "//intrinsic/skills/internal:kinematic_object_for_position_part",
        "//intrinsic/stats:scoped_span",
        "//intrinsic_control/intrinsic/icon/cc_client:client",
        "//intrinsic_control/intrinsic/icon/cc_client:operational_status",
        "//intrinsic_control/intrinsic/icon/equipment:channel_factory",
        "//intrinsic_control/intrinsic/icon/equipment:equipment_utils",
        "//intrinsic_control/intrinsic/icon/utils:arm_utils",
        "//intrinsic_sdk/intrinsic/skills/cc:equipment_pack",
        "//intrinsic_sdk/intrinsic/skills/cc:skill_interface",
        "//intrinsic_sdk/intrinsic/skills/cc:skill_interface_utils",
        "//intrinsic_sdk/intrinsic/skills/proto:skill_service_cc_proto",
        "//intrinsic_sdk/intrinsic/util/status:status_builder",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world/objects:kinematic_object",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_client",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/icon/proto:part_status_cc_proto",
        "@intrinsic_apis//intrinsic/resources/proto:resource_handle_cc_proto",
        "@intrinsic_apis//intrinsic/skills/proto:equipment_cc_proto",
        "@intrinsic_apis//intrinsic/skills/proto:footprint_cc_proto",
        "@intrinsic_apis//intrinsic/skills/proto:skills_cc_proto",
    ],
)

skill_manifest(
    name = "disable_realtime_control_manifest",
    src = "disable_realtime_control_manifest.textproto",
    visibility = [
        "//intrinsic:public_asset_users",
        "//intrinsic:release_tooling",
    ],
    deps = [
        ":disable_realtime_control_proto",
    ],
)

cc_skill(
    name = "disable_realtime_control_skill",
    manifest = ":disable_realtime_control_manifest",
    visibility = [
        "//intrinsic:public_asset_users",
        "//intrinsic:release_tooling",
    ],
    deps = [
        ":disable_realtime_control",
        ":disable_realtime_control_cc_proto",
    ],
)

proto_library(
    name = "disable_realtime_control_proto",
    srcs = ["disable_realtime_control.proto"],
    visibility = ["//intrinsic:public_asset_users"],
    deps = [
        "@intrinsic_apis//intrinsic/assets/proto:field_metadata_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:resolved_dependency_proto",
    ],
)

cc_proto_library(
    name = "disable_realtime_control_cc_proto",
    deps = [
        ":disable_realtime_control_proto",
    ],
)

go_proto_library(
    name = "disable_realtime_control_go_proto",
    importpath = "intrinsic/icon/skills/disable_realtime_control_go_proto",
    protos = [":disable_realtime_control_proto"],
    deps = [
        "@intrinsic_apis//intrinsic/assets/proto:field_metadata_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:resolved_dependency_go_proto",
    ],
)

cc_library(
    name = "disable_realtime_control",
    srcs = ["disable_realtime_control.cc"],
    hdrs = ["disable_realtime_control.h"],
    deps = [
        ":disable_realtime_control_cc_proto",
        "//intrinsic_control/intrinsic/icon/cc_client:client",
        "//intrinsic_sdk/intrinsic/assets:interface_utils",
        "//intrinsic_sdk/intrinsic/assets/dependencies:utils_cc",
        "//intrinsic_sdk/intrinsic/skills/cc:skill_interface",
        "//intrinsic_sdk/intrinsic/skills/cc:skill_interface_utils",
        "//intrinsic_sdk/intrinsic/skills/proto:skill_service_cc_proto",
        "//intrinsic_sdk/intrinsic/util/grpc:channel",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/assets/proto/v1:resolved_dependency_cc_proto",
        "@intrinsic_apis//intrinsic/icon/proto/v1:service_cc_proto",
        "@intrinsic_apis//intrinsic/skills/proto:footprint_cc_proto",
    ],
)

skill_manifest(
    name = "update_robot_joint_positions_manifest",
    src = "update_robot_joint_positions_manifest.textproto",
    incompatible_disallow_manifest_dependencies = False,
    visibility = [
        "//intrinsic:public_asset_users",
        "//intrinsic:release_tooling",
        "//intrinsic/skills/apps/icon2:__subpackages__",
    ],
    deps = [
        ":update_robot_joint_positions_proto",
    ],
)

cc_skill(
    name = "update_robot_joint_positions_skill",
    manifest = ":update_robot_joint_positions_manifest",
    visibility = [
        "//intrinsic:public_asset_users",
        "//intrinsic:release_tooling",
    ],
    deps = [
        ":update_robot_joint_positions",
        ":update_robot_joint_positions_cc_proto",
    ],
)

proto_library(
    name = "update_robot_joint_positions_proto",
    srcs = ["update_robot_joint_positions.proto"],
    visibility = [
        "//intrinsic:public_asset_users",
    ],
)

cc_proto_library(
    name = "update_robot_joint_positions_cc_proto",
    visibility = [
        "//intrinsic_motion_planning/intrinsic/motion_planning/skills:__pkg__",
        "//intrinsic_perception/intrinsic/perception/skills/calibration:__pkg__",
        "//incode/motion_planning/skills:__pkg__",
    ],
    deps = [
        ":update_robot_joint_positions_proto",
    ],
)

go_proto_library(
    name = "update_robot_joint_positions_go_proto",
    importpath = "intrinsic/icon/skills/update_robot_joint_positions_go_proto",
    protos = [":update_robot_joint_positions_proto"],
)

cc_library(
    name = "update_robot_joint_positions",
    srcs = ["update_robot_joint_positions.cc"],
    hdrs = ["update_robot_joint_positions.h"],
    deps = [
        ":update_robot_joint_positions_cc_proto",
        "//intrinsic_control/intrinsic/icon/cc_client:client",
        "//intrinsic_control/intrinsic/icon/equipment:channel_factory",
        "//intrinsic_control/intrinsic/icon/equipment:equipment_utils",
        "//intrinsic_control/intrinsic/icon/utils:arm_utils",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/skills/cc:equipment_pack",
        "//intrinsic_sdk/intrinsic/skills/cc:skill_interface",
        "//intrinsic_sdk/intrinsic/skills/proto:skill_service_cc_proto",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world/objects:kinematic_object",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_client",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status:statusor",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/icon/proto:part_status_cc_proto",
        "@intrinsic_apis//intrinsic/skills/proto:equipment_cc_proto",
        "@intrinsic_apis//intrinsic/skills/proto:skills_cc_proto",
    ],
)

skill_manifest(
    name = "move_gripper_joints_manifest_cc",
    src = "move_gripper_joints_manifest_cc.textproto",
    incompatible_disallow_manifest_dependencies = False,
    visibility = [
        "//intrinsic:public_asset_users",
        "//intrinsic:release_tooling",
    ],
    deps = [
        ":move_gripper_joints_proto",
    ],
)

cc_skill(
    name = "move_gripper_joints_skill",
    manifest = ":move_gripper_joints_manifest_cc",
    visibility = [
        "//intrinsic:public_asset_users",
        "//intrinsic:release_tooling",
    ],
    deps = [
        ":move_gripper_joints_cc",
        ":move_gripper_joints_cc_proto",
    ],
)

cc_library(
    name = "move_gripper_joints_cc",
    srcs = ["move_gripper_joints.cc"],
    hdrs = ["move_gripper_joints.h"],
    deps = [
        ":move_gripper_joints_cc_proto",
        "//intrinsic_control/intrinsic/icon/equipment:channel_factory",
        "//intrinsic_control/intrinsic/icon/equipment:equipment_utils",
        "//intrinsic_control/intrinsic/icon/proto:joint_trajectory_conversion",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_kinematics/intrinsic/kinematics:group",
        "//intrinsic_kinematics/intrinsic/kinematics/types:cartesian_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:check_joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits_xd",
        "//intrinsic_motion_planning/intrinsic/motion_planning:trajectory_execution_utils",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning:path_segment",
        "//intrinsic_motion_planning/intrinsic/motion_planning/proto/v1:motion_planner_service_cc_proto",
        "//intrinsic_motion_planning/intrinsic/motion_planning/service:motion_planner_service_asset_utils",
        "//intrinsic_motion_planning/intrinsic/motion_planning/trajectory_planning/path_refinement:spline_based_path_refinement",
        "//intrinsic_motion_planning/intrinsic/motion_planning/trajectory_planning/topp:refine_path_and_compute_acceleration_limited_trajectory",
        "//intrinsic_motion_planning/intrinsic/motion_planning/trajectory_planning/topp:topp_options",
        "//intrinsic_motion_planning/intrinsic/motion_planning/trajectory_planning/topp:topp_trajectory_result",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/motion_planning:motion_planner_client",
        "//intrinsic_sdk/intrinsic/skills/cc:equipment_pack",
        "//intrinsic_sdk/intrinsic/skills/cc:skill_interface",
        "//intrinsic_sdk/intrinsic/skills/cc:skill_utils",
        "//intrinsic_sdk/intrinsic/skills/proto:skill_service_cc_proto",
        "//intrinsic_sdk/intrinsic/util:eigen",
        "//intrinsic_sdk/intrinsic/util/status:get_extended_status",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world/objects:kinematic_object",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_client",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
        "@com_gitlab_libeigen_eigen//:eigen",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/icon/proto:joint_space_cc_proto",
        "@intrinsic_apis//intrinsic/skills/proto:footprint_cc_proto",
        "@intrinsic_apis//intrinsic/skills/proto:skills_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:collision_settings_cc_proto",
    ],
)

proto_library(
    name = "set_payload_proto",
    srcs = ["set_payload.proto"],
    visibility = ["//intrinsic:public_asset_users"],
    deps = [
        "@intrinsic_apis//intrinsic/world/proto:robot_payload_proto",
    ],
)

proto_library(
    name = "move_gripper_joints_proto",
    srcs = ["move_gripper_joints.proto"],
    visibility = ["//visibility:public"],
    deps = [
        "@intrinsic_apis//intrinsic/assets/proto:field_metadata_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:resolved_dependency_proto",
        "@intrinsic_apis//intrinsic/skills/proto:skills_proto",
        "@intrinsic_apis//intrinsic/world/proto:collision_settings_proto",
        "@intrinsic_apis//intrinsic/world/proto:object_world_refs_proto",
    ],
)

cc_proto_library(
    name = "move_gripper_joints_cc_proto",
    deps = [
        ":move_gripper_joints_proto",
    ],
)

go_proto_library(
    name = "move_gripper_joints_go_proto",
    importpath = "intrinsic/icon/skills/move_gripper_joints_go_proto",
    protos = [":move_gripper_joints_proto"],
    deps = [
        "@intrinsic_apis//intrinsic/assets/proto:field_metadata_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:resolved_dependency_go_proto",
        "@intrinsic_apis//intrinsic/skills/proto:skills_go_proto",
        "@intrinsic_apis//intrinsic/world/proto:collision_settings_go_proto",
        "@intrinsic_apis//intrinsic/world/proto:object_world_refs_go_proto",
    ],
)

go_proto_library(
    name = "set_payload_go_proto",
    importpath = "intrinsic/icon/skills/set_payload_go_proto",
    protos = [":set_payload_proto"],
    deps = [
        "@intrinsic_apis//intrinsic/world/proto:robot_payload_go_proto",
    ],
)

cc_proto_library(
    name = "set_payload_cc_proto",
    deps = [
        ":set_payload_proto",
    ],
)

skill_manifest(
    name = "set_payload_manifest",
    src = "set_payload_manifest.textproto",
    incompatible_disallow_manifest_dependencies = False,
    visibility = [
        "//intrinsic:public_asset_users",
        "//intrinsic:release_tooling",
    ],
    deps = [
        ":set_payload_proto",
    ],
)

cc_library(
    name = "set_payload",
    srcs = ["set_payload.cc"],
    hdrs = ["set_payload.h"],
    deps = [
        ":set_payload_cc_proto",
        "//intrinsic/skills/internal:kinematic_object_for_position_part",
        "//intrinsic_control/intrinsic/icon/cc_client:client",
        "//intrinsic_control/intrinsic/icon/equipment:channel_factory",
        "//intrinsic_control/intrinsic/icon/equipment:equipment_utils",
        "//intrinsic_sdk/intrinsic/skills/cc:equipment_pack",
        "//intrinsic_sdk/intrinsic/skills/cc:skill_interface",
        "//intrinsic_sdk/intrinsic/skills/cc:skill_utils",
        "//intrinsic_sdk/intrinsic/skills/proto:skill_service_cc_proto",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world/objects:kinematic_object",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_client",
        "//intrinsic_sdk/intrinsic/world/robot_payload",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/math/proto:point_cc_proto",
        "@intrinsic_apis//intrinsic/resources/proto:resource_handle_cc_proto",
        "@intrinsic_apis//intrinsic/skills/proto:equipment_cc_proto",
        "@intrinsic_apis//intrinsic/skills/proto:footprint_cc_proto",
    ],
)

cc_skill(
    name = "set_payload_skill",
    manifest = ":set_payload_manifest",
    visibility = [
        "//intrinsic:public_asset_users",
        "//intrinsic:release_tooling",
    ],
    deps = [
        ":set_payload",
        ":set_payload_cc_proto",
    ],
)
