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
    name = "motion_planner_service_proto_utils",
    srcs = ["motion_planner_service_proto_utils.cc"],
    hdrs = ["motion_planner_service_proto_utils.h"],
    deps = [
        "//intrinsic/world/objects:object_world",
        "//intrinsic/world/objects:object_world_proto_utils",
        "//intrinsic_motion_planning/intrinsic/motion_planning/proto/v1:motion_planner_service_cc_proto",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:string_view",
        "@intrinsic_apis//intrinsic/motion_planning/proto/v1:robot_specification_cc_proto",
    ],
)

cc_library(
    name = "motion_specification_proto_utils",
    srcs = ["motion_specification_proto_utils.cc"],
    hdrs = ["motion_specification_proto_utils.h"],
    deps = [
        "//intrinsic/math/numopt:constraint_interface",
        "//intrinsic/world/objects:object_world",
        "//intrinsic/world/objects:object_world_proto_utils",
        "//intrinsic_control/intrinsic/icon/proto:eigen_conversion",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_kinematics/intrinsic/kinematics/types:cartesian_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_motion_planning/intrinsic/motion_planning/data_types/constraints/joint_position:joint_position_limits",
        "//intrinsic_motion_planning/intrinsic/motion_planning/data_types/constraints/joint_position:joint_position_sum_limit",
        "//intrinsic_motion_planning/intrinsic/motion_planning/data_types/constraints/joint_position:point_at",
        "//intrinsic_motion_planning/intrinsic/motion_planning/data_types/constraints/joint_position:pose_equality",
        "//intrinsic_motion_planning/intrinsic/motion_planning/data_types/constraints/joint_position:position_bounding_box",
        "//intrinsic_motion_planning/intrinsic/motion_planning/data_types/constraints/joint_position:position_equality",
        "//intrinsic_motion_planning/intrinsic/motion_planning/data_types/constraints/joint_position:rotation_cone",
        "//intrinsic_motion_planning/intrinsic/motion_planning/data_types/constraints/joint_position:rotation_equality",
        "//intrinsic_motion_planning/intrinsic/motion_planning/proto/v1:motion_specification_cc_proto",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:proto_conversion",
        "//intrinsic_sdk/intrinsic/util:eigen",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world:entity_id",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@intrinsic_apis//intrinsic/motion_planning/proto/v1:geometric_constraints_cc_proto",
        "@intrinsic_apis//intrinsic/motion_planning/proto/v1:motion_planning_limits_cc_proto",
    ],
)
