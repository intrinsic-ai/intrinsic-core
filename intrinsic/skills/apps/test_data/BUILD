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

# Test data for skill implementations.
load("//intrinsic/world/build_def:sdf_world.bzl", "sdf_world")

package(default_visibility = [
    "//visibility:public",
])

# Allowlist for `ur5e_world`. Do not use `ur5e_world` in new tests. This world
# overrides `ur5e_base.xacro` parameters (`shoulder_pan_yaw=0`, `wrist_3_pitch=0`,
# `include_ft_sensor=false`, `include_power_cable_visual=false`) to emulate a
# non-Intrinsic-default URDF structure for backward compatibility with existing
# tests.
# New tests should use the canonical UR5e definitions in
# `//intrinsic_control/intrinsic/models/robot_definitions/ur` (such as
# `:ur5e_scene_object`, `:example_ur5e_world_with_calibrated_kinematics`, or
# `<xacro:ur5e_sdf>` / `<xacro:ur5e_base_sdf>` from `:ur5e` with default
# parameters).
# Contact sesc@ if you would like to extend this allowlist.
package_group(
    name = "non_intrinsic_default_ur5e_world_users",
    packages = [
        "//intrinsic/manipulation/service",
        "//intrinsic/manipulation/skills",
        "//intrinsic/manipulation/skills/force",
        "//intrinsic/perception/skills",
        "//intrinsic/perception/skills/calibration",
        "//intrinsic/perception/skills/multi_view/single_camera",
        "//intrinsic/skills/apps",
        "//intrinsic/skills/apps/icon2",
        "//intrinsic/skills/apps/testing",
        "//intrinsic/skills/examples",
        "//intrinsic/world",
        "//intrinsic_control/intrinsic/icon/skills",
        "//intrinsic_kinematics/intrinsic/kinematics/utils",
        "//intrinsic_motion_planning/intrinsic/motion_planning/motion_planner",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning/path_ik",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning/planners",
        "//intrinsic_perception/intrinsic/perception/client/v1/python/camera",
        "//intrinsic_perception/intrinsic/perception/skills/calibration",
        "//intrinsic_perception/intrinsic/perception/skills/multi_view",
        "//intrinsic_runtime/intrinsic/world/service/robot_calibration",
        "//incode/ml/rl/skills",
    ],
)

sdf_world(
    name = "ur5e_world",
    testonly = True,
    src = "ur5e_non_intrinsic_default.sdf.xacro",
    generate_group_ids = True,  # TODO(b/283374158): Set to False or remove
    model_resource_names = {
        "my_robot": "my_robot",
    },
    sdf_assets = ["//third_party/universal_robots:ur5e"],
    updates_pbtxts = ["ur5e_world_updates.pbtxt"],
    visibility = [":non_intrinsic_default_ur5e_world_users"],
    xacro_data = ["//intrinsic_control/intrinsic/models/robot_definitions/ur:ur5e"],
)

# Allowlist for `ur5_world`. Do not use `ur5_world` in new tests. This world uses
# the `CB3 Series — Legacy` `//intrinsic/robot_definitions/ur:legacy_ur5`
# model, which is not officially supported in real workcells and is not part of
# the Intrinsic Core open-source release. New tests should use the canonical
# e-Series definitions in
# `//intrinsic_control/intrinsic/models/robot_definitions/ur`.
# Contact sesc@ if you would like to extend this allowlist.
package_group(
    name = "legacy_ur5_world_users",
    packages = [
        "//intrinsic_motion_planning/intrinsic/motion_planning/motion_planner",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning/path_ik",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning/planners",
    ],
)
