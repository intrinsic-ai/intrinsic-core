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
load("@io_bazel_rules_go//proto:def.bzl", "go_proto_library")
load("//bazel:cc_macros.bzl", "cc_library")
load("//bazel:protos.bzl", "proto_library")
load("//intrinsic/skills/build_defs:skill.bzl", "cc_skill", "skill_manifest")

package(default_visibility = [
    "//visibility:public",
])

cc_library(
    name = "calibrate_camera_to_robot",
    srcs = ["calibrate_camera_to_robot.cc"],
    hdrs = ["calibrate_camera_to_robot.h"],
    deps = [
        ":calibrate_camera_to_robot_cc_proto",
        "//intrinsic/assets/dependencies:utils_cc",
        "//intrinsic/util/status:status_conversion_grpc",
        "//intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/connect/cc/grpc:channel",
        "//intrinsic_sdk/intrinsic/skills/cc:skill_interface",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/perception/proto/v1:calibration_service_cc_grpc_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:calibration_service_cc_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_setup_cc_proto",
        "@intrinsic_apis//intrinsic/skills/proto:footprint_cc_proto",
    ],
)

proto_library(
    name = "calibrate_camera_to_robot_proto",
    srcs = ["calibrate_camera_to_robot.proto"],
    deps = [
        "@intrinsic_apis//intrinsic/assets/proto:field_metadata_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:resolved_dependency_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_setup_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_to_robot_calibration",
    ],
)

cc_proto_library(
    name = "calibrate_camera_to_robot_cc_proto",
    deps = [":calibrate_camera_to_robot_proto"],
)

py_proto_library(
    name = "calibrate_camera_to_robot_py_pb2",
    deps = [":calibrate_camera_to_robot_proto"],
)

skill_manifest(
    name = "calibrate_camera_to_robot_manifest",
    src = "calibrate_camera_to_robot_manifest.textproto",
    deps = [
        ":calibrate_camera_to_robot_proto",
        "@com_google_protobuf//:empty_proto",
    ],
)

cc_skill(
    name = "calibrate_camera_to_robot_skill",
    manifest = ":calibrate_camera_to_robot_manifest",
    deps = [
        ":calibrate_camera_to_robot",
        ":calibrate_camera_to_robot_cc_proto",
    ],
)

cc_library(
    name = "calibrate_intrinsic",
    srcs = ["calibrate_intrinsic.cc"],
    hdrs = ["calibrate_intrinsic.h"],
    deps = [
        ":calibrate_intrinsic_cc_proto",
        "//intrinsic/assets/dependencies:utils_cc",
        "//intrinsic/util/status:status_conversion_grpc",
        "//intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/connect/cc/grpc:channel",
        "//intrinsic_sdk/intrinsic/skills/cc:skill_interface",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/perception/proto/v1:calibration_service_cc_grpc_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:calibration_service_cc_proto",
        "@intrinsic_apis//intrinsic/skills/proto:footprint_cc_proto",
    ],
)

proto_library(
    name = "calibrate_intrinsic_proto",
    srcs = ["calibrate_intrinsic.proto"],
    deps = [
        "@intrinsic_apis//intrinsic/assets/proto:field_metadata_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:resolved_dependency_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:calibration_service_proto",
    ],
)

cc_proto_library(
    name = "calibrate_intrinsic_cc_proto",
    deps = [":calibrate_intrinsic_proto"],
)

py_proto_library(
    name = "calibrate_intrinsic_py_pb2",
    deps = [":calibrate_intrinsic_proto"],
)

skill_manifest(
    name = "calibrate_intrinsic_manifest",
    src = "calibrate_intrinsic_manifest.textproto",
    deps = [":calibrate_intrinsic_proto"],
)

cc_skill(
    name = "calibrate_intrinsic_skill",
    manifest = ":calibrate_intrinsic_manifest",
    deps = [
        ":calibrate_intrinsic",
        ":calibrate_intrinsic_cc_proto",
    ],
)

cc_library(
    name = "calibration_robot_motion_utils",
    srcs = ["calibration_robot_motion_utils.cc"],
    hdrs = ["calibration_robot_motion_utils.h"],
    deps = [
        ":collect_calibration_data_cc_proto",
        "//intrinsic/util/grpc:channel_interface",
        "//intrinsic/util/status:status_macros",
        "//intrinsic_control/intrinsic/icon/cc_client:client",
        "//intrinsic_control/intrinsic/icon/proto:cart_space_conversion",
        "//intrinsic_control/intrinsic/icon/skills/util:position_part_util",
        "//intrinsic_control/intrinsic/icon/utils:arm_utils",
        "//intrinsic_motion_planning/intrinsic/motion_planning:trajectory_execution_utils",
        "//intrinsic_motion_planning/intrinsic/motion_planning/skills:motion_planning_util",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:proto_conversion",
        "//intrinsic_sdk/intrinsic/motion_planning:motion_planner_client",
        "//intrinsic_sdk/intrinsic/skills/cc:skill_interface",
        "//intrinsic_sdk/intrinsic/world/objects:kinematic_object",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_client",
        "//intrinsic_sdk/intrinsic/world/objects:world_object",
        "//intrinsic_sdk/intrinsic/world/util:object_reference_utils",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/icon/proto:part_status_cc_proto",
        "@intrinsic_apis//intrinsic/logging/proto:context_cc_proto",
        "@intrinsic_apis//intrinsic/motion_planning/proto/v1:geometric_constraints_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:object_world_refs_cc_proto",
    ],
)

cc_library(
    name = "collect_calibration_data",
    srcs = ["collect_calibration_data.cc"],
    hdrs = ["collect_calibration_data.h"],
    deps = [
        ":calibration_robot_motion_utils",
        ":collect_calibration_data_cc_proto",
        "//intrinsic/assets/dependencies:utils_cc",
        "//intrinsic/platform/pubsub",
        "//intrinsic/util:eigen",
        "//intrinsic/util/grpc:channel_interface",
        "//intrinsic/util/status:status_conversion_grpc",
        "//intrinsic/util/status:status_macros",
        "//intrinsic_control/intrinsic/icon/cc_client:client",
        "//intrinsic_control/intrinsic/icon/equipment:channel_factory",
        "//intrinsic_control/intrinsic/icon/equipment:equipment_utils",
        "//intrinsic_control/intrinsic/icon/proto:cart_space_conversion",
        "//intrinsic_control/intrinsic/icon/skills/util:position_part_util",
        "//intrinsic_control/intrinsic/icon/utils:arm_utils",
        "//intrinsic_motion_planning/intrinsic/motion_planning:trajectory_execution_utils",
        "//intrinsic_motion_planning/intrinsic/motion_planning/service:motion_planner_service_asset_utils",
        "//intrinsic_motion_planning/intrinsic/motion_planning/skills:motion_planning_util",
        "//intrinsic_sdk/intrinsic/connect/cc/grpc:channel",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:proto_conversion",
        "//intrinsic_sdk/intrinsic/motion_planning:motion_planner_client",
        "//intrinsic_sdk/intrinsic/skills/cc:equipment_pack",
        "//intrinsic_sdk/intrinsic/skills/cc:skill_interface",
        "//intrinsic_sdk/intrinsic/skills/cc:skill_utils",
        "//intrinsic_sdk/intrinsic/world/objects:frame",
        "//intrinsic_sdk/intrinsic/world/objects:kinematic_object",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_client",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_ids",
        "//intrinsic_sdk/intrinsic/world/objects:world_object",
        "//intrinsic_sdk/intrinsic/world/util:object_reference_utils",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/assets/proto/v1:resolved_dependency_cc_proto",
        "@intrinsic_apis//intrinsic/icon/proto:part_status_cc_proto",
        "@intrinsic_apis//intrinsic/logging/proto:context_cc_proto",
        "@intrinsic_apis//intrinsic/motion_planning/proto/v1:geometric_constraints_cc_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:calibration_service_cc_grpc_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:calibration_service_cc_proto",
        "@intrinsic_apis//intrinsic/resources/proto:resource_handle_cc_proto",
        "@intrinsic_apis//intrinsic/skills/proto:footprint_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:collision_settings_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:object_world_refs_cc_proto",
    ],
)

proto_library(
    name = "collect_calibration_data_proto",
    srcs = ["collect_calibration_data.proto"],
    deps = [
        "@intrinsic_apis//intrinsic/assets/proto:field_metadata_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:resolved_dependency_proto",
        "@intrinsic_apis//intrinsic/motion_planning/proto/v1:geometric_constraints_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_setup_proto",
        "@intrinsic_apis//intrinsic/skills/proto:skill_parameter_metadata_proto",
        "@intrinsic_apis//intrinsic/world/proto:object_world_refs_proto",
    ],
)

cc_proto_library(
    name = "collect_calibration_data_cc_proto",
    deps = [":collect_calibration_data_proto"],
)

py_proto_library(
    name = "collect_calibration_data_py_pb2",
    deps = [":collect_calibration_data_proto"],
)

skill_manifest(
    name = "collect_calibration_data_manifest",
    src = "collect_calibration_data_manifest.textproto",
    incompatible_disallow_manifest_dependencies = False,
    deps = [":collect_calibration_data_proto"],
)

cc_skill(
    name = "collect_calibration_data_skill",
    manifest = ":collect_calibration_data_manifest",
    deps = [
        ":collect_calibration_data",
        ":collect_calibration_data_cc_proto",
    ],
)

cc_library(
    name = "initialize_calibration",
    srcs = ["initialize_calibration.cc"],
    hdrs = ["initialize_calibration.h"],
    deps = [
        ":initialize_calibration_cc_proto",
        "//intrinsic/assets/dependencies:utils_cc",
        "//intrinsic/util/grpc:connection_params",
        "//intrinsic/util/status:status_conversion_grpc",
        "//intrinsic/util/status:status_macros",
        "//intrinsic_control/intrinsic/icon/equipment:equipment_utils",
        "//intrinsic_control/intrinsic/icon/skills/util:position_part_util",
        "//intrinsic_perception/intrinsic/perception:asset_utils",
        "//intrinsic_perception/intrinsic/perception:pose_estimator_id_utils",
        "//intrinsic_perception/intrinsic/perception/cameras:camera",
        "//intrinsic_sdk/intrinsic/connect/cc/grpc:channel",
        "//intrinsic_sdk/intrinsic/skills/cc:equipment_pack",
        "//intrinsic_sdk/intrinsic/skills/cc:skill_interface",
        "//intrinsic_sdk/intrinsic/skills/cc:skill_utils",
        "//intrinsic_sdk/intrinsic/world/util:object_reference_utils",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/assets/data/proto/v1:data_assets_cc_grpc_proto",
        "@intrinsic_apis//intrinsic/assets/data/proto/v1:data_assets_cc_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:resolved_dependency_cc_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:calibration_service_cc_grpc_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:calibration_service_cc_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_config_cc_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:pattern_detection_config_cc_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:perception_model_cc_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:pose_estimation_config_cc_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:pose_estimator_id_cc_proto",
        "@intrinsic_apis//intrinsic/resources/proto:resource_handle_cc_proto",
        "@intrinsic_apis//intrinsic/skills/proto:footprint_cc_proto",
    ],
)

proto_library(
    name = "initialize_calibration_proto",
    srcs = ["initialize_calibration.proto"],
    deps = [
        "@intrinsic_apis//intrinsic/assets/proto:field_metadata_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:resolved_dependency_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:pattern_detection_config_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:pose_estimator_id_proto",
        "@intrinsic_apis//intrinsic/skills/proto:skill_parameter_metadata_proto",
        "@intrinsic_apis//intrinsic/world/proto:object_world_refs_proto",
    ],
)

cc_proto_library(
    name = "initialize_calibration_cc_proto",
    deps = [":initialize_calibration_proto"],
)

py_proto_library(
    name = "initialize_calibration_py_pb2",
    deps = [":initialize_calibration_proto"],
)

skill_manifest(
    name = "initialize_calibration_manifest",
    src = "initialize_calibration_manifest.textproto",
    incompatible_disallow_manifest_dependencies = False,
    deps = [":initialize_calibration_proto"],
)

cc_skill(
    name = "initialize_calibration_skill",
    manifest = ":initialize_calibration_manifest",
    deps = [
        ":initialize_calibration",
        ":initialize_calibration_cc_proto",
    ],
)

cc_library(
    name = "sample_calibration_poses",
    srcs = ["sample_calibration_poses.cc"],
    hdrs = ["sample_calibration_poses.h"],
    deps = [
        ":sample_calibration_poses_cc_proto",
        "//intrinsic/util:eigen",
        "//intrinsic/util/status:status_macros",
        "//intrinsic/util/thread",
        "//intrinsic_control/intrinsic/icon/cc_client:client",
        "//intrinsic_control/intrinsic/icon/equipment:channel_factory",
        "//intrinsic_control/intrinsic/icon/equipment:equipment_utils",
        "//intrinsic_control/intrinsic/icon/proto:cart_space_conversion",
        "//intrinsic_control/intrinsic/icon/proto:eigen_conversion",
        "//intrinsic_control/intrinsic/icon/skills/util:position_part_util",
        "//intrinsic_control/intrinsic/icon/utils:arm_utils",
        "//intrinsic_kinematics/intrinsic/kinematics/types:cartesian_limits",
        "//intrinsic_motion_planning/intrinsic/motion_planning/proto/v1:motion_specification_cc_proto",
        "//intrinsic_motion_planning/intrinsic/motion_planning/service:motion_planner_service_asset_utils",
        "//intrinsic_motion_planning/intrinsic/motion_planning/skills:motion_planning_util",
        "//intrinsic_perception/intrinsic/perception/calibration:pose_sampling",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:proto_conversion",
        "//intrinsic_sdk/intrinsic/math:units",
        "//intrinsic_sdk/intrinsic/motion_planning:motion_planner_client",
        "//intrinsic_sdk/intrinsic/skills/cc:equipment_pack",
        "//intrinsic_sdk/intrinsic/skills/cc:skill_interface",
        "//intrinsic_sdk/intrinsic/skills/cc:skill_utils",
        "//intrinsic_sdk/intrinsic/util/thread:sysinfo",
        "//intrinsic_sdk/intrinsic/world/objects:frame",
        "//intrinsic_sdk/intrinsic/world/objects:kinematic_object",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_client",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_ids",
        "//intrinsic_sdk/intrinsic/world/objects:transform_node",
        "//intrinsic_sdk/intrinsic/world/objects:world_object",
        "//intrinsic_sdk/intrinsic/world/util:object_reference_utils",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/random",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/synchronization",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/assets/proto/v1:resolved_dependency_cc_proto",
        "@intrinsic_apis//intrinsic/icon/proto:part_status_cc_proto",
        "@intrinsic_apis//intrinsic/motion_planning/proto/v1:geometric_constraints_cc_proto",
        "@intrinsic_apis//intrinsic/motion_planning/proto/v1:motion_planning_error_cc_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_setup_cc_proto",
        "@intrinsic_apis//intrinsic/resources/proto:resource_handle_cc_proto",
        "@intrinsic_apis//intrinsic/skills/proto:footprint_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:collision_settings_cc_proto",
    ],
)

proto_library(
    name = "sample_calibration_poses_proto",
    srcs = ["sample_calibration_poses.proto"],
    deps = [
        "@intrinsic_apis//intrinsic/assets/proto:field_metadata_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:resolved_dependency_proto",
        "@intrinsic_apis//intrinsic/motion_planning/proto/v1:geometric_constraints_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_setup_proto",
        "@intrinsic_apis//intrinsic/skills/proto:skills_proto",
        "@intrinsic_apis//intrinsic/world/proto:object_world_refs_proto",
    ],
)

cc_proto_library(
    name = "sample_calibration_poses_cc_proto",
    deps = [":sample_calibration_poses_proto"],
)

py_proto_library(
    name = "sample_calibration_poses_py_pb2",
    deps = [":sample_calibration_poses_proto"],
)

go_proto_library(
    name = "sample_calibration_poses_go_proto",
    importpath = "intrinsic/perception/skills/calibration/sample_calibration_poses_go_proto",
    proto = ":sample_calibration_poses_proto",
    deps = [
        "@intrinsic_apis//intrinsic/assets/proto:field_metadata_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:resolved_dependency_go_proto",
        "@intrinsic_apis//intrinsic/motion_planning/proto/v1:geometric_constraints_go_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_setup_go_proto",
        "@intrinsic_apis//intrinsic/skills/proto:skills_go_proto",
        "@intrinsic_apis//intrinsic/world/proto:object_world_refs_go_proto",
    ],
)

skill_manifest(
    name = "sample_calibration_poses_manifest",
    src = "sample_calibration_poses_manifest.textproto",
    incompatible_disallow_manifest_dependencies = False,
    deps = [":sample_calibration_poses_proto"],
)

cc_skill(
    name = "sample_calibration_poses_skill",
    manifest = ":sample_calibration_poses_manifest",
    deps = [
        ":sample_calibration_poses",
        ":sample_calibration_poses_cc_proto",
    ],
)

cc_library(
    name = "save_calibration_result",
    srcs = ["save_calibration_result.cc"],
    hdrs = ["save_calibration_result.h"],
    deps = [
        ":save_calibration_result_cc_proto",
        "//intrinsic/assets/dependencies:utils_cc",
        "//intrinsic/util/status:status_conversion_grpc",
        "//intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/connect/cc/grpc:channel",
        "//intrinsic_sdk/intrinsic/skills/cc:skill_interface",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status:statusor",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/assets/proto/v1:resolved_dependency_cc_proto",
        "@intrinsic_apis//intrinsic/geometry/proto:oriented_bounding_box_cc_proto",
        "@intrinsic_apis//intrinsic/logging/proto:context_cc_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:calibration_service_cc_grpc_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:calibration_service_cc_proto",
        "@intrinsic_apis//intrinsic/skills/proto:footprint_cc_proto",
    ],
)

proto_library(
    name = "save_calibration_result_proto",
    srcs = ["save_calibration_result.proto"],
    deps = [
        "@intrinsic_apis//intrinsic/assets/proto:field_metadata_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:resolved_dependency_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:calibration_service_proto",
    ],
)

cc_proto_library(
    name = "save_calibration_result_cc_proto",
    deps = [":save_calibration_result_proto"],
)

py_proto_library(
    name = "save_calibration_result_py_pb2",
    deps = [":save_calibration_result_proto"],
)

skill_manifest(
    name = "save_calibration_result_manifest",
    src = "save_calibration_result_manifest.textproto",
    deps = [":save_calibration_result_proto"],
)

cc_skill(
    name = "save_calibration_result_skill",
    manifest = ":save_calibration_result_manifest",
    deps = [
        ":save_calibration_result",
        ":save_calibration_result_cc_proto",
    ],
)

go_proto_library(
    name = "save_calibration_result_go_proto",
    importpath = "intrinsic/perception/skills/calibration/save_calibration_result_go_proto",
    proto = ":save_calibration_result_proto",
    deps = [
        "@intrinsic_apis//intrinsic/assets/proto:field_metadata_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:resolved_dependency_go_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:calibration_service_go_proto",
    ],
)

go_proto_library(
    name = "calibrate_camera_to_robot_go_proto",
    importpath = "intrinsic/perception/skills/calibration/calibrate_camera_to_robot_go_proto",
    proto = ":calibrate_camera_to_robot_proto",
    deps = [
        "@intrinsic_apis//intrinsic/assets/proto:field_metadata_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:resolved_dependency_go_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_setup_go_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_to_robot_calibration_go_proto",
    ],
)

go_proto_library(
    name = "calibrate_intrinsic_go_proto",
    importpath = "intrinsic/perception/skills/calibration/calibrate_intrinsic_go_proto",
    proto = ":calibrate_intrinsic_proto",
    deps = [
        "@intrinsic_apis//intrinsic/assets/proto:field_metadata_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:resolved_dependency_go_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:calibration_service_go_proto",
    ],
)

go_proto_library(
    name = "collect_calibration_data_go_proto",
    importpath = "intrinsic/perception/skills/calibration/collect_calibration_data_go_proto",
    proto = ":collect_calibration_data_proto",
    deps = [
        "@intrinsic_apis//intrinsic/assets/proto:field_metadata_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:resolved_dependency_go_proto",
        "@intrinsic_apis//intrinsic/motion_planning/proto/v1:geometric_constraints_go_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_setup_go_proto",
        "@intrinsic_apis//intrinsic/skills/proto:skill_parameter_metadata_go_proto",
        "@intrinsic_apis//intrinsic/world/proto:object_world_refs_go_proto",
    ],
)

go_proto_library(
    name = "initialize_calibration_go_proto",
    importpath = "intrinsic/perception/skills/calibration/initialize_calibration_go_proto",
    proto = ":initialize_calibration_proto",
    deps = [
        "@intrinsic_apis//intrinsic/assets/proto:field_metadata_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:resolved_dependency_go_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:pattern_detection_config_go_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:pose_estimator_id_go_proto",
        "@intrinsic_apis//intrinsic/skills/proto:skill_parameter_metadata_go_proto",
        "@intrinsic_apis//intrinsic/world/proto:object_world_refs_go_proto",
    ],
)
