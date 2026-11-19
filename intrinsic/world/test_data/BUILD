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

load("//bazel:xacro.bzl", "xacro_file")
load("//intrinsic/world/build_def:sdf_world.bzl", "sdf_world")

package(default_visibility = ["//visibility:public"])

xacro_file(
    name = "4_robots_cube_world_base_xacro",
    testonly = True,
    src = "4_robots_cube_world_base.sdf.xacro",
    out = "4_robots_cube_world_base.sdf",
    data = ["//intrinsic_control/intrinsic/models/robot_definitions/kuka:kr6_r900_2"],
)

sdf_world(
    name = "4_robots_cube_world",
    testonly = True,
    src = ":4_robots_cube_world_base.sdf",
    generate_group_ids = True,  # TODO(b/283374158): Set to False or remove
    model_resource_names = {
        "agilus-03": "agilus-03",
        "agilus-04": "agilus-04",
        "iris622": "iris622",
        "iris692": "iris692",
    },
    sdf_assets = [
        "//intrinsic/models/assets/11140_agilus_cell",
        "//intrinsic/models/assets/adjustable_tool",
        "//intrinsic/models/assets/quad_table",
        "//intrinsic/models/assets/table_cube",
        "//intrinsic/models/assets/universal_plate",
        "//intrinsic_simulation/models/gazebo:base_models",
        "//third_party/kuka/kr6_r900_2:kr6_r900_2_resources",
    ],
    updates_pbtxts = ["4_robots_cube_world_updates.pbtxt"],
)

sdf_world(
    name = "4_robots_cube_object_world",
    testonly = True,
    src = ":4_robots_cube_world_base.sdf",
    generate_group_ids = True,  # TODO(b/283374158): Set to False or remove
    model_resource_names = {
        "agilus-03": "agilus_03",
        "agilus-04": "agilus_04",
        "iris622": "iris622",
        "iris692": "iris692",
    },
    sdf_assets = [
        "//intrinsic/models/assets/11140_agilus_cell",
        "//intrinsic/models/assets/adjustable_tool",
        "//intrinsic/models/assets/quad_table",
        "//intrinsic/models/assets/table_cube",
        "//intrinsic/models/assets/universal_plate",
        "//intrinsic_simulation/models/gazebo:base_models",
        "//third_party/kuka/kr6_r900_2:kr6_r900_2_resources",
    ],
    updates_pbtxts = [
        "4_robots_cube_world_updates.pbtxt",
        "4_robots_cube_object_world_updates.pbtxt",
    ],
)
