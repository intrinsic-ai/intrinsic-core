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
    name = "compute_ik_util",
    srcs = ["compute_ik_util.cc"],
    hdrs = ["compute_ik_util.h"],
    deps = [
        "//intrinsic/math/numopt:constraint_interface",
        "//intrinsic/math/numopt:costfunction_interface",
        "//intrinsic/stats:scoped_span",
        "//intrinsic/world",
        "//intrinsic/world:kinematic_world_views",
        "//intrinsic/world/collision:collision_checker",
        "//intrinsic/world/collision:collision_types",
        "//intrinsic/world/objects:object_world",
        "//intrinsic/world/objects:object_world_proto_utils",
        "//intrinsic/world/util:entity_search_util",
        "//intrinsic_control/intrinsic/icon/control/parts:feature_interfaces",
        "//intrinsic_control/intrinsic/icon/proto:eigen_conversion",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_kinematics/intrinsic/kinematics",
        "//intrinsic_kinematics/intrinsic/kinematics:group",
        "//intrinsic_kinematics/intrinsic/kinematics:model_interface",
        "//intrinsic_kinematics/intrinsic/kinematics/ik:inverse_kinematics_interface",
        "//intrinsic_kinematics/intrinsic/kinematics/ik/constrained:constrained_ik_cc_proto",
        "//intrinsic_kinematics/intrinsic/kinematics/ik/constrained:constrained_ik_interface",
        "//intrinsic_kinematics/intrinsic/kinematics/ik/constrained:cost_functions",
        "//intrinsic_kinematics/intrinsic/kinematics/ik/constrained:nlopt_constrained_ik_solver",
        "//intrinsic_kinematics/intrinsic/kinematics/ik_solvers/kinematic_chain:kinematic_chain_ik_solver",
        "//intrinsic_kinematics/intrinsic/kinematics/ik_solvers/kinematic_chain:kinematic_chain_newton_raphson_ik_solver",
        "//intrinsic_kinematics/intrinsic/kinematics/types:check_joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits_xd",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_state",
        "//intrinsic_motion_planning/intrinsic/motion_planning/data_types/constraints/joint_position:joint_position_limits",
        "//intrinsic_motion_planning/intrinsic/motion_planning/data_types/constraints/utils:geometric_constraints_proto_utils",
        "//intrinsic_motion_planning/intrinsic/motion_planning/motion_planner:motion_planning_error_utils",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning:kinematics_system_proxy",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning:path_planning_utils",
        "//intrinsic_motion_planning/intrinsic/motion_planning/proto:motion_specification_proto_utils",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:proto_conversion",
        "//intrinsic_sdk/intrinsic/util:aggregate_type",
        "//intrinsic_sdk/intrinsic/util:eigen",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world:entity_id",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_client",
        "//intrinsic_sdk/intrinsic/world/objects:transform_node",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/time",
        "@intrinsic_apis//intrinsic/icon/proto:joint_space_cc_proto",
        "@intrinsic_apis//intrinsic/math/proto:pose_cc_proto",
        "@intrinsic_apis//intrinsic/motion_planning/proto:motion_target_cc_proto",
        "@intrinsic_apis//intrinsic/motion_planning/proto/v1:geometric_constraints_cc_proto",
        "@intrinsic_apis//intrinsic/motion_planning/proto/v1:motion_planning_error_cc_proto",
        "@intrinsic_apis//intrinsic/skills/proto:motion_targets_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:collision_settings_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:object_world_refs_cc_proto",
    ],
)
