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
load("//intrinsic_sdk/intrinsic/scene/build_defs:sdf_scene_object.bzl", "sdf_scene_object")

filegroup(
    name = "plugins",
    srcs = [
        "simulation_plugins.xacro",
    ],
)

filegroup(
    name = "ur3e",
    srcs = [
        "load_kinematic_parameters.xacro",
        "ur3e/ur3e.xacro",
        "ur3e/ur3e_base.xacro",
        ":plugins",
    ],
)

xacro_file(
    name = "ur3e_xacro",
    src = "ur3e/ur3e.sdf.xacro",
    out = "ur3e_world.sdf",
    data = [":ur3e"],
    visibility = ["//visibility:private"],
)

sdf_scene_object(
    name = "ur3e_scene_object",
    src = "ur3e_world.sdf",
    sdf_assets = ["//intrinsic/robot_definitions/ur:ur3e"],
    updates_pbtxts = [
        "ur.named_configurations.pbtxt",
        "ur3e/ur3e_limits.updates.pbtxt",
    ],
    visibility = [
        "//intrinsic/tools/inctl/util:__pkg__",
        "//intrinsic/world/service:__pkg__",
        "//intrinsic_control:__pkg__",
        "//intrinsic_control/intrinsic/icon/control/services:__pkg__",
        "//intrinsic_control/intrinsic/icon/hardware_modules:__subpackages__",
        "//intrinsic_kinematics/intrinsic/kinematics:__pkg__",
        "//intrinsic_sdk/intrinsic/tools/inctl/util:__pkg__",
    ],
    deps = [
        "@intrinsic_apis//intrinsic/icon/hal/proto/v1:digital_input_output_proto",
    ],
)

filegroup(
    name = "ur5e",
    srcs = [
        "ur5e/ur5e.xacro",
        "ur5e/ur5e_base.xacro",
        ":plugins",
    ],
)

xacro_file(
    name = "ur5e_xacro",
    src = "ur5e/ur5e_world.sdf.xacro",
    out = "ur5e_world.sdf",
    data = [":ur5e"],
    visibility = ["//visibility:private"],
)

sdf_scene_object(
    name = "ur5e_scene_object",
    src = "ur5e_world.sdf",
    sdf_assets = ["//intrinsic/robot_definitions/ur:ur5e"],
    updates_pbtxts = [
        "ur.named_configurations.pbtxt",
        "ur5e/ur5e_limits.updates.pbtxt",
    ],
    visibility = [
        "//intrinsic_control:__pkg__",
        "//intrinsic_control/intrinsic/icon/hardware_modules:__subpackages__",
        "//intrinsic_control/intrinsic/icon/server:__pkg__",
        "//intrinsic_kinematics/intrinsic/kinematics:__pkg__",
    ],
    deps = [
        "@intrinsic_apis//intrinsic/icon/hal/proto/v1:digital_input_output_proto",
    ],
)

filegroup(
    name = "ur10e",
    srcs = [
        "ur10e/ur10e.xacro",
        "ur10e/ur10e_base.xacro",
        ":plugins",
    ],
)

xacro_file(
    name = "ur10e_xacro",
    src = "ur10e/ur10e.sdf.xacro",
    out = "ur10e_world.sdf",
    data = [":ur10e"],
    visibility = ["//visibility:private"],
)

sdf_scene_object(
    name = "ur10e_scene_object",
    src = "ur10e_world.sdf",
    sdf_assets = ["//intrinsic/robot_definitions/ur:ur10e"],
    updates_pbtxts = [
        "ur.named_configurations.pbtxt",
        "ur10e/ur10e_limits.updates.pbtxt",
    ],
    visibility = [
        "//intrinsic_control:__pkg__",
        "//intrinsic_control/intrinsic/icon/hardware_modules:__subpackages__",
        "//intrinsic_kinematics/intrinsic/kinematics:__pkg__",
    ],
    deps = [
        "@intrinsic_apis//intrinsic/icon/hal/proto/v1:digital_input_output_proto",
    ],
)
